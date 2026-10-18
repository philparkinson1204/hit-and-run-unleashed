// Sound effects: small WAV files from WeaponWheel\ mixed through DirectSound 8, so several
// can play at once with their own volume, pitch and pan (each sound has a few voices; a
// busy sound reuses its oldest one). Our sounds go quiet when the game window loses focus,
// like the game's. If DirectSound can't start, winmm's PlaySound plays one sound at a time.
#include "common.h"
#include <mmsystem.h>
#include <dsound.h>

typedef HRESULT(WINAPI* PFN_DSCreate8)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
typedef BOOL(WINAPI* PFN_PlaySoundW)(LPCWSTR, HMODULE, DWORD);

static const int kVoices = 4;
struct Snd {
    BYTE* file;            // whole WAV (PlaySound fallback plays it from memory)
    WAVEFORMATEX fmt;
    const BYTE* pcm;
    DWORD pcmBytes;
    IDirectSoundBuffer* voice[kVoices];
    DWORD started[kVoices];
};
static Snd g_snd[SND_COUNT];
static IDirectSound8* g_ds;
static PFN_PlaySoundW p_PlaySound;
static bool g_loaded, g_dsTried;

static const wchar_t* kFiles[SND_COUNT] = {L"sfx_shot.wav", L"sfx_equip.wav", L"sfx_holster.wav", L"sfx_tick.wav",
                                           L"sfx_cycle.wav", L"sfx_open.wav", L"sfx_close.wav", L"sfx_page.wav",
                                           L"sfx_locked.wav", L"sfx_hit_hard.wav", L"sfx_hit_soft.wav",
                                           L"sfx_rocket.wav", L"sfx_boom1.wav", L"sfx_boom2.wav", L"sfx_boom3.wav",
                                           L"sfx_metal1.wav", L"sfx_metal2.wav", L"sfx_metal3.wav", L"sfx_glass1.wav",
                                           L"sfx_glass2.wav", L"sfx_pop.wav", L"sfx_hiss.wav",
                                           L"sfx_shotgun.wav", L"sfx_pump.wav", L"sfx_shell.wav", L"sfx_bite.wav",
                                           L"sfx_chew.wav", L"sfx_gulp.wav", L"sfx_pat.wav",
                                           L"sfx_pssht.wav", L"sfx_burp.wav", L"sfx_crunch.wav", L"sfx_clank.wav", L"sfx_ahh.wav",
                                           L"sfx_hiccup.wav", L"sfx_whoa.wav", L"sfx_thud.wav", L"sfx_groan.wav",
                                           L"sfx_snore.wav"};

// the game's own sounds (read from the player's soundfx.rcf; null = ours, a WAV in WeaponWheel\)
static const char* kGame[SND_COUNT] = {
    nullptr, nullptr, nullptr, "sound\\soundfx\\frontend\\scroll.rsd", nullptr,
    "sound\\soundfx\\frontend\\pause_on_02.rsd", "sound\\soundfx\\frontend\\pause_off.rsd",
    "sound\\soundfx\\frontend\\back_02.rsd", "sound\\soundfx\\frontend\\locked_out.rsd",
    "sound\\soundfx\\collisions\\concrete_hit_01.rsd", "sound\\soundfx\\collisions\\shrub_hit_01.rsd", nullptr,
    "sound\\soundfx\\collisions\\car_explosion_01b.rsd", "sound\\soundfx\\collisions\\car_explosion_04b.rsd",
    "sound\\soundfx\\collisions\\car_explosion_05b.rsd", "sound\\soundfx\\collisions\\gen_metal_01.rsd",
    "sound\\soundfx\\collisions\\gen_metal_02.rsd", "sound\\soundfx\\collisions\\gen_metal_03.rsd",
    "sound\\soundfx\\collisions\\lrg_glass_09.rsd", "sound\\soundfx\\collisions\\lrg_glass_11.rsd",
    "sound\\soundfx\\collisions\\car_hit_body_01.rsd", "sound\\soundfx\\interactive_props\\gag_gas.rsd",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr};

// finds the fmt and data chunks of a RIFF/WAVE file
static bool ParseWav(Snd& s, DWORD size)
{
    const BYTE* p = s.file;
    if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return false;
    bool haveFmt = false;
    for (DWORD o = 12; o + 8 <= size;) {
        DWORD len = *(const DWORD*)(p + o + 4);
        if (len > size - o - 8) len = size - o - 8;   // (not o + 8 + len: it wraps - a streamed WAV says 0xFFFFFFFF)
        if (!memcmp(p + o, "fmt ", 4) && len >= 16) {
            memset(&s.fmt, 0, sizeof(s.fmt));
            memcpy(&s.fmt, p + o + 8, 16);
            const WAVEFORMATEX& f = s.fmt;
            haveFmt = f.wFormatTag == WAVE_FORMAT_PCM && (f.nChannels == 1 || f.nChannels == 2) &&
                      (f.wBitsPerSample == 8 || f.wBitsPerSample == 16) && f.nSamplesPerSec >= 4000 &&
                      f.nSamplesPerSec <= 192000 && f.nBlockAlign == f.nChannels * f.wBitsPerSample / 8;
        } else if (!memcmp(p + o, "data", 4)) {
            s.pcm = p + o + 8;
            s.pcmBytes = len;
        }
        o += 8 + ((len + 1) & ~1u);
    }
    return haveFmt && s.pcm && s.pcmBytes;
}

void Sound_Init()
{
    if (g_loaded) return;
    g_loaded = true;
    HMODULE w = LoadLibraryA("winmm.dll");
    p_PlaySound = w ? (PFN_PlaySoundW)GetProcAddress(w, "PlaySoundW") : nullptr;
    int n = 0, fromGame = 0;
    // the game's sounds first, straight from its archive
    const char* names[SND_COUNT];
    int ids[SND_COUNT], ng = 0;
    for (int i = 0; i < SND_COUNT; i++)
        if (kGame[i]) names[ng] = kGame[i], ids[ng++] = i;
    BYTE* wavs[SND_COUNT];
    DWORD sizes[SND_COUNT];
    GameFile_Sounds(names, ng, wavs, sizes);
    for (int k = 0; k < ng; k++) {
        Snd& s = g_snd[ids[k]];
        s.file = wavs[k];
        if (s.file && !ParseWav(s, sizes[k])) {
            free(s.file);
            s.file = nullptr;
        }
        if (s.file) n++, fromGame++;
        if (s.file && g_cfg.debug) {   // (for tools: compare with the WAVs assets/build_game_sounds.py makes)
            DWORD sum = 0;
            for (DWORD b = 0; b < s.pcmBytes; b++) sum = sum * 31 + s.pcm[b];
            Log("game sound %s: %lu bytes, hash %08lX", names[k], s.pcmBytes, sum);
        }
    }
    for (int i = 0; i < SND_COUNT; i++) {
        if (g_snd[i].file) continue;
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls%ls", g_modDir, kFiles[i]);
        FILE* f = _wfopen(path, L"rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        Snd& s = g_snd[i];
        s.file = (BYTE*)malloc(size);
        if (fread(s.file, 1, size, f) != (size_t)size || !ParseWav(s, (DWORD)size)) {
            free(s.file);
            s.file = nullptr;
        } else {
            n++;
        }
        fclose(f);
    }
    Log("Sounds: %d of %d loaded (%d from the game's soundfx.rcf)", n, SND_COUNT, fromGame);
}

// DirectSound needs the game window; called with it every frame, starts once
void Sound_SetWindow(HWND hwnd)
{
    if (g_dsTried || !hwnd || !g_loaded) return;
    g_dsTried = true;
    HMODULE dll = LoadLibraryA("dsound.dll");
    PFN_DSCreate8 create = dll ? (PFN_DSCreate8)GetProcAddress(dll, "DirectSoundCreate8") : nullptr;
    if (!create || FAILED(create(nullptr, &g_ds, nullptr)) || !g_ds) {
        g_ds = nullptr;
        Log("DirectSound unavailable - one sound at a time");
        return;
    }
    if (FAILED(g_ds->SetCooperativeLevel(hwnd, DSSCL_NORMAL))) {
        g_ds->Release();
        g_ds = nullptr;
        Log("DirectSound cooperative level failed - one sound at a time");
        return;
    }
    int voices = 0;
    for (int i = 0; i < SND_COUNT; i++) {
        Snd& s = g_snd[i];
        if (!s.file) continue;
        DSBUFFERDESC d = {};
        d.dwSize = sizeof(d);
        d.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GETCURRENTPOSITION2;
        d.dwBufferBytes = s.pcmBytes;
        d.lpwfxFormat = &s.fmt;
        if (FAILED(g_ds->CreateSoundBuffer(&d, &s.voice[0], nullptr))) continue;
        void *a, *b;
        DWORD na, nb;
        if (SUCCEEDED(s.voice[0]->Lock(0, s.pcmBytes, &a, &na, &b, &nb, 0))) {
            memcpy(a, s.pcm, na);
            if (b) memcpy(b, s.pcm + na, nb);
            s.voice[0]->Unlock(a, na, b, nb);
        }
        for (int v = 1; v < kVoices; v++)
            if (FAILED(g_ds->DuplicateSoundBuffer(s.voice[0], &s.voice[v]))) s.voice[v] = nullptr;
        for (int v = 0; v < kVoices; v++) voices += s.voice[v] != nullptr;
    }
    Log("DirectSound: %d voices", voices);
}

void Sound_Play(int id, float vol, float pitch, float pan)
{
    if (id < 0 || id >= SND_COUNT || !g_snd[id].file) return;
    Snd& s = g_snd[id];
    vol *= g_cfg.volume;
    if (g_ds && s.voice[0]) {
        int pick = -1, oldest = 0;
        for (int v = 0; v < kVoices; v++) {
            if (!s.voice[v]) continue;
            DWORD st = 0;
            s.voice[v]->GetStatus(&st);
            if (st & DSBSTATUS_BUFFERLOST) s.voice[v]->Restore();
            if (!(st & DSBSTATUS_PLAYING)) {
                pick = v;
                break;
            }
            if ((LONG)(s.started[v] - s.started[oldest]) < 0) oldest = v;
        }
        if (pick < 0) pick = oldest;
        IDirectSoundBuffer* b = s.voice[pick];
        b->Stop();
        b->SetCurrentPosition(0);
        DWORD freq = (DWORD)(s.fmt.nSamplesPerSec * pitch);
        if (freq < DSBFREQUENCY_MIN) freq = DSBFREQUENCY_MIN;
        if (freq > DSBFREQUENCY_MAX) freq = DSBFREQUENCY_MAX;
        b->SetFrequency(freq);
        LONG db = vol > 0.0001f ? (LONG)(2000.0f * log10f(vol)) : DSBVOLUME_MIN;
        b->SetVolume(db < DSBVOLUME_MIN ? DSBVOLUME_MIN : (db > 0 ? 0 : db));
        b->SetPan((LONG)(fmaxf(-1.0f, fminf(1.0f, pan)) * 2500.0f));
        b->Play(0, 0, 0);
        s.started[pick] = GetTickCount();
        return;
    }
    // fallback: SND_MEMORY | SND_ASYNC | SND_NODEFAULT (no volume or pitch)
    if (p_PlaySound) p_PlaySound((LPCWSTR)s.file, nullptr, 0x0004 | 0x0001 | 0x0002);
}
