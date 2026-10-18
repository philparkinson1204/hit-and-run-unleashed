// The drunk body (Item 2, refined): his skeleton posed in joint space, the way Radical's own clips pose it.
//
// Why: the first drunk mode drew the fall from a particle ragdoll, each limb swung onto its particle by a shortest-arc turn and
// each joint put where its particle was. That bent knees and elbows sideways and backwards (Radical's are pure hinges about their
// local Z, 0 to ~156 deg, never past straight), twisted them (an elbow 136 deg off its hinge), stretched bones with the soft
// sticks and let the neck turn unchecked (the chest-neck skin 3.4x). The QA's numbers: tools/lab/qa/animqa.py on out/qa/cur_ff.trace.
//
// Here a pose is a rotation per joint (in its parent's frame) and the world placement of Character_Root; the bones are always the
// rig's own lengths; knees and elbows only ever turn about their hinge; the other joints are held inside the ranges Radical's
// clips use (dbody_rom.h); hands, knees and feet are put on the ground by two-bone IK that keeps the hinge. The motion comes from
// Radical's own clips (his get-up, the NPCs' landing - read at run time from the player's copy of the game, never shipped) and
// from procedural layers (stepping, the lean, the physics of the fall) - dbody_motion.h.
//
// Platform-neutral (math3d.h only): the mod includes it (drunkbody.inc) and so does the offline lab harness (tools/lab/bodysim).
// Conventions as the mod's: row-vector matrices (p' = p M), translation in row 3, child world = child local x parent world.
// Quaternions are the textbook (column) convention: QRow(q) is the same rotation as a row-vector matrix; a joint's world
// rotation is (parent's world) * (its local).
#pragma once
#include "math3d.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <initializer_list>
#include "dbody_rom.h"
#include "dbody_points.h"

namespace db {

static const float kPi = 3.14159265f;
static inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float Sat(float v) { return Clamp(v, 0.0f, 1.0f); }
static inline float Smooth(float u) { u = Sat(u); return u * u * (3.0f - 2.0f * u); }
static inline float Smoother(float u) { u = Sat(u); return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f); }
static inline float Mix(float a, float b, float t) { return a + (b - a) * t; }
static inline float Wrap(float a) { return remainderf(a, 2.0f * kPi); }
static inline float Deg(float r) { return r * (180.0f / kPi); }
static inline float Rad(float d) { return d * (kPi / 180.0f); }

// ------------------------------------------------------------------ quaternions
struct Q { float x, y, z, w; };
static inline Q Qi() { return {0, 0, 0, 1}; }
static inline Q operator*(Q a, Q b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static inline Q Conj(Q a) { return {-a.x, -a.y, -a.z, a.w}; }
static inline float QDot(Q a, Q b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline Q QNeg(Q a) { return {-a.x, -a.y, -a.z, -a.w}; }
static inline Q QNorm(Q a)
{
    float n = sqrtf(QDot(a, a));
    if (n < 1e-12f) return Qi();
    float k = 1.0f / n;
    return {a.x * k, a.y * k, a.z * k, a.w * k};
}
static inline Q QAxis(Vec3 ax, float ang)
{
    float l = Len(ax);
    if (l < 1e-9f) return Qi();
    float s = sinf(ang * 0.5f) / l;
    return {ax.x * s, ax.y * s, ax.z * s, cosf(ang * 0.5f)};
}
static inline Q QRotVec(Vec3 rv) { return QAxis(rv, Len(rv)); }   // (a rotation vector: axis x angle)
static inline Vec3 QLog(Q q)   // -> rotation vector
{
    if (q.w < 0) q = QNeg(q);
    Vec3 v = {q.x, q.y, q.z};
    float s = Len(v);
    if (s < 1e-9f) return v * 2.0f;
    return v * (2.0f * atan2f(s, q.w) / s);
}
static inline float QAngle(Q q) { return 2.0f * atan2f(sqrtf(q.x * q.x + q.y * q.y + q.z * q.z), fabsf(q.w)); }
static inline Vec3 QRot(Q q, Vec3 v)
{
    Vec3 u = {q.x, q.y, q.z};
    Vec3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
static inline Q QSlerp(Q a, Q b, float t)
{
    float d = QDot(a, b);
    if (d < 0) b = QNeg(b), d = -d;
    if (d > 0.9995f) return QNorm({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    float th = acosf(d), s = sinf(th), k0 = sinf((1 - t) * th) / s, k1 = sinf(t * th) / s;
    return {a.x * k0 + b.x * k1, a.y * k0 + b.y * k1, a.z * k0 + b.z * k1, a.w * k0 + b.w * k1};
}
static inline Mat4 QRow(Q q)   // the row-vector matrix (the transpose of the textbook one)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 m = Identity();
    m.m[0] = 1 - 2 * (y * y + z * z), m.m[1] = 2 * (x * y + w * z), m.m[2] = 2 * (x * z - w * y);
    m.m[4] = 2 * (x * y - w * z), m.m[5] = 1 - 2 * (x * x + z * z), m.m[6] = 2 * (y * z + w * x);
    m.m[8] = 2 * (x * z + w * y), m.m[9] = 2 * (y * z - w * x), m.m[10] = 1 - 2 * (x * x + y * y);
    return m;
}
static inline Q RowQ(const Mat4& M)   // a row-vector matrix's rotation (its rows normalised first: the game's carry a scale)
{
    Vec3 r0 = Norm(Row(M, 0)), r1 = Norm(Row(M, 1)), r2 = Norm(Row(M, 2));
    // textbook C = M^T: C[i][j] = M[j][i]
    float c00 = r0.x, c01 = r1.x, c02 = r2.x, c10 = r0.y, c11 = r1.y, c12 = r2.y, c20 = r0.z, c21 = r1.z, c22 = r2.z;
    float tr = c00 + c11 + c22;
    Q q;
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q = {(c21 - c12) / s, (c02 - c20) / s, (c10 - c01) / s, 0.25f * s};
    } else if (c00 > c11 && c00 > c22) {
        float s = sqrtf(1.0f + c00 - c11 - c22) * 2;
        q = {0.25f * s, (c01 + c10) / s, (c02 + c20) / s, (c21 - c12) / s};
    } else if (c11 > c22) {
        float s = sqrtf(1.0f + c11 - c00 - c22) * 2;
        q = {(c01 + c10) / s, 0.25f * s, (c12 + c21) / s, (c02 - c20) / s};
    } else {
        float s = sqrtf(1.0f + c22 - c00 - c11) * 2;
        q = {(c02 + c20) / s, (c12 + c21) / s, 0.25f * s, (c10 - c01) / s};
    }
    return QNorm(q);
}
static inline Q QBetween(Vec3 a, Vec3 b)   // the shortest turn taking direction a to direction b
{
    a = Norm(a), b = Norm(b);
    float d = Dot(a, b);
    if (d < -0.99999f) {
        Vec3 o = fabsf(a.x) < 0.9f ? Cross(a, V3(1, 0, 0)) : Cross(a, V3(0, 1, 0));
        return QAxis(o, kPi);
    }
    Vec3 c = Cross(a, b);
    return QNorm({c.x, c.y, c.z, 1.0f + d});
}
// the turn taking frame (e1, e2) onto (f1, f2): e1 exactly onto f1, e2 as near f2 as it can go
static inline Q QFrames(Vec3 e1, Vec3 e2, Vec3 f1, Vec3 f2)
{
    Q q1 = QBetween(e1, f1);
    Vec3 e2r = QRot(q1, e2);
    f1 = Norm(f1);
    Vec3 a = e2r - f1 * Dot(e2r, f1), b = f2 - f1 * Dot(f2, f1);
    if (Len(a) < 1e-6f || Len(b) < 1e-6f) return q1;
    float ang = atan2f(Dot(Cross(a, b), f1), Dot(a, b));
    return QAxis(f1, ang) * q1;
}
static inline Q QYaw(float yaw) { return QAxis(V3(0, 1, 0), yaw); }

// ------------------------------------------------------------------ the rig
enum {
    J_MOTION = 0, J_EXTRA, J_BALANCE, J_ROOT, J_PELVIS, J_HIP_L, J_KNEE_L, J_ANKLE_L, J_BALL_L, J_HIP_R, J_KNEE_R, J_ANKLE_R, J_BALL_R,
    J_ASS, J_SPINE1, J_SPINE2, J_NECK, J_HEAD, J_JAW, J_SHO_L, J_ELB_L, J_WR_L, J_MIDB_L, J_MID_L, J_THB_L, J_TH_L,
    J_SHO_R, J_ELB_R, J_WR_R, J_MIDB_R, J_MID_R, J_THB_R, J_TH_R, J_STD = 33, J_MAX = 40
};
static const char* const kJointNames[J_STD] = {
    "Motion_Root", "Extra_Attributes", "Balance_Root", "Character_Root", "Pelvis", "Hip_L", "Knee_L", "Ankle_L", "Ball_L", "Hip_R",
    "Knee_R", "Ankle_R", "Ball_R", "Ass_Joint", "Spine_1", "Spine_2", "Neck", "Head", "Jaw", "Shoulder_L", "Elbow_L", "Wrist_L",
    "Middle_Base_L", "Middle_L", "Thumb_Base_L", "Thumb_L", "Shoulder_R", "Elbow_R", "Wrist_R", "Middle_Base_R", "Middle_R",
    "Thumb_Base_R", "Thumb_R"};

struct Lim { int j; Vec3 ax, b1, b2; float tw0, tw1, t1a, t1b, t2a, t2b; };

struct Rig {
    int n = 0;
    int parent[J_MAX];
    Vec3 t[J_MAX];       // rest local translation (the bone from the parent)
    Q bq[J_MAX];         // bind (rest) local rotation
    Mat4 bindW[J_MAX];   // bind world (model space)
    Mat4 invBind[J_MAX];
    Mat4 restL[J_MAX];   // rest local matrices
    Lim lim[16];
    int nlim = 0;
    float k = 1.0f;      // size against Homer's (pelvis-to-head): the other characters' strides and reaches scale with it
    // measured from the rig
    float hipH;          // the hips above the ground at rest (model)
    float pelvisH;       // Character_Root above the ground at rest
    float legLen;        // hip -> ankle, straight
    float armLen;        // shoulder -> wrist, straight
    float ankleH;        // the ankle above the ground at rest
};

static inline Mat4 Local(Q q, Vec3 t)
{
    Mat4 m = QRow(q);
    SetRow(m, 3, t, 1);
    return m;
}

// from the parents and the inverse binds (the mod reads the game's from memory; the harness from the export)
static bool RigBuild(Rig* r, int n, const int* parent, const Mat4* invBind)
{
    if (n < J_STD || n > J_MAX) return false;
    r->n = n;
    for (int j = 0; j < n; j++) {
        r->parent[j] = parent[j];
        r->invBind[j] = invBind[j];
        r->bindW[j] = FullInverse(invBind[j]);
    }
    for (int j = 0; j < n; j++) {
        int p = r->parent[j];
        Mat4 L = (j == 0 || p < 0 || p == j) ? r->bindW[j] : Mul(r->bindW[j], FullInverse(r->bindW[p]));
        r->restL[j] = L;
        r->t[j] = Row(L, 3);
        r->bq[j] = RowQ(L);
    }
    float footY = fminf(Row(r->bindW[J_BALL_L], 3).y, Row(r->bindW[J_BALL_R], 3).y);
    r->hipH = Row(r->bindW[J_HIP_L], 3).y - footY;
    r->pelvisH = Row(r->bindW[J_ROOT], 3).y - footY;
    r->ankleH = Row(r->bindW[J_ANKLE_L], 3).y - footY;
    r->legLen = Len(r->t[J_KNEE_L]) + Len(r->t[J_ANKLE_L]);
    r->armLen = Len(r->t[J_ELB_L]) + Len(r->t[J_WR_L]);
    r->k = Clamp(Len(Row(r->bindW[J_HEAD], 3) - Row(r->bindW[J_PELVIS], 3)) / 0.80f, 0.4f, 1.5f);
    // the joint ranges (Homer's, measured: dbody_rom.h); the reference directions taken into each joint's bind frame
    r->nlim = 0;
    for (const DbRomEntry& e : kDbRom) {
        if (e.j >= n || r->nlim >= 16) continue;
        Lim& L = r->lim[r->nlim++];
        L.j = e.j;
        L.ax = Norm(V3(e.ax[0], e.ax[1], e.ax[2]));
        Q gb = RowQ(r->bindW[e.j]);
        Vec3 u1 = QRot(Conj(gb), V3(e.u1[0], e.u1[1], e.u1[2])), u2 = QRot(Conj(gb), V3(e.u2[0], e.u2[1], e.u2[2]));
        L.b1 = Norm(Cross(L.ax, u1)), L.b2 = Norm(Cross(L.ax, u2));
        L.tw0 = Rad(e.tw[0]), L.tw1 = Rad(e.tw[1]);
        L.t1a = Rad(e.t1[0]), L.t1b = Rad(e.t1[1]), L.t2a = Rad(e.t2[0]), L.t2b = Rad(e.t2[1]);
    }
    return true;
}

// ------------------------------------------------------------------ poses
struct Pose {
    Q q[J_MAX];   // local rotations (joints 4 and on; 0-3 unused)
    Vec3 rp;      // Character_Root in the world
    Q rq;
};

static inline void PoseRest(const Rig& r, Pose* p)
{
    for (int j = 0; j < r.n; j++) p->q[j] = r.bq[j];
    p->rp = Row(r.bindW[J_ROOT], 3);
    p->rq = RowQ(r.bindW[J_ROOT]);
}

// world matrices; joints 0-2 follow Character_Root back through their rest locals (nothing of him is skinned to them)
static inline void FK(const Rig& r, const Pose& p, Mat4* W)
{
    W[J_ROOT] = Local(p.rq, p.rp);
    W[J_BALANCE] = Mul(FullInverse(r.restL[J_ROOT]), W[J_ROOT]);
    W[J_MOTION] = Mul(FullInverse(r.restL[J_BALANCE]), W[J_BALANCE]);
    W[J_EXTRA] = Mul(r.restL[J_EXTRA], W[J_MOTION]);
    for (int j = 4; j < r.n; j++) W[j] = Mul(Local(p.q[j], r.t[j]), W[r.parent[j]]);
}
static inline Q WorldQ(const Rig& r, const Pose& p, int j)   // a joint's world rotation, from the pose alone
{
    if (j <= J_ROOT) return p.rq;
    return WorldQ(r, p, r.parent[j]) * p.q[j];
}
static inline Vec3 Pos(const Mat4* W, int j) { return Row(W[j], 3); }

static inline void Blend(const Rig& r, const Pose& a, const Pose& b, float t, Pose* out)
{
    for (int j = 4; j < r.n; j++) out->q[j] = QSlerp(a.q[j], b.q[j], t);
    out->rp = Lerp(a.rp, b.rp, t);
    out->rq = QSlerp(a.rq, b.rq, t);
}
// a part of the body only (a joint and everything under it)
static inline bool Under(const Rig& r, int j, int top)
{
    for (int k = j, guard = 0; k > J_ROOT && guard < J_MAX; k = r.parent[k], guard++)
        if (k == top) return true;
    return j == top;
}
static inline void BlendPart(const Rig& r, const Pose& a, const Pose& b, float t, int top, Pose* out)
{
    for (int j = 4; j < r.n; j++)
        if (Under(r, j, top)) out->q[j] = QSlerp(a.q[j], b.q[j], t);
}

// ------------------------------------------------------------------ Radical's clips (Pure3D animation banks, art\chars\*_a.p3d)
// Format: research/anim/decode.py (verified there against every clip of the five players' banks and the NPCs'). A group drives
// the joint with its name; its ROT replaces the joint's rest rotation; TRAN only on the root joints.
struct Track {
    int j;
    int nr, nt;
    uint16_t* fr;      // rotation key frames
    float* qr;         // (x, y, z, w) per key
    uint16_t* ft;      // translation key frames
    float* vt;         // (x, y, z) per key
    bool ir, it;       // interpolate
};
struct Clip {
    char name[48];
    float frames, rate;
    bool cyclic;
    int nt;
    Track* tr;
};
struct Bank {
    int n = 0;
    Clip* c = nullptr;
    uint8_t* mem = nullptr;   // one block for everything
    size_t used = 0, cap = 0;
};

namespace detail {
static inline uint32_t U32(const uint8_t* b) { uint32_t v; memcpy(&v, b, 4); return v; }
static inline uint16_t U16(const uint8_t* b) { uint16_t v; memcpy(&v, b, 2); return v; }
static inline float F32(const uint8_t* b) { float v; memcpy(&v, b, 4); return v; }
static inline void* Take(Bank* k, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (k->used + n > k->cap) return nullptr;
    void* p = k->mem + k->used;
    k->used += n;
    return p;
}
static inline int JointIndex(const char* name)
{
    for (int j = 0; j < J_STD; j++)
        if (!strcmp(name, kJointNames[j])) return j;
    return -1;
}
enum { ANIM = 0x121000, GROUP = 0x121001, GLIST = 0x121002, CH_VEC1 = 0x121102, CH_VEC2 = 0x121103, CH_VEC3 = 0x121104,
       CH_QUAT = 0x121105, CH_CQUAT = 0x121111, CH_INTERP = 0x121110 };
// one channel into a track (rotations and root translations only; the rest of what a bank holds isn't needed)
static inline bool Channel(Bank* k, Track* t, const uint8_t* b, uint32_t cid, size_t o, size_t dsz, size_t tsz, size_t end)
{
    if (o + 20 > end) return false;
    size_t q = o + 12 + 4;   // (version)
    char param[5] = {0};
    memcpy(param, b + q, 4);
    q += 4;
    int index = 0;
    float cst[3] = {0, 0, 0};
    if (cid == CH_VEC1 || cid == CH_VEC2) {
        index = U16(b + q), q += 2;
        cst[0] = F32(b + q), cst[1] = F32(b + q + 4), cst[2] = F32(b + q + 8), q += 12;
    }
    uint32_t n = U32(b + q);
    q += 4;
    if (n == 0 || n > 4096 || q + 2 * n > end) return false;
    const uint8_t* frames = b + q;
    q += 2 * n;
    bool interp = true;
    for (size_t c = o + dsz; c + 12 <= o + tsz;) {   // the interpolation mode child
        uint32_t id = U32(b + c), ts = U32(b + c + 8);
        if (ts < 12) break;
        if (id == CH_INTERP && c + 20 <= end) interp = U32(b + c + 16) != 0;
        c += ts;
    }
    if (cid == CH_QUAT || cid == CH_CQUAT) {
        if (!memcmp(param, "ROT", 3)) {
            size_t need = (cid == CH_QUAT ? 16 : 8) * (size_t)n;
            if (q + need > end) return false;
            t->fr = (uint16_t*)Take(k, 2 * n), t->qr = (float*)Take(k, 16 * n);
            if (!t->fr || !t->qr) return false;
            for (uint32_t i = 0; i < n; i++) {
                t->fr[i] = U16(frames + 2 * i);
                float w, x, y, z;
                if (cid == CH_QUAT) w = F32(b + q + 16 * i), x = F32(b + q + 16 * i + 4), y = F32(b + q + 16 * i + 8), z = F32(b + q + 16 * i + 12);
                else {
                    int16_t s[4];
                    memcpy(s, b + q + 8 * i, 8);
                    w = s[0] / 32767.0f, x = s[1] / 32767.0f, y = s[2] / 32767.0f, z = s[3] / 32767.0f;
                }
                Q qq = QNorm({x, y, z, w});
                memcpy(t->qr + 4 * i, &qq, 16);
            }
            t->nr = (int)n, t->ir = interp;
        }
        return true;
    }
    if ((cid == CH_VEC1 || cid == CH_VEC2 || cid == CH_VEC3) && !memcmp(param, "TRAN", 4)) {
        int dim = cid == CH_VEC1 ? 1 : cid == CH_VEC2 ? 2 : 3;
        if (q + 4 * dim * (size_t)n > end) return false;
        t->ft = (uint16_t*)Take(k, 2 * n), t->vt = (float*)Take(k, 12 * n);
        if (!t->ft || !t->vt) return false;
        for (uint32_t i = 0; i < n; i++) {
            t->ft[i] = U16(frames + 2 * i);
            float v[3] = {cst[0], cst[1], cst[2]};
            const uint8_t* s = b + q + 4 * dim * i;
            if (dim == 3) v[0] = F32(s), v[1] = F32(s + 4), v[2] = F32(s + 8);
            else if (dim == 1) v[index] = F32(s);
            else {
                int d0 = index == 0 ? 1 : 0, d1 = index == 2 ? 1 : 2;
                v[d0] = F32(s), v[d1] = F32(s + 4);
            }
            memcpy(t->vt + 3 * i, v, 12);
        }
        t->nt = (int)n, t->it = interp;
    }
    return true;
}
// every Animation chunk at any depth: fn(offset, data size, total size)
template <class F> static inline void Scan(const uint8_t* b, size_t off, size_t end, int depth, F&& fn)
{
    while (off + 12 <= end && depth < 8) {
        uint32_t cid = U32(b + off), dsz = U32(b + off + 4), tsz = U32(b + off + 8);
        if (tsz < 12 || dsz < 12 || off + tsz > end) return;
        if (cid == ANIM) fn(off, (size_t)dsz, (size_t)tsz);
        else Scan(b, off + dsz, off + tsz, depth + 1, fn);
        off += tsz;
    }
}
// an Animation's groups that drive one of the 33 joints: fn(joint, group offset, data size, total size)
template <class F> static inline void Groups(const uint8_t* b, size_t off, size_t dsz, size_t tsz, F&& fn)
{
    for (size_t c2 = off + dsz; c2 + 12 <= off + tsz;) {
        uint32_t id2 = U32(b + c2), d2 = U32(b + c2 + 4), t2 = U32(b + c2 + 8);
        if (t2 < 12 || d2 < 12) break;
        if (id2 == GLIST)
            for (size_t c3 = c2 + d2; c3 + 12 <= c2 + t2;) {
                uint32_t id3 = U32(b + c3), d3 = U32(b + c3 + 4), t3 = U32(b + c3 + 8);
                if (t3 < 12 || d3 < 12) break;
                if (id3 == GROUP) {
                    uint8_t gl = b[c3 + 16];
                    char gname[48] = {0};
                    memcpy(gname, b + c3 + 17, gl < 47 ? gl : 47);
                    int j = JointIndex(gname);
                    if (j >= 0) fn(j, c3, (size_t)d3, (size_t)t3);
                }
                c3 += t3;
            }
        c2 += t2;
    }
}
}   // namespace detail

static inline void BankFree(Bank* k)
{
    free(k->mem), free(k->c);
    k->mem = nullptr, k->c = nullptr, k->n = 0, k->used = k->cap = 0;
}
// every animation in a .p3d (its bytes): counted, then decoded into one block
static inline bool BankLoad(Bank* k, const uint8_t* b, size_t size)
{
    BankFree(k);
    int nclips = 0, ntracks = 0;
    detail::Scan(b, 12, size, 0, [&](size_t off, size_t dsz, size_t tsz) {
        nclips++;
        detail::Groups(b, off, dsz, tsz, [&](int, size_t, size_t, size_t) { ntracks++; });
    });
    if (nclips <= 0) return false;
    k->c = (Clip*)calloc(nclips, sizeof(Clip));
    k->cap = size * 3 + (size_t)(ntracks + nclips) * sizeof(Track) + 4096;   // (keys decoded to floats take more than the file's int16s)
    k->mem = (uint8_t*)malloc(k->cap);
    if (!k->c || !k->mem) return BankFree(k), false;
    int ci = 0;
    bool ok = true;
    detail::Scan(b, 12, size, 0, [&](size_t off, size_t dsz, size_t tsz) {
        if (ci >= nclips || !ok) return;
        Clip* c = &k->c[ci++];
        size_t p = off + 16;
        uint8_t ln = b[p];
        memcpy(c->name, b + p + 1, ln < 47 ? ln : 47);
        p += 1 + ln + 4;
        c->frames = detail::F32(b + p), c->rate = detail::F32(b + p + 4), c->cyclic = detail::U32(b + p + 8) != 0;
        int ng = 0;
        detail::Groups(b, off, dsz, tsz, [&](int, size_t, size_t, size_t) { ng++; });
        c->tr = (Track*)detail::Take(k, sizeof(Track) * (ng > 0 ? ng : 1));
        if (!c->tr) { ok = false; return; }
        detail::Groups(b, off, dsz, tsz, [&](int j, size_t c3, size_t d3, size_t t3) {
            Track* t = &c->tr[c->nt++];
            memset(t, 0, sizeof(*t));
            t->j = j;
            for (size_t c4 = c3 + d3; c4 + 12 <= c3 + t3;) {
                uint32_t id4 = detail::U32(b + c4), d4 = detail::U32(b + c4 + 4), t4 = detail::U32(b + c4 + 8);
                if (t4 < 12 || d4 < 12) break;
                detail::Channel(k, t, b, id4, c4, d4, t4, size);
                c4 += t4;
            }
        });
    });
    k->n = ci;
    if (!ok) return BankFree(k), false;
    return true;
}
static inline const Clip* BankFind(const Bank& k, const char* name)
{
    for (int i = 0; i < k.n; i++)
        if (!strcmp(k.c[i].name, name)) return &k.c[i];
    return nullptr;
}

static inline Q SampleRot(const Track& t, float f)
{
    const Q* q = (const Q*)t.qr;
    if (t.nr == 1 || f <= t.fr[0]) return q[0];
    if (f >= t.fr[t.nr - 1]) return q[t.nr - 1];
    int i = 0;
    while (i + 1 < t.nr && t.fr[i + 1] <= f) i++;
    if (!t.ir) return q[i];
    float u = (f - t.fr[i]) / (float)(t.fr[i + 1] - t.fr[i]);
    return QSlerp(q[i], q[i + 1], u);
}
static inline Vec3 SampleTran(const Track& t, float f)
{
    const Vec3* v = (const Vec3*)t.vt;
    if (t.nt == 1 || f <= t.ft[0]) return v[0];
    if (f >= t.ft[t.nt - 1]) return v[t.nt - 1];
    int i = 0;
    while (i + 1 < t.nt && t.ft[i + 1] <= f) i++;
    if (!t.it) return v[i];
    float u = (f - t.ft[i]) / (float)(t.ft[i + 1] - t.ft[i]);
    return Lerp(v[i], v[i + 1], u);
}

// a clip's pose at frame f: every joint's rotation (the clip's, or the rest); and Character_Root in the clip's model space
// (its Motion_Root / Balance_Root / Character_Root keys composed: the clip's travel and its bob), as a matrix (no scale)
static inline void ClipPose(const Rig& r, const Clip& c, float f, Pose* out, Mat4* rootModel = nullptr)
{
    Q lq[J_MAX];
    Vec3 lt[J_MAX];
    for (int j = 0; j < r.n; j++) lq[j] = r.bq[j], lt[j] = r.t[j];
    for (int i = 0; i < c.nt; i++) {
        const Track& t = c.tr[i];
        if (t.j >= r.n) continue;
        if (t.nr > 0) lq[t.j] = SampleRot(t, f);
        if (t.nt > 0 && t.j <= J_ROOT) lt[t.j] = SampleTran(t, f);
    }
    for (int j = 4; j < r.n; j++) out->q[j] = lq[j];
    Mat4 W0 = Local(lq[J_MOTION], lt[J_MOTION]);
    Mat4 W2 = Mul(Local(lq[J_BALANCE], lt[J_BALANCE]), W0);
    Mat4 W3 = Mul(Local(lq[J_ROOT], lt[J_ROOT]), W2);
    out->rp = Row(W3, 3);
    out->rq = RowQ(W3);
    if (rootModel) *rootModel = W3;
}
// ... placed in the world: the clip's model space at `anchor` (a position on the ground and a heading)
static inline void PlaceRoot(Pose* p, Vec3 anchor, Q heading)
{
    p->rp = anchor + QRot(heading, p->rp);
    p->rq = heading * p->rq;
}

// ------------------------------------------------------------------ joint ranges
// soft clamp: inside 80 % of the range nothing changes; beyond, it eases into the end (never past it)
static inline float SoftClamp(float x, float lo, float hi)
{
    float mid = 0.5f * (lo + hi), half = 0.5f * (hi - lo);
    if (half <= 1e-6f) return mid;
    float u = (x - mid) / half, a = fabsf(u), k = 0.8f;
    if (a <= k) return x;
    float e = k + (1.0f - k) * tanhf((a - k) / (1.0f - k));
    return mid + half * (u < 0 ? -e : e);
}
// one joint into its range: the turn from its bind rotation split into the twist about its bone and the swing; the twist clamped,
// the swing's two tilts held inside an ellipse per quarter (Radical's ranges aren't symmetric)
static inline void LimitJoint(const Rig& r, Pose* p, const Lim& L)
{
    Q D = Conj(r.bq[L.j]) * p->q[L.j];
    if (D.w < 0) D = QNeg(D);
    Vec3 v = {D.x, D.y, D.z};
    float tw = 2.0f * atan2f(Dot(v, L.ax), D.w);
    Q T = QAxis(L.ax, tw);
    Q S = D * Conj(T);   // D = S * T
    Vec3 rv = QLog(S);
    float t1 = Dot(rv, L.b1), t2 = Dot(rv, L.b2);
    float tw2 = SoftClamp(tw, L.tw0, L.tw1);
    float e1 = t1 >= 0 ? t1 / fmaxf(1e-3f, L.t1b) : t1 / fminf(-1e-3f, L.t1a);
    float e2 = t2 >= 0 ? t2 / fmaxf(1e-3f, L.t2b) : t2 / fminf(-1e-3f, L.t2a);
    float e = sqrtf(e1 * e1 + e2 * e2);
    float n1 = t1, n2 = t2;
    if (e > 0.8f) {
        float es = 0.8f + 0.2f * tanhf((e - 0.8f) / 0.2f);
        n1 = t1 * es / e, n2 = t2 * es / e;
    }
    if (tw2 == tw && n1 == t1 && n2 == t2) return;
    Vec3 rv2 = rv + L.b1 * (n1 - t1) + L.b2 * (n2 - t2);
    Q D2 = QRotVec(rv2) * QAxis(L.ax, tw2);
    p->q[L.j] = r.bq[L.j] * D2;
}
static inline void LimitAll(const Rig& r, Pose* p)
{
    for (int i = 0; i < r.nlim; i++) LimitJoint(r, p, r.lim[i]);
}
// the hinges: knees and elbows only ever turn about their local Z, 0 (straight) to the deepest bend Radical plays
static const float kKneeMax = 2.70f, kElbowMax = 2.70f;   // (155 deg)
static inline float HingeAngle(Q q) { if (q.w < 0) q = QNeg(q); return 2.0f * atan2f(q.z, q.w); }
static inline void HingeFix(Pose* p, int j, float maxA)
{
    float a = Clamp(HingeAngle(p->q[j]), 0.0f, maxA);
    p->q[j] = QAxis(V3(0, 0, 1), a);
}
static inline void HingesFix(Pose* p)
{
    HingeFix(p, J_KNEE_L, kKneeMax), HingeFix(p, J_KNEE_R, kKneeMax);
    HingeFix(p, J_ELB_L, kElbowMax), HingeFix(p, J_ELB_R, kElbowMax);
}

// ------------------------------------------------------------------ two-bone IK with a hinge
// a -> b -> c, b a hinge about its local Z: set b's bend so c is as far from a as `target` is, then turn a so c lands on target and
// b bends toward `pole` (a world direction). a's parent stays as it is. -> how far c ends from the target (0: reached).
// W must be the pose's world matrices (at least a's parent's).
static inline float IKHinge(const Rig& r, Pose* p, const Mat4* W, int a, int b, int c, Vec3 target, Vec3 pole, float maxBend)
{
    Q pa = WorldQ(r, *p, r.parent[a]);
    Vec3 A = Row(Mul(Local(p->q[a], r.t[a]), W[r.parent[a]]), 3);
    Vec3 tb = r.t[b], tc = r.t[c];
    float lb = Len(tb), lc = Len(tc);
    Vec3 to = target - A;
    float d = Len(to);
    float dmin = fabsf(lb - lc) + 0.002f, dmax = (lb + lc) * 0.9995f;
    float dc = Clamp(d, dmin, dmax);
    // |tb + Rz(th) tc| = dc  ->  (bx cx + by cy) cos th + (by cx - bx cy) sin th = (dc^2 - |tb|^2 - |tc|^2 - 2 bz cz) / 2
    float P = tb.x * tc.x + tb.y * tc.y, S = tb.y * tc.x - tb.x * tc.y;
    float Rr = sqrtf(P * P + S * S), phi = atan2f(S, P);
    float rhs = (dc * dc - lb * lb - lc * lc - 2.0f * tb.z * tc.z) * 0.5f;
    float cs = Rr > 1e-6f ? Clamp(rhs / Rr, -1.0f, 1.0f) : 1.0f;
    float ac = acosf(cs);
    float th1 = Wrap(phi + ac), th2 = Wrap(phi - ac);
    float cur = HingeAngle(p->q[b]);
    auto ok = [&](float th) { return th >= -0.02f && th <= maxBend + 0.02f; };
    float th = ok(th1) && ok(th2) ? (fabsf(th1 - cur) < fabsf(th2 - cur) ? th1 : th2) : ok(th1) ? th1 : ok(th2) ? th2
             : (fabsf(Wrap(th1)) < fabsf(Wrap(th2)) ? th1 : th2);
    th = Clamp(th, 0.0f, maxBend);
    p->q[b] = QAxis(V3(0, 0, 1), th);
    // a's turn, in two steps that never flip: (1) the smallest turn that takes the end, as the limb is now, onto the target - the
    // limb's twist kept as it came in; (2) a turn about the limb's line bringing the middle joint toward the pole, as much as the
    // limb is bent (a straight limb has no "toward": the pole would only spin it)
    Vec3 Cl = tb + QRot(p->q[b], tc);
    Q Ga = pa * p->q[a];
    if (Len(to) > 1e-6f) {
        Vec3 f1 = to * (1.0f / Len(to));
        Ga = QBetween(QRot(Ga, Cl), f1) * Ga;
        Vec3 mid = QRot(Ga, tb);
        Vec3 mp = mid - f1 * Dot(mid, f1), pp = pole - f1 * Dot(pole, f1);
        float lm = Len(mp), lp = Len(pp);
        if (lm > 1e-5f && lp > 1e-5f) {
            float ang = atan2f(Dot(Cross(mp, pp), f1), Dot(mp, pp));
            float bent = Smooth(lm / (0.18f * lb));   // (the middle joint 18 % of the bone off the line: fully steered)
            Ga = QAxis(f1, ang * bent) * Ga;
        }
    }
    p->q[a] = Conj(pa) * Ga;
    Vec3 C = A + QRot(pa * p->q[a], Cl);
    return Len(C - target);
}
// a joint's world rotation set (as near as its parent allows): its local = parent's world^-1 * wanted
static inline void SetWorldQ(const Rig& r, Pose* p, int j, Q want) { p->q[j] = Conj(WorldQ(r, *p, r.parent[j])) * want; }

// ------------------------------------------------------------------ the ground
typedef float (*GroundFn)(void* ctx, float x, float z);   // the ground's height under (x, z)
static inline float GroundAt(GroundFn g, void* ctx, Vec3 p) { return g ? g(ctx, p.x, p.z) : 0.0f; }

// a skin point in the world, blended between its joint and its second as the game skins it (rigid to its own joint, a knee bent
// double swung its crease round with the shin - 3.5 cm deeper than the skin really goes)
static inline Vec3 PointW(const DbPoint& pt, const Mat4* W)
{
    Vec3 a = XformPoint(V3(pt.p[0], pt.p[1], pt.p[2]), W[pt.j]);
    if (pt.w2 <= 0.0f) return a;
    return a + (XformPoint(V3(pt.p2[0], pt.p2[1], pt.p2[2]), W[pt.j2]) - a) * pt.w2;
}
// how far a joint's skin goes under the ground (>0): its extreme skin points (dbody_points.h: Homer's, measured from his mesh)
static inline float SkinDepth(const Rig& r, const Mat4* W, int j, GroundFn g, void* gc)
{
    float d = -1e9f;
    for (const DbPoint& p : kDbPoints) {
        if (p.j != j) continue;
        Vec3 w = PointW(p, W);
        d = fmaxf(d, GroundAt(g, gc, w) - w.y);
    }
    return d;
}
// a joint's skin out of the ground by bending the joint about its own hinge (local Z: toes, fingers, the foot at the ankle): the
// deepest of its skin points raised to the ground, never more than maxAng; which way lifts is tried, not assumed. (Turning about
// "whatever lifts the deepest point" flipped from frame to frame as the deepest point changed sides.)
static inline float JointDepth(const Rig& r, const Pose& p, int j, GroundFn g, void* gc)
{
    Mat4 W[J_MAX];
    FK(r, p, W);
    float d = -1e9f;
    for (const DbPoint& pt : kDbPoints) {
        if (pt.j != j) continue;
        Vec3 w = PointW(pt, W);
        d = fmaxf(d, GroundAt(g, gc, w) - w.y);
    }
    return d;
}
static inline void Unsink(const Rig& r, Pose* p, int j, GroundFn g, void* gc, float slack = 0.008f, float maxAng = 0.7f)
{
    float d0 = JointDepth(r, *p, j, g, gc);
    if (d0 <= slack) return;
    Q base = p->q[j];
    auto at = [&](float a) { Pose t = *p; t.q[j] = base * QAxis(V3(0, 0, 1), a); return JointDepth(r, t, j, g, gc); };
    float dp = at(0.05f), dm = at(-0.05f);
    float sgn = dp < dm ? 1.0f : -1.0f, best = 0.0f, bestD = d0;
    if (fminf(dp, dm) >= d0) return;   // (neither way lifts it)
    // the smallest bend that brings it out (or as far out as maxAng goes): stepped, then refined
    for (float a = 0.05f; a <= maxAng + 1e-4f; a += 0.05f) {
        float d = at(sgn * a);
        if (d < bestD) bestD = d, best = a;
        if (d <= slack) break;
    }
    if (best > 0.0f && bestD <= slack) {
        float lo = best - 0.05f, hi = best;
        for (int i = 0; i < 5; i++) {
            float mid = 0.5f * (lo + hi);
            if (at(sgn * mid) <= slack) hi = mid; else lo = mid;
        }
        best = hi;
    }
    p->q[j] = base * QAxis(V3(0, 0, 1), sgn * best);
}

// keep his body out of the ground: lifted by the deepest of his middle, chest, head and upper arms past `slack` (his hands, knees
// and feet are put where they go by their own contacts - planted hands, knees on their marks). -> how far he was lifted
static inline float GroundPass(const Rig& r, Pose* p, GroundFn g, void* gc, float slack = 0.01f, float maxLift = 0.25f)
{
    Mat4 W[J_MAX];
    FK(r, *p, W);
    static const int body[] = {J_PELVIS, J_SPINE1, J_SPINE2, J_NECK, J_HEAD, J_JAW, J_SHO_L, J_SHO_R};
    float lift = 0.0f;
    for (int j : body) lift = fmaxf(lift, SkinDepth(r, W, j, g, gc) - slack);
    lift = Clamp(lift, 0.0f, maxLift);
    p->rp.y += lift;
    return lift;
}

}   // namespace db
