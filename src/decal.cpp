// Bullet holes (and scorch marks): decals left where shots land on buildings, walls and roads (like GTA), drawn
// into the game's 3D world with its camera and depth buffer, flat on the surface's plane, so
// they sit on the wall and things in front of them hide them. Any weapon leaves them through
// its WeaponSpec (holeSize). The newest kDecals stay; a new level clears them.
#include "render.h"
#include "math3d.h"
#include "hit.h"

static const int kDecals = 64;
static const float kDrawDist = 80.0f;   // metres
static struct Decal {
    Vec3 p, n, right, up;   // centre, surface normal, half-size axes on the surface
    bool pending;           // still at the collision point: to be put on the visible surface
    int kind;               // DECAL_HOLE, DECAL_SCORCH (-1: gone)
    uint32_t veh;           // in a car's bodywork (0: the world's)
    Vec3 lp, ln, lr, lu;    // ... then p, n, right, up in the car's own frame
    Vec3 vpos;              // where the car was last frame
    uint32_t seed;          // a crack's own pattern
} g_decals[kDecals];

// Holes in a car are kept in its own frame (its transform at Vehicle+184: rows 0-2 its axes, row 3
// its position) and put back into the world each frame, so they ride along with it. A car that
// leaves the game's active list (wrecked into a husk, driven off and taken back by the traffic) or
// is suddenly somewhere else (the traffic reuses its cars) takes its holes with it.
static bool VehicleFrame(uint32_t v, Mat4* m)
{
    if (!v || !Game_VehicleActive(v)) return false;
    memcpy(m->m, (const void*)(uintptr_t)(v + 184), 64);
    return true;
}

static Vec3 ToLocal(Vec3 w, const Mat4& m, bool point)
{
    Vec3 d = point ? w - Row(m, 3) : w;
    return V3(Dot(d, Row(m, 0)), Dot(d, Row(m, 1)), Dot(d, Row(m, 2)));
}

static Vec3 ToWorld(Vec3 l, const Mat4& m, bool point)
{
    Vec3 w = Row(m, 0) * l.x + Row(m, 1) * l.y + Row(m, 2) * l.z;
    return point ? w + Row(m, 3) : w;
}

static void Attach(Decal& d, uint32_t veh, const Mat4& m)
{
    d.veh = veh;
    d.lp = ToLocal(d.p, m, true);
    d.ln = ToLocal(d.n, m, false);
    d.lr = ToLocal(d.right, m, false);
    d.lu = ToLocal(d.up, m, false);
    d.vpos = Row(m, 3);
}

static int g_count, g_next;

// once a frame before the holes are used: the ones in cars follow their car
static int g_followFrame = -1;
static void FollowVehicles(int frame)
{
    if (frame == g_followFrame) return;
    g_followFrame = frame;
    for (int i = 0; i < g_count; i++) {
        Decal& d = g_decals[i];
        if (!d.veh || d.kind < 0) continue;
        Mat4 m;
        if (!VehicleFrame(d.veh, &m) || Len(Row(m, 3) - d.vpos) > 12.0f) {
            d.kind = -1;   // gone, or reused for another car somewhere else
            d.veh = 0;
            continue;
        }
        d.vpos = Row(m, 3);
        d.p = ToWorld(d.lp, m, true);
        d.n = ToWorld(d.ln, m, false);
        d.right = ToWorld(d.lr, m, false);
        d.up = ToWorld(d.lu, m, false);
    }
}

// The game's collision isn't its picture: a bay window's frame sticks out in front of the
// wall's collision box, so a hole there would hide behind the frame. A new hole is drawn at
// once at the collision point, and on the next frame the depth probe reads where the visible
// surface is along the camera's line through it (from 0.75 m in front to 0.25 m behind, about
// 1 cm steps); the hole moves there. Where the player is on screen, a reading that could be
// him (nearer than 0.3 m behind his chest) is thrown away; with nothing found in the range the
// hole stays where it is.
static const float kProbeFront = 0.75f, kProbeBack = 0.25f;
static struct Probe {
    int decal;
    Mat4 camWorld;    // the camera the probe was drawn with
    float px, py;     // pixel centre
    float sx, sy;     // projection scale (proj _11, _22)
    int w, h;
    float minDepth;   // readings nearer than this could be the player: thrown away
} g_probe[3];
static int g_nprobe;
static uint32_t g_level;   // the player Character* the holes belong to (a new one = a new level)
static uint32_t g_rng = 0x6C8E9CF5u;
int g_decalLog;   // debug "decallog <frames>"

static float Rnd01()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

void Decal_Track(uint32_t player)
{
    if (!player) {   // no player (loading, the menus): the next one starts clean, even at the same address
        if (g_level) CarGlass_Clear(), Tires_Clear();
        g_level = 0;
        g_count = g_next = 0;   // (and nothing of the last level is drawn meanwhile)
        return;
    }
    if (player != g_level) {
        g_level = player;
        g_count = g_next = 0;
    }
}

bool Decal_Any() { return g_count > 0; }

void Decal_Add(Vec3 p, Vec3 n, float size, int kind)
{
    n = Norm(n);
    if (Len(n) < 0.5f || size <= 0) return;
    Vec3 r = Norm(Cross(fabsf(n.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), n));
    Vec3 u = Cross(n, r);
    float a = Rnd01() * 6.2831853f, half = size * 0.5f * (0.85f + 0.3f * Rnd01());
    Decal& d = g_decals[g_next];
    d.p = p + n * 0.01f;   // just off the surface, so the wall can't cover it
    d.n = n;
    d.right = (r * cosf(a) + u * sinf(a)) * half;
    d.up = Cross(n, d.right);
    d.pending = true;
    d.kind = kind;
    d.veh = 0;
    d.seed = g_rng;
    g_next = (g_next + 1) % kDecals;
    if (g_count < kDecals) g_count++;
}

void Decal_AddOnVehicle(uint32_t veh, Vec3 p, Vec3 n, float size, int kind, bool exact)
{
    Mat4 m;
    if (!VehicleFrame(veh, &m)) return;
    int at = g_next;
    Decal_Add(p, n, size, kind);
    if (g_next == at) return;   // (not added)
    if (exact) g_decals[at].pending = false;   // (found on the car as drawn: no probe)
    Attach(g_decals[at], veh, m);
}

void Decal_RemoveOnVehicle(uint32_t veh, int kind)
{
    for (int i = 0; i < g_count; i++)
        if (g_decals[i].veh == veh && g_decals[i].kind == kind) g_decals[i].kind = -1, g_decals[i].veh = 0;
}

void Decal_RemoveOnVehicleIn(uint32_t veh, int kind, Vec3 lo, Vec3 hi)
{
    for (int i = 0; i < g_count; i++) {
        Decal& d = g_decals[i];
        if (d.veh == veh && d.kind == kind && d.lp.x >= lo.x && d.lp.x <= hi.x && d.lp.y >= lo.y && d.lp.y <= hi.y && d.lp.z >= lo.z &&
            d.lp.z <= hi.z)
            d.kind = -1, d.veh = 0;
    }
}

// Inside the game's scene, before anything of ours is drawn: up to `max` pixels to probe.
int Decal_ProbeWant(float (*px)[2], float (*range)[2], int max, int W, int H, uint32_t player)
{
    g_nprobe = 0;
    float v[16], pj[16];
    static int s_frame;
    FollowVehicles(++s_frame);
    if (!g_count || max <= 0 || !Game_GetCamera(v, pj)) return 0;
    Mat4 view, proj;
    memcpy(view.m, v, 64);
    memcpy(proj.m, pj, 64);
    Mat4 vp = Mul(view, proj);
    // the player's box on screen (feet to 1.9 m up, 0.35 m round) and his depth
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f, pp[3], pdepth = 0;
    if (Game_PlayerPosition(player, pp)) {
        pdepth = XformPoint(V3(pp[0], pp[1] + 1.0f, pp[2]), view).z;
        for (int k = 0; k < 8; k++) {
            Vec3 c = V3(pp[0] + (k & 1 ? 0.35f : -0.35f), pp[1] + (k & 2 ? 1.9f : 0.0f), pp[2] + (k & 4 ? 0.35f : -0.35f));
            float x, y, z;
            if (!Project(c, vp, (float)W, (float)H, &x, &y, &z)) {
                bx0 = by0 = -1e9f;
                bx1 = by1 = 1e9f;
                break;
            }
            bx0 = fminf(bx0, x), by0 = fminf(by0, y), bx1 = fmaxf(bx1, x), by1 = fmaxf(by1, y);
        }
    }
    for (int i = 0; i < g_count && g_nprobe < max && g_nprobe < 3; i++) {
        Decal& d = g_decals[i];
        if (!d.pending || d.kind < 0) continue;
        d.pending = false;   // one try
        Vec3 c = XformPoint(d.p, view);   // view space: z = depth
        float x, y, z;
        if (c.z < 0.3f || !Project(d.p, vp, (float)W, (float)H, &x, &y, &z)) continue;
        if (x < 1 || y < 1 || x > W - 2 || y > H - 2) continue;
        bool overPlayer = x > bx0 && x < bx1 && y > by0 && y < by1;
        // each probe pixel must be its own: two probes on one pixel add up into nonsense (the
        // crosshair's probe is at the centre, where aimed shots land) - step a pixel aside
        for (int tries = 0; tries < 8; tries++) {
            bool clash = fabsf(floorf(x) - floorf(W * 0.5f)) < 0.5f && fabsf(floorf(y) - floorf(H * 0.5f)) < 0.5f;
            for (int j = 0; j < g_nprobe; j++)
                if (fabsf(px[j][0] - floorf(x)) < 0.5f && fabsf(px[j][1] - floorf(y)) < 0.5f) clash = true;
            if (!clash) break;
            x += 1.0f;
        }
        Probe& p = g_probe[g_nprobe];
        p.decal = i;
        p.camWorld = RigidInverse(view);
        p.px = floorf(x) + 0.5f;
        p.py = floorf(y) + 0.5f;
        p.sx = proj.m[0];
        p.sy = proj.m[5];
        p.w = W;
        p.h = H;
        p.minDepth = overPlayer ? pdepth + 0.3f : 0.0f;
        px[g_nprobe][0] = floorf(x);
        px[g_nprobe][1] = floorf(y);
        range[g_nprobe][0] = fmaxf(0.3f, c.z - kProbeFront);
        range[g_nprobe][1] = c.z + kProbeBack;
        g_nprobe++;
    }
    return g_nprobe;
}

// At Present: the depths read for those pixels (-1 / -2: nothing in range)
void Decal_ProbeResult(const float* depth, int n)
{
    for (int i = 0; i < n && i < g_nprobe; i++) {
        if (g_decalLog > 0 && depth[i] <= 0) Log("decal %d: nothing in the probe's range (%.0f)", g_probe[i].decal, depth[i]);
        if (depth[i] <= 0 || depth[i] < g_probe[i].minDepth) {
            if (g_decalLog > 0 && depth[i] > 0) Log("decal %d: reading %.2f could be the player", g_probe[i].decal, depth[i]);
            continue;
        }
        const Probe& p = g_probe[i];
        Decal& d = g_decals[p.decal];
        float nx = p.px / p.w * 2.0f - 1.0f, ny = 1.0f - p.py / p.h * 2.0f;
        Vec3 viewPt = V3(nx * depth[i] / p.sx, ny * depth[i] / p.sy, depth[i]);
        Vec3 was = d.p;
        // what the probe saw in front could be a car or a person standing just before the wall
        // (it reads anything within 0.75 m): the hole would jump onto them and hang in the air
        // once they move. Only the world's own surfaces count
        HitResult in;
        if (d.kind < 0) continue;
        if (TraceShot(Row(p.camWorld, 3), was - d.n * 0.02f, g_level, &in, 0.0f) &&
            ((in.kind == HIT_VEHICLE && in.object != d.veh) || in.kind == HIT_CHARACTER)) {
            if (g_decalLog > 0) Log("decal %d: a %s is in front of it, stays", p.decal, in.kind == HIT_VEHICLE ? "vehicle" : "character");
            continue;
        }
        d.p = XformPoint(viewPt, p.camWorld) + d.n * 0.01f;
        Mat4 m;
        if (d.veh && VehicleFrame(d.veh, &m)) Attach(d, d.veh, m);   // (its new place, in the car's frame)
        if (g_decalLog > 0)
            Log("decal %d moved onto the visible surface: depth %.3f, %.3f m", p.decal, depth[i], Len(d.p - was));
    }
    g_nprobe = 0;
}

static bool ProjectQuad(const Vec3* c, const Mat4& vp, const Renderer& r, float (*q)[4])
{
    for (int k = 0; k < 4; k++) {
        const float* m = vp.m;
        float x = c[k].x * m[0] + c[k].y * m[4] + c[k].z * m[8] + m[12];
        float y = c[k].x * m[1] + c[k].y * m[5] + c[k].z * m[9] + m[13];
        float z = c[k].x * m[2] + c[k].y * m[6] + c[k].z * m[10] + m[14];
        float w = c[k].x * m[3] + c[k].y * m[7] + c[k].z * m[11] + m[15];
        if (w < 0.05f) return false;
        q[k][0] = (x / w * 0.5f + 0.5f) * r.W;
        q[k][1] = (-y / w * 0.5f + 0.5f) * r.H;
        q[k][2] = z / w;
        q[k][3] = 1.0f / w;
    }
    return true;
}

// a bullet in a car's glass: a frosted spot round a dark chip, cracks running out from it and a
// couple of broken rings between them (each crack its own, from the decal's seed). The lines are
// at least a pixel and a half wide however far it is, so a crack reads from across the street.
static void DrawCrack(Renderer& r, const Decal& d, const Mat4& vp, Vec3 eye, float proj22)
{
    const Sprite* white = r.Find("white");
    const Sprite* hole = r.Find("bullethole");
    const Sprite* blob = r.Find("blob");
    if (!white || !hole || !blob) return;
    float R = Len(d.right);
    Vec3 ru = d.right * (1.0f / R), uu = d.up * (1.0f / Len(d.up));
    float pxPerM = 0.5f * r.H * proj22 / fmaxf(0.5f, Len(d.p - eye));   // pixels a metre, there
    float wdt = fmaxf(R * 0.045f, 0.8f / pxPerM);                       // (half widths)
    uint32_t s = d.seed;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; };
    auto quad = [&](const Sprite* sp, float half, Color c) {
        Vec3 cc[4] = {d.p - ru * half + uu * half, d.p + ru * half + uu * half, d.p + ru * half - uu * half,
                      d.p - ru * half - uu * half};
        float q[4][4];
        if (ProjectQuad(cc, vp, r, q)) r.SpriteQuad(sp, q, c);
    };
    quad(blob, R * 0.28f, Rgba(0.95f, 0.97f, 1.0f, 0.75f));    // frosted round the hole
    quad(hole, fmaxf(R * 0.12f, 1.2f / pxPerM), Rgba(0.75f, 0.8f, 0.85f, 0.95f));
    const int kRays = 9;
    float ang[kRays], len[kRays];
    for (int i = 0; i < kRays; i++) {
        ang[i] = (i + 0.25f + rnd() * 0.5f) * 6.2831853f / kRays;
        len[i] = R * (0.5f + rnd() * 0.5f);
    }
    auto line = [&](Vec3 a, Vec3 b, float wa, float wb, float alpha) {
        Vec3 dir = b - a, side = Norm(Cross(d.n, dir));
        Vec3 c[4] = {a + side * wa, b + side * wb, b - side * wb, a - side * wa};
        float lq[4][4];
        if (ProjectQuad(c, vp, r, lq)) r.SpriteQuad(white, lq, Rgba(0.96f, 0.98f, 1.0f, alpha));
    };
    for (int i = 0; i < kRays; i++) {   // the cracks, each in two bends
        Vec3 dir = ru * cosf(ang[i]) + uu * sinf(ang[i]);
        Vec3 bend = ru * cosf(ang[i] + (rnd() - 0.5f) * 0.5f) + uu * sinf(ang[i] + (rnd() - 0.5f) * 0.5f);
        Vec3 a = d.p + dir * (R * 0.08f), m = d.p + dir * (len[i] * 0.55f), e = m + bend * (len[i] * 0.45f);
        line(a, m, wdt, wdt * 0.8f, 0.9f);
        line(m, e, wdt * 0.8f, wdt * 0.35f, 0.75f);
    }
    for (int ring = 0; ring < 2; ring++) {   // broken rings: between neighbouring cracks that reach that far
        float rr = R * (ring ? 0.62f : 0.32f);
        for (int i = 0; i < kRays; i++) {
            int j = (i + 1) % kRays;
            if (len[i] < rr || len[j] < rr || rnd() < 0.3f) continue;
            float f1 = rr * (0.9f + rnd() * 0.2f), f2 = rr * (0.9f + rnd() * 0.2f);
            Vec3 a = d.p + (ru * cosf(ang[i]) + uu * sinf(ang[i])) * f1, b = d.p + (ru * cosf(ang[j]) + uu * sinf(ang[j])) * f2;
            line(a, b, wdt * 0.7f, wdt * 0.7f, 0.65f);
        }
    }
}

void Decal_Draw(Renderer& r)
{
    float v[16], pj[16];
    // a scorch mark is the cartoon smoke puff, sooty and see-through: a soft blotch, not a hole
    const Sprite* spr = r.Find("bullethole");
    const Sprite* soot = r.Find("smoke");
    if (!g_count || !spr || !soot || !Game_GetCamera(v, pj)) return;
    FollowVehicles(g_followFrame + 1);   // (a frame the probe didn't run)
    Mat4 view, proj;
    memcpy(view.m, v, 64);
    memcpy(proj.m, pj, 64);
    Mat4 vp = Mul(view, proj);
    Vec3 eye = Row(RigidInverse(view), 3);
    r.SpriteDepth(true);
    for (int i = 0; i < g_count; i++) {
        const Decal& d = g_decals[i];
        if (d.kind < 0) continue;
        Vec3 toEye = eye - d.p;
        if (Dot(toEye, d.n) <= 0 || Len(toEye) > kDrawDist) continue;   // behind its wall, or far
        const Vec3 c[4] = {d.p - d.right + d.up, d.p + d.right + d.up, d.p + d.right - d.up, d.p - d.right - d.up};
        float q[4][4];
        bool ok = true;
        for (int k = 0; k < 4 && ok; k++) {
            const float* m = vp.m;
            float x = c[k].x * m[0] + c[k].y * m[4] + c[k].z * m[8] + m[12];
            float y = c[k].x * m[1] + c[k].y * m[5] + c[k].z * m[9] + m[13];
            float z = c[k].x * m[2] + c[k].y * m[6] + c[k].z * m[10] + m[14];
            float w = c[k].x * m[3] + c[k].y * m[7] + c[k].z * m[11] + m[15];
            if (w < 0.05f) ok = false;   // a corner behind the camera: skip this one
            else {
                q[k][0] = (x / w * 0.5f + 0.5f) * r.W;
                q[k][1] = (-y / w * 0.5f + 0.5f) * r.H;
                q[k][2] = z / w;
                q[k][3] = 1.0f / w;
            }
        }
        if (ok) {
            if (d.kind == DECAL_SCORCH) r.SpriteQuad(soot, q, Rgba(0.07f, 0.06f, 0.05f, 0.62f));
            else if (d.kind == DECAL_GLASS) DrawCrack(r, d, vp, eye, proj.m[5]);
            else r.SpriteQuad(spr, q, Rgba(1, 1, 1, 1));
        }
        if (g_decalLog > 0)
            Log("decal %d at (%.2f %.2f %.2f) n (%.2f %.2f %.2f) %.2f m: %s, corner0 (%.0f %.0f) z %.5f, corner2 (%.0f %.0f)",
                i, d.p.x, d.p.y, d.p.z, d.n.x, d.n.y, d.n.z, Len(toEye), ok ? "drawn" : "corner behind", q[0][0], q[0][1],
                q[0][2], q[2][0], q[2][1]);
    }
    r.SpriteDepth(false);
    if (g_decalLog > 0) g_decalLog--;
}
