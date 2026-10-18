// Keyboard and mouse. The game reads both through DirectInput 8 (created through COM, so no
// dinput8.dll proxy: see HOW_IT_WORKS.md). All its devices share one vtable inside dinput8.dll; we
// create a mouse of our own once, only to find that vtable, and patch three slots:
//   SetDataFormat  (11): learn where the game's format puts each mouse axis and button
//   GetDeviceState  (9): hide what we hide from GetDeviceData (it seeds the starting values)
//   GetDeviceData  (10): the game's buffered mouse records. We add up the movement and the scroll
//                        wheel (raw counts, no Windows pointer acceleration: exactly what the game
//                        sees), hide movement and buttons from the game while they are ours (the
//                        wheel, aiming, the gun's buttons), and slip in records of our own (the
//                        action key, debug test moves).
// Keys and mouse buttons are read with GetAsyncKeyState while the game window has focus. Letters
// and digits are found by where they sit on the keyboard, as the game finds its own keys.
#include "common.h"
#include <stdarg.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

// (local copies: no dxguid.lib)
static const GUID kIID_DI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kXAxis = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kYAxis = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kZAxis = {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kButton = {0xA36D02F0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

typedef HRESULT(WINAPI* PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT(WINAPI* PFN_GetState)(void*, DWORD, void*);
typedef HRESULT(WINAPI* PFN_GetData)(void*, DWORD, DIDEVICEOBJECTDATA*, DWORD*, DWORD);
typedef HRESULT(WINAPI* PFN_SetFormat)(void*, const DIDATAFORMAT*);
typedef HRESULT(WINAPI* PFN_GetCaps)(void*, DIDEVCAPS*);
typedef HRESULT(WINAPI* PFN_EnumObjects)(void*, LPDIENUMDEVICEOBJECTSCALLBACKA, void*, DWORD);
typedef HRESULT(WINAPI* PFN_SetCoop)(void*, HWND, DWORD);

static PFN_GetState o_GetState;
static PFN_GetData o_GetData;
static PFN_SetFormat o_SetFormat;
static PFN_SetCoop o_SetCoop;

// ------------------------------------------------------------------ the game's mouse devices
enum Role { R_NONE, R_X, R_Y, R_Z, R_BTN0 };   // R_BTN0 + n = button n
struct Dev {
    void* p;
    int kind;                 // 0 unknown, 1 mouse, 2 other, 3 keyboard
    bool formatKnown;         // from SetDataFormat (else guessed: 4 bytes per object, enumeration order)
    int n;
    DWORD ofs[24];
    int role[24];
};
static Dev g_devs[32];   // (never freed: the game makes its devices once, but leave room)
static CRITICAL_SECTION g_cs;
static bool g_installed;

static int RoleOf(const GUID* g, DWORD type, int* buttons)
{
    if (g) {
        if (IsEqualGUID(*g, kXAxis)) return R_X;
        if (IsEqualGUID(*g, kYAxis)) return R_Y;
        if (IsEqualGUID(*g, kZAxis)) return R_Z;
        if (IsEqualGUID(*g, kButton)) return R_BTN0 + (*buttons)++;
        return R_NONE;
    }
    int inst = DIDFT_GETINSTANCE(type);
    if (type & DIDFT_AXIS) return inst == 0 ? R_X : inst == 1 ? R_Y : inst == 2 ? R_Z : R_NONE;
    if (type & DIDFT_BUTTON) {   // in the order listed (Wine numbers a mouse's buttons on from its axes)
        int b = (*buttons)++;
        return b < 8 ? R_BTN0 + b : R_NONE;
    }
    return R_NONE;
}

static int DevKind(void* dev)
{
    DIDEVCAPS caps = {};
    caps.dwSize = sizeof(caps);
    PFN_GetCaps gc = (PFN_GetCaps)(*(void***)dev)[3];
    if (FAILED(gc(dev, &caps))) return 2;
    int t = GET_DIDEVICE_TYPE(caps.dwDevType);
    return t == DI8DEVTYPE_MOUSE ? 1 : t == DI8DEVTYPE_KEYBOARD ? 3 : 2;
}

struct EnumCtx { Dev* d; int buttons; };
static BOOL CALLBACK EnumCb(LPCDIDEVICEOBJECTINSTANCEA o, LPVOID ctx)
{
    EnumCtx* c = (EnumCtx*)ctx;
    if (c->d->n >= 24) return DIENUM_STOP;
    int i = c->d->n++;
    c->d->ofs[i] = (DWORD)i * 4;
    c->d->role[i] = RoleOf(&o->guidType, o->dwType, &c->buttons);
    return DIENUM_CONTINUE;
}

static void LogDev(const Dev* d, const char* how)
{
    char buf[256] = "";
    for (int i = 0; i < d->n && strlen(buf) < 220; i++) {
        static const char* names[] = {"-", "X", "Y", "Z"};
        char one[16];
        if (d->role[i] >= R_BTN0) snprintf(one, sizeof(one), " %lu:B%d", d->ofs[i], d->role[i] - R_BTN0);
        else snprintf(one, sizeof(one), " %lu:%s", d->ofs[i], names[d->role[i]]);
        strcat(buf, one);
    }
    Log("Keyboard/mouse: the game's mouse %p, format %s:%s", d->p, how, buf);
}

static Dev* FindDev(void* p, bool create)
{
    Dev* freeSlot = nullptr;
    for (auto& d : g_devs) {
        if (d.p == p) return &d;
        if (!d.p && !freeSlot) freeSlot = &d;
    }
    if (!create || !freeSlot) return nullptr;
    freeSlot->p = p;
    freeSlot->kind = DevKind(p);
    freeSlot->n = 0;
    freeSlot->formatKnown = false;
    return freeSlot;
}

// a mouse seen first in GetDeviceData (its SetDataFormat came before our hook): guess the layout
static Dev* MouseDev(void* p)
{
    Dev* d = FindDev(p, true);
    if (!d || d->kind != 1) return nullptr;
    if (!d->n && !d->formatKnown) {
        EnumCtx c = {d, 0};
        PFN_EnumObjects eo = (PFN_EnumObjects)(*(void***)p)[4];
        eo(p, EnumCb, &c, DIDFT_ALL);
        d->formatKnown = true;   // (don't enumerate again)
        LogDev(d, "guessed from its objects (SetDataFormat came before the hook)");
    }
    return d;
}

static int DevRole(const Dev* d, DWORD ofs)
{
    for (int i = 0; i < d->n; i++)
        if (d->ofs[i] == ofs) return d->role[i];
    return R_NONE;
}

static bool DevOfs(const Dev* d, int role, DWORD* ofs)
{
    for (int i = 0; i < d->n; i++)
        if (d->role[i] == role) {
            *ofs = d->ofs[i];
            return true;
        }
    return false;
}

// ------------------------------------------------------------------ shared with the hooks
static volatile LONG g_accX, g_accY, g_accZ;   // raw counts since the last frame
static volatile LONG g_hideMove, g_hideZ, g_hideButtons;   // what the game doesn't get (buttons: bit n)
static volatile LONG g_recs;                   // records seen (debug)

struct Inject { int role; LONG data; bool tap; };
static Inject g_q[64];
static int g_qHead, g_qTail;

// the game's keyboard, for the debug "gkey" only (its format puts each key at an offset of its own)
static void* g_kbDev;
static DWORD g_kbOfs[256];   // by DirectInput key code (DIK_*); ~0: not in the game's format
static struct { DWORD ofs, data; } g_kq[32];
static int g_kqHead, g_kqTail;

static void PushGameKey(int dik, bool down)
{
    EnterCriticalSection(&g_cs);
    int next = (g_kqTail + 1) % 32;
    if (next != g_kqHead) {
        g_kq[g_kqTail].ofs = g_kbOfs[dik];
        g_kq[g_kqTail].data = down ? 0x80 : 0;
        g_kqTail = next;
    }
    LeaveCriticalSection(&g_cs);
}

static void Push(int role, LONG data, bool tap)
{
    EnterCriticalSection(&g_cs);
    int next = (g_qTail + 1) % 64;
    if (next != g_qHead) {
        g_q[g_qTail] = {role, data, tap};
        g_qTail = next;
    }
    LeaveCriticalSection(&g_cs);
}

static void Tap(int role, DWORD data)
{
    if (role == R_X) InterlockedExchangeAdd(&g_accX, (LONG)data);
    else if (role == R_Y) InterlockedExchangeAdd(&g_accY, (LONG)data);
    else if (role == R_Z) InterlockedExchangeAdd(&g_accZ, (LONG)data);
}

static void Hide(int role, DWORD* data)
{
    if ((role == R_X || role == R_Y) && g_hideMove) *data = 0;
    else if (role == R_Z && g_hideZ) *data = 0;
    else if (role >= R_BTN0 && (g_hideButtons & (1 << (role - R_BTN0)))) *data = 0;
}

static HRESULT WINAPI h_GetData(void* self, DWORD cb, DIDEVICEOBJECTDATA* rg, DWORD* inout, DWORD flags)
{
    DWORD want = inout ? *inout : 0;
    HRESULT hr = o_GetData(self, cb, rg, inout, flags);
    if (!rg || !inout || cb < sizeof(DWORD) * 2 || FAILED(hr)) return hr;
    EnterCriticalSection(&g_cs);
    bool peek = (flags & DIGDD_PEEK) != 0;
    if (self == g_kbDev && *inout == 0 && want > 0 && g_kqHead != g_kqTail) {   // debug "gkey"
        memset(rg, 0, cb);
        rg->dwOfs = g_kq[g_kqHead].ofs;
        rg->dwData = g_kq[g_kqHead].data;
        rg->dwTimeStamp = GetTickCount();
        if (!peek) g_kqHead = (g_kqHead + 1) % 32;
        *inout = 1;
        LeaveCriticalSection(&g_cs);
        return DI_OK;
    }
    Dev* d = MouseDev(self);
    if (d) {
        for (DWORD i = 0; i < *inout; i++) {
            DIDEVICEOBJECTDATA* r = (DIDEVICEOBJECTDATA*)((BYTE*)rg + i * cb);
            int role = DevRole(d, r->dwOfs);
            if (!peek) {
                Tap(role, r->dwData);
                InterlockedIncrement(&g_recs);
            }
            Hide(role, &r->dwData);
        }
        // nothing real waiting: hand over one of ours
        DWORD ofs;
        if (*inout == 0 && want > 0 && g_qHead != g_qTail && DevOfs(d, g_q[g_qHead].role, &ofs)) {
            Inject& q = g_q[g_qHead];
            DIDEVICEOBJECTDATA* r = rg;
            memset(r, 0, cb);
            r->dwOfs = ofs;
            r->dwData = (DWORD)q.data;
            r->dwTimeStamp = GetTickCount();
            if (!peek) {
                if (q.tap) Tap(q.role, r->dwData);
                g_qHead = (g_qHead + 1) % 64;
            }
            if (q.tap) Hide(q.role, &r->dwData);   // (a test move is treated as a real one)
            *inout = 1;
            hr = DI_OK;
        }
    }
    LeaveCriticalSection(&g_cs);
    return hr;
}

static HRESULT WINAPI h_GetState(void* self, DWORD cb, void* data)
{
    HRESULT hr = o_GetState(self, cb, data);
    if (FAILED(hr) || !data) return hr;
    EnterCriticalSection(&g_cs);
    Dev* d = MouseDev(self);
    if (d)
        for (int i = 0; i < d->n; i++)
            if (d->ofs[i] + 4 <= cb) Hide(d->role[i], (DWORD*)((BYTE*)data + d->ofs[i]));
    LeaveCriticalSection(&g_cs);
    return hr;
}

static HRESULT WINAPI h_SetFormat(void* self, const DIDATAFORMAT* f)
{
    HRESULT hr = o_SetFormat(self, f);
    if (SUCCEEDED(hr) && f && f->rgodf) {
        EnterCriticalSection(&g_cs);
        Dev* d = FindDev(self, true);
        if (d && d->kind == 1) {
            d->n = 0;
            int buttons = 0;
            for (DWORD i = 0; i < f->dwNumObjs && d->n < 24; i++) {
                const DIOBJECTDATAFORMAT& o = f->rgodf[i];
                d->ofs[d->n] = o.dwOfs;
                d->role[d->n] = RoleOf(o.pguid, o.dwType, &buttons);
                d->n++;
            }
            d->formatKnown = true;
            LogDev(d, "from SetDataFormat");
        } else if (d && d->kind == 3) {
            g_kbDev = self;
            memset(g_kbOfs, 0xFF, sizeof(g_kbOfs));
            int keys = 0;
            for (DWORD i = 0; i < f->dwNumObjs; i++) {
                DWORD inst = DIDFT_GETINSTANCE(f->rgodf[i].dwType);
                if ((f->rgodf[i].dwType & DIDFT_BUTTON) && inst < 256 && g_kbOfs[inst] == ~0ul) {
                    g_kbOfs[inst] = f->rgodf[i].dwOfs;
                    keys++;
                }
            }
            Log("Keyboard/mouse: the game's keyboard %p, %d keys in its format", self, keys);
        }
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

// (only logged: how the game holds its mouse and keyboard, for reports from other machines)
static HRESULT WINAPI h_SetCoop(void* self, HWND wnd, DWORD flags)
{
    HRESULT hr = o_SetCoop(self, wnd, flags);
    int kind = DevKind(self);
    if (kind != 2)
        Log("Keyboard/mouse: the game's %s is %s, %s%s (0x%lX) -> 0x%08lX", kind == 1 ? "mouse" : "keyboard",
            flags & DISCL_EXCLUSIVE ? "exclusive" : "non-exclusive", flags & DISCL_BACKGROUND ? "background" : "foreground",
            flags & DISCL_NOWINKEY ? ", no Windows key" : "", flags, hr);
    return hr;
}

void Kbm_Install()
{
    if (!g_cfg.kbm) {
        Log("Keyboard/mouse: off (Keyboard=0)");
        return;
    }
    InitializeCriticalSection(&g_cs);
    HMODULE di = LoadLibraryA("dinput8.dll");
    PFN_DI8Create create = di ? (PFN_DI8Create)GetProcAddress(di, "DirectInput8Create") : nullptr;
    void* dinput = nullptr;
    if (!create || FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, kIID_DI8A, &dinput, nullptr)) || !dinput) {
        Log("Keyboard/mouse: DirectInput not available - controller only");
        return;
    }
    typedef HRESULT(WINAPI * PFN_CreateDev)(void*, REFGUID, void**, LPUNKNOWN);
    typedef ULONG(WINAPI * PFN_Release)(void*);
    void* dev = nullptr;
    PFN_CreateDev cd = (PFN_CreateDev)(*(void***)dinput)[3];
    if (SUCCEEDED(cd(dinput, kSysMouse, &dev, nullptr)) && dev) {
        void* a = PatchVTable(dev, 9, (void*)h_GetState);
        void* b = PatchVTable(dev, 10, (void*)h_GetData);
        void* c = PatchVTable(dev, 11, (void*)h_SetFormat);
        void* e = PatchVTable(dev, 13, (void*)h_SetCoop);
        if (a != (void*)h_GetState) o_GetState = (PFN_GetState)a;
        if (b != (void*)h_GetData) o_GetData = (PFN_GetData)b;
        if (c != (void*)h_SetFormat) o_SetFormat = (PFN_SetFormat)c;
        if (e != (void*)h_SetCoop) o_SetCoop = (PFN_SetCoop)e;
        g_installed = o_GetState && o_GetData && o_SetFormat;
        ((PFN_Release)(*(void***)dev)[2])(dev);   // (the patched vtable stays: it is the DLL's)
    }
    ((PFN_Release)(*(void***)dinput)[2])(dinput);
    Log("Keyboard/mouse: DirectInput mouse hooks %s", g_installed ? "ok" : "FAILED (keys still work, no mouse aim)");
}

// ------------------------------------------------------------------ binds
// A bind is up to two codes:
//   0x01-0xFF     a virtual key that is the same on every keyboard layout (TAB, SPACE, F1...)
//   0x101-0x105   MOUSE1-5
//   0x200 + scan  a letter, a digit or GRAVE, named after the key in that place on a US keyboard,
//                 the way the game names its own keys (DirectInput scan codes). On a French AZERTY
//                 keyboard "Q" is the key marked A: the one marked Q moves the player left there.
//                 The hints show what the player's own keyboard has printed on it.
enum { KC_MOUSE = 0x100, KC_POS = 0x200, KC_COUNT = 0x300 };
static const struct { char name; BYTE scan; } kPositional[] = {
    {'1', 0x02}, {'2', 0x03}, {'3', 0x04}, {'4', 0x05}, {'5', 0x06}, {'6', 0x07}, {'7', 0x08}, {'8', 0x09},
    {'9', 0x0A}, {'0', 0x0B}, {'Q', 0x10}, {'W', 0x11}, {'E', 0x12}, {'R', 0x13}, {'T', 0x14}, {'Y', 0x15},
    {'U', 0x16}, {'I', 0x17}, {'O', 0x18}, {'P', 0x19}, {'A', 0x1E}, {'S', 0x1F}, {'D', 0x20}, {'F', 0x21},
    {'G', 0x22}, {'H', 0x23}, {'J', 0x24}, {'K', 0x25}, {'L', 0x26}, {'Z', 0x2C}, {'X', 0x2D}, {'C', 0x2E},
    {'V', 0x2F}, {'B', 0x30}, {'N', 0x31}, {'M', 0x32}, {'`', 0x29}};
static const struct { const char* name; int code; const char* label; } kKeys[] = {
    {"MOUSE1", 0x101, "LMB"}, {"MOUSE2", 0x102, "RMB"}, {"MOUSE3", 0x103, "MMB"}, {"MOUSE4", 0x104, "MOUSE4"},
    {"MOUSE5", 0x105, "MOUSE5"}, {"LMB", 0x101, "LMB"}, {"RMB", 0x102, "RMB"}, {"MMB", 0x103, "MMB"},
    {"TAB", VK_TAB, "TAB"}, {"SPACE", VK_SPACE, "SPACE"}, {"SHIFT", VK_SHIFT, "SHIFT"}, {"LSHIFT", VK_LSHIFT, "SHIFT"},
    {"CTRL", VK_CONTROL, "CTRL"}, {"LCTRL", VK_LCONTROL, "CTRL"}, {"ALT", VK_MENU, "ALT"}, {"CAPSLOCK", VK_CAPITAL, "CAPS"},
    {"ENTER", VK_RETURN, "ENTER"}, {"BACKSPACE", VK_BACK, "BKSP"}, {"UP", VK_UP, "UP"},
    {"DOWN", VK_DOWN, "DOWN"}, {"LEFT", VK_LEFT, "LEFT"}, {"RIGHT", VK_RIGHT, "RIGHT"}};

// per scan code, for the keyboard layout in use: the virtual key there and the label printed on it
static BYTE g_posVk[0x80];
static char g_posLabel[0x80][2];
static HKL g_layout;

static void ReadLayout()
{
    g_layout = GetKeyboardLayout(0);
    for (auto& p : kPositional) {
        UINT vk = MapVirtualKeyW(p.scan, MAPVK_VSC_TO_VK);
        char ch = 0;
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
            ch = (char)vk;
        else if (vk) {
            UINT u = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFF;
            if (u > ' ' && u < 0x7F) ch = (char)toupper((int)u);   // (the game's fonts have plain ASCII)
        }
        g_posVk[p.scan] = (BYTE)(vk ? vk : p.name == '`' ? VK_OEM_3 : p.name);
        g_posLabel[p.scan][0] = ch ? ch : p.name;
    }
}

static int KeyCode(const char* name, const char** label)
{
    for (auto& k : kKeys)
        if (!_stricmp(name, k.name)) {
            if (label) *label = k.label;
            return k.code;
        }
    char c = (char)toupper((unsigned char)name[0]);
    if (!_stricmp(name, "GRAVE")) c = '`';
    else if (!c || name[1]) c = 0;
    for (auto& p : kPositional)
        if (c && p.name == c) {
            if (!g_layout) ReadLayout();
            if (label) *label = g_posLabel[p.scan];
            return KC_POS + p.scan;
        }
    if ((name[0] == 'F' || name[0] == 'f') && atoi(name + 1) >= 1 && atoi(name + 1) <= 12) {
        static char fn[12][4];
        int n = atoi(name + 1);
        snprintf(fn[n - 1], 4, "F%d", n);
        if (label) *label = fn[n - 1];
        return VK_F1 + n - 1;
    }
    return 0;
}

// what parsing the binds found, for the log (the ini is read before the log is open)
static char g_bindNotes[600];

static void Note(const char* fmt, ...)
{
    size_t n = strlen(g_bindNotes);
    if (n + 2 >= sizeof(g_bindNotes)) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_bindNotes + n, sizeof(g_bindNotes) - n, fmt, ap);
    va_end(ap);
}

void Kbm_ParseBind(const char* job, const char* text, Bind* b, const char* def)
{
    for (int pass = 0; pass < 2; pass++) {
        const char* src = pass ? def : text;
        b->code[0] = b->code[1] = 0;
        b->label = nullptr;
        char buf[64];
        strncpy(buf, src, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        buf[strcspn(buf, ";#")] = 0;   // ("KeyFire=MOUSE1 ; fire" - the profile API keeps a comment after a value)
        int n = 0;
        bool none = false;
        for (char* t = strtok(buf, " ,+|\t"); t; t = strtok(nullptr, " ,+|\t")) {
            if (!_stricmp(t, "NONE")) {
                none = true;
                continue;
            }
            const char* label = nullptr;
            int c = KeyCode(t, &label);
            if (!c) {
                if (!pass) Note("|%s: \"%s\" is not a key name", job, t);
                continue;
            }
            if (n == 2) {
                if (!pass) Note("|%s: only two keys per job (\"%s\" left out)", job, t);
                continue;
            }
            if (!b->label) b->label = label;
            b->code[n++] = c;
        }
        if (n || none) return;
        if (!pass && text[strspn(text, " \t")]) Note("|%s: nothing usable in \"%s\", so the default %s", job, text, def);
    }
}

void Kbm_LogBinds()
{
    if (!g_cfg.kbm) return;
    const struct { const char* job; const Bind* b; } all[] = {
        {"wheel", &g_cfg.keyWheel}, {"aim", &g_cfg.keyAim}, {"fire", &g_cfg.keyFire}, {"fists", &g_cfg.keyFists},
        {"handgun", &g_cfg.keyHandgun}, {"RPG", &g_cfg.keyRPG}, {"shotgun", &g_cfg.keyShotgun}, {"donut", &g_cfg.keyDonut}, {"beer", &g_cfg.keyBeer}, {"last weapon", &g_cfg.keyLast},
        {"switch page", &g_cfg.keyPage}, {"action", &g_cfg.keyAction}, {"kick", &g_cfg.keyKick},
        {"car aim", &g_cfg.keyCarAim}, {"car fire", &g_cfg.keyCarFire}};
    char line[480] = "";
    for (auto& a : all) {
        char one[48];
        snprintf(one, sizeof(one), "%s%s %s", line[0] ? ", " : "", a.job, a.b->label ? a.b->label : "(none)");
        strncat(line, one, sizeof(line) - strlen(line) - 1);
    }
    Log("Keys: %s (keyboard layout %04X)", line, (unsigned)((UINT_PTR)GetKeyboardLayout(0) & 0xFFFF));
    // the same key on two jobs (last weapon / switch page share Q on purpose: one works in the wheel,
    // the other outside it)
    // (the car's keys are used only in a car, where aim / fire / kick aren't: sharing them with those is fine)
    for (int i = 0; i < 12; i++)
        for (int j = i + 1; j < 12; j++)
            for (int ci : all[i].b->code)
                for (int cj : all[j].b->code)
                    if (ci && ci == cj && !(i == 6 && j == 7) && !(j >= 10 && (i == 1 || i == 2 || i == 9 || i >= 10)))
                        Note("|%s and %s share a key: it does both", all[i].job, all[j].job);
    for (char* t = strtok(g_bindNotes, "|"); t; t = strtok(nullptr, "|")) Log("Keys: %s", t);
    g_bindNotes[0] = 0;
}

// ------------------------------------------------------------------ per frame
static const int kMoveKeys[] = {KC_POS + 0x11, KC_POS + 0x1E, KC_POS + 0x1F, KC_POS + 0x20, VK_SPACE, VK_SHIFT};   // the game's own WASD, Space, Shift (count as using the keyboard)
static Kbm g_k;
static bool g_focus;
static float g_fakeLeft[KC_COUNT];   // debug "key": held for this long (-1: until "key none")
static float g_gameKeyLeft[256];     // debug "gkey": the game's key is let go after this long
static int g_moveFrames, g_moveDx, g_moveDy;   // debug "mmove": still to inject
static int g_scrollLeft, g_scrollEvery, g_scrollWait;   // debug "mscroll n,frames": notches still to inject, frames between
static int g_log;                 // debug "kbmlog": frames left
static int g_dbgFocus = -1;       // debug "focus": 0 / 1 pretends the window lost / has focus
static double g_switchT;
static WORD g_padPrevButtons;
static LONG g_zRest;
static bool g_actionDown, g_kickDown;
// Aiming with the mouse button: hold it, or give it a quick click (under a quarter of a second) and
// the aim stays on until the next click: a trackpad can't hold one button and click another.
static bool g_aimToggled, g_aimWasToggled, g_aimAllowed, g_carAimAllowed;
static double g_aimPressT;
static bool g_prevFocus, g_wheelOpen;
static float g_mouseUse, g_lStick, g_rStick;   // (the hints: recent mouse counts, last frame's stick pushes)
// A key pressed while Alt or a Windows key is held is the system's (Alt+Tab, Win+Tab), and a key or
// button held down in a menu, a cutscene or another window was pressed for that: either is ours only
// again once it has been let go. (Tab opened the wheel on every Alt+Tab; the right-click that closed
// a menu could come up in play as a click-to-aim.)
static bool g_sysKeys, g_away;
static BYTE g_seen[KC_COUNT];     // bit 0: down when last read; bit 1: not ours until let go

static bool g_pointerIn = true;   // the pointer is over the game's picture (this frame)

static bool RawDown(int c)
{
    if (c <= 0 || c >= KC_COUNT) return false;
    if (g_fakeLeft[c] != 0) return true;
    if (!g_focus) return false;
    // a mouse button counts over the game only: let go outside it (on a Mac's Dock, say) and the
    // window may never hear it come up - the aim stayed on, "stuck"
    if (c > KC_MOUSE && c < KC_POS && !g_pointerIn) return false;
    int vk = c >= KC_POS ? g_posVk[(c - KC_POS) & 0x7F]
           : c > KC_MOUSE ? (c == 0x101 ? VK_LBUTTON : c == 0x102 ? VK_RBUTTON : c == 0x103 ? VK_MBUTTON : c == 0x104 ? VK_XBUTTON1 : VK_XBUTTON2)
           : c;
    return vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool CodeDown(int c)
{
    if (c <= 0 || c >= KC_COUNT) return false;
    bool down = RawDown(c);
    if (c == VK_MENU || c == VK_LWIN || c == VK_RWIN) return down;
    bool button = c > KC_MOUSE && c < KC_POS;
    BYTE& s = g_seen[c];
    if (!down) s = 0;
    else s = (BYTE)(s | 1 | (g_away || (!(s & 1) && g_sysKeys && !button) ? 2 : 0));
    return down && !(s & 2);
}

static bool Down(const Bind& b) { return CodeDown(b.code[0]) || CodeDown(b.code[1]); }

static bool Binds(int code)
{
    const Bind* all[] = {&g_cfg.keyWheel, &g_cfg.keyAim, &g_cfg.keyFire, &g_cfg.keyFists, &g_cfg.keyHandgun,
                         &g_cfg.keyRPG, &g_cfg.keyShotgun, &g_cfg.keyDonut, &g_cfg.keyBeer, &g_cfg.keyLast, &g_cfg.keyPage, &g_cfg.keyAction, &g_cfg.keyKick,
                         &g_cfg.keyCarAim, &g_cfg.keyCarFire};
    for (const Bind* b : all)
        if (b->code[0] == code || b->code[1] == code) return true;
    return false;
}

static uint32_t MouseBits(const Bind& b)
{
    uint32_t m = 0;
    for (int c : b.code)
        if (c > KC_MOUSE && c < KC_POS) m |= 1u << (c - 0x101);
    return m;
}

void Kbm_Update(float dt, const Pad& pad, bool focused, bool gameplay)
{
    Kbm& k = g_k;
    if (!g_cfg.kbm) {
        k = Kbm{};
        k.pick = -1;
        return;
    }
    g_focus = focused;
    for (int c = 1; c < KC_COUNT; c++)
        if (g_fakeLeft[c] > 0 && (g_fakeLeft[c] -= dt) <= 0) g_fakeLeft[c] = 0;
    for (int i = 0; i < 256; i++)
        if (g_gameKeyLeft[i] > 0 && (g_gameKeyLeft[i] -= dt) <= 0) g_gameKeyLeft[i] = 0, PushGameKey(i, false);
    if (GetKeyboardLayout(0) != g_layout) ReadLayout();   // (the labels follow a change of layout)
    g_sysKeys = RawDown(VK_LWIN) || RawDown(VK_RWIN) || (RawDown(VK_MENU) && !Binds(VK_MENU));
    // (the click that brings the window back is held as it gets focus: that one isn't a shot)
    g_away = !gameplay || (focused && !g_prevFocus);
    g_prevFocus = focused;
    if (g_scrollLeft && --g_scrollWait <= 0) {   // (debug "mscroll n,frames": a notch every few frames)
        int d = g_scrollLeft > 0 ? 1 : -1;
        Push(R_Z, d * 120, true);
        g_scrollLeft -= d, g_scrollWait = g_scrollEvery;
    }
    if (g_moveFrames > 0) {   // debug test movement, a share each frame, through the game's own mouse
        Push(R_X, g_moveDx / g_moveFrames, true);
        Push(R_Y, g_moveDy / g_moveFrames, true);
        g_moveDx -= g_moveDx / g_moveFrames;
        g_moveDy -= g_moveDy / g_moveFrames;
        g_moveFrames--;
    }
    k.mdx = (float)InterlockedExchange(&g_accX, 0);
    k.mdy = (float)InterlockedExchange(&g_accY, 0);
    LONG z = InterlockedExchange(&g_accZ, 0) + g_zRest;   // a notch is 120 (less on a smooth wheel)
    int notches = z / 120;
    g_zRest = z - notches * 120;
    // (v1.0) One step a gesture. A flick of the wheel is a few notches over a few frames, and each frame's notch was a step: from
    // fists one flick ran past the handgun to the RPG, and the part notch a smooth wheel left over stepped again later (review:
    // "scrolling between fists and the handgun is too difficult/inconsistent"). The first notch steps at once; more the same way
    // with less than kScrollGap between them are the same flick; a pause, or the other way, is the next step. A part notch goes
    // once the wheel rests
    {
        static const double kScrollGap = 0.14;
        static double s_lastNotch;
        static int s_dir;
        double now = Time_Real();
        k.scroll = 0;
        if (notches) {
            int dir = notches > 0 ? 1 : -1;
            if (dir != s_dir || now - s_lastNotch > kScrollGap) k.scroll = dir;
            s_dir = dir, s_lastNotch = now;
        } else if (now - s_lastNotch > 0.4) {
            g_zRest = 0;
        }
    }
    if (!focused) k.mdx = k.mdy = 0, k.scroll = 0, g_zRest = 0;

    auto edge = [](bool now, bool& prev) { bool e = now && !prev; prev = now; return e; };
    static bool pWheel, pFists, pGun, pRpg, pShotgun, pDonut, pBeer, pLast, pPage, pAim, pFire, pCarAim;
    k.wheel = Down(g_cfg.keyWheel);
    bool rawAim = Down(g_cfg.keyAim);
    k.fire = Down(g_cfg.keyFire);
    bool wheelE = edge(k.wheel, pWheel), fireE = edge(k.fire, pFire);
    bool aimE = rawAim && !pAim, aimUp = !rawAim && pAim;
    pAim = rawAim;
    {
        double t = Time_Real();
        if (!g_aimAllowed) g_aimToggled = false;   // weapon away, wheel open, in a car: the click is forgotten
        if (aimE) g_aimPressT = t, g_aimWasToggled = g_aimToggled;
        if (aimUp && g_cfg.aimClick) g_aimToggled = (t - g_aimPressT < 0.25) && !g_aimWasToggled;
        k.aim = rawAim || g_aimToggled;
        // in a car, the same with the car's aim key (a quick click keeps aiming too)
        static bool carToggled, carWasToggled;
        static double carPressT;
        bool rawCar = Down(g_cfg.keyCarAim);
        if (!g_carAimAllowed) carToggled = false;
        if (rawCar && !pCarAim) carPressT = t, carWasToggled = carToggled;
        if (!rawCar && pCarAim && g_cfg.aimClick) carToggled = (t - carPressT < 0.25) && !carWasToggled;
        pCarAim = rawCar;
        k.carAim = rawCar || carToggled;
        k.carFire = Down(g_cfg.keyCarFire);
    }
    k.pick = -1;
    if (edge(Down(g_cfg.keyFists), pFists)) k.pick = WPN_NONE;
    if (edge(Down(g_cfg.keyHandgun), pGun)) k.pick = WPN_HANDGUN;
    if (edge(Down(g_cfg.keyRPG), pRpg)) k.pick = WPN_RPG;
    if (edge(Down(g_cfg.keyShotgun), pShotgun)) k.pick = WPN_SHOTGUN;
    if (edge(Down(g_cfg.keyDonut), pDonut)) k.pick = WPN_DONUT;
    if (edge(Down(g_cfg.keyBeer), pBeer)) k.pick = WPN_BEER;
    k.last = edge(Down(g_cfg.keyLast), pLast);
    k.page = edge(Down(g_cfg.keyPage), pPage);

    // the action key (E) and the kick key: the game's own left / right mouse button, slipped into
    // its mouse while the real buttons fire and aim (only in play: in a menu a click is a click; and
    // not in the wheel, which keeps every button from the game)
    bool act = gameplay && !g_wheelOpen && Down(g_cfg.keyAction);
    bool kick = gameplay && !g_wheelOpen && Down(g_cfg.keyKick);
    if (act != g_actionDown) Push(R_BTN0, act ? 0x80 : 0, false);
    if (kick != g_kickDown) Push(R_BTN0 + 1, kick ? 0x80 : 0, false);
    g_actionDown = act;
    g_kickDown = kick;

    // which hints to show: the last thing touched, held for a quarter of a second. The mouse counts
    // when it moved a little over the last moments (not a count of jitter from a resting hand on a
    // high-resolution mouse); a stick when it is pushed or swung (a drifting one sitting off centre
    // doesn't hold the hints on the pad)
    g_mouseUse = g_mouseUse * expf(-dt / 0.15f) + fabsf(k.mdx) + fabsf(k.mdy);
    bool kbmUsed = wheelE || aimE || fireE || k.pick >= 0 || k.last || k.page || k.scroll || act || kick ||
                   g_mouseUse >= 20.0f;
    if (!kbmUsed)
        for (int c : kMoveKeys)
            if (RawDown(c)) kbmUsed = true;
    float ls = sqrtf(pad.lx * pad.lx + pad.ly * pad.ly), rs = sqrtf(pad.rx * pad.rx + pad.ry * pad.ry);
    bool padUsed = pad.connected && ((pad.buttons & ~g_padPrevButtons) || pad.lt > 77 || pad.rt > 77 ||
                                     (ls > 0.3f && (g_lStick <= 0.3f || fabsf(ls - g_lStick) > 0.08f)) ||
                                     (rs > 0.3f && (g_rStick <= 0.3f || fabsf(rs - g_rStick) > 0.08f)));
    g_lStick = pad.connected ? ls : 0;
    g_rStick = pad.connected ? rs : 0;
    g_padPrevButtons = pad.connected ? pad.buttons : 0;
    double now = Time_Real();
    if (now - g_switchT > 0.25 || !pad.connected) {
        bool want = kbmUsed && !padUsed ? true : padUsed && !kbmUsed ? false : k.active;
        if (!pad.connected) want = true;   // no controller connected: never its buttons in the hints
        if (want != k.active) {
            k.active = want;
            g_switchT = now;
            Log("Hints: %s", want ? "keyboard and mouse" : "controller");
        }
    }
    if (g_log > 0) {
        g_log--;
        Log("kbmlog: mouse %+.0f %+.0f scroll %d | wheel %d aim %d fire %d pick %d last %d page %d act %d kick %d | hide move %ld z %ld btn 0x%lX | recs %ld | %s",
            k.mdx, k.mdy, k.scroll, k.wheel, k.aim, k.fire, k.pick, k.last, k.page, act, kick, g_hideMove, g_hideZ,
            g_hideButtons, InterlockedExchange(&g_recs, 0), k.active ? "kbm" : "pad");
    }
}

const Kbm& Kbm_Get() { return g_k; }

// ------------------------------------------------------------------ the pointer
// While playing with the keyboard and mouse the pointer is kept in the middle of the window
// (ClipCursor): on a Mac aiming down ran it into the Dock, which swelled up, and the game got no
// mouse movement there at all (CrossOver only sends moves over its own window; held at the clip's
// edge it sends them as relative moves, so aiming carries on); in a window on Windows a click
// outside went to the desktop. Let go in menus and the pause menu; and a watcher lets go at once if
// the game loses the front or is minimised, so the desktop's pointer is never left boxed in.
static volatile LONG g_grabbed;
static HWND g_grabWnd;

static bool PointerInWindow(HWND w)
{
    RECT rc;
    POINT tl = {0, 0}, pt;
    if (!w || !GetClientRect(w, &rc) || !ClientToScreen(w, &tl) || !GetCursorPos(&pt)) return true;   // (can't tell: yes)
    return pt.x >= tl.x && pt.y >= tl.y && pt.x < tl.x + rc.right && pt.y < tl.y + rc.bottom;
}

// in front: the game's window, or one it owns (the same test as the frame's "focused")
static bool InFront(HWND w)
{
    HWND fg = GetForegroundWindow();
    return fg && w && (fg == w || GetAncestor(fg, GA_ROOTOWNER) == GetAncestor(w, GA_ROOTOWNER));
}

static DWORD WINAPI GrabWatch(LPVOID)
{
    for (;;) {
        Sleep(100);
        HWND w = g_grabWnd;
        if (g_grabbed && (!IsWindow(w) || IsIconic(w) || !InFront(w))) {
            ClipCursor(nullptr);
            InterlockedExchange(&g_grabbed, 0);
        }
    }
}

// The game hides the pointer itself: its window procedure hides it on a mouse move over the picture
// (WM_MOUSEMOVE) and shows it on one over the frame (WM_NCMOUSEMOVE), through its own switch at
// 0x444640 (AL = shown; its state at 0x651B88; ShowCursor only when that changes). Coming back to the
// game over its title bar (from another app, on a Mac) left the pointer showing over the picture all
// through play (2026-10-08): held in the middle of the window, the game didn't get the move that
// hides it. So while playing with the mouse it is hidden here as the game would: its switch, and the
// display count at exactly -1, reached from 0 so the display driver is told to hide it again.
static bool PointerShowing()
{
    CURSORINFO ci = {sizeof(ci)};
    return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
}

static void HidePointer()
{
    if (Game_IsEnglish() && *(volatile const uint8_t*)0x651B88) {
        uint32_t shown = 0, fn = 0x444640;
        __asm__ __volatile__("mov %0, %%eax\n\t"
                             "call *%1"
                             :
                             : "r"(shown), "r"(fn)
                             : "eax", "ecx", "edx", "memory", "cc");
    }
    int c = ShowCursor(TRUE);
    for (int i = 0; i < 8 && c < 0; i++) c = ShowCursor(TRUE);
    for (int i = 0; i < 8 && c > -1; i++) c = ShowCursor(FALSE);
}

void Kbm_GrabPointer(bool on, HWND w)
{
    static bool s_watch;
    static int s_logs;
    g_pointerIn = PointerInWindow(w);   // (for the next frame's mouse buttons)
    if (on && w) {
        RECT rc;
        POINT tl = {0, 0};
        if (!GetClientRect(w, &rc) || !ClientToScreen(w, &tl) || rc.right < 64 || rc.bottom < 64) return;
        RECT box = {tl.x + rc.right / 4, tl.y + rc.bottom / 4, tl.x + rc.right * 3 / 4, tl.y + rc.bottom * 3 / 4};
        RECT now;
        if (!g_grabbed || !GetClipCursor(&now) || memcmp(&now, &box, sizeof(box))) {
            BOOL ok = ClipCursor(&box);
            if (g_cfg.debug && !g_grabbed) {
                RECT got = {};
                GetClipCursor(&got);
                Log("pointer: kept in (%ld,%ld)-(%ld,%ld): %s, clip now (%ld,%ld)-(%ld,%ld)", box.left, box.top, box.right, box.bottom,
                    ok ? "ok" : "refused", got.left, got.top, got.right, got.bottom);
            }
        }
        // hidden while held: once as it's taken, and again whenever it shows over the picture
        bool taking = !g_grabbed, showing = PointerShowing();
        g_pointerIn = PointerInWindow(w);   // (just moved into the window, when taken)
        if (g_pointerIn && (taking || showing)) {
            uint8_t flag = Game_IsEnglish() ? *(volatile const uint8_t*)0x651B88 : 0;
            HidePointer();
            if ((showing || taking) && s_logs < 12) {
                s_logs++;
                Log("Pointer: %s (it was %s; the game's own switch: %s)%s", taking ? "held in the game, hidden" : "hidden again while playing",
                    showing ? "showing" : "hidden", flag ? "shown" : "hidden", PointerShowing() ? " - but it still shows" : "");
            }
        }
        g_grabWnd = w;
        InterlockedExchange(&g_grabbed, 1);
        if (!s_watch) {
            s_watch = true;
            HANDLE t = CreateThread(nullptr, 0, GrabWatch, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
        }
    } else if (g_grabbed) {
        ClipCursor(nullptr);
        InterlockedExchange(&g_grabbed, 0);
        if (g_cfg.debug) Log("pointer: let go");
    }
}

void Kbm_AimAllowed(bool allowed) { g_aimAllowed = allowed; }
void Kbm_CarAimAllowed(bool allowed) { g_carAimAllowed = allowed; }

int Kbm_DebugFocus() { return g_dbgFocus; }

void Kbm_SetHide(bool move, bool scroll, bool aimFireButtons, bool allButtons)
{
    g_wheelOpen = allButtons;
    if (!g_installed) return;
    uint32_t b = allButtons ? 0xFF : aimFireButtons ? (MouseBits(g_cfg.keyAim) | MouseBits(g_cfg.keyFire) | MouseBits(g_cfg.keyCarAim) | MouseBits(g_cfg.keyCarFire)) : 0;
    InterlockedExchange(&g_hideMove, move ? 1 : 0);
    InterlockedExchange(&g_hideZ, scroll ? 1 : 0);
    InterlockedExchange(&g_hideButtons, (LONG)b);
}

// ------------------------------------------------------------------ debug
//   key <NAME>[:s]      hold a key or mouse button (bind names) for s seconds (default 0.2; 0 =
//                       until "key none")
//   mmove <dx>,<dy>[,<frames>]   move the mouse (raw counts) through the game's own mouse, spread
//                       over a number of frames (default 1)
//   mscroll <notches>   turn the scroll wheel (+ = away from you)
//   kbmlog <frames>     log the keyboard and mouse each frame
//   gkey <DIK hex>[:s]  press a key on the game's own keyboard (01 Escape, 1C Enter, C8/D0/CB/CD
//                       the arrows) for s seconds (default 0.15)
//   focus <0|1|auto>    pretend the game window lost / has focus (for the mod only)
bool Kbm_DebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "key")) {
        char buf[64];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char* colon = strchr(buf, ':');
        float secs = colon ? (float)atof(colon + 1) : 0.2f;
        if (colon) *colon = 0;
        if (!_stricmp(buf, "none")) {
            memset(g_fakeLeft, 0, sizeof(g_fakeLeft));
            return true;
        }
        int c = KeyCode(buf, nullptr);
        if (c > 0 && c < KC_COUNT) g_fakeLeft[c] = secs > 0 ? secs : -1.0f;
        else Log("key: unknown key %s", buf);
    } else if (!strcmp(cmd, "gkey")) {
        unsigned dik = 0;
        float secs = 0.15f;
        sscanf(arg, "%x:%f", &dik, &secs);
        if (dik > 0 && dik < 256 && g_kbDev && g_kbOfs[dik] != ~0ul) {
            PushGameKey((int)dik, true);
            g_gameKeyLeft[dik] = secs > 0.02f ? secs : 0.02f;
        } else {
            Log("gkey: the game's keyboard has no key %02X", dik);
        }
    } else if (!strcmp(cmd, "focus")) {
        g_dbgFocus = !arg[0] || !strcmp(arg, "auto") ? -1 : atoi(arg) != 0;
    } else if (!strcmp(cmd, "pointerhide")) {   // (lab) the game's own pointer hidden, as playing with the mouse does: left over the
                                                // picture by the last grab, it was drawn on Homer's chest in every recording
        HidePointer();
        Log("pointerhide: %s", PointerShowing() ? "it still shows" : "hidden");
    } else if (!strcmp(cmd, "mmove")) {
        int dx = 0, dy = 0, frames = 1;
        sscanf(arg, "%d,%d,%d", &dx, &dy, &frames);
        g_moveDx = dx;
        g_moveDy = dy;
        g_moveFrames = frames < 1 ? 1 : frames;
    } else if (!strcmp(cmd, "mscroll")) {   // "mscroll <notches>[,<frames>]": all at once, or a notch every few frames (a flick)
        int n = 0, frames = 0;
        sscanf(arg, "%d,%d", &n, &frames);
        if (frames > 1 && n) g_scrollLeft = n, g_scrollEvery = frames / (n < 0 ? -n : n), g_scrollWait = 0;
        else Push(R_Z, n * 120, true);
    } else if (!strcmp(cmd, "kbmlog")) {
        g_log = arg[0] ? atoi(arg) : 30;
    } else {
        return false;
    }
    return true;
}
