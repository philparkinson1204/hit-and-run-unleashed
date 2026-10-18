// d3d8.dll proxy. The game's graphics driver (pddidx8r.dll) imports Direct3DCreate8
// from d3d8.dll, so a d3d8.dll in the game folder is loaded instead of the system one.
// We forward everything to the real system d3d8.dll and, inside Simpsons.exe only,
// hook the device so the wheel can be drawn on top of each frame.
//
// The mod launcher also loads d3d8.dll (to list anti-aliasing modes); in any process
// other than Simpsons.exe this DLL is a plain pass-through.
#include "common.h"
#include <stdarg.h>
#include <ctype.h>

Config g_cfg;
wchar_t g_modDir[MAX_PATH];

static HMODULE g_real;
static bool g_active;       // running inside Simpsons.exe
static volatile bool g_initDone;   // (also read by the start watcher thread)
static FILE* g_log;

// ------------------------------------------------------------------ logging
void Log_Open()
{
    if (g_log) return;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsWeaponWheel.log", g_modDir);
    g_log = _wfopen(path, L"w");
}

void Log(const char* fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ------------------------------------------------------------------ config
// a missing, empty or unreadable value gives the default (it gave 0: "Brightness=" or
// "Brightness=0,72" meant a black backdrop, "Volume=0,6" silence); a decimal comma is accepted
static float IniFloat(const wchar_t* path, const wchar_t* key, float def)
{
    wchar_t buf[64];
    GetPrivateProfileStringW(L"WeaponWheel", key, L"", buf, 64, path);
    for (wchar_t* c = buf; *c; c++)
        if (*c == L',') *c = L'.';
    wchar_t* end;
    double v = wcstod(buf, &end);
    return (end == buf || v != v) ? def : (float)v;
}

// 1 / 0, and the words people write for them (Enabled=false left the mod on)
static bool IniBool(const wchar_t* path, const wchar_t* key, bool def)
{
    wchar_t buf[64];
    GetPrivateProfileStringW(L"WeaponWheel", key, L"", buf, 64, path);
    wchar_t* t = buf + wcsspn(buf, L" \t\"");
    if (!*t) return def;
    if (!_wcsnicmp(t, L"true", 4) || !_wcsnicmp(t, L"yes", 3) || !_wcsnicmp(t, L"on", 2)) return true;
    if (!_wcsnicmp(t, L"false", 5) || !_wcsnicmp(t, L"no", 2) || !_wcsnicmp(t, L"off", 3)) return false;
    return IniFloat(path, key, def ? 1.0f : 0.0f) != 0;
}

static const struct { const char* name; uint32_t bit; } kButtons[] = {
    {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000}, {"LB", 0x0100}, {"RB", 0x0200},
    {"LS", 0x0040}, {"RS", 0x0080}, {"BACK", 0x0020}, {"START", 0x0010}, {"DPAD_UP", 0x0001},
    {"DPAD_DOWN", 0x0002}, {"DPAD_LEFT", 0x0004}, {"DPAD_RIGHT", 0x0008}, {"LT", BTN_LT}, {"RT", BTN_RT}};

uint32_t ParseButtons(const char* text, uint32_t def)
{
    uint32_t mask = 0;
    char buf[128];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    buf[strcspn(buf, ";#")] = 0;   // ("WheelButton=RB ; or A": the profile API keeps a comment after a value)
    for (char* tok = strtok(buf, " ,+|\t"); tok; tok = strtok(nullptr, " ,+|\t")) {
        for (char* c = tok; *c; c++) *c = (char)toupper((unsigned char)*c);
        for (auto& b : kButtons)
            if (!strcmp(tok, b.name)) mask |= b.bit;
    }
    return mask ? mask : def;
}

void ButtonNames(uint32_t mask, char* out, int n)
{
    out[0] = 0;
    for (auto& b : kButtons)
        if (mask & b.bit) {
            if (out[0]) strncat(out, "/", n - strlen(out) - 1);
            const char* nm = b.name;
            if (!strncmp(nm, "DPAD_", 5)) nm += 5;   // short names for the on-screen hints
            strncat(out, nm, n - strlen(out) - 1);
        }
}

// up to max on-screen names for the buttons in mask (unused entries are null)
int ButtonList(uint32_t mask, const char** out, int max)
{
    static const struct { uint32_t bit; const char* label; } kLabels[] = {
        {0x0004, "D-LEFT"}, {0x0008, "D-RIGHT"}, {0x0001, "D-UP"}, {0x0002, "D-DOWN"}, {BTN_LT, "LT"},
        {BTN_RT, "RT"}, {0x0100, "LB"}, {0x0200, "RB"}, {0x1000, "A"}, {0x2000, "B"}, {0x4000, "X"},
        {0x8000, "Y"}, {0x0040, "LS"}, {0x0080, "RS"}, {0x0020, "BACK"}, {0x0010, "START"}};
    int n = 0;
    for (int i = 0; i < max; i++) out[i] = nullptr;
    for (auto& l : kLabels)
        if ((mask & l.bit) && n < max) out[n++] = l.label;
    return n;
}

static uint32_t IniButtons(const wchar_t* path, const wchar_t* key, uint32_t def)
{
    wchar_t w[128];
    GetPrivateProfileStringW(L"WeaponWheel", key, L"", w, 128, path);
    char a[128];
    WideCharToMultiByte(CP_ACP, 0, w, -1, a, sizeof(a), nullptr, nullptr);
    return ParseButtons(a, def);
}

void Config_Load()
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsWeaponWheel.ini", g_modDir);
    g_cfg.wheelButtons = IniButtons(path, L"WheelButton", g_cfg.wheelButtons);
    g_cfg.aimButtons = IniButtons(path, L"AimButton", g_cfg.aimButtons);
    g_cfg.fireButtons = IniButtons(path, L"FireButton", g_cfg.fireButtons);
    g_cfg.carAimButtons = IniButtons(path, L"CarAimButton", g_cfg.carAimButtons);
    g_cfg.carFireButtons = IniButtons(path, L"CarFireButton", g_cfg.carFireButtons);
    g_cfg.enabled = IniBool(path, L"Enabled", true);
    g_cfg.slowMotion = IniFloat(path, L"SlowMotion", g_cfg.slowMotion);
    g_cfg.blur = IniFloat(path, L"Blur", g_cfg.blur);
    g_cfg.dim = IniFloat(path, L"Brightness", g_cfg.dim);
    g_cfg.uiScale = IniFloat(path, L"Size", g_cfg.uiScale);
    g_cfg.debug = IniBool(path, L"Debug", false);
    {
        extern float g_labFpsCap;
        wchar_t buf[32];
        GetPrivateProfileStringW(L"WeaponWheel", L"LabFpsCap", L"0", buf, 32, path);
        g_labFpsCap = (float)_wtof(buf);
    }
    g_cfg.aimSens = IniFloat(path, L"AimSensitivity", g_cfg.aimSens);
    g_cfg.invertY = IniBool(path, L"InvertY", false);
    g_cfg.aimDeadzone = IniFloat(path, L"AimDeadzone", g_cfg.aimDeadzone);
    g_cfg.volume = IniFloat(path, L"Volume", g_cfg.volume);
    g_cfg.kbm = IniBool(path, L"Keyboard", true);
    g_cfg.mouseSens = IniFloat(path, L"MouseSensitivity", g_cfg.mouseSens);
    g_cfg.mouseInvertY = IniBool(path, L"MouseInvertY", false);
    g_cfg.wheelMouse = IniFloat(path, L"WheelMouseDistance", g_cfg.wheelMouse);
    g_cfg.aimClick = IniBool(path, L"AimClick", true);
    g_cfg.driveBy = IniBool(path, L"DriveBy", false);
    {
        wchar_t w[32];
        GetPrivateProfileStringW(L"WeaponWheel", L"DriveByCamera", L"", w, 32, path);
        if (!_wcsnicmp(w, L"roof", 4) || !wcscmp(w, L"0")) g_cfg.driveByCam = 0;
    }
    g_cfg.driveByLean = IniBool(path, L"DriveByLean", false);
    g_cfg.driveBySmash = IniBool(path, L"DriveByElbowSmash", true);
    g_cfg.driveByFullAim = IniBool(path, L"DriveByFullAim", true);
    if (g_cfg.mouseSens < 0.05f) g_cfg.mouseSens = 0.05f;
    if (g_cfg.mouseSens > 10.0f) g_cfg.mouseSens = 10.0f;
    if (g_cfg.wheelMouse < 10.0f) g_cfg.wheelMouse = 10.0f;
    if (g_cfg.wheelMouse > 5000.0f) g_cfg.wheelMouse = 5000.0f;
    static const struct { const wchar_t* key; Bind* bind; const char* def; } kBinds[] = {
        {L"KeyWheel", &g_cfg.keyWheel, "TAB"}, {L"KeyAim", &g_cfg.keyAim, "MOUSE2"}, {L"KeyFire", &g_cfg.keyFire, "MOUSE1"},
        {L"KeyFists", &g_cfg.keyFists, "1"}, {L"KeyHandgun", &g_cfg.keyHandgun, "2"}, {L"KeyRPG", &g_cfg.keyRPG, "3"}, {L"KeyShotgun", &g_cfg.keyShotgun, "4"}, {L"KeyDonut", &g_cfg.keyDonut, "5"}, {L"KeyBeer", &g_cfg.keyBeer, "6"},
        {L"KeyLastWeapon", &g_cfg.keyLast, "Q"}, {L"KeySwitchPage", &g_cfg.keyPage, "Q"}, {L"KeyAction", &g_cfg.keyAction, "E"},
        {L"KeyKick", &g_cfg.keyKick, "F"}, {L"KeyCarAim", &g_cfg.keyCarAim, "MOUSE2"}, {L"KeyCarFire", &g_cfg.keyCarFire, "MOUSE1"}};
    for (auto& b : kBinds) {
        wchar_t w[64];
        char a[64], job[32];
        GetPrivateProfileStringW(L"WeaponWheel", b.key, L"", w, 64, path);
        WideCharToMultiByte(CP_ACP, 0, w, -1, a, sizeof(a), nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, b.key, -1, job, sizeof(job), nullptr, nullptr);
        Kbm_ParseBind(job, a[0] ? a : b.def, b.bind, b.def);
    }
    if (g_cfg.volume < 0.0f) g_cfg.volume = 0.0f;
    if (g_cfg.volume > 1.0f) g_cfg.volume = 1.0f;
    if (g_cfg.aimSens < 0.2f) g_cfg.aimSens = 0.2f;
    if (g_cfg.aimSens > 4.0f) g_cfg.aimSens = 4.0f;
    if (g_cfg.aimDeadzone < 0.02f) g_cfg.aimDeadzone = 0.02f;
    if (g_cfg.aimDeadzone > 0.6f) g_cfg.aimDeadzone = 0.6f;
    if (g_cfg.dim < 0.0f) g_cfg.dim = 0.0f;
    if (g_cfg.dim > 1.0f) g_cfg.dim = 1.0f;
    if (g_cfg.blur < 0.0f) g_cfg.blur = 0.0f;
    if (g_cfg.blur > 3.0f) g_cfg.blur = 3.0f;
    if (g_cfg.slowMotion < 0.05f) g_cfg.slowMotion = 0.05f;
    if (g_cfg.slowMotion > 1.0f) g_cfg.slowMotion = 1.0f;
    if (g_cfg.uiScale < 0.5f) g_cfg.uiScale = 0.5f;
    if (g_cfg.uiScale > 1.25f) g_cfg.uiScale = 1.25f;   // bigger pushed the tabs and the hint bar off screen
    // the wheel opens on a button, not a trigger (LT/RT parsed but could never open it)
    if (!(g_cfg.wheelButtons & 0xFFFF)) g_cfg.wheelButtons = 0x0100;
}

// ------------------------------------------------------------------ real d3d8
static HMODULE Real()
{
    if (!g_real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);   // SysWOW64 via redirection for 32-bit
        wcscat(path, L"\\d3d8.dll");
        g_real = LoadLibraryW(path);
    }
    return g_real;
}

static FARPROC RealProc(const char* name)
{
    HMODULE h = Real();
    return h ? GetProcAddress(h, name) : nullptr;
}

// ------------------------------------------------------------------ device hooks
typedef HRESULT(WINAPI* PFN_CreateDevice)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD,
                                          D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
typedef HRESULT(WINAPI* PFN_Reset)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* PFN_Present)(IDirect3DDevice8*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(WINAPI* PFN_CreateSwapChain)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain8**);
typedef HRESULT(WINAPI* PFN_SwapPresent)(IDirect3DSwapChain8*, const RECT*, const RECT*, HWND, const RGNDATA*);

static PFN_CreateDevice o_CreateDevice;
static PFN_Reset o_Reset;
static PFN_Present o_Present;
static PFN_CreateSwapChain o_CreateSwapChain;
static PFN_SwapPresent o_SwapPresent;
static IDirect3DDevice8* g_device;
static HWND g_deviceHwnd;
static bool g_swapSeen;   // the game presents through an additional swap chain (windowed Pure3D)

// The game's Pure3D driver creates a 1x1 device and renders the real window through
// an additional swap chain, so frames are shown by IDirect3DSwapChain8::Present.
static struct { IDirect3DSwapChain8* sc; HWND hwnd; } g_chains[8];

static HWND ChainWindow(IDirect3DSwapChain8* sc)
{
    for (auto& c : g_chains)
        if (c.sc == sc) return c.hwnd;
    return g_deviceHwnd;
}

// (Weapons Lab, Debug=1 only) "LabFpsCap=N" in the ini / debug "fpscap N": at most N frames a second - the game under Wine
// keeps the Mac's CPU near 100 C otherwise; 0 = off
float g_labFpsCap;
static void LabFrameCap()
{
    static LARGE_INTEGER last, freq;
    if (g_labFpsCap < 1.0f || !g_cfg.debug) return;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double want = 1.0 / g_labFpsCap, el = last.QuadPart ? (double)(now.QuadPart - last.QuadPart) / freq.QuadPart : want;
    if (el < want) {
        Sleep((DWORD)((want - el) * 1000.0));
        QueryPerformanceCounter(&now);
    }
    last = now;
}

static HRESULT WINAPI h_SwapPresent(IDirect3DSwapChain8* self, const RECT* src, const RECT* dst, HWND wnd,
                                    const RGNDATA* dirty)
{
    LabFrameCap();
    if (g_device) {
        if (!g_swapSeen) Log("Frames are presented through swap chain %p", self);
        g_swapSeen = true;
        IDirect3DSurface8* bb = nullptr;
        if (SUCCEEDED(self->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            Wheel_OnPresent(g_device, bb, wnd ? wnd : ChainWindow(self));
            bb->Release();
        }
    }
    Game_RestoreCamera();   // undo the aiming camera before the game updates again
    return o_SwapPresent(self, src, dst, wnd, dirty);
}

static HRESULT WINAPI h_CreateSwapChain(IDirect3DDevice8* self, D3DPRESENT_PARAMETERS* pp, IDirect3DSwapChain8** out)
{
    HRESULT hr = o_CreateSwapChain(self, pp, out);
    Log("CreateAdditionalSwapChain -> 0x%08lX (%ux%u hwnd=%p)", hr, pp ? pp->BackBufferWidth : 0,
        pp ? pp->BackBufferHeight : 0, pp ? pp->hDeviceWindow : nullptr);
    if (SUCCEEDED(hr) && out && *out) {
        void* p = PatchVTable(*out, 3, (void*)h_SwapPresent);
        if (p && p != (void*)h_SwapPresent) o_SwapPresent = (PFN_SwapPresent)p;
        for (auto& c : g_chains)
            if (!c.sc || c.sc == *out) {
                c.sc = *out;
                c.hwnd = pp ? pp->hDeviceWindow : nullptr;
                break;
            }
    }
    return hr;
}

static HRESULT WINAPI h_Reset(IDirect3DDevice8* self, D3DPRESENT_PARAMETERS* pp)
{
    if (self == g_device) Wheel_OnLostDevice();
    HRESULT hr = o_Reset(self, pp);
    if (self == g_device) {
        Log("Reset -> 0x%08lX (%ux%u windowed=%d)", hr, pp ? pp->BackBufferWidth : 0,
            pp ? pp->BackBufferHeight : 0, pp ? pp->Windowed : -1);
        if (SUCCEEDED(hr)) Wheel_OnResetDevice();
    }
    return hr;
}

static HRESULT WINAPI h_Present(IDirect3DDevice8* self, const RECT* src, const RECT* dst,
                                HWND wnd, const RGNDATA* dirty)
{
    if (self == g_device && !g_swapSeen) {
        IDirect3DSurface8* bb = nullptr;
        if (SUCCEEDED(self->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            Wheel_OnPresent(self, bb, wnd ? wnd : g_deviceHwnd);
            bb->Release();
        }
    }
    if (self == g_device && !g_swapSeen) Game_RestoreCamera();   // windowed: done after the swap chain present
    return o_Present(self, src, dst, wnd, dirty);
}

static void HookDevice(IDirect3DDevice8* dev, D3DPRESENT_PARAMETERS* pp, HWND focus)
{
    void* r = PatchVTable(dev, 14, (void*)h_Reset);
    if (r && r != (void*)h_Reset) o_Reset = (PFN_Reset)r;
    void* p = PatchVTable(dev, 15, (void*)h_Present);
    if (p && p != (void*)h_Present) o_Present = (PFN_Present)p;
    void* c = PatchVTable(dev, 13, (void*)h_CreateSwapChain);
    if (c && c != (void*)h_CreateSwapChain) o_CreateSwapChain = (PFN_CreateSwapChain)c;
    Scene_HookDevice(dev);
    g_device = dev;
    g_deviceHwnd = (pp && pp->hDeviceWindow) ? pp->hDeviceWindow : focus;
    Log("Device %p hooked: %ux%u fmt=%d windowed=%d ms=%d swap=%d hwnd=%p",
        dev, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
        pp ? pp->BackBufferFormat : 0, pp ? pp->Windowed : -1,
        pp ? pp->MultiSampleType : -1, pp ? pp->SwapEffect : -1, g_deviceHwnd);
    Wheel_OnDevice(dev);
}

static HRESULT WINAPI h_CreateDevice(IDirect3D8* self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                     DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** out)
{
    HRESULT hr = o_CreateDevice(self, adapter, type, focus, flags, pp, out);
    Log("CreateDevice -> 0x%08lX", hr);
    if (SUCCEEDED(hr) && out && *out) HookDevice(*out, pp, focus);
    return hr;
}

static void InitOnce()
{
    if (g_initDone) return;
    g_initDone = true;
    CreateDirectoryW(g_modDir, nullptr);
    Config_Load();
    Log_Open();
    Log("SHAR Weapon Wheel " WW_VERSION " loaded (enabled=%d slowmo=%.2f blur=%.2f size=%.2f debug=%d)",
        g_cfg.enabled, g_cfg.slowMotion, g_cfg.blur, g_cfg.uiScale, g_cfg.debug);
    if (!g_cfg.enabled) return;
    // the hooks below point the game's clock imports and XInput into this DLL: stay loaded for
    // good (we are only held through the game's video driver DLL, which could be freed at exit)
    HMODULE self;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&InitOnce, &self);
    Game_Init();
    Time_Install();
    Input_Install();
    Kbm_Install();
    Kbm_LogBinds();
    Gun_StartBodyData();   // (Homer's skin points, built from his own files on a thread of their own)
}

// ------------------------------------------------------------------ not hooked in
// The Mod Launcher's Direct3D 9 hack builds the game's graphics on Direct3D 9 and never calls our
// Direct3DCreate8, so the mod can't run - and used to say nothing at all. This watcher (started from
// DllMain) waits for the game's window; if the mod still hasn't started a few seconds after it shows,
// it says why, in the log and in a message box.
static BOOL CALLBACK FindGameWindow(HWND w, LPARAM out)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    *(HWND*)out = w;
    return FALSE;
}

static bool CoversMonitor(HWND w)
{
    RECT r;
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    return GetWindowRect(w, &r) && GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi) &&
           r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
           r.bottom >= mi.rcMonitor.bottom;
}

static DWORD WINAPI StartWatch(LPVOID)
{
    double shown = 0;
    for (int i = 0; i < 600 && !g_initDone; i++) {   // up to 2 minutes
        Sleep(200);
        HWND w = nullptr;
        EnumWindows(FindGameWindow, (LPARAM)&w);
        if (!w) continue;
        if (shown == 0) shown = i;
        if (i - shown < 25) continue;   // 5 s after the window showed
        if (g_initDone) return 0;
        CreateDirectoryW(g_modDir, nullptr);
        Config_Load();
        if (!g_cfg.enabled) return 0;
        Log_Open();
        bool d3d9 = GetModuleHandleW(L"d3d9.dll") != nullptr;
        Log("SHAR Weapon Wheel " WW_VERSION ": NOT STARTED - the game's graphics never came through d3d8.dll%s",
            d3d9 ? " (the Mod Launcher's Direct3D 9 hack is on)" : "");
        // a box in front of a fullscreen game would throw it out to the desktop: wait until the
        // player leaves it (Alt+Tab). If the game closes first, the log has it.
        while (IsWindow(w) && GetForegroundWindow() == w && CoversMonitor(w)) Sleep(500);
        if (!IsWindow(w) || g_initDone) return 0;
        MessageBoxW(nullptr,
                    d3d9 ? L"The weapon wheel can't run while the Mod Launcher's \"Direct3D 9\" hack is on.\n\n"
                           L"Turn it off in the Mod Launcher (Hacks) and start the game again.\n"
                           L"The game itself runs normally, just without the weapon wheel."
                         : L"The weapon wheel didn't start: the game's graphics never came through its d3d8.dll.\n\n"
                           L"Check that d3d8.dll is in the game folder next to Simpsons.exe, and see\n"
                           L"WeaponWheel\\WeaponWheel.log.",
                    L"SHAR Weapon Wheel", MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        return 0;
    }
    return 0;
}

// ------------------------------------------------------------------ exports
extern "C" IDirect3D8* WINAPI Proxy_Direct3DCreate8(UINT sdk)
{
    typedef IDirect3D8*(WINAPI * PFN)(UINT);
    static PFN real;
    if (!real) real = (PFN)RealProc("Direct3DCreate8");
    if (!real) return nullptr;
    IDirect3D8* d3d = real(sdk);
    if (d3d && g_active) {
        InitOnce();
        if (g_cfg.enabled) {
            void* p = PatchVTable(d3d, 15, (void*)h_CreateDevice);
            if (p && p != (void*)h_CreateDevice) o_CreateDevice = (PFN_CreateDevice)p;
            Log("Direct3DCreate8(%u) -> %p, CreateDevice hooked", sdk, d3d);
        }
    }
    return d3d;
}

extern "C" HRESULT WINAPI Proxy_ValidatePixelShader(const DWORD* a, const D3DCAPS8* b, BOOL c, char** d)
{
    typedef HRESULT(WINAPI * PFN)(const DWORD*, const D3DCAPS8*, BOOL, char**);
    PFN f = (PFN)RealProc("ValidatePixelShader");
    return f ? f(a, b, c, d) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_ValidateVertexShader(const DWORD* a, const DWORD* b, const DWORD* c, BOOL d, char** e)
{
    typedef HRESULT(WINAPI * PFN)(const DWORD*, const DWORD*, const DWORD*, BOOL, char**);
    PFN f = (PFN)RealProc("ValidateVertexShader");
    return f ? f(a, b, c, d, e) : E_FAIL;
}

extern "C" void WINAPI Proxy_DebugSetMute(void)
{
    typedef void(WINAPI * PFN)(void);
    PFN f = (PFN)RealProc("DebugSetMute");
    if (f) f();
}

extern "C" HRESULT WINAPI Proxy_Direct3D8EnableMaximizedWindowedModeShim(UINT a)
{
    typedef HRESULT(WINAPI * PFN)(UINT);
    PFN f = (PFN)RealProc("Direct3D8EnableMaximizedWindowedModeShim");
    return f ? f(a) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* base = wcsrchr(exe, L'\\');
        base = base ? base + 1 : exe;
        g_active = _wcsicmp(base, L"Simpsons.exe") == 0;
        GetModuleFileNameW(inst, g_modDir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_modDir, L'\\');
        if (slash) slash[1] = 0;
        wcscat(g_modDir, L"WeaponWheel\\");
        if (g_active) {
            HANDLE t = CreateThread(nullptr, 0, StartWatch, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
        }
    }
    return TRUE;
}
