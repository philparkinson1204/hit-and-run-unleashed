// Controller input. The mod launcher's XInput hack reads the pad through
// xinput1_4.dll (XInputGetState / XInputGetStateEx = ordinal 100). We hot-patch both
// so that, while the wheel is open, the game sees a completely idle controller:
// no buttons, no triggers, no sticks. The wheel reads the untouched state through
// the original functions.
#include "common.h"
#include <xinput.h>

typedef DWORD(WINAPI* PFN_GetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_SetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* PFN_GetCaps)(DWORD, DWORD, XINPUT_CAPABILITIES*);

static PFN_GetState o_GetState, o_GetStateEx;
static PFN_SetState p_SetState;
static PFN_GetCaps o_GetCaps;
static volatile LONG g_block;
static volatile LONG g_gunBlock;
static volatile LONG g_gunButtons;
static volatile LONG g_pad = -1;   // user index the wheel is using
static volatile LONG g_calls, g_filtered;

// Debug test hook: a pretend controller merged into what both the game and the mod read, so
// scripts can walk, press buttons and fire without anyone holding a pad. Off unless a debug
// command switches it on.
static struct {
    volatile LONG on;
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
} g_fake;

static LONG FakeSlot() { LONG p = g_pad; return p >= 0 ? p : 0; }
static volatile LONG g_slotCalls[4], g_slotOk[4], g_capsCalls[4];   // game reads per controller slot (debug)

void Input_DebugInfo(char* out, int n)
{
    LONG c[4], ok[4], caps[4];
    for (int i = 0; i < 4; i++) {
        c[i] = InterlockedExchange(&g_slotCalls[i], 0);
        ok[i] = InterlockedExchange(&g_slotOk[i], 0);
        caps[i] = InterlockedExchange(&g_capsCalls[i], 0);
    }
    snprintf(out, n, "pad slot %ld, fake %s; game reads since last: slot0 %ld (%ld ok) slot1 %ld (%ld ok) slot2 %ld slot3 %ld;"
             " capability queries %ld %ld %ld %ld", g_pad, g_fake.on ? "on" : "off", c[0], ok[0], c[1], ok[1], c[2], c[3],
             caps[0], caps[1], caps[2], caps[3]);
}

static DWORD MergeFake(DWORD r, XINPUT_STATE* s)
{
    if (r != ERROR_SUCCESS) {   // no real pad: pretend one is connected
        memset(s, 0, sizeof(*s));
        r = ERROR_SUCCESS;
    }
    XINPUT_GAMEPAD& g = s->Gamepad;
    g.wButtons |= g_fake.buttons;
    if (g_fake.lt > g.bLeftTrigger) g.bLeftTrigger = g_fake.lt;
    if (g_fake.rt > g.bRightTrigger) g.bRightTrigger = g_fake.rt;
    if (g_fake.lx || g_fake.ly) {
        g.sThumbLX = g_fake.lx;
        g.sThumbLY = g_fake.ly;
    }
    if (g_fake.rx || g_fake.ry) {
        g.sThumbRX = g_fake.rx;
        g.sThumbRY = g_fake.ry;
    }
    return r;
}

void Input_SetFake(bool on, WORD buttons, BYTE lt, BYTE rt, float lx, float ly, float rx, float ry)
{
    auto S = [](float v) { return (SHORT)(fmaxf(-1.0f, fminf(1.0f, v)) * 32767.0f); };
    g_fake.buttons = on ? buttons : 0;
    g_fake.lt = on ? lt : 0;
    g_fake.rt = on ? rt : 0;
    g_fake.lx = on ? S(lx) : 0;
    g_fake.ly = on ? S(ly) : 0;
    g_fake.rx = on ? S(rx) : 0;
    g_fake.ry = on ? S(ry) : 0;
    InterlockedExchange(&g_fake.on, on ? 1 : 0);
}

// The game only re-reads the pad when the packet number changes, so every change in
// what we hand it (not just in the real pad) gets a new number.
static DWORD g_rawPacket, g_outPacket = 0x40000000u;
static XINPUT_GAMEPAD g_lastOut;

static void Renumber(XINPUT_STATE* s)
{
    if (s->dwPacketNumber != g_rawPacket || memcmp(&s->Gamepad, &g_lastOut, sizeof(g_lastOut)) != 0) {
        g_rawPacket = s->dwPacketNumber;
        g_lastOut = s->Gamepad;
        g_outPacket = (g_outPacket + 1) | 0x40000000u;
    }
    s->dwPacketNumber = g_outPacket;
}

// Drunk steering (drunk mode, in his car - gun.cpp sets the level): the left stick's X as the game reads it comes through late (up
// to 0.25 s), through an under-damped spring (it lags, then overshoots: you correct, and over-correct) and with a slow wander that
// grows with the car's speed. Menus never see it (only set in play, in his car); the right stick and buttons are left alone.
static volatile LONG g_drunkSteer;    // level x 1000 (0: off)
static volatile LONG g_drunkSpeed;    // the car's speed, cm/s
static struct { double t0, last; float h[96][2]; int hn; float y, yv; } g_ds;
void Input_DrunkSteer(float level, float speed)
{
    InterlockedExchange(&g_drunkSteer, (LONG)(fmaxf(level, 0.0f) * 1000.0f));
    InterlockedExchange(&g_drunkSpeed, (LONG)(fmaxf(speed, 0.0f) * 100.0f));
}
static double DsNow()
{
    static LARGE_INTEGER f = {};
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static void DrunkSteer(XINPUT_STATE* s)
{
    float L = g_drunkSteer / 1000.0f, spd = g_drunkSpeed / 100.0f;
    double now = DsNow();
    if (g_ds.t0 == 0.0) g_ds.t0 = now, g_ds.last = now;
    float dt = (float)(now - g_ds.last);
    g_ds.last = now;
    if (dt > 0.1f) dt = 0.1f;
    float u = s->Gamepad.sThumbLX / 32767.0f, t = (float)(now - g_ds.t0);
    g_ds.h[g_ds.hn % 96][0] = t, g_ds.h[g_ds.hn % 96][1] = u;
    g_ds.hn++;
    float delay = 0.25f * fminf(L, 1.2f), ud = u;
    for (int i = 1; i < 96 && i <= g_ds.hn; i++) {
        ud = g_ds.h[(g_ds.hn - i) % 96][1];
        if (t - g_ds.h[(g_ds.hn - i) % 96][0] >= delay) break;
    }
    float Lc = fminf(L, 1.0f), w = 25.0f - 19.0f * Lc, z = 1.0f - 0.65f * Lc;
    for (float left = dt; left > 1e-5f; left -= 0.01f) {   // (steps of 10 ms)
        float h = fminf(left, 0.01f);
        g_ds.yv += (w * w * (ud - g_ds.y) - 2.0f * z * w * g_ds.yv) * h;
        g_ds.y += g_ds.yv * h;
    }
    float A = 0.22f * fminf(L, 1.3f) * fminf(spd / 12.0f, 1.0f);
    float wander = A * (sinf(6.2831853f * 0.13f * t + 1.0f) + 0.6f * sinf(6.2831853f * 0.31f * t + 2.0f));
    float out = g_ds.y + wander;
    out = out < -1.0f ? -1.0f : out > 1.0f ? 1.0f : out;
    s->Gamepad.sThumbLX = (SHORT)(out * 32767.0f);
}

static void Filter(XINPUT_STATE* s)
{
    if (g_block) {
        memset(&s->Gamepad, 0, sizeof(s->Gamepad));
        Renumber(s);
        return;
    }
    LONG f = g_gunBlock;
    s->Gamepad.wButtons &= (WORD)~g_gunButtons;
    if (f & GUNBLOCK_TRIGGERS) {
        s->Gamepad.bLeftTrigger = 0;
        s->Gamepad.bRightTrigger = 0;
    }
    if (f & GUNBLOCK_MOVE) {
        s->Gamepad.sThumbLX = 0;
        s->Gamepad.sThumbLY = 0;
    }
    if (f & GUNBLOCK_LOOKY) s->Gamepad.sThumbRY = 0;
    if (f & GUNBLOCK_LOOKX) s->Gamepad.sThumbRX = 0;
    if (g_drunkSteer > 0) DrunkSteer(s);
    Renumber(s);
}

static DWORD Hooked(PFN_GetState real, DWORD i, XINPUT_STATE* s)
{
    DWORD r = real(i, s);
    InterlockedIncrement(&g_calls);
    bool fake = g_fake.on && s && (LONG)i == FakeSlot();
    if (fake) r = MergeFake(r, s);
    if (i < 4) {
        InterlockedIncrement(&g_slotCalls[i]);
        if (r == ERROR_SUCCESS) InterlockedIncrement(&g_slotOk[i]);
    }
    if (r == ERROR_SUCCESS && s && (fake || g_block || g_gunBlock || g_gunButtons || g_drunkSteer) && (LONG)i == g_pad) {
        Filter(s);
        InterlockedIncrement(&g_filtered);
    } else if (fake) {
        Renumber(s);
    }
    return r;
}

static DWORD WINAPI h_GetState(DWORD i, XINPUT_STATE* s) { return Hooked(o_GetState, i, s); }
static DWORD WINAPI h_GetStateEx(DWORD i, XINPUT_STATE* s) { return Hooked(o_GetStateEx, i, s); }

// The launcher only starts reading a slot once XInputGetCapabilities says a pad is there.
static DWORD WINAPI h_GetCaps(DWORD i, DWORD flags, XINPUT_CAPABILITIES* c)
{
    DWORD r = o_GetCaps(i, flags, c);
    if (i < 4) InterlockedIncrement(&g_capsCalls[i]);
    if (r != ERROR_SUCCESS && c && g_fake.on && (LONG)i == FakeSlot()) {
        memset(c, 0, sizeof(*c));
        c->Type = XINPUT_DEVTYPE_GAMEPAD;
        c->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        c->Gamepad.wButtons = 0xF3FF;
        c->Gamepad.bLeftTrigger = c->Gamepad.bRightTrigger = 0xFF;
        c->Gamepad.sThumbLX = c->Gamepad.sThumbLY = c->Gamepad.sThumbRX = c->Gamepad.sThumbRY = (SHORT)0xFFC0;
        c->Vibration.wLeftMotorSpeed = c->Vibration.wRightMotorSpeed = 0xFF;
        r = ERROR_SUCCESS;
    }
    return r;
}

void Input_SetGunBlock(int flags, WORD buttons)
{
    InterlockedExchange(&g_gunBlock, flags);
    InterlockedExchange(&g_gunButtons, buttons);
}

void Input_Stats(long* calls, long* filtered)
{
    *calls = InterlockedExchange(&g_calls, 0);
    *filtered = InterlockedExchange(&g_filtered, 0);
}

void Input_Install()
{
    HMODULE x = LoadLibraryA("xinput1_4.dll");
    if (!x) {
        Log("xinput1_4.dll not available");
        return;
    }
    void* gs = (void*)GetProcAddress(x, "XInputGetState");
    void* gsx = (void*)GetProcAddress(x, (LPCSTR)100);
    bool a = HotPatch(gs, (void*)h_GetState, (void**)&o_GetState);
    bool b = gsx && HotPatch(gsx, (void*)h_GetStateEx, (void**)&o_GetStateEx);
    if (!a) o_GetState = (PFN_GetState)gs;
    if (!b) o_GetStateEx = (PFN_GetState)gsx;
    p_SetState = (PFN_SetState)GetProcAddress(x, "XInputSetState");
    if (g_cfg.debug) {   // only the test hook's pretend controller needs it
        void* gc = (void*)GetProcAddress(x, "XInputGetCapabilities");
        if (!gc || !HotPatch(gc, (void*)h_GetCaps, (void**)&o_GetCaps)) {
            o_GetCaps = (PFN_GetCaps)gc;
            Log("XInputGetCapabilities hook FAILED (the test hook's pretend pad may be ignored)");
        }
    }
    Log("XInput hooks: GetState=%s GetStateEx=%s rumble=%s", a ? "ok" : "FAILED", b ? "ok" : "FAILED",
        p_SetState ? "ok" : "missing");
}

bool Input_Poll(Pad* out)
{
    memset(out, 0, sizeof(*out));
    if (!o_GetState) return false;
    // asking empty controller slots is slow, so with no pad look again only once a second
    static double s_nextScan;
    if (g_pad < 0 && Time_Real() < s_nextScan && !g_fake.on) return false;
    XINPUT_STATE s;
    LONG first = g_pad >= 0 ? g_pad : 0;
    for (int n = 0; n < 4; n++) {
        LONG i = (first + n) % 4;
        DWORD r = o_GetState((DWORD)i, &s);
        if (g_fake.on && i == FakeSlot()) r = MergeFake(r, &s);
        if (r != ERROR_SUCCESS) continue;
        g_pad = i;
        out->connected = true;
        out->buttons = s.Gamepad.wButtons;
        out->lx = s.Gamepad.sThumbLX / 32767.0f;
        out->ly = s.Gamepad.sThumbLY / 32767.0f;
        out->rx = s.Gamepad.sThumbRX / 32767.0f;
        out->ry = s.Gamepad.sThumbRY / 32767.0f;
        out->lt = s.Gamepad.bLeftTrigger;
        out->rt = s.Gamepad.bRightTrigger;
        return true;
    }
    InterlockedExchange(&g_pad, -1);
    s_nextScan = Time_Real() + 1.0;
    return false;
}

// Rumble: both motors fade out linearly; the pad is only told when a level really changes.
static float g_rumLo, g_rumHi, g_rumLoFade, g_rumHiFade;
static int g_sentLo = -1, g_sentHi = -1;

void Input_Rumble(float lo, float hi, float seconds)
{
    if (seconds < 0.01f) seconds = 0.01f;
    if (lo > g_rumLo) g_rumLo = lo;
    if (hi > g_rumHi) g_rumHi = hi;
    g_rumLoFade = g_rumLo / seconds;
    g_rumHiFade = g_rumHi / seconds;
}

void Input_RumbleUpdate(float dt, bool allowed)
{
    if (!allowed) g_rumLo = g_rumHi = 0;
    g_rumLo = g_rumLo - g_rumLoFade * dt;
    g_rumHi = g_rumHi - g_rumHiFade * dt;
    if (g_rumLo < 0) g_rumLo = 0;
    if (g_rumHi < 0) g_rumHi = 0;
    LONG pad = g_pad;
    if (!p_SetState || pad < 0) return;
    int lo = (int)(fminf(g_rumLo, 1.0f) * 65535.0f), hi = (int)(fminf(g_rumHi, 1.0f) * 65535.0f);   // over 1 wrapped round
    bool changed = (lo == 0) != (g_sentLo == 0) || (hi == 0) != (g_sentHi == 0) || abs(lo - g_sentLo) > 2048 ||
                   abs(hi - g_sentHi) > 2048;
    if (!changed) return;
    XINPUT_VIBRATION v = {(WORD)lo, (WORD)hi};
    p_SetState((DWORD)pad, &v);
    g_sentLo = lo;
    g_sentHi = hi;
}

void Input_SetBlock(bool block)
{
    InterlockedExchange(&g_block, block ? 1 : 0);
}
