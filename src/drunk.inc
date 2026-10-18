// Drunk mode (Item 2 of the Weapons Lab, with beer.inc and ragdoll.inc): how drunk he is - one number, going up with every can and
// wearing off over minutes - and what it does to him. Included from gun.cpp before beer.inc.
//
// His balance is a physical model, not an animation: his body is an inverted pendulum on his feet (research/beer/techniques.md -
// the capture point idea). Gravity tips it; a balance reflex - late, weak and shaky, the more so the more he's drunk - pushes back;
// his own starts, stops and turns rock it (his feet go, his body stays). When it leans too far, his legs are steered under it - a
// stagger, through the game's own walking (game.cpp: Game_DrunkWalk) - and past the point of no return he falls (ragdoll.inc).
// What you see is that same lean (the pose layer), his head lagging and lolling, his arms out for balance, his eyelids and jaw,
// and a camera that leans with him.

// ------------------------------------------------------------------ how drunk
// 0 sober .. 1 legless (1.6 at most: more cans keep it there longer). Four cans take him to ~1. A drink is felt over a few
// seconds, not at once; it wears off over minutes.
static struct {
    float level;        // what the drink has done to him (the effects follow this)
    float pending;      // drunk but not yet felt: comes in over a few seconds
    float timeSober;    // (report) seconds since he was last fully sober
    int cans;           // (report) cans this session
    // the balance: his lean (radians; horizontal world vector, x and z: which way and how far his body leans off his feet), its rate
    Vec3 lean, leanVel;
    struct { float t; Vec3 lean, vel; } hist[96];   // (the reflex sees the lean late)
    int hn;
    float clock;
    Vec3 noise;         // (a slow random push: his wobble)
    Vec3 rootVel, rootPos;
    bool haveRoot;
    float stepW;        // how much his legs are being steered under him (0 .. 1)
    float stumbles;     // (report)
    float tripT;        // a trip's push still to come (s)
    Vec3 tripDir;
    float headLag[2];   // the head's lag behind the lean (a spring), its rate
    float hiccupT;      // next hiccup in (s)
    float hicT;         // a hiccup's jolt (s since; < 0 none)
    float fallCool;     // (no new fall for a moment after getting up)
    float lurchCool;    // (time to the next near miss can come)
    float lids, jaw;    // (face) eyelids' droop, the jaw's slack
    bool fallen;        // on the ground (ragdoll.inc)
    float teeterT;      // >= 0: hanging at the tipping point (s into it), arms going round - then over he goes
    float teeterLen;
    Vec3 teeterDir;
    float teeterSpeed;
    float idleT;        // seconds standing still (legless: he passes out)
    float standW;       // (v1.0) 0 walking .. 1 standing: his balance's reflex as at level 1 at most, standing (eased)
} g_drunk = {};
static float kDrunkMax = 1.6f;
static float kDrunkWearOff = 0.0045f;   // per second at level 1 (and more above): ~3.5 min from 1 to 0
static float kDrunkFeel = 0.35f;        // how fast a drink is felt (share per second of what is pending)
static float g_drunkSet = -1.0f;        // debug "drunk <level>": held there (-1 off)
// the balance's numbers (dtune)
static float kDrH = 0.78f;              // his centre of mass above his feet (m, x k): Homer 0.78 -> the pendulum's 3.55 rad/s
static float kDrStepAt = 0.11f;         // lean (rad) from which his legs are steered under him
static float kDrFallAt = 0.42f;         // lean (rad) past which he can't catch himself
static float kDrNoise = 0.60f;          // the wobble (rad/s^2, its spread, at level 1)
static float kDrInertia = 0.40f;        // how much his own starts/stops/turns rock him (x the level: sober, he sees them coming)
static float kDrTripRate = 0.035f;      // trips a second while walking at level 1 (squared with the level)
static float kDrLurchRate = 0.30f;      // near misses a second at level 1 (a lurch he just catches - a big, awkward step or two)
static float kDrLeanShow = 1.5f;        // how much of the lean the pose shows (a cartoon: a bit more than it is; v1.0, on Radical's walk:
                                        // was 1.25)
static float kDrWaddle = 0.10f;         // (v1.0) walking, his roll over onto each foot (rad at level 1)
// (falls and passing out for those the drunk body doesn't draw - Marge, Apu: the old particle ragdoll, the stretched and folded-up
// fall the review turned down; off for the release - they stagger and catch themselves at the edge instead)
static bool kDrFamilyFalls = false;
// (kDrCam: the camera's lean and drift, 0 off - defined in gun.cpp before the camera code)
static int g_drunkLog = 0;

static void Drunk_AddDrink(float amount) { g_drunk.pending += amount; }
static bool Drunk_Staggering() { return g_drunk.stepW > 0.35f && g_drunk.level > 0.03f && g_drunk.teeterT < 0.0f; }
static bool g_drunkOnFoot = true;   // (the pose layer: on foot only - in his car, his seat's pose is left alone)
static float Drunk_Level() { return g_drunk.level; }
static bool Drunk_Active() { return g_drunk.level > 0.03f || g_drunk.fallen; }
static void Drunk_Teeter(Vec3 dir, float speed);

// ------------------------------------------------------------------ the fall (ragdoll.inc)
static bool Ragdoll_Active();
static void Ragdoll_Fall(Vec3 dir, float speed, float level);
static float Ragdoll_Update(float dt);   // -> 1 while he can't walk (on the ground, getting up), easing to 0 as he stands
static bool Ragdoll_RootGoal(Vec3* at, float* heading);
static void Ragdoll_PassOut(Vec3 back, float level);
// (drunkbody.inc: the drunk body - his walk on his own feet and the fall that keeps his shape - in place of the ragdoll)
static bool DrunkBody_Update(float dt, bool onFoot, float level);
static bool DrunkBody_Fall(Vec3 dir, float speed, float lean, float level);
static bool DrunkBody_Falling();
static bool DrunkBody_On();
static bool DrunkBody_Pose(PoserPose& P);
static bool DrunkBody_Handles();
static void DrunkBody_SeePose(const PoserPose& P);
// (v1.0) the game's own moves are its own - a jump, a kick, a car's knock and the get-up after it, walking to a car door: no stagger,
// trip or fall while one runs, and the sway eases off him (drawn over them, the drunk body's walk glided him along the ground sitting
// down as a car's knock threw him - the Windows play-test). g_drunkLayerW: how much of the sway is on him (0 .. 1)
static float g_drunkLayerW = 1.0f;
static bool g_drunkPaused = false;      // (gun.cpp, each frame: the pause menu is up)
static bool g_drunkGameKnock = false;   // (the pose layer: the game's own pose has him knocked over - his head low over his root)

// ------------------------------------------------------------------ per frame
static void Drunk_Teeter(Vec3 dir, float speed)
{
    g_drunk.teeterT = 0.0f;
    g_drunk.teeterLen = 0.35f + 0.25f * Clamp01(g_drunk.level - 0.5f);
    g_drunk.teeterDir = Norm(V3(dir.x, 0, dir.z) + V3(0, 0, 1e-5f));
    g_drunk.teeterSpeed = speed;
    Sound_Play(SND_WHOA, 0.9f, 0.95f + 0.1f * Rnd());
}
static float DrRand(uint32_t& s) { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; }
#include "drunk_trip.h"   // (natural trips: a hazard process on a seeded stream of their own - "drunk seed,<n>", "drunk trips")
static DrTripGen g_drTrip;
static bool g_drTripInit = false;
static bool PlayerIsKid();          // (gun.cpp: Bart or Lisa)
static void DrunkBody_Reset();      // (drunkbody.inc)
static void Ragdoll_Reset();        // (ragdoll.inc)
// sober, now: nothing of a drink left in him or in what he's doing (a new level's player - whoever drank isn't who's playing now)
static void Drunk_Reset(const char* why)
{
    if (g_drunk.level > 0.0f || g_drunk.pending > 0.0f || g_drunk.fallen) Log("drunk: sober (%s; was %.2f)", why, g_drunk.level);
    float clock = g_drunk.clock;
    int cans = g_drunk.cans;
    g_drunk = {};
    g_drunk.clock = clock, g_drunk.cans = cans;
    g_drunk.teeterT = g_drunk.hicT = -1.0f, g_drunk.hiccupT = 8.0f;
    DrunkBody_Reset();
    Ragdoll_Reset();
}
static void Drunk_Update(float dt, bool onFoot, uint32_t player)
{
    static bool init = false;
    if (!init) g_drunk.teeterT = g_drunk.hicT = -1.0f, g_drunk.hiccupT = 8.0f, init = true;
    static uint32_t lastPlayer = 0;
    if (player && player != lastPlayer) {   // (a new level: whoever drank, the player starts sober)
        if (lastPlayer) Drunk_Reset("a new player");
        lastPlayer = player;
    }
    if (PlayerIsKid() && g_drunkSet < 0.0f && (g_drunk.level > 0.0f || g_drunk.pending > 0.0f)) Drunk_Reset("one of the kids");
    if (dt <= 0.0f || g_drunkPaused) return;   // (v1.0, paused: all of it held as it was - his sway, his walk's filter, a fall halfway
                                               // down; it went on as if he'd got into a car: the sway dropped off him at once, the filter
                                               // started over and a fall was called off - he stood up in one frame after the menu)
    float felt = g_drunk.pending * Clamp01(kDrunkFeel * dt);
    g_drunk.pending -= felt;
    g_drunk.level = fminf(kDrunkMax, g_drunk.level + felt);
    if (g_drunkSet >= 0.0f) g_drunk.level = g_drunkSet;
    else if (g_drunk.level > 0.0f) g_drunk.level = fmaxf(0.0f, g_drunk.level - kDrunkWearOff * dt * (0.6f + 0.4f * fmaxf(g_drunk.level, 0.3f)));
    g_drunk.timeSober = g_drunk.level > 0.0f ? g_drunk.timeSober + dt : 0.0f;
    float L = g_drunk.level, Lc = fminf(L, 1.3f);
    g_drunk.clock += dt;
    g_drunkOnFoot = onFoot;
    g_drunk.fallCool = fmaxf(0.0f, g_drunk.fallCool - dt);

    // his feet: where he is and how fast he goes (the pendulum's base)
    Vec3 pos;
    bool have = onFoot && PlayerPos(&pos);
    Vec3 acc = {0, 0, 0};
    if (have) {
        if (g_drunk.haveRoot) {
            Vec3 v = (pos - g_drunk.rootPos) * (1.0f / dt);
            v.y = 0;
            if (Len(v) > 12.0f) v = g_drunk.rootVel;   // (a warp, not a step)
            Vec3 sm = g_drunk.rootVel + (v - g_drunk.rootVel) * Clamp01(dt * 12.0f);
            acc = (sm - g_drunk.rootVel) * (1.0f / dt);
            float al = Len(acc);
            if (al > 6.0f) acc = acc * (6.0f / al);   // (a walk doesn't do more: past that it's a collision or a warp)
            g_drunk.rootVel = sm;
        }
        g_drunk.rootPos = pos, g_drunk.haveRoot = true;
    } else
        g_drunk.haveRoot = false, g_drunk.rootVel = {0, 0, 0};

    // the fall in progress: the ragdoll has him (no balance to keep; the walk held)
    bool wasDown = g_drunk.fallen;
    float down = Ragdoll_Update(dt);
    g_drunk.fallen = Ragdoll_Active() || DrunkBody_Falling();
    if (wasDown && !g_drunk.fallen) {   // (up again: a moment to find his feet - and the walk to where he lay is no stumble)
        g_drunk.fallCool = 4.0f;
        g_drunk.rootVel = {0, 0, 0}, acc = {0, 0, 0};
        g_drunk.lean = g_drunk.leanVel = {0, 0, 0};
        g_drunk.hn = 0;
        // (and he comes up not quite balanced: a sway he has to catch - a step or two - before he walks; never enough to go over
        // again, fallCool holds that)
        float a = g_drunk.clock * 7.31f, Lw = fminf(g_drunk.level, 1.3f);
        g_drunk.leanVel = V3(sinf(a), 0, cosf(a)) * (0.22f + 0.14f * Lw);
    }
    // (v1.0) a move of the game's own running (Game_WalkerActive: his walk isn't what it runs now; or its pose has him knocked over); and
    // whether he means to walk at all - the stick or the keys as the game read them, before the drunk filter: standing, his feet are
    // his own and the sway is all of it (a stagger walked him about as he stood - two steps a second, drifting: the Windows play-test)
    bool gameOwns = onFoot && player && (!Game_WalkerActive(player) || g_drunkGameKnock);
    g_drunkLayerW = Clamp01(g_drunkLayerW + (gameOwns ? -dt / 0.12f : dt / 0.35f));
    float wiD, wiW, woD, woW;
    Game_DrunkWalkState(&wiD, &wiW, &woD, &woW);
    bool wish = wiW > 0.01f;
    if (onFoot && player && L > 0.2f) Game_HoldIdleTimer(player);   // (no bored fidgets drunk: his idle is the drunk one)
    if (g_drunk.fallen || down > 0.0f) {
        g_drunk.lean = g_drunk.lean * fmaxf(0.0f, 1.0f - dt * 4.0f), g_drunk.leanVel = {0, 0, 0};
        // (the game's body held still where he went over; it's moved to where he gets up as he stands - ragdoll.inc)
        Game_DrunkWalk(player, onFoot, L, 0, 0, 0, true);
        g_drunk.stepW = 0;
    } else if (L < 0.03f || !onFoot) {
        g_drunk.lean = g_drunk.leanVel = {0, 0, 0};
        g_drunk.stepW = 0;
        Game_DrunkWalk(player, false, 0, 0, 0, 0, false);
    } else if (gameOwns) {   // (v1.0: the game's own move - no balance kept against it, nothing steered)
        float k = fmaxf(0.0f, 1.0f - dt * 4.0f);
        g_drunk.lean = g_drunk.lean * k, g_drunk.leanVel = g_drunk.leanVel * k;
        g_drunk.stepW = 0, g_drunk.tripT = 0;
        Game_DrunkWalk(player, false, 0, 0, 0, 0, false);
    } else {
        // the pendulum (substeps of 1/120 s): gravity tips it, the reflex (late, weak, shaky) holds it, his feet's starts and
        // stops rock it, a slow random wobble, and the odd trip
        float k = 1.0f;   // (Homer's size; the others the same pendulum - close enough)
        float h = kDrH * k, w0 = sqrtf(9.81f / h);
        // (the numbers from research/beer/balance_sim.py: standing, he sways and from ~0.9 takes the odd catch step, at 1.3 he
        // staggers about without falling; walking, steps from ~0.7 and falls at 1.3 a few times a minute - and trips on top)
        // (v1.0: standing, nothing steps under him now - his feet stay put - so his reflex is as at level 1 at most: later and shakier
        // past it, it rang - a 0.9 Hz sway that grew till he went over within seconds, standing still at 1.4. Walking, the drink's
        // full measure: his staggers catch what it doesn't)
        g_drunk.standW += ((wish ? 0.0f : 1.0f) - g_drunk.standW) * Clamp01(dt * 2.0f);
        float Lr = Lc - (Lc - fminf(Lc, 1.0f)) * g_drunk.standW;
        float kp = w0 * w0 * (3.0f - 0.85f * fminf(Lr, 1.0f)), kd = 2.0f * sqrtf(kp - w0 * w0) * (1.0f - 0.25f * fminf(Lr, 1.0f));
        float delay = 0.05f + 0.08f * Lr;
        static uint32_t seed = 12345u;
        float sig = kDrNoise * Lr * Lr, lam = 1.2f;   // (the wobble: a slow random push, its spread sig)
        auto gauss = [&]() { return (DrRand(seed) + DrRand(seed) + DrRand(seed) - 1.5f) * 2.0f; };
        g_drunk.noise = g_drunk.noise - g_drunk.noise * (lam * dt) + V3(gauss(), 0, gauss()) * (sig * sqrtf(2.0f * lam * dt));
        // a trip while walking: his foot catches - his body thrown forward (and a little to a side)
        float spd = Len(g_drunk.rootVel);
        // (round 4: a hazard process on its own seeded stream - drunk_trip.h; was a coin tossed each frame on the wobble's stream: no
        // seed could repeat a walk's trips, and the stress test's 30 s legless walk waited on one by chance)
        if (!g_drTripInit) DrTripSeed(g_drTrip, 12345u), g_drTripInit = true;
        float tside = 0.0f;
        if (g_drunk.tripT <= 0.0f && DrTripStep(g_drTrip, dt, kDrTripRate, L, spd, &tside)) {
            Vec3 fw = Norm(g_drunk.rootVel), sd = Cross(UP, fw);
            g_drunk.tripDir = Norm(fw + sd * (tside * 1.2f));
            g_drunk.tripT = 0.12f;
            g_drunk.stumbles += 1.0f;
            Log("drunk: trip %d (natural: level %.2f, speed %.1f, at hazard %.4f of seed %u)", g_drTrip.n, L, spd, (float)g_drTrip.total, g_drTrip.seed);
        }
        // a near miss: his weight goes - to a side mostly - further than a sway, not as far as a trip; he catches it with a big,
        // awkward step (often across the other foot), arms out, knees giving. Drunker, more often
        g_drunk.lurchCool -= dt;
        if (g_drunk.tripT <= 0.0f && g_drunk.lurchCool <= 0.0f && L > 0.4f && g_drunk.fallCool <= 0.0f && wish &&
            DrRand(seed) < kDrLurchRate * Clampf((L - 0.4f) / 0.6f, 0.0f, 1.2f) * dt) {   // (v1.0: walking only)
            Vec3 fw = spd > 0.3f ? Norm(g_drunk.rootVel) : HeadingDir(G.bodyYaw), sd = Cross(UP, fw);
            float side = DrRand(seed) < 0.5f ? -1.0f : 1.0f;
            g_drunk.tripDir = Norm(sd * side + fw * (DrRand(seed) * 0.8f - 0.2f));
            g_drunk.tripT = 0.045f + 0.03f * DrRand(seed);
            g_drunk.lurchCool = 2.0f + 2.5f * DrRand(seed);
            if (g_drunkLog) Log("drunk: lurch (level %.2f)", L);
        }
        int sub = (int)ceilf(dt * 120.0f);
        sub = sub < 1 ? 1 : sub > 8 ? 8 : sub;
        float hdt = dt / sub;
        for (int s = 0; s < sub; s++) {
            // (each substep at its own time - v1.0: every one was stamped with the frame's, so the reflex saw a state whole frames
            // old: at 16 fps its 0.13 s came to 0.19, and standing still his sway grew into a shudder of 6 rad/s till he went over;
            // the lab's 18-30 fps had it too)
            float ts = g_drunk.clock - dt + s * hdt;
            g_drunk.hist[g_drunk.hn % 96] = {ts, g_drunk.lean, g_drunk.leanVel};
            g_drunk.hn++;
            Vec3 dl = g_drunk.lean, dv = g_drunk.leanVel;
            for (int i = 1; i < 96 && i <= g_drunk.hn; i++) {
                const auto& e = g_drunk.hist[(g_drunk.hn - i) % 96];
                dl = e.lean, dv = e.vel;
                if (ts - e.t >= delay) break;
            }
            Vec3 a = g_drunk.lean * (w0 * w0) - dl * kp - dv * kd + g_drunk.noise;
            a = a - acc * (kDrInertia * Lc / h);   // (his feet speeding up leaves his body behind; stopping throws it forward)
            if (g_drunk.tripT > 0.0f) a = a + g_drunk.tripDir * (9.0f * Lc);
            a.y = 0;
            g_drunk.leanVel = g_drunk.leanVel + a * hdt;
            g_drunk.lean = g_drunk.lean + g_drunk.leanVel * hdt;
        }
        g_drunk.tripT -= dt;
        // his legs steered under him: past kDrStepAt the walk goes the lean's way, faster the further he leans - and his feet
        // catching up with his body is what brings it back (the inertia term above)
        float lm = Len(g_drunk.lean);
        // (no fall allowed just now - just up, or teetering already: the lean held short of the edge)
        if (lm > kDrFallAt * 0.97f && (g_drunk.fallCool > 0.0f || Ragdoll_Active())) {
            g_drunk.lean = g_drunk.lean * (kDrFallAt * 0.97f / lm);
            Vec3 ldir = g_drunk.lean * (1.0f / Len(g_drunk.lean));
            float out = Dot(g_drunk.leanVel, ldir);
            if (out > 0.0f) g_drunk.leanVel = g_drunk.leanVel - ldir * out;
            lm = kDrFallAt * 0.97f;
        }
        // his legs go to the capture point - where the lean is heading, not where it is (the lean plus its rate over the
        // pendulum's frequency): stepping where it is, the steps came late and rocked him the other way
        Vec3 cp = g_drunk.lean + g_drunk.leanVel * (1.0f / w0);
        float cm = Len(cp);
        float sw = Clamp01((cm - kDrStepAt) / kDrStepAt);
        if (g_drunk.fallCool > 2.8f) sw = 0.0f;   // (just up: his own feet catch the sway - the game's walk dragged about by it
                                                  // zigzagged his body as he stood)
        g_drunk.stepW = g_drunk.stepW + (sw - g_drunk.stepW) * Clamp01(dt * 10.0f);
        Vec3 ld = lm > 1e-5f ? g_drunk.lean * (1.0f / lm) : V3(0, 0, 1);
        Vec3 cd = cm > 1e-5f ? cp * (1.0f / cm) : ld;
        float head = atan2f(cd.x, cd.z);
        float speed = fminf(4.0f, 0.4f + 5.0f * cm);
        // (v1.0: standing, nothing - his sway is his upper body's; walking, his way bends toward it and he quickens: game.cpp)
        Game_DrunkWalk(player, true, L, head, speed, wish ? g_drunk.stepW : 0.0f, false);
        // past the point of no return: he teeters there a moment - arms going round, feet stuck, "whoa" - then over he goes (the
        // way he leans, as fast as he was going)
        // legless and standing still a while, nothing in his hands: he passes out where he stands (backwards, slowly)
        g_drunk.idleT = (L > 1.35f && Len(g_drunk.rootVel) < 0.3f && G.held == WPN_NONE) ? g_drunk.idleT + dt : 0.0f;
        if (g_drunk.idleT > 7.0f && g_drunk.fallCool <= 0.0f && !Ragdoll_Active() && g_drunk.teeterT < 0.0f && !DrunkBody_On() &&
            kDrFamilyFalls) {
            g_drunk.idleT = 0.0f;
            float hy = G.haveBody ? G.bodyYaw : G.yaw;
            Ragdoll_PassOut(V3(-sinf(hy), 0, -cosf(hy)), L);
            g_drunk.fallCool = 3.0f;
        }
        if (lm > kDrFallAt && g_drunk.fallCool <= 0.0f && !Ragdoll_Active() && g_drunk.teeterT < 0.0f && !DrunkBody_Falling()) {
            if (DrunkBody_On()) {
                // the drunk body: over he goes forward (its own teeter, then his knees, his hands); a lean past the edge behind him or to
                // a side is his feet's to catch - held at the edge, his walk going under it
                float hy = G.haveBody ? G.bodyYaw : G.yaw;
                Vec3 fwd = HeadingDir(hy);
                if (Dot(ld, fwd) > 0.35f && DrunkBody_Fall(ld, fminf(2.5f, Len(g_drunk.rootVel)), lm, L)) {
                    g_drunk.fallCool = 3.0f;
                    g_drunk.lean = g_drunk.leanVel = {0, 0, 0};
                } else {
                    g_drunk.lean = ld * (kDrFallAt * 0.97f);
                    float outV = Dot(g_drunk.leanVel, ld);
                    if (outV > 0.0f) g_drunk.leanVel = g_drunk.leanVel - ld * outV;
                }
            } else if (kDrFamilyFalls)
                Drunk_Teeter(ld, fminf(3.5f, Len(g_drunk.rootVel) + Len(g_drunk.leanVel) * h));
            else {   // (no fall for them: held at the edge, his walk going under it - as the drunk body does a lean it won't fall)
                g_drunk.lean = ld * (kDrFallAt * 0.97f);
                float outV = Dot(g_drunk.leanVel, ld);
                if (outV > 0.0f) g_drunk.leanVel = g_drunk.leanVel - ld * outV;
            }
        }
    }
    // the teeter: the lean hangs at the tipping point (a little further, slowly), his walk held - then the fall
    if (g_drunk.teeterT >= 0.0f) {
        g_drunk.teeterT += dt;
        Vec3 d = g_drunk.teeterDir;
        float ext = kDrFallAt + 0.06f * g_drunk.teeterT / g_drunk.teeterLen + 0.015f * sinf(g_drunk.teeterT * 21.0f);
        g_drunk.lean = d * ext, g_drunk.leanVel = {0, 0, 0};
        g_drunk.stepW = 0;
        Game_DrunkWalk(player, onFoot, L, 0, 0, 0, true);
        if (g_drunk.teeterT >= g_drunk.teeterLen) {
            if (g_drunkLog) Log("drunk: falls (level %.2f)", L);
            g_drunk.teeterT = -1.0f;
            Ragdoll_Fall(d, g_drunk.teeterSpeed, L);
            g_drunk.fallCool = 3.0f;
            g_drunk.lean = {0, 0, 0}, g_drunk.leanVel = {0, 0, 0};
        }
    }
    // his face: eyelids drooping, the jaw slack (and the odd hiccup)
    g_drunk.lids = Clamp01((L - 0.15f) / 1.0f) * 0.55f;
    g_drunkLids = g_drunk.fallen ? 0.0f : g_drunk.lids;
    g_drunk.jaw = Clamp01((L - 0.4f) / 0.8f) * 0.10f;
    if (L > 0.3f && !g_drunk.fallen) {
        g_drunk.hiccupT -= dt;
        if (g_drunk.hiccupT <= 0.0f) {
            static uint32_t hs = 777u;
            g_drunk.hiccupT = 6.0f + 14.0f * DrRand(hs) / Lc;
            g_drunk.hicT = 0.0f;
            Sound_Play(SND_HICCUP, 0.8f, 0.95f + 0.15f * DrRand(hs));
        }
    }
    if (g_drunk.hicT >= 0.0f) g_drunk.hicT = g_drunk.hicT + dt > 0.5f ? -1.0f : g_drunk.hicT + dt;
    // the drunk body: his feet and his fall from this frame's balance (drunkbody.inc)
    DrunkBody_Update(dt, onFoot, L);
    // the head's lag behind the body (a spring: it lolls)
    if (g_drunkLog > 0 && (int)(g_drunk.clock * 4.0f) != (int)((g_drunk.clock - dt) * 4.0f)) {
        float iD, iW, oD, oW;
        Game_DrunkWalkState(&iD, &iW, &oD, &oW);
        Log("drunk: L %.2f lean %.3f (%.2f %.2f) vel %.2f step %.2f | walk in %.2f/%.2f out %.2f/%.2f | feet v %.2f a %.1f%s", L,
            Len(g_drunk.lean), g_drunk.lean.x, g_drunk.lean.z, Len(g_drunk.leanVel), g_drunk.stepW, iD, iW, oD, oW, Len(g_drunk.rootVel),
            Len(acc), g_drunk.fallen ? " FALLEN" : "");
        g_drunkLog--;
    }
}

// ------------------------------------------------------------------ the pose layer: his body leans as the pendulum does
// (applied to the game's skeleton before any weapon's pose is built on it - so a gun or a can goes with him; the joints moved are
// marked for the skin, the game's own pose kept for matching its upload)
static void DrRotateSubtree(Mat4* W, int n, int root, Vec3 pivot, const Mat4& R)
{
    Mat4 M = Mul(Mul(Translation(pivot * -1.0f), R), Translation(pivot));
    for (int j = root; j < n; j++) {
        bool in = j == root;
        for (int a = j; a > 0 && !in; a = g_parent[a]) in = g_parent[a] == root;
        if (in) W[j] = Mul(W[j], M), g_drunkPosed[j] = true;
    }
}
static void Drunk_PoseLayer(PoserPose& P)
{
    g_drunkSwayed = false;
    if (P.numJoints >= 33) {   // (v1.0: the game's own pose knocked over - a car's knock, a punch: its head low over its root)
        Vec3 hr = Row(ToMat(P.world[17]), 3) - Row(ToMat(P.world[0]), 3);
        g_drunkGameKnock = Len(hr) > 0.1f && Norm(hr).y < 0.8f;
    }
    if (P.numJoints >= 33 && DrunkBody_Pose(P)) return;   // (all of him from the drunk body)
    if (P.numJoints >= 33 && DrunkBody_Handles()) return;  // (Homer: the drunk body's - on a frame it doesn't draw him, its first,
                                                           // the game's own pose stands; the sway below turned whole subtrees about
                                                           // pivots off their joints - a stretched spine for that frame)
    if (!Drunk_Active() || Ragdoll_Active() || P.numJoints < 33 || !g_drunkOnFoot) return;
    int n = P.numJoints < 40 ? P.numJoints : 40;
    if (!LoadInvBind(P.numJoints)) return;
    g_drunkOrig = P;
    static Mat4 W[40];
    for (int j = 0; j < n; j++) W[j] = ToMat(P.world[j]), g_drunkPosed[j] = false;
    float L = fminf(g_drunk.level, 1.3f) * g_drunkLayerW;   // (all of the sway eased off him while a move of the game's own runs)
    Vec3 lean = g_drunk.lean * (kDrLeanShow * g_drunkLayerW);
    Vec3 fB = HeadingDir(atan2f(-P.world[0][8], -P.world[0][10])), rB = Norm(Cross(UP, fB));
    {   // (v1.0) walking, his weight rolls over onto each foot as it takes him - Radical's walk's own sway made a drunk's waddle: the
        // lower foot (his ankles as the game has them) is the one he's on
        static float wad = 0.0f, lastC = 0.0f;
        float dtc = Clampf(g_drunk.clock - lastC, 0.0f, 0.1f);
        lastC = g_drunk.clock;
        float s = Clampf((Row(W[11], 3).y - Row(W[7], 3).y) / 0.05f, -1.0f, 1.0f);   // (+: his left foot the lower)
        wad += (s - wad) * (1.0f - expf(-dtc * 9.0f));
        float mv = Smooth01((Len(g_drunk.rootVel) - 0.3f) / 0.6f);
        lean = lean - rB * (wad * kDrWaddle * L * mv);
    }
    float lm = Len(lean);
    // the lean: shared out from his hips up (pelvis-side 40 %, Spine_1 30 %, Spine_2 30 %), so he bends as he tips - his feet stay
    if (lm > 1e-4f) {
        Vec3 ax = Norm(Cross(UP, lean));
        DrRotateSubtree(W, n, 13, Row(W[4], 3), AxisAngle(ax, lm * 0.40f));
        DrRotateSubtree(W, n, 14, Row(W[14], 3), AxisAngle(ax, lm * 0.30f));
        DrRotateSubtree(W, n, 15, Row(W[15], 3), AxisAngle(ax, lm * 0.30f));
    }
    // his head: lolling - a slow roll and nod that grow with the drink, a spring behind the body's lean, and a hiccup's jolt
    float c = g_drunk.clock;
    float roll = L * (0.10f * sinf(c * 0.9f + 0.4f) + 0.05f * sinf(c * 2.3f)) + 0.6f * Dot(lean, rB);
    float nod = L * (0.06f * sinf(c * 0.7f + 2.0f) + 0.05f) - 0.4f * Dot(lean, fB);
    if (g_drunk.hicT >= 0.0f) nod -= 0.12f * sinf(Clamp01(g_drunk.hicT / 0.25f) * PI);
    Mat4 Rh = Mul(AxisAngle(rB, nod), AxisAngle(fB, roll));
    DrRotateSubtree(W, n, 16, Row(W[16], 3), Rh);
    // his arms out for balance as he leans (both shoulders out and a little forward; a gun's or an item's pose takes its arm back) -
    // loose, the elbows giving (v1.0: on Radical's walk, up to 85 deg out and straight was a T-pose; now 40 at most)
    float out = (Clamp01((Len(g_drunk.lean) - 0.04f) / 0.25f) * 0.45f + 0.06f * L + 0.25f * g_drunk.stepW) * g_drunkLayerW;
    if (g_drunk.teeterT >= 0.0f) {   // teetering: both arms going round like windmills (opposite ways), out wide
        float ph = g_drunk.teeterT * 15.0f;
        float o = 1.2f;
        DrRotateSubtree(W, n, 19, Row(W[19], 3), Mul(AxisAngle(fB, -o), AxisAngle(rB, -ph)));
        DrRotateSubtree(W, n, 26, Row(W[26], 3), Mul(AxisAngle(fB, o), AxisAngle(rB, -ph - PI)));
        out = 0.0f;
    }
    if (out > 0.01f) {
        float flap = sinf(c * 8.0f) * 0.12f * g_drunk.stepW;
        DrRotateSubtree(W, n, 19, Row(W[19], 3), Mul(AxisAngle(fB, -(out + flap)), AxisAngle(rB, -0.35f * out)));
        DrRotateSubtree(W, n, 26, Row(W[26], 3), Mul(AxisAngle(fB, out - flap), AxisAngle(rB, -0.35f * out)));
        DrRotateSubtree(W, n, 20, Row(W[20], 3), AxisAngle(rB, -0.7f * out));   // (the elbows)
        DrRotateSubtree(W, n, 27, Row(W[27], 3), AxisAngle(rB, -0.7f * out));
    }
    // the jaw slack
    if (g_drunk.jaw > 0.002f && n > 18) {
        Mat4 L18 = Mul(W[18], FullInverse(W[17]));
        Mat4 Lj = WithTrans(Mul(AxisAngle(V3(0, 0, 1), g_drunk.jaw), Rot3(L18)), Row(L18, 3));
        W[18] = Mul(Lj, W[17]), g_drunkPosed[18] = true;
    }
    // back into the pose (world, and the locals of the joints whose parents moved differently)
    for (int j = 0; j < n; j++) {
        memcpy(P.world[j], W[j].m, 64);
        if (j > 0) {
            Mat4 Lj = Mul(W[j], FullInverse(W[g_parent[j]]));
            memcpy(P.object[j], Lj.m, 64);
        }
    }
    g_drunkSwayed = true;
    DrunkBody_SeePose(P);   // (v1.0: Homer's pose as drawn - a fall starts from it)
}

// ------------------------------------------------------------------ drunk driving
// His car: the stick's steering late and springy (input.cpp: you correct, then over-correct), and the car itself wandering - a slow
// turn added and taken away, and at higher levels a fishtail - through the game's own physics (tires.cpp), so it works with the
// keyboard too, and with nothing to put back afterwards: when he sobers up or gets out it simply stops.
static float kDrCarYaw = 0.9f;      // the wander's turn (rad/s^2 at level 1, at speed)
static float kDrCarTail = 1.6f;     // the fishtail's push (m/s^2 at level 1.3)
static struct { uint32_t veh; Vec3 lastPos; bool have; float speed, clock, yaw, tail; int log; } g_dcar;
static void Drunk_Car(float dt, const GameState& gs, bool paused)
{
    float L = g_drunk.level;
    if (!gs.inCar || L < 0.05f || paused || !gs.player) {
        if (g_dcar.log > 0 && gs.inCar) {
            static float t = 0;
            t += dt;
            if (t > 1.0f) t = 0, Log("drunk car: off (L %.2f paused %d player %08X)", L, (int)paused, gs.player);
        }
        Input_DrunkSteer(0, 0);
        g_dcar.have = false, g_dcar.veh = 0;
        return;
    }
    // his car: the one he sits in (within 3 m of him)
    uint32_t ptrs[30];
    float pos[30][3], me[3];
    int n = Game_ListVehiclePtrs(ptrs, pos, 30);
    uint32_t veh = 0;
    Vec3 vp = {0, 0, 0};
    if (Game_PlayerPosition(gs.player, me)) {
        float bd = 9.0f;
        for (int i = 0; i < n; i++) {
            float dx = pos[i][0] - me[0], dy = pos[i][1] - me[1], dz = pos[i][2] - me[2], d = dx * dx + dy * dy + dz * dz;
            if (d < bd) bd = d, veh = ptrs[i], vp = V3(pos[i][0], pos[i][1], pos[i][2]);
        }
    }
    if (!veh) {
        if (g_dcar.log > 0) {
            static float t = 0;
            t += dt;
            if (t > 1.0f) t = 0, Log("drunk car: no car within 3 m of him (%d cars; him at %.1f %.1f %.1f)", n, me[0], me[1], me[2]);
        }
        Input_DrunkSteer(0, 0);
        return;
    }
    if (veh != g_dcar.veh) g_dcar.veh = veh, g_dcar.have = false;
    if (dt > 0.0f && g_dcar.have) {
        float sp = Len(V3(vp.x - g_dcar.lastPos.x, 0, vp.z - g_dcar.lastPos.z)) / dt;
        if (sp < 80.0f) g_dcar.speed += (sp - g_dcar.speed) * Clamp01(dt * 4.0f);
    }
    g_dcar.lastPos = vp, g_dcar.have = true;
    Input_DrunkSteer(L, g_dcar.speed);
    g_dcar.clock += dt;
    float c = g_dcar.clock, Lc = fminf(L, 1.3f);
    float atSpeed = Clamp01((g_dcar.speed - 2.0f) / 10.0f);
    float wander = 0.6f * sinf(c * 0.83f + 0.4f) + 0.4f * sinf(c * 1.91f + 2.2f) + 0.25f * sinf(c * 3.7f);
    g_dcar.yaw = kDrCarYaw * Lc * wander * atSpeed;
    g_dcar.tail = Lc > 0.6f && g_dcar.speed > 8.0f ? kDrCarTail * (Lc - 0.6f) / 0.7f * sinf(c * 4.4f) * Clamp01((g_dcar.speed - 8.0f) / 6.0f) : 0.0f;
    Tires_DrunkNudge(veh, g_dcar.yaw, g_dcar.tail, dt);
    if (g_dcar.log > 0 && (int)(c * 2.0f) != (int)((c - dt) * 2.0f)) {
        g_dcar.log--;
        Log("drunk car: L %.2f speed %.1f yaw %+.2f tail %+.2f", L, g_dcar.speed, g_dcar.yaw, g_dcar.tail);
    }
}

// ------------------------------------------------------------------ the camera: leans with him, drifts, wobbles
// (2026-10-17, review: "it could use a lil more screen effects like wobbliness": the drift twice as wide, a quicker wobble on top of it,
// the lens breathing deeper and the camera itself swaying a few centimetres)
static void Drunk_Camera(Mat4& world, float* fov)
{
    float L = fminf(g_drunk.level, 1.3f) * kDrCam;
    if (L <= 0.0f) return;
    float c = g_drunk.clock;
    Vec3 fw = Norm(Row(world, 2));
    // roll with his lean (sideways to the camera) and a slow drift with a wobble on it; pitch a little; a slow "breathing" of the lens
    Vec3 rt = Norm(Cross(UP, fw));
    float roll = -Dot(g_drunk.lean, rt) * 0.45f + L * (0.050f * sinf(c * 0.5f + 1.0f) + 0.018f * sinf(c * 1.7f + 0.3f));
    float pitch = L * (0.020f * sinf(c * 0.37f + 0.5f) + 0.008f * sinf(c * 1.3f + 2.1f));
    float yaw = L * (0.014f * sinf(c * 0.29f) + 0.006f * sinf(c * 1.1f + 0.7f));   // (small: the game walks him by the camera's heading)
    Mat4 R = Mul(Mul(AxisAngle(fw, roll), AxisAngle(rt, pitch)), AxisAngle(UP, yaw));
    Vec3 sway = rt * (L * 0.05f * sinf(c * 0.43f + 2.0f)) + UP * (L * 0.02f * sinf(c * 0.61f));
    world = WithTrans(Mul(Rot3(world), R), Row(world, 3) + sway);
    if (fov) *fov *= 1.0f + L * (0.035f * sinf(c * 1.25f) + 0.012f * sinf(c * 2.9f + 1.0f));
}

// ------------------------------------------------------------------ the screen: double vision
// a second picture swimming beside the first - soft, see-through, drifting apart and back together and now and then gone - from about
// half drunk (wheel.cpp draws it, Renderer::DrawGhost: the frame copied, halved and laid back over itself shifted); weaker at the wheel
// of his car. -> false when there's none this frame
static float kDrGhost = 1.0f;   // (dtune "ghost": its strength, 0 off)
bool Gun_DrunkGhost(float W, float* alpha, float* dx, float* dy)
{
    float I = Clamp01((g_drunk.level - 0.35f) / 0.65f) * kDrGhost * (g_drunkOnFoot ? 1.0f : 0.6f);
    if (I <= 0.002f) return false;
    float c = g_drunk.clock;
    float come = 0.5f + 0.5f * sinf(c * 0.31f + 1.3f) * (0.75f + 0.25f * sinf(c * 0.83f));
    *alpha = I * 0.22f * Smooth01((come - 0.35f) / 0.5f);   // (a third of the time gone; at 0.30 most of the time it was a double exposure)
    float sep = W * 0.016f * I * (0.45f + 0.55f * come), th = 0.45f * sinf(c * 0.27f + 0.8f);
    *dx = sep * cosf(th), *dy = sep * sinf(th) * 0.6f;
    return *alpha > 0.004f;
}

// ------------------------------------------------------------------ debug
static bool DrunkDebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "drunk")) {   // "drunk <level>" hold him at that level ("drunk -1": let it wear off; "drunk add,0.25": a drink)
        if (!strncmp(arg, "add,", 4)) Drunk_AddDrink((float)atof(arg + 4));
        else if (!strncmp(arg, "seed,", 5)) {   // "drunk seed,<n>": the natural trips from stream n, from now
            DrTripSeed(g_drTrip, (uint32_t)strtoul(arg + 5, nullptr, 10)), g_drTripInit = true;
            Log("drunk: trips seeded %u - the first at hazard %.4f", g_drTrip.seed, g_drTrip.next);
            return true;
        } else if (!strcmp(arg, "trips")) {   // "drunk trips": how many, and the hazard walked up
            if (!g_drTripInit) DrTripSeed(g_drTrip, 12345u), g_drTripInit = true;
            Log("drunk: trips %d of seed %u - hazard walked up %.4f, the next at %.4f more (%.4f now)", g_drTrip.n, g_drTrip.seed,
                (float)g_drTrip.total, g_drTrip.next, g_drTrip.haz);
            return true;
        } else {
            g_drunkSet = (float)atof(arg);
            if (g_drunkSet >= 0.0f) g_drunk.level = g_drunkSet, g_drunk.pending = 0.0f;
        }
        Log("drunk: level %.2f (held %.2f, pending %.2f)", g_drunk.level, g_drunkSet, g_drunk.pending);
        return true;
    }
    if (!strcmp(cmd, "drunklog")) {   // "drunklog <n>": the balance 4 times a second for n reports
        g_drunkLog = arg[0] ? atoi(arg) : 20;
        Log("drunk: level %.2f pending %.2f for %.0f s, cans %d, stumbles %.0f", g_drunk.level, g_drunk.pending, g_drunk.timeSober,
            g_drunk.cans, g_drunk.stumbles);
        return true;
    }
    if (!strcmp(cmd, "drcarlog")) {
        g_dcar.log = arg[0] ? atoi(arg) : 20;
        return true;
    }
    if (!strcmp(cmd, "drpush")) {   // "drpush x,z": shove his balance (rad/s)
        float x = 0, z = 0;
        sscanf(arg, "%f,%f", &x, &z);
        g_drunk.leanVel = g_drunk.leanVel + V3(x, 0, z);
        return true;
    }
    if (!strcmp(cmd, "dtune")) {   // "dtune name=value,..."
        static const struct { const char* name; float* v; } kv[] = {
            {"h", &kDrH}, {"stepat", &kDrStepAt}, {"fallat", &kDrFallAt}, {"noise", &kDrNoise}, {"inertia", &kDrInertia},
            {"trip", &kDrTripRate}, {"lurch", &kDrLurchRate}, {"show", &kDrLeanShow}, {"cam", &kDrCam}, {"wearoff", &kDrunkWearOff}, {"caryaw", &kDrCarYaw},
            {"cartail", &kDrCarTail}, {"ghost", &kDrGhost}, {"waddle", &kDrWaddle}};
        char buf[400];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        for (char* p = strtok(buf, ","); p; p = strtok(nullptr, ",")) {
            char* eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            for (auto& e : kv)
                if (!strcmp(e.name, p)) *e.v = (float)atof(eq + 1);
        }
        return true;
    }
    return false;
}
