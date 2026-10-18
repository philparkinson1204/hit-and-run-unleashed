// The rocket launcher's rocket: a real projectile, not a hitscan. It leaves the launcher towards
// what was under the crosshair, flies at an arcade speed (faster as its motor burns), and every
// frame the stretch it covered - from where it was to where it is now - is tested against the
// game's own collision (TraceShot), the way the game's wasp-camera bolts fly (Projectile::Update
// 0x41D530 tests their last position to the new one), so it can't slip through anything thin.
// What it hits explodes (Explode, explode.cpp); with nothing in the way it bursts in the air at
// the end of its range. It leaves a trail of cartoon smoke puffs (the game's own particles come
// from small pools, and a trail would use them up: ParticleManager::Add 0x4B6520).
//
// Game time: the rocket slows down with the game (the wheel's slow motion), stops while the game
// is paused, and is taken away without a bang when gameplay ends (a cutscene, a level or mission
// change, a new player), so nothing explodes into a scene it doesn't belong to.
#include "hit.h"

static const float kSpeed0 = 26.0f;     // m/s leaving the tube
static const float kSpeedMax = 46.0f;   // once the motor is going
static const float kAccel = 45.0f;      // m/s^2
static const float kLife = 5.0f;        // seconds before it bursts in the air
static const float kRadius = 0.07f;     // how thick it is, for the collision
static const float kPuffEvery = 1.0f / 55.0f;

struct RocketS {
    bool live;
    Vec3 p, dir;
    float speed, t, dist, roll, scale, trailT;
    uint32_t shooter;
    WeaponSpec spec;
    bool haveStop;   // (v1.0) a drawn stop on its way: what the crosshair was on, drawn but not in the game's collision
    HitResult stop;
    float lastStep;  // (v1.0) how far it went last frame (the depth probe looks that far ahead and a bit)
};
static RocketS g_rk[4];
static RocketPuff g_trail[200];
static int g_ntrail;
static uint32_t g_rkPlayer;
static uint32_t g_rng = 0x6D2B79F5u;
static int g_rocketLog;   // debug "rocketlog <rockets>"

// (v1.0) What's drawn just ahead of each rocket in flight. The game's collision has no piece for a lot of what's drawn - the
// Simpsons' bay window and garage side, buildings all over Level 1 - and rockets flew through them. An aimed shot carries a
// drawn stop (FireRPG); this is for every rocket, from the hip or past what the crosshair was on: inside the game's scene the
// depth probe (render.cpp, the frame's own depth as bullet holes use it) reads two pixels on its way - a little and a step and a
// bit ahead - each in a window round that point's depth, and something drawn across its way, nearer than anything the trace
// meets, is where it bursts. Off screen, or hidden behind something nearer (-2), it isn't known: the trace alone, as before
static const float kAhead[2] = {0.6f, 1.3f};   // (steps ahead, plus 0.25 m)
static struct RkProbe {
    int rk;
    Mat4 camWorld;
    float px, py, sx, sy, w, h;
} g_rkProbe[8];
static int g_nRkProbe;
static int g_rkSeenN[4];
static Vec3 g_rkSeen[4][2];   // the drawn points ahead of each rocket, read on the frame just shown

static float Rnd()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

static void AddPuff(Vec3 p, Vec3 drift, float size, float life, float shade)
{
    RocketPuff& q = g_ntrail < 200 ? g_trail[g_ntrail++] : g_trail[(int)(Rnd() * 199.0f)];
    q.p = p;
    q.v = drift;
    q.t = 0;
    q.life = life;
    q.size = size;
    q.rot = Rnd() * 6.2831853f;
    q.shade = shade;
}

void Rocket_Clear()
{
    for (RocketS& r : g_rk) r.live = false;
    g_ntrail = 0;
}

bool Rocket_Any()
{
    if (g_ntrail) return true;
    for (const RocketS& r : g_rk)
        if (r.live) return true;
    return false;
}

static void Burst(RocketS& r, const HitResult& h, Vec3 at, const char* why)
{
    r.live = false;
    if (g_rocketLog > 0) {
        g_rocketLog--;
        char line[200];
        Trace_Describe(h, line, sizeof(line));
        Log("rocket: %s after %.2f s, %.1f m: %s", why, r.t, r.dist, line);
    }
    Explode(r.spec, h, at, r.dir, r.shooter);
}

void Rocket_Fire(const WeaponSpec& w, Vec3 body, Vec3 start, Vec3 dir, float scale, uint32_t shooter, const HitResult* drawnStop)
{
    RocketS* r = nullptr;
    for (RocketS& q : g_rk)
        if (!q.live) {
            r = &q;
            break;
        }
    if (!r) {   // four in the air already: the oldest one makes room (it used to be slot 0, whatever its age)
        r = &g_rk[0];
        for (RocketS& q : g_rk)
            if (q.t > r->t) r = &q;
    }
    *r = {};
    r->live = true;
    r->p = start;
    r->dir = Norm(dir);
    r->speed = kSpeed0;
    r->scale = scale;
    r->shooter = shooter;
    r->spec = w;
    if (drawnStop) r->haveStop = true, r->stop = *drawnStop;
    // a launcher poked through a wall: the rocket meets the wall at once
    HitResult h;
    if (TraceShot(body, start, shooter, &h, kRadius)) Burst(*r, h, h.pos, "point blank");
}

int Rocket_ProbeWant(float (*px)[2], float (*range)[2], int used, int max, int W, int H)
{
    g_nRkProbe = 0;
    float v[16], pj[16];
    if (max <= 0 || W <= 0 || H <= 0 || !Game_GetCamera(v, pj)) return 0;
    Mat4 view, proj;
    memcpy(view.m, v, 64);
    memcpy(proj.m, pj, 64);
    Mat4 vp = Mul(view, proj), cw = RigidInverse(view);
    for (int i = 0; i < 4; i++) {
        const RocketS& r = g_rk[i];
        if (!r.live || r.dist < 1.0f) continue;   // (just out of the tube: his own head and arm are drawn round it)
        float zNow = XformPoint(r.p, view).z;
        for (int a = 0; a < 2 && g_nRkProbe < max; a++) {
            Vec3 ahead = r.p + r.dir * (fmaxf(r.lastStep, 0.3f) * kAhead[a] + 0.25f);
            Vec3 c = XformPoint(ahead, view);
            float x, y, z;
            if (c.z < 0.5f || !Project(ahead, vp, (float)W, (float)H, &x, &y, &z)) continue;
            if (x < 1 || y < 1 || x > W - 2 || y > H - 2) continue;
            for (int tries = 0; tries < 8; tries++) {   // (its own pixel: two probes on one add up - the crosshair's at the centre)
                bool clash = fabsf(floorf(x) - floorf(W * 0.5f)) < 0.5f && fabsf(floorf(y) - floorf(H * 0.5f)) < 0.5f;
                for (int j = 0; j < used + g_nRkProbe; j++)
                    if (fabsf(px[j][0] - floorf(x)) < 0.5f && fabsf(px[j][1] - floorf(y)) < 0.5f) clash = true;
                if (!clash) break;
                x += 1.0f;
            }
            RkProbe& p = g_rkProbe[g_nRkProbe];
            p.rk = i, p.camWorld = cw, p.px = floorf(x) + 0.5f, p.py = floorf(y) + 0.5f;
            p.sx = proj.m[0], p.sy = proj.m[5], p.w = (float)W, p.h = (float)H;
            px[used + g_nRkProbe][0] = floorf(x), px[used + g_nRkProbe][1] = floorf(y);
            range[used + g_nRkProbe][0] = fmaxf(0.3f, fminf(zNow, c.z) - 0.6f);
            range[used + g_nRkProbe][1] = c.z + 0.3f;
            g_nRkProbe++;
        }
    }
    return g_nRkProbe;
}

void Rocket_ProbeResult(const float* depth, int n)
{
    for (int i = 0; i < 4; i++) g_rkSeenN[i] = 0;
    for (int i = 0; i < n && i < g_nRkProbe; i++) {
        if (depth[i] <= 0) continue;   // (-1: nothing drawn in the window; -2: something nearer hides it)
        const RkProbe& p = g_rkProbe[i];
        float nx = p.px / p.w * 2.0f - 1.0f, ny = 1.0f - p.py / p.h * 2.0f;
        Vec3 viewPt = V3(nx * depth[i] / p.sx, ny * depth[i] / p.sy, depth[i]);
        if (g_rkSeenN[p.rk] < 2) g_rkSeen[p.rk][g_rkSeenN[p.rk]++] = XformPoint(viewPt, p.camWorld);
    }
    if (g_rocketLog > 0)
        for (int i = 0; i < n && i < g_nRkProbe; i++)
            Log("rocket %d: probe (%.0f %.0f) read %.2f%s", g_rkProbe[i].rk, g_rkProbe[i].px, g_rkProbe[i].py, depth[i],
                depth[i] == -2.0f ? " (hidden: something nearer)" : depth[i] == -1.0f ? " (nothing drawn there)" : "");
    g_nRkProbe = 0;
}

void Rocket_Update(float dt, uint32_t player, bool run, bool freeze)
{
    if (player != g_rkPlayer) {   // a new level or player: nothing carries over
        Rocket_Clear();
        g_rkPlayer = player;
    }
    if (!run) {
        if (!freeze) Rocket_Clear();
        return;
    }
    if (dt <= 0) return;
    for (int ri = 0; ri < 4; ri++) {
        RocketS& r = g_rk[ri];
        if (!r.live) continue;
        r.t += dt;
        r.speed = fminf(kSpeedMax, r.speed + kAccel * dt);
        float step = r.speed * dt;
        Vec3 next = r.p + r.dir * step;
        // from a step behind the nose: a car that drove over the rocket's last position since the
        // last frame is entered from outside (a trace starting inside a volume doesn't count it, and
        // rockets flew through oncoming cars). That stretch was clear of everything that stands still.
        // Cars are slower than the rocket, so one step back is enough.
        Vec3 from = r.p - r.dir * fminf(r.dist, step);
        HitResult h;
        bool hit = TraceShot(from, next, r.shooter, &h, kRadius);
        // (v1.0) the drawn stop: reached this step - before anything the trace met - it bursts there. Rockets went
        // straight through the Simpsons' bay window and garage side and through buildings all over Level 1: the game's
        // collision has no piece there, only the picture does (FireRPG reads it under the crosshair: vcover)
        if (r.haveStop) {
            float s = Dot(r.stop.pos - r.p, r.dir), lat = Len(r.stop.pos - (r.p + r.dir * s));
            float hs = hit ? Dot(h.pos - r.p, r.dir) : 1e9f;
            if (lat > 0.6f) r.haveStop = false;   // (not on its line after all)
            else if (s <= step + kRadius && s <= hs + 0.05f) {
                Burst(r, r.stop, r.stop.pos, "hit (drawn)");
                continue;
            }
        }
        // (v1.0) something drawn across its way this step (the depth probe ahead of it), nearer than what the trace met
        {
            float hs = hit ? Dot(h.pos - r.p, r.dir) : 1e9f, best = 1e9f;
            Vec3 at = {};
            for (int k = 0; k < g_rkSeenN[ri]; k++) {
                Vec3 S = g_rkSeen[ri][k];
                float s = Dot(S - r.p, r.dir), lat = Len(S - (r.p + r.dir * s));
                if (s > -0.2f && s <= step + 0.3f && lat < 0.2f && s < hs - 0.05f && s < best) best = s, at = S;
            }
            g_rkSeenN[ri] = 0;
            if (best < 1e8f) {
                HitResult d = {};
                d.kind = HIT_WORLD, d.pos = at, d.normal = r.dir * -1.0f, d.dist = r.dist + best, d.ground = d.volume = -1;
                d.drawn = true;
                Burst(r, d, at, "hit (drawn, in flight)");
                continue;
            }
        }
        if (hit) {
            Burst(r, h, h.pos, "hit");
            continue;
        }
        // smoke from the tail, spread along the stretch it covered so a fast frame leaves no gap
        Vec3 tail = r.dir * (-0.38f * r.scale);
        r.trailT += dt;
        int n = 0;
        while (r.trailT >= kPuffEvery && n < 6) {
            r.trailT -= kPuffEvery;
            float f = 1.0f - r.trailT / dt;
            Vec3 at = r.p + r.dir * (step * Clamp01f(f)) + tail;
            Vec3 drift = V3(Rnd() - 0.5f, Rnd() * 0.6f + 0.2f, Rnd() - 0.5f) * 0.6f - r.dir * 1.5f;
            AddPuff(at, drift, (0.20f + Rnd() * 0.08f) * r.scale, 0.9f + Rnd() * 0.5f, 0.82f + Rnd() * 0.18f);
            n++;
        }
        r.trailT = fminf(r.trailT, kPuffEvery);
        r.p = next;
        r.dist += step;
        r.lastStep = step;
        r.roll += 10.0f * dt;
        if (r.dist >= r.spec.range || r.t >= kLife) {
            HitResult none = {};
            none.kind = HIT_NONE;
            none.ground = none.volume = -1;
            none.pos = r.p;
            none.normal = r.dir * -1.0f;
            Burst(r, none, r.p, "out of range");
        }
    }
    for (int i = 0; i < g_ntrail;) {
        RocketPuff& q = g_trail[i];
        q.t += dt;
        q.p = q.p + q.v * dt;
        q.v = q.v * (1.0f - fminf(1.0f, dt * 2.2f)) + V3(0, 0.25f * dt, 0);
        if (q.t > q.life) g_trail[i] = g_trail[--g_ntrail]; else i++;
    }
}

int Rocket_Views(RocketView* out, int max)
{
    int n = 0;
    for (const RocketS& r : g_rk) {
        if (!r.live || n >= max) continue;
        Vec3 up = fabsf(r.dir.y) < 0.95f ? V3(0, 1, 0) : V3(1, 0, 0);
        Vec3 x = Norm(Cross(up, r.dir));
        Vec3 y = Cross(r.dir, x);
        float c = cosf(r.roll), s = sinf(r.roll);
        Vec3 xr = x * c + y * s, yr = y * c - x * s;   // it spins along its flight
        Mat4 m = Identity();
        SetRow(m, 0, xr * r.scale, 0);
        SetRow(m, 1, yr * r.scale, 0);
        SetRow(m, 2, r.dir * r.scale, 0);
        SetRow(m, 3, r.p, 1);
        out[n].world = m;
        out[n].tail = r.p + r.dir * (-0.38f * r.scale);
        out[n].dir = r.dir;
        out[n].scale = r.scale;
        out[n].age = r.t;
        n++;
    }
    return n;
}

int Rocket_Trail(const RocketPuff** out)
{
    *out = g_trail;
    return g_ntrail;
}

void Rocket_DebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "rocketlog")) g_rocketLog = arg[0] ? atoi(arg) : 1;
}
