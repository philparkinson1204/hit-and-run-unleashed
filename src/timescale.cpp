// Slow motion. The game measures time with QueryPerformanceCounter (radcore's
// radTimeGet* functions convert it on every call) and GetTickCount. We patch those two
// imports of Simpsons.exe only, and hand the game a "virtual" clock that runs at
// g_scale times real speed. Other modules (the mod launcher's hacks, sound, our own
// animations) keep reading the real clock.
#include "common.h"

typedef BOOL(WINAPI* PFN_QPC)(LARGE_INTEGER*);
typedef DWORD(WINAPI* PFN_GTC)(void);

static PFN_QPC o_QPC;
static PFN_GTC o_GTC;
static CRITICAL_SECTION g_cs;
static double g_scale = 1.0;
static LONGLONG g_realBase, g_virtBase;     // QPC ticks
static DWORD g_tickRealBase;
static double g_tickVirtBase;
static double g_freq = 1.0;
static bool g_installed;

static BOOL WINAPI h_QPC(LARGE_INTEGER* out)
{
    if (!out) return o_QPC(out);
    LARGE_INTEGER r;
    EnterCriticalSection(&g_cs);
    BOOL ok = o_QPC(&r);
    if (ok) out->QuadPart = g_virtBase + (LONGLONG)((double)(r.QuadPart - g_realBase) * g_scale);
    LeaveCriticalSection(&g_cs);
    return ok;
}

static DWORD WINAPI h_GTC(void)
{
    EnterCriticalSection(&g_cs);
    DWORD r = o_GTC();
    double v = g_tickVirtBase + (double)(DWORD)(r - g_tickRealBase) * g_scale;
    LeaveCriticalSection(&g_cs);
    return (DWORD)(LONGLONG)v;
}

void Time_Install()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = (double)f.QuadPart;
    InitializeCriticalSection(&g_cs);

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    o_QPC = (PFN_QPC)GetProcAddress(k32, "QueryPerformanceCounter");
    o_GTC = (PFN_GTC)GetProcAddress(k32, "GetTickCount");

    LARGE_INTEGER now;
    o_QPC(&now);
    g_realBase = g_virtBase = now.QuadPart;
    g_tickRealBase = o_GTC();
    g_tickVirtBase = g_tickRealBase;

    HMODULE exe = GetModuleHandleW(nullptr);
    void* prevQ = PatchIAT(exe, "kernel32.dll", "QueryPerformanceCounter", (void*)h_QPC);
    void* prevT = PatchIAT(exe, "kernel32.dll", "GetTickCount", (void*)h_GTC);
    // chain to whatever was there (another mod may have hooked it first)
    EnterCriticalSection(&g_cs);
    if (prevQ && prevQ != (void*)o_QPC) {
        o_QPC = (PFN_QPC)prevQ;
        o_QPC(&now);
        g_realBase = g_virtBase = now.QuadPart;
    }
    if (prevT && prevT != (void*)o_GTC) {
        o_GTC = (PFN_GTC)prevT;
        g_tickRealBase = o_GTC();
        g_tickVirtBase = g_tickRealBase;
    }
    LeaveCriticalSection(&g_cs);
    g_installed = prevQ != nullptr;
    Log("Time hooks: QPC=%s GetTickCount=%s", prevQ ? "ok" : "MISSING", prevT ? "ok" : "MISSING");
}

void Time_SetScale(double scale)
{
    if (!g_installed || fabs(scale - g_scale) < 1e-4) return;
    EnterCriticalSection(&g_cs);
    LARGE_INTEGER r;
    o_QPC(&r);
    DWORD t = o_GTC();
    g_virtBase += (LONGLONG)((double)(r.QuadPart - g_realBase) * g_scale);
    g_realBase = r.QuadPart;
    g_tickVirtBase += (double)(DWORD)(t - g_tickRealBase) * g_scale;
    g_tickRealBase = t;
    g_scale = scale;
    LeaveCriticalSection(&g_cs);
}

double Time_GetScale()
{
    return g_scale;
}

double Time_Real()
{
    LARGE_INTEGER r;
    if (o_QPC)
        o_QPC(&r);
    else
        QueryPerformanceCounter(&r);
    return (double)r.QuadPart / g_freq;
}
