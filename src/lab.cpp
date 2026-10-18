// Weapons Lab: test hooks for the lab tools on the Mac (tools/lab), only with Debug=1. Nothing here
// runs for players.
//   labwarp <x>,<y>,<z>[,<heading>]   put him there at once (the game's Character::RelocateAndReset) - a
//                                     test stage's start, so a test doesn't walk across the level first
//   labinfo                           where he is, which way he faces, the drawn camera
//   labscan [<cols>,<rows>]           coverage scan: along a grid of rays through the picture, the drawn
//                                     surface (the depth buffer) against what a bullet meets (TraceShot,
//                                     and a car's drawn body) - where the two disagree, shots go through
//                                     something you can see, or stop at something you can't
//   labhit                            what is under the crosshair, in detail: the registry entry for it
#include "hit.h"
#include "render.h"

static Renderer* s_r;   // the wheel's renderer (wheel.cpp hands it over each frame)

static uint32_t RdU32(uint32_t a)
{
    if (a < 0x10000) return 0;
    uint32_t v = 0;
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)a, &v, 4, &got) || got != 4) return 0;
    return v;
}

static bool RdMem(uint32_t a, void* out, int n)
{
    if (a < 0x10000) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)a, out, n, &got) && got == (SIZE_T)n;
}

// ------------------------------------------------------------------ warp
// Character::RelocateAndReset (0x4F38D0): stdcall(Character*, const Vec3* feet, float heading, bool, bool reset),
// ret 0x14. The heading goes to Character+0x110 (the desired direction: atan2(x, z) of where he faces, + pi).
typedef void(__stdcall* RelocateFn)(uint32_t ch, const Vec3* pos, float heading, int keep, int reset);
static const RelocateFn Relocate = (RelocateFn)0x4F38D0;

static uint32_t Player()
{
    uint32_t cm = RdU32(0x6C8470);
    return cm ? RdU32(cm + 192) : 0;
}

// (drunk mode: his game body moved to where the ragdoll got up - ragdoll.inc)
bool Lab_Relocate(float x, float y, float z, float heading)
{
    uint32_t ch = Player();
    if (!ch || !Game_IsEnglish() || RdU32(ch + 0x128)) return false;
    Vec3 p = {x, y, z};
    Relocate(ch, &p, heading, 1, 1);
    return true;
}

static void Warp(const char* arg)
{
    Vec3 p = {};
    float heading = 0;
    int n = sscanf(arg, "%f,%f,%f,%f", &p.x, &p.y, &p.z, &heading);
    uint32_t ch = Player();
    if (n < 3 || !ch || !Game_IsEnglish()) {
        Log("labwarp: needs x,y,z[,heading] and a player on foot (English release)");
        return;
    }
    if (RdU32(ch + 0x128)) {
        Log("labwarp: he is in a car");
        return;
    }
    Relocate(ch, &p, heading, 1, 1);
    float now[3];
    Game_PlayerPosition(ch, now);
    Log("labwarp: (%.2f %.2f %.2f) heading %.3f -> now (%.2f %.2f %.2f)", p.x, p.y, p.z, heading, now[0], now[1], now[2]);
}

static void Info()
{
    uint32_t ch = Player();
    float pos[3] = {};
    if (!ch || !Game_PlayerPosition(ch, pos)) {
        Log("labinfo: no player");
        return;
    }
    float desired = 0;
    RdMem(ch + 0x110, &desired, 4);
    float view[16], proj[16];
    char cam[160] = "no camera";
    if (Game_GetCamera(view, proj)) {
        Mat4 v;
        memcpy(v.m, view, 64);
        Mat4 cw = RigidInverse(v);
        Vec3 e = Row(cw, 3), f = Row(cw, 2);
        snprintf(cam, sizeof(cam), "eye (%.2f %.2f %.2f) fwd (%.3f %.3f %.3f) fov %.1f deg", e.x, e.y, e.z, f.x, f.y, f.z,
                 2.0f * atanf(1.0f / proj[0]) * 57.29578f);
    }
    Log("labinfo: player %08X (%s) at (%.2f %.2f %.2f) desired dir %.3f, in car %d | %s", ch, Game_ClassName(ch), pos[0],
        pos[1], pos[2], desired, RdU32(ch + 0x128) ? 1 : 0, cam);
}

// ------------------------------------------------------------------ the registry entry for one hit
// What a shot meets, as much as the game says about it: the kind, the object's class, its collision volume, the
// building's / prop's attributes (sound name, particle, break type, mass, class), or the ground's terrain type.
static void DescribeObject(const HitResult& h, const char* tag)
{
    char line[220];
    Trace_Describe(h, line, sizeof(line));
    Log("%s: %s | surface %d", tag, line, (int)Hit_Surface(h));
    if (h.kind == HIT_WORLD && h.object && h.ground < 0) {
        uint32_t vt = RdU32(h.object);
        uint32_t attr = RdU32(h.object + 0x30);
        char snd[24] = {0};
        int particle = -1, brk = -1, cls = -1;
        float mass = 0;
        if (attr) {
            uint32_t sp = RdU32(attr + 0x08);
            if (sp) RdMem(sp, snd, 23);
            RdMem(attr + 0x48, &particle, 4);
            RdMem(attr + 0x4C, &brk, 4);
            RdMem(attr + 0x50, &mass, 4);
            RdMem(attr + 0x5C, &cls, 4);
        }
        for (char& c : snd)
            if (c && (c < 32 || c > 126)) c = '?';
        uint32_t raw[12] = {};
        RdMem(h.object, raw, sizeof(raw));
        Log("%s: object vtable %08X attributes %08X (sound '%s', particle %d, break %d, mass %.1f, class %d) | +00: %08X %08X %08X "
            "%08X %08X %08X %08X %08X %08X %08X %08X %08X",
            tag, vt, attr, snd, particle, brk, mass, cls, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8],
            raw[9], raw[10], raw[11]);
    }
}

static void CentreRay(Vec3* eye, Vec3* fwd)
{
    float view[16], proj[16];
    *eye = *fwd = V3(0, 0, 0);
    if (!Game_GetCamera(view, proj)) return;
    Mat4 v;
    memcpy(v.m, view, 64);
    Mat4 cw = RigidInverse(v);
    *eye = Row(cw, 3);
    *fwd = Norm(Row(cw, 2));
}

static void HitHere()
{
    Vec3 eye, fwd;
    CentreRay(&eye, &fwd);
    if (Len(fwd) < 0.5f) {
        Log("labhit: no camera");
        return;
    }
    HitResult h;
    TraceShot(eye, eye + fwd * 150.0f, Player(), &h);
    if (h.kind == HIT_VEHICLE && h.object) {
        CarHit ch;
        if (CarGlass_Raycast(h.object, eye, fwd, h.dist + 8.0f, &ch))
            Log("labhit: the car as drawn at %.2f m (%s)", Len(ch.pos - eye), ch.glass ? "glass" : "paint");
        TireHit th;
        if (Tires_Raycast(eye, fwd, h.dist + 1.0f, &th)) Log("labhit: tire %d of %08X at %.2f m", th.wheel, th.veh, Len(th.pos - eye));
    }
    DescribeObject(h, "labhit");
}

// ------------------------------------------------------------------ coverage scan
// A grid of screen pixels. For each: the drawn surface's depth (two passes of the depth probe: 1-150 m, then a
// fine pass around that), then a bullet's trace along the same ray. Logged per cell for tools/lab/scan.py.
enum { kScanCells = 64 * 48 };
static struct Scan {
    int state;              // 0 idle, 1 coarse depth, 2 fine depth, 3 traces
    int cols, rows, n;
    int next;               // next cell to draw / trace
    int batch0, batchN;     // the batch in flight (drawn, read at Present)
    bool pending;
    float px[kScanCells][2];
    float coarse[kScanCells], fine[kScanCells];
    float view[16], proj[16];
    double t0;
    float drift;            // how far the camera moved during the scan (metres + radians)
    int frames;
} S;

bool Lab_Wants() { return S.state == 1 || S.state == 2; }

static void ScanStart(const char* arg)
{
    int cols = 48, rows = 27;
    if (arg[0]) sscanf(arg, "%d,%d", &cols, &rows);
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    while (cols * rows > kScanCells) cols--, rows = rows > 2 ? rows - 1 : rows;
    if (!s_r || !s_r->W || !s_r->H || !Game_GetCamera(S.view, S.proj)) {
        Log("labscan: no picture or camera yet");
        return;
    }
    S.cols = cols;
    S.rows = rows;
    S.n = cols * rows;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            // cell centres, nudged off the exact screen centre (the crosshair's own probe pixel)
            float x = floorf((c + 0.5f) * s_r->W / cols) + 0.0f, y = floorf((r + 0.5f) * s_r->H / rows);
            if (fabsf(x - s_r->W * 0.5f) < 1 && fabsf(y - s_r->H * 0.5f) < 1) x += 2;
            S.px[r * cols + c][0] = x;
            S.px[r * cols + c][1] = y;
        }
    S.state = 1;
    S.next = 0;
    S.pending = false;
    S.drift = 0;
    S.frames = 0;
    S.t0 = Time_Real();
    Log("labscan: start %d x %d cells on %d x %d", cols, rows, s_r->W, s_r->H);
}

// inside the game's scene, after the world, before anything of ours writes depth
void Lab_OnWorldDrawn(Renderer& r)
{
    if (!Lab_Wants() || S.pending || S.next >= S.n) return;
    float view[16], proj[16];
    if (!Game_GetCamera(view, proj)) return;
    float d = 0;
    for (int i = 0; i < 16; i++) d = fmaxf(d, fabsf(view[i] - S.view[i]));
    S.drift = fmaxf(S.drift, d);
    int k = S.n - S.next;
    if (k > Renderer::kScanMax) k = Renderer::kScanMax;
    static float range[Renderer::kScanMax][2];
    for (int i = 0; i < k; i++) {
        float c = S.coarse[S.next + i];
        if (S.state == 2 && c > 0) {   // fine: 128 steps across +-6 % (+10 cm) of the coarse reading
            range[i][0] = fmaxf(0.3f, c * 0.94f - 0.1f);
            range[i][1] = c * 1.06f + 0.1f;
        } else
            range[i][0] = range[i][1] = 0;
    }
    if (r.ScanDraw(&S.px[S.next], k, proj[10], proj[14], S.state == 2 ? range : nullptr)) {
        S.batch0 = S.next;
        S.batchN = k;
        S.pending = true;
    }
}

static void TraceCells(uint32_t player)
{
    Mat4 v;
    memcpy(v.m, S.view, 64);
    Mat4 cw = RigidInverse(v);
    Vec3 eye = Row(cw, 3), R = Row(cw, 0), U = Row(cw, 1), F = Row(cw, 2);
    float feet[3] = {};
    bool havePlayer = player && Game_PlayerPosition(player, feet);
    Log("labscan: camera eye (%.3f %.3f %.3f) fwd (%.4f %.4f %.4f) p0 %.5f p5 %.5f | %d x %d on %d x %d | drift %.4f | player (%.2f %.2f %.2f)",
        eye.x, eye.y, eye.z, F.x, F.y, F.z, S.proj[0], S.proj[5], S.cols, S.rows, s_r->W, s_r->H, S.drift, feet[0], feet[1], feet[2]);
    int counts[6] = {}, counts2[6] = {};
    for (int i = 0; i < S.n; i++) {
        float nx = S.px[i][0] / s_r->W * 2.0f - 1.0f, ny = 1.0f - S.px[i][1] / s_r->H * 2.0f;   // (Direct3D 8: pixel centres on whole numbers)
        Vec3 dv = R * (nx / S.proj[0]) + U * (ny / S.proj[5]) + F;   // view depth 1 along this ray
        float k = Len(dv);
        Vec3 dir = dv * (1.0f / k);
        // the drawn surface along the ray (metres from the eye), -1 none within 150 m
        float vz = S.fine[i] > 0 ? S.fine[i] : S.coarse[i];
        if (S.fine[i] == -2.0f) vz = S.coarse[i];   // (the fine range started behind it: keep the coarse reading)
        float vis = vz > 0 ? vz * k : -1.0f;
        // a bullet along it, traced the way an aimed shot is (from level with him: what is between the camera and his
        // back doesn't count)
        float skip = 0.0f;
        if (havePlayer) skip = fmaxf(0.0f, Dot(V3(feet[0], feet[1] + 1.25f, feet[2]) - eye, dir) - 0.5f);
        HitResult h;
        bool got = TraceShot(eye + dir * skip, eye + dir * 150.0f, player, &h);
        float col = got ? skip + h.dist : -1.0f;
        int glass = 0;
        if (got && h.kind == HIT_VEHICLE && h.object) {
            CarHit ch;
            if (CarGlass_Raycast(h.object, eye, dir, col + 8.0f, &ch)) {
                col = Len(ch.pos - eye);
                glass = ch.glass ? 1 : 0;
                h.pos = ch.pos;
            }
        }
        // is the drawn surface him?
        int me = 0;
        if (havePlayer && vis > 0) {
            Vec3 p = eye + dir * vis;
            float dx = p.x - feet[0], dz = p.z - feet[2];
            if (dx * dx + dz * dz < 0.75f * 0.75f && p.y > feet[1] - 0.15f && p.y < feet[1] + 2.3f) me = 1;
        }
        // M match, P passes through what is drawn, I stops in front of it (unseen), S sky both, H him
        auto classify = [&](float c) {
            float tol = 0.12f + 0.03f * (vis > 0 ? vis : 0);
            if (me) return 'H';
            if (vis < 0 && c < 0) return 'S';
            if (vis < 0) return c > 140.0f ? 'S' : 'I';
            if (c < 0) return vis > 140.0f ? 'S' : 'P';
            if (c > vis + tol) return 'P';
            if (c < vis - tol) return 'I';
            return 'M';
        };
        char cls = classify(col);
        // and with visual cover (vcover.cpp): what the same rule makes of it
        char after = cls;
        int dec = me ? 0 : VCover_Decide(eye, dir, vis, skip, h, player);
        if (dec == 1 || dec >= 4) after = classify(vis);
        else if (dec == 2) {   // past the person: what is behind them
            Trace_SetIgnore4(h.object);
            HitResult h2;
            bool got2 = TraceShot(h.pos, eye + dir * 150.0f, player, &h2);
            Trace_SetIgnore4(0);
            float c2 = got2 ? Dot(h2.pos - eye, dir) : -1.0f;
            if (VCover_Decide(eye, dir, vis, skip, h2, player) == 1) c2 = vis;
            after = classify(c2);
        }
        counts[cls == 'M' ? 0 : cls == 'P' ? 1 : cls == 'I' ? 2 : cls == 'S' ? 3 : 4]++;
        counts2[after == 'M' ? 0 : after == 'P' ? 1 : after == 'I' ? 2 : after == 'S' ? 3 : 4]++;
        Log("LS %d %d %d %c%c %.3f %.3f %d %d %08X %X %d %d %.2f %.2f %.2f", i, (int)S.px[i][0], (int)S.px[i][1], cls, after, vis, col,
            (int)h.kind, h.volume, h.object, h.aiRef, h.ground, glass, h.normal.x, h.normal.y, h.normal.z);
    }
    Log("labscan: done in %.1f s, %d frames: %d match, %d pass through, %d unseen stop, %d sky, %d him | with visual cover: %d match, "
        "%d pass through, %d unseen stop", Time_Real() - S.t0, S.frames, counts[0], counts[1], counts[2], counts[3], counts[4], counts2[0],
        counts2[1], counts2[2]);
}

// at Present: read back the batch drawn this frame; once every cell has its depth, trace them all
void Lab_OnPresent(Renderer& r, uint32_t player)
{
    s_r = &r;
    if (!S.state) return;
    S.frames++;
    if (S.pending) {
        static float d[Renderer::kScanMax];
        if (r.ScanRead(d, S.batchN)) {
            for (int i = 0; i < S.batchN; i++) (S.state == 1 ? S.coarse : S.fine)[S.batch0 + i] = d[i];
            S.next = S.batch0 + S.batchN;
        }
        S.pending = false;
    }
    if (S.state <= 2 && S.next >= S.n) {
        if (S.state == 1) {
            S.state = 2;
            S.next = 0;
        } else
            S.state = 3;
    }
    if (S.state == 3) {
        TraceCells(player);
        S.state = 0;
    }
    if (S.state && S.frames > 600) {
        Log("labscan: gave up after %d frames (cell %d of %d, state %d)", S.frames, S.next, S.n, S.state);
        S.state = 0;
    }
}

// ------------------------------------------------------------------ one ray, in detail
// "labray <px>,<py>[,<metres>]": the scan's ray through that pixel of the last frame's camera, traced with the
// trace log on (every piece, the objects listed, the volume hit) - why a cell came out the way it did
extern float g_traceFillPad;
static void RayAt(const char* arg)
{
    float px = 0, py = 0, range = 150.0f;
    float view[16], proj[16];
    if (sscanf(arg, "%f,%f,%f", &px, &py, &range) < 2 || !s_r || !Game_GetCamera(view, proj)) {
        Log("labray: needs px,py and a camera");
        return;
    }
    Mat4 v;
    memcpy(v.m, view, 64);
    Mat4 cw = RigidInverse(v);
    Vec3 eye = Row(cw, 3);
    float nx = px / s_r->W * 2.0f - 1.0f, ny = 1.0f - py / s_r->H * 2.0f;
    Vec3 dir = Norm(Row(cw, 0) * (nx / proj[0]) + Row(cw, 1) * (ny / proj[5]) + Row(cw, 2));
    HitResult h;
    g_traceLog = 1;
    TraceShot(eye, eye + dir * range, Player(), &h);
    g_traceLog = 0;
    DescribeObject(h, "labray");
}

// ------------------------------------------------------------------ a collision object's volumes
// "labvols <hex volume>": the volume tree under a collision object's root volume (CollisionObject +0x20). Every
// volume: +0x08 centre, +0x14 half sizes (its box), +0x24 type (1 sphere, 2 cylinder, 3 oriented box, 4 wall,
// 5 box of children); +0x34 its children: a list with the count at +0x0C and the pointers at +0x10. Leaves are
// logged with their shape for tools/lab/registry.py.
static void Vols(uint32_t v, int depth, int* n)
{
    if (!v || depth > 8 || *n > 600) return;
    float f[32] = {};
    if (!RdMem(v, f, sizeof(f))) return;
    int type = 0;
    RdMem(v + 0x24, &type, 4);
    uint32_t list = RdU32(v + 0x34);
    int count = list ? (int)RdU32(list + 0x0C) : 0;
    uint32_t data = list ? RdU32(list + 0x10) : 0;
    (*n)++;
    if (type == 3)
        Log("LV %d %08X obb c %.3f %.3f %.3f ax %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f h %.3f %.3f %.3f", depth, v, f[2], f[3], f[4],
            f[19], f[20], f[21], f[22], f[23], f[24], f[25], f[26], f[27], f[28], f[29], f[30]);
    else if (type == 2)
        Log("LV %d %08X cyl c %.3f %.3f %.3f axis %.4f %.4f %.4f halflen %.3f r %.3f", depth, v, f[2], f[3], f[4], f[19], f[20], f[21],
            f[22], f[23]);
    else
        Log("LV %d %08X type%d c %.3f %.3f %.3f h %.3f %.3f %.3f r %.3f children %d", depth, v, type, f[2], f[3], f[4], f[5], f[6], f[7],
            f[8], count);
    if (count > 0 && count < 400 && data)
        for (int i = 0; i < count; i++) Vols(RdU32(data + 4 * i), depth + 1, n);
}

// ------------------------------------------------------------------ commands
bool Lab_DebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "labwarp")) Warp(arg);
    else if (!strcmp(cmd, "labinfo")) Info();
    else if (!strcmp(cmd, "labscan")) ScanStart(arg);
    else if (!strcmp(cmd, "labhit")) HitHere();
    else if (!strcmp(cmd, "labray")) RayAt(arg);
    else if (!strcmp(cmd, "labvols")) {
        int n = 0;
        Vols(strtoul(arg, nullptr, 16), 0, &n);
        Log("labvols: %d volumes", n);
    }
    else if (!strcmp(cmd, "labmem")) {   // "labmem <hex address>,<dwords>": a dump as hex and floats (reverse engineering)
        uint32_t a = 0;
        int n = 32;
        sscanf(arg, "%x,%d", &a, &n);
        if (n > 256) n = 256;
        for (int i = 0; i < n; i += 4) {
            uint32_t w[4] = {};
            if (!RdMem(a + i * 4, w, 16)) {
                Log("labmem %08X: unreadable", a + i * 4);
                break;
            }
            float f[4];
            memcpy(f, w, 16);
            Log("labmem %08X: %08X %08X %08X %08X | %10.3f %10.3f %10.3f %10.3f", a + i * 4, w[0], w[1], w[2], w[3], f[0], f[1], f[2], f[3]);
        }
    }
    else if (!strcmp(cmd, "labstatics")) {   // "labstatics x,y,z,r" (default: round him, 15 m)
        extern void Trace_LogStatics(Vec3 c, float r);
        Vec3 c = {};
        float r = 15.0f;
        if (sscanf(arg, "%f,%f,%f,%f", &c.x, &c.y, &c.z, &r) < 3) {
            float p[3] = {};
            Game_PlayerPosition(Player(), p);
            c = V3(p[0], p[1], p[2]);
        }
        Trace_LogStatics(c, r);
    }
    else if (!strcmp(cmd, "groundmap")) {   // "groundmap x0,z0,x1,z1,step[,y]": the terrain's height over a grid (LG lines) - slopes, kerbs
        float x0, z0, x1, z1, st = 2.0f, y = 50.0f;
        if (sscanf(arg, "%f,%f,%f,%f,%f,%f", &x0, &z0, &x1, &z1, &st, &y) < 4) return true;
        if (st < 0.25f) st = 0.25f;
        int n = 0;
        for (float z = z0; z <= z1 + 1e-3f && n < 4000; z += st) {
            char line[1800];
            int o = snprintf(line, sizeof(line), "LG %.2f", z);
            for (float x = x0; x <= x1 + 1e-3f && o < (int)sizeof(line) - 12; x += st, n++) {
                float gy;
                o += snprintf(line + o, sizeof(line) - o, Trace_GroundY(V3(x, y, z), &gy) ? " %.2f" : " -", gy);
            }
            Log("%s", line);
        }
        Log("groundmap: %d points, x %.1f..%.1f z %.1f..%.1f step %.2f", n, x0, x1, z0, z1, st);
    }
    else if (!strcmp(cmd, "labpad")) {   // the trace's list fill reach past each piece (default 0.5 m)
        g_traceFillPad = arg[0] ? (float)atof(arg) : 0.5f;
        Log("labpad: list fills reach %.2f m past each piece", g_traceFillPad);
    }
    else return false;
    return true;
}
