// The drunk body's motion (Item 2, refined): the controllers that make the poses - the fall and getting back up (here), his
// stepping (dbody_steps.h) - on top of dbody.h's joint-space skeleton. Platform-neutral, as dbody.h.
//
// The fall is a hybrid, after the design brief: physics says when he goes and which way, and how hard he lands; the body is moved through
// constrained poses - his joints only ever turning as Radical's do, his hands and feet on the ground by IK, never sliding; and the
// recovery is Radical's own get-up for Homer (hom_get_up: from face down - a push-up, hands and knees, one knee under him, standing),
// slowed and broken up to look drunk - a push-up that gives way, a rest on all fours, a wobble on one knee.
//
//   TEETER   his feet stuck, his arms going round - the forward half of hom_balence (Radical's teeter) over his lean
//   TOPPLE   over he goes, turning about his toes (an inverted pendulum under gravity, from the lean and speed he had); his hips and
//            knees give, his head comes up, his arms reach - late, he's drunk - for the ground ahead
//   CATCH    his hands land where they will lie (hom_get_up's first frame, placed under him at the start: they never slide); his
//            elbows give as his weight comes onto them, his knees and belly go down - into that first frame exactly
//   DOWN     a beat face down
//   GETUP    hom_get_up from the same place, its timing stretched and broken up (see GetupPlan); IK keeps his hands and knees on
//            uneven ground
//   RECOVER  standing: handed back to the walk (a stagger to start with: drunkbody.inc)
#pragma once
#include "dbody.h"

namespace db {


struct ClipSet {
    const Clip* getUp = nullptr;     // homer: hom_get_up
    const Clip* balance = nullptr;   // homer: hom_balence
    const Clip* idle = nullptr;      // homer: hom_loco_idle_rest
    const Clip* walk = nullptr;      // homer: hom_loco_walk
};

// a small deterministic random source (the fall's variations; the harness replays a seed)
struct Rng {
    uint32_t s = 12345u;
    float Next() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFFFF) / (float)0x1000000; }
    float Range(float a, float b) { return a + (b - a) * Next(); }
};
// smooth noise (sums of sines with odd periods): -1..1
static inline float Wobble(float t, float seed)
{
    return 0.55f * sinf(t * 2.1f + seed) + 0.3f * sinf(t * 3.7f + seed * 2.3f) + 0.15f * sinf(t * 7.3f + seed * 0.7f);
}

struct Contact {
    bool on = false;
    Vec3 at = {0, 0, 0};
    float w = 0;
};

enum FallPhase { F_OFF = 0, F_TEETER, F_TOPPLE, F_CATCH, F_DOWN, F_GETUP, F_RECOVER };
static const char* const kFallPhase[] = {"off", "teeter", "topple", "catch", "down", "getup", "recover"};

// the get-up's timing: hom_get_up's frames in segments, each with its own speed, and holds between
struct GetupSeg { float f0, f1, dur; int kind; bool both; int ease = 0; };   // kind: 0 play, 1 hold (f0 == f1), 2 give way (back down fast);
                                                               // both: both hands kept pushing (a push-up that will give way)
enum { GS_PLAY = 0, GS_HOLD, GS_GIVE };
// how a move goes: 0 eased in and out; 1 a push (slow to get going, then it goes, then eases - effort, not a slide); 2 a settle
// (quick, then easing into place); 3 a collapse (gravity: accelerating)
enum { GE_SMOOTH = 0, GE_PUSH, GE_SETTLE, GE_DROP };
static inline float GetupEase(int ease, float u)
{
    u = Clamp(u, 0.0f, 1.0f);
    if (ease == GE_PUSH) return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f) * 0.85f + u * u * 0.15f;
    if (ease == GE_SETTLE) { float e = u * u * (3.0f - 2.0f * u); return 1.0f - (1.0f - e) * (1.0f - e); }   // (from rest: no snap)
    if (ease == GE_DROP) return u * u;
    return u * u * (3.0f - 2.0f * u);
}

struct Fall {
    int ph = F_OFF;
    float t = 0, pt = 0;           // time since the fall began; in this phase
    float level = 1;
    Vec3 dir = {0, 0, -1};         // the way he goes over
    Vec3 pivot;                    // his toes (on the ground), what he turns about
    Vec3 axis;                     // the turn's axis
    float r = 0.7f;                // pivot -> his middle
    float phi = 0, om = 0, phi0 = 0;
    float phiCatch = 1.2f;         // where his hands meet the ground
    float catchT = 0;              // time of the catch
    Pose S;                        // the pose he went over in
    Q rqSquare;                    // ... its hips squared to the way he goes and level (the teeter turns him to it)
    Pose prone;                    // hom_get_up's first frame, placed: where he ends
    Vec3 anchor;                   // hom_get_up's model space in the world
    Q tiltQ = Qi();                // on a slope: the ground under him as he lies - the turn from level to it, about tiltP (on the
    Vec3 tiltP = {0, 0, 0};        // ground under his middle), and its normal
    Vec3 tiltN = {0, 1, 0};
    Q heading;
    Vec3 handAt[2];                // where his hands land - under his shoulders as they come down to an arm's height - and push from
    Vec3 handB[2];                 // where the right hand steps to as he pushes up (Radical's own, on all fours); [0] unused
    Vec3 kneeG[2];                 // the get-up's knees, planted (knee joints) - where they lie face down
    Vec3 footG0[2], footG1[2];     // the get-up's feet (balls, on the ground): where each is planted, then where it steps to - Radical's
                                   // own drag them along the ground (the left 63 cm as it comes under him, the right as he rises)
    Q handQ[2];                    // each hand's turn on the ground (palm down, fingers ahead and out) - set as it lands, kept
    float lift0 = 0;               // hom_get_up's first frames sink into the ground: lifted by this at its first frame...
    float liftF[31];               // ... and by these, frame by frame (each frame what it needs, smoothed)
    Vec3 kShift[31];               // ... and moved back by these: Radical's own knees slide along the ground as he sits back - the
                                   // body moved the other way keeps them where they're planted (his hips move over them)
    int forceFail = -1;            // (the harness: -1 chance, 0 never, 1 always)
    GroundFn gfn = nullptr;        // the ground (for the plan)
    void* gctx = nullptr;
    Vec3 footAt[2];                // his feet (ball joints) as he went over
    // walking, his feet aren't under him as he trips (a stride apart, one in the air): as he teeters he gets them back under his
    // hips - a short, stumbling step each (a foot in the air just comes down) - from stepFrom (its ball) to footAt, at stepT0 for
    // stepDur (0: it stays where it is)
    Vec3 stepFrom[2];
    float stepT0[2] = {0, 0}, stepDur[2] = {0, 0}, stepLift[2] = {0, 0};
    Contact hand[2];
    float reachDelay = 0.25f;      // s before his arms go out (drunk: late)
    float handLag = 0.05f;         // one hand lands this much after the other
    int first = 0;                 // which (0 left, 1 right)
    // teeter
    float teeterDur = 0.45f;
    // the get-up plan
    GetupSeg seg[16];
    int nseg = 0, si = 0;
    float sf = 0;                  // the clip frame now
    Rng rng;
    float seed = 0;
    int thuds = 0;                 // (events for the mod's sounds: count of impacts so far)
    int groans = 0;
    float impact = 0;              // the last impact's strength (0..1)
    float shakeW = 0;              // (the arms' shake, smoothed)
    float catchV = 0;              // the catch's starting speed (share of the drop per second)
    // the collapse (TOPPLE): his knees go, then his top half over them - a timeline worked out as it begins (Collapse*)
    Vec3 kneeAt[2], ankleAt[2];    // where his knees and ankles end (the prone pose's, on the ground)
    Vec3 hipS[2], kneeS[2], ankleS[2], ballS[2];   // ... and where they were as it began
    Q toeQ[2];                     // his toes' turn as he stood (kept flat on the ground as his heels come up)
    Vec3 knee0[2], ankle0[2];      // his knees and ankles as he stood (the balls: footAt)
    Q footQ0[2];                   // his feet's turn as he stood
    Vec3 hipMidS;
    float lean0 = 0;               // how far over he was as it began
    static const int kTL = 256;
    float tlC[kTL], tlTh[kTL];     // the timeline (120 Hz): how far his knees have gone (0..1), his top half's pitch
    int ntl = 0;
    float tKnee = 0, tCatch = 0;   // when his knees land, when his hands do
    float aTot = 1.2f;             // how far his top half pitches, all told: from how he went over to how he'll lie (CollapsePlan)
    bool kneesDown = false;
    float reachAt[2];              // how far along the fall each arm sets off
    bool reaching[2];
    Vec3 r0[2], r1[2];             // each hand's curve: from, through
    Vec3 catchFrom[2];             // where each hand was as the catch began
    Pose prev;                     // the pose it gave last frame (each phase that blends on from where he was blends from this)
    Vec3 catchAnk[2];              // ... each ankle
    Q catchFootQ[2];               // ... each foot's turn
    Q catchToe[2];                 // ... the toes (local)
};

// ------------------------------------------------------------------ helpers
static inline Vec3 Flat(Vec3 v) { return V3(v.x, 0, v.z); }
static inline Vec3 FlatN(Vec3 v) { Vec3 f = Flat(v); float l = Len(f); return l > 1e-6f ? f * (1.0f / l) : V3(0, 0, -1); }
static inline float Ground(GroundFn g, void* ctx, Vec3 p) { return GroundAt(g, ctx, p); }

// a clip pose placed: its model space at `anchor` turned by `heading`
static inline void ClipPlaced(const Rig& r, const Clip& c, float f, Vec3 anchor, Q heading, Pose* out)
{
    ClipPose(r, c, f, out);
    PlaceRoot(out, anchor, heading);
}

// legs: each ankle to `ankle[i]` with the foot turned to `footQ[i]` (world), the knee toward `pole[i]`
static inline void LegsIK(const Rig& r, Pose* p, const Vec3* ankle, const Q* footQ, const Vec3* pole, float w)
{
    if (w <= 0.001f) return;
    Mat4 W[J_MAX];
    Pose in = *p;
    for (int s = 0; s < 2; s++) {
        int h = s ? J_HIP_R : J_HIP_L, k = s ? J_KNEE_R : J_KNEE_L, a = s ? J_ANKLE_R : J_ANKLE_L;
        FK(r, *p, W);
        IKHinge(r, p, W, h, k, a, ankle[s], pole[s], kKneeMax);
        if (footQ) SetWorldQ(r, p, a, footQ[s]);
    }
    if (w < 0.999f)
        for (int j : {J_HIP_L, J_KNEE_L, J_ANKLE_L, J_HIP_R, J_KNEE_R, J_ANKLE_R}) p->q[j] = QSlerp(in.q[j], p->q[j], w);
}
// an arm's wrist to `target` (world), the elbow out and back (Radical's: below the shoulder-wrist line, out ~20-50 deg)
static inline void ArmIK(const Rig& r, Pose* p, int side, Vec3 target, Vec3 pole, float w)
{
    if (w <= 0.001f) return;
    int s = side ? J_SHO_R : J_SHO_L, e = side ? J_ELB_R : J_ELB_L, wr = side ? J_WR_R : J_WR_L;
    Mat4 W[J_MAX];
    FK(r, *p, W);
    Q s0 = p->q[s], e0 = p->q[e];
    IKHinge(r, p, W, s, e, wr, target, pole, kElbowMax);
    if (w < 0.999f) p->q[s] = QSlerp(s0, p->q[s], w), p->q[e] = QSlerp(e0, p->q[e], w);
}
// the palm flat on the ground, the fingers along `fingers` (a world direction on the ground): in the bind T-pose his palms face
// down and his fingers run along the wrist's local X (+ left hand, - right). (Never from the forearm's own direction: on all fours
// the forearm is upright and its direction on the ground is noise - the wrist flipped.) Held inside the wrist's range.
static inline Q HandFlatQ(int side, Vec3 fingers)
{
    Vec3 eF = side ? V3(-1, 0, 0) : V3(1, 0, 0);                         // fingers, local
    Vec3 eN = side ? V3(0, 0, 1) : V3(0, 0, -1);                         // the palm's normal, local
    return QFrames(eF, eN, Norm(fingers), V3(0, -1, 0));
}
// a hand flat on the ground has its fingers flat too (the rig's own, straight - Radical's prone pose curls them, and curled fingers
// under a flat palm go through the ground)
static inline void FingersFlat(const Rig& r, Pose* p, int side, float w)
{
    if (w <= 0.001f) return;
    int j0 = side ? J_MIDB_R : J_MIDB_L;
    for (int j = j0; j < j0 + 4 && j < r.n; j++) p->q[j] = QSlerp(p->q[j], r.bq[j], w);
}
// how far his wrist is above the lowest skin of his hand when the hand is turned `hq` (world): so a planted hand's palm lies on
// the ground, neither in it nor floating (his fingers flat, as HandTurn lays them)
static inline float PalmDrop(const Rig& r, const Pose& p, int side, Q hq)
{
    Pose t = p;
    FingersFlat(r, &t, side, 1.0f);
    SetWorldQ(r, &t, side ? J_WR_R : J_WR_L, hq);
    Mat4 W[J_MAX];
    FK(r, t, W);
    int wr = side ? J_WR_R : J_WR_L;
    float low = 1e9f;
    for (const DbPoint& pt : kDbPoints) {
        if (!Under(r, pt.j, wr)) continue;
        low = fminf(low, PointW(pt, W).y);
    }
    return low < 1e8f ? Pos(W, wr).y - low : 0.10f * r.k;
}
// (a hand taking his weight bends back further than Homer's own clips ever bend it - 63-71 deg: Radical's landing, npd_fall_land on
// Homer's rig, bends the wrists 87-99 deg as the body goes on over the hands, the palms still flat. Planted, his wrists get that -
// in both tilts: with his elbows bent back his forearm is turned through ~90 deg (his rig has no forearm roll: the wrist's twist
// does it), and then the palm's bend back is the range's second tilt, not its first)
static const float kWristLoaded = 1.66f;   // (95 deg)
static inline void HandTurn(const Rig& r, Pose* p, int side, Q want, float w)
{
    if (w <= 0.001f) return;
    FingersFlat(r, p, side, w);
    int wr = side ? J_WR_R : J_WR_L;
    SetWorldQ(r, p, wr, QSlerp(WorldQ(r, *p, wr), want, w));
    for (int i = 0; i < r.nlim; i++)
        if (r.lim[i].j == wr) {
            Lim L = r.lim[i];
            L.t1b = Mix(L.t1b, fmaxf(L.t1b, kWristLoaded), w);   // (t1: the fingers tilting toward the back of the hand)
            L.t2a = Mix(L.t2a, fminf(L.t2a, -kWristLoaded), w), L.t2b = Mix(L.t2b, fmaxf(L.t2b, kWristLoaded), w);
            LimitJoint(r, p, L);
        }
}

// every pose out: inside the joint ranges (the procedural ones - Radical's own clip frames are left as they are), on top of the
// ground, the knees and elbows pure hinges
static inline void Finish(const Rig& r, Pose* p, GroundFn g, void* gc, bool limits)
{
    if (limits) LimitAll(r, p);
    GroundPass(r, p, g, gc);
    HingesFix(p);
    // his feet out of the ground: a foot whose skin is in it lifted by its leg (its turn kept) - the knee taking it
    for (int s = 0; s < 2; s++) {
        int hp = s ? J_HIP_R : J_HIP_L, kn = s ? J_KNEE_R : J_KNEE_L, an = s ? J_ANKLE_R : J_ANKLE_L, ba = s ? J_BALL_R : J_BALL_L;
        Mat4 W[J_MAX];
        FK(r, *p, W);
        float d = fmaxf(SkinDepth(r, W, an, g, gc), SkinDepth(r, W, ba, g, gc));
        if (d <= 0.004f) continue;
        Vec3 H = Pos(W, hp), K = Pos(W, kn), A = Pos(W, an);
        Vec3 line = Norm(A - H), bend = K - H - line * Dot(K - H, line);
        Q keep = WorldQ(r, *p, an);
        IKHinge(r, p, W, hp, kn, an, A + V3(0, d, 0), Len(bend) > 1e-4f ? Norm(bend) : V3(0, 0, 1), kKneeMax);
        SetWorldQ(r, p, an, keep);
    }
    // hands, fingers, feet and toes laid on the ground rather than through it
    static const int ends[] = {J_BALL_L, J_BALL_R, J_MIDB_L, J_MID_L, J_MIDB_R, J_MID_R};   // (not the ankles: turning a planted foot moves its toes)
    for (int j : ends) Unsink(r, p, j, g, gc);
}

// ------------------------------------------------------------------ the plan
// hom_get_up, frame by frame (research/drunk/clips): 0 face down, hands by his face - 4-6 the push-up onto hands and knees - 8
// sitting back on his heels - 10-12 his left foot comes under him - 12-15 one knee up, a hand on the thigh - 18-24 up, the right
// foot comes forward - 27-30 standing
static inline void PlanGetup(Fall* f)
{
    Rng& R = f->rng;
    float L = Clamp(f->level, 0.3f, 1.6f);
    // (how drunk he is shows in how he gets up: just drunk enough to fall, up quickly and rarely a failed push; legless, slow, and the
    // first push-up most likely gives way - 10 % at 0.9, 70 % from 1.5)
    int fails = R.Next() < 0.10f + 0.60f * Clamp((L - 0.9f) / 0.6f, 0.0f, 1.0f) ? 1 : 0;
    if (f->forceFail >= 0) fails = f->forceFail;
    int n = 0;
    auto add = [&](float a, float b, float d, int k, bool both, int e) { if (n < 16) f->seg[n++] = {a, b, d, k, both, e}; };
    // phrased like effort, not played like a clip: each move gathers, goes and settles; between them he stops - getting his breath,
    // swaying, gathering for the next. Drunker, slower and longer pauses
    float slow = 0.9f + 0.5f * Clamp((L - 0.8f) / 0.7f, 0.0f, 1.0f);   // (x the timings: 0.9 up to 0.8, 1.4 from 1.5)
    auto jit = [&](float a, float b) { return R.Range(a, b); };
    if (fails) {   // a push-up that gives way: up as far as his arms get him, shaking, and down again
        add(0, 0.5f, 0.30f * slow, GS_PLAY, true, GE_SMOOTH);                 // (palms set, gathering)
        add(0.5f, 3.0f, 0.55f * slow * jit(1.0f, 1.2f), GS_PLAY, true, GE_PUSH);
        add(3.0f, 3.0f, jit(0.35f, 0.55f), GS_HOLD, true, GE_SMOOTH);           // (arms shaking)
        add(3.0f, 0.0f, 0.26f, GS_GIVE, true, GE_DROP);                        // (gives way: flat again)
        add(0, 0, jit(0.35f, 0.55f), GS_HOLD, true, GE_SMOOTH);                // (a groan)
    }
    add(0, 0.5f, 0.30f * slow, GS_PLAY, false, GE_SMOOTH);                     // palms set, a breath
    add(0.5f, 4.2f, 0.55f * slow * jit(1.0f, 1.2f), GS_PLAY, false, GE_PUSH); // the push: elbows straighten, chest up
    add(4.2f, 4.6f, jit(0.18f, 0.30f) * slow, GS_PLAY, false, GE_SMOOTH);    // (it nearly stalls)
    add(4.6f, 6.0f, 0.28f * slow, GS_PLAY, false, GE_SETTLE);                 // onto hands and knees
    add(6.0f, 6.0f, jit(0.30f, 0.55f) * slow, GS_HOLD, false, GE_SMOOTH);     // on all fours, head hanging
    add(6.0f, 10.0f, 0.42f * slow * jit(1.0f, 1.15f), GS_PLAY, false, GE_SMOOTH);   // weight back onto his knees
    add(10.0f, 13.0f, 0.42f * slow * jit(1.0f, 1.15f), GS_PLAY, false, GE_SETTLE);  // the left foot comes under him
    add(13.0f, 13.0f, jit(0.30f, 0.50f) * slow, GS_HOLD, false, GE_SMOOTH);   // one knee up - a wobble
    add(13.0f, 24.0f, 0.55f * slow * jit(1.0f, 1.15f), GS_PLAY, false, GE_PUSH);    // up, pushing off the front foot
    add(24.0f, 30.0f, 0.38f * slow, GS_PLAY, false, GE_SETTLE);               // standing
    f->nseg = n, f->si = 0, f->sf = 0;
}

// the ankle between a knee and a ball, both given: the shin and the foot their own lengths - of the circle of such points, the one
// nearest `ref` (where the ankle would be if the foot just turned with the leg: his feet turn out, so "the highest point" isn't it)
static inline Vec3 AnkleBetween(const Rig& r, int side, Vec3 K, Vec3 B, Vec3 ref)
{
    float ls = Len(r.t[side ? J_ANKLE_R : J_ANKLE_L]), lf = Len(r.t[side ? J_BALL_R : J_BALL_L]);
    Vec3 kb = B - K;
    float d = Len(kb);
    if (d < 1e-4f) return K + V3(0, ls, 0);
    Vec3 e = kb * (1.0f / d);
    if (d >= ls + lf - 1e-4f) return K + e * ls;
    float a = (ls * ls - lf * lf + d * d) / (2 * d), h = sqrtf(fmaxf(0.0f, ls * ls - a * a));
    Vec3 C = K + e * a;
    Vec3 n = (ref - C) - e * Dot(ref - C, e);
    if (Len(n) < 1e-5f) n = V3(0, 1, 0) - e * e.y;
    return C + Norm(n) * h;
}
// the lowest of a foot's skin: the ankle at A turned fq, the toes at B turned tq (world)
static inline float FootSkinLow(int side, Vec3 A, Q fq, Vec3 B, Q tq)
{
    int an = side ? J_ANKLE_R : J_ANKLE_L, ba = side ? J_BALL_R : J_BALL_L;
    float low = 1e9f;
    for (const DbPoint& pt : kDbPoints) {
        if (pt.j == an) low = fminf(low, (A + QRot(fq, V3(pt.p[0], pt.p[1], pt.p[2]))).y);
        else if (pt.j == ba) low = fminf(low, (B + QRot(tq, V3(pt.p[0], pt.p[1], pt.p[2]))).y);
    }
    return low;
}
// his legs with his toes planted where he stood: each knee at K[s], the ankle between it and the ball (heel up), the foot turned
// from how it stood so the ball stays on its spot, the toes flat as he stood (bent at the ball no more than 60 deg)
static inline void LegsOnToes(const Fall* f, const Rig& r, Pose* p, const Vec3* K, const Vec3* Aref, const Vec3* balls = nullptr)
{
    Vec3 A[2], pole[2];
    Q fq[2];
    Mat4 W[J_MAX];
    FK(r, *p, W);
    for (int s = 0; s < 2; s++) {
        Vec3 B = balls ? balls[s] : f->footAt[s];
        A[s] = AnkleBetween(r, s, K[s], B, Aref[s]);
        fq[s] = QBetween(f->footAt[s] - f->ankle0[s], B - A[s]) * f->footQ0[s];
        // (up on his toes, a foot turns about where its sole meets the ground - under the ball - not about the ball joint: as the heel
        // comes up the ball joint rises with it. Turned about the joint, the sole in front of it went into the ground)
        if (f->gfn)
            for (int it = 0; it < 2; it++) {
                float d = Ground(f->gfn, f->gctx, B) - FootSkinLow(s, A[s], fq[s], B, f->toeQ[s]);
                if (d <= 0.004f) break;
                B.y += d;
                A[s] = AnkleBetween(r, s, K[s], B, Aref[s]);
                fq[s] = QBetween(f->footAt[s] - f->ankle0[s], B - A[s]) * f->footQ0[s];
            }
        Vec3 hip = Pos(W, s ? J_HIP_R : J_HIP_L);
        pole[s] = Norm(K[s] - Lerp(hip, A[s], 0.5f) + f->dir * 0.03f);   // (a straight leg's own bend direction is noise)
    }
    LegsIK(r, p, A, fq, pole, 1.0f);
    for (int s = 0; s < 2; s++) {
        int b = s ? J_BALL_R : J_BALL_L;
        Q loc = Conj(WorldQ(r, *p, r.parent[b])) * f->toeQ[s], rel = Conj(r.bq[b]) * loc;
        float ang = QAngle(rel);
        if (ang > 1.05f) loc = r.bq[b] * QSlerp(Qi(), rel, 1.05f / ang);
        p->q[b] = loc;
    }
}

// the collapse, as a pose: c how far his knees have gone (0 standing as the teeter left him .. 1 on their marks on the ground),
// a how far everything above his knees has pitched forward about them since (rad). His balls stay where they are; his knees go round
// them, down and forward onto their marks; his heels come up; all of him above the knees turns about them as one, as a body falling
// forward from its knees does (the bend at his hips the teeter gave him kept) - then his legs are IK'd onto that
static inline void CollapsePose(const Fall* f, const Rig& r, float c, float a, Pose* p)
{
    *p = f->S;
    float cc = Smooth(c);
    Vec3 K[2], Ar[2];
    for (int s = 0; s < 2; s++) {
        Vec3 B = f->footAt[s];
        Vec3 k0 = f->kneeS[s] - B, k1 = f->kneeAt[s] - B;
        Q kq = QSlerp(Qi(), QBetween(k0, k1), cc);
        K[s] = B + QRot(kq, Norm(k0)) * Mix(Len(k0), Len(k1), cc);
        Ar[s] = B + QRot(kq, f->ankleS[s] - B);   // (the ankle turning with the shin about the toes)
    }
    Vec3 Km0 = (f->kneeS[0] + f->kneeS[1]) * 0.5f, Km = (K[0] + K[1]) * 0.5f;
    Q pitch = QAxis(f->axis, a);
    p->rq = pitch * f->S.rq;
    p->rp = Km + QRot(pitch, f->S.rp - Km0);
    // as he goes: a little more bend at the waist, his head coming up to see it coming (local Z to his right: + forward; - lifts
    // the head - mostly at the head joint: the neck's turn stretches the skin over his collar)
    float give = Smooth(c * 1.3f);
    p->q[J_SPINE1] = f->S.q[J_SPINE1] * QAxis(V3(0, 0, 1), 0.10f * give);
    p->q[J_SPINE2] = f->S.q[J_SPINE2] * QAxis(V3(0, 0, 1), 0.06f * give);
    p->q[J_NECK] = f->S.q[J_NECK] * QAxis(V3(0, 0, 1), -0.08f * give);
    p->q[J_HEAD] = f->S.q[J_HEAD] * QAxis(V3(0, 0, 1), -0.24f * give);
    // as he goes over he takes the shape he'll lie in - stretched out, level, his head as Radical's prone pose holds it: that pose's
    // top half, pitched back up about its knees by what's still to come, more and more of it. So his hands land with him nearly in
    // it, and he comes down onto them rather than on past them (and his shoulders level: a body still turned as it walked has one
    // too high for its hand to reach the ground)
    // (its shape sooner than its place: Radical's teeter windmills - twisted, one shoulder high - and as his knees go and his arms
    // come forward to catch him he squares up; where he is follows the pitch all the way)
    float wm = Smooth(a / fmaxf(0.2f, f->aTot)), ws = Smooth(fmaxf(c, a / fmaxf(0.1f, 0.5f * f->aTot)));
    if (wm > 0.001f || ws > 0.001f) {
        Vec3 KmP = (f->kneeAt[0] + f->kneeAt[1]) * 0.5f;
        Q back = QAxis(f->axis, a - f->aTot);
        Vec3 rpUp = Km + QRot(back, f->prone.rp - KmP);
        Q rqUp = back * f->prone.rq;
        p->rp = Lerp(p->rp, rpUp, wm), p->rq = QSlerp(p->rq, rqUp, wm);
        for (int j : {J_PELVIS, J_SPINE1, J_SPINE2, J_NECK, J_HEAD}) p->q[j] = QSlerp(p->q[j], f->prone.q[j], ws);
    }
    LegsOnToes(f, r, p, K, Ar);
}
static inline void CollapseAt(const Fall* f, float t, float* c, float* th);
// where his hands land, as they meet the ground (W: the pose then): under each shoulder, ahead and out a little, and further ahead by
// half of how far that shoulder has still to go forward as he comes down onto his hands (to the prone pose's) - his forearms end
// leaning onto his hands, not past them (past, no wrist bends a palm flat: his fingers went into the ground) - but never beyond the
// reach of his arm as they land. Palms flat, the fingers ahead and turned in a little (Radical's prone hands are turned in)
static inline void CatchPlants(Fall* f, const Rig& r, const Mat4* W, const Pose& p)
{
    Mat4 Wp[J_MAX];
    FK(r, f->prone, Wp);
    Vec3 side = Norm(Pos(W, J_SHO_L) - Pos(W, J_SHO_R));
    for (int s = 0; s < 2; s++) {
        int sh = s ? J_SHO_R : J_SHO_L;
        Vec3 S = Pos(W, sh);
        Vec3 out_ = Flat(side * (s ? -1.0f : 1.0f));
        float travel = fmaxf(0.0f, Dot(Pos(Wp, sh) - S, f->dir));
        float ahead = 0.06f * r.k + 0.5f * travel, out = 0.07f * r.k;
        f->handQ[s] = HandFlatQ(s, f->dir - out_ * 0.27f);
        float y = Ground(f->gfn, f->gctx, S) + PalmDrop(r, p, s, f->handQ[s]) - 0.003f;
        float reach = 0.95f * r.armLen, dy = S.y - y;
        float flat = sqrtf(fmaxf(0.0f, reach * reach - dy * dy));
        if (ahead * ahead + out * out > flat * flat) ahead = sqrtf(fmaxf(0.0f, flat * flat - out * out));
        Vec3 at = V3(S.x, 0, S.z) + f->dir * ahead + out_ * out;
        at.y = Ground(f->gfn, f->gctx, at) + PalmDrop(r, p, s, f->handQ[s]) - 0.003f;
        f->handAt[s] = at;
    }
}
// its timeline, worked out as the collapse begins: his knees buckle (his legs barely holding: the hips drop at ~6 m/s^2, the knees
// land in about a third of a second); all of him above the knees falls forward about them like a body hinged there (gravity on its
// middle, half of it while the knees are still dropping); his hands land the moment both shoulders come within an arm (the elbow a
// little bent) of where they'll lie - or, if they never can, when he's as far over as he goes
static inline void CollapsePlan(Fall* f, const Rig& r)
{
    const float h = 1.0f / 120.0f;
    Mat4 W[J_MAX];
    FK(r, f->S, W);
    Vec3 Km0 = (f->kneeS[0] + f->kneeS[1]) * 0.5f;
    Vec3 com = Pos(W, J_PELVIS) * 0.4f + Pos(W, J_SPINE2) * 0.4f + Pos(W, J_HEAD) * 0.2f - Km0;   // (his top half's middle)
    float Lc = fmaxf(0.2f, Len(com)), g0 = atan2f(Dot(com, f->dir), fmaxf(0.01f, com.y));
    float kk = 0.25f * r.k;   // (his top half's radius of gyration)
    float drop = fmaxf(0.1f, f->hipMidS.y - (f->kneeAt[0].y + f->kneeAt[1].y) * 0.5f - Len(r.t[J_KNEE_L]));
    float ac = 6.0f / drop;
    float c = 0, cv = 0.5f, a = 0, av = Clamp(f->om * 0.6f, 0.0f, 0.9f);
    {   // (how far over his top half goes: from his knees to his shoulders, as he went over and as he'll lie)
        Mat4 Wp[J_MAX];
        FK(r, f->prone, Wp);
        Vec3 up = V3(0, 1, 0);
        Vec3 vS = (Pos(W, J_SHO_L) + Pos(W, J_SHO_R)) * 0.5f - Km0;
        Vec3 vP = (Pos(Wp, J_SHO_L) + Pos(Wp, J_SHO_R)) * 0.5f - (f->kneeAt[0] + f->kneeAt[1]) * 0.5f;
        float eS = atan2f(Dot(vS, up), Dot(vS, f->dir)), eP = atan2f(Dot(vP, up), Dot(vP, f->dir));
        f->aTot = Clamp(eS - eP, 0.3f, 1.55f - g0);
    }
    Pose p;
    f->ntl = 0, f->tKnee = 0, f->tCatch = 0;
    float best = 1e9f, tBest = 0;   // (the lowest his shoulders come)
    for (int i = 0; i < Fall::kTL; i++) {
        f->tlC[i] = c, f->tlTh[i] = a, f->ntl = i + 1;
        if (i % 2 == 0 && i > 15) {   // (his shoulders coming down to an arm's height above the ground: his hands meet it)
            CollapsePose(f, r, c, a, &p);
            FK(r, p, W);
            float hl = Pos(W, J_SHO_L).y - Ground(f->gfn, f->gctx, Pos(W, J_SHO_L)), hr = Pos(W, J_SHO_R).y - Ground(f->gfn, f->gctx, Pos(W, J_SHO_R));
            float d = fminf(hl, hr);
            if (d < best - 1e-4f) best = d, tBest = i * h;
            if (d < r.armLen * 0.86f + 0.10f * r.k) {
                f->tCatch = i * h;
                CatchPlants(f, r, W, p);
                break;
            }
        }
        if (c < 1.0f) {
            cv += ac * h;
            c += cv * h;
            if (c >= 1.0f) c = 1.0f, f->tKnee = i * h;
        }
        float gam = g0 + a;
        av += ((9.81f * Lc / (Lc * Lc + kk * kk)) * sinf(gam) * (c >= 1.0f ? 1.0f : 0.5f) - 0.6f * av) * h;
        a = fminf(a + av * h, f->aTot);
    }
    if (f->tCatch <= 0) {   // (never got that low: as low as he comes)
        f->tCatch = tBest > 0 ? tBest : (f->ntl - 1) * h;
        CollapseAt(f, f->tCatch, &c, &a);
        CollapsePose(f, r, c, a, &p);
        FK(r, p, W);
        CatchPlants(f, r, W, p);
    }
}
static inline void CollapseAt(const Fall* f, float t, float* c, float* th)
{
    float x = Clamp(t * 120.0f, 0.0f, (float)(f->ntl - 1));
    int i = (int)x;
    int j = i + 1 < f->ntl ? i + 1 : i;
    float u = x - i;
    *c = Mix(f->tlC[i], f->tlC[j], u), *th = Mix(f->tlTh[i], f->tlTh[j], u);
}

// hom_get_up at clip frame sf as he does it: placed, lifted clear of the ground (fading out as he rises: frames 10-18), his hands
// held on their plants while they push - the right through frame 9.5 (Radical's own right hand is down from 5 to 10), the left
// through 2.5 (Radical's lifts at 2-3 to go to his knee) unless `both` (a push-up that will give way: both stay) - then handed
// back to the clip's arms over a frame or two
// (the struggle: layers on the clip, put on before the hands are pinned - so pinned arms flex with a trembling body)
struct Struggle { float tremble = 0, breathe = 0, headHang = 0, wobble = 0; };
// On a slope. hom_get_up is played on level ground; laid level across a slope and lifted clear of its highest ground, his body floated
// over the rest (fallen across a 13 % lawn, a knee 10 cm off the grass; uphill, his knees and hands 15 cm - bodysim). Lying, on all
// fours and kneeling he lies along the ground under him - the clip turned onto its plane, about the ground under his middle - and he
// comes upright again as he stands (hom_get_up's frames 12-22); his hands, knees and feet are set on the ground where each is
static float kSlopeTilt = 1.0f;   // (1: on; 0: off - as before)
static inline float GetupTiltW(float sf) { return kSlopeTilt * (1.0f - Smooth((sf - 12.0f) / 10.0f)); }
static inline void GetupTilt(const Fall* f, float sf, Pose* p)
{
    float w = GetupTiltW(sf);
    if (w <= 0.001f || f->tiltN.y > 0.99995f) return;
    Q q = QSlerp(Qi(), f->tiltQ, w);
    p->rq = q * p->rq;
    p->rp = f->tiltP + QRot(q, p->rp - f->tiltP);
}
// (the plane's height at a place: what "level with the ground under his middle" is there)
static inline float TiltPlaneY(const Fall* f, Vec3 at)
{
    return f->tiltP.y - (f->tiltN.x * (at.x - f->tiltP.x) + f->tiltN.z * (at.z - f->tiltP.z)) / f->tiltN.y;
}
static inline float GetupLift(const Fall* f, float sf)
{
    float x = Clamp(sf, 0.0f, 30.0f);
    int i = (int)x;
    int j = i < 30 ? i + 1 : 30;
    return Mix(f->liftF[i], f->liftF[j], x - i);
}
static inline Vec3 GetupAnchor(const Fall* f, float sf)
{
    float x = Clamp(sf, 0.0f, 30.0f);
    int i = (int)x;
    int j = i < 30 ? i + 1 : 30;
    return f->anchor + V3(0, GetupLift(f, sf), 0) - Lerp(f->kShift[i], f->kShift[j], x - i);
}
// each hand while he gets up (by hom_get_up's frame): the left pushes from where it landed until frame 2.5, then goes where Radical
// sends it (to his knee) by 4.5; the right pushes from where it landed until 2.5, steps back under his shoulder (lifted a little,
// 2.5-4.5) to where Radical's right hand is on all fours, stays there until 9.5 and is handed back to the clip by 11. A push-up that
// will give way keeps both where they landed. -> its weight (0 = the clip's own hand)
static inline float HandPin(const Fall* f, const Rig& r, int side, float sf, bool both, Vec3* at)
{
    if (both) return *at = f->handAt[side], 1.0f;
    if (side == 0) return *at = f->handAt[0], 1.0f - Smooth((sf - 2.5f) / 2.0f);
    float u = Sat((sf - 2.5f) / 2.0f);
    *at = Lerp(f->handAt[1], f->handB[1], Smooth(u)) + V3(0, 0.07f * r.k * sinf(kPi * u), 0);
    return 1.0f - Smooth((sf - 9.5f) / 1.5f);
}
// his feet while he gets up (by hom_get_up's frame): the right planted where it lies until frame 18, then a step (lifted) to where
// he stands, 18-25; the left planted until frame 6, then a step - the knee coming up, the foot swung under him - to its spot by
// frame 12. Each foot keeps the clip's turn; its lowest skin set on the ground
static inline float FootStepAt(const Fall* f, int s, float sf, Vec3* B, float* lift)
{
    float t0 = s ? 18.0f : 6.0f, t1 = s ? 25.0f : 12.0f;
    float u = Sat((sf - t0) / (t1 - t0)), e = Smooth(u);
    *B = Lerp(f->footG0[s], f->footG1[s], e);
    *lift = (s ? 0.06f : 0.05f) * sinf(kPi * u);
    return u;
}
// how much each knee is planted (by hom_get_up's frame): the left until its foot steps under him (6), the right until he rises (14)
static inline float KneeDown(int s, float sf) { return 1.0f - Smooth((sf - (s ? 13.0f : 5.0f)) / 1.5f); }
static inline void GetupFeet(const Fall* f, const Rig& r, float sf, Pose* p)
{
    if (!f->gfn || sf > 28.5f) return;
    // kneeling, his knees are what he's on: each where it lay, its skin on the ground; his body moved so each hip stays a thigh's
    // length from its knee (Radical's knees slide back along the ground as he sits back; it's his hips that should move)
    Vec3 Kt[2];
    float kw[2];
    {
        Mat4 W[J_MAX];
        FK(r, *p, W);
        Vec3 shift = V3(0, 0, 0);
        float wsum = 0;
        for (int s = 0; s < 2; s++) {
            kw[s] = KneeDown(s, sf);
            int kn = s ? J_KNEE_R : J_KNEE_L;
            Vec3 Kc = Pos(W, kn);
            float low = 1e9f;
            for (const DbPoint& pt : kDbPoints)
                if (pt.j == kn) low = fminf(low, PointW(pt, W).y);
            Kt[s] = V3(f->kneeG[s].x, Ground(f->gfn, f->gctx, f->kneeG[s]) + (Kc.y - low) - 0.008f, f->kneeG[s].z);
            Kt[s] = Lerp(Kc, Kt[s], kw[s]);
            if (kw[s] <= 0.001f) continue;
            Vec3 H = Pos(W, s ? J_HIP_R : J_HIP_L), d = H - Kt[s];
            float e = Len(d) - Len(r.t[kn]);
            shift = shift - Norm(d) * (e * kw[s]);
            wsum += kw[s];
        }
        if (wsum > 0.001f) p->rp = p->rp + shift * (1.0f / fmaxf(1.0f, wsum));
    }
    float w = 1.0f - Smooth((sf - 27.0f) / 1.5f);   // (handed back to the clip's own as he stands - it stands where they are)
    for (int s = 0; s < 2; s++) {
        int hp = s ? J_HIP_R : J_HIP_L, kn = s ? J_KNEE_R : J_KNEE_L, an = s ? J_ANKLE_R : J_ANKLE_L, ba = s ? J_BALL_R : J_BALL_L;
        Mat4 W[J_MAX];
        FK(r, *p, W);
        Vec3 Ac = Pos(W, an), Bc = Pos(W, ba), Kc = Pos(W, kn), H = Pos(W, hp);
        Q fq = WorldQ(r, *p, an), tq = WorldQ(r, *p, ba);
        Vec3 B;
        float lift;
        float u = FootStepAt(f, s, sf, &B, &lift);
        float lowRel = FootSkinLow(s, Ac, fq, Bc, tq) - Bc.y;   // (the foot's lowest skin below its ball, as the clip holds it)
        B.y = Ground(f->gfn, f->gctx, B) - lowRel + lift;
        Vec3 A = Ac + (B - Bc);
        A = Lerp(Ac, A, w);
        if (kw[s] > 0.001f) A = Lerp(A, Kt[s] + (Ac - Kc), kw[s]);   // (the shin as the clip holds it, hung from the planted knee)
        // (a foot swung under a kneeling hip passes close under it: never closer than his folded leg reaches - closer, the leg
        // flipped out sideways)
        float dmin = 0.20f * r.k;
        if (Len(A - H) < dmin) A = H + Norm(A - H) * dmin;
        Vec3 line = Norm(A - H), bend = Kc - H - line * Dot(Kc - H, line);
        // (and as it steps the knee comes forward over it - Radical's left knee is still behind, dragged)
        Vec3 pole = Len(bend) > 1e-4f ? Norm(bend) : f->dir;
        if (kw[s] > 0.001f) {   // (bending toward its planted spot)
            Vec3 kb = Kt[s] - H - line * Dot(Kt[s] - H, line);
            if (Len(kb) > 1e-4f) pole = Norm(Lerp(pole, Norm(kb), kw[s]));
        }
        float sw = Smooth(u) * (u < 1.0f ? 1.0f : 0.0f);   // (the knee turning forward over the whole step, not in a jerk)
        if (u > 0.0f && u < 1.0f) pole = Norm(pole * (1.0f - sw) + Norm(f->dir + V3(0, 0.35f, 0)) * sw);
        else if (u >= 1.0f && s == 0) pole = Norm(f->dir + V3(0, 0.35f, 0));
        IKHinge(r, p, W, hp, kn, an, A, pole, kKneeMax);
        SetWorldQ(r, p, an, fq);
        SetWorldQ(r, p, ba, tq);
        if (u > 0.0f && u < 1.0f)   // (in the air the foot turns as an ankle can - the clip's, dragged, doesn't fit the swung leg)
            for (int i = 0; i < r.nlim; i++)
                if (r.lim[i].j == an) LimitJoint(r, p, r.lim[i]);
    }
}
static inline void GetupPose(const Fall* f, const Rig& r, const ClipSet& cs, float sf, bool both, Pose* p, const Struggle* st = nullptr)
{
    Vec3 an = GetupAnchor(f, sf);
    ClipPlaced(r, *cs.getUp, sf, an, f->heading, p);
    GetupTilt(f, sf, p);
    if (st) {
        p->q[J_SPINE2] = p->q[J_SPINE2] * QAxis(V3(0, 0, 1), st->breathe + st->tremble);
        p->q[J_NECK] = p->q[J_NECK] * QAxis(V3(0, 0, 1), st->headHang);     // (local Z to his right: + forward - the head hangs)
        p->q[J_SPINE1] = p->q[J_SPINE1] * QAxis(V3(0, 1, 0), st->wobble);   // (local Y: forward - a sway to the side)
    }
    Mat4 W[J_MAX];
    // (his arms are short, and the clip's body is lifted clear of the ground - for its knees: where a hand he's pushing on is out of
    // his arm's reach, all of him leans forward over his hands, turning about his knees - his weight going onto his arms as a
    // push-up's does - rather than the hand being dragged off its spot (bending his back down to it hunched him). His face kept
    // clear of the ground. Only while he pushes - the push-up and on all fours: after that Radical's own hands leave the ground)
    float assistW = 1.0f - Smooth((sf - 6.0f) / 1.5f);
    if (assistW > 0.001f) {
        FK(r, *p, W);
        float need = 0;
        for (int s = 0; s < 2; s++) {
            Vec3 pin;
            float w = HandPin(f, r, s, sf, both, &pin);
            if (w > 0.001f) need = fmaxf(need, (Len(Pos(W, s ? J_SHO_R : J_SHO_L) - pin) - 0.95f * r.armLen) * w);
        }
        if (need > 0.002f) {
            Vec3 K = (Pos(W, J_KNEE_L) + Pos(W, J_KNEE_R)) * 0.5f;
            Vec3 sm = (Pos(W, J_SHO_L) + Pos(W, J_SHO_R)) * 0.5f;
            float want = Clamp(need / fmaxf(0.25f, Len(sm - K)), 0.0f, 0.20f) * assistW;
            Pose base = *p;
            auto pitchBy = [&](float ang) {
                Q q = QAxis(f->axis, ang);
                p->rq = q * base.rq, p->rp = K + QRot(q, base.rp - K);
            };
            auto clear = [&]() {
                if (!f->gfn) return true;
                FK(r, *p, W);
                float deep = 0;
                for (int j : {J_SPINE1, J_SPINE2, J_NECK, J_HEAD, J_JAW}) deep = fmaxf(deep, SkinDepth(r, W, j, f->gfn, f->gctx));
                return deep <= 0.002f;
            };
            pitchBy(want);
            if (!clear()) {
                float lo = 0, hi = want;
                for (int k = 0; k < 6; k++) {
                    float mid = 0.5f * (lo + hi);
                    pitchBy(mid);
                    if (clear()) lo = mid;
                    else hi = mid;
                }
                pitchBy(lo);
            }
        }
    }
    // his feet planted where they stand, stepping when they move (GetupFeet)
    GetupFeet(f, r, sf, p);
    for (int s = 0; s < 2; s++) {
        Vec3 pin;
        float w = HandPin(f, r, s, sf, both, &pin);
        if (w <= 0.001f) continue;
        FK(r, *p, W);
        int sh = s ? J_SHO_R : J_SHO_L, el = s ? J_ELB_R : J_ELB_L;
        Vec3 S = Pos(W, sh), E = Pos(W, el), line = Norm(pin - S);
        Vec3 cur = E - S - line * Dot(E - S, line);
        Vec3 out_ = Norm(S - Pos(W, J_SPINE2));
        // (a push-up's elbows: back and out - the forearm then leans onto the hand and a flat palm is a wrist bend Radical's range
        // has; the clip's own elbow as the hand goes back to it)
        Vec3 push = Norm(Flat(out_) * 0.7f - f->dir * 0.7f);
        Vec3 pole = Norm(push * w + (Len(cur) > 1e-4f ? Norm(cur) : out_) * (1.0f - w));
        ArmIK(r, p, s, Lerp(Pos(W, s ? J_WR_R : J_WR_L), pin, w), pole, 1.0f);
        HandTurn(r, p, s, f->handQ[s], w);
    }
    // a hand of Radical's on its way (not pinned) kept out of the ground where it isn't flat: lifted by its arm
    for (int s = 0; s < 2; s++) {
        Vec3 pin;
        if (HandPin(f, r, s, sf, both, &pin) > 0.5f || !f->gfn) continue;
        int wr = s ? J_WR_R : J_WR_L;
        FK(r, *p, W);
        float low = 1e9f;
        for (const DbPoint& pt : kDbPoints)
            if (Under(r, pt.j, wr)) {
                Vec3 q = PointW(pt, W);
                low = fminf(low, q.y - Ground(f->gfn, f->gctx, q));
            }
        if (low < -0.005f) {
            Vec3 S = Pos(W, s ? J_SHO_R : J_SHO_L), E = Pos(W, s ? J_ELB_R : J_ELB_L), A = Pos(W, wr), line = Norm(A - S);
            Vec3 cur = E - S - line * Dot(E - S, line);
            Q keep = WorldQ(r, *p, wr);
            ArmIK(r, p, s, A + V3(0, -low, 0), Len(cur) > 1e-4f ? Norm(cur) : V3(0, -1, 0), 1.0f);
            SetWorldQ(r, p, wr, keep);
        }
    }
}
// where his right hand steps to as he pushes up: Radical's right hand on all fours (hom_get_up frame 6), on the ground
static inline void PlantStep(Fall* f, const Rig& r, const ClipSet& cs, GroundFn g, void* gc)
{
    Pose p6;
    Mat4 W6[J_MAX];
    ClipPose(r, *cs.getUp, 6.0f, &p6), FK(r, p6, W6);
    Vec3 m = Pos(W6, J_WR_R);
    Vec3 w = GetupAnchor(f, 6.0f) + QRot(f->heading, V3(m.x, 0, m.z));
    w.y = Ground(g, gc, w) + PalmDrop(r, p6, 1, f->handQ[1]) - 0.003f;
    f->handB[1] = w, f->handB[0] = w;
}

// ------------------------------------------------------------------ starting
// `now`: the pose he's in (drawn) as he goes; `dir` the way he goes over (world, from the balance); `speed` how fast he was going that
// way (m/s); the ground under him
// `feet`: where his feet are on the ground (each ankle's spot; a swinging foot's landing spot), or null - the pose's own
static inline void FallStart(Fall* f, const Rig& r, const ClipSet& cs, const Pose& now, Vec3 dir, float speed, float lean, float level,
                             GroundFn g, void* gc, uint32_t seed, const Vec3* feet = nullptr, const Vec3* feetFwd = nullptr)
{
    f->rng.s = seed * 2654435761u + 7u;
    f->seed = f->rng.Range(0.0f, 6.28f);
    f->level = level;
    f->S = now, f->prev = now;
    f->ph = F_TEETER, f->t = 0, f->pt = 0;
    f->thuds = f->groans = 0, f->impact = 0;
    Mat4 W[J_MAX];
    FK(r, now, W);
    // (walking: where his feet end up - under his hips, a little ahead of them, as wide apart as they were (within a stance's
    // width); a foot already near its spot and on the ground stays where it is, the others step there)
    Vec3 fin[2], ballNow[2] = {Pos(W, J_BALL_L), Pos(W, J_BALL_R)};
    bool step[2] = {false, false};
    if (feet) {
        Vec3 d0 = FlatN(dir), left = Norm(Cross(d0, V3(0, 1, 0)));
        Vec3 hc = Flat((Pos(W, J_HIP_L) + Pos(W, J_HIP_R)) * 0.5f) + d0 * (0.08f * r.k);
        for (int s = 0; s < 2; s++) {
            float w = Clamp(fabsf(Dot(Flat(feet[s]) - hc, left)), 0.09f * r.k, 0.16f * r.k);
            fin[s] = hc + left * (s == 0 ? w : -w);
            float gy = Ground(g, gc, ballNow[s]);
            bool down = ballNow[s].y < gy + 0.05f * r.k;
            if (down && Len(Flat(feet[s] - fin[s])) < 0.10f * r.k) fin[s] = feet[s];   // (planted near enough: it stays)
            else step[s] = true;
        }
        feet = fin;
    }
    // the way he goes: forward-ish (the fall is built for falling forward: a lean behind him or to a side turns him on his feet
    // first - his feet are where the turn happens - so he still goes over facing the way he falls)
    f->dir = FlatN(dir);
    {   // (his hips as he goes: facing the way he goes and level - a walk's twist, the hips following the feet and a lean to the side
        // taken out as he teeters, so he goes over square, both shoulders coming down together; the way they're tipped forward kept)
        Vec3 fw = QRot(now.rq, V3(0, 0, -1));
        float pitch = asinf(Clamp(fw.y, -1.0f, 1.0f));
        Vec3 f1 = f->dir * cosf(pitch) + V3(0, sinf(pitch), 0), u1 = V3(0, cosf(pitch), 0) - f->dir * sinf(pitch);
        f->rqSquare = QFrames(V3(0, 0, -1), V3(0, 1, 0), f1, u1);
    }
    for (int s = 0; s < 2; s++) {   // (his legs as he stands: what the teeter and the collapse bend from)
        f->toeQ[s] = WorldQ(r, now, s ? J_BALL_R : J_BALL_L);
        f->knee0[s] = Pos(W, s ? J_KNEE_R : J_KNEE_L);
        f->ankle0[s] = Pos(W, s ? J_ANKLE_R : J_ANKLE_L);
        f->footQ0[s] = WorldQ(r, now, s ? J_ANKLE_R : J_ANKLE_L);
        if (feet) {   // (walking: a foot flat on its spot, toes ahead - the knee above it as the leg stood)
            Vec3 ff = feetFwd ? FlatN(feetFwd[s]) : f->dir;
            Vec3 bo = QRot(f->footQ0[s], r.t[s ? J_BALL_R : J_BALL_L]);
            Q flat = QBetween(bo, ff * Len(V3(bo.x, 0, bo.z)) + V3(0, -fabsf(bo.y), 0)) * f->footQ0[s];   // (its toes ahead, the ball down)
            Vec3 an = V3(feet[s].x, Ground(g, gc, feet[s]) + Len(r.t[s ? J_BALL_R : J_BALL_L]) * 0.5f, feet[s].z);
            f->knee0[s] = step[s] ? an + V3(0, Len(r.t[s ? J_ANKLE_R : J_ANKLE_L]), 0) + f->dir * (0.04f * r.k)   // (it'll stand there)
                                  : an + (f->knee0[s] - f->ankle0[s]);
            f->toeQ[s] = flat * Conj(f->footQ0[s]) * f->toeQ[s];   // (the toes keep their set to the foot)
            f->ankle0[s] = an, f->footQ0[s] = flat;
        }
    }
    Vec3 balls[2] = {Pos(W, J_BALL_L), Pos(W, J_BALL_R)};
    for (int s = 0; s < 2; s++) f->footAt[s] = balls[s], balls[s].y = Ground(g, gc, balls[s]);   // (the balls as they are; their
                                                                                                  // ground under them for the pivot)
    if (feet) {   // (walking: both feet on the ground - a swinging one where it was going to land; the teeter puts it down)
        for (int s = 0; s < 2; s++) {
            Vec3 ff = feetFwd ? FlatN(feetFwd[s]) : f->dir;
            Vec3 bo = QRot(WorldQ(r, now, s ? J_ANKLE_R : J_ANKLE_L), r.t[s ? J_BALL_R : J_BALL_L]);
            float bf = Len(V3(bo.x, 0, bo.z));
            Vec3 b = V3(feet[s].x, 0, feet[s].z) + ff * bf;
            b.y = Ground(g, gc, b) + 0.017f * r.k;
            f->footAt[s] = b, balls[s] = V3(b.x, Ground(g, gc, b), b.z);
        }
    }
    for (int s = 0; s < 2; s++) f->stepDur[s] = 0;
    if (feet) {   // (the steps: the one in the air first, the other a beat later)
        int first = (ballNow[0].y - ballNow[1].y) > 0 ? 0 : 1;
        for (int s = 0; s < 2; s++) {
            if (!step[s]) continue;
            float dist = Len(Flat(f->footAt[s] - ballNow[s]));
            f->stepFrom[s] = ballNow[s];
            f->stepDur[s] = Clamp(0.10f + 0.30f * dist, 0.12f, 0.30f);
            f->stepT0[s] = (step[0] && step[1] && s != first) ? 0.08f : 0.0f;
            f->stepLift[s] = ballNow[s].y < Ground(g, gc, ballNow[s]) + 0.05f * r.k ? 0.06f * r.k : 0.0f;   // (a planted foot lifts)
        }
    }
    f->pivot = (balls[0] + balls[1]) * 0.5f + f->dir * (0.04f * r.k);
    f->axis = Norm(Cross(V3(0, 1, 0), f->dir));   // (turning about it by +phi tips him toward dir)
    Vec3 mid = Pos(W, J_ROOT) - f->pivot;
    f->r = fmaxf(0.3f, Len(mid));
    // how far over he is: the balance's lean (his body's middle past his toes - that's why he's going); at least a little, so
    // gravity takes him the way he's going
    f->phi0 = Clamp(lean, 0.12f, 0.6f);
    f->phi = f->phi0;
    f->om = Clamp(speed / f->r, 0.0f, 1.6f) + 0.25f;
    // where he ends: hom_get_up's first frame (face down), his head along dir, its feet where his are now
    f->heading = QBetween(V3(0, 0, -1), f->dir);
    Pose p0;
    ClipPose(r, *cs.getUp, 0, &p0);
    Mat4 W0[J_MAX];
    FK(r, p0, W0);   // (model space: anchor at the origin)
    // placed by its knees: they land where a body dropping to its knees puts them - his toes still where they are, the shins
    // folded back over the feet (knee to toe ~0.32 m, Homer's size)
    Vec3 knee0 = (Pos(W0, J_KNEE_L) + Pos(W0, J_KNEE_R)) * 0.5f;
    Vec3 feetNow = (balls[0] + balls[1]) * 0.5f;
    Vec3 kneeTo = feetNow + f->dir * ((0.32f + 0.12f * Clamp(speed, 0.0f, 2.0f)) * r.k);   // (going: his knees land further on)
    Vec3 a = kneeTo - QRot(f->heading, Flat(knee0));
    a.y = Ground(g, gc, feetNow + f->dir * 0.6f);   // (the ground under his middle as he lies)
    f->anchor = a;
    {   // the ground under him as he'll lie: its plane - the rise along him and across him, over his length and width - through the
        // ground under his middle (not past 14 deg: a curb's edge read as a slope)
        Vec3 c = feetNow + f->dir * 0.6f, side = Norm(Cross(V3(0, 1, 0), f->dir));
        float Lh = 0.5f * r.k, Wh = 0.3f * r.k;
        float ga = (Ground(g, gc, c + f->dir * Lh) - Ground(g, gc, c - f->dir * Lh)) / (2.0f * Lh);
        float gs = (Ground(g, gc, c + side * Wh) - Ground(g, gc, c - side * Wh)) / (2.0f * Wh);
        ga = Clamp(ga, -0.25f, 0.25f), gs = Clamp(gs, -0.25f, 0.25f);
        Vec3 grad = f->dir * ga + side * gs;
        f->tiltN = Norm(V3(-grad.x, 1.0f, -grad.z));
        f->tiltQ = QBetween(V3(0, 1, 0), f->tiltN);
        f->tiltP = V3(c.x, a.y, c.z);
    }
    // (hom_get_up's first frames sink his belly and face ~3.5 cm into the ground, his knees 4-7: each frame lifted by as much as it
    // needs - his knees allowed to press 2 cm in (a kneecap on the ground) - then smoothed. One lift for every frame, the most any
    // needed, left him hovering where less would do: lying, a knee 3 cm over the ground; and every cm he's lifted is a cm his
    // short arms don't reach the ground with)
    {
        Pose q;
        Mat4 Wq[J_MAX];
        float need[31];
        for (int k = 0; k <= 30; k++) {
            ClipPose(r, *cs.getUp, (float)k, &q);
            FK(r, q, Wq);
            float d = 0.0f;
            for (int j : {J_PELVIS, J_SPINE1, J_SPINE2, J_NECK, J_HEAD, J_JAW}) d = fmaxf(d, SkinDepth(r, Wq, j, nullptr, nullptr));
            for (int j : {J_HIP_L, J_HIP_R}) d = fmaxf(d, SkinDepth(r, Wq, j, nullptr, nullptr));   // (his knees: planted, GetupFeet)
            need[k] = Clamp(d + 0.004f, 0.0f, 0.12f);
        }
        for (int k = 0; k <= 30; k++) {   // (no frame below its neighbours' needs, then a little smoothing)
            float m = need[k];
            if (k > 0) m = fmaxf(m, need[k - 1]);
            if (k < 30) m = fmaxf(m, need[k + 1]);
            f->liftF[k] = m;
        }
        float sm[31];
        for (int k = 0; k <= 30; k++) sm[k] = (f->liftF[k > 0 ? k - 1 : 0] + 2.0f * f->liftF[k] + f->liftF[k < 30 ? k + 1 : 30]) * 0.25f;
        for (int k = 0; k <= 30; k++) f->liftF[k] = fmaxf(sm[k], need[k]);
        f->liftF[0] = need[0];   // (lying, as low as he can be)
        f->lift0 = f->liftF[0];
    }
    for (int k = 0; k <= 30; k++) f->kShift[k] = V3(0, 0, 0);
    f->gfn = g, f->gctx = gc;
    for (int s = 0; s < 2; s++) f->handQ[s] = HandFlatQ(s, f->dir), f->handAt[s] = f->anchor;   // (set as the collapse plans the catch)
    PlantStep(f, r, cs, g, gc);
    {   // (the get-up's feet: each ball where the clip has it at the frame it's planted from, and where it steps to)
        static const float fr0[2] = {0.0f, 0.0f}, fr1[2] = {13.0f, 27.0f};
        for (int s = 0; s < 2; s++)
            for (int k = 0; k < 2; k++) {
                Pose q;
                Mat4 Wq[J_MAX];
                float sf = k ? fr1[s] : fr0[s];
                ClipPlaced(r, *cs.getUp, sf, GetupAnchor(f, sf), f->heading, &q);
                GetupTilt(f, sf, &q);
                FK(r, q, Wq);
                (k ? f->footG1 : f->footG0)[s] = Pos(Wq, s ? J_BALL_R : J_BALL_L);
                if (!k) f->kneeG[s] = Pos(Wq, s ? J_KNEE_R : J_KNEE_L);
            }
    }
    ClipPlaced(r, *cs.getUp, 0.0f, f->anchor + V3(0, f->lift0, 0), f->heading, &f->prone);   // (Radical's own, lifted clear: its
    GetupTilt(f, 0.0f, &f->prone);
                                                                                        // arms are set once the collapse plans the catch)
    Mat4 Wp[J_MAX];
    FK(r, f->prone, Wp);
    for (int s = 0; s < 2; s++) {
        f->hand[s] = Contact();
        f->kneeAt[s] = Pos(Wp, s ? J_KNEE_R : J_KNEE_L);
        f->kneeAt[s].y += Ground(g, gc, f->kneeAt[s]) - TiltPlaneY(f, f->kneeAt[s]);
        f->ankleAt[s] = Pos(Wp, s ? J_ANKLE_R : J_ANKLE_L);
        f->ankleAt[s].y += Ground(g, gc, f->ankleAt[s]) - TiltPlaneY(f, f->ankleAt[s]);
    }
    f->reachDelay = Clamp(0.03f + 0.07f * level, 0.03f, 0.14f) * f->rng.Range(0.8f, 1.2f);
    f->first = f->rng.Next() < 0.5f ? 0 : 1;
    f->handLag = f->rng.Range(0.02f, 0.08f);
    // the reach: each arm sets off when the fall is this far along (drunk: later; one arm a little behind the other)
    for (int s = 0; s < 2; s++) {
        f->reachAt[s] = Clamp(0.02f + 0.05f * Clamp(level, 0.0f, 1.4f) + (s == f->first ? 0.0f : 0.06f), 0.02f, 0.2f);   // (late enough
                                                                                     // to read as drunk; any later and a hand slammed down from overhead in three frames)
        f->reaching[s] = false;
    }
    f->teeterDur = f->rng.Range(0.35f, 0.6f) / (1.0f + 0.6f * Clamp(speed, 0.0f, 2.0f));   // (going: his momentum doesn't wait)
    for (int s = 0; s < 2; s++)
        if (f->stepDur[s] > 0) f->teeterDur = fmaxf(f->teeterDur, f->stepT0[s] + f->stepDur[s] + 0.06f);   // (his steps in first)
    PlanGetup(f);
    // (the collapse's timeline is worked out when it begins: the teeter changes where he starts from)
}

// ------------------------------------------------------------------ each frame
// -> the pose to draw; false when he's back on his feet (RECOVER done: the walk has him)
static inline bool FallUpdate(Fall* f, const Rig& r, const ClipSet& cs, float dt, GroundFn g, void* gc, Pose* out)
{
    if (f->ph == F_OFF) return false;
    if (dt <= 0) dt = 1.0f / 60.0f;
    f->t += dt, f->pt += dt;
    float L = Clamp(f->level, 0.3f, 1.6f);
    Mat4 W[J_MAX];
    Pose p = f->S;

    if (f->ph == F_TEETER) {
        // feet stuck, his weight going: the forward half of Radical's teeter (hom_balence 2..16: bent at the hips, arms going round)
        float u = Sat(f->pt / f->teeterDur);
        if (cs.balance) {
            Pose b;
            ClipPose(r, *cs.balance, 2.0f + 14.0f * Smooth(u), &b);
            // (his arms going round, then - as he goes - coming forward again: the reach then starts from in front of him)
            float wArms = Smooth(u * 1.6f) * 0.85f * (1.0f - 0.55f * Smooth((u - 0.55f) / 0.45f));
            for (int j : {J_SPINE1, J_SPINE2, J_NECK, J_HEAD})
                p.q[j] = QSlerp(f->S.q[j], b.q[j], Smooth(u * 1.6f) * 0.85f);
            for (int j : {J_SHO_L, J_ELB_L, J_WR_L, J_SHO_R, J_ELB_R, J_WR_R})
                p.q[j] = QSlerp(f->S.q[j], b.q[j], wArms);
        }
        // the lean grows: tipping about his toes (slowly at first)
        float Lb = 1.45f * r.k;
        float resist = Mix(5.0f, 1.2f, Smooth(u * 1.3f - 0.3f));   // (fighting it, then losing)
        f->om += ((1.5f * 9.81f / Lb) * sinf(f->phi) - resist * f->om) * dt;
        f->phi += f->om * dt;
        Q tip = QAxis(f->axis, f->phi - f->phi0);
        p.rp = f->pivot + QRot(tip, f->S.rp - f->pivot);
        p.rq = tip * QSlerp(f->S.rq, f->rqSquare, Smooth(u * 1.4f));   // (squaring up to it)
        // (his legs tip with him, each about its own toes: up onto them)
        Vec3 K[2], Ar[2];
        for (int s = 0; s < 2; s++) {
            K[s] = f->footAt[s] + QRot(tip, f->knee0[s] - f->footAt[s]);
            Ar[s] = f->footAt[s] + QRot(tip, f->ankle0[s] - f->footAt[s]);
        }
        Pose legsFrom = p;
        LegsOnToes(f, r, &p, K, Ar);
        // (from his legs as they were over the first 0.15 s - a foot on the ground)
        float lw = Smooth(f->pt / 0.15f);
        if (lw < 0.999f)
            for (int s = 0; s < 2; s++)
                if (f->stepDur[s] <= 0)
                    for (int j : {s ? J_HIP_R : J_HIP_L, s ? J_KNEE_R : J_KNEE_L, s ? J_ANKLE_R : J_ANKLE_L, s ? J_BALL_R : J_BALL_L})
                        p.q[j] = QSlerp(legsFrom.q[j], p.q[j], lw);
        // a foot stepping (getting under him): its ball along an arc to its spot - a foot in the air just comes down - turning from
        // how it was to flat on the ground, the knee forward; then it stands there (LegsOnToes, above)
        for (int s = 0; s < 2; s++) {
            if (f->stepDur[s] <= 0) continue;
            float e = Smooth((f->pt - f->stepT0[s]) / f->stepDur[s]);
            if (e >= 0.999f) continue;
            int hp = s ? J_HIP_R : J_HIP_L, kn = s ? J_KNEE_R : J_KNEE_L, an = s ? J_ANKLE_R : J_ANKLE_L, ba = s ? J_BALL_R : J_BALL_L;
            Q fq = QSlerp(WorldQ(r, f->S, an), WorldQ(r, p, an), e);
            Vec3 B = Lerp(f->stepFrom[s], f->footAt[s], e) + V3(0, f->stepLift[s] * sinf(kPi * e), 0);
            Vec3 A = B - QRot(fq, r.t[ba]);
            Q toe = QSlerp(f->S.q[ba], p.q[ba], e);
            Mat4 Wl[J_MAX];
            FK(r, p, Wl);
            IKHinge(r, &p, Wl, hp, kn, an, A, Norm(f->dir + V3(0, 0.3f, 0)), kKneeMax);
            SetWorldQ(r, &p, an, fq);
            p.q[ba] = toe;
        }
        Finish(r, &p, g, gc, true);
        if (u >= 1.0f) {   // (the collapse begins from this pose: his feet, knees, hips where the teeter left them)
            f->S = p, f->ph = F_TOPPLE, f->pt = 0;
            Mat4 Wt[J_MAX];
            FK(r, p, Wt);
            for (int s = 0; s < 2; s++) {
                f->ballS[s] = f->footAt[s];
                f->hipS[s] = Pos(Wt, s ? J_HIP_R : J_HIP_L);
                f->kneeS[s] = Pos(Wt, s ? J_KNEE_R : J_KNEE_L);
                f->ankleS[s] = Pos(Wt, s ? J_ANKLE_R : J_ANKLE_L);
            }
            f->hipMidS = (f->hipS[0] + f->hipS[1]) * 0.5f;
            f->lean0 = f->phi;
            f->kneesDown = false;
            CollapsePlan(f, r);
            PlantStep(f, r, cs, f->gfn, f->gctx);           // (the right hand's step: its palm height with the hand as it lands)
            GetupPose(f, r, cs, 0.0f, true, &f->prone);   // (the prone pose with his hands where they'll land)
        }
        *out = p, f->prev = p;
        return true;
    }

    if (f->ph == F_TOPPLE || f->ph == F_CATCH) {
        // the collapse, along its timeline (CollapsePlan): his knees go, his top half over them, his hands landing as it reaches them
        float c = 1, th = 0;
        CollapseAt(f, f->ph == F_TOPPLE ? f->pt : f->tCatch, &c, &th);
        float prog = Sat(f->pt / fmaxf(0.05f, f->tCatch));   // 0 as he goes .. 1 his hands down
        if (f->ph == F_TOPPLE && !f->kneesDown && f->tKnee > 0 && f->pt >= f->tKnee) f->kneesDown = true, f->thuds++;   // (knees: thud)
        CollapsePose(f, r, c, th, &p);
        // arms: as he goes - a hair late with drink - out and down, each hand along a curve from where it was to where it will lie,
        // timed by the collapse itself: they land as it reaches them
        FK(r, p, W);
        for (int s = 0; s < 2; s++) {
            int sh = s ? J_SHO_R : J_SHO_L, el = s ? J_ELB_R : J_ELB_L, wr = s ? J_WR_R : J_WR_L;
            float p0 = f->reachAt[s];
            if (prog < p0 && f->ph == F_TOPPLE) continue;
            if (!f->reaching[s]) {   // (the reach begins: its curve from here, bowed forward and up - a hand swung out to catch him)
                f->reaching[s] = true;
                f->r0[s] = Pos(W, wr);
                f->r1[s] = Lerp(f->r0[s], f->handAt[s], 0.5f) + f->dir * (0.12f * r.k) + V3(0, 0.10f * r.k, 0);
            }
            float pEnd = s == f->first ? 0.88f : 1.0f;   // (one hand down a couple of frames before the other)
            float v = f->ph == F_CATCH ? 1.0f : Sat((prog - p0) / fmaxf(0.05f, pEnd - p0));
            float e = Smooth(v);
            Vec3 a = Lerp(f->r0[s], f->r1[s], e), b = Lerp(f->r1[s], f->handAt[s], e);
            Vec3 target = Lerp(a, b, e);
            float w = Smooth(v * 1.6f);   // (into the curve over most of the reach, not snapped onto it)
            Vec3 S = Pos(W, sh), E = Pos(W, el), line = Norm(target - S);
            Vec3 cur = E - S - line * Dot(E - S, line);
            Vec3 out_ = Norm(S - Pos(W, J_SPINE2));
            // (the elbow from where it is to a push-up's - back and out - as the hand comes down)
            Vec3 push = Norm(Flat(out_) * 0.7f - f->dir * 0.7f);
            Vec3 pole = Norm((Len(cur) > 1e-4f ? Norm(cur) : out_) * (1.0f - Smooth(v)) + push * Smooth(v));
            ArmIK(r, &p, s, target, pole, w);
            HandTurn(r, &p, s, f->handQ[s], Smooth(v));
            FK(r, p, W);
        }
        if (f->ph == F_TOPPLE && prog >= 1.0f) {
            f->ph = F_CATCH, f->pt = 0, f->catchT = f->t;
            // how fast he's still going down, in the catch's own measure (its share of the drop to the prone pose per second): the
            // catch starts at that speed - his arms take it, he doesn't stop dead
            {
                float c1, th1, c0, th0;
                CollapseAt(f, f->tCatch, &c1, &th1);
                CollapseAt(f, f->tCatch - 1.0f / 30.0f, &c0, &th0);
                Pose a0, a1;
                CollapsePose(f, r, c0, th0, &a0);
                CollapsePose(f, r, c1, th1, &a1);
                Mat4 W0[J_MAX], W1[J_MAX], Wp2[J_MAX];
                FK(r, a0, W0), FK(r, a1, W1), FK(r, f->prone, Wp2);
                float vchest = Len(Pos(W1, J_SPINE2) - Pos(W0, J_SPINE2)) * 30.0f;
                float drop = fmaxf(0.05f, Len(Pos(W1, J_SPINE2) - Pos(Wp2, J_SPINE2)));
                f->catchV = Clamp(vchest / drop, 0.0f, 12.0f);
                f->impact = Clamp(vchest / 2.5f, 0.2f, 1.0f);
            }
            for (int s = 0; s < 2; s++) {
                f->hand[s].on = true, f->hand[s].at = f->handAt[s];
                f->catchFrom[s] = Pos(W, s ? J_WR_R : J_WR_L);
                f->catchAnk[s] = Pos(W, s ? J_ANKLE_R : J_ANKLE_L);
                f->catchFootQ[s] = WorldQ(r, p, s ? J_ANKLE_R : J_ANKLE_L);
                f->catchToe[s] = p.q[s ? J_BALL_R : J_BALL_L];
            }
            f->S = p;   // (from here on, the catch blends from this)
            f->thuds++;
        }
        if (f->ph == F_CATCH) {
            // his weight onto his hands: elbows give, knees and belly down, into hom_get_up's first frame exactly
            // his arms take the landing: from the speed he lands with to rest in the prone pose along a Hermite - its length from that
            // speed (a slope of 2.5, the most that doesn't overshoot): ~0.3 s from a hard landing, more from a soft one. His speed
            // carries on, then eases out (a 1.2 g stop on bracing arms) - no dead stop, no lurch
            float T = Clamp(2.5f / fmaxf(0.5f, f->catchV), 0.22f, 0.6f);
            float m0 = Clamp(f->catchV * T, 0.0f, 2.5f);
            float ub = Sat(f->pt / T);
            float e = (ub * ub * ub - 2 * ub * ub + ub) * m0 + (-2 * ub * ub * ub + 3 * ub * ub);
            e = Clamp(e, 0.0f, 1.0f);
            float eL = Smooth(f->pt / 0.45f);   // (his legs settle at their own pace - his toes don't flip over with the slam)
            float u = fminf(ub, Sat(f->pt / 0.45f));
            Pose q;
            Blend(r, f->S, f->prone, e, &q);
            // (the root along with the rest: Blend has it - straight to the prone pose's, as fast as the spring)
            // hands stay put, the elbows bending the way the blended pose bends them (its twist kept)
            for (int s = 0; s < 2; s++) {
                FK(r, q, W);
                int sh = s ? J_SHO_R : J_SHO_L;
                Vec3 S = Pos(W, sh);
                Vec3 out_ = Norm(Pos(W, sh) - Pos(W, J_SPINE2));
                Vec3 at = Lerp(f->catchFrom[s], f->hand[s].at, Smooth(f->pt / 0.08f));
                ArmIK(r, &q, s, at, Norm(Flat(out_) * 0.7f - f->dir * 0.7f), 1.0f);   // (a push-up's elbows: back and out)
                (void)S;
                HandTurn(r, &q, s, f->handQ[s], 1.0f);
            }
            // knees and feet: from where the collapse left them to the prone pose's, at the blend's pace (the knees toward their marks)
            {
                Vec3 ank[2], pole[2];
                Q fq[2];
                FK(r, q, W);
                for (int s = 0; s < 2; s++) {
                    ank[s] = Lerp(f->catchAnk[s], f->ankleAt[s], eL);
                    Vec3 hp = Pos(W, s ? J_HIP_R : J_HIP_L);
                    pole[s] = Norm(f->kneeAt[s] - Lerp(hp, ank[s], 0.5f) + f->dir * 0.03f);
                    fq[s] = QSlerp(f->catchFootQ[s], WorldQ(r, f->prone, s ? J_ANKLE_R : J_ANKLE_L), eL);
                }
                LegsIK(r, &q, ank, fq, pole, 1.0f);
                for (int s = 0; s < 2; s++) {   // (the toes too)
                    int b = s ? J_BALL_R : J_BALL_L;
                    q.q[b] = QSlerp(f->catchToe[s], f->prone.q[b], eL);
                }
            }
            p = q;
            if (u >= 1.0f) {
                f->ph = F_DOWN, f->pt = 0;
                f->thuds++;
            }
        }
        Finish(r, &p, g, gc, true);
        *out = p, f->prev = p;
        return true;
    }

    if (f->ph == F_DOWN) {
        // a beat face down, breathing hard (his back rising and falling; his hands where they landed)
        Struggle st;
        st.breathe = sinf(f->pt * 2.4f) * 0.03f;
        GetupPose(f, r, cs, 0.0f, true, &p, &st);
        if (f->pt < 0.15f) {
            Pose q = p;
            Blend(r, f->prev, q, Smooth(f->pt / 0.15f), &p);
        }
        float down = 0.45f + 0.4f * L;
        if (f->pt > down) f->ph = F_GETUP, f->pt = 0, f->si = 0, f->sf = 0, f->groans++;
        Finish(r, &p, g, gc, false);
        *out = p, f->prev = p;
        return true;
    }

    if (f->ph == F_GETUP) {
        // hom_get_up through the plan's segments
        GetupSeg& s = f->seg[f->si];
        float u = Sat(f->pt / fmaxf(0.01f, s.dur));
        float e = s.kind == GS_HOLD ? 0.0f : GetupEase(s.ease, u);
        f->sf = s.f0 + (s.f1 - s.f0) * e;
        // struggling, as layers that fade in and out with where he is in the clip (nothing switches on or off between segments):
        // his body trembling as his arms push it up (frames 0-6 - harder while he holds, drunker harder), his head hanging on all
        // fours (around 6), a sway on one knee (around 13), and breathing all the while
        Struggle st;
        // (the shake is his arms', not his body's: a small pulse at the chest the pinned hands take up in the elbows - shaking the
        // whole body shook his knees and feet on the ground too. Smooth noise, not a sine: arms giving and catching)
        float pushing = Smooth((6.5f - f->sf) / 1.5f) * (s.kind == GS_GIVE ? 0.0f : 1.0f) * (s.kind == GS_HOLD && s.both ? 1.8f : 1.0f);
        st.tremble = Wobble(f->t * 11.0f, f->seed * 1.7f) * 0.018f * (0.5f + 0.5f * L) * pushing;
        st.breathe = sinf(f->t * 2.6f) * 0.03f;
        st.headHang = 0.15f * Smooth(1.0f - fabsf(f->sf - 6.0f) / 2.5f);
        st.wobble = Wobble(f->t * 1.7f, f->seed) * 0.10f * L * (Smooth(1.0f - fabsf(f->sf - 13.0f) / 3.5f) + 0.5f * Smooth(1.0f - fabsf(f->sf - 6.0f) / 2.0f));
        GetupPose(f, r, cs, f->sf, s.both, &p, &st);
        if (s.kind == GS_GIVE && u >= 1.0f) f->thuds++, f->groans++;
        if (u >= 1.0f) {
            f->si++, f->pt = 0;
            if (f->si >= f->nseg) f->ph = F_RECOVER, f->pt = 0;
        }
        Finish(r, &p, g, gc, false);
        *out = p, f->prev = p;
        return true;
    }

    if (f->ph == F_RECOVER) {
        GetupPose(f, r, cs, 30.0f, false, &p);
        Finish(r, &p, g, gc, false);
        *out = p, f->prev = p;
        if (f->pt > 0.05f) {
            f->ph = F_OFF;
            return false;
        }
        return true;
    }
    return false;
}

// where his game body should stand when he's up (hom_get_up's last frame: on the ground under Character_Root), and his heading
static inline void FallStandAt(const Fall& f, const Rig& r, const ClipSet& cs, Vec3* at, Q* heading)
{
    Pose p;
    ClipPlaced(r, *cs.getUp, 30.0f, GetupAnchor(&f, 30.0f) - V3(0, GetupLift(&f, 30.0f), 0), f.heading, &p);
    Mat4 W[J_MAX];
    FK(r, p, W);
    Vec3 m = Pos(W, J_MOTION);
    *at = V3(p.rp.x, f.anchor.y, p.rp.z);
    (void)m;
    *heading = f.heading;
}

}   // namespace db
