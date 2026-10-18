// The drunk body's walk (Item 2, refined): his feet placed by his balance, not by the walk cycle. Platform-neutral, as dbody.h.
//
// The game still moves his body (its walking, filtered drunk: game.cpp's DrunkWalkFilter) and the balance pendulum (drunk.inc's
// model, here as Balance) says where his weight is going. His feet are planted until they step; a step goes where his weight is
// heading (its capture point) - so when he lurches, the foot that catches him lands under the lurch: wide when he sways out, across
// the other foot when he lurches over it, short and quick when it's urgent, long and late when he's slow to react. A foot lifts heel
// first and lands heel first; while it's down it doesn't move. His legs are IK'd onto his feet (knees and ankles as Radical's rig
// has them); his hips ride as high as his legs allow (the bob comes from that); his upper body is Radical's own walk (hom_loco_walk)
// played in time with his steps - or his idle when he stands - with the lean, the lolling head and the arms out for balance on top.
#pragma once
#include "dbody_motion.h"

namespace db {

// (release hardening) how far ahead his hips look for a foot about to take his weight - the spring heads down that early; and how
// long the walk takes to have him from the get-up's last pose
static float kWalkAhead = 0.12f;
static float kWalkUpBlend = 0.45f;
// (a step's toe-roll: how much further the heel goes up as it starts at most, and how much longer the roll takes the more it has
// to rise - share of the swing)
static float kWalkRollUp = 0.25f;
static float kWalkRollT = 0.18f;

// ------------------------------------------------------------------ the balance (the pendulum of drunk.inc)
// His body an inverted pendulum on his feet: gravity tips it, a balance reflex (late, weak and shaky with drink) pushes back, his
// feet's starts and stops rock it, a slow random wobble; a trip kicks it. Its numbers from research/beer/balance_sim.py.
struct Balance {
    Vec3 lean = {0, 0, 0}, vel = {0, 0, 0};   // rad: which way and how far his body leans off his feet (x, z), its rate
    struct H { float t; Vec3 lean, vel; } hist[96];
    int hn = 0;
    float clock = 0;
    Vec3 noise = {0, 0, 0};
    float tripT = 0;
    Vec3 tripDir = {0, 0, 1};
    float h = 0.78f;                     // his middle above his feet (m, x k)
    float w0() const { return sqrtf(9.81f / h); }
    Vec3 Capture() const { return lean + vel * (1.0f / w0()); }   // where the lean is heading (rad)
    void Kick(Vec3 dir, float amount) { tripDir = dir, tripT = amount; }
    void Update(float dt, Vec3 rootAcc, float level, Rng& rng)
    {
        if (dt <= 0) return;
        clock += dt;
        float L = Clamp(level, 0.0f, 1.6f), Lc = fminf(L, 1.3f);
        float w = w0();
        float kp = w * w * (3.0f - 0.85f * fminf(Lc, 1.0f)), kd = 2.0f * sqrtf(kp - w * w) * (1.0f - 0.25f * fminf(Lc, 1.0f));
        float delay = 0.05f + 0.08f * Lc;
        float sig = 0.60f * Lc * Lc, lam = 1.2f;
        auto gauss = [&]() { return (rng.Next() + rng.Next() + rng.Next() - 1.5f) * 2.0f; };
        noise = noise - noise * (lam * dt) + V3(gauss(), 0, gauss()) * (sig * sqrtf(2.0f * lam * dt));
        int sub = (int)ceilf(dt * 120.0f);
        sub = sub < 1 ? 1 : sub > 8 ? 8 : sub;
        float hdt = dt / sub;
        for (int s = 0; s < sub; s++) {
            hist[hn % 96] = {clock, lean, vel};
            hn++;
            Vec3 dl = lean, dv = vel;
            for (int i = 1; i < 96 && i <= hn; i++) {
                const H& e = hist[(hn - i) % 96];
                dl = e.lean, dv = e.vel;
                if (clock - e.t >= delay) break;
            }
            Vec3 a = lean * (w * w) - dl * kp - dv * kd + noise;
            a = a - rootAcc * (0.40f * Lc / h);
            if (tripT > 0.0f) a = a + tripDir * (9.0f * Lc);
            a.y = 0;
            vel = vel + a * hdt;
            lean = lean + vel * hdt;
        }
        tripT -= dt;
    }
};

// ------------------------------------------------------------------ the walker
struct WFoot {
    bool swing = false;
    Vec3 at = {0, 0, 0};       // where the ankle stands: x, z (y: the ground there)
    Vec3 fwd = {0, 0, -1};     // where its toes point (flat)
    Vec3 a, b, fa, fb, bulge;  // a swing: from, to, toes from / to, the arc's sideways bow (round the other foot)
    float t = 0, dur = 0.37f, lift = 0.13f;
    float down = 0;            // seconds planted
    float heel = 0;            // the heel's rise as the body passes over (rad, about the ball)
    float heel0 = 0;           // ... as the swing began (the toe-roll carries on from it)
    float width = 0, lead = 0; // the step's own width and lead (kept as it re-aims: a swinging foot lands where his body will be)
    bool cross = false, walk = false;
    Vec3 ankB = {0, 0, 0};     // where the ankle was as the toe-roll ended (the flight starts there)
};
struct WalkIn {
    Vec3 root;                 // where the game has his body: on the ground (Motion_Root)
    Vec3 fwd;                  // which way he faces (flat, unit)
    Vec3 vel;                  // how fast the game moves him (flat)
    Vec3 lean, leanVel;        // the balance (rad)
    float level;
    float dt;
};
struct Walker {
    bool init = false;
    WFoot f[2];
    int last = 1;              // the foot that stepped last
    float phase = 0;           // the walk clip's phase (0..1: a stride - each foot's swing half of it)
    float pelY = 0, pelYv = 0; // his hips' height above his feet's ground (a spring), its rate
    float moveW = 0;           // 0 standing .. 1 walking (the upper body: idle clip vs walk clip)
    float idleF = 0;           // the idle clip's frame
    float still = 0;           // seconds standing still
    Vec3 bodyP = {0, 0, 0}, bodyV = {0, 0, 0};   // his body as drawn: after the game's, smoothly (it speeds up and stops as a body
                                                 // does - behind his feet - but no lag at a steady walk)
    Vec3 bodyF = {0, 0, -1};
    float hipTurn = 0;         // his hips' turn toward his feet (smoothed: a foot landing changes where they point at once)   // ... and its facing: after the game's, never faster than a body turns (the game spins him round at once)
    int steps = 0, crosses = 0;
    Rng rng;
    float dbg[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // (the last frame's hips: target, limit now, ahead, wanted, drawn, ground, each foot's now - bodysim)
    // measured from the rig and Radical's clips (WalkerSetup)
    Q flatQ[2], toeQ[2];       // each foot flat on the ground, toes along model -Z (the idle's); the toes
    float ankleH = 0.09f;      // a flat foot's ankle above the ground
    float halfW = 0.135f;      // half his stance (ankles), walking
    float rootH = 0.63f;       // Character_Root above the ground, standing (the idle)
    float walkRootH = 0.59f;   // ... walking (the walk clip's middle)
    float stepLen = 0.43f;     // Radical's step at his walk (0.87 m a stride)
    float walkSpeed = 0.87f;   // ... and its speed
    Vec3 hipOff[2];            // each hip from Character_Root (its frame), standing
    float ballFwd = 0.15f;     // how far ahead of the ankle the ball is, a flat foot (measured)
};

// his right and his left, from his forward (model space faces -Z, his left at +X): a turn about his right by + tips the forward down
static inline Vec3 Perp(Vec3 fwd) { return Norm(Cross(V3(0, 1, 0), fwd)); }
static inline Vec3 LeftOf(Vec3 fwd) { return Norm(Cross(fwd, V3(0, 1, 0))); }
static inline Q HeadingQ(Vec3 fwd) { return QBetween(V3(0, 0, -1), Norm(V3(fwd.x, 0, fwd.z) + V3(0, 0, 1e-6f))); }

static inline void WalkerSetup(Walker* w, const Rig& r, const ClipSet& cs)
{
    Pose p;
    Mat4 W[J_MAX];
    ClipPose(r, *cs.idle, 0, &p);
    FK(r, p, W);
    float g = fminf(Pos(W, J_BALL_L).y, Pos(W, J_BALL_R).y) - 0.017f * r.k;   // (the idle's ball joints 1.7 cm up)
    for (int s = 0; s < 2; s++) {
        int an = s ? J_ANKLE_R : J_ANKLE_L, ba = s ? J_BALL_R : J_BALL_L;
        // flat, toes along -Z: the idle's foot turned about up so its ankle->ball points along -Z
        Vec3 d = Pos(W, ba) - Pos(W, an);
        Q turn = QBetween(Norm(V3(d.x, 0, d.z) + V3(0, 0, 1e-6f)), V3(0, 0, -1));
        w->flatQ[s] = turn * WorldQ(r, p, an);
        w->toeQ[s] = turn * WorldQ(r, p, ba);
        Mat4 R = Local(p.rq, p.rp);
        w->hipOff[s] = XformPoint(Pos(W, s ? J_HIP_R : J_HIP_L), FullInverse(R));
    }
    w->ankleH = 0.5f * (Pos(W, J_ANKLE_L).y + Pos(W, J_ANKLE_R).y) - g;
    {
        Vec3 bo = QRot(w->flatQ[0], r.t[J_BALL_L]);
        w->ballFwd = Len(V3(bo.x, 0, bo.z));
    }
    w->rootH = p.rp.y - g;
    w->halfW = 0.135f * r.k;
    w->stepLen = 0.43f * r.k;
    w->walkSpeed = 0.87f * r.k;
    // the walk's hips: its Character_Root's middle height
    float lo = 1e9f, hi = -1e9f;
    for (float fr = 0; fr < cs.walk->frames - 1; fr += 1.0f) {
        Pose q;
        ClipPose(r, *cs.walk, fr, &q);
        lo = fminf(lo, q.rp.y), hi = fmaxf(hi, q.rp.y);
    }
    w->walkRootH = 0.5f * (lo + hi);
}

// the feet set down where he stands (the pose he's in: after a get-up, or as the walker takes over)
static inline void WalkerPlant(Walker* w, const Rig& r, const Pose& now, Vec3 fwd, GroundFn g, void* gc)
{
    Mat4 W[J_MAX];
    FK(r, now, W);
    for (int s = 0; s < 2; s++) {
        WFoot& f = w->f[s];
        Vec3 an = Pos(W, s ? J_ANKLE_R : J_ANKLE_L);
        f = WFoot();
        f.at = V3(an.x, GroundAt(g, gc, an), an.z);
        Vec3 d = Pos(W, s ? J_BALL_R : J_BALL_L) - an;
        f.fwd = Len(V3(d.x, 0, d.z)) > 0.01f ? FlatN(d) : fwd;
    }
    w->init = true;
    w->still = 0;
    w->bodyP = V3(now.rp.x, 0, now.rp.z), w->bodyV = V3(0, 0, 0);
    w->bodyF = fwd;
    // (his hips' spring from where his hips are: kept from before a fall, it had him 12 cm lower in the walk's first frame after
    // the get-up)
    w->pelY = now.rp.y - GroundAt(g, gc, now.rp), w->pelYv = 0;
}

// ------------------------------------------------------------------ deciding a step
// the spot he needs a foot on: where his weight is heading (the capture point, from where the game has him), and where his body
// will be by the time the foot lands
static inline Vec3 WalkerNeed(const Walker& w, const Balance& b, const WalkIn& in, float ahead)
{
    // where his body will be when the foot lands, plus where his weight is heading from there - and, walking, a stride's lead
    // ahead of that (Radical's walk lands a foot ~0.24 m ahead of the body)
    Vec3 cp = b.Capture() * b.h;
    float spd = Len(Flat(in.vel));
    Vec3 lead = spd > 0.05f ? Flat(in.vel) * (1.0f / spd) * (0.24f * Clamp(spd / w.walkSpeed, 0.0f, 1.3f)) : V3(0, 0, 0);
    return V3(in.root.x + cp.x + in.vel.x * ahead + lead.x, 0, in.root.z + cp.z + in.vel.z * ahead + lead.z);
}
// how far a point is from the line between his feet (the support), past its ends
static inline float OffSupport(Vec3 p, Vec3 a, Vec3 b)
{
    Vec3 ab = Flat(b - a), ap = Flat(p - a);
    float l2 = Dot(ab, ab);
    float u = l2 > 1e-6f ? Clamp(Dot(ap, ab) / l2, 0.0f, 1.0f) : 0.0f;
    return Len(ap - ab * u);
}

static inline void WalkerStep(Walker* w, const Rig& r, const Balance& b, const WalkIn& in, GroundFn g, void* gc)
{
    float L = Clamp(in.level, 0.0f, 1.6f);
    float spd = Len(Flat(in.vel));
    Vec3 fwd = Norm(Flat(in.fwd) + V3(0, 0, 1e-6f)), left = LeftOf(fwd);
    Vec3 mfwd = spd > 0.1f ? Flat(in.vel) * (1.0f / spd) : fwd;   // (the way he's going)
    // the gait at this speed (Radical's walk at his walk speed: steps 0.435 m, swings 0.37 s, both feet down 0.13 s a step):
    // faster, longer steps and quicker swings
    float vr = Clamp(spd / w->walkSpeed, 0.15f, 2.0f);
    float S = Clamp(w->stepLen * powf(vr, 0.6f), 0.15f * r.k, 0.75f * r.k);
    float Ts = Clamp(0.37f * powf(1.0f / vr, 0.3f), 0.26f, 0.45f);
    float ds = Clamp(S / fmaxf(spd, 0.05f) - Ts, 0.04f, 0.5f);
    const WFoot& A = w->f[0];
    const WFoot& B = w->f[1];
    // a lurch: his weight heading away from where his body is going (the balance's capture point, off his body's own motion) and
    // off the line between his feet - the catch step can't wait for the gait
    Vec3 cpw = b.Capture() * b.h;
    Vec3 need0 = V3(in.root.x + cpw.x + in.vel.x * Ts * 0.5f, 0, in.root.z + cpw.z + in.vel.z * Ts * 0.5f);
    float off = OffSupport(need0, A.at, B.at);
    float margin = (0.07f - 0.03f * fminf(L, 1.0f)) * r.k;
    bool lurch = Len(Flat(cpw)) > (0.10f - 0.04f * fminf(L, 1.0f)) * r.k && (off > margin || spd < 0.12f);
    float behind[2], wantS[2];
    for (int s = 0; s < 2; s++) {
        Vec3 side = left * (s ? -1.0f : 1.0f);
        Vec3 home = V3(in.root.x, 0, in.root.z) + side * w->halfW;
        behind[s] = Dot(Flat(in.root - w->f[s].at), mfwd);
        wantS[s] = Len(Flat(w->f[s].at - home));
    }
    bool walking = spd > 0.12f;
    int s = -1;
    if (walking) {
        // the rear foot goes once the front one has borne him its share (both feet down a moment)
        int rear = behind[0] > behind[1] ? 0 : 1;
        // (or as soon as the rear foot is about out of reach - his first steps as the game sets off at once)
        bool reach = behind[rear] > 0.36f * r.k;
        if (w->f[1 - rear].down >= ds * (1.0f + 0.4f * L * (w->rng.Next() - 0.5f)) || behind[rear] > S * 1.2f || reach) s = rear;
    } else if (w->still > 0.4f && fmaxf(wantS[0], wantS[1]) > 0.16f * r.k)
        s = wantS[0] > wantS[1] ? 0 : 1;   // (standing: settling his stance)
    // his body turned well past a foot: that foot steps round (turning on the spot, or a sharp turn walking)
    if (s < 0)
        for (int k = 0; k < 2; k++) {
            float d = fabsf(remainderf(atan2f(w->f[k].fwd.x, w->f[k].fwd.z) - atan2f(fwd.x, fwd.z), 2.0f * kPi));
            if (d > 0.75f && w->f[k].down > 0.1f) { s = k; break; }
        }
    if (lurch) {
        // the foot nearer the way he's going - unless it's the one that just landed
        Vec3 dir = Flat(need0 - (A.at + B.at) * 0.5f);
        float lat = Dot(dir, left);
        int pick = fabsf(lat) > 0.08f * r.k ? (lat > 0 ? 0 : 1) : (behind[0] > behind[1] ? 0 : 1);
        if (w->f[pick].down < 0.08f) pick = 1 - pick;
        s = pick;
    }
    if (s < 0) return;
    WFoot& F = w->f[s];
    const WFoot& O = w->f[1 - s];
    // drunk: each step its own length, timing and width (and quicker when it's to catch him)
    float dur = Ts * (1.0f + 0.4f * L * (w->rng.Next() - 0.5f)) * (lurch ? 0.85f : 1.0f);
    dur = Clamp(dur, 0.2f, 0.55f);
    float Sl = S * (1.0f + 0.35f * L * (w->rng.Next() - 0.5f));
    // where: half a step ahead of where his body will be (and where his weight is heading), on his side - wider the drunker
    Vec3 cp = b.Capture() * b.h;
    Vec3 bodyAt = V3(in.root.x + in.vel.x * dur + cp.x, 0, in.root.z + in.vel.z * dur + cp.z);
    Vec3 side = left * (s ? -1.0f : 1.0f);
    float width = w->halfW * (1.0f + 0.5f * L * w->rng.Next() + 0.7f * Clamp(Len(Flat(b.lean)) * 3.0f, 0.0f, 1.0f));
    // (Radical's walk lands a foot ahead of the body by 46 % of the ground it covers while that foot is down - 0.25 of 0.54 m at his
    // walk: it lifts as far behind as it landed ahead, a little more)
    float lead = 0.46f * (Sl + spd * ds);
    Vec3 to = bodyAt + (walking ? mfwd * lead : V3(0, 0, 0)) + side * width;
    // lurching across: his weight heading past the other foot - this foot goes across, under it (a cross step)
    bool cross = false;
    float across = Dot(Flat(bodyAt - O.at), side * -1.0f);
    if (lurch && across > 0.03f * r.k && L > 0.2f) to = bodyAt + side * (-0.05f * r.k), cross = true;
    // never on top of the other foot; never further than a long stride
    Vec3 d = Flat(to - O.at);
    float dl = Len(d), minD = 0.17f * r.k;
    if (dl < minD) to = O.at + (dl > 1e-4f ? d * (1.0f / dl) : side) * minD;
    Vec3 step = Flat(to - F.at);
    float maxStep = (2.4f * S + 0.15f * L) * 1.0f + 0.2f * r.k;
    if (Len(step) > maxStep) to = F.at + step * (maxStep / Len(step));
    to.y = GroundAt(g, gc, to);
    if (Len(Flat(to - F.at)) < 0.05f * r.k) return;   // (not worth a step)
    F.swing = true;
    F.a = F.at, F.b = to;
    F.width = width, F.lead = walking ? lead : 0.0f, F.cross = cross, F.walk = walking;
    F.fa = F.fwd;
    float toeOut = (s ? -1.0f : 1.0f) * (0.12f + 0.25f * L * (w->rng.Next() - 0.3f));   // (toes out a little; drunk, all over)
    Vec3 ff = walking ? Norm(mfwd + fwd) : fwd;
    F.fb = Norm(ff * cosf(toeOut) + left * sinf(toeOut));
    F.t = 0, F.dur = dur;
    F.lift = (0.13f * Clamp(vr, 0.5f, 1.3f) + 0.04f * L * w->rng.Next()) * r.k;
    // the arc bows round the other foot when it passes close to it (a cross step round in front, and a little higher)
    F.bulge = V3(0, 0, 0);
    Vec3 mid = (F.a + F.b) * 0.5f;
    float pass = Len(Flat(mid - O.at));
    if (pass < 0.16f * r.k) {
        Vec3 away = Flat(mid - O.at);
        away = Len(away) > 1e-3f ? Norm(away) : side;
        F.bulge = away * (0.16f * r.k - pass) + (cross ? fwd * (0.06f * r.k) : V3(0, 0, 0));
        if (cross) F.lift += 0.03f * r.k;
    }
    F.heel0 = F.heel, F.heel = 0;
    w->last = s, w->steps++;
    if (cross) w->crosses++;
}

// the lowest point of a foot's skin (its ankle's and toes' extreme points) with the ankle at `ank` turned `fq` (world), the toes `tq`
static inline float FootLow(const Rig& r, int side, Vec3 ank, Q fq, Q tq)
{
    int an = side ? J_ANKLE_R : J_ANKLE_L, ba = side ? J_BALL_R : J_BALL_L;
    Vec3 ball = ank + QRot(fq, r.t[ba]);
    float low = 1e9f;
    for (const DbPoint& pt : kDbPoints) {
        if (pt.j == an) low = fminf(low, (ank + QRot(fq, V3(pt.p[0], pt.p[1], pt.p[2]))).y);
        else if (pt.j == ba) low = fminf(low, (ball + QRot(tq, V3(pt.p[0], pt.p[1], pt.p[2]))).y);
    }
    return low;
}

// ------------------------------------------------------------------ each frame
// -> his pose; `cs` gives his upper body (the walk and the idle)
// the ground's tilt under a foot's spot - the turn from level to it - from the ground either side of the spot along both axes. A kerb's
// edge under it (the two sides of the spot disagreeing by over 3 cm) is taken as level that way; never past 12 deg. (Kept level on a
// slope, a planted foot's toe or heel went 3 cm into the grass of a 13 % lawn - bodysim, the slope test)
static float kWalkGroundTilt = 1.0f;   // (1 on; 0 the feet level, as before)
static inline Q GroundTiltQ(GroundFn g, void* gc, Vec3 at)
{
    if (kWalkGroundTilt <= 0.0f) return Qi();
    const float h = 0.12f;
    float y0 = GroundAt(g, gc, at);
    float xp = GroundAt(g, gc, at + V3(h, 0, 0)) - y0, xm = y0 - GroundAt(g, gc, at - V3(h, 0, 0));
    float zp = GroundAt(g, gc, at + V3(0, 0, h)) - y0, zm = y0 - GroundAt(g, gc, at - V3(0, 0, h));
    float gx = fabsf(xp - xm) > 0.03f ? 0.0f : (xp + xm) / (2.0f * h);
    float gz = fabsf(zp - zm) > 0.03f ? 0.0f : (zp + zm) / (2.0f * h);
    gx = Clamp(gx, -0.21f, 0.21f) * kWalkGroundTilt, gz = Clamp(gz, -0.21f, 0.21f) * kWalkGroundTilt;
    if (fabsf(gx) < 1e-4f && fabsf(gz) < 1e-4f) return Qi();
    return QBetween(V3(0, 1, 0), Norm(V3(-gx, 1.0f, -gz)));
}

static inline void WalkerUpdate(Walker* w, const Rig& r, const ClipSet& cs, const Balance& b, const WalkIn& in0, GroundFn g, void* gc, Pose* out)
{
    float dt0 = in0.dt;
    float dt = dt0 > 0 ? dt0 : 1.0f / 30.0f;
    // his body: after where the game has him, with a critically damped follow that feeds the game's speed forward (no lag at a
    // steady walk; a start or a stop takes a third of a second, as a body does) - never more than 0.3 m from it
    {
        const float wn = 9.0f;
        Vec3 rp = V3(in0.root.x, 0, in0.root.z), rv = Flat(in0.vel);
        if (!w->init || Len(rp - w->bodyP) > 1.0f) w->bodyP = rp, w->bodyV = rv;
        // (in steps of at most 1/120 s, as the balance is: one step of a frame's length is no spring at low frame rates)
        int ns = (int)ceilf(dt * 120.0f);
        float h = dt / (float)(ns > 0 ? ns : 1);
        for (int i = 0; i < ns; i++) {
            Vec3 acc = (rp - w->bodyP) * (wn * wn) + (rv - w->bodyV) * (2.0f * wn);
            w->bodyV = w->bodyV + acc * h;
            w->bodyP = w->bodyP + w->bodyV * h;
        }
        Vec3 off = w->bodyP - rp;
        if (Len(off) > 0.3f) w->bodyP = rp + off * (0.3f / Len(off));
    }
    {
        Vec3 gf = FlatN(in0.fwd);
        float a0 = atan2f(w->bodyF.x, w->bodyF.z), a1 = atan2f(gf.x, gf.z);
        float da = remainderf(a1 - a0, 2.0f * kPi), mx = 7.0f * dt;   // (400 deg/s at most)
        a0 += Clamp(da * Sat(dt * 14.0f) * 2.0f, -mx, mx);
        if (!w->init) a0 = a1;
        // ... and no further round than his feet: within 45 deg of where they point (he turns through his steps; the game can spin
        // its body faster than any feet - for a moment his drawn body is behind it)
        if (w->init) {
            Vec3 fm = Flat(w->f[0].fwd + w->f[1].fwd);
            for (int k = 0; k < 2; k++)
                if (w->f[k].swing) fm = fm + Flat(w->f[k].fb) * 0.5f;
            if (Len(fm) > 0.1f) {
                float af = atan2f(fm.x, fm.z), off = remainderf(a0 - af, 2.0f * kPi);
                if (fabsf(off) > 0.6f) a0 = af + (off > 0 ? 0.6f : -0.6f);
            }
        }
        w->bodyF = V3(sinf(a0), 0, cosf(a0));
    }
    WalkIn in = in0;
    in.root = V3(w->bodyP.x, in0.root.y, w->bodyP.z);
    in.vel = w->bodyV;
    in.fwd = w->bodyF;
    float L = Clamp(in.level, 0.0f, 1.6f);
    Vec3 fwd = Norm(Flat(in.fwd) + V3(0, 0, 1e-6f)), left = LeftOf(fwd);
    float spd = Len(Flat(in.vel));
    w->still = spd < 0.12f ? w->still + dt : 0.0f;
    // the swing in progress (and a planted foot his body has turned well past pivots on its ball - a turn on the spot)
    int swinging = -1;
    for (int s = 0; s < 2; s++) {
        WFoot& F = w->f[s];
        if (!F.swing) {
            F.down += dt;
            float a0 = atan2f(F.fwd.x, F.fwd.z), a1 = atan2f(fwd.x, fwd.z) + (s ? -0.12f : 0.12f);
            float da = remainderf(a1 - a0, 2.0f * kPi), lim = 0.45f;
            if (fabsf(da) > lim) {
                float turn = Clamp(da - (da > 0 ? lim : -lim), -9.0f * dt, 9.0f * dt);
                Vec3 ball = F.at + F.fwd * w->ballFwd;
                float an = a0 + turn;
                F.fwd = V3(sinf(an), 0, cosf(an));
                F.at = V3(ball.x, F.at.y, ball.z) - F.fwd * w->ballFwd;
            }
            continue;
        }
        swinging = s;
        F.t += dt;
        // re-aiming as it goes (a foot adjusts its landing to where his body will be - the game's walk speeds up and turns under
        // him), until late in the swing; a cross step keeps its spot
        if (!F.cross && F.t < F.dur * 0.75f) {
            float rem = F.dur - F.t;
            Vec3 cp = b.Capture() * b.h;
            Vec3 mv = Len(Flat(in.vel)) > 0.1f ? FlatN(in.vel) : fwd;
            Vec3 side = left * (s ? -1.0f : 1.0f);
            Vec3 to = V3(in.root.x + in.vel.x * rem + cp.x, 0, in.root.z + in.vel.z * rem + cp.z) + (F.walk ? mv * F.lead : V3(0, 0, 0)) + side * F.width;
            const WFoot& O = w->f[1 - s];
            Vec3 d = Flat(to - O.at);
            float dl = Len(d), minD = 0.17f * r.k;
            if (dl < minD) to = O.at + (dl > 1e-4f ? d * (1.0f / dl) : side) * minD;
            to.y = GroundAt(g, gc, to);
            F.b = Lerp(F.b, to, Sat(dt * 10.0f));
        }
        if (F.t >= F.dur) {
            F.swing = false, F.at = F.b, F.fwd = F.fb, F.down = 0;
            swinging = -1;
        }
    }
    if (swinging < 0) WalkerStep(w, r, b, in, g, gc);
    for (int s = 0; s < 2; s++)
        if (w->f[s].swing) swinging = s;
    // the walk clip's phase: each swing is half a stride - the left's from 0.67 to 1.13, the right's from 0.17 to 0.63 (Radical's
    // walk: the left foot lands at 0.13, the right at 0.67); between swings it waits
    if (swinging >= 0) {
        const WFoot& F = w->f[swinging];
        float u = Sat(F.t / F.dur);
        float p0 = swinging == 0 ? 0.67f : 0.17f;
        float want = p0 + 0.46f * u;
        float cur = w->phase;
        float dlt = remainderf(want - cur, 1.0f);
        w->phase = fmodf(cur + dlt * Sat(dt * 20.0f) + 1.0f, 1.0f);
    }
    w->moveW += ((spd > 0.15f || swinging >= 0 ? 1.0f : 0.0f) - w->moveW) * Sat(dt * 3.0f);
    w->idleF = fmodf(w->idleF + dt * cs.idle->rate, cs.idle->frames - 1);

    // his upper body: Radical's walk at the phase (or his idle), turned his way
    Pose pw, pi;
    Mat4 Mw, Mi;
    ClipPose(r, *cs.walk, w->phase * (cs.walk->frames - 1), &pw, &Mw);
    ClipPose(r, *cs.idle, w->idleF, &pi, &Mi);
    Pose p;
    Blend(r, pi, pw, Smooth(w->moveW), &p);
    // (the clips' own Character_Root turn - the walk's sway and twist - kept, relative to his facing)
    Q hq = HeadingQ(fwd);
    p.rq = hq * QSlerp(RowQ(Mi), RowQ(Mw), Smooth(w->moveW));
    // his hips: where the game has him (plus how far his body leans: his hips go with his weight a little), as high as his legs
    // allow over his feet
    Vec3 leanW = b.lean * b.h;
    Vec3 base = V3(in.root.x + leanW.x * 0.25f, 0, in.root.z + leanW.z * 0.25f);
    float baseY = GroundAt(g, gc, base);
    float targetH = Mix(w->rootH, w->walkRootH, Smooth(w->moveW)) - 0.03f * fminf(L, 1.2f) * r.k;   // (drunk: knees a little bent)
    targetH -= 0.06f * r.k * Smooth((Len(Flat(b.lean)) - 0.08f) / 0.22f);   // (losing his balance, his knees give: bracing)
    // as high as the legs allow: each foot that bears weight (a planted one; the swinging one as it lands) - his hip within
    // 98 % of his leg's length of its ankle
    float legL = (Len(r.t[J_KNEE_L]) + Len(r.t[J_ANKLE_L])) * 0.98f;
    Mat4 R = Local(p.rq, V3(base.x, baseY + targetH, base.z));
    // ... now (his hips never above it) and a moment ahead for a foot that's landing (what the spring heads for: going down only
    // when the foot bore him, a long drunk stride took his hips down 6-8 cm in one frame - all the clamp below lets them go - at
    // 30 fps as at 18; heading down as the foot comes in, they're there when it lands)
    // (a planted foot's heel rises as his body goes on past it - rolling onto the ball: its ankle that much higher for his hips as
    // for his leg. Reckoned flat here, a rear foot about to lift pulled his hips down 3-11 cm in the frame or two before it went)
    for (int s = 0; s < 2; s++) {
        WFoot& F = w->f[s];
        if (F.swing) continue;
        float past = Dot(Flat(in.root - F.at), fwd);
        float heel = Clamp((past - 0.10f * r.k) / (0.25f * r.k), 0.0f, 1.0f) * 0.55f;
        F.heel += (heel - F.heel) * Sat(dt * 12.0f);
    }
    float maxH = 1e9f, aheadH = 1e9f;
    w->dbg[6] = w->dbg[7] = 9.0f;
    for (int s = 0; s < 2; s++) {
        const WFoot& F = w->f[s];
        // (ahead: the lowest of the moment it lands, if that's sooner, and kWalkAhead on - looked at only kWalkAhead on, a foot
        // landing before then was judged with his body already further over it: higher than when it landed)
        float land = F.swing ? F.dur - F.t : 0.0f;
        for (int k = 0; k < (F.swing ? 3 : 1); k++) {
            float ta = k == 0 ? 0.0f : k == 1 ? kWalkAhead : land;
            if (k == 2 && (land <= 0.0f || land >= kWalkAhead)) continue;
            float u = F.swing ? (F.t + ta) / F.dur : 1.0f;
            Vec3 an = F.swing ? Lerp(F.a, F.b, Smooth(u)) : F.at;
            float ankY = an.y + w->ankleH;
            if (!F.swing) {   // (its ankle as the leg will have it: the heel up about the ball - on the ground's tilt)
                Q gq = GroundTiltQ(g, gc, F.at);
                Vec3 ball = F.at + QRot(gq, F.fwd * w->ballFwd), a0 = F.at + QRot(gq, V3(0, w->ankleH, 0));
                Vec3 ah = ball + QRot(QAxis(QRot(gq, Perp(F.fwd)), F.heel), a0 - ball);
                an = V3(ah.x, F.at.y, ah.z), ankY = ah.y;
            }
            // (a landing foot takes his weight over the last 40 % of its swing: over the last 25 % a long drunk stride dropped his
            // hips 9 cm in one frame at 18 fps - a jolt every few steps)
            float bear = F.swing ? Smooth((u - 0.60f) / 0.40f) : 1.0f;
            if (bear <= 0.01f) continue;
            Vec3 hip = XformPoint(w->hipOff[s], R) + Flat(in.vel) * ta;
            float hd = Len(Flat(hip - an));
            float up = hd < legL ? sqrtf(legL * legL - hd * hd) : 0.0f;
            float hipY = ankY + up;
            float rootY = hipY - (hip.y - (baseY + targetH)) - baseY;   // (the root's height that puts this hip there)
            float lim = Mix(targetH + 0.2f, rootY, bear);
            if (k == 0) maxH = fminf(maxH, lim), w->dbg[6 + s] = lim;
            aheadH = fminf(aheadH, lim);
        }
    }
    // (a foot that can't reach doesn't pull him down: at most 8 cm - it's the foot that has to step, and the heel rises)
    maxH = fmaxf(maxH, targetH - 0.08f * r.k);
    aheadH = fmaxf(aheadH, targetH - 0.08f * r.k);
    float wantH = fminf(targetH, aheadH);
    // a spring after it (the bob) - never above what the legs allow
    float wn = 16.0f;
    // (in steps of at most 1/120 s: stepped once a frame this spring is unstable under ~22 frames a second - at 18 his body bounced
    // up and down 15 cm every other frame, standing or walking drunk; the lab records at 18)
    {
        int ns = (int)ceilf(dt * 120.0f);
        float h = dt / (float)(ns > 0 ? ns : 1);
        for (int i = 0; i < ns; i++) {
            w->pelYv += (wn * wn * (wantH - w->pelY) - 2.0f * wn * w->pelYv) * h;
            w->pelY += w->pelYv * h;
        }
    }
    if (!w->init || fabsf(w->pelY - wantH) > 0.3f) w->pelY = wantH, w->pelYv = 0;
    if (w->pelY > maxH) {   // (down to what his legs allow - fast, but never in one jump: 1.5 m/s at most)
        w->pelY = fmaxf(maxH, w->pelY - 1.5f * dt);
        w->pelYv = fminf(w->pelYv, 0.0f);
    }
    p.rp = V3(base.x, baseY + w->pelY, base.z);
    w->dbg[0] = targetH, w->dbg[1] = maxH, w->dbg[2] = aheadH, w->dbg[3] = wantH, w->dbg[4] = w->pelY, w->dbg[5] = baseY;

    // turning: his hips follow his feet (half of where they point off his facing), his chest still his way (the spine turns back)
    {
        Vec3 fm = Flat(w->f[0].fwd + w->f[1].fwd);
        if (Len(fm) > 0.1f) {
            float d = remainderf(atan2f(fm.x, fm.z) - atan2f(fwd.x, fwd.z), 2.0f * kPi) * 0.5f;
            d = Clamp(d, -0.6f, 0.6f);
            w->hipTurn += (d - w->hipTurn) * Sat(dt * 8.0f);
            d = w->hipTurn;
            if (fabsf(d) > 0.01f) {
                p.rq = QYaw(d) * p.rq;
                SetWorldQ(r, &p, J_SPINE1, QYaw(-d * 0.5f) * WorldQ(r, p, J_SPINE1));
                SetWorldQ(r, &p, J_SPINE2, QYaw(-d * 0.5f) * WorldQ(r, p, J_SPINE2));
            }
        }
    }
    // the drunk on top: his body leaning (shared out from his hips up), his head lolling and lagging, his arms out for balance
    float lm = Len(leanW);
    if (lm > 1e-4f) {
        Vec3 ax = Norm(Cross(V3(0, 1, 0), Flat(b.lean)));
        float ang = Len(b.lean) * 1.25f;
        p.rq = QAxis(ax, ang * 0.35f) * p.rq;
        SetWorldQ(r, &p, J_SPINE1, QAxis(ax, ang * 0.30f) * WorldQ(r, p, J_SPINE1));
        SetWorldQ(r, &p, J_SPINE2, QAxis(ax, ang * 0.30f) * WorldQ(r, p, J_SPINE2));
    }
    float c = b.clock, Lc = fminf(L, 1.3f);
    float roll = Lc * (0.10f * sinf(c * 0.9f + 0.4f) + 0.05f * sinf(c * 2.3f)) + 0.6f * Dot(b.lean, left * -1.0f);
    float nod = Lc * (0.06f * sinf(c * 0.7f + 2.0f) + 0.05f) - 0.4f * Dot(b.lean, fwd);
    SetWorldQ(r, &p, J_NECK, QAxis(fwd, roll * 0.5f) * QAxis(left * -1.0f, nod * 0.5f) * WorldQ(r, p, J_NECK));
    SetWorldQ(r, &p, J_HEAD, QAxis(fwd, roll * 0.5f) * QAxis(left * -1.0f, nod * 0.5f) * WorldQ(r, p, J_HEAD));
    float outA = Clamp((Len(b.lean) - 0.04f) / 0.25f, 0.0f, 1.0f) * 0.9f + 0.10f * Lc;
    if (outA > 0.01f) {
        SetWorldQ(r, &p, J_SHO_L, QAxis(fwd, -outA) * WorldQ(r, p, J_SHO_L));
        SetWorldQ(r, &p, J_SHO_R, QAxis(fwd, outA) * WorldQ(r, p, J_SHO_R));
    }
    LimitAll(r, &p);

    // his legs onto his feet - from his rest legs (knees forward), not the walk clip's: a straight leg takes its twist from what it
    // starts as, and the clip's turns through the stride - the knee flipped across as it bent
    for (int j : {J_HIP_L, J_KNEE_L, J_ANKLE_L, J_HIP_R, J_KNEE_R, J_ANKLE_R}) p.q[j] = r.bq[j];
    Vec3 ank[2], pole[2];
    Q fq[2], tq[2];
    Mat4 W[J_MAX];
    FK(r, p, W);
    for (int s = 0; s < 2; s++) {
        WFoot& F = w->f[s];
        Q flat = HeadingQ(F.fwd) * w->flatQ[s];
        Vec3 side = Perp(F.fwd);   // (the foot's own sideways axis: its pitch turns about it)
        if (!F.swing) {
            // planted: flat, its ankle above its spot - its heel rising as his body goes on past it (rolling onto the ball)
            // (its heel as his hips reckoned it, above)
            // (on the ground's tilt under it: GroundTiltQ)
            Q gq = GroundTiltQ(g, gc, F.at);
            Vec3 ball = F.at + QRot(gq, F.fwd * w->ballFwd);   // (the ball on the ground: the heel rises about it)
            Q pitch = QAxis(QRot(gq, side), F.heel);
            Vec3 a0 = F.at + QRot(gq, V3(0, w->ankleH, 0));
            ank[s] = ball + QRot(pitch, a0 - ball);
            fq[s] = pitch * gq * flat;
            tq[s] = gq * HeadingQ(F.fwd) * w->toeQ[s];
        } else {
            // the swing: a toe-roll first (the ball still on the ground, the heel going on up from where the stance left it), then
            // the flight - up and through (round the other foot if it has to), toes down, then up to land heel first, flat
            float u = Sat(F.t / F.dur);
            Q gqA = GroundTiltQ(g, gc, F.a), gqB = GroundTiltQ(g, gc, F.b);   // (the ground's tilt where it left, where it lands)
            Vec3 ballA = F.a + QRot(gqA, F.fa * w->ballFwd);
            Q flatA = gqA * HeadingQ(F.fa) * w->flatQ[s];
            Vec3 a0 = F.a + QRot(gqA, V3(0, w->ankleH, 0));
            const float roll = fmaxf(F.heel0, fminf(0.5f, F.heel0 + kWalkRollUp));
            // (the roll takes longer the more the heel has to rise: from a flat foot - a first step from standing - its 0.5 rad in
            // 12 % of the swing threw the ankle up 7-12 cm in a frame)
            float uT = 0.12f + kWalkRollT * Clamp((roll - F.heel0) / 0.5f, 0.0f, 1.0f);
            // (his toes stay flat on the ground through the roll, as they were while the foot was down, and go with the foot early
            // in the flight - turned with the foot at once, their tips went into the ground and the whole foot was lifted out: the
            // ball up 5 cm in one frame as every step began)
            Q toesFlat = gqA * HeadingQ(F.fa) * w->toeQ[s];
            if (u < uT) {
                // (easing in, not in and out: it ends moving - up and on about the ball as fast as the flight sets off. Stopped at the
                // end of the roll, the foot then left at full speed in one frame)
                float q = u / uT;
                float pr = Mix(F.heel0, roll, q * q);
                Vec3 sideA = QRot(gqA, Perp(F.fa));
                Q pitch = QAxis(sideA, pr);
                ank[s] = ballA + QRot(pitch, a0 - ballA);
                fq[s] = pitch * flatA;
                tq[s] = toesFlat;
                F.ankB = ballA + QRot(QAxis(sideA, roll), a0 - ballA);
            } else {
                // (leaving at the body's own speed - from a standstill, the leg would stretch out behind and then whip through)
                float v = (u - uT) / (1.0f - uT);
                Vec3 end = F.b + QRot(gqB, V3(0, w->ankleH, 0));
                float D = fmaxf(0.05f, Len(Flat(end - F.ankB)));
                float m0 = Clamp(Len(Flat(in.vel)) * F.dur * (1.0f - uT) / D, 0.0f, 1.2f);
                float e = (v * v * v - 2 * v * v + v) * m0 + (-2 * v * v * v + 3 * v * v);
                Vec3 spot = Lerp(F.ankB, end, e) + F.bulge * sinf(kPi * v);
                spot.y += F.lift * sinf(kPi * v) * (1.0f - 0.3f * v);
                ank[s] = spot;
                // pitch: from the roll (toes down) to toes up by 85 %, then down flat as the heel lands
                float pt = v < 0.85f ? Mix(roll, -0.25f, Smooth(v / 0.85f)) : Mix(-0.25f, 0.0f, Smooth((v - 0.85f) / 0.15f));
                Vec3 tf = Norm(F.fa * (1.0f - e) + F.fb * e);
                Q gqv = QSlerp(gqA, gqB, Smooth(v));
                fq[s] = gqv * QAxis(Perp(tf), pt) * HeadingQ(tf) * w->flatQ[s];
                tq[s] = QSlerp(toesFlat, fq[s] * Conj(w->flatQ[s]) * w->toeQ[s], Smooth(v / 0.25f));
            }
            // (no part of the shoe under the ground: lifted if it would be - not while it rolls off, toes flat as they were when it
            // was down, and coming in over the start of the flight: lifted at once, the foot jumped 1 cm as every step began - a planted
            // shoe sits up to 0.8 cm into the ground)
            float low = FootLow(r, s, ank[s], fq[s], tq[s]);
            float gy = GroundAt(g, gc, ank[s]);
            float lw = u < uT ? 0.0f : Smooth((u - uT) / (1.0f - uT) / 0.3f);
            if (low < gy + 0.002f) ank[s].y += (gy + 0.002f - low) * lw;
        }
        Vec3 ff = F.swing ? Norm(F.fa + F.fb) : F.fwd;
        pole[s] = Norm(ff + fwd * 0.2f);   // (the knee the way its foot points)
    }
    {   // (never a foot above his hip: an ankle at most 70 % of his leg's length below its hip - whatever the ground under it says;
        // a foot set on a hedge's top had his legs straight up through his body)
        Mat4 Wh[J_MAX];
        FK(r, p, Wh);
        for (int s = 0; s < 2; s++) {
            float top = Pos(Wh, s ? J_HIP_R : J_HIP_L).y - 0.30f * legL;
            if (ank[s].y > top) ank[s].y = top;
        }
    }
    LegsIK(r, &p, ank, fq, pole, 1.0f);
    for (int s = 0; s < 2; s++)   // (his toes flat on the ground when the foot's down and as it rolls off; then with the foot)
        SetWorldQ(r, &p, s ? J_BALL_R : J_BALL_L, tq[s]);
    HingesFix(&p);
    static const int toes[] = {J_BALL_L, J_BALL_R};
    for (int j : toes) Unsink(r, &p, j, g, gc);
    *out = p;
}

// the walk taking him from another pose (the get-up's last - Radical's standing frame): `from` blended into the walk's pose by w
// (0 from .. 1 the walk) - all of him but his legs, which stay on the walk's feet (re-solved from wherever the blend has his hips),
// as the fade-in from the game's own walk does (drunkbody.inc). Straight into the walk, he went from upright to the drunk crouch
// in one frame: 12 cm down, a joint 18-28 cm, after every get-up
static inline void BlendIntoWalk(const Rig& r, const Pose& from, float w, Pose* p)
{
    if (w >= 0.999f) return;
    Mat4 Wp[J_MAX], Wo[J_MAX];
    FK(r, *p, Wp);
    Pose out;
    Blend(r, from, *p, Smooth(w), &out);
    for (int s = 0; s < 2; s++) {
        int hp = s ? J_HIP_R : J_HIP_L, kn = s ? J_KNEE_R : J_KNEE_L, an = s ? J_ANKLE_R : J_ANKLE_L, ba = s ? J_BALL_R : J_BALL_L;
        for (int j : {hp, kn, an}) out.q[j] = p->q[j];
        FK(r, out, Wo);
        Vec3 H = Pos(Wp, hp), K = Pos(Wp, kn), A = Pos(Wp, an);
        // (the knee the way the walk bends it - and, the straighter the leg, the way its foot points, as the walk's own legs have
        // it: the legs nearly straight after the get-up, the bend alone was noise - a knee flipped 10-14 cm in a frame)
        Vec3 line = Norm(A - H), bend = K - H - line * Dot(K - H, line);
        Vec3 ff = Flat(Pos(Wp, ba) - A);
        ff = Len(ff) > 1e-3f ? Norm(ff) : Flat(QRot(p->rq, V3(0, 0, -1)));
        Vec3 pole = Norm(bend * (1.0f / 0.03f) + ff);
        IKHinge(r, &out, Wo, hp, kn, an, A, pole, kKneeMax);
        SetWorldQ(r, &out, an, WorldQ(r, *p, an));
        out.q[ba] = p->q[ba];
    }
    *p = out;
}

}   // namespace db
