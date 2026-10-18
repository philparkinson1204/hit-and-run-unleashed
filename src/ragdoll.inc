// Falling down drunk (Item 2 of the Weapons Lab): a ragdoll made by the mod - the game has none - from his own posed skeleton.
// Included from gun.cpp after drunk.inc.
//
// Particles at his joints (and a few made up: his belly, the top of his head, his nose, his hands), sticks between them, his torso
// and head near-rigid blocks, his limbs on sticks with limits, under gravity, colliding with the ground (a little height map probed
// where he falls) with friction - Verlet, 120 steps a second (research/beer/techniques.md). Muscles on top: as he goes over his
// arms reach for the ground (late: he's drunk), his chin tucks if he goes backwards. On the ground he settles, lies there a beat, then
// gets up - the same particles pulled through a get-up (hands under his shoulders, a push-up - which sometimes gives way - onto his
// knees, a knee up, standing), on his back he rolls over first. All the while the game's own body (where the camera and the world
// think he is) is walked under the one we draw, so he stands up where he lies. Then the game's animation takes back over.

enum RgPhase { RG_OFF, RG_FALL, RG_DOWN, RG_ROLL, RG_GETUP, RG_BLEND };
enum RgP {   // the particles
    RP_PELVIS, RP_HIPL, RP_KNEEL, RP_ANKL, RP_BALLL, RP_HIPR, RP_KNEER, RP_ANKR, RP_BALLR, RP_SP1, RP_SP2, RP_BELLY,
    RP_NECK, RP_HEAD, RP_TOP, RP_NOSE, RP_SHL, RP_ELBL, RP_WRL, RP_HANDL, RP_SHR, RP_ELBR, RP_WRR, RP_HANDR, RP_N
};
static const int kRgJoint[RP_N] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, -1, 16, 17, -1, -1, 19, 20, 21, -1, 26, 27, 28, -1};
static const float kRgRad[RP_N] = {0.17f, 0.10f, 0.08f, 0.06f, 0.05f, 0.10f, 0.08f, 0.06f, 0.05f, 0.18f, 0.20f, 0.26f,
                                   0.09f, 0.16f, 0.14f, 0.08f, 0.10f, 0.07f, 0.055f, 0.07f, 0.10f, 0.07f, 0.055f, 0.07f};
static const float kRgMass[RP_N] = {8, 3, 2, 1, 0.5f, 3, 2, 1, 0.5f, 7, 7, 9, 2, 3, 1, 0.5f, 2, 1.5f, 1, 0.6f, 2, 1.5f, 1, 0.6f};
struct RgStick { int a, b; float len, stiff; float minL, maxL; };   // (minL/maxL > 0: a range instead of a length)

static struct {
    int phase;
    float t, phaseT;               // seconds since the fall, in this phase
    float k;                       // his size
    Vec3 p[RP_N], q[RP_N];         // positions, last positions (Verlet)
    Vec3 rest[RP_N];               // at the start (the pose's own shape)
    Mat4 W0[40];                   // his skeleton when he went over
    int n;
    RgStick s[160];
    int ns;
    Vec3 dir;                      // which way he fell
    float level;
    // the ground: a 3 x 3 grid of heights probed where he'll land
    Vec3 g0;                       // its corner
    float gstep, gy[9];
    Vec3 wallP[12], wallN[12];     // walls round him (found once, as he goes over): a point on each and its normal (level)
    int nwall;
    float still;                   // seconds he's been (nearly) still
    bool prone;                    // face down
    int attempt;                   // get-up attempts
    bool willFail;                 // this one gives way
    Vec3 upFwd;                    // the get-up's forward (where his head is, along the ground)
    Vec3 upPos;                    // the get-up's place (his pelvis on the ground)
    Vec3 key[RP_N];                // the get-up's current targets
    float keyW;
    float blend;                   // RG_BLEND: 1 -> 0 back to the animation
    Vec3 root;                     // where the game has him (the controller)
    float impact;                  // (sound) the hardest landing so far
    int sounds;
    bool dropped;
    bool asleep;                   // passed out: lies there a while, snoring, before he tries to get up
    float snoreT;
} g_rg;
static int g_rgLog = 0;
static float kRgGravity = 1.25f;    // gravity x (a cartoon falls a little faster)
static float kRgReach = 0.28f;      // seconds before his arms go out to catch him (drunk: late)
static float kRgFailP = 0.45f;      // the chance a first push-up gives way (x level)
static float kRgDownTime = 0.7f;    // seconds he lies there before trying to get up

static bool Ragdoll_Active() { return g_rg.phase != RG_OFF; }
static void Ragdoll_Reset() { g_rg.phase = RG_OFF; }
static bool Ragdoll_Pelvis(Vec3* p) { if (g_rg.phase == RG_OFF) return false; *p = g_rg.p[RP_PELVIS]; return true; }

// ------------------------------------------------------------------ the ground
static float RgGroundAt(float x, float z)
{
    float fx = (x - g_rg.g0.x) / g_rg.gstep, fz = (z - g_rg.g0.z) / g_rg.gstep;
    fx = Clampf(fx, 0.0f, 1.999f), fz = Clampf(fz, 0.0f, 1.999f);
    int ix = (int)fx, iz = (int)fz;
    float ux = fx - ix, uz = fz - iz;
    float a = g_rg.gy[iz * 3 + ix], b = g_rg.gy[iz * 3 + ix + 1], c = g_rg.gy[(iz + 1) * 3 + ix], d = g_rg.gy[(iz + 1) * 3 + ix + 1];
    return (a * (1 - ux) + b * ux) * (1 - uz) + (c * (1 - ux) + d * ux) * uz;
}
static void RgProbeGround(Vec3 ctr, float feetY)
{
    g_rg.gstep = 1.1f;
    g_rg.g0 = ctr - V3(g_rg.gstep, 0, g_rg.gstep);
    for (int iz = 0; iz < 3; iz++)
        for (int ix = 0; ix < 3; ix++) {
            Vec3 p = g_rg.g0 + V3(ix * g_rg.gstep, 0, iz * g_rg.gstep);
            float y;
            // (the terrain under it; a deck or a roof far above his feet isn't where he lands)
            if (Trace_GroundY(V3(p.x, feetY + 0.8f, p.z), &y) && y < feetY + 0.6f && y > feetY - 1.5f) g_rg.gy[iz * 3 + ix] = y;
            else g_rg.gy[iz * 3 + ix] = feetY;
        }
}

// the walls round him: 16 traces out from his feet (8 ways, at his knees and his chest), each hit on something upright (a wall, a
// hedge, a car, a fence) kept as a plane he can't go through. (With the ground alone he fell into a hedge and through a wall.)
static void RgProbeWalls(Vec3 feet, float k)
{
    g_rg.nwall = 0;
    if (!Trace_Available()) return;
    for (int hgt = 0; hgt < 2; hgt++)
        for (int a = 0; a < 8; a++) {
            float ang = a * 0.785398f;
            Vec3 d = V3(sinf(ang), 0, cosf(ang)), from = feet + UP * ((hgt ? 1.0f : 0.35f) * k);
            HitResult h;
            if (!TraceShot(from, from + d * (2.0f * k), g_player, &h, 0.05f) || h.kind == HIT_CHARACTER || h.ground >= 0) continue;
            Vec3 n = V3(h.normal.x, 0, h.normal.z);
            if (Len(n) < 0.6f) continue;   // (not upright)
            n = Norm(n);
            bool dup = false;
            for (int i = 0; i < g_rg.nwall && !dup; i++)
                dup = Dot(n, g_rg.wallN[i]) > 0.95f && fabsf(Dot(h.pos - g_rg.wallP[i], n)) < 0.15f;
            if (!dup && g_rg.nwall < 12) g_rg.wallP[g_rg.nwall] = h.pos, g_rg.wallN[g_rg.nwall] = n, g_rg.nwall++;
        }
}

// a place to get up: kept clear of the walls he knows (the game body is put there when he stands - inside a hedge, the game
// would push him out on top of it)
static Vec3 RgClearOfWalls(Vec3 p)
{
    float r = 0.55f * g_rg.k;
    for (int pass = 0; pass < 2; pass++)
        for (int w = 0; w < g_rg.nwall; w++) {
            float d = Dot(V3(p.x - g_rg.wallP[w].x, 0, p.z - g_rg.wallP[w].z), g_rg.wallN[w]);
            if (d < r && d > -0.6f * g_rg.k) p = p + g_rg.wallN[w] * (r - d);
        }
    return p;
}

// ------------------------------------------------------------------ building it
static void RgStickAdd(int a, int b, float stiff = 1.0f, float minS = 0, float maxS = 0)
{
    if (g_rg.ns >= 160) return;
    float L = Len(g_rg.p[a] - g_rg.p[b]);
    g_rg.s[g_rg.ns++] = {a, b, L, stiff, minS > 0 ? L * minS : 0, maxS > 0 ? L * maxS : 0};
}
static void RgRigid(const int* ids, int m, float stiff)
{
    for (int i = 0; i < m; i++)
        for (int j = i + 1; j < m; j++) RgStickAdd(ids[i], ids[j], stiff);
}

static void Gun_DropForFall();   // (gun.cpp: whatever's in his hand goes)
static void Ragdoll_Fall(Vec3 dir, float speed, float level);
static void Ragdoll_PassOut(Vec3 back, float level)   // (drunk.inc: legless and standing still - down he goes, asleep)
{
    Ragdoll_Fall(back, 0.35f, level);
    if (g_rg.phase != RG_OFF) g_rg.asleep = true, g_rg.snoreT = 1.5f, Log("ragdoll: passed out");
}
static void Ragdoll_Fall(Vec3 dir, float speed, float level)
{
    if (!g_skin.haveHand || g_skin.n < 33) return;   // (no skeleton of his drawn yet)
    memset(&g_rg, 0, sizeof(g_rg));
    g_rg.n = g_skin.n;
    for (int j = 0; j < g_rg.n; j++) g_rg.W0[j] = g_skin.world[j];
    const Mat4* W = g_rg.W0;
    float k = Clampf(Len(Row(W[17], 3) - Row(W[4], 3)) / 0.80f, 0.5f, 1.4f);   // (Homer: head 0.80 above his pelvis)
    g_rg.k = k;
    for (int i = 0; i < RP_N; i++)
        if (kRgJoint[i] >= 0) g_rg.p[i] = Row(W[kRgJoint[i]], 3);
    Vec3 fB = Norm(V3(-W[0].m[8], 0, -W[0].m[10]) + V3(0, 0, 1e-5f));
    // the made-up points: his belly (in front of Spine_1 by his belly's depth), the top of his head, his nose, his hands
    const CharShape* shape = CharShapeNow();
    float front = shape ? FrontAt(shape, -0.15f) : 0.30f;
    g_rg.p[RP_BELLY] = Row(W[15], 3) * 0.4f + Row(W[14], 3) * 0.6f + fB * (front * 0.55f);
    g_rg.p[RP_TOP] = Row(W[17], 3) + UP * (0.38f * k);
    g_rg.p[RP_NOSE] = Row(W[17], 3) + Norm(Row(W[17], 0)) * (0.20f * k) + UP * (0.05f * k);
    g_rg.p[RP_HANDL] = Row(W[21], 3) + Norm(Row(W[22], 3) - Row(W[21], 3) + V3(0, -1e-5f, 0)) * (0.10f * k);
    g_rg.p[RP_HANDR] = Row(W[28], 3) + Norm(Row(W[29], 3) - Row(W[28], 3) + V3(0, -1e-5f, 0)) * (0.10f * k);
    for (int i = 0; i < RP_N; i++) g_rg.rest[i] = g_rg.p[i];
    // the sticks: his torso a near-rigid block (Homer goes over like a tree), his head another, limbs on sticks, a few limits
    static const int torso[] = {RP_PELVIS, RP_HIPL, RP_HIPR, RP_SP1, RP_SP2, RP_BELLY, RP_SHL, RP_SHR, RP_NECK};
    RgRigid(torso, 9, 1.0f);
    static const int head[] = {RP_HEAD, RP_TOP, RP_NOSE};
    RgRigid(head, 3, 1.0f);
    RgStickAdd(RP_NECK, RP_HEAD, 1.0f);
    RgStickAdd(RP_NECK, RP_TOP, 0.5f, 0.85f, 1.10f);   // (the neck bends, a little)
    RgStickAdd(RP_SP2, RP_TOP, 0.5f, 0.85f, 1.08f);
    RgStickAdd(RP_SP2, RP_NOSE, 0.4f, 0.80f, 1.15f);
    for (int side = 0; side < 2; side++) {
        int hip = side ? RP_HIPR : RP_HIPL, knee = hip + 1, ank = hip + 2, ball = hip + 3;
        RgStickAdd(hip, knee), RgStickAdd(knee, ank), RgStickAdd(ank, ball), RgStickAdd(knee, ball, 0.6f);
        RgStickAdd(hip, ank, 0.5f, 0.55f, 1.0f);              // (the knee: folds to about half, never past straight)
        RgStickAdd(RP_PELVIS, knee, 0.3f, 0.70f, 1.25f);      // (the hip: a cone, roughly)
        RgStickAdd(RP_SP1, knee, 0.3f, 0.55f, 1.30f);
        int sh = side ? RP_SHR : RP_SHL, elb = sh + 1, wr = sh + 2, hand = sh + 3;
        RgStickAdd(sh, elb), RgStickAdd(elb, wr), RgStickAdd(wr, hand), RgStickAdd(elb, hand, 0.5f, 0.70f, 1.05f);
        RgStickAdd(sh, wr, 0.5f, 0.35f, 1.0f);                // (the elbow)
        RgStickAdd(RP_NECK, elb, 0.2f, 0.35f, 1.60f);         // (the shoulder: wide)
    }
    // falling: tipping over his feet the way he leans, as fast as he went (his feet stay where they are)
    g_rg.dir = Norm(V3(dir.x, 0, dir.z) + V3(0, 0, 1e-5f));
    g_rg.level = level;
    float feetY = fminf(g_rg.p[RP_BALLL].y, g_rg.p[RP_BALLR].y) - 0.04f * k;
    Vec3 feet = (g_rg.p[RP_ANKL] + g_rg.p[RP_ANKR]) * 0.5f;
    RgProbeGround(V3(feet.x, 0, feet.z) + g_rg.dir * (0.9f * k), feetY);
    RgProbeWalls(V3(feet.x, feetY, feet.z), k);
    float h = 0.78f * k, w = (0.6f + 0.6f * fminf(speed, 3.0f)) / h;   // (tipping rate: rad/s)
    float dt = 1.0f / 120.0f;
    for (int i = 0; i < RP_N; i++) {
        float up = fmaxf(0.0f, g_rg.p[i].y - feet.y);
        Vec3 v = g_rg.dir * (w * up) + g_drunk.rootVel * 0.5f;
        g_rg.q[i] = g_rg.p[i] - v * dt;
    }
    g_rg.phase = RG_FALL;
    g_rg.root = feet;
    Gun_DropForFall();
    Log("ragdoll: %d walls round him", g_rg.nwall);
    Log("ragdoll: falls %s (speed %.1f, level %.2f)", Dot(g_rg.dir, fB) > 0.5f ? "forward" : Dot(g_rg.dir, fB) < -0.5f ? "backward" : "sideways",
        speed, level);
}

// ------------------------------------------------------------------ simulating it
static void RgSolve(float hdt, float muscleW)
{
    float g = 9.81f * kRgGravity;
    // Verlet: carry on, with gravity (a touch of air drag)
    for (int i = 0; i < RP_N; i++) {
        Vec3 v = (g_rg.p[i] - g_rg.q[i]) * 0.995f;
        g_rg.q[i] = g_rg.p[i];
        g_rg.p[i] = g_rg.p[i] + v + V3(0, -g * hdt * hdt, 0);
    }
    for (int it = 0; it < 8; it++) {
        // the sticks (mass-weighted)
        for (int c = 0; c < g_rg.ns; c++) {
            const RgStick& s = g_rg.s[c];
            Vec3 d = g_rg.p[s.b] - g_rg.p[s.a];
            float L = Len(d);
            if (L < 1e-6f) continue;
            float want = s.len;
            if (s.minL > 0 || s.maxL > 0) {
                if (L < s.minL) want = s.minL;
                else if (L > s.maxL) want = s.maxL;
                else continue;
            }
            float ma = kRgMass[s.a], mb = kRgMass[s.b];
            Vec3 corr = d * ((L - want) / L * s.stiff);
            g_rg.p[s.a] = g_rg.p[s.a] + corr * (mb / (ma + mb));
            g_rg.p[s.b] = g_rg.p[s.b] - corr * (ma / (ma + mb));
        }
        // the muscles: a get-up's targets, or his arms reaching for the ground
        if (muscleW > 0.0f)
            for (int i = 0; i < RP_N; i++) {
                if (g_rg.key[i].y < -1e8f) continue;
                g_rg.p[i] = g_rg.p[i] + (g_rg.key[i] - g_rg.p[i]) * (0.06f * muscleW);
            }
        // his hands and elbows don't go through his belly or chest
        static const int limbs[] = {RP_HANDL, RP_HANDR, RP_WRL, RP_WRR, RP_ELBL, RP_ELBR, RP_KNEEL, RP_KNEER};
        static const int bodies[] = {RP_BELLY, RP_SP2, RP_PELVIS};
        for (int a : limbs)
            for (int b : bodies) {
                if ((a == RP_KNEEL || a == RP_KNEER) && b != RP_BELLY) continue;
                Vec3 d = g_rg.p[a] - g_rg.p[b];
                float L = Len(d), r = (kRgRad[a] + kRgRad[b]) * g_rg.k * 0.9f;
                if (L < r && L > 1e-6f) g_rg.p[a] = g_rg.p[b] + d * (r / L);
            }
        // the walls (and the like): pushed back out, sliding along them with some friction
        for (int w = 0; w < g_rg.nwall; w++)
            for (int i = 0; i < RP_N; i++) {
                float r = kRgRad[i] * g_rg.k, d = Dot(g_rg.p[i] - g_rg.wallP[w], g_rg.wallN[w]) - r;
                if (d < 0.0f && d > -0.6f * g_rg.k) {   // (only near its face: a plane is endless, the wall isn't)
                    g_rg.p[i] = g_rg.p[i] - g_rg.wallN[w] * d;
                    Vec3 v = g_rg.p[i] - g_rg.q[i];
                    Vec3 vt = v - g_rg.wallN[w] * Dot(v, g_rg.wallN[w]);
                    g_rg.q[i] = g_rg.q[i] + vt * 0.3f;
                }
            }
        // the ground (friction: the part in contact loses most of its slide)
        for (int i = 0; i < RP_N; i++) {
            float r = kRgRad[i] * g_rg.k, gy = RgGroundAt(g_rg.p[i].x, g_rg.p[i].z) + r;
            if (g_rg.p[i].y < gy) {
                float pen = gy - g_rg.p[i].y;
                g_rg.p[i].y = gy;
                Vec3 v = g_rg.p[i] - g_rg.q[i];
                float fr = Clamp01(0.35f + pen * 40.0f);
                g_rg.q[i].x += v.x * fr, g_rg.q[i].z += v.z * fr;
                float vy = v.y / hdt;
                if (vy < -1.2f && it == 0) g_rg.impact = fmaxf(g_rg.impact, -vy);
            }
        }
    }
}

// the get-up's poses (targets for the particles), in a frame on the ground: his pelvis at `upPos`, `upFwd` where his head points
static void RgKeys(int stage, float u)
{
    Vec3 F = g_rg.upFwd, R = Norm(Cross(UP, F)), O = g_rg.upPos;
    float k = g_rg.k, gy = RgGroundAt(O.x, O.z);
    auto at = [&](float f, float r, float up) { return O + F * (f * k) + R * (r * k) + V3(0, gy + up * k - O.y, 0); };
    for (int i = 0; i < RP_N; i++) g_rg.key[i] = V3(0, -1e9f, 0);   // (free unless set)
    float sh = 0.24f, hip = 0.14f;
    switch (stage) {
    case 0:   // hands down under his shoulders (still flat)
        g_rg.key[RP_HANDL] = at(0.55f, -sh - 0.05f, 0.05f), g_rg.key[RP_HANDR] = at(0.55f, sh + 0.05f, 0.05f);
        break;
    case 1: {  // the push-up: chest up on straight arms (u: how far; a failing one gives way at the top)
        float hgt = 0.15f + 0.40f * u;
        g_rg.key[RP_HANDL] = at(0.55f, -sh - 0.05f, 0.05f), g_rg.key[RP_HANDR] = at(0.55f, sh + 0.05f, 0.05f);
        g_rg.key[RP_SP2] = at(0.45f, 0, hgt), g_rg.key[RP_NECK] = at(0.62f, 0, hgt + 0.08f);
        g_rg.key[RP_SHL] = at(0.55f, -sh, hgt + 0.04f), g_rg.key[RP_SHR] = at(0.55f, sh, hgt + 0.04f);
        break;
    }
    case 2:   // onto his knees: pelvis up and back over his knees, still on his hands
        g_rg.key[RP_HANDL] = at(0.55f, -sh - 0.05f, 0.05f), g_rg.key[RP_HANDR] = at(0.55f, sh + 0.05f, 0.05f);
        g_rg.key[RP_SP2] = at(0.40f, 0, 0.55f), g_rg.key[RP_SHL] = at(0.50f, -sh, 0.58f), g_rg.key[RP_SHR] = at(0.50f, sh, 0.58f);
        g_rg.key[RP_PELVIS] = at(-0.15f, 0, 0.55f), g_rg.key[RP_KNEEL] = at(-0.10f, -hip, 0.07f), g_rg.key[RP_KNEER] = at(-0.10f, hip, 0.07f);
        g_rg.key[RP_ANKL] = at(-0.50f, -hip, 0.06f), g_rg.key[RP_ANKR] = at(-0.50f, hip, 0.06f);
        break;
    case 3:   // kneeling up: torso upright, hands off the ground
        g_rg.key[RP_PELVIS] = at(-0.05f, 0, 0.50f), g_rg.key[RP_SP2] = at(-0.02f, 0, 1.00f), g_rg.key[RP_TOP] = at(0.0f, 0, 1.75f);
        g_rg.key[RP_KNEEL] = at(-0.05f, -hip, 0.07f), g_rg.key[RP_KNEER] = at(-0.05f, hip, 0.07f);
        g_rg.key[RP_ANKL] = at(-0.45f, -hip, 0.06f), g_rg.key[RP_ANKR] = at(-0.45f, hip, 0.06f);
        break;
    case 4:   // one foot forward (his right), a hand on his knee
        g_rg.key[RP_PELVIS] = at(-0.05f, 0, 0.55f), g_rg.key[RP_SP2] = at(0.08f, 0, 1.02f), g_rg.key[RP_TOP] = at(0.15f, 0, 1.75f);
        g_rg.key[RP_KNEEL] = at(-0.10f, -hip, 0.07f), g_rg.key[RP_KNEER] = at(0.35f, hip, 0.48f), g_rg.key[RP_ANKR] = at(0.35f, hip, 0.06f);
        g_rg.key[RP_ANKL] = at(-0.50f, -hip, 0.06f), g_rg.key[RP_HANDR] = at(0.35f, hip + 0.04f, 0.55f);
        break;
    default: {  // standing (the game's pose takes over from here): at his own heights, as he stood before he fell (a guessed
                // pelvis height held him off the ground - his feet dangled as the game's pose came back)
        Vec3 f0 = (g_rg.rest[RP_ANKL] + g_rg.rest[RP_ANKR]) * 0.5f;
        float g0 = RgGroundAt(f0.x, f0.z);
        auto H = [&](int i) { return (g_rg.rest[i].y - g0) / k; };
        g_rg.key[RP_PELVIS] = at(0.0f, 0, H(RP_PELVIS)), g_rg.key[RP_SP2] = at(0.02f, 0, H(RP_SP2)), g_rg.key[RP_TOP] = at(0.03f, 0, H(RP_TOP));
        g_rg.key[RP_ANKL] = at(0.0f, -hip, H(RP_ANKL)), g_rg.key[RP_ANKR] = at(0.0f, hip, H(RP_ANKR));
        g_rg.key[RP_KNEEL] = at(0.04f, -hip, H(RP_KNEEL)), g_rg.key[RP_KNEER] = at(0.04f, hip, H(RP_KNEER));
        if (g_rgLog && u >= 1.0f) Log("ragdoll: standing at pelvis %.2f spine %.2f top %.2f knee %.2f ankle %.2f (x k %.2f)", H(RP_PELVIS),
                                      H(RP_SP2), H(RP_TOP), H(RP_KNEEL), H(RP_ANKL), k);
        break;
    }
    }
}

// -> 1 while he can't walk (fallen, getting up), down to 0 as he stands; 0 when not fallen
// his game body moved to where he'll stand, as he starts to get up (unseen: the ragdoll's drawn over all of him) - his shadow and
// the game's camera go with him then, not with a jump as he stands
static void RgMoveBody()
{
    Vec3 o = g_rg.upPos;
    float gy = RgGroundAt(o.x, o.z);
    Lab_Relocate(o.x, gy, o.z, remainderf(atan2f(g_rg.upFwd.x, g_rg.upFwd.z) + PI, 2.0f * PI));
    if (g_rgLog) Log("ragdoll: body moved to (%.2f %.3f %.2f)", o.x, gy, o.z);
}
static float Ragdoll_Update(float dt)
{
    if (g_rg.phase == RG_OFF) return 0.0f;
    if (dt <= 0.0f) return 1.0f;
    g_rg.t += dt, g_rg.phaseT += dt;
    int sub = (int)ceilf(dt * 120.0f);
    sub = sub < 1 ? 1 : sub > 10 ? 10 : sub;
    float hdt = dt / sub;
    float muscle = 0.0f;
    Vec3 F = g_rg.dir, R = Norm(Cross(UP, F));
    float k = g_rg.k;
    // the muscles this phase
    for (int i = 0; i < RP_N; i++) g_rg.key[i] = V3(0, -1e9f, 0);
    if (g_rg.phase == RG_FALL) {
        // his arms out to catch him (late), his chin tucked going backwards; his feet stay planted at first
        if (g_rg.phaseT > kRgReach) {
            Vec3 c = (g_rg.p[RP_SHL] + g_rg.p[RP_SHR]) * 0.5f;
            Vec3 aim = c + F * (0.45f * k);
            float gy = RgGroundAt(aim.x, aim.z);
            g_rg.key[RP_HANDL] = V3(aim.x, gy + 0.05f * k, aim.z) - R * (0.30f * k);
            g_rg.key[RP_HANDR] = V3(aim.x, gy + 0.05f * k, aim.z) + R * (0.30f * k);
            muscle = 0.6f;
        }
        if (g_rg.phaseT < 0.30f) {   // (his feet: still on the ground)
            for (int i : {RP_ANKL, RP_ANKR, RP_BALLL, RP_BALLR}) g_rg.key[i] = g_rg.rest[i];
            muscle = fmaxf(muscle, 0.8f);
        }
    } else if (g_rg.phase == RG_ROLL) {   // on his back: roll over onto his front (his shoulders and hips turned round his long axis)
        float u = DnEase(g_rg.phaseT / 0.9f);
        Vec3 axis = g_rg.upFwd, side = Norm(Cross(UP, axis));
        Vec3 O = g_rg.upPos;
        float gy = RgGroundAt(O.x, O.z);
        float a = u * PI;
        Vec3 off = side * (-0.22f * k * cosf(a)) + UP * (0.22f * k * sinf(a));
        g_rg.key[RP_SHL] = O + axis * (0.55f * k) + off + V3(0, gy + 0.18f * k - O.y, 0);
        g_rg.key[RP_SHR] = O + axis * (0.55f * k) - off + V3(0, gy + 0.18f * k - O.y, 0);
        g_rg.key[RP_HIPL] = O + off * 0.6f + V3(0, gy + 0.15f * k - O.y, 0);
        g_rg.key[RP_HIPR] = O - off * 0.6f + V3(0, gy + 0.15f * k - O.y, 0);
        muscle = 0.5f;
    } else if (g_rg.phase == RG_GETUP) {
        // the stages: hands 0.45 s, push-up 0.6 (a failing one: 0.45 up, then gives way 0.15 - and back to the start), knees 0.6,
        // kneel 0.5, a foot up 0.5, stand 0.6
        static const float len[6] = {0.45f, 0.60f, 0.60f, 0.50f, 0.50f, 0.60f};
        float tt = g_rg.phaseT;
        int st = 0;
        while (st < 5 && tt > len[st]) tt -= len[st], st++;
        float u = Clamp01(tt / len[st]);
        if (g_rg.willFail && st == 1 && u > 0.70f) {   // the arms give way: flat on his face again
            g_rg.willFail = false;
            g_rg.phase = RG_DOWN, g_rg.phaseT = 0.0f, g_rg.still = 0.0f;
            Sound_Play(SND_THUD, 0.7f, 0.9f);
            Sound_Play(SND_GROAN, 0.8f, 1.0f);
            Log("ragdoll: the push-up gave way");
            return 1.0f;
        }
        RgKeys(st, DnEase(u));
        muscle = 0.35f + 0.25f * st;
        if (st == 5 && u >= 1.0f) {   // standing: over to the game's animation - his game body moved to where he stands
            g_rg.phase = RG_BLEND, g_rg.phaseT = 0.0f, g_rg.blend = 1.0f;
            Log("ragdoll: up (%.1f s after the fall, %d tries)", g_rg.t, g_rg.attempt);
        }
    }
    for (int s = 0; s < sub; s++) RgSolve(hdt, muscle);
    // the landing's thud (once, the hardest)
    if (g_rg.impact > 2.0f && g_rg.sounds == 0) {
        Sound_Play(SND_THUD, Clamp01(g_rg.impact / 5.0f), 0.85f + 0.1f * Rnd());
        g_rg.sounds = 1;
        Input_Rumble(0.6f, 0.6f, 0.25f);
    }
    // settling: nearly still for a while -> down
    float maxV = 0.0f;
    for (int i = 0; i < RP_N; i++) maxV = fmaxf(maxV, Len(g_rg.p[i] - g_rg.q[i]) / hdt);
    g_rg.still = maxV < 0.25f ? g_rg.still + dt : 0.0f;
    if (g_rg.phase == RG_FALL && ((g_rg.still > 0.35f && g_rg.phaseT > 0.8f) || g_rg.phaseT > 4.0f)) {
        g_rg.phase = RG_DOWN, g_rg.phaseT = 0.0f;
        Sound_Play(SND_GROAN, 0.7f, 0.95f + 0.1f * Rnd());
    }
    if (g_rg.phase == RG_DOWN && g_rg.asleep) {   // asleep: snoring (a little puff each time), then he wakes with a snort
        g_rg.snoreT -= dt;
        if (g_rg.snoreT <= 0.0f && g_rg.phaseT < 6.0f) {
            g_rg.snoreT = 1.7f;
            Sound_Play(SND_SNORE, 0.8f, 0.95f + 0.1f * Rnd());
            for (int i = 0; i < 2 && G.npuffs < 24; i++) {
                Puff& pf = G.puffs[G.npuffs++];
                pf = {g_rg.p[RP_NOSE] + UP * (0.10f * k + 0.05f * i), 0, 1.4f, 0.04f + 0.02f * i, Rnd() * 6.3f, V3(0.85f, 0.92f, 1.0f)};
            }
        }
        if (g_rg.phaseT > 6.5f) {
            g_rg.asleep = false, g_rg.phaseT = 0.0f;
            Sound_Play(SND_GROAN, 0.9f, 1.1f);
        }
    }
    if (g_rg.phase == RG_DOWN && !g_rg.asleep && g_rg.phaseT > kRgDownTime * (0.7f + 0.6f * fminf(g_rg.level, 1.3f))) {
        // which way up: his chest's facing (from his torso block) - down: up from his front; up: roll over first
        Vec3 up = Norm(g_rg.p[RP_SP2] - g_rg.p[RP_PELVIS]);
        Vec3 side = Norm(g_rg.p[RP_SHR] - g_rg.p[RP_SHL]);
        Vec3 chest = Norm(Cross(side, up));   // (his chest's way: forward)
        Vec3 belly = Norm(g_rg.p[RP_BELLY] - (g_rg.p[RP_PELVIS] + g_rg.p[RP_SP2]) * 0.5f);
        if (Dot(belly, chest) < 0.0f) chest = chest * -1.0f;
        g_rg.prone = chest.y < 0.0f;
        Vec3 f = g_rg.p[RP_NECK] - g_rg.p[RP_PELVIS];
        g_rg.upFwd = Norm(V3(f.x, 0, f.z) + V3(0, 0, 1e-5f));
        g_rg.upPos = RgClearOfWalls(g_rg.p[RP_PELVIS]);
        g_rg.attempt++;
        g_rg.willFail = g_rg.attempt == 1 && Rnd() < kRgFailP * fminf(g_rg.level, 1.3f);
        g_rg.phase = g_rg.prone ? RG_GETUP : RG_ROLL;
        if (g_rg.prone) RgMoveBody();
        g_rg.phaseT = 0.0f;
        if (g_rgLog) Log("ragdoll: %s, getting up (try %d%s)", g_rg.prone ? "face down" : "on his back", g_rg.attempt,
                         g_rg.willFail ? ", it will give way" : "");
    }
    if (g_rg.phase == RG_ROLL && g_rg.phaseT > 1.0f) {
        g_rg.prone = true;
        Vec3 f = g_rg.p[RP_NECK] - g_rg.p[RP_PELVIS];
        g_rg.upFwd = Norm(V3(f.x, 0, f.z) + V3(0, 0, 1e-5f));
        g_rg.upPos = RgClearOfWalls(g_rg.p[RP_PELVIS]);
        g_rg.phase = RG_GETUP, g_rg.phaseT = 0.0f;
        RgMoveBody();
    }
    if (g_rg.phase == RG_BLEND) {
        g_rg.blend -= dt / 0.35f;
        if (g_rg.blend <= 0.0f) {
            g_rg.phase = RG_OFF;
            return 0.0f;
        }
        return g_rg.blend;
    }
    return 1.0f;
}

// where the game's body should be walked to (under the one we draw) and which way it should face: -> true while it's needed
static bool Ragdoll_RootGoal(Vec3* at, float* heading)
{
    if (g_rg.phase == RG_OFF || g_rg.phase == RG_FALL) return false;
    Vec3 p = g_rg.phase >= RG_GETUP ? g_rg.upPos : g_rg.p[RP_PELVIS];
    *at = V3(p.x, 0, p.z);
    Vec3 f = g_rg.phase >= RG_GETUP ? g_rg.upFwd : g_rg.dir;
    *heading = atan2f(f.x, f.z);
    return true;
}

// ------------------------------------------------------------------ his skeleton from the particles
// the torso's and the head's turn from their points (a frame from three of them, then and now); the limbs swung onto their sticks
// on top of their parent's turn (their twist inherited)
static Mat4 RgFrame(Vec3 a, Vec3 b, Vec3 c, Vec3 d)   // (up: a -> b; side: c -> d)
{
    Vec3 Y = Norm(b - a), X = d - c;
    X = Norm(X - Y * Dot(X, Y));
    return Basis(X, Y, Cross(X, Y));
}
static void Ragdoll_Pose(Mat4* W, int n)
{
    const Vec3* p = g_rg.p;
    const Vec3* r = g_rg.rest;
    const Mat4* W0 = g_rg.W0;
    Mat4 Ft0 = RgFrame(r[RP_PELVIS], r[RP_SP2], r[RP_SHL], r[RP_SHR]), Ft = RgFrame(p[RP_PELVIS], p[RP_SP2], p[RP_SHL], p[RP_SHR]);
    Mat4 Dt = Mul(Transpose3(Ft0), Ft);   // (the torso's turn, world)
    Mat4 Fh0 = RgFrame(r[RP_NECK], r[RP_TOP], r[RP_HEAD], r[RP_NOSE]), Fh = RgFrame(p[RP_NECK], p[RP_TOP], p[RP_HEAD], p[RP_NOSE]);
    Mat4 Dh = Mul(Transpose3(Fh0), Fh);
    // the torso block moved rigidly: every joint of it (and the roots) by the torso's turn about the pelvis
    Vec3 piv0 = r[RP_PELVIS], piv = p[RP_PELVIS];
    auto Rigid = [&](int j, const Mat4& D, Vec3 p0, Vec3 pn) {
        Vec3 pos = pn + XformDir(Row(W0[j], 3) - p0, D);
        W[j] = WithTrans(Mul(Rot3(W0[j]), D), pos);
    };
    for (int j : {0, 1, 2, 3, 4, 13, 14, 15}) if (j < n) Rigid(j, Dt, piv0, piv);
    // the neck and head: the head block's turn about the neck point
    for (int j : {16, 17, 18}) Rigid(j, Dh, r[RP_NECK], p[RP_NECK]);
    // a limb: each joint swung from its rest direction (turned by its parent's turn) onto its stick now
    auto Limb = [&](int j, int pj, int pc, const Mat4& parentD, Mat4* outD) {
        Vec3 d0 = XformDir(r[pc] - r[pj], parentD), d1 = p[pc] - p[pj];
        Mat4 D = Mul(parentD, RotBetween(Norm(d0), Norm(d1)));
        W[j] = WithTrans(Mul(Rot3(W0[j]), D), p[pj]);
        if (outD) *outD = D;
    };
    Mat4 D1, D2, D3;
    // legs (from the torso's turn)
    Limb(5, RP_HIPL, RP_KNEEL, Dt, &D1), Limb(6, RP_KNEEL, RP_ANKL, D1, &D2), Limb(7, RP_ANKL, RP_BALLL, D2, &D3);
    if (n > 8) W[8] = Mul(Mul(W0[8], FullInverse(W0[7])), W[7]);
    Limb(9, RP_HIPR, RP_KNEER, Dt, &D1), Limb(10, RP_KNEER, RP_ANKR, D1, &D2), Limb(11, RP_ANKR, RP_BALLR, D2, &D3);
    if (n > 12) W[12] = Mul(Mul(W0[12], FullInverse(W0[11])), W[11]);
    // arms
    Limb(19, RP_SHL, RP_ELBL, Dt, &D1), Limb(20, RP_ELBL, RP_WRL, D1, &D2), Limb(21, RP_WRL, RP_HANDL, D2, &D3);
    for (int j = 22; j <= 25 && j < n; j++) W[j] = Mul(Mul(W0[j], FullInverse(W0[g_parent[j]])), W[g_parent[j]]);
    Limb(26, RP_SHR, RP_ELBR, Dt, &D1), Limb(27, RP_ELBR, RP_WRR, D1, &D2), Limb(28, RP_WRR, RP_HANDR, D2, &D3);
    for (int j = 29; j <= 32 && j < n; j++) W[j] = Mul(Mul(W0[j], FullInverse(W0[g_parent[j]])), W[g_parent[j]]);
    for (int j = 33; j < n; j++) W[j] = Mul(Mul(W0[j], FullInverse(W0[g_parent[j]])), W[g_parent[j]]);
}

// the pose for the skin: the ragdoll's skeleton (blended back into the game's own as he stands)
static void BuildPoseRagdoll(const PoserPose& P)
{
    int n = P.numJoints < 40 ? P.numJoints : 40;
    if (!LoadInvBind(P.numJoints) || n < 33) {
        g_skin.active = false;
        return;
    }
    static Mat4 W[40], A[40];
    Ragdoll_Pose(W, n);
    if (g_rg.phase == RG_BLEND) {   // back to the game's pose: the pelvis (and the roots) blended in the world, every other joint
        // in its parent's frame on the game's own bone (blending each joint's world place, his bones shrank to a quarter for a frame)
        float b = DnEase(g_rg.blend);
        static Mat4 R[40];
        for (int j = 0; j < n; j++) R[j] = W[j], A[j] = ToMat(P.world[j]);
        for (int j = 0; j < n; j++) {
            int pj = g_parent[j];
            if (j <= 4 || pj < 0) {
                W[j] = WithTrans(BlendRot(A[j], R[j], b), Lerp(Row(A[j], 3), Row(R[j], 3), b));
                continue;
            }
            Mat4 la = Mul(Rot3(A[j]), Transpose3(Rot3(A[pj]))), lr = Mul(Rot3(R[j]), Transpose3(Rot3(R[pj])));
            Mat4 L = WithTrans(BlendRot(la, lr, b), Row(ToMat(P.object[j]), 3));
            W[j] = Mul(L, W[pj]);
        }
    }
    bool roots[40] = {};
    roots[0] = true;   // (all of him)
    G.haveBody = true;
    G.knocked = true;
    SkinPrepare(P, W, roots);
    PtSnapshot();
}

static bool RagdollDebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "fall")) {   // "fall <heading deg rel. to him>,<speed>": push him over now (0 forward, 180 back, 90 his left)
        if (Ragdoll_Active()) return true;
        float deg = 0, spd = 1.5f;
        sscanf(arg, "%f,%f", &deg, &spd);
        float hy = G.haveBody ? G.bodyYaw : G.yaw;
        float a = hy + deg * PI / 180.0f;
        Ragdoll_Fall(V3(sinf(a), 0, cosf(a)), spd, fmaxf(g_drunk.level, 0.8f));
        return true;
    }
    if (!strcmp(cmd, "teeter")) {   // "teeter <deg rel. to him>,<speed>": teeter, then fall (as a drunk fall goes)
        if (Ragdoll_Active() || g_drunk.teeterT >= 0.0f) return true;
        float deg = 0, spd = 1.5f;
        sscanf(arg, "%f,%f", &deg, &spd);
        float hy = G.haveBody ? G.bodyYaw : G.yaw;
        float a = hy + deg * PI / 180.0f;
        Drunk_Teeter(V3(sinf(a), 0, cosf(a)), spd);
        return true;
    }
    if (!strcmp(cmd, "passout")) {   // "passout": down he goes, asleep (as at the top level, standing still)
        if (Ragdoll_Active()) return true;
        float hy = G.haveBody ? G.bodyYaw : G.yaw;
        Ragdoll_PassOut(V3(-sinf(hy), 0, -cosf(hy)), fmaxf(g_drunk.level, 1.4f));
        return true;
    }
    if (!strcmp(cmd, "rgwalls")) {   // "rgwalls x,z": the walls a fall there would know (to stage one)
        float x = 0, z = 0, y = 0;
        if (sscanf(arg, "%f,%f", &x, &z) < 2 || !Trace_GroundY(V3(x, 50.0f, z), &y)) return Log("rgwalls: x,z (on the ground)"), true;
        auto s = g_rg;
        RgProbeWalls(V3(x, y, z), 1.0f);
        for (int i = 0; i < g_rg.nwall; i++)
            Log("rgwalls: (%.2f %.2f %.2f) normal (%.2f %.2f) %.2f m", g_rg.wallP[i].x, g_rg.wallP[i].y, g_rg.wallP[i].z, g_rg.wallN[i].x,
                g_rg.wallN[i].z, Len(V3(g_rg.wallP[i].x - x, 0, g_rg.wallP[i].z - z)));
        Log("rgwalls: %d at (%.2f %.2f %.2f)", g_rg.nwall, x, y, z);
        g_rg = s;
        return true;
    }
    if (!strcmp(cmd, "rglog")) {
        g_rgLog = arg[0] ? atoi(arg) : 1;
        Log("ragdoll: phase %d t %.2f phaseT %.2f still %.2f prone %d attempt %d k %.2f impact %.1f", g_rg.phase, g_rg.t, g_rg.phaseT,
            g_rg.still, (int)g_rg.prone, g_rg.attempt, g_rg.k, g_rg.impact);
        return true;
    }
    if (!strcmp(cmd, "rgtune")) {   // "rgtune name=value,..."
        static const struct { const char* name; float* v; } kv[] = {
            {"gravity", &kRgGravity}, {"reach", &kRgReach}, {"failp", &kRgFailP}, {"downtime", &kRgDownTime}};
        char buf[300];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        for (char* q = strtok(buf, ","); q; q = strtok(nullptr, ",")) {
            char* eq = strchr(q, '=');
            if (!eq) continue;
            *eq = 0;
            for (auto& e : kv)
                if (!strcmp(e.name, q)) *e.v = (float)atof(eq + 1);
        }
        return true;
    }
    return false;
}
