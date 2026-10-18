// Debug commands (only when Debug=1 in WeaponWheel.ini): write lines to
// WeaponWheel\debug_cmd.txt; the file is read and deleted.
//   open | close          force the wheel open without LB or game checks
//   stick <degrees>       pretend the right stick points that way (0 = up)
//   nostick               stop pretending
//   tab <0|1>             switch page
//   shot <file.bmp>       save the next frame (with the wheel) to WeaponWheel\<file>
//   rec <seconds>         record every frame at half size to WeaponWheel\rec.raw (+ rec.txt: size, times); rec -1 stops
// Test hook (a pretend controller merged into the real one, seen by the game and the mod):
//   fakepad <1|0>         pretend controller on (neutral) / off
//   pad <BUTTONS>[:s]     press buttons (names as in the ini, joined with +) for s seconds
//                         (default 0.2; 0 = hold until "pad none")
//   lstick <x>,<y>        hold the left stick (-1..1, y up; 0,0 = let go); rstick likewise
//   wstick <x>,<y>,<h>    the left stick as if the camera looked along heading h (rad, G.yaw's): the same walk whatever the
//                         camera does (a debug orbit camera round him turned "lstick" with him)
//   walkto <x>,<z>[,<speed>] | walkto car|npc[,<speed>]   steer the left stick there (camera
//                         relative) and let go on arrival (car / npc: the nearest vehicle / character)
//   where                 log his position, heading, the camera heading and nearby vehicles
#include "common.h"
bool Lab_DebugCmd(const char* cmd, const char* arg);
#include <math.h>

static bool s_force, s_stick;
static float s_angle;
static double s_nextPoll;

// "poke <p|b> <hexaddr> <float>": write a float every frame, at Present (p) or at the
// game's BeginScene (b). Addresses may be "player+OFF", "loco+OFF", "puppet+OFF".
struct Poke { char when; uint32_t addr; char base[12]; float value; };
static Poke s_pokes[16];
static int s_npokes;

static uint32_t RdU32(uint32_t a)
{
    uint32_t v = 0;
    SIZE_T got = 0;
    if (a < 0x10000 || !ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)a, &v, 4, &got)) return 0;
    return v;
}

static uint32_t ResolveBase(const char* base)
{
    if (!Game_IsEnglish()) return 0;   // (0x6C8470 is the English release's CharacterManager)
    uint32_t cm = RdU32(0x6C8470);
    uint32_t player = cm ? RdU32(cm + 192) : 0;
    if (!strcmp(base, "player")) return player;
    uint32_t puppet = player ? RdU32(player + 0x108) : 0;
    if (!strcmp(base, "puppet")) return puppet;
    if (!strcmp(base, "loco")) return puppet ? RdU32(RdU32(puppet + 0x20)) : 0;
    if (!strcmp(base, "sim")) return player ? RdU32(player + 0x70) : 0;
    if (!strcmp(base, "ctrl")) return player ? RdU32(player + 0x100) : 0;
    return 0;
}

void Debug_ApplyPokes(char when)
{
    for (int i = 0; i < s_npokes; i++) {
        Poke& p = s_pokes[i];
        if (p.when != when) continue;
        uint32_t a = p.addr + (p.base[0] ? ResolveBase(p.base) : 0);
        if (a < 0x10000) continue;
        SIZE_T done;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)a, &p.value, 4, &done);
    }
}

static void AddPoke(const char* line)
{
    char when = 'p', target[64] = {0};
    float value = 0;
    if (sscanf(line, "poke %c %63s %f", &when, target, &value) != 3 || s_npokes >= 16) return;
    Poke p = {};
    p.when = when;
    p.value = value;
    char* plus = strchr(target, '+');
    if (plus) {
        *plus = 0;
        strncpy(p.base, target, sizeof(p.base) - 1);
        p.addr = strtoul(plus + 1, nullptr, 16);
    } else {
        p.addr = strtoul(target, nullptr, 16);
    }
    s_pokes[s_npokes++] = p;
    Log("poke %c %s+%X = %f", p.when, p.base, p.addr, p.value);
}

// ------------------------------------------------------------------ test hook
static struct {
    bool on;
    uint32_t buttons;     // held by "pad" (BTN_LT / BTN_RT = full trigger)
    float buttonsLeft;    // seconds; < 0 = hold
    float lx, ly, rx, ry;
    bool walking, toCar, toNpc;
    float wx, wz, wspeed, wtime;
    bool wst;              // "wstick x,y,heading": the stick held relative to a heading in the world, whatever the camera does
    float wsx, wsy, wsh;
} T;

// "routeadd x,z;x,z;..." then "route <m/s>" ("route 0" stops): drive his car along those points - the pretend pad's
// left stick steers towards the next point, RT / LT hold the speed (the buttons of "pad" still work on top)
static struct {
    float x[160], z[160];
    int n, i;
    float speed, v, logT, lastR;
    bool on, have;
    float lx, lz;
    uint32_t buttons;
} R;

// (test, m6 "pace") a driver for his car from elsewhere: steering, gas and brake 0..1 over the pretend pad's (its buttons kept)
static struct { bool on; float steer, gas, brake; } s_drive;
static void TestApply();
void Debug_SetDrive(bool on, float steer, float gas, float brake)
{
    if (on) T.on = true;
    s_drive.on = on, s_drive.steer = steer, s_drive.gas = gas, s_drive.brake = brake;
    TestApply();
}

static void TestApply()
{
    uint32_t b = T.buttons | R.buttons;
    if (s_drive.on) {
        Input_SetFake(true, (WORD)(b & 0xFFFF), (BYTE)(s_drive.brake * 255.0f), (BYTE)(s_drive.gas * 255.0f), s_drive.steer, 0.0f, T.rx, T.ry);
        return;
    }
    Input_SetFake(T.on, (WORD)(b & 0xFFFF), (b & BTN_LT) ? 255 : 0, (b & BTN_RT) ? 255 : 0, T.lx, T.ly, T.rx, T.ry);
}

static bool NearestVehicle(const float* from, float* out, char* name)
{
    float pos[30][3];
    char names[30][24];
    int n = Game_ListVehicles(pos, names, 30);
    float best = 1e9f;
    for (int i = 0; i < n; i++) {
        float dx = pos[i][0] - from[0], dz = pos[i][2] - from[2], d = dx * dx + dz * dz;
        if (d < best) {
            best = d;
            memcpy(out, pos[i], 12);
            strcpy(name, names[i]);
        }
    }
    return best < 1e9f;
}

static bool NearestCharacter(const float* from, float* out)
{
    float pos[63][3];
    int n = Game_ListCharacters(pos, 63);
    float best = 1e9f;
    for (int i = 0; i < n; i++) {
        float dx = pos[i][0] - from[0], dz = pos[i][2] - from[2], d = dx * dx + dz * dz;
        if (d < best) {
            best = d;
            memcpy(out, pos[i], 12);
        }
    }
    return best < 1e9f;
}

static float g_whereR = 60.0f;   // "where [radius]": the cars listed (60 m)
static void TestWhere(const GameState& gs)
{
    float p[3] = {}, gw[16], fov = 0;
    bool havePos = Game_PlayerPosition(gs.player, p);
    bool haveCam = Game_GetGameCamera(gw, &fov);
    Log("where: pos (%.2f %.2f %.2f)%s inCar %d onFoot %d context %d input 0x%X camera heading %.2f", p[0], p[1], p[2],
        havePos ? "" : " (none)", gs.inCar, gs.onFoot, gs.context, gs.inputState,
        haveCam ? atan2f(gw[8], gw[10]) : 0.0f);
    char info[200];
    Input_DebugInfo(info, sizeof(info));
    Log("where: %s", info);
    float pos[30][3];
    char names[30][24];
    int n = Game_ListVehicles(pos, names, 30);
    for (int i = 0; i < n; i++) {
        float dx = pos[i][0] - p[0], dz = pos[i][2] - p[2], d = sqrtf(dx * dx + dz * dz);
        float f[16];
        float hd = Game_NearestVehicleFrame(pos[i], 0.5f, f) ? atan2f(f[8], f[10]) : 9.0f;
        if (d < g_whereR) Log("where:   vehicle %-12s (%.1f %.1f %.1f) %.1f m heading %+.3f", names[i], pos[i][0], pos[i][1], pos[i][2], d, hd);
    }
    n = Game_ListCharacters(pos, 30);
    for (int i = 0; i < n; i++) {
        float dx = pos[i][0] - p[0], dz = pos[i][2] - p[2], d = sqrtf(dx * dx + dz * dz);
        if (d < 30.0f) Log("where:   character (%.1f %.1f %.1f) %.1f m", pos[i][0], pos[i][1], pos[i][2], d);
    }
}

static bool g_wantWhere;

static bool TestCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "fakepad")) {
        memset(&T, 0, sizeof(T));
        T.on = atoi(arg) != 0;
    } else if (!strcmp(cmd, "pad")) {
        char buf[64];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char* colon = strchr(buf, ':');
        float secs = colon ? (float)atof(colon + 1) : 0.2f;
        if (colon) *colon = 0;
        T.on = true;
        T.buttons = ParseButtons(buf, 0);
        T.buttonsLeft = secs > 0 ? secs : -1.0f;
    } else if (!strcmp(cmd, "lstick") || !strcmp(cmd, "rstick")) {
        float x = 0, y = 0;
        sscanf(arg, "%f,%f", &x, &y);
        T.on = true;
        if (cmd[0] == 'l') {
            T.lx = x;
            T.ly = y;
            T.walking = T.wst = false;
        } else {
            T.rx = x;
            T.ry = y;
        }
    } else if (!strcmp(cmd, "wstick")) {   // (lab) "wstick x,y,heading": the left stick as if the camera looked along that heading (G.yaw's
                                           // convention) - the same walk filmed from any debug orbit camera
        float x = 0, y = 0, h = 0;
        sscanf(arg, "%f,%f,%f", &x, &y, &h);
        T.on = true;
        T.walking = false;
        T.wst = x != 0.0f || y != 0.0f;
        T.wsx = x, T.wsy = y, T.wsh = h;
        if (!T.wst) T.lx = T.ly = 0;
    } else if (!strcmp(cmd, "walkto")) {
        T.on = true;
        T.walking = true;
        T.wst = false;
        T.wtime = 0;
        T.wspeed = 1.0f;
        T.toCar = T.toNpc = false;
        if (!strncmp(arg, "car", 3)) {
            T.toCar = true;
            if (arg[3] == ',') T.wspeed = (float)atof(arg + 4);
        } else if (!strncmp(arg, "npc", 3)) {
            T.toNpc = true;
            if (arg[3] == ',') T.wspeed = (float)atof(arg + 4);
        } else {
            sscanf(arg, "%f,%f,%f", &T.wx, &T.wz, &T.wspeed);
        }
    } else if (!strcmp(cmd, "where")) {
        g_wantWhere = true;
        g_whereR = arg[0] ? (float)atof(arg) : 60.0f;
    } else if (!strcmp(cmd, "routeadd")) {
        for (const char* q = arg; q && *q && R.n < 160;) {
            float x, z;
            if (sscanf(q, "%f,%f", &x, &z) != 2) break;
            R.x[R.n] = x, R.z[R.n] = z, R.n++;
            q = strchr(q, ';');
            if (q) q++;
        }
        Log("route: %d points", R.n);
    } else if (!strcmp(cmd, "route")) {   // route <m/s>[,<how near the last point counts as there>]
        R.speed = (float)atof(arg);
        R.lastR = 7.0f;
        if (const char* c = strchr(arg, ',')) R.lastR = (float)atof(c + 1);
        R.on = R.speed > 0.0f && R.n > 0;
        R.buttons = 0;
        if (!R.on) T.lx = 0, R.n = R.i = 0;
        R.have = false;
        T.on = true;
        Log("route: %s at %.1f m/s, %d points", R.on ? "driving" : "stopped", R.speed, R.n);
    } else {
        return false;
    }
    TestApply();
    return true;
}

void Debug_TestTick(float dt, const GameState& gs)
{
    if (g_wantWhere) {
        g_wantWhere = false;
        TestWhere(gs);
    }
    if (!T.on) return;
    if (R.on) {
        float p[3], f[16];
        if (!gs.inCar || !Game_PlayerPosition(gs.player, p) || !Game_NearestVehicleFrame(p, 3.0f, f)) {
            R.buttons = 0;
        } else {
            if (R.have && dt > 0.0f) {
                float sp = sqrtf((p[0] - R.lx) * (p[0] - R.lx) + (p[2] - R.lz) * (p[2] - R.lz)) / dt;
                R.v += (fminf(sp, 60.0f) - R.v) * (1.0f - expf(-dt * 6.0f));
            }
            R.lx = p[0], R.lz = p[2], R.have = true;
            if (R.i == 0) {   // start from the point nearest to the car (and the one after it, if that's ahead)
                float bd = 1e9f;
                for (int k = 0; k < R.n; k++) {
                    float d = (R.x[k] - p[0]) * (R.x[k] - p[0]) + (R.z[k] - p[2]) * (R.z[k] - p[2]);
                    if (d < bd) bd = d, R.i = k;
                }
                if (R.i == 0) R.i = 1;
                Log("route: starting at point %d (%.1f, %.1f)", R.i, R.x[R.i], R.z[R.i]);
            }
            while (R.i < R.n && (R.x[R.i] - p[0]) * (R.x[R.i] - p[0]) + (R.z[R.i] - p[2]) * (R.z[R.i] - p[2]) < (R.i == R.n - 1 ? R.lastR * R.lastR : 49.0f)) R.i++;
            if (R.i >= R.n) {   // the end: braked to a stop (m3.1), then let go
                T.lx = 0;
                R.buttons = R.v > 0.4f ? BTN_LT : 0;
                if (!R.buttons) {
                    R.on = false;
                    Log("route: end (%.1f, %.1f)", p[0], p[2]);
                }
            } else {
                float head = atan2f(f[8], f[10]), want = atan2f(R.x[R.i] - p[0], R.z[R.i] - p[2]);
                float e = fmodf(want - head + 3.14159265f, 6.28318531f);
                if (e < 0) e += 6.28318531f;
                e -= 3.14159265f;
                T.lx = fmaxf(-1.0f, fminf(1.0f, e * 2.2f));
                float target = R.speed * (fabsf(e) > 0.5f ? 0.6f : 1.0f);   // slower through the turns
                R.buttons = R.v < target - 0.5f ? BTN_RT : R.v > target + 3.0f ? BTN_LT : 0;
                if ((R.logT += dt) >= 1.0f) {
                    R.logT = 0;
                    Log("route: point %d of %d, at (%.1f, %.1f) %.1f m/s, heading %+.2f, steer %+.2f", R.i, R.n, p[0], p[2], R.v, head, T.lx);
                }
            }
        }
    }
    if (T.buttonsLeft > 0 && (T.buttonsLeft -= dt) <= 0) {
        T.buttons = 0;
        T.buttonsLeft = 0;
    }
    if (T.wst) {   // (the world's move: wsx along the heading's right, wsy along its forward; read in the stick's frame this frame)
        float d = T.wsh - Gun_StickHeading(), c = cosf(d), s = sinf(d);
        T.lx = T.wsx * c + T.wsy * s;
        T.ly = -T.wsx * s + T.wsy * c;
    }
    if (T.walking) {
        float p[3], gw[16], fov;
        T.wtime += dt;
        if (!gs.onFoot || !Game_PlayerPosition(gs.player, p) || !Game_GetGameCamera(gw, &fov) || T.wtime > 20.0f) {
            Log("walkto: stopped (%s)", T.wtime > 20.0f ? "gave up after 20 s" : "not on foot");
            T.walking = false;
            T.lx = T.ly = 0;
        } else {
            char name[24] = "";
            float tgt[3] = {T.wx, 0, T.wz};
            if (T.toCar && !NearestVehicle(p, tgt, name)) tgt[0] = p[0], tgt[2] = p[2];
            if (T.toNpc && !NearestCharacter(p, tgt)) tgt[0] = p[0], tgt[2] = p[2];
            float dx = tgt[0] - p[0], dz = tgt[2] - p[2], d = sqrtf(dx * dx + dz * dz);
            float stop = T.toCar ? 2.4f : T.toNpc ? 1.2f : 0.8f;
            if (d < stop) {
                Log("walkto: arrived %.2f m from (%.1f, %.1f)%s%s after %.1f s", d, tgt[0], tgt[2], name[0] ? " " : "", name, T.wtime);
                T.walking = false;
                T.lx = T.ly = 0;
            } else {
                // the stick moves him relative to the camera: up = its forward, right = its right
                float fx = gw[8], fz = gw[10], rx = gw[0], rz = gw[2];
                float fl = sqrtf(fx * fx + fz * fz), rl = sqrtf(rx * rx + rz * rz);
                if (fl < 1e-4f || rl < 1e-4f) return;
                float sx = (dx * rx + dz * rz) / (d * rl), sy = (dx * fx + dz * fz) / (d * fl);
                float sl = sqrtf(sx * sx + sy * sy);
                float k = T.wspeed * fminf(1.0f, 0.4f + d / 3.0f) / fmaxf(sl, 1e-4f);   // ease in at the end
                T.lx = sx * k;
                T.ly = sy * k;
            }
        }
    }
    TestApply();
}

void Debug_Poll(DebugCmds* out)
{
    out->tab = -1;
    out->markers = -1;
    double now = Time_Real();
    if (now >= s_nextPoll) {
        s_nextPoll = now + 0.2;
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%lsdebug_cmd.txt", g_modDir);
        FILE* f = _wfopen(path, L"r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char cmd[32] = {0}, arg[200] = {0};
                if (sscanf(line, "%31s %199s", cmd, arg) < 1) continue;
                Log("debug cmd: %s %s", cmd, arg);
                if (!strcmp(cmd, "open")) s_force = true;
                else if (!strcmp(cmd, "close")) { s_force = false; s_stick = false; }
                else if (!strcmp(cmd, "stick")) { s_stick = true; s_angle = (float)atof(arg); }
                else if (!strcmp(cmd, "nostick")) s_stick = false;
                else if (!strcmp(cmd, "tab")) out->tab = atoi(arg) == 1 ? 1 : 0;   // (anything else indexed past the two pages)
                else if (!strcmp(cmd, "cycle")) out->cycle = arg[0] ? atoi(arg) : 1;
                else if (!strcmp(cmd, "probe")) out->probe = arg[0] ? atoi(arg) : 1;
                else if (!strcmp(cmd, "hud")) Scene_HudHide(arg[0] && atoi(arg) == 0);   // (lab: "hud 0" hides the game's HUD, "hud 1" back)
                else if (!strcmp(cmd, "carprobe")) Scene_CarProbe(arg[0] ? atoi(arg) : 1);
                else if (!strcmp(cmd, "texdump")) Scene_TexDump(arg);
                else if (!strcmp(cmd, "markers")) out->markers = arg[0] ? atoi(arg) : 1;
                else if (!strcmp(cmd, "poke")) AddPoke(line);
                else if (!strcmp(cmd, "unpoke")) s_npokes = 0;
                else if (Kbm_DebugCmd(cmd, arg)) {
                }
                else if (TestCmd(cmd, arg)) {
                }
                else if (Lab_DebugCmd(cmd, arg)) {   // (lab.cpp) the Weapons Lab's hooks
                }
                else if (!strcmp(cmd, "fpscap")) {
                    extern float g_labFpsCap;
                    g_labFpsCap = (float)atof(arg);
                    Log("fpscap: %.0f frames a second at most (0 = off)", g_labFpsCap);
                }
                else if (!strcmp(cmd, "rec")) out->rec = arg[0] ? (float)atof(arg) : 10.0f, out->recFull = strstr(arg, "full") != nullptr;
                else if (!strcmp(cmd, "shot")) {
                    out->screenshot = true;
                    swprintf(out->shotPath, MAX_PATH, L"%ls%hs", g_modDir, arg[0] ? arg : "shot.bmp");
                }
                else Gun_DebugCmd(cmd, arg);
            }
            fclose(f);
            _wremove(path);
        }
    }
    out->forceOpen = s_force;
    out->haveStick = s_stick;
    out->stickAngle = s_angle;
}
