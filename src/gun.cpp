// Handgun: equip, 3D model in the player's right hand, aiming pose, firing and effects.
//
// How it hooks into the game:
//  - Pose: the skinned player mesh gets its bone matrices as vertex shader constants at
//    draw time. Gun_PreRender (at the game's first BeginScene of a frame) reads the
//    animated skeleton, builds our right arm - a fist around the grip whenever the gun is
//    out, the aiming pose blended on top - and Gun_SkinResolve swaps those bones in
//    before each skinned draw (scene.cpp collects the uploads).
//  - Aiming camera: an over-the-shoulder view written into the game's camera object for
//    drawing only (Gun_PreRenderCamera); the game's follow camera is kept turned the
//    same way so letting go of aim hands straight back to it.
//  - Facing while aiming: Character::mfDesiredDir is set to the aiming heading.
//  - Gun_Draw renders the gun on our wrist matrix into the 3D world (game camera and
//    depth buffer) just before the game clears depth for its HUD.
//
// Joint indices are shared by every character model in the game:
//   15 Spine_2, 26 Shoulder_R, 27 Elbow_R, 28 Wrist_R, 29/30 Middle_R, 31/32 Thumb_R.
#include "render.h"
#include "math3d.h"
#include "hit.h"
#include <xinput.h>

static const float PI = 3.14159265f;
static const Vec3 UP = {0, 1, 0};

// ------------------------------------------------------------------ small math helpers
static Mat4 ToMat(const float* f)
{
    Mat4 m;
    memcpy(m.m, f, 64);
    return m;
}
static Mat4 Rot3(const Mat4& m)
{
    Mat4 r = m;
    r.m[3] = r.m[7] = r.m[11] = 0;
    r.m[12] = r.m[13] = r.m[14] = 0;
    r.m[15] = 1;
    return r;
}
static Mat4 Transpose3(const Mat4& m)
{
    Mat4 r = Identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.at(i, j) = m.at(j, i);
    return r;
}
static Mat4 WithTrans(Mat4 r, Vec3 t)
{
    SetRow(r, 3, t, 1);
    return r;
}
static Mat4 ScaleM(float s)
{
    Mat4 r = Identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}
static Mat4 Basis(Vec3 x, Vec3 y, Vec3 z)
{
    Mat4 r = Identity();
    SetRow(r, 0, x, 0);
    SetRow(r, 1, y, 0);
    SetRow(r, 2, z, 0);
    return r;
}
double g_daProf[6];   // (m3.1 profiling) ms this second: camera, pose, aim update, probe read, overlay, (spare)
static double ProfNow() { LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return 1000.0 * (double)q.QuadPart / (double)f.QuadPart; }
// (lab) debug "perf <seconds>": what the donut, the can and the drunk body cost a frame (ms: average, worst) over that long, with the
// frame time - the performance numbers for the release report
// (and where an item's frame goes: his body skinned for the push tests, the push tests, the item drawn, the pose rebuilt at a skinned
// draw, the skinned draws' bones resolved, the whole of the start-of-frame build)
static const int kPerfN = 11;
static struct PerfMeter { double sum[kPerfN], mx[kPerfN]; int n[kPerfN]; double until, t0; int frames; } g_perf;
static const char* const kPerfName[kPerfN] = {"donut pose", "can pose", "drunk update", "drunk pose", "frame", "body skin", "item push",
                                              "item draw", "skin refresh", "skin resolve", "pre-render"};
static void PerfAdd(int k, double ms)
{
    if (g_perf.until <= 0.0) return;
    g_perf.sum[k] += ms, g_perf.n[k]++;
    if (ms > g_perf.mx[k]) g_perf.mx[k] = ms;
}
static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float Smooth01(float t)
{
    t = Clamp01(t);
    return t * t * (3 - 2 * t);
}

// damped spring, sub-stepped so it stays stable at any frame time
struct Spring {
    float x, v;
};
static void Step(Spring& s, float w, float z, float dt)
{
    for (; dt > 0; dt -= 1.0f / 240.0f) {
        float h = fminf(dt, 1.0f / 240.0f);
        s.v += (-w * w * s.x - 2 * z * w * s.v) * h;
        s.x += s.v * h;
    }
    if (!isfinite(s.x) || !isfinite(s.v)) s.x = s.v = 0;
}

// Aiming stick: round deadzone with the rest rescaled from zero, a curve for fine control
// near the centre, and a small snap so a nearly flat push turns without drifting up/down.
static void ShapeAimStick(float x, float y, float dz, float* ox, float* oy)
{
    *ox = *oy = 0;
    float mag = sqrtf(x * x + y * y);
    if (mag <= dz) return;
    float t = Clamp01((mag - dz) / (0.95f - dz));
    float c = 0.30f * t + 0.70f * t * t * t;
    float nx = x / mag, ny = y / mag;
    if (fabsf(ny) < 0.17f) {
        ny = 0;
        nx = nx > 0 ? 1.0f : -1.0f;
    } else if (fabsf(nx) < 0.17f) {
        nx = 0;
        ny = ny > 0 ? 1.0f : -1.0f;
    }
    *ox = nx * c;
    *oy = ny * c;
}

// angle into [-PI, PI)
static float WrapPi(float a)
{
    a = fmodf(a + 3.14159265f, 6.28318531f);
    if (a < 0) a += 6.28318531f;
    return a - 3.14159265f;
}
static float EaseOutCubic(float t)
{
    t = Clamp01(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}
static float EaseOutBack(float t)
{
    t = Clamp01(t);
    const float c1 = 1.9f, c3 = c1 + 1;
    return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

struct Quat {
    float x, y, z, w;
};

// matrices here are row-vector rotations: the column-vector form is their transpose
static Quat QFromM(const Mat4& m)
{
    float r00 = m.at(0, 0), r11 = m.at(1, 1), r22 = m.at(2, 2);
    float r01 = m.at(1, 0), r10 = m.at(0, 1), r02 = m.at(2, 0), r20 = m.at(0, 2), r12 = m.at(2, 1), r21 = m.at(1, 2);
    Quat q;
    float tr = r00 + r11 + r22;
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25f * s};
    } else if (r00 > r11 && r00 > r22) {
        float s = sqrtf(1.0f + r00 - r11 - r22) * 2;
        q = {0.25f * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        float s = sqrtf(1.0f + r11 - r00 - r22) * 2;
        q = {(r01 + r10) / s, 0.25f * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        float s = sqrtf(1.0f + r22 - r00 - r11) * 2;
        q = {(r02 + r20) / s, (r12 + r21) / s, 0.25f * s, (r10 - r01) / s};
    }
    return q;
}

static Mat4 MFromQ(Quat q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                     {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                     {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    Mat4 m = Identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m.at(j, i) = R[i][j];
    return m;
}

static Quat Slerp(Quat a, Quat b, float t)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    float k0, k1;
    if (d > 0.9995f) {
        k0 = 1 - t;
        k1 = t;
    } else {
        float th = acosf(d), s = sinf(th);
        k0 = sinf((1 - t) * th) / s;
        k1 = sinf(t * th) / s;
    }
    Quat r = {a.x * k0 + b.x * k1, a.y * k0 + b.y * k1, a.z * k0 + b.z * k1, a.w * k0 + b.w * k1};
    float l = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    return {r.x / l, r.y / l, r.z / l, r.w / l};
}

static Mat4 BlendRot(const Mat4& a, const Mat4& b, float t)
{
    if (t <= 0.0001f) return Rot3(a);
    if (t >= 0.9999f) return Rot3(b);
    return MFromQ(Slerp(QFromM(a), QFromM(b), t));
}

// ------------------------------------------------------------------ pose helpers
static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Heading(Vec3 v) { return atan2f(v.x, v.z); }
static Vec3 HeadingDir(float h) { return V3(sinf(h), 0, cosf(h)); }
// linear up to 70% of the limit, then eases into it with the same slope (no hard stop)
static float SoftLimit(float v, float lim)
{
    float knee = 0.7f * lim, r = lim - knee;
    return v <= knee ? v : lim - r * expf(-(v - knee) / r);
}
static float SoftClamp(float v, float lo, float hi) { return v >= 0 ? SoftLimit(v, hi) : -SoftLimit(-v, -lo); }
// rotate a joint's world matrix by R (world axes) about the joint itself
static Mat4 RotateInPlace(const Mat4& W, const Mat4& R)
{
    Mat4 r = Mul(Rot3(W), R);
    SetRow(r, 3, Row(W, 3), 1);
    return r;
}
// yaw y about UP, then pitch p (+ = up) about the level right axis of heading h + y
static Mat4 YawPitchAt(float h, float y, float p)
{
    return Mul(AxisAngle(UP, y), AxisAngle(Norm(Cross(HeadingDir(h + y), UP)), p));
}
// bone frame: X exactly along xAxis, Z close to zHint, Z = X x Y like every bind-pose joint
static Mat4 FrameXZ(Vec3 xAxis, Vec3 zHint)
{
    Vec3 y = Norm(Cross(zHint, xAxis));
    return Basis(xAxis, y, Cross(xAxis, y));
}
// local-space blend: animated local -> target local rotation, animated bone offset, new parent
static Mat4 BlendLocal(const Mat4& Lanim, const Mat4& Ltarget, const Mat4& Wparent, float w)
{
    return Mul(WithTrans(BlendRot(Rot3(Lanim), Ltarget, w), Row(Lanim, 3)), Wparent);
}
// Two-bone IK: shoulder pS, unit direction u and distance d to the wrist, bone lengths a, b.
// The elbow bends towards `pole` (blending to `poleAlt` when u runs along the pole).
// *hinge = Norm(Cross(upper, fore)) = the arm bones' bind-pose local Z.
static Vec3 TwoBone(Vec3 pS, Vec3 u, float d, float a, float b, Vec3 pole, Vec3 poleAlt, Vec3* hinge)
{
    float cosA = Clampf((a * a + d * d - b * b) / (2 * a * d), -1, 1), sinA = sqrtf(1 - cosA * cosA);
    Vec3 v1 = pole - u * Dot(pole, u), v2 = poleAlt - u * Dot(poleAlt, u);
    float k = Smooth01((Len(v1) - 0.15f) / 0.25f);
    Vec3 v = Norm(v2 * (1 - k) + v1 * k);
    *hinge = Cross(v, u);
    return pS + (u * cosA + v * sinA) * a;
}
static void StepTo(Spring& s, float target, float w, float z, float dt)
{
    s.x -= target;
    Step(s, w, z, dt);
    s.x += target;
}

// ------------------------------------------------------------------ gun model
// Built from boxes/cylinders in "grip space": origin in the middle of the grip,
// +Z down the barrel, +Y up, +X to the gun's right. Metres.
struct GV {
    Vec3 p, n, col;
    float spec;
    int part;   // 0 frame, 1 slide (moves back when firing)
};
// A mesh being built (AddQuad, AddBox, AddCylZ and AddFrustum write into g_mb)
struct MeshBuf {
    GV* v;
    uint16_t* idx;
    int nv, ni, maxv, maxi;
};
static GV g_gv[900];
static uint16_t g_gi[1600];
static MeshBuf g_gunMesh = {g_gv, g_gi, 0, 0, 900, 1600};
static MeshBuf* g_mb = &g_gunMesh;

static const Vec3 MUZZLE = {0, 0.060f, 0.152f};
static const Vec3 EJECT = {0.016f, 0.066f, 0.066f};

static void AddQuadN(const Vec3 p[4], const Vec3 n[4], Vec3 col, float spec, int part)
{
    MeshBuf& m = *g_mb;
    if (m.nv + 4 > m.maxv || m.ni + 6 > m.maxi) return;
    int b = m.nv;
    for (int i = 0; i < 4; i++) m.v[m.nv++] = {p[i], n[i], col, spec, part};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) m.idx[m.ni++] = (uint16_t)(b + idx[i]);
}
static void AddQuad(const Vec3 p[4], Vec3 n, Vec3 col, float spec, int part)
{
    const Vec3 ns[4] = {n, n, n, n};
    AddQuadN(p, ns, col, spec, part);
}

static void AddBox(Vec3 c, Vec3 h, const Mat4& rot, Vec3 col, float spec, int part)
{
    // faces: +x -x +y -y +z -z
    static const int f[6][4][3] = {
        {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},     {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}},
        {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},     {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},     {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}};
    static const Vec3 nrm[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < 6; i++) {
        Vec3 p[4];
        for (int k = 0; k < 4; k++) {
            Vec3 l = {f[i][k][0] * h.x, f[i][k][1] * h.y, f[i][k][2] * h.z};
            p[k] = XformDir(l, rot) + c;
        }
        AddQuad(p, XformDir(nrm[i], rot), col, spec, part);
    }
}

static void AddCylZ(Vec3 c, float r, float z0, float z1, int segs, Vec3 col, float spec, int part)
{
    for (int i = 0; i < segs; i++) {
        float a0 = 2 * PI * i / segs, a1 = 2 * PI * (i + 1) / segs;
        Vec3 d0 = {cosf(a0), sinf(a0), 0}, d1 = {cosf(a1), sinf(a1), 0};
        Vec3 p[4] = {c + d0 * r + V3(0, 0, z0), c + d1 * r + V3(0, 0, z0), c + d1 * r + V3(0, 0, z1), c + d0 * r + V3(0, 0, z1)};
        AddQuad(p, Norm(d0 + d1), col, spec, part);
        Vec3 cap[4] = {c + V3(0, 0, z1), c + d0 * r + V3(0, 0, z1), c + d1 * r + V3(0, 0, z1), c + V3(0, 0, z1)};
        AddQuad(cap, V3(0, 0, 1), col, spec, part);
    }
}

// A round section along Z from z0 (radius r0) to z1 (r1), smooth shaded, centred on (cx, cy);
// caps: 1 = a flat disc on the z0 end, 2 = on the z1 end
static void AddFrustum(float cx, float cy, float z0, float r0, float z1, float r1, int segs, Vec3 col, float spec,
                       int caps = 0)
{
    float slope = (r0 - r1) / fmaxf(fabsf(z1 - z0), 1e-4f);
    for (int i = 0; i < segs; i++) {
        float a0 = 2 * PI * i / segs, a1 = 2 * PI * (i + 1) / segs;
        Vec3 d0 = {cosf(a0), sinf(a0), 0}, d1 = {cosf(a1), sinf(a1), 0}, c = {cx, cy, 0};
        Vec3 p[4] = {c + d0 * r0 + V3(0, 0, z0), c + d1 * r0 + V3(0, 0, z0), c + d1 * r1 + V3(0, 0, z1),
                     c + d0 * r1 + V3(0, 0, z1)};
        Vec3 n0 = Norm(d0 + V3(0, 0, slope)), n1 = Norm(d1 + V3(0, 0, slope));
        const Vec3 n[4] = {n0, n1, n1, n0};
        AddQuadN(p, n, col, spec, 0);
        if (caps & 1) {
            Vec3 q[4] = {c + V3(0, 0, z0), c + d1 * r0 + V3(0, 0, z0), c + d0 * r0 + V3(0, 0, z0), c + V3(0, 0, z0)};
            AddQuad(q, V3(0, 0, -1), col, spec, 0);
        }
        if (caps & 2) {
            Vec3 q[4] = {c + V3(0, 0, z1), c + d0 * r1 + V3(0, 0, z1), c + d1 * r1 + V3(0, 0, z1), c + V3(0, 0, z1)};
            AddQuad(q, V3(0, 0, 1), col, spec, 0);
        }
    }
}

static void BuildGun()
{
    if (g_gunMesh.nv) return;
    g_mb = &g_gunMesh;
    Mat4 I = Identity();
    // colours from the game's own character palette (char_swatches)
    Vec3 metal = {0.39f, 0.42f, 0.44f}, metalLight = {0.65f, 0.67f, 0.67f}, frame = {0.26f, 0.29f, 0.31f};
    Vec3 dark = {0.16f, 0.19f, 0.22f}, polymer = {0.20f, 0.20f, 0.21f}, wood = {0.46f, 0.25f, 0.11f};
    // slide
    AddBox({0, 0.060f, 0.045f}, {0.0145f, 0.019f, 0.094f}, I, metal, 0.9f, 1);
    AddBox({0, 0.0805f, 0.045f}, {0.0100f, 0.0025f, 0.090f}, I, metalLight, 1.0f, 1);
    AddBox({0.0147f, 0.064f, 0.066f}, {0.0007f, 0.009f, 0.022f}, I, dark, 0.2f, 1);   // ejection port
    for (int s = -1; s <= 1; s += 2)
        for (int i = 0; i < 5; i++)
            AddBox({s * 0.0147f, 0.058f, -0.040f + i * 0.0055f}, {0.0006f, 0.0135f, 0.0013f}, I, dark, 0.2f, 1);
    AddBox({0, 0.0838f, 0.128f}, {0.0022f, 0.0045f, 0.0040f}, I, dark, 0.3f, 1);    // front sight
    AddBox({0, 0.0832f, -0.040f}, {0.0080f, 0.0042f, 0.0035f}, I, dark, 0.3f, 1);   // rear sight
    // frame, barrel, muzzle
    AddBox({0, 0.032f, 0.038f}, {0.0125f, 0.0115f, 0.082f}, I, frame, 0.6f, 0);
    AddCylZ({0, 0.060f, 0}, 0.0088f, 0.138f, 0.148f, 12, dark, 0.5f, 0);
    AddCylZ({0, 0.060f, 0}, 0.0045f, 0.148f, 0.1485f, 10, {0.01f, 0.01f, 0.01f}, 0, 0);
    // grip raked back, wooden panels, magazine base
    Mat4 rake = AxisAngle({1, 0, 0}, 0.27f);
    AddBox({0, -0.024f, -0.016f}, {0.0150f, 0.050f, 0.0225f}, rake, polymer, 0.2f, 0);
    for (int s = -1; s <= 1; s += 2)
        AddBox({s * 0.0157f, -0.026f, -0.017f}, {0.0012f, 0.043f, 0.0185f}, rake, wood, 0.35f, 0);
    AddBox({0, -0.074f, -0.031f}, {0.0160f, 0.0045f, 0.0240f}, rake, dark, 0.3f, 0);
    // trigger guard, trigger, hammer
    AddBox({0, 0.005f, 0.074f}, {0.0045f, 0.0165f, 0.0032f}, I, frame, 0.5f, 0);
    AddBox({0, -0.0105f, 0.046f}, {0.0045f, 0.0032f, 0.0305f}, I, frame, 0.5f, 0);
    AddBox({0, 0.009f, 0.043f}, {0.0028f, 0.0105f, 0.0028f}, AxisAngle({1, 0, 0}, -0.32f), dark, 0.4f, 0);
    AddBox({0, 0.074f, -0.055f}, {0.0045f, 0.0090f, 0.0050f}, AxisAngle({1, 0, 0}, -0.40f), dark, 0.5f, 0);
    Log("Gun model: %d vertices, %d triangles", g_gunMesh.nv, g_gunMesh.ni / 3);
}

// ------------------------------------------------------------------ rocket launcher model
// A chunky cartoon bazooka in "launcher space": origin on the tube's axis where it rests on his
// shoulder, +Z out of the muzzle, +Y up, +X to his right. Metres at Homer's size; drawn scaled
// to the character. Army green with yellow bands, a rubber shoulder pad, a trigger grip, a
// ridged sleeve for the other hand, a sight on the left, and the loaded rocket's fat red nose
// sticking out of the muzzle. Colours as in the wheel's icon.
static GV g_lv[1600];
static uint16_t g_li[2600];
static MeshBuf g_lchMesh = {g_lv, g_li, 0, 0, 1600, 2600};
static GV g_rv[700];
static uint16_t g_ri[1100];
static MeshBuf g_rktMesh = {g_rv, g_ri, 0, 0, 700, 1100};
static const float kLchMuzzleZ = 0.74f;   // the muzzle's face
static const float kLchBackZ = -0.52f;    // the flared back end's face
static const float kRktLoadedZ = 0.60f;   // a loaded rocket's origin (its nose sticks out of the muzzle)

// The rocket: +Z forward, origin in its middle. A red warhead, a grey neck, an olive motor, four
// dark fins and a nozzle. Also drawn in the launcher while it's loaded.
static void BuildRocket()
{
    if (g_rktMesh.nv) return;
    g_mb = &g_rktMesh;
    Vec3 red = {0.86f, 0.17f, 0.09f}, redL = {0.98f, 0.42f, 0.26f}, grey = {0.46f, 0.48f, 0.50f};
    Vec3 olive = {0.38f, 0.44f, 0.20f}, dark = {0.20f, 0.25f, 0.10f}, black = {0.10f, 0.10f, 0.10f};
    // warhead: a short shoulder, the bulge, then the cone to a rounded tip
    AddFrustum(0, 0, 0.100f, 0.050f, 0.130f, 0.078f, 16, red, 0.5f, 1);
    AddFrustum(0, 0, 0.130f, 0.078f, 0.205f, 0.088f, 16, red, 0.5f);
    AddFrustum(0, 0, 0.205f, 0.088f, 0.330f, 0.040f, 16, red, 0.5f);
    AddFrustum(0, 0, 0.330f, 0.040f, 0.372f, 0.016f, 16, redL, 0.6f);
    AddFrustum(0, 0, 0.372f, 0.016f, 0.380f, 0.000f, 16, redL, 0.6f);
    AddFrustum(0, 0, 0.020f, 0.042f, 0.100f, 0.042f, 12, grey, 0.8f);         // neck
    AddFrustum(0, 0, -0.300f, 0.040f, 0.020f, 0.040f, 12, olive, 0.4f);       // motor
    AddFrustum(0, 0, -0.360f, 0.030f, -0.300f, 0.040f, 12, black, 0.3f, 1);   // nozzle
    for (int i = 0; i < 4; i++) {   // fins
        Mat4 r = AxisAngle(V3(0, 0, 1), PI * 0.25f + i * PI * 0.5f);
        AddBox(XformDir(V3(0.068f, 0, -0.255f), r), V3(0.030f, 0.004f, 0.050f), r, dark, 0.3f, 0);
    }
    Log("Rocket model: %d vertices, %d triangles", g_rktMesh.nv, g_rktMesh.ni / 3);
}

static void BuildLauncher()
{
    if (g_lchMesh.nv) return;
    g_mb = &g_lchMesh;
    Mat4 I = Identity();
    Vec3 green = {0.40f, 0.50f, 0.20f}, greenD = {0.25f, 0.32f, 0.12f}, yellow = {0.98f, 0.80f, 0.10f};
    Vec3 rubber = {0.17f, 0.14f, 0.11f}, grip = {0.24f, 0.18f, 0.13f}, metal = {0.40f, 0.43f, 0.46f};
    Vec3 black = {0.05f, 0.05f, 0.05f}, glass = {0.55f, 0.80f, 1.00f};
    const int S = 18;
    // back end: a flared bell, open: a dark olive inside round a black hole (from the aiming
    // camera it's right in front of you; pure black read as a hole in the picture)
    AddFrustum(0, 0, kLchBackZ, 0.118f, -0.40f, 0.084f, S, greenD, 0.35f);
    AddFrustum(0, 0, kLchBackZ + 0.004f, 0.106f, kLchBackZ + 0.005f, 0.106f, S, V3(0.13f, 0.16f, 0.08f), 0.0f, 1);
    AddFrustum(0, 0, kLchBackZ + 0.002f, 0.058f, kLchBackZ + 0.003f, 0.058f, S, black, 0.0f, 1);
    AddFrustum(0, 0, -0.40f, 0.078f, -0.12f, 0.078f, S, green, 0.35f);
    AddFrustum(0, 0, -0.305f, 0.082f, -0.255f, 0.082f, S, yellow, 0.3f);     // band
    AddFrustum(0, 0, -0.12f, 0.090f, 0.06f, 0.090f, S, rubber, 0.15f, 3);     // shoulder pad
    AddFrustum(0, 0, 0.06f, 0.078f, 0.30f, 0.078f, S, green, 0.35f);
    for (int i = 0; i < 4; i++) {   // the other hand's sleeve: ridges
        AddFrustum(0, 0, 0.30f + i * 0.08f, 0.084f, 0.34f + i * 0.08f, 0.084f, S, greenD, 0.25f, 3);
        AddFrustum(0, 0, 0.34f + i * 0.08f, 0.080f, 0.38f + i * 0.08f, 0.080f, S, greenD, 0.25f);
    }
    AddFrustum(0, 0, 0.62f, 0.082f, 0.66f, 0.082f, S, yellow, 0.3f);           // band
    AddFrustum(0, 0, 0.66f, 0.094f, kLchMuzzleZ, 0.094f, S, greenD, 0.4f, 3);  // muzzle ring
    AddFrustum(0, 0, kLchMuzzleZ + 0.001f, 0.070f, kLchMuzzleZ + 0.002f, 0.070f, S, black, 0.0f, 2);   // the bore
    // trigger grip under the tube, raked back, on a mount
    AddBox({0, -0.090f, 0.36f}, {0.030f, 0.016f, 0.075f}, I, metal, 0.6f, 0);
    Mat4 rake = AxisAngle({1, 0, 0}, 0.25f);
    AddBox({0, -0.150f, 0.335f}, {0.024f, 0.062f, 0.032f}, rake, grip, 0.2f, 0);
    AddBox({0, -0.122f, 0.405f}, {0.006f, 0.022f, 0.005f}, AxisAngle({1, 0, 0}, -0.3f), metal, 0.5f, 0);   // trigger
    AddBox({0, -0.150f, 0.425f}, {0.007f, 0.004f, 0.030f}, I, metal, 0.5f, 0);                            // guard
    AddBox({0, -0.128f, 0.452f}, {0.007f, 0.026f, 0.004f}, I, metal, 0.5f, 0);
    // sight on the left, with a blue lens at the back
    AddBox({-0.112f, 0.040f, 0.120f}, {0.024f, 0.030f, 0.055f}, I, metal, 0.7f, 0);
    AddBox({-0.112f, 0.040f, 0.064f}, {0.016f, 0.020f, 0.002f}, I, glass, 1.0f, 0);
    AddBox({-0.090f, 0.010f, 0.120f}, {0.010f, 0.012f, 0.030f}, I, metal, 0.6f, 0);   // its bracket
    Log("Launcher model: %d vertices, %d triangles", g_lchMesh.nv, g_lchMesh.ni / 3);
}

// ------------------------------------------------------------------ state
struct Trig {
    float rest;
    bool init;
};

struct Flash { Vec3 p, dir; float t, rot, size, jet; uint32_t veh; Vec3 lp, ld; };   // jet: length of the flame jet (1 = the pistol's);
                                                                                    // veh: (drive-by) rides that car, lp / ld in its frame
struct Smoke { Vec3 p, v; float t, life, rot, size; };
struct Tracer { Vec3 p, dir; float t, dist, maxDist; };
struct Shell { Vec3 p, v, axis; float ang, spin, t, ground; int kind; bool landed; };   // kind 0 a pistol's brass, 1 a shotgun shell
struct Spark { Vec3 p; float t, life, rot, size; };
struct Chip { Vec3 p, v; float t, life, rot, spin, size; Vec3 col; };
struct Puff { Vec3 p; float t, life, size, rot; Vec3 col; };

static struct {
    int weapon;          // the weapon picked on the wheel (WPN_*; WPN_NONE = fists)
    int held;            // the one in his hand (drawn and posed): changes over once the old one is away
    float show;          // weapon in hand 0..1
    float popT;          // time since equipped (pop animation)
    float aim;           // arm-up pose blend 0..1 (aiming or a quick hip shot)
    float camAim;        // over-the-shoulder camera blend 0..1 (aiming only)
    bool aiming;
    float hipTimer;
    bool pendingShot;
    Spring aimS, camS;   // arm-up / camera blends (critically damped springs)
    Spring reach;        // aim-pose weight: 0 while the target is out of his reach
    float reachTarget;   // set by BuildPose, stepped once a frame in Gun_Update
    Spring kickWrist, kickArm, kickElbow, kickChest;   // body recoil
    Spring carryW;       // low-ready carry pose weight (less while walking / running)
    Spring knockW;       // how far he is knocked over (0 upright, 1 down): the carry lets go, the launcher rolls with him
    float speed;         // his ground speed (m/s), smoothed
    Vec3 vel;            // his ground velocity (m/s), smoothed the same way
    Spring legYaw;       // walking while aiming: his legs turned toward where he walks (radians from his facing)
    Spring legBack;      // ... and stepping backwards (1: the walk cycle mirrored front to back)
    bool legBackMode;    // walking away from the aim: the legs step backwards
    float legW;          // how much of the leg layer is on this frame (BuildPose reads it)
    float slide;
    float cooldown;
    float spread;
    bool rtPrev;
    Trig lt, rt;
    Vec3 aimPoint;       // world
    bool haveAim;
    Mat4 gunWorld;
    bool haveGun;
    float pitch;         // aiming camera pitch (radians, + = up)
    float yaw;           // aiming heading (radians, atan2(x, z)); right stick turns it
    float yawVel, pitchVel;   // right stick, ramped
    float bodyYaw;       // where his body actually faces (root joint), same convention as yaw
    bool haveBody, bodyAligned, knocked;
    float feetY;         // damped feet height for the aiming camera
    double lastCamT;
    float fireQueued;    // a press during the cooldown fires when it ends
    bool aimLatch;       // after the wheel: wait for the aim button to be let go
    Vec3 lastPos;
    bool haveLastPos;
    double lastPosT;     // real time of lastPos (dt is clamped to 0.1 s; a hitch is longer)
    float suspend;       // knocked down / falling / teleported: no aiming for a moment
    Spring kickPitch, kickYaw, fovPunch, xhKick;
    Spring lchBack, lchFlip;   // rocket launcher recoil: shoved back along its tube (m), muzzle up (rad)
    bool rpgLoaded;            // a rocket in the launcher (its nose shows)
    float reloadT;             // seconds since the new rocket went in (its pop)
    Mat4 lchWorld;             // the launcher as last drawn (scaled), for the muzzle
    bool haveLch;
    float hitDist;       // depth probe: metres to the world under the crosshair, -1 none
    float hitAge;        // seconds since that was measured
    Vec3 camPos, camFwd; // the aiming camera's centre ray (for the probe's hit point)
    float rigDist;       // camera to pivot along the view
    float camSideK;      // share of the shoulder offset the walls allow (eased back out)
    float camFree;       // how far behind the pivot the walls let the camera go (eased back out)
    Vec3 shotPoint;      // where a shot fired now lands
    bool shotHits;       // ... on something solid (not the sky)
    bool forceAim;       // debug
    int forceFire;       // debug
    Flash flash[6];
    int nflash;
    Smoke smoke[64];
    int nsmoke;
    Tracer tracers[24];
    int ntracers;
    Shell shells[16];
    int nshells;
    Spark sparks[16];
    int nsparks;
    Chip chips[64];
    int nchips;
    Puff puffs[24];
    int npuffs;
    // (shotgun.inc) the pump and the recoil of a shouldered gun
    float sgPumpT;             // seconds into racking the pump (< 0: waiting for the recoil to settle; >= 1 none)
    bool sgEject;              // this rack throws out a spent shell
    bool sgHolster;            // putting it away (back into the pocket)
    Spring sgBack, sgFlip, sgTwist;   // shoved back into his shoulder (m), muzzle up (rad), right shoulder back (rad)
    Spring sgSettle;           // the raise landing: the muzzle a little past the line and back (rad)
    Spring sgWall, sgWallA;    // a wall or a car where the gun would be: 0..1 - carried (its muzzle up) / aimed (lowered out of the aim)
    Mat4 sgRayL, sgRayA;       // the shotgun carried / aimed as it would be with no wall (scaled): the rays go along these
    bool sgRayAOn;
    bool sgRaising;            // the gun is on its way up (or held up), not down (the pose orders the raise and the lowering)
    Mat4 sgWorld;              // the shotgun as last drawn (scaled)
    bool haveSg;
    uint32_t rng;
} G;

static uint32_t g_player;
static int g_logGun;
static int g_actLog;   // debug "actlog <frames>": log his action whenever it changes
static bool g_logGunDrawn;   // the handgun counted "glog" down itself this frame
static float g_ground;

void Gun_EyeReset();   // (face.inc)
void Gun_SetPlayer(uint32_t player)
{
    if (player != g_player && player) Gun_EyeReset();   // (a new level or character: his eye piece and textures are found again)
    g_player = player;
    Decal_Track(player);
}
bool Gun_IsEquipped() { return G.weapon != WPN_NONE; }
static float DaCamAim();   // (diagnostic, driveby-m3)
bool Gun_Aiming() { return G.aiming || G.camAim > 0.0f || DaCamAim() > 0.0f; }
int Gun_Weapon() { return G.weapon; }
static const char* const kWeaponNames[WPN_COUNT + 2] = {"fists", "handgun", "rpg", "shotgun", "donut", "beer"};
static Vec3 g_lastHand;      // wrist position of the last posed frame
static bool g_haveLastHand;

static float Rnd()
{
    G.rng = G.rng * 1664525u + 1013904223u;
    return (float)((G.rng >> 8) & 0xFFFF) / 65535.0f;
}

// Whether the player is one of the kids - Bart or Lisa - by his skeleton's head at bind (0.84 / 0.82 m up, against Homer's 1.33, Marge's
// and Apu's more; a costume keeps its character's skeleton). Read from the game each time the player or his skeleton changes, so it is
// right from a level's first frame, before anything is held (the posing's own cache can still be the last character's).
static bool PlayerIsKid()
{
    static uint32_t s_player, s_skel;
    static bool s_kid;
    if (!g_player) return false;
    uint32_t puppet = 0, eng = 0, pe = 0, pose = 0, skel = 0, arr = 0;
    SIZE_T got;
    HANDLE me = GetCurrentProcess();
    if (!ReadProcessMemory(me, (LPCVOID)(uintptr_t)(g_player + 0x108), &puppet, 4, &got) || !puppet) return s_kid;
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(puppet + 0x10), &eng, 4, &got);
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(eng + 0xC), &pe, 4, &got);
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(pe + 0xC), &pose, 4, &got);
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(pose + 0x8), &skel, 4, &got);
    if (!pose || !skel) return s_kid;
    if (skel == s_skel && g_player == s_player) return s_kid;
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(skel + 0x18), &arr, 4, &got);
    Mat4 rest;
    if (!arr || !ReadProcessMemory(me, (LPCVOID)(uintptr_t)(arr + 17 * 0xF0 + 0x50), rest.m, 64, &got)) return s_kid;
    s_player = g_player, s_skel = skel;
    s_kid = rest.m[13] > 0.2f && rest.m[13] < 1.05f;
    Log("player: head at %.2f m at bind -> %s", rest.m[13], s_kid ? "one of the kids (no beer)" : "a grown-up");
    return s_kid;
}
// (no beer for Bart or Lisa - decided 2026-10-15, "for now": this one switch puts it back)
static const bool kBeerForKids = false;
bool Gun_BeerAllowed() { return kBeerForKids || !PlayerIsKid(); }

void Gun_Equip(int weapon)
{
    if (weapon < WPN_NONE || weapon > WPN_BEER || weapon == G.weapon) return;
    if (weapon == WPN_BEER && !Gun_BeerAllowed()) {
        Log("beer: not for the kids");
        return;
    }
    bool equip = weapon != WPN_NONE;
    G.weapon = weapon;
    // (the shotgun's draw and holster have their own sounds, and the donut's eating)
    if (weapon != WPN_SHOTGUN && weapon != WPN_DONUT && weapon != WPN_BEER &&
        !(weapon == WPN_NONE && (G.held == WPN_SHOTGUN || G.held == WPN_DONUT || G.held == WPN_BEER)))
        Sound_Play(equip ? SND_EQUIP : SND_HOLSTER, 1.0f, weapon == WPN_RPG ? 0.8f : 1.0f);   // the launcher is heavier
    if (equip && G.show <= 0.0f) {   // else it comes out once the one in his hand is put away (Gun_Update)
        G.held = weapon;
        G.popT = 0;
        G.cooldown = 0;   // one weapon's wait (the RPG's reload) doesn't hold up the next
    }
    // a little cartoon "poof" of sparkles at the hand
    if (g_haveLastHand || G.haveGun) {
        Vec3 c = equip || !G.haveGun ? g_lastHand : XformPoint(V3(0, 0.03f, 0.04f), G.gunWorld);
        for (int i = 0; i < 6 && G.nsparks < 16; i++) {
            Spark& s = G.sparks[G.nsparks++];
            s.p = c + V3(Rnd() - 0.5f, Rnd() - 0.3f, Rnd() - 0.5f) * 0.35f;
            s.t = -i * 0.03f;
            s.life = 0.35f + Rnd() * 0.2f;
            s.rot = Rnd() * PI;
            s.size = 0.35f + Rnd() * 0.3f;
        }
    }
    Log("Weapon: %s", kWeaponNames[weapon]);
}

// Triggers. Some pads (like this user's) report a trigger backwards: resting at ~8%,
// dropping to 0 when squeezed. Treat any clear move away from the resting value as a press.
static bool TrigDown(Trig& t, BYTE raw, bool connected)
{
    if (!connected) {
        t.init = false;
        return false;
    }
    float v = raw;
    if (!t.init) {   // (squeezed when the pad connects: rest is still released, not "held forever")
        t.rest = v > 40.0f ? 0.0f : v;
        t.init = true;
    }
    float d = fabsf(v - t.rest);
    bool down = t.rest < 6.0f ? v > 40.0f : d > 9.0f;
    if (!down && d < 3.0f) t.rest += (v - t.rest) * 0.02f;   // slow drift only
    return down;
}

// finger curl tuning (radians); signs found by testing in game
// The thumb bends about its local Z (its X runs along the bone, so turning about X only
// twisted it into a thumbs-up over the slide); negative tucks it down beside the grip.
static float kCurl1 = 1.25f, kCurl2 = 1.05f, kThumb = -1.0f, kThumb2 = -0.6f;
static Vec3 kGrip = {-0.100f, -0.035f, 0.0f};   // grip centre in wrist space (-X fingers, -Y thumb side)
static int kThumbAxis = 2;   // local axis the thumb curls about (0 X, 1 Y, 2 Z)

// aiming pose tuning (radians unless noted): chest twist / lean share of the aim, how far
// the arm may reach relative to the chest before the pose fades back, IK and wrist shape
static float kBlade = -0.25f, kTwistMax = 0.70f, kTwist1 = 0.35f;
static float kLean = 0.35f, kLeanLo = -0.30f, kLeanHi = 0.22f;
static float kArmYawLo = -0.60f, kArmYawHi = 1.20f, kArmPitchLo = -1.00f, kArmPitchHi = 1.10f;
static float kOverFade = 0.50f, kReach = 0.985f, kPoleOut = 0.25f, kCant = 0.25f, kWristCone = 0.70f;
static float kHeadYaw = 0.80f, kHeadPitch = 0.60f;
static bool g_torsoOn = true;   // debug "torso <0|1>": spine/head/left arm layer

// carrying the gun when not aiming ("low ready"): upper arm hanging down, a little out past
// his belly and forward; forearm angled forward and down; barrel pointing forward-down
static float kCarryUpOut = 0.32f, kCarryUpFwd = 0.06f;                  // upper arm (down is 1)
static float kCarryForeFwd = 0.85f, kCarryForeDown = 0.50f, kCarryForeOut = 0.18f;
static float kCarryGunDown = 0.55f, kCarryGunOut = 0.10f;               // barrel: radians below level, outward
static float kCarryCant = -0.15f;                                       // roll of the gun top (+ = inward)

// Over-the-shoulder aiming camera. A pivot beside the head (side / height above the feet)
// and a distance back along the view, both depending on pitch: closer and lower when
// looking up, a little higher and closer when looking down.
static const float kPitchDown = -0.55f, kPitchUp = 0.65f;
static const float kSoftLead = 0.40f, kMaxLead = 0.75f;   // how far the view may lead his body
struct RigKey {
    float pitch, dist, side, height;
};
static RigKey kRig[3] = {{kPitchDown, 2.05f, 0.70f, 1.62f}, {0.00f, 2.55f, 0.78f, 1.50f}, {kPitchUp, 1.90f, 0.72f, 1.38f}};
static float kAimZoom = 0.14f;   // narrower field of view while aiming

static RigKey RigAt(float p)
{
    const RigKey& a = kRig[p < 0 ? 0 : 1];
    const RigKey& b = kRig[p < 0 ? 1 : 2];
    float t = Smooth01((p - a.pitch) / (b.pitch - a.pitch));
    return {p, a.dist + (b.dist - a.dist) * t, a.side + (b.side - a.side) * t, a.height + (b.height - a.height) * t};
}

// debug "orbit <deg>,<dist>,<fov deg>": look at the aiming arm from another side
static float g_orbitDeg, g_orbitDist, g_orbitFov = 30.0f;
// (lab "wstick") the heading the left stick moves him by: the camera's the game walks him by - the debug orbit camera's while it is
// on (the game reads the camera we drew with, aiming or not: an orbit round him turned the stick's forward with him, and "lstick"
// ran him in circles), the aim's while he walks aiming (the aim camera looks along it)
static float g_walkCamHead;
static bool g_aimWalking;
float Gun_StickHeading()
{
    if (g_orbitDist > 0.1f) return g_walkCamHead;
    if (g_aimWalking) return G.yaw;
    float gw[16], fov;
    return Game_GetGameCamera(gw, &fov) ? atan2f(gw[8], gw[10]) : G.yaw;
}
static float g_orbitBody = -1.0f;   // debug "orbitbody <height>": look at him at that height above his feet, not at his hand (-1)
static int g_orbitJoint = -1;       // debug "orbitjoint <joint>": look at that joint of his posed skeleton (-1 off; 21 his left wrist)
static float g_orbitUp = 0.35f;     // debug "orbitup <m>": the orbit camera this far above what it looks at
static float g_orbitYaw = -999.0f;  // debug "orbityaw <deg>": the orbit's angles from this world heading, not his (-999: his)
static bool SkinJointPos(int j, Vec3* out);   // (below: a joint of his posed skeleton, world space)

// Walls. The aiming camera keeps out of them the way the game's follow camera does: a small
// sphere is swept with the game's own camera line test (Game_CameraSweep), from his head out
// to the shoulder pivot and from the pivot back to the camera. Something in the way brings
// the pivot / camera in at once; when it's clear again they ease back out.
static bool g_camColl = true;   // debug "camcoll <0|1>"
static int g_camLog;            // debug "camlog <frames>"
static const float kCamRadius = 0.25f;   // sweep sphere
static const float kCamMinDist = 0.45f;  // closest the camera comes behind the pivot (the near plane comes in while aiming)

static void EaseOpen(float& v, float want, float dt, bool reset)
{
    if (reset || want < v) v = want;   // in at once
    else v += (want - v) * (1.0f - expf(-dt * 3.5f));
}

// (shotgun.inc) the aim camera with the shotgun: further out to his right and a little lower than the other weapons' (x side, +m, +m),
// so the gun shows past his right shoulder, which otherwise hides it
static float kSgCamSide = 1.25f, kSgCamUp = -0.08f, kSgCamBack = 0.0f;
// the full aiming camera (camera->world) for the current yaw / pitch (+ recoil kick)
static Mat4 AimCameraWorld(Vec3 feet, float dt)
{
    float yaw = G.yaw + G.kickYaw.x, pitch = G.pitch + G.kickPitch.x;
    Vec3 look = {sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)};
    Vec3 right = Norm(Cross(UP, look));
    Vec3 camUp = Cross(look, right);
    RigKey k = RigAt(G.pitch);
    if (G.held == WPN_SHOTGUN) k.side *= kSgCamSide, k.height += kSgCamUp, k.dist += kSgCamBack;   // (shouldered at his eye: his head mustn't hide it)
    Vec3 head = feet + UP * k.height;
    bool reset = G.camAim < 0.01f;   // a new aim starts from what the walls allow right now
    Vec3 shoulder = head + right * k.side;
    float s = g_camColl ? Game_CameraSweep(&head.x, &shoulder.x, kCamRadius, nullptr) : 1.0f;
    EaseOpen(G.camSideK, s < 0 ? 1.0f : s, dt, reset);
    Vec3 pivot = head + right * (k.side * G.camSideK);
    float dist = k.dist;
    if (look.y > 0.01f) dist = fminf(dist, (pivot.y - feet.y - 0.30f) / look.y);   // slide in, stay on the view line
    dist = fmaxf(dist, 0.90f);
    float cov = 0, t = 1.0f;
    Vec3 back = pivot - look * dist;
    if (g_camColl) t = Game_CameraSweep(&pivot.x, &back.x, kCamRadius, &cov);
    float want = t < 0 ? dist : fmaxf(fminf(dist, kCamMinDist), dist * t);
    EaseOpen(G.camFree, want, dt, reset);
    if (g_camLog > 0) {
        g_camLog--;
        Log("camcoll: side %.2f -> %.2f, back %.2f -> %.2f of %.2f m (list reaches %.2f m)", s, G.camSideK, t,
            G.camFree, dist, cov);
    }
    // squeezed in close, the shoulder offset tightens too, or he would slide out of the picture
    float full = dist;
    dist = fminf(dist, G.camFree);
    if (dist < full) {
        float tight = 0.6f + 0.4f * Clamp01((dist - kCamMinDist) / fmaxf(full - kCamMinDist, 0.1f));
        pivot = head + right * (k.side * G.camSideK * tight);
    }
    G.rigDist = dist + Dot(pivot - head, look);
    return WithTrans(Basis(right, camUp, look), pivot - look * dist);
}

static bool PlayerPos(Vec3* out)
{
    float p[3];
    if (!g_player || !Game_PlayerPosition(g_player, p)) return false;
    *out = V3(p[0], p[1], p[2]);
    return true;
}

static int g_syncLog;   // debug "synclog <frames>": where he is (+0x64) against his drawn skeleton, per frame
static bool DrvCamera();   // (diagnostic)
static bool DaCamera();    // (diagnostic, driveby-m3: driveby_aim.inc)
static float DaCamAim();
static bool Drunk_Active();
static void Drunk_Camera(Mat4& world, float* fov);
static bool Ragdoll_Pelvis(Vec3* p);
static bool DrunkBody_Pelvis(Vec3* p);
static bool DrunkBody_On();
static float kDrCam = 1.0f;   // (drunk.inc: the camera's lean and drift, 0 off)
static int g_camDistLog;   // debug "camdist <frames>"
void Gun_PreRenderCamera()
{
    {
        double t0 = ProfNow();
        bool da = DaCamera();
        g_daProf[0] += ProfNow() - t0;
        if (da) return;
    }
    if (DrvCamera()) return;
    Vec3 pos;
    if (!g_player || !PlayerPos(&pos)) return;
    if (g_syncLog > 0) {
        g_syncLog--;
        PoserPose Q;
        if (Game_ReadPoser(g_player, &Q))
            Log("sync: begin pos (%.3f %.3f) root (%.3f %.3f) joint3 (%.3f %.3f)", pos.x, pos.z, Q.world[0][12], Q.world[0][14],
                Q.world[3][12], Q.world[3][14]);
    }
    double now = Time_Real();
    float cdt = G.lastCamT > 0 ? (float)(now - G.lastCamT) : 0.016f;
    G.lastCamT = now;
    if (cdt > 0.1f) cdt = 0.1f;
    // feet height, damped while aiming so steps and bumps don't jerk the view
    if (G.camAim < 0.01f || fabsf(pos.y - G.feetY) > 2.0f)
        G.feetY = pos.y;
    else
        G.feetY += (pos.y - G.feetY) * (1.0f - expf(-cdt * (pos.y > G.feetY ? 7.0f : 14.0f)));
    Mat4 aimCam = AimCameraWorld(V3(pos.x, G.feetY, pos.z), cdt);

    // where the gun points: straight out of the crosshair while aiming, else level ahead of
    // his body for a quick shot from the hip
    if (G.camAim > 0.001f) {
        // the arm converges on what is under the crosshair (not closer than a little past
        // his shoulder, or it would bend inward); shots land exactly there
        Vec3 cp = Row(aimCam, 3), cf = Row(aimCam, 2);
        bool probed = G.hitDist > 0 && G.hitAge < 0.25f;
        float armD = probed ? fmaxf(G.hitDist, G.rigDist + 2.5f) : 30.0f;
        G.aimPoint = cp + cf * fminf(armD, 60.0f);
        G.camPos = cp;
        G.camFwd = cf;
        G.shotHits = probed;
        G.shotPoint = cp + cf * (probed ? G.hitDist : 60.0f);
    } else {
        float hy = G.haveBody ? G.bodyYaw : G.yaw;
        G.aimPoint = pos + UP * 1.25f + V3(sinf(hy), 0, cosf(hy)) * 30.0f;
        G.shotHits = false;
        G.shotPoint = G.aimPoint;
    }
    G.haveAim = true;

    bool orbit = g_orbitDist > 0.1f;   // (debug: on until "orbit 0,0,0" - with no weapon out too, so a draw or a holster is filmed in one shot)
    bool drunkCam = Drunk_Active() && kDrCam > 0.0f;   // (drunk: the camera leans and drifts with him - drunk.inc)
    if (G.camAim <= 0.001f && !orbit && !drunkCam) return;
    float gw[16], fov;
    if (!Game_GetGameCamera(gw, &fov)) return;
    if (g_camDistLog > 0) {
        g_camDistLog--;
        Log("camdist: %.2f m from him (eye %.1f %.1f %.1f)", Len(V3(gw[12], gw[13], gw[14]) - pos), gw[12], gw[13], gw[14]);
    }
    // blend from the game's camera to ours
    Mat4 W0 = ToMat(gw);
    float a = G.camAim * G.camAim * (3 - 2 * G.camAim);
    Mat4 world = WithTrans(BlendRot(Rot3(W0), Rot3(aimCam), a), Lerp(Row(W0, 3), Row(aimCam, 3), a));
    float f = fov * (1.0f - kAimZoom * a) * (1.0f + G.fovPunch.x * a);
    if (orbit) {
        Vec3 tgt = g_orbitBody >= 0.0f ? pos + UP * g_orbitBody : g_haveLastHand ? g_lastHand : pos + UP * 1.2f;
        Vec3 rp;
        if (g_orbitBody >= 0.0f && (Ragdoll_Pelvis(&rp) || DrunkBody_Pelvis(&rp))) {   // (the body we draw: no jump as he's moved to stand)
            // (lower as he goes down: by how low his pelvis is - a step from 1.2 to 0.72 m the moment the drunk body took him, standing,
            // swung the camera mid-drink)
            float down = Clamp01((0.50f - (rp.y - pos.y)) / 0.30f);
            tgt = V3(rp.x, pos.y + g_orbitBody * (1.0f - 0.4f * down * down * (3.0f - 2.0f * down)), rp.z);
        }
        Vec3 jp;
        if (g_orbitJoint >= 0 && SkinJointPos(g_orbitJoint, &jp)) tgt = jp;
        // round his body, aiming too (an aim starts with its yaw set to the game camera's, and an orbit round the aim swung away
        // for a few frames until his body had turned to it)
        float oy = g_orbitYaw > -900.0f ? g_orbitYaw * PI / 180.0f : G.haveBody ? G.bodyYaw : G.yaw;
        Vec3 back = Norm(V3(-sinf(oy), 0, -cosf(oy)));
        Vec3 dir = XformDir(back, AxisAngle(UP, g_orbitDeg * PI / 180.0f));
        Vec3 cp = tgt + dir * g_orbitDist + UP * g_orbitUp;
        Vec3 fw = Norm(tgt - cp), rr = Norm(Cross(UP, fw)), uu = Cross(fw, rr);
        world = WithTrans(Basis(rr, uu, fw), cp);
        f = g_orbitFov * PI / 180.0f;
        g_walkCamHead = atan2f(fw.x, fw.z);
    }
    if (drunkCam && !orbit) Drunk_Camera(world, &f);
    Game_OverrideCamera(world.m, f, orbit ? 0.0f : a, orbit || a > 0.001f);   // (v1.0: drunk alone, the game's place kept)
}

// Skin override. The game uploads each bone of a skinned mesh to vertex shader
// constants c17+ as (inverse bind pose * joint world * view), 3 registers per bone, at
// draw time - and computes them from data we cannot usefully change in time. So we
// recognise the player's right-arm bones as they are uploaded (by comparing with the
// expected matrix for every joint) and replace them with our pose.
static struct {
    bool active;
    int n;
    Mat4 expect[40];   // invBind * W * V for every joint (as the game will upload it)
    Mat4 repl[40];     // replacement palette entries (posed joints and everything below them)
    bool hasRepl[40];
    Mat4 hand;         // our wrist world matrix (the gun follows it)
    Mat4 world[40];    // the posed skeleton (world space)
    bool haveHand;
    Mat4 launcher;     // the rocket launcher's frame (unscaled) on his shoulder, this build
    float lchScale;    // its size for this character
    bool haveLauncher;
    float handK;       // his hand's size against Homer's (the grip offset scales with it)
} g_skin;
static bool SkinJointPos(int j, Vec3* out)
{
    if (j < 0 || j >= g_skin.n) return false;
    *out = Row(g_skin.world[j], 3);
    return true;
}
// the grip centre in his wrist space: Homer's, scaled to his hand. For the pistol a smaller fist
// (Bart, Lisa) also has the gun a little further into the palm, and a smaller gun (PistolScale):
// Homer's pistol was thicker than their whole fist, and over the right shoulder their hand
// vanished inside its grip. (The RPG's trigger hand only scales; its launcher is sized already.)
static Vec3 GripInWrist(bool pistol)
{
    float k = g_skin.handK > 0 ? g_skin.handK : 1.0f;
    Vec3 g = kGrip * k;
    if (pistol) g.z += 0.027f * (1.0f - fminf(k, 1.0f));   // +Z: towards the palm (1.3 cm for Bart and Lisa)
    return g;
}
// the pistol's size for his hand: Homer, Marge, Apu as before; Bart and Lisa 70 %
static float PistolScale()
{
    float k = g_skin.handK > 0 ? g_skin.handK : 1.0f;
    return 1.0f - 0.625f * (1.0f - fminf(k, 1.0f));
}
// joint parents (the shared character skeleton; refreshed from the drawn skeleton)
static int g_parent[40] = {0, 0, 0, 2, 3, 4, 5, 6, 7, 4, 9, 10, 11, 4, 3, 14, 15, 16, 17,
                           15, 19, 20, 21, 22, 21, 24, 15, 26, 27, 28, 29, 28, 31};
static bool g_torsoOK = true;   // false: the drawn model has joints under Spine_1 the pose lacks
static Mat4 g_invBind[40];
static int g_skinTest;   // debug: 1 = replace with the expected matrix (no visible change)
static bool g_skinOff;   // debug "skinoff <1|0>": the matcher ignores him, as when nothing matches
static int g_skinHits[40];
static float g_skinView[16];        // the camera view the expected matrices were built with
static bool g_viewRefresh = true;   // debug "viewrefresh <0|1>"
// Which joint each palette slot holds, per piece of his mesh. The game uploads one bone
// per call and draws him in several pieces, each with its own palette, so a slot number
// only means something together with its piece (keyed by the draw call). Two joints can
// upload identical matrices (a child sitting exactly as in the bind pose relative to its
// parent: a relaxed finger, a straight elbow); the map, learned whenever exactly one joint
// fits a slot, settles those.
struct SlotMap {
    uint32_t key;
    int age;                  // 0 = free
    signed char joint[32];    // slot -> joint + 1 (0 = unknown)
};
static SlotMap g_maps[48];
static int g_mapClock;
static int g_mapLearned;      // slots learned (debug)
static int g_skinRepl, g_skinGuess;   // slots replaced this frame / of them by a guess (debug)
static int g_skinLog;            // debug "skinlog <frames>"
static int g_slotDump;           // debug "slotdump <frames>": every candidate joint of every slot of his
static float g_dbgYawRate;       // debug "yawrate <rad/s>": turn the aim like a held stick
static float g_dbgCamSpin;       // debug "camspin <rad/s>": turn the game's follow camera
static int g_dbgAimAt;           // debug "aimat npc|car|traffic|<x>,<y>,<z>|off": keep the aim on the nearest character / vehicle / a point
static float g_dbgAimMin = 1.5f;  // "aimat npc:<metres>": the nearest character at least this far
static int g_dbgAimChar;          // "aimat char:<n>": character n of the game's list, wherever he goes
static char g_dbgAimCar[24];     // "aimat car:<name>": the nearest vehicle whose name starts with this
static Vec3 g_dbgAimPoint;
static bool g_dbgHoldFx;         // debug "fxhold <0|1>": muzzle flashes stay on screen
static float g_fpsLeft;          // debug "fps"
static int g_fpsFrames;
static float g_fpsT;
static float DaCamAim();
static int g_skinRefreshes;      // pose re-reads that changed something this frame (debug)
static int g_skinNear;           // skinned draws of his body this frame (bones within 1.5 m of his joints)
static int g_buildLog, g_buildFrame;   // debug "buildlog <frames>": every pose build's inputs and result
static const char* g_buildWhy = "pre";
static float g_skinBest[40];     // closest match error per joint this frame (while logging)
static uint32_t g_invBindSkel;

static bool LoadInvBind(int n)
{
    uint32_t puppet = 0, eng = 0, pe = 0, pose = 0, skel = 0, arr = 0;
    SIZE_T got;
    HANDLE me = GetCurrentProcess();
    if (!ReadProcessMemory(me, (LPCVOID)(uintptr_t)(g_player + 0x108), &puppet, 4, &got) || !puppet) return false;
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(puppet + 0x10), &eng, 4, &got);
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(eng + 0xC), &pe, 4, &got);
    // his poser's skeleton (the one he is drawn with). Not the PoseEngine's tPose (+0x8): all
    // characters share that one and between draws it holds whoever was drawn last - reading
    // it gave us Marge's or a pedestrian's bind pose and the arm/hand flickered back
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(pe + 0xC), &pose, 4, &got);
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(pose + 0x8), &skel, 4, &got);
    if (!pose || !skel) return false;
    ReadProcessMemory(me, (LPCVOID)(uintptr_t)(skel + 0x18), &arr, 4, &got);
    // a level or costume change can put a different skeleton at the same address: key on
    // the player, the array, the joint counts and every joint's bind matrix as well (outfits can
    // differ only in the head, jaw or hair - Prison Marge, Ninja Bart - so one joint wasn't enough)
    static uint32_t s_player, s_arr, s_key;
    static int s_drawn, s_n;
    static uint8_t s_joints[64 * 0xF0];
    int drawn = 0;
    if (!arr || !ReadProcessMemory(me, (LPCVOID)(uintptr_t)(skel + 0x14), &drawn, 4, &got) ||
        drawn < 33 || n < 33 ||   // every character model has 33 (the hand ends at 32): else not posable
        !ReadProcessMemory(me, (LPCVOID)(uintptr_t)arr, s_joints, (SIZE_T)(drawn < 64 ? drawn : 64) * 0xF0, &got))
        return false;
    uint32_t key = 2166136261u;
    for (int i = 0; i < drawn && i < 64; i++)
        for (int b = 0; b < 64; b++) key = (key ^ s_joints[i * 0xF0 + 0x50 + b]) * 16777619u;
    if (skel == g_invBindSkel && arr == s_arr && g_player == s_player && drawn == s_drawn && n == s_n && key == s_key)
        return true;
    for (int i = 0; i < n && i < 40; i++) {
        Mat4 rest;
        if (!ReadProcessMemory(me, (LPCVOID)(uintptr_t)(arr + i * 0xF0 + 0x50), rest.m, 64, &got)) return false;
        g_invBind[i] = FullInverse(rest);
    }
    g_invBindSkel = skel;
    s_arr = arr;
    s_player = g_player;
    s_drawn = drawn;
    s_n = n;
    s_key = key;
    memset(g_maps, 0, sizeof(g_maps));
    // parents from the skeleton; extra joints hanging under Spine_1 that the pose doesn't
    // carry can't follow a twisted chest: no torso layer then (kept as a guard)
    g_torsoOK = true;
    for (int i = 1; i < drawn && i < 64; i++) {
        int par = -1;
        ReadProcessMemory(me, (LPCVOID)(uintptr_t)(arr + i * 0xF0 + 0x8), &par, 4, &got);
        if (par < 0 || par >= i) par = i < 33 ? g_parent[i] : 0;   // expect an index; else keep the table
        if (i < 40) g_parent[i] = par;
        if (i >= n)
            for (int k = par; k > 0; k = k < 40 ? g_parent[k] : 0)
                if (k == 14) g_torsoOK = false;
    }
    Log("Skeleton %08X: %d joints drawn, pose %d, torso layer %s", skel, drawn, n, g_torsoOK ? "on" : "off");
    return true;
}

// once per frame: report last frame's matches (debug) and reset the counters
static void SkinFrameStats()
{
    if (g_logGun > 0) {
        char buf[256];
        int n = 0;
        for (int j = 0; j < 40; j++)
            if (g_skinHits[j]) n += snprintf(buf + n, sizeof(buf) - n, " %d:%d", j, g_skinHits[j]);
        buf[n] = 0;
        Log("skin bones matched last frame:%s", buf);
    }
    if (g_skinLog > 0) {
        g_skinLog--;
        Log("skin: arm hits 26:%d 27:%d 28:%d 29:%d | closest 26 %.3f 27 %.3f 28 %.3f | spine15 hits %d | refreshes %d | slots learned %d | replaced %d (%d guessed) | body draws %d",
            g_skinHits[26], g_skinHits[27], g_skinHits[28], g_skinHits[29], g_skinBest[26], g_skinBest[27], g_skinBest[28],
            g_skinHits[15], g_skinRefreshes, g_mapLearned, g_skinRepl, g_skinGuess, g_skinNear);
    }
    if (g_skinLog > 0 || g_logGun > 0) {
        char miss[160];
        int m = 0;
        miss[0] = 0;
        for (int j = 14; j <= 32 && j < g_skin.n; j++)
            if (g_skin.hasRepl[j] && !g_skinHits[j]) m += snprintf(miss + m, sizeof(miss) - m, " %d", j);
        if (m) Log("skin: posed joints with no match last frame:%s", miss);
    }
    if (g_slotDump > 0 && --g_slotDump > 0) Log("slotdump: frame end");
    if (g_logGun > 0 && !g_logGunDrawn) g_logGun--;   // no handgun drawn (RPG, fists, car): "glog" still ends
    g_logGunDrawn = false;
    for (int j = 0; j < 40; j++) g_skinBest[j] = 99.0f;
    memset(g_skinHits, 0, sizeof(g_skinHits));
    g_skinRefreshes = 0;
    g_skinNear = 0;
    g_skinRepl = g_skinGuess = 0;
}

// (diagnostic, driveby-m2) some cars draw the people in them smaller (a car's SetCharacterScale: the pickup
// 0.9, the school bus 0.95), about where they sit, while their skeleton stays full size: the expected
// matrices carry that scale (1 everywhere else)
static float g_skinScale = 1.0f;
static Vec3 g_skinPivot;
static Mat4 SkinScaleM()
{
    return Mul(Mul(Translation(g_skinPivot * -1.0f), ScaleM(g_skinScale)), Translation(g_skinPivot));
}
// (drunk.inc: the joints its pose layer moved, and the game's own pose from before - the upload is matched against that)
static bool g_drunkPosed[48];
static PoserPose g_drunkOrig;
static bool g_drunkSwayed = false;
static void SkinPrepare(const PoserPose& P, const Mat4* Wn, const bool* roots)
{
    g_skin.active = false;
    float v[16], p[16];
    if (!Game_GetCamera(v, p) || !LoadInvBind(P.numJoints)) return;
    memcpy(g_skinView, v, 64);
    Mat4 V = ToMat(v);
    if (g_skinScale != 1.0f) V = Mul(SkinScaleM(), V);
    g_skin.n = P.numJoints < 40 ? P.numJoints : 40;
    for (int j = 0; j < g_skin.n; j++) {
        g_skin.expect[j] = Mul(Mul(g_invBind[j], ToMat((g_drunkSwayed ? g_drunkOrig : P).world[j])), V);
        g_skin.hasRepl[j] = roots[j] || (g_drunkSwayed && g_drunkPosed[j]) || (j > 0 && g_skin.hasRepl[g_parent[j]]);   // the whole subtree
        if (g_skin.hasRepl[j]) g_skin.repl[j] = g_skinTest == 1 ? g_skin.expect[j] : Mul(Mul(g_invBind[j], Wn[j]), V);
        g_skin.world[j] = Wn[j];
    }
    g_skin.hand = Wn[28];
    g_lastHand = Row(g_skin.hand, 3);
    g_haveLastHand = g_skin.haveHand = g_skin.active = true;
}

bool Gun_SkinActive() { return g_skin.active; }

static SlotMap* MapFor(uint32_t key)
{
    SlotMap* pick = &g_maps[0];
    for (SlotMap& m : g_maps) {
        if (m.age && m.key == key) {
            m.age = ++g_mapClock;
            return &m;
        }
        if (m.age < pick->age) pick = &m;   // free, else the least recently drawn
    }
    memset(pick, 0, sizeof(*pick));
    pick->key = key;
    pick->age = ++g_mapClock;
    return pick;
}

static float BoneErr(const float* c, const Mat4& e, float cap)
{
    float err = 0;
    for (int r = 0; r < 3 && err < cap; r++)
        for (int i = 0; i < 4; i++) err = fmaxf(err, fabsf(c[r * 4 + i] - e.at(i, r)));
    return err;
}

// One piece of a skinned draw: bones[s] holds the 3 registers (columns of a 4x3 matrix)
// uploaded for palette slot s since the last draw (set[s]). Works out which of his joints
// each slot is and writes our matrix into out[s] (rep[s] = true) where we pose that joint.
// Returns the number of slots replaced.
static int g_drvMissLog;   // (diagnostic) "drvmiss <n>": log n unmatched pieces near him
static Mat4 g_dhCarCF;     // (diagnostic) the car he sits in (DhFindSeat), for drvmiss
static bool g_dhHaveCar;
static float g_dhScaleSeen = 1.0f;   // (diagnostic) the scale his unmatched pieces near him are drawn at
static bool g_dhSeated;              // (diagnostic) the m2 pose is on and he is in a car
int g_skinHeadPiece;   // (face.inc) the last piece resolved was his and one slot, on his head (his eyes): its key, else 0
static int Gun_SkinResolveBody(const float (*bones)[12], const bool* set, int nslots, uint32_t key, float (*out)[12], bool* rep);
int Gun_SkinResolve(const float (*bones)[12], const bool* set, int nslots, uint32_t key, float (*out)[12], bool* rep)
{
    double t0 = ProfNow();
    int r = Gun_SkinResolveBody(bones, set, nslots, key, out, rep);
    PerfAdd(9, ProfNow() - t0);
    return r;
}
static int Gun_SkinResolveBody(const float (*bones)[12], const bool* set, int nslots, uint32_t key, float (*out)[12], bool* rep)
{
    g_skinHeadPiece = 0;
    const float kTol = 0.03f;     // a match at all
    const float kExact = 0.002f;  // as good as the game's own numbers (true matches are ~1e-4)
    if (!g_skin.active || g_skinOff || nslots <= 0 || nslots > 32) return 0;
    struct Cand {
        int n;
        signed char j[8];
        float e[8];
    } cand[32];
    int pick[32];
    bool sure[32], any = false;
    for (int s = 0; s < nslots; s++) {
        cand[s].n = 0;
        pick[s] = -1;
        sure[s] = rep[s] = false;
        if (!set[s]) continue;
        for (int j = 0; j < g_skin.n && cand[s].n < 8; j++) {
            float err = BoneErr(bones[s], g_skin.expect[j], kTol);
            if (err < kTol) {
                cand[s].j[cand[s].n] = (signed char)j;
                cand[s].e[cand[s].n++] = err;
            }
        }
        any |= cand[s].n > 0;
    }
    if (!any && g_drvMissLog > 0) {   // (diagnostic) how far off the nearest of his joints is, per slot
        g_drvMissLog--;
        for (int s = 0; s < nslots && s < 12; s++) {
            if (!set[s]) continue;
            float best = 1e9f, bestT = 1e9f;
            int bj = -1, bjT = -1;
            for (int j = 0; j < g_skin.n; j++) {
                float e = BoneErr(bones[s], g_skin.expect[j], 1e9f);
                float dx = bones[s][3] - g_skin.expect[j].m[12], dy = bones[s][7] - g_skin.expect[j].m[13], dz = bones[s][11] - g_skin.expect[j].m[14];
                float t = sqrtf(dx * dx + dy * dy + dz * dz);
                if (e < best) best = e, bj = j;
                if (t < bestT) bestT = t, bjT = j;
            }
            Log("drvmiss: piece %08X slot %d: closest joint %d (max element error %.3f), closest position joint %d at %.3f m (view space)", key, s, bj,
                best, bjT, bestT);
            if (bestT < 0.3f && bjT >= 0) {   // his own piece, drawn off: how (the drawn joint against the posed one)
                Mat4 M = Identity();
                for (int r = 0; r < 3; r++)
                    for (int i = 0; i < 4; i++) M.at(i, r) = bones[s][r * 4 + i];
                Mat4 Wd = Mul(Mul(FullInverse(g_invBind[bjT]), M), RigidInverse(ToMat(g_skinView)));
                Mat4 Wp = Mul(Mul(FullInverse(g_invBind[bjT]), g_skin.expect[bjT]), RigidInverse(ToMat(g_skinView)));
                Mat4 D = Mul(FullInverse(Wp), Wd);   // world: drawn = posed * D
                Mat4 Dc = g_dhHaveCar ? Mul(Mul(g_dhCarCF, D), RigidInverse(g_dhCarCF)) : D;   // the same in the car's axes
                float tr = (Dc.m[0] + Dc.m[5] + Dc.m[10] - 1.0f) * 0.5f, ang = acosf(Clampf(tr, -1, 1)) * 180.0f / PI;
                Vec3 ax = Norm(V3(Dc.m[6] - Dc.m[9], Dc.m[8] - Dc.m[2], Dc.m[1] - Dc.m[4]));
                Vec3 pw = Row(Wp, 3), pd = Row(Wd, 3), dc = g_dhHaveCar ? XformDir(pd - pw, RigidInverse(g_dhCarCF)) : pd - pw;
                Log("drvmiss:   joint %d drawn %+.3f %+.3f %+.3f m from posed (car axes), turned %.2f deg about (%.2f %.2f %.2f) (car axes)%s", bjT,
                    dc.x, dc.y, dc.z, ang, ax.x, ax.y, ax.z, g_dhHaveCar ? "" : " [world axes: no car]");
            }
        }
    }
    if (!any && g_dhSeated) {   // (diagnostic) his own pieces, drawn at another scale? (a car's
                                              // SetCharacterScale): from the palette's 3x3 (unit without one)
        float sum = 0;
        int k = 0;
        for (int s = 0; s < nslots; s++) {
            if (!set[s]) continue;
            float dmin = 1e9f;
            for (int j = 0; j < g_skin.n; j++) {
                float dx = bones[s][3] - g_skin.expect[j].m[12], dy = bones[s][7] - g_skin.expect[j].m[13], dz = bones[s][11] - g_skin.expect[j].m[14];
                dmin = fminf(dmin, dx * dx + dy * dy + dz * dz);
            }
            if (dmin > 0.3f * 0.3f) continue;
            for (int r = 0; r < 3; r++)
                sum += sqrtf(bones[s][r * 4] * bones[s][r * 4] + bones[s][r * 4 + 1] * bones[s][r * 4 + 1] + bones[s][r * 4 + 2] * bones[s][r * 4 + 2]);
            k += 3;
        }
        if (k >= 6) {
            float sc = floorf(sum / k * 100.0f + 0.5f) / 100.0f;
            if (fabsf(sc - 1.0f) > 0.015f && fabsf(sc - g_dhScaleSeen) > 0.015f) {   // (m3: only a check now - the pose reads it up front)
                Log("dh: his pieces near him are drawn at scale %.2f, but the pose used %.2f", sc, g_dhScaleSeen);
            }
        }
    }
    if (!any) return 0;   // somebody else's mesh: don't touch the maps
    SlotMap* map = MapFor(key);
    bool used[40] = {};
    // 1. the remembered joint if it still fits exactly, else the only joint that does
    for (int s = 0; s < nslots; s++) {
        const Cand& c = cand[s];
        int known = map->joint[s] - 1, nex = 0, ex = -1;
        bool knownFits = false;
        for (int k = 0; k < c.n; k++)
            if (c.e[k] < kExact) {
                nex++;
                ex = c.j[k];
                knownFits |= c.j[k] == known;
            }
        if (knownFits) pick[s] = known;
        else if (nex == 1) {
            pick[s] = ex;
            map->joint[s] = (signed char)(ex + 1);
            g_mapLearned++;
        }
        if (pick[s] >= 0) {
            sure[s] = true;
            used[pick[s]] = true;
        }
    }
    // 2. ties: a joint sits in one slot per piece, so drop the ones placed elsewhere; then
    //    the closest
    for (int s = 0; s < nslots; s++) {
        const Cand& c = cand[s];
        if (sure[s] || !c.n) continue;
        int left = 0, lj = -1, best = -1;
        float bestErr = 1e9f;
        for (int k = 0; k < c.n; k++) {
            if (c.e[k] < kExact && !used[c.j[k]]) {
                left++;
                lj = c.j[k];
            }
            float e = c.e[k] + (used[c.j[k]] ? 1.0f : 0.0f);
            if (e < bestErr) {
                bestErr = e;
                best = c.j[k];
            }
        }
        if (left == 1) {
            pick[s] = lj;
            map->joint[s] = (signed char)(lj + 1);
            g_mapLearned++;
        } else {
            pick[s] = best;
        }
        used[pick[s]] = true;
    }
    if (nslots == 1 && pick[0] == 17) g_skinHeadPiece = (int)key;
    int nrep = 0;
    for (int s = 0; s < nslots; s++) {
        int j = pick[s];
        if (g_slotDump > 0 && set[s]) {
            char buf[256];
            int k = snprintf(buf, sizeof(buf), "piece %08X slot %d:", key, s);
            for (int q = 0; q < cand[s].n && k < 200; q++)
                k += snprintf(buf + k, sizeof(buf) - k, " %d(%.4f)", cand[s].j[q], cand[s].e[q]);
            snprintf(buf + k, sizeof(buf) - k, " -> %d%s", j, j < 0 || sure[s] ? "" : " (guess)");
            Log("%s", buf);
        }
        if (j < 0) continue;
        g_skinHits[j]++;
        if (g_skinLog > 0)
            for (int q = 26; q <= 28 && q < g_skin.n; q++)
                g_skinBest[q] = fminf(g_skinBest[q], BoneErr(bones[s], g_skin.expect[q], 99.0f));
        if (!g_skin.hasRepl[j]) continue;
        g_skinRepl++;
        if (!sure[s]) g_skinGuess++;
        const Mat4& m = g_skin.repl[j];
        for (int r = 0; r < 3; r++)
            for (int i = 0; i < 4; i++) out[s][r * 4 + i] = m.at(i, r);
        rep[s] = true;
        nrep++;
    }
    return nrep;
}

// Called once per frame at the game's first BeginScene, after its animation update. The
// right arm is rebuilt from the game's own animated local rotations (so nothing
// accumulates) with the aiming pose and a fist around the grip blended in, and handed to
// the skin swap above.

// ------------------------------------------------------------------ DIAGNOSTIC (driveby-m1): Homer seated
// Debug commands only; nothing runs unless asked. "drv 1": his seated skeleton, the car's driver seat, door
// hinge and window drawn over the picture, and a log once a second. "drv 2": also his left arm posed (the
// skin swap) from the game's own nps_point_left (homer.cho "point_left") and a pistol in his left hand.
// "drvframe <0-23>" the frame, "drvblend <0-1>" its weight over his driving pose, "drvsrc 1" the game's
// ndr_in_car_idle instead (a check: rebuilt from it the arm must land on the one the game drew).
#if WW_DRIVEBY
#include "drivebydiag_anim.h"
#endif
#if WW_DRIVEBY
static int g_drv, g_drvSrc, g_drvSide;   // "drvside 1": his right arm out of the right window (nps_point_right)
static float g_drvFrame = 12.0f, g_drvBlend = 1.0f, g_drvLogT, g_drvUpper, g_drvWristStraight;   // "drvupper", "drvwrist" 0..1
static float g_drvFkErr;
#else
static int g_drv, g_drvSide;   // (always 0: shooting from cars isn't in this build)
#endif
static bool g_drvInCar, g_drvPosed;
static Mat4 g_drvWn[40];
static Mat4 g_drvGun;
[[maybe_unused]] static bool g_drvHaveGun;   // (set as the pistol is drawn; read only by the car-shooting code)
static bool DhPose(PoserPose& P);   // (diagnostic, driveby-m2: driveby_hold.inc)
static Mat4 DhGunWorld();
static void DaSweep(PoserPose& P);  // (diagnostic, driveby-m3: driveby_aim.inc)

#if WW_DRIVEBY
static Mat4 DrvAnimRot(int k)   // joint 19 + k, this frame (between two keys), as a 3x3 in a Mat4
{
    const float* a;
    const float* b;
    float t = 0;
    if (g_drvSrc == 1) a = b = kInCarIdle[0][k];
    else {
        float f = fmaxf(0.0f, fminf(23.0f, g_drvFrame));
        int i = (int)f;
        t = f - i;
        a = g_drvSide ? kPointRight[i][k] : kPointLeft[i][k];
        b = g_drvSide ? kPointRight[i < 23 ? i + 1 : 23][k] : kPointLeft[i < 23 ? i + 1 : 23][k];
    }
    Mat4 m = Identity();
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m.m[r * 4 + c] = a[r * 3 + c] + (b[r * 3 + c] - a[r * 3 + c]) * t;
    return m;
}
static Mat4 DrvUpperRot(int k)   // joint 14 + k (spine, neck, head) from the same source and frame
{
    const float* a;
    const float* b;
    float t = 0;
    if (g_drvSrc == 1) a = b = kInCarIdleUpper[0][k];
    else {
        float f = fmaxf(0.0f, fminf(23.0f, g_drvFrame));
        int i = (int)f;
        t = f - i;
        a = g_drvSide ? kPointRightUpper[i][k] : kPointLeftUpper[i][k];
        b = g_drvSide ? kPointRightUpper[i < 23 ? i + 1 : 23][k] : kPointLeftUpper[i < 23 ? i + 1 : 23][k];
    }
    Mat4 m = Identity();
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m.m[r * 4 + c] = a[r * 3 + c] + (b[r * 3 + c] - a[r * 3 + c]) * t;
    return m;
}
static Mat4 Ortho(Mat4 m)   // rows 0-2 orthonormal again (after a blend)
{
    Vec3 x = Norm(Row(m, 0)), y = Row(m, 1);
    y = Norm(y - x * Dot(x, y));
    Vec3 z = Cross(x, y);
    if (Dot(z, Row(m, 2)) < 0) z = z * -1.0f;
    SetRow(m, 0, x, 0), SetRow(m, 1, y, 0), SetRow(m, 2, z, 0);
    return m;
}
static void DrvBuild(const PoserPose& P)
{
    static const int parL[7] = {15, 19, 20, 21, 22, 21, 24}, parR[7] = {15, 26, 27, 28, 29, 28, 31};
    const int* par = g_drvSide ? parR : parL;
    const int base = g_drvSide ? 26 : 19, wristJ = base + 2;
    g_drvFkErr = 0;
    for (int j = 0; j < P.numJoints && j < 40; j++) g_drvWn[j] = ToMat(P.world[j]);
    if (g_drvUpper > 0.0f) {   // the animation's own lean and head turn: spine, neck, head (then everything above them)
        for (int j = 14; j <= 17; j++) {
            Mat4 live = ToMat(P.object[j]), a = DrvUpperRot(j - 14), loc = live;
            for (int i = 0; i < 12; i++)
                if ((i & 3) != 3) loc.m[i] = live.m[i] + (a.m[i] - live.m[i]) * g_drvUpper;
            g_drvWn[j] = Mul(Ortho(loc), g_drvWn[g_parent[j]]);
        }
        for (int j = 18; j < P.numJoints && j < 33; j++)   // the jaw and right arm follow (the left arm is rebuilt below)
            if (j < base || j > base + 6) g_drvWn[j] = Mul(ToMat(P.object[j]), g_drvWn[g_parent[j]]);
    }
    for (int k = 0; k < 7; k++) {
        int j = base + k;
        Mat4 live = ToMat(P.object[j]);
        // the check: his own local times his parent's world is the world the game has
        Mat4 fk = Mul(live, ToMat(P.world[par[k]]));
        g_drvFkErr = fmaxf(g_drvFkErr, Len(Row(fk, 3) - Row(ToMat(P.world[j]), 3)));
        Mat4 a = DrvAnimRot(k), loc = live;
        for (int i = 0; i < 12; i++)
            if ((i & 3) != 3) loc.m[i] = live.m[i] + (a.m[i] - live.m[i]) * g_drvBlend;
        loc = Ortho(loc);
        if (j == wristJ && g_drvWristStraight > 0.0f) {   // the wrist in line with the forearm (a grip, not a pointing finger)
            Mat4 straight = WithTrans(Identity(), Row(loc, 3)), b2 = loc;
            for (int i = 0; i < 12; i++)
                if ((i & 3) != 3) b2.m[i] = loc.m[i] + (straight.m[i] - loc.m[i]) * g_drvWristStraight;
            loc = Ortho(b2);
        }
        g_drvWn[j] = Mul(loc, g_drvWn[par[k]]);
    }
}
static bool DrvPose(PoserPose& P)
{
    DrvBuild(P);
    bool roots[40] = {};
    roots[g_drvUpper > 0.0f ? 14 : (g_drvSide ? 26 : 19)] = true;   // (the upper body from the spine up when the lean is on)
    SkinPrepare(P, g_drvWn, roots);
    return g_skin.active;
}
#else
static bool DrvPose(PoserPose&) { return false; }
#endif

static PoserPose g_skinPose;   // the pose the arm was last built from
static void BuildPose(const PoserPose& P);
static bool Drunk_Active();
static bool Ragdoll_Active();
static void BuildPoseRagdoll(const PoserPose& P);
static void Drunk_PoseLayer(PoserPose& P);
static void Drunk_Camera(Mat4& world, float* fov);
static void BuildPoseRPG(const PoserPose& P);
static void BuildPoseShotgun(const PoserPose& P);
static void BuildPoseDonut(const PoserPose& P);
static void BuildPoseBeer(const PoserPose& P);
static bool Dn_Running();   // (donut.inc / beer.inc: in use - eaten or put back)
static bool Br_Running();

static void PoseTraceFrame();   // (posetrace.inc: debug "posetrace", last frame as drawn - before anything is rebuilt)
static int g_lazyItemBuild = 1;   // (debug "lazybuild 0|1": the item's pose built once a frame, at his first skinned draw)
static bool g_lazyBuild;          // (this frame's build waits for it)
static bool LazyItem()
{
    return !Ragdoll_Active() && ((G.held == WPN_DONUT && Dn_Running()) || (G.held == WPN_BEER && Br_Running()));
}
static void Gun_PreRenderBody();
void Gun_PreRender()
{
    double t0 = ProfNow();
    Gun_PreRenderBody();
    PerfAdd(10, ProfNow() - t0);
}
static void Gun_PreRenderBody()
{
    PoseTraceFrame();
    g_skin.haveLauncher = false;
    SkinFrameStats();
    g_buildFrame++;
    if (g_buildLog > 0) g_buildLog--;
    g_buildWhy = "pre";
    float fist = G.show, aim = G.aim;
    g_drvPosed = false;
    g_dhSeated = g_drv == 3 && g_drvInCar;
    if (g_drv == 3 && g_drvInCar && g_player && Game_ReadPoser(g_player, &g_skinPose)) {   // (diagnostic, m2)
        double t0 = ProfNow();
        DaSweep(g_skinPose);   // (m3: "drvsweep" only)
        g_drvPosed = DhPose(g_skinPose);
        g_daProf[1] += ProfNow() - t0;
        if (!g_drvPosed) g_skin.active = false;
        return;
    }
    if (g_drv == 2 && g_drvInCar && g_player && Game_ReadPoser(g_player, &g_skinPose)) {   // (diagnostic)
        g_drvPosed = DrvPose(g_skinPose);
        return;
    }
    g_skinScale = 1.0f;   // (on foot: drawn full size)
    if (!g_player || (fist < 0.001f && aim < 0.001f && !Drunk_Active()) || !Game_ReadPoser(g_player, &g_skinPose)) {
        g_skin.active = false;
        return;
    }
    {
        double t0 = ProfNow();
        Drunk_PoseLayer(g_skinPose);   // (drunk: his body leans - drunk.inc; the game's own pose kept for matching)
        if (Drunk_Active()) PerfAdd(3, ProfNow() - t0);
    }
    // (the donut and the can: built once a frame, at his first skinned draw - the game moves his skeleton after our BeginScene, so this
    // start-of-frame build was thrown away and done again there every frame: twice a frame, 3.9 ms each under Rosetta - the perf
    // meter. Only once his skin was ours last frame: the first draw of his is found by the matching a build sets up)
    if (g_lazyItemBuild && g_skin.active && LazyItem()) {
        g_lazyBuild = true;
        return;
    }
    g_lazyBuild = false;
    BuildPose(g_skinPose);
}

// Called before each skinned draw. The game updates the skeleton - and, when we
// aren't overriding it, its camera - after our BeginScene, and the uploaded matrices include
// both: if either moved, rebuild so the upload is matched and replaced exactly. (Matching
// against a stale pose or view made the arm or hand flicker back to the game's pose.)
static void Gun_SkinRefreshBody(const float* firstBone);
void Gun_SkinRefresh(const float* firstBone)
{
    if (!g_skin.active) return;
    double t0 = ProfNow();
    Gun_SkinRefreshBody(firstBone);
    PerfAdd(8, ProfNow() - t0);
}
static void Gun_SkinRefreshBody(const float* firstBone)
{
    // bone translations are in view space: skip uploads from characters across the street
    if (firstBone) {
        float best = 1e9f;
        for (int j = 0; j < g_skin.n; j++) {
            const Mat4& e = g_skin.expect[j];
            float dx = firstBone[3] - e.m[12], dy = firstBone[7] - e.m[13], dz = firstBone[11] - e.m[14];
            best = fminf(best, dx * dx + dy * dy + dz * dz);
        }
        if (best > 2.25f) return;   // more than 1.5 m from every joint of his
    }
    g_skinNear++;
    static PoserPose P;
    if (!Game_ReadPoserJoints(g_skinPose.jointsAddr, g_skinPose.numJoints, &P)) return;
    bool poseMoved = memcmp(P.world, (g_drunkSwayed ? g_drunkOrig : g_skinPose).world, sizeof(float) * 16 * P.numJoints) != 0;
    bool viewMoved = false;
    float v[16], pr[16];
    if (g_viewRefresh && Game_GetCamera(v, pr))
        for (int i = 0; i < 16 && !viewMoved; i++) viewMoved = fabsf(v[i] - g_skinView[i]) > 1e-5f;
    if (!poseMoved && !viewMoved && !g_lazyBuild) return;
    if (poseMoved) {   // (the joints only: where he is stays as read at the start of the frame - the seated pose finds his seat by it)
        float at[3];
        memcpy(at, g_skinPose.position, sizeof(at));
        g_skinPose = P;
        memcpy(g_skinPose.position, at, sizeof(at));
    }
    if (g_syncLog > 0 && poseMoved) {
        Vec3 pp;
        PlayerPos(&pp);
        Log("sync:   draw refresh pos (%.3f %.3f) root (%.3f %.3f)", pp.x, pp.z, P.world[0][12], P.world[0][14]);
    }
    g_skinRefreshes++;
    g_buildWhy = poseMoved ? "pose" : "view";
    if (poseMoved) Drunk_PoseLayer(g_skinPose);
    g_lazyBuild = false;
    if (g_drvPosed) {   // (diagnostic)
        double t0 = ProfNow();
        if (g_drv == 3) DhPose(g_skinPose);
        else DrvPose(g_skinPose);
        g_daProf[1] += ProfNow() - t0;
        return;
    }
    BuildPose(g_skinPose);
}

// how much of the aiming pose he can do: the chest twists towards the aim (up to
// kTwistMax), the arm covers the rest within its yaw range; past that the pose fades back
// to the animation instead of reaching behind his back
static float ReachFor(float rel, float blade, float* chestYaw)
{
    float cy = SoftClamp(rel + blade, -kTwistMax, kTwistMax);
    if (chestYaw) *chestYaw = cy;
    float need = rel - cy, over = fmaxf(0.0f, fmaxf(need - kArmYawHi, kArmYawLo - need));
    return 1.0f - Smooth01(over / kOverFade);
}

// Walking while aiming. The game only has forward walk / run / dash cycles (homer.cho:
// "walkerLoco", directionCount 1), so it moves him sideways or backwards with his legs still
// striding forward. The legs are turned instead, taken from the game's own cycle every frame:
//  - walking backwards: each foot's place under the hip is mirrored front to back, so the
//    planted foot travels forward under him as he moves back (the knee is re-solved to keep
//    bending forward, the foot's tilt is mirrored: lands on the toes, pushes off the heel)
//  - then the whole lower body (Pelvis and below) turns about the pelvis toward where he
//    walks; the spine and everything above stay on the aim.
// Lower-body joints: 4 Pelvis, 5-8 left Hip, Knee, Ankle, Ball, 9-12 the right, 13 Ass_Joint.
static int g_legDbg;          // debug "legs": 1 = log the leg layer once a frame
static const float kLegYawMax = 0.65f;   // how far the hips turn toward the walk (about 37 degrees)
static float g_legYawSet = 99.0f, g_legBackSet = -1.0f;   // debug "legset <yaw>,<back>": force (99 / -1 = off)
static Mat4 RotBetween(Vec3 a, Vec3 b)
{
    Vec3 ax = Cross(a, b);
    float s = Len(ax), c = Dot(a, b);
    if (s < 1e-5f) return Identity();
    return AxisAngle(ax * (1.0f / s), atan2f(s, c));
}
// the foot's tilt at rest (rad, down negative): the ankle to the ball of his bind pose
static float FootRestPitch(int a, int b)
{
    const float kHomer = -0.44f;   // (25 degrees: before his bind pose is read)
    if (!g_invBindSkel) return kHomer;
    Vec3 v = Row(FullInverse(g_invBind[b]), 3) - Row(FullInverse(g_invBind[a]), 3);
    float l = Len(v);
    return l > 1e-4f ? asinf(Clampf(v.y / l, -1.0f, 1.0f)) : kHomer;
}
static void LegLayer(Mat4* W, const Mat4* L, bool* roots, int n)
{
    float yaw = g_legYawSet < 90.0f ? g_legYawSet : G.legYaw.x * G.legW;
    float back = g_legBackSet >= 0.0f ? g_legBackSet : Clamp01(G.legBack.x) * G.legW;
    if (n < 14 || G.knocked || (fabsf(yaw) < 0.002f && back < 0.002f)) return;
    Vec3 fB = HeadingDir(G.bodyYaw);
    if (back > 0.002f) {
        for (int leg = 0; leg < 2; leg++) {
            int h = 5 + 4 * leg, k = h + 1, a = h + 2, b = h + 3;
            Vec3 pH = Row(W[h], 3), pK = Row(W[k], 3), pA = Row(W[a], 3), pB = Row(W[b], 3);
            float l1 = Len(pK - pH), l2 = Len(pA - pK);
            // the ankle mirrored about the hip's side plane (blended in by `back`)
            Vec3 tA = pA - fB * (2.0f * Dot(pA - pH, fB) * back);
            Vec3 u = tA - pH;
            float d = Len(u);
            if (d < 1e-4f || l1 < 1e-4f || l2 < 1e-4f) continue;
            u = u * (1.0f / d);
            d = Clampf(d, fabsf(l1 - l2) * 1.05f + 0.005f, (l1 + l2) * 0.999f);
            Vec3 hinge;
            Vec3 nK = TwoBone(pH, u, d, l1, l2, fB, fB, &hinge);   // the knee keeps pointing forward
            Vec3 nA = pH + u * d;
            Mat4 Rh = RotBetween(Norm(pK - pH), Norm(nK - pH));
            Mat4 Rk = RotBetween(Norm(pA - pK), Norm(nA - nK));
            // the foot: its tilt mirrored about its tilt at rest (toes down where the cycle had them up), turned about his hips'
            // side axis - about the rest tilt: the ankle sits above the ball (Homer: 25 degrees), and mirroring the bare tilt turned
            // a flat foot 50 degrees toes up; about his side axis: a turn between the foot's own directions lost its heading as the
            // foot pointed straight down, and the feet flipped 120-160 degrees in a frame walking backwards (the pose QA, 2026-10-14)
            Vec3 f = Norm(pB - pA), side = Norm(Cross(UP, fB));
            float th = asinf(Clampf(Dot(f, UP), -1.0f, 1.0f)), th0 = FootRestPitch(a, b);
            float turn = 2.0f * (th0 - th) * back;   // (toes up: +)
            float up = Dot(XformDir(fB, AxisAngle(side, 0.1f)), UP) > 0.0f ? 1.0f : -1.0f;
            Mat4 Rf = AxisAngle(side, up * turn);
            W[h] = RotateInPlace(W[h], Rh);
            W[k] = WithTrans(Mul(Rot3(W[k]), Rk), nK);
            W[a] = WithTrans(Mul(Rot3(W[a]), Rf), nA);
            W[b] = Mul(L[b], W[a]);
        }
    }
    if (fabsf(yaw) >= 0.002f) {
        Vec3 piv = Row(W[4], 3);
        Mat4 Y = Mul(Mul(Translation(piv * -1.0f), AxisAngle(UP, yaw)), Translation(piv));
        for (int j = 4; j <= 13; j++) W[j] = Mul(W[j], Y);
    }
    roots[4] = true;
    if (g_legDbg > 0) {
        g_legDbg--;
        Log("legs: yaw %+.2f back %.2f w %.2f vel (%.2f %.2f) heading %+.2f body %+.2f aim %+.2f speed %.2f mode %s", yaw, back,
            G.legW, G.vel.x, G.vel.z, atan2f(G.vel.x, G.vel.z), G.bodyYaw, G.yaw, G.speed, G.legBackMode ? "back" : "fwd");
    }
}

// The aiming pose, rebuilt from the game's animated skeleton each time it's skinned:
// chest twist and lean, head looking at the target, a relaxed left arm, the right arm by
// two-bone IK with the gun on the line to the aim point, and a fist around the grip.
// Only computes; every spring is stepped once a frame in Gun_Update.
static void BuildPose(const PoserPose& P)
{
    if (Ragdoll_Active()) {   // (fallen: all of him is the ragdoll's - ragdoll.inc)
        BuildPoseRagdoll(P);
        return;
    }
    if (G.held == WPN_RPG) {
        BuildPoseRPG(P);
        return;
    }
    if (G.held == WPN_SHOTGUN) {
        BuildPoseShotgun(P);
        return;
    }
    // (the donut and the can pose him only while they're in use - eaten or put back, the last one stays "held" with nothing shown, and
    // drunk (his skin posed anyway) the donut's pose went on holding his head to his body's way: the stress tests)
    if (G.held == WPN_DONUT && Dn_Running()) {
        double t0 = ProfNow();
        BuildPoseDonut(P);
        PerfAdd(0, ProfNow() - t0);
        return;
    }
    if (G.held == WPN_BEER && Br_Running()) {
        double t0 = ProfNow();
        BuildPoseBeer(P);
        PerfAdd(1, ProfNow() - t0);
        return;
    }
    float fist = G.show, aim = G.aim;
    int n = P.numJoints < 40 ? P.numJoints : 40;
    if (!LoadInvBind(P.numJoints)) {   // parents first (cached)
        g_skin.active = false;
        return;
    }
    static Mat4 W[40], L[40];
    for (int j = 0; j < n; j++) {
        W[j] = ToMat(P.world[j]);
        L[j] = ToMat(P.object[j]);
    }
    // the grip sits where Homer's fist closes, 10 cm out from his wrist; Bart's and Lisa's hands are
    // half as long (wrist to knuckles 6.0 cm, Homer's 11.6), so it scales with the hand, or their
    // pistol floated ahead of the fist (Homer, Marge, Apu: x1.00, 1.03, 0.99)
    g_skin.handK = n > 29 ? Clampf(Len(Row(L[29], 3)) / 0.116f, 0.4f, 1.3f) : 1.0f;
    auto Rebuild = [&](int j) { W[j] = Mul(L[j], W[g_parent[j]]); };
    G.bodyYaw = atan2f(-P.world[0][8], -P.world[0][10]);   // characters face -Z of the root
    G.haveBody = true;
    G.knocked = Norm(Row(W[17], 3) - Row(W[0], 3)).y < 0.8f;
    bool roots[40] = {};
    roots[26] = true;   // right arm always (fist)
    G.reachTarget = 1.0f;

    // carry: with the gun out the right arm holds it at low ready instead of dangling it from
    // the animation; becomes the base the aiming pose rises from
    float wC = Clamp01(G.show) * Clamp01(G.carryW.x);
    if (wC > 0.001f) {
        Vec3 fB = HeadingDir(G.bodyYaw), rB = Norm(Cross(UP, fB));
        Mat4 W26c = Mul(L[26], W[15]);
        Vec3 pS = Row(W26c, 3);
        float a = Len(Row(L[27], 3)), b = Len(Row(L[28], 3));
        Vec3 up1 = Norm(UP * -1.0f + rB * kCarryUpOut + fB * kCarryUpFwd);
        Vec3 pE0 = pS + up1 * a;
        Vec3 fore1 = Norm(fB * kCarryForeFwd - UP * kCarryForeDown + rB * kCarryForeOut);
        Vec3 pW0 = pE0 + fore1 * b;
        Vec3 u = pW0 - pS;
        float d = Len(u);
        u = u * (1.0f / d);
        Vec3 pole = Norm((pE0 - pS) - u * Dot(pE0 - pS, u));
        Vec3 hinge;
        Vec3 pE = TwoBone(pS, u, fminf(d, (a + b) * 0.995f), a, b, pole, fB * -1.0f, &hinge);
        Vec3 pW = pS + u * fminf(d, (a + b) * 0.995f), fore = Norm(pW - pE);
        Mat4 R26 = FrameXZ(Norm(pS - pE), hinge);   // right-arm bones run along -X
        Mat4 R27 = FrameXZ(fore * -1.0f, hinge);
        Vec3 Dg = Norm(fB * cosf(kCarryGunDown) - UP * sinf(kCarryGunDown) + rB * kCarryGunOut);
        float c = Dot(Dg, fore);
        if (c < cosf(kWristCone)) Dg = Norm(fore * cosf(kWristCone) + Norm(Dg - fore * c) * sinf(kWristCone));
        Vec3 top = Norm(XformDir(Norm(UP - Dg * Dot(UP, Dg)), AxisAngle(Dg, kCarryCant)));
        Vec3 x = Dg * -1.0f, y = top * -1.0f;   // fingers -X, thumb -Y (how the gun is mounted)
        Mat4 R28 = Basis(x, y, Cross(x, y));
        Mat4 L26c = Mul(R26, Transpose3(W[15])), L27c = Mul(R27, Transpose3(R26)), L28c = Mul(R28, Transpose3(R27));
        L[26] = WithTrans(BlendRot(Rot3(L[26]), L26c, wC), Row(L[26], 3));
        L[27] = WithTrans(BlendRot(Rot3(L[27]), L27c, wC), Row(L[27], 3));
        L[28] = WithTrans(BlendRot(Rot3(L[28]), L28c, wC), Row(L[28], 3));
    }
    if (aim > 0.001f && G.haveAim) {
        bool torso = g_torsoOK && g_torsoOn;
        // 1. the aim relative to his hips (he may still be turning, or be held by an action)
        Vec3 D0 = Norm(G.aimPoint - Row(W[15], 3));
        float chestYaw, rel = WrapPi(Heading(D0) - G.bodyYaw);
        G.reachTarget = ReachFor(rel, kBlade * G.camAim, &chestYaw);
        float wR = Clamp01(G.reach.x), wU = torso ? aim * wR : 0.0f;
        float aimPitch = asinf(Clampf(D0.y, -1, 1));
        float lean = SoftClamp(aimPitch * kLean, kLeanLo, kLeanHi) + G.kickChest.x;
        float twisted = 0;
        if (torso) {
            // 2. spine: twist about up, then lean (Spine_1 35%, Spine_2 65%)
            for (int j = 14; j <= 15; j++) {
                if (j == 15) Rebuild(15);
                float sh = j == 14 ? kTwist1 : 1 - kTwist1;
                W[j] = RotateInPlace(W[j], YawPitchAt(G.bodyYaw + twisted, chestYaw * sh * wU, lean * sh * wU));
                twisted += chestYaw * sh * wU;
            }
            // 3. head: the rest of the heading, most of the remaining pitch; the jaw follows
            Rebuild(16);
            Rebuild(17);
            Vec3 fH = Norm(Row(W[17], 0)), dH = Norm(G.aimPoint - Row(W[17], 3));
            float hy = SoftClamp(WrapPi(Heading(dH) - Heading(fH)), -0.9f, 0.9f) * kHeadYaw * wU;
            float hp = SoftClamp((aimPitch - lean) * kHeadPitch, -0.45f, 0.40f) * wU;
            W[17] = RotateInPlace(W[17], YawPitchAt(Heading(fH), hy, hp));
            Rebuild(18);
        }
        float yawC = G.bodyYaw + twisted;
        Vec3 fC = HeadingDir(yawC), rC = Norm(Cross(UP, fC));   // chest forward / right, level
        if (torso) {
            // 4. left arm: relaxed at his side (walk / idle animations can't flail it)
            Rebuild(19);
            Vec3 pS = Row(W[19], 3), hinge;
            float a = Len(Row(L[20], 3)), b = Len(Row(L[21], 3)), d = (a + b) * 0.92f;
            Vec3 u = Norm(UP * -1.0f + rC * -0.30f + fC * 0.15f);
            Vec3 pE = TwoBone(pS, u, d, a, b, Norm(fC * -1.0f + rC * -0.4f), rC * -1.0f, &hinge);
            Mat4 R19 = FrameXZ(Norm(pE - pS), hinge);   // left-arm bones run along +X
            Mat4 R20 = FrameXZ(Norm(pS + u * d - pE), hinge);
            W[19] = BlendLocal(L[19], Mul(R19, Transpose3(W[15])), W[15], aim);
            W[20] = BlendLocal(L[20], Mul(R20, Transpose3(R19)), W[19], aim);
            for (int j = 21; j <= 25; j++) Rebuild(j);
        }
        // 5. right arm: two-bone IK, the wrist on the line from the shoulder to the aim point
        Rebuild(26);
        {
            Vec3 pS = Row(W[26], 3), hinge;
            float a = Len(Row(L[27], 3)), b = Len(Row(L[28], 3));
            Vec3 D = Norm(G.aimPoint - pS);
            D = Norm(XformDir(D, AxisAngle(Norm(Cross(D, UP)), G.kickArm.x)));        // recoil lifts the arm
            float ay = SoftClamp(WrapPi(Heading(D) - yawC), kArmYawLo, kArmYawHi);   // stay in front of him
            float ap = SoftClamp(asinf(Clampf(D.y, -1, 1)), kArmPitchLo, kArmPitchHi);
            Vec3 u = HeadingDir(yawC + ay) * cosf(ap) + UP * sinf(ap);
            float d = (a + b) * (kReach - 0.6f * G.kickElbow.x);                      // recoil folds the elbow
            Vec3 pE = TwoBone(pS, u, d, a, b, Norm(UP * -1.0f + rC * kPoleOut), fC * -1.0f, &hinge);
            Vec3 pW = pS + u * d, fore = Norm(pW - pE);
            Mat4 R26 = FrameXZ(Norm(pS - pE), hinge);   // right-arm bones run along -X
            Mat4 R27 = FrameXZ(fore * -1.0f, hinge);
            Vec3 Dg = Norm(G.aimPoint - pW);
            Dg = Norm(XformDir(Dg, AxisAngle(Norm(Cross(Dg, UP)), G.kickWrist.x)));   // muzzle flip
            float c = Dot(Dg, fore);
            if (c < cosf(kWristCone)) Dg = Norm(fore * cosf(kWristCone) + Norm(Dg - fore * c) * sinf(kWristCone));
            Vec3 top = Norm(XformDir(Norm(UP - Dg * Dot(UP, Dg)), AxisAngle(Dg, kCant)));   // + = top tilted inward
            Vec3 x = Dg * -1.0f, y = top * -1.0f;   // fingers -X, thumb -Y (how the gun is mounted)
            Mat4 R28 = Basis(x, y, Cross(x, y));
            float wA = aim * wR;
            W[26] = BlendLocal(L[26], Mul(R26, Transpose3(W[15])), W[15], wA);
            W[27] = BlendLocal(L[27], Mul(R27, Transpose3(R26)), W[26], wA);
            W[28] = BlendLocal(L[28], Mul(R28, Transpose3(R27)), W[27], wA);
        }
        roots[14] = torso;
        roots[19] = torso;
    } else {
        for (int j = 26; j <= 28; j++) Rebuild(j);
    }
    // 6. fist around the grip (rotations in each finger's parent frame)
    Mat4 L29 = L[29], L30 = L[30], L31 = L[31], L32 = L[32];
    if (fist > 0.001f) {
        Mat4 L29r = Mul(Rot3(L29), AxisAngle(V3(0, 1, 0), kCurl1 * fist));
        Vec3 ax30 = Norm(XformDir(V3(0, 1, 0), Transpose3(L29r)));
        Mat4 L30r = Mul(Rot3(L30), AxisAngle(ax30, kCurl2 * fist));
        Vec3 tAx = kThumbAxis == 0 ? V3(1, 0, 0) : kThumbAxis == 1 ? V3(0, 1, 0) : V3(0, 0, 1);
        Mat4 L31r = Mul(Rot3(L31), AxisAngle(tAx, kThumb * fist));
        Vec3 ax32 = Norm(XformDir(tAx, Transpose3(L31r)));
        Mat4 L32r = Mul(Rot3(L32), AxisAngle(ax32, kThumb2 * fist));
        L29 = WithTrans(L29r, Row(L29, 3));
        L30 = WithTrans(L30r, Row(L30, 3));
        L31 = WithTrans(L31r, Row(L31, 3));
        L32 = WithTrans(L32r, Row(L32, 3));
    }
    W[29] = Mul(L29, W[28]);
    W[30] = Mul(L30, W[29]);
    W[31] = Mul(L31, W[28]);
    W[32] = Mul(L32, W[31]);
    for (int j = 33; j < n; j++) Rebuild(j);   // extra joints (hair...) ride on their parents
    LegLayer(W, L, roots, n);
    SkinPrepare(P, W, roots);
    if (g_buildLog > 0) {
        Vec3 h = Row(W[28], 3), f = Row(W[28], 0), r0 = Row(W[0], 3), s15 = Row(W[15], 3);
        Log("build f%d %-7s yaw %+.4f root (%.3f %.3f %.3f) rootZ (%.3f %.3f) spine2 (%.3f %.3f %.3f) hand (%.3f %.3f %.3f) fingers (%.3f %.3f %.3f) carryW %.3f show %.3f",
            g_buildFrame, g_buildWhy, G.bodyYaw, r0.x, r0.y, r0.z, P.world[0][8], P.world[0][10], s15.x, s15.y, s15.z, h.x, h.y,
            h.z, f.x, f.y, f.z, G.carryW.x, G.show);
    }
}

// ------------------------------------------------------------------ rocket launcher pose
// The launcher rides on his right shoulder and his hands are put on it (the other way round from
// the handgun, which follows his hand). Carried, it points forward and up; aiming brings it down
// level at the aim point, his chest turns his left side to the target and his left hand comes up
// under the sleeve. All of it comes from his own skeleton - where his shoulders and neck are, how
// long his arms are - and the launcher is sized to him (LauncherScale), so the same pose fits
// Bart, Lisa, Marge and Apu as well as Homer.
static float kLchRestUp = 0.170f, kLchRestOut = 0.030f;   // where it rests: above / beyond Shoulder_R (x scale)
static float kLchCarryUp = 0.75f, kLchCarryOut = 0.10f;   // carried: muzzle above level / outward (radians, share)
static float kLchBlade = 0.45f;                           // aiming: chest turned this far, left side forward
static float kLchGripZ = 0.335f, kLchGripY = -0.150f;     // the trigger grip's centre (launcher space)
static float kLchSupZ0 = 0.30f, kLchSupZ1 = 0.60f;        // the sleeve the left hand holds
static const float kLchSupR = 0.084f;                     // its radius
static float kLHandAlong = 0.45f, kLHandThick = 0.030f;   // left palm centre: along the hand (share of its length), palm thickness (m)
static float kLFinger = 0.55f;                            // left fingers: 0 straight forward, 1 across under the tube
static float kLCurl = 0.95f, kLThumb = -0.6f;             // left hand's curl round the sleeve
static float kLchScaleSet;                                // debug "lscale": a fixed size (0 = his own)

// The launcher's size for him: by his reach (upper arm + forearm) and shoulder width, against
// Homer's (0.411 m, 0.381 m). Bart and Lisa get a smaller one, Apu a bigger one.
static float LauncherScale(const Mat4* L)
{
    if (kLchScaleSet > 0.01f) return kLchScaleSet;
    float reach = Len(Row(L[27], 3)) + Len(Row(L[28], 3));
    float width = Len(Row(L[26], 3) - Row(L[19], 3));   // both shoulders hang off Spine_2
    return Clampf(0.5f * reach / 0.411f + 0.5f * width / 0.381f, 0.70f, 1.15f);
}

// The fist around a grip: fingers curl about their Y, the thumb about its Z (the same angles
// close either hand: the left is the right mirrored in X and Y). j0 = Middle_Base, j0 + 2 = Thumb_Base.
static void CurlHand(Mat4* W, Mat4* L, int wrist, int j0, float curl1, float curl2, float thumb1, float thumb2)
{
    Mat4 L0 = L[j0], L1 = L[j0 + 1], T0 = L[j0 + 2], T1 = L[j0 + 3];
    Mat4 L0r = Mul(Rot3(L0), AxisAngle(V3(0, 1, 0), curl1));
    Vec3 ax1 = Norm(XformDir(V3(0, 1, 0), Transpose3(L0r)));
    Mat4 L1r = Mul(Rot3(L1), AxisAngle(ax1, curl2));
    Mat4 T0r = Mul(Rot3(T0), AxisAngle(V3(0, 0, 1), thumb1));
    Vec3 axt = Norm(XformDir(V3(0, 0, 1), Transpose3(T0r)));
    Mat4 T1r = Mul(Rot3(T1), AxisAngle(axt, thumb2));
    W[j0] = Mul(WithTrans(L0r, Row(L0, 3)), W[wrist]);
    W[j0 + 1] = Mul(WithTrans(L1r, Row(L1, 3)), W[j0]);
    W[j0 + 2] = Mul(WithTrans(T0r, Row(T0, 3)), W[wrist]);
    W[j0 + 3] = Mul(WithTrans(T1r, Row(T1, 3)), W[j0 + 2]);
}

static void BuildPoseRPG(const PoserPose& P)
{
    float show = Clamp01(G.show), aim = G.aim;
    int n = P.numJoints < 40 ? P.numJoints : 40;
    if (!LoadInvBind(P.numJoints)) {
        g_skin.active = false;
        return;
    }
    static Mat4 W[40], L[40];
    for (int j = 0; j < n; j++) {
        W[j] = ToMat(P.world[j]);
        L[j] = ToMat(P.object[j]);
    }
    // the grip sits where Homer's fist closes, 10 cm out from his wrist; Bart's and Lisa's hands are
    // half as long (wrist to knuckles 6.0 cm, Homer's 11.6), so it scales with the hand, or their
    // pistol floated ahead of the fist (Homer, Marge, Apu: x1.00, 1.03, 0.99)
    g_skin.handK = n > 29 ? Clampf(Len(Row(L[29], 3)) / 0.116f, 0.4f, 1.3f) : 1.0f;
    auto Rebuild = [&](int j) { W[j] = Mul(L[j], W[g_parent[j]]); };
    G.bodyYaw = atan2f(-P.world[0][8], -P.world[0][10]);   // characters face -Z of the root
    G.haveBody = true;
    G.knocked = Norm(Row(W[17], 3) - Row(W[0], 3)).y < 0.8f;
    bool roots[40] = {};
    G.reachTarget = 1.0f;
    float k = LauncherScale(L);
    bool torso = g_torsoOK && g_torsoOn;

    // 1. aiming: chest and head as with the handgun, but bladed the other way (left side to the
    //    target, the launcher on the right shoulder)
    float wA = 0.0f;
    if (aim > 0.001f && G.haveAim) {
        Vec3 D0 = Norm(G.aimPoint - Row(W[15], 3));
        float chestYaw, rel = WrapPi(Heading(D0) - G.bodyYaw);
        G.reachTarget = ReachFor(rel, kLchBlade * G.camAim, &chestYaw);
        wA = aim * Clamp01(G.reach.x);
        float aimPitch = asinf(Clampf(D0.y, -1, 1));
        float lean = SoftClamp(aimPitch * kLean, kLeanLo, kLeanHi) + G.kickChest.x;
        if (torso) {
            float twisted = 0;
            for (int j = 14; j <= 15; j++) {
                if (j == 15) Rebuild(15);
                float sh = j == 14 ? kTwist1 : 1 - kTwist1;
                W[j] = RotateInPlace(W[j], YawPitchAt(G.bodyYaw + twisted, chestYaw * sh * wA, lean * sh * wA));
                twisted += chestYaw * sh * wA;
            }
            Rebuild(16);
            Rebuild(17);
            Vec3 fH = Norm(Row(W[17], 0)), dH = Norm(G.aimPoint - Row(W[17], 3));
            float hy = SoftClamp(WrapPi(Heading(dH) - Heading(fH)), -0.9f, 0.9f) * kHeadYaw * wA;
            float hp = SoftClamp((aimPitch - lean) * kHeadPitch, -0.45f, 0.40f) * wA;
            W[17] = RotateInPlace(W[17], YawPitchAt(Heading(fH), hy, hp));
            Rebuild(18);
            roots[14] = true;
        }
    }
    Rebuild(19);
    Rebuild(26);

    // 2. the launcher: resting on top of his right shoulder, its frame from his own joints
    Vec3 sR = Row(W[26], 3), sL = Row(W[19], 3), neck = Row(W[16], 3);
    Vec3 cUp = Norm(neck - Row(W[15], 3));
    Vec3 side = sR - sL;
    side = Norm(side - cUp * Dot(side, cUp));   // his right
    Vec3 cFwd = Norm(Cross(side, cUp));         // right = up x forward
    Vec3 rest = sR + cUp * (kLchRestUp * k) + side * (kLchRestOut * k);
    Vec3 dCarry = Norm(cFwd * cosf(kLchCarryUp) + cUp * sinf(kLchCarryUp) + side * kLchCarryOut);
    Vec3 dAim = G.haveAim ? Norm(G.aimPoint - rest) : cFwd;
    Vec3 D = Norm(dCarry * (1.0f - wA) + dAim * wA);
    Vec3 flipAx = Cross(UP, D);
    flipAx = Len(flipAx) > 0.2f ? Norm(flipAx) : side;
    D = Norm(XformDir(D, AxisAngle(flipAx, -G.lchFlip.x)));   // recoil: the muzzle kicks up
    rest = rest - D * (G.lchBack.x * k);                      // and the launcher shoves back
    // its roll from world up; knocked over, from his chest's up (getting up off his back the tube
    // passed vertical and the launcher spun round between two frames). Upright: as before
    float kw = Clamp01(G.knockW.x);
    Vec3 ref = kw > 0.001f ? Norm(UP * (1.0f - kw) + cUp * kw) : UP;
    Vec3 Y = ref - D * Dot(ref, D);
    Y = Len(Y) > 0.2f ? Norm(Y) : cUp;
    Vec3 X = Norm(Cross(Y, D));
    Y = Cross(D, X);
    Mat4 LW = WithTrans(Basis(X, Y, D), rest);

    // 3. right hand on the trigger grip, closed round it as round the pistol's (the same wrist
    //    mapping: tube forward = the wrist's -X, launcher top = its -Y), arm by two-bone IK
    {
        Vec3 grip = XformPoint(V3(0, kLchGripY * k, kLchGripZ * k), LW);
        Mat4 R28 = Basis(D * -1.0f, Y * -1.0f, Cross(D * -1.0f, Y * -1.0f));
        Vec3 pWt = grip - XformDir(GripInWrist(false), R28);
        float a = Len(Row(L[27], 3)), b = Len(Row(L[28], 3));
        Vec3 u = pWt - sR;
        float d = Len(u);
        u = d > 1e-4f ? u * (1.0f / d) : cFwd;
        d = Clampf(d, fabsf(a - b) * 1.05f + 0.01f, (a + b) * 0.995f);
        Vec3 hinge;
        Vec3 pE = TwoBone(sR, u, d, a, b, Norm(cUp * -1.0f + side * 0.6f), cFwd * -1.0f, &hinge);
        Vec3 fore = Norm(sR + u * d - pE);
        Mat4 R26 = FrameXZ(Norm(sR - pE), hinge);   // right-arm bones run along -X
        Mat4 R27 = FrameXZ(fore * -1.0f, hinge);
        W[26] = BlendLocal(L[26], Mul(R26, Transpose3(W[15])), W[15], show);
        W[27] = BlendLocal(L[27], Mul(R27, Transpose3(R26)), W[26], show);
        W[28] = BlendLocal(L[28], Mul(R28, Transpose3(R27)), W[27], show);
        roots[26] = true;
    }
    CurlHand(W, L, 28, 29, kCurl1 * show, kCurl2 * show, kThumb * show, kThumb2 * show);

    // 4. aiming: left hand under the sleeve, palm up, fingers round it towards his right, as
    //    far forward along it as his arm reaches (short arms hold it nearer the grip)
    float wL = 0.0f;
    if (torso && wA > 0.001f) {
        float a = Len(Row(L[20], 3)), b = Len(Row(L[21], 3)), handLen = Len(Row(L[22], 3));
        Vec3 fingers = Norm(X * kLFinger + D * (1.0f - kLFinger));
        Vec3 zl = Y * -1.0f;                                 // the left palm faces -Z: up, at the tube
        Vec3 xl = Norm(fingers - zl * Dot(fingers, zl));
        Mat4 R21 = Basis(xl, Cross(zl, xl), zl);             // fingers +X, thumb +Y (left hand)
        // the wrist, from a point on the sleeve's underside: back along the hand, below the palm
        Vec3 off = Y * -(kLchSupR * k) - xl * (kLHandAlong * handLen) + zl * (kLHandThick * k);
        Vec3 C = rest + off - sL;
        float r = (a + b) * 0.97f, bq = Dot(C, D), disc = bq * bq - (Dot(C, C) - r * r);
        float z0 = kLchSupZ0 * k, z1 = kLchSupZ1 * k, z = z0, reachL = 0.0f;
        if (disc >= 0.0f) {
            float zMax = -bq + sqrtf(disc);
            z = Clampf(zMax - 0.02f, z0, z1);
            reachL = 1.0f - Smooth01((z0 - zMax) / 0.12f);
        }
        wL = wA * reachL;
        if (wL > 0.001f) {
            Vec3 pWt = rest + D * z + off;
            Vec3 u = pWt - sL;
            float d = Len(u);
            u = d > 1e-4f ? u * (1.0f / d) : cFwd;
            d = Clampf(d, fabsf(a - b) * 1.05f + 0.01f, (a + b) * 0.995f);
            Vec3 hinge;
            Vec3 pE = TwoBone(sL, u, d, a, b, Norm(cUp * -1.0f + side * -0.5f), cFwd * -1.0f, &hinge);
            Mat4 R19 = FrameXZ(Norm(pE - sL), hinge);   // left-arm bones run along +X
            Mat4 R20 = FrameXZ(Norm(sL + u * d - pE), hinge);
            W[19] = BlendLocal(L[19], Mul(R19, Transpose3(W[15])), W[15], wL);
            W[20] = BlendLocal(L[20], Mul(R20, Transpose3(R19)), W[19], wL);
            W[21] = BlendLocal(L[21], Mul(R21, Transpose3(R20)), W[20], wL);
            CurlHand(W, L, 21, 22, kLCurl * wL, kLCurl * 0.8f * wL, kLThumb * wL, kLThumb * 0.5f * wL);
            roots[19] = true;
        }
    }
    if (!roots[19])
        for (int j = 20; j <= 25; j++) Rebuild(j);
    for (int j = 33; j < n; j++) Rebuild(j);   // extra joints (hair...) ride on their parents
    LegLayer(W, L, roots, n);
    SkinPrepare(P, W, roots);
    g_skin.launcher = LW;
    g_skin.lchScale = k;
    g_skin.haveLauncher = true;
    if (g_buildLog > 0) {
        Vec3 h = Row(W[28], 3), hl = Row(W[21], 3);
        Log("build f%d %-7s rpg k %.2f aim %.2f wA %.2f wL %.2f rest (%.3f %.3f %.3f) dir (%.2f %.2f %.2f) fwd.body %.2f "
            "hand R (%.3f %.3f %.3f) L (%.3f %.3f %.3f)",
            g_buildFrame, g_buildWhy, k, aim, wA, wL, rest.x, rest.y, rest.z, D.x, D.y, D.z,
            Dot(cFwd, HeadingDir(G.bodyYaw)), h.x, h.y, h.z, hl.x, hl.y, hl.z);
    }
}

#if WW_DRIVEBY
#include "driveby_hold.inc"   // (diagnostic, driveby-m2)
#else
// (shooting from cars left out of this build: the stand-ins the rest of gun.cpp calls, each as the real one with DriveBy=0)
static float g_dhShow;   // (the pistol in the car never shows)
static bool DhPose(PoserPose&) { return false; }
static Mat4 DhGunWorld() { return Identity(); }
static bool DhCarFrame(uint32_t, Mat4*) { return false; }   // (only shots fired from a car ride with it)
#endif

// ------------------------------------------------------------------ firing
// A bullet hits something: a starburst, chips flying off the surface, a dust puff (left out when
// the game showed its own) and the game's own hit sound. `out` is the surface normal (or back
// along the shot when it isn't known). A person gets only the starburst and a soft thud.
static void Impact(Vec3 p, Vec3 out, Surface surf, bool gamePuff, float size = 1.0f, bool sound = true)
{
    bool soft = Surface_IsSoft(surf), body = surf == SURF_BODY, glass = surf == SURF_GLASS, tire = surf == SURF_TIRE;
    Vec3 dust = soft ? V3(0.62f, 0.55f, 0.38f) : V3(0.80f, 0.78f, 0.74f);
    if (G.npuffs < 24)
        G.puffs[G.npuffs++] = {p + out * 0.05f, 0, gamePuff || body ? 0.12f : 0.60f + Rnd() * 0.2f,
                               (gamePuff || body ? 0.35f : 0.55f + Rnd() * 0.15f) * size, Rnd() * 6.3f, dust};
    for (int i = 0; i < (size < 1.0f ? 4 : 8) && G.nchips < 64 && !body; i++) {
        Chip& c = G.chips[G.nchips++];
        Vec3 spray = Norm(out + V3(Rnd() - 0.5f, Rnd() * 0.8f, Rnd() - 0.5f) * 1.4f);
        c = {p + out * 0.03f, spray * (2.0f + Rnd() * 2.5f) + UP * 1.0f, 0, 0.5f + Rnd() * 0.4f, Rnd() * 6.3f,
             (Rnd() - 0.5f) * 20.0f, 0.018f + Rnd() * 0.02f,
             glass ? V3(0.80f, 0.92f, 1.0f) : tire ? V3(0.10f, 0.10f, 0.11f) : soft ? V3(0.36f, 0.30f, 0.18f) : V3(0.52f, 0.50f, 0.47f)};
    }
    // heard from the camera on screen: the aim camera's position is only kept while aiming (from the
    // hip it was the last aim's, or (0,0,0) before the first - Level 1's yard is ~285 m from that,
    // so hip-shot impacts were silent)
    Vec3 ear = G.camPos;
    float gw[16], gf;
    if (G.camAim <= 0.001f && Game_GetGameCamera(gw, &gf)) ear = V3(gw[12], gw[13], gw[14]);
    float d = Len(p - ear);
    if (glass || tire || !sound) return;   // (a window's crack and a tire's bang have their own sounds: carglass.cpp, tires.cpp)
    int snd = soft || body ? SND_HIT_SOFT : surf == SURF_CAR ? SND_METAL1 + (int)(Rnd() * 2.99f) : SND_HIT_HARD;   // a car: a metal clank
    Sound_Play(snd, Clamp01(1.1f - d / 50.0f) * 0.8f, 0.9f + 0.2f * Rnd());
}

// The handgun, the reference weapon: one bullet straight down the sights; about 12 hits wreck a
// car and each rocks it a little; a hit knocks a pedestrian down a little harder than a kick;
// bullet-sized impacts (the dust puff at 0.4 of a car's tyre smoke).
static const WeaponSpec kHandgun = {
    "handgun",
    70.0f, kTraceRadius,   // range, thickness
    1, 0.0f,               // pellets, spread
    0.08f, 400.0f,         // vehicles: share of hit points, shove
    500.0f, 0.785398f,     // pedestrians: push, 45 degrees up (the kick's)
    400.0f, true,          // props: push (the kick's), breaks breakables
    true,                  // Hit & Run
    0.16f,                 // bullet holes 16 cm across (larger than life, as in GTA, so they read from afar)
    {0.4f, 0.6f, 1.0f, 1.0f, 1.0f, 0.25f, 0.5f},   // dust, dirt, grass, water, own, sparks, car sparks
};
// debug "testshot <pellets>,<spread deg>[,<shots>]": the next shots use a copy of the handgun with
// more pellets (checks the shared pellet path; not a weapon)
static WeaponSpec g_testSpec;
static int g_testShots;
static const float kHipHold = 0.6f;    // the arm stays up this long after a shot from the hip
static const float kHipReady = 0.85f;  // a shot from the hip waits for the arm to be this far up
static int g_shotLog;                  // debug "shotlog <shots>": arm and barrel at each shot

static void FireHandgun()
{
    G.cooldown = 0.24f;
    G.kickWrist.v += 7.5f + (Rnd() - 0.5f) * 1.5f;   // muzzle flips up ~8 degrees
    G.kickArm.v += 3.0f;                            // the arm lifts
    G.kickElbow.v += 3.5f;                          // the elbow absorbs it, hand pulls back
    G.kickChest.v += 1.2f;                          // a little lean back
    G.slide = 1.0f;
    G.spread = fminf(G.spread + 1.0f, 2.0f);
    G.kickPitch.v += 1.1f;                    // camera kicks up ~2 degrees and settles
    G.kickYaw.v += (Rnd() - 0.5f) * 0.5f;     // with a small sideways twitch
    G.fovPunch.v += 1.2f;                     // field of view widens for a beat
    G.xhKick.v += 14.0f;                      // crosshair pops
    Input_Rumble(0.30f, 0.80f, 0.12f);
    Sound_Play(SND_SHOT, 0.85f + 0.15f * Rnd(), 0.94f + 0.12f * Rnd());
    {
        Vec3 me;
        if (PlayerPos(&me)) React_PanicAround(me, kFirePanic);   // a gunshot: the people near him run
    }
    if (!G.haveGun) return;
    Vec3 muzzle = XformPoint(MUZZLE, G.gunWorld);
    Vec3 barrel = Norm(XformDir(V3(0, 0, 1), G.gunWorld));
    Vec3 dir = barrel;
    if (G.haveAim && G.aim > 0.5f) {
        Vec3 toAim = Norm(G.aimPoint - muzzle);
        if (Dot(toAim, barrel) > 0.85f) dir = toAim;
    }
    // the hit system: where the shot goes, what it hits and how that reacts, in the game's own
    // collision and rules
    const WeaponSpec& w = g_testShots > 0 ? g_testSpec : kHandgun;
    if (g_testShots > 0) g_testShots--;
    ShotOrigin so = {};
    Vec3 feet;
    so.shooter = g_player;
    so.body = PlayerPos(&feet) ? feet + UP * 1.25f : muzzle - barrel * 0.4f;
    so.muzzle = muzzle;
    so.dir = dir;
    so.aimed = G.aiming && G.camAim > 0.5f;
    so.eye = G.camPos;
    so.eyeDir = G.camFwd;
    ShotResult res[kMaxPellets];
    int n = Trace_Available() ? FireWeapon(w, so, res, kMaxPellets) : 0;
    if (g_shotLog > 0) {
        g_shotLog--;
        char line[200] = "no game collision";
        if (n > 0) Trace_Describe(res[0].hit, line, sizeof(line));
        Log("shot: %s, arm up %.2f, barrel pitch %.1f deg, shot pitch %.1f deg | hit %s | probe %.2f m (age %.2f s)",
            G.aiming ? "aimed" : "hip", G.aim, asinf(fmaxf(-1.0f, fminf(1.0f, barrel.y))) * 57.3f,
            asinf(fmaxf(-1.0f, fminf(1.0f, dir.y))) * 57.3f, line, G.hitDist, G.hitAge);
    }
    if (G.nflash < 6) G.flash[G.nflash++] = {muzzle + dir * 0.03f, dir, 0, Rnd() * 2 * PI, 0.9f + Rnd() * 0.3f, 1.0f};
    for (int i = 0; i < 3 && G.nsmoke < 64; i++)
        G.smoke[G.nsmoke++] = {muzzle + dir * (0.05f + i * 0.04f), dir * (0.6f - i * 0.15f) + V3(0, 0.25f, 0), 0,
                               0.55f + Rnd() * 0.3f, Rnd() * 2 * PI, 0.30f + i * 0.08f};
    // each pellet: a tracer to where it went, and the gun's own impact where it hit (the game's
    // reaction already happened in FireWeapon)
    for (int i = 0; i < n; i++) {
        const ShotResult& r = res[i];
        Vec3 path = r.to - r.from, td = dir;
        float d = Len(path);
        if (d > 0.05f && Dot(path, dir) > 0.0f) td = path * (1.0f / d);   // (a hit behind the muzzle: no backwards tracer)
        if (r.hit.kind != HIT_NONE) Impact(r.hit.pos, r.hit.normal, Hit_Surface(r.hit), r.gameFx);
        if (G.ntracers < 24) G.tracers[G.ntracers++] = {muzzle + td * 0.1f, td, 0, 0, d};
    }
    float range = w.range;
    if (n == 0 && G.shotHits && G.aim > 0.5f) {   // no game collision: what the depth probe saw under the crosshair
        Vec3 toHit = G.shotPoint - muzzle;
        float d = Len(toHit);
        if (d > 0.2f && Dot(toHit * (1.0f / d), barrel) > 0.85f) {
            dir = toHit * (1.0f / d);
            range = d;
            bool soft = fabsf(G.shotPoint.y - g_ground) < 0.12f;   // the lawn or the road under his feet
            Impact(G.shotPoint, dir * -1.0f, soft ? SURF_GRASS : SURF_HARD, false);
        }
    }
    if (n == 0 && G.ntracers < 8) G.tracers[G.ntracers++] = {muzzle + dir * 0.1f, dir, 0, 0, range};
    if (G.nshells < 16) {
        Vec3 right = Norm(XformDir(V3(1, 0, 0), G.gunWorld));
        Vec3 port = XformPoint(EJECT, G.gunWorld);
        Shell& s = G.shells[G.nshells++];
        s.kind = 0;
        s.landed = false;
        s.p = port;
        s.v = right * (1.6f + Rnd() * 0.6f) + UP * (2.0f + Rnd() * 0.6f) - barrel * 0.4f;
        s.axis = Norm(V3(Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f));
        s.ang = 0;
        s.spin = 18 + Rnd() * 12;
        s.t = 0;
        s.ground = g_ground;
    }
}

// The rocket launcher: one rocket at a time, a real projectile (rocket.cpp) that explodes where
// it lands (explode.cpp). A direct hit wrecks a car that can be wrecked; the blast reaches 7 m,
// shoving cars as hard as the game's own car explosion (20000 N) and knocking people flying.
static const WeaponSpec kRocket = {
    "rpg",
    150.0f, 0.07f,          // range, the rocket's thickness
    1, 0.0f,                // one rocket, dead on
    1.0f, 20000.0f,         // vehicles: a direct hit wrecks one; the shove of the game's car explosion
    900.0f, 0.95f,          // pedestrians: knocked flying, 55 degrees up
    4000.0f, true,          // props: pushed hard, breakables break
    true,                   // Hit & Run
    0.0f,                   // no bullet holes
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    7.0f,                   // blast radius
};
static const float kRpgReload = 1.6f;   // seconds before the next rocket is in
const WeaponSpec* Gun_RocketSpec() { return &kRocket; }

static void FireRPG()
{
    if (!G.rpgLoaded || !G.haveLch) return;
    G.cooldown = kRpgReload;
    G.rpgLoaded = false;
    // recoil: the launcher shoves back along the tube and its muzzle kicks up, he rocks back, and
    // the camera takes a big kick
    G.lchBack.v += 1.8f;
    G.lchFlip.v += 2.6f;
    G.kickChest.v += 2.2f;
    G.kickPitch.v += 2.8f;
    G.kickYaw.v += (Rnd() - 0.5f) * 0.9f;
    G.fovPunch.v += 2.6f;
    G.xhKick.v += 20.0f;
    Input_Rumble(0.85f, 1.0f, 0.35f);
    Sound_Play(SND_ROCKET, 1.0f, 0.95f + 0.1f * Rnd());
    {
        Vec3 me;
        if (PlayerPos(&me)) React_PanicAround(me, kFirePanic + 4.0f);   // the launch's roar
    }
    // the launcher as drawn (scaled to him), without the draw "pop": a rocket fired while the
    // launcher was still popping out flew its whole way up to 11 % too big
    Mat4 L = g_skin.haveLauncher ? Mul(ScaleM(g_skin.lchScale), g_skin.launcher) : G.lchWorld;
    float scale = Len(Row(L, 0));
    Vec3 origin = Row(L, 3);      // on his shoulder
    Vec3 start = XformPoint(V3(0, 0, kRktLoadedZ), L), muzzle = XformPoint(V3(0, 0, kLchMuzzleZ), L);
    Vec3 back = XformPoint(V3(0, 0, kLchBackZ), L), barrel = Norm(XformDir(V3(0, 0, 1), L));
    // where it goes: aiming, at what's under the crosshair; from the hip, along the tube
    Vec3 dir = barrel;
    HitResult aimHit = {}, stop = {};
    bool haveStop = false;
    if (G.aiming && G.camAim > 0.5f) {
        ShotOrigin so = {};
        so.shooter = g_player;
        so.body = origin;
        so.aimed = true;
        so.eye = G.camPos;
        so.eyeDir = G.camFwd;
        Vec3 target;
        Weapon_AimPoint(kRocket, so, &target, &aimHit);
        // (v1.0, vcover) what's drawn under the crosshair, as the handgun's and shotgun's shots have it: a building's piece
        // the game's collision lacks is where the rocket goes and stops (people and cars the trace missed are left to the
        // rocket's own trace: they move)
        if (VCover_Ready(kRocket.pellets, kRocket.spread)) {
            Vec3 veye, vdir, vn;
            float vis;
            VCover_Pellet(0, &veye, &vdir, &vis, &vn);
            float skip = fmaxf(0.0f, Dot(origin - veye, vdir) - 0.5f);
            if (VCover_Decide(veye, vdir, vis, skip, aimHit, g_player) == 1) {
                stop.kind = HIT_WORLD, stop.pos = veye + vdir * vis, stop.normal = vn, stop.dist = vis;
                stop.ground = stop.volume = -1, stop.drawn = true;
                target = stop.pos, haveStop = true;
            }
            VCover_Used();
        }
        Vec3 to = target - start;
        float d = Len(to);
        if (d > 0.5f && Dot(to * (1.0f / d), barrel) > 0.85f) dir = to * (1.0f / d);
        else haveStop = false;   // (not where the tube can send it)
    } else if (G.haveAim && G.aim > 0.5f) {
        // the tube isn't level yet (from the hip it fires with the arm 85 % up, or the aim camera
        // is still coming in): like the handgun, go for the aim point, not along the tube, which
        // still pointed several degrees up and sent the rocket over cars
        Vec3 toAim = Norm(G.aimPoint - start);
        if (Dot(toAim, barrel) > 0.85f) dir = toAim;
    }
    Rocket_Fire(kRocket, origin, start, dir, scale, g_player, haveStop ? &stop : nullptr);
    if (g_shotLog > 0) {
        g_shotLog--;
        char line[200] = "nothing";
        if (aimHit.kind != HIT_NONE) Trace_Describe(aimHit, line, sizeof(line));
        Log("shot: rpg %s, launcher up %.2f, dir (%.2f %.2f %.2f) | crosshair on %s%s", G.aiming ? "aimed" : "hip", G.aim, dir.x,
            dir.y, dir.z, line, haveStop ? " - drawn in front of it: stops there" : "");
    }
    // the blast out of the muzzle and the backblast out of the back end: flame, cartoon smoke, and
    // the game's own dust puff behind him
    if (G.nflash < 6) G.flash[G.nflash++] = {muzzle + dir * 0.05f, dir, 0, Rnd() * 2 * PI, 2.4f * scale, 2.6f};
    if (G.nflash < 6) G.flash[G.nflash++] = {back - barrel * 0.05f, barrel * -1.0f, 0, Rnd() * 2 * PI, 2.8f * scale, 3.4f};
    for (int i = 0; i < 4 && G.nsmoke < 64; i++)
        G.smoke[G.nsmoke++] = {muzzle + dir * (0.10f + i * 0.08f), dir * (2.0f - i * 0.4f) + V3(Rnd() - 0.5f, 0.4f, Rnd() - 0.5f) * 0.6f,
                               0, 0.6f + Rnd() * 0.3f, Rnd() * 2 * PI, (0.30f + i * 0.06f) * scale};
    Vec3 rgt = Norm(Cross(UP, barrel));
    for (int i = 0; i < 10 && G.nsmoke < 64; i++) {
        // back and out to his left, away from the aiming camera over his right shoulder (a cloud
        // drifting over the camera hid him for seconds)
        Vec3 v = barrel * -(2.5f + Rnd() * 3.0f) - rgt * (1.8f + Rnd() * 1.6f) + V3(Rnd() - 0.5f, Rnd() * 0.6f, Rnd() - 0.5f) * 1.2f;
        G.smoke[G.nsmoke++] = {back - barrel * (0.05f + 0.04f * i), v, 0, 0.55f + Rnd() * 0.45f, Rnd() * 2 * PI,
                               (0.30f + Rnd() * 0.18f) * scale};
    }
    // the game's dust puff, a third of a car's tyre smoke, out behind and to his left
    Fx_Particle(9, back - barrel * 0.6f - rgt * 0.5f, Norm(barrel * -1.0f - rgt * 0.7f), 0.33f);
}

static DWORD Lit(Vec3 col, Vec3 n, float metal, Vec3 V);   // (drawing, below)
static void DrawLitMesh(Renderer& r, const MeshBuf& m, const Mat4& world, Vec3 viewDir);
static float g_gunSlow = 1.0f;   // debug "gunslow <scale>": the mod's own animations at this speed (with "slowmo": slow-motion footage)
#include "face.inc"      // his head and body in numbers, and a shut eye (the shotgun's wink)
#include "bodymesh.inc"  // his real mesh (from the game's files), skinned with our pose: the guns fit against his skin
#include "shotgun.inc"   // weapon 1 of the Weapons Lab: the pump-action shotgun
#include "donut.inc"     // item 1 of the Weapons Lab: the pink sprinkled donut
#include "drunk.inc"     // item 2: drunk mode - how drunk he is, and what it does to him
#include "ragdoll.inc"   // item 2: falling down drunk - a ragdoll of our own (no longer used for Homer: drunkbody.inc)
#include "drunkbody.inc" // item 2, refined: the drunk body - his walk on his own feet, and a fall that keeps his shape
#include "beer.inc"      // item 2: a can of beer

// he's gone over (ragdoll.inc): whatever was in his hand goes - a can flies out of it, the rest is just gone
static void Gun_DropForFall()
{
    if (G.held == WPN_BEER && g_br.have && g_br.running) {
        Mat4 R = Rot3(g_br.world);
        for (int i = 0; i < 3; i++) SetRow(R, i, Norm(Row(R, i)), 0);
        BrToss(WithTrans(R, Row(g_br.world, 3)), g_br.vel + UP * 1.5f, g_br.k, g_br.crush);
        g_br.running = false, g_br.done = true;
    }
    if (G.held == WPN_DONUT) g_dn.running = false, g_dn.done = true;
    G.weapon = WPN_NONE;
    G.held = WPN_NONE;
    G.show = 0.0f;
    G.aim = 0.0f;
}
#include "posetrace.inc" // (debug) every frame's pose for the offline animation QA

// the trigger, for the weapon in his hand
static void Fire()
{
    if (G.held == WPN_RPG) FireRPG();
    else if (G.held == WPN_SHOTGUN) FireShotgun();
    else FireHandgun();
}

// ------------------------------------------------------------------ per frame
static int g_aimMove = 2;   // debug "aimmove": 0 stand still to aim (as before), 1 stick not blocked (nothing else), 2 walk while aiming
static float g_mouseLead = 1.2f;   // debug "mouselead": how far the mouse may take the view ahead of his body
static int g_mouseLog;             // debug "mouselog <frames>"

static void DaUpdate(float dt, const Pad& pad, const GameState& gs, const Kbm& kb, bool wheelOpen, bool aimHeld, bool fireHeld, int* block, WORD* blockButtons);   // (diagnostic, m3)
static void DaFromIni();
void Gun_Update(float dt, const Pad& pad, const GameState& gs, bool wheelOpen)
{
    dt *= g_gunSlow;
    g_drvInCar = gs.inCar && gs.player;   // (diagnostic)
    {   // (m3.1) "DriveBy=1" in the ini: aiming from the car on from the start, no debug commands needed
        static bool once;
        if (!once && g_cfg.driveBy) DaFromIni();
        once = true;
    }
    BuildGun();
    BuildLauncher();
    BuildRocket();
    if (!G.rng) G.rng = GetTickCount() | 1;
    bool onFoot = gs.onFoot;

    // show / hide the weapon in hand; no idle fidgets while it's out. Switching puts the old
    // one away first, then the new one comes out.
    bool armed = G.weapon != WPN_NONE;
    if (armed && G.held != G.weapon && G.show <= 0.0f) {
        G.held = G.weapon;
        G.popT = 0;
        G.cooldown = 0;   // one weapon's wait (the RPG's reload) doesn't hold up the next
    }
    if (G.held == WPN_RPG && G.show <= 0.0f) G.rpgLoaded = true;   // it comes out loaded
    // (vcover) aimed hitscan weapons: the next shot's pellet rays are read from the depth buffer every frame
    if (G.camAim > 0.3f && (G.held == WPN_HANDGUN || G.held == WPN_SHOTGUN || G.held == WPN_RPG) && onFoot) {   // (v1.0: the RPG too)
        const WeaponSpec& cw = G.held == WPN_SHOTGUN ? kShotgun : G.held == WPN_RPG ? kRocket : g_testShots > 0 ? g_testSpec : kHandgun;
        VCover_Arm(cw.pellets, cw.spread);
    }
    float want = (armed && G.held == G.weapon && onFoot) ? 1.0f : 0.0f;
    if (armed && onFoot) Game_HoldIdleTimer(gs.player);
    // (only ever towards `want`: easing down whenever it isn't below made a fully shown gun
    // drop to 0.88 every few frames - the fist, the carry pose and the gun size twitched)
    // The shotgun's draw (out of the pocket, into both hands, the pump) and holster are whole animations: `show` runs
    // through them (shotgun.inc)
    bool sg = G.held == WPN_SHOTGUN;
    float before = G.show;
    if (want > G.show)
        G.show = fminf(want, G.show + dt / (sg ? kSgDrawTime : 0.30f));
    else if (want < G.show) {
        // (the shotgun comes down out of the aim first, then goes away: the holster used to start from the aim and the gun jumped
        // 70 cm in one frame - the pose QA's stress run)
        bool lowerFirst = sg && onFoot && G.aim > 0.03f;
        if (!lowerFirst) G.show = fmaxf(want, G.show - dt / (sg && onFoot ? kSgHolsterTime : 0.14f));   // (into a car: put away at once)
    }
    if (sg) {
        G.sgHolster = want < G.show || (want < 0.5f && G.show < before);
        if (!G.sgHolster && before < 0.17f && G.show >= 0.17f) Sound_Play(SND_EQUIP, 0.8f, 0.72f);   // his hand closes on it in the pocket
        if (!G.sgHolster && before < 0.68f && G.show >= 0.68f) Sound_Play(SND_PUMP, 0.95f, 1.0f);    // the rack at the end of the draw
        if (G.sgHolster && before >= 0.999f && G.show < 0.999f) Sound_Play(SND_HOLSTER, 0.9f, 0.75f);
    } else
        G.sgHolster = false;
    SgUpdate(dt, sg && G.show >= 0.999f && G.camAim > 0.5f && onFoot);
    // knocked down while he eats or drinks (a car, a punch - the game's own flail): it goes - the can flies out of his hand and lands,
    // the donut bursts into crumbs where it was (in his hand through the whole knockdown before - donut v1's open issue 3)
    if (G.knocked && onFoot && ((G.held == WPN_DONUT && g_dn.running && !g_dn.aborting) || (G.held == WPN_BEER && g_br.running))) {
        if (G.held == WPN_DONUT && g_dn.have) DnCrumbs(Row(g_dn.world, 3), UP, 16);
        Log("knocked down: the %s dropped", G.held == WPN_DONUT ? "donut" : "can");
        Gun_DropForFall();
    }
    // the donut runs `show` itself: on while he eats it (or puts it back), off when it's done (donut.inc)
    DnUpdate(dt, onFoot);
    if (G.held == WPN_DONUT) G.show = g_dn.running ? 1.0f : 0.0f;
    BrUpdate(dt, onFoot);   // (beer.inc: the same)
    {
        double t0 = ProfNow();
        g_drunkPaused = gs.context == 10 || gs.nextContext == 10 || gs.inputState == 2;   // (as the rockets' "paused" below)
        Drunk_Update(dt, onFoot, gs.player);
        if (Drunk_Active()) PerfAdd(2, ProfNow() - t0);
        // (the perf window: the frame's time, and the report when it ends)
        if (g_perf.until > 0.0) {
            double now = ProfNow();
            if (g_perf.t0 > 0.0) PerfAdd(4, now - g_perf.t0);
            g_perf.t0 = now;
            if (now >= g_perf.until) {
                for (int k = 0; k < kPerfN; k++)   // (a pose can be built more than once a frame: its cost a frame is the sum over the frames)
                    if (g_perf.n[k]) Log("perf: %-12s %.3f ms a frame (%.3f a call, worst call %.3f; %d calls, %d frames)", kPerfName[k],
                                         g_perf.n[4] ? g_perf.sum[k] / g_perf.n[4] : 0.0, g_perf.sum[k] / g_perf.n[k], g_perf.mx[k], g_perf.n[k], g_perf.n[4]);
                if (g_perf.n[4]) Log("perf: %.1f frames a second", 1000.0 * g_perf.n[4] / g_perf.sum[4]);
                g_perf = {};
            }
        }
    }
    if (G.held == WPN_BEER) G.show = g_br.running ? 1.0f : 0.0f;
    G.popT += dt;

    // input: configurable buttons (WeaponWheel.ini AimButton / FireButton), triggers read
    // with the backwards-trigger calibration
    bool ltDown = TrigDown(G.lt, pad.lt, pad.connected), rtDown = TrigDown(G.rt, pad.rt, pad.connected);
    auto held = [&](uint32_t mask) {
        return (pad.buttons & (mask & 0xFFFF)) != 0 || ((mask & BTN_LT) && ltDown) || ((mask & BTN_RT) && rtDown);
    };
    const Kbm& kb = Kbm_Get();
    Kbm_AimAllowed(armed && onFoot && !wheelOpen && G.held != WPN_DONUT && G.held != WPN_BEER);
    bool lt = held(g_cfg.aimButtons) || kb.aim || G.forceAim;   // + KeyAim / KeyFire (right / left mouse)
    bool rt = held(g_cfg.fireButtons) || kb.fire;
    if (G.forceFire > 0) {
        rt = true;
        G.forceFire--;
    }
    // the D-pad also changes the wheel's slot 1: after the wheel, aim only on a fresh press. The
    // same after a car (LT can be the brake): holding it while getting out isn't aiming
    if (wheelOpen || gs.inCar) G.aimLatch = true;
    if (G.aimLatch && !lt) G.aimLatch = false;
    if (G.aimLatch && !G.forceAim) lt = false;

    // knocked down, falling or teleported: hold off for a moment
    Vec3 pos;
    if (PlayerPos(&pos)) {
        if (G.haveLastPos && dt > 0.0001f) {
            Vec3 dv = pos - G.lastPos;
            dv.y = 0;
            float sp = fminf(Len(dv) / dt, 20.0f);
            G.speed += (sp - G.speed) * (1.0f - expf(-dt * 8.0f));
            Vec3 v = sp > 0.0f ? dv * (fminf(Len(dv) / dt, 20.0f) / Len(dv)) : V3(0, 0, 0);
            G.vel = G.vel + (v - G.vel) * (1.0f - expf(-dt * 8.0f));
        }
        // (over 2 m in a frame, and faster than he can run: running through a loading hitch of a
        // few tenths of a second isn't a jolt - it dropped the arm mid-burst)
        double nowT = Time_Real();
        float jump = G.haveLastPos ? Len(pos - G.lastPos) : 0.0f;
        float rdt = G.lastPosT > 0 ? fmaxf((float)(nowT - G.lastPosT), dt) : dt;
        bool jolt = G.knocked || (G.haveLastPos && ((jump > 2.0f && jump > 15.0f * rdt) ||
                                                     (pos.y - G.lastPos.y) / fmaxf(dt, 1e-3f) < -8.0f));
        // moved somewhere else in an instant (a mission restart - done while paused - or a building's
        // door): rockets and explosions from before don't belong there. Nothing moves while paused,
        // so any jump during the menu counts (a restart near where he stood wasn't caught)
        bool menu = gs.context == 10 || gs.nextContext == 10 || gs.inputState == 2;
        if (G.haveLastPos && ((jump > 3.0f && jump / fmaxf(dt, 1e-3f) > 80.0f) || (menu && jump > 0.5f))) {
            if (Rocket_Any() || Explode_Any()) Log("teleported: rockets and explosions cleared");
            Rocket_Clear();
            Explode_Clear();
            React_Clear();
        }
        G.lastPos = pos;
        G.lastPosT = nowT;
        G.haveLastPos = true;
        if (jolt) G.suspend = 0.35f;
    }
    G.suspend = fmaxf(0.0f, G.suspend - dt);
    // (no aiming or firing while the game walks him to a car door and climbs him in: the aim camera
    // stayed on through it, inside his head as he turned to the door, with the crosshair up)
    bool active = armed && G.held == G.weapon && onFoot && !wheelOpen && G.show > (G.held == WPN_SHOTGUN ? 0.999f : 0.5f) &&
                  G.suspend <= 0 && !Game_ScriptedMove(gs.player) && G.held != WPN_DONUT &&
                  G.held != WPN_BEER;   // (no aiming a donut, or a beer)

    G.hitAge += dt;
    // rockets in flight and explosions run on game time (slow motion slows them); they wait while
    // the game is paused or in a menu, and go away in cutscenes and when the level changes. They
    // keep flying if he gets into a car.
    {
        // (pausing is a frame of context 9 going to 10, and coming back one of 10 going to 9 with the
        // menu's input: both are "paused", or a rocket vanished on Escape)
        bool play = gs.context == 9 && gs.nextContext == 9 && (gs.inputState == 1 || gs.inputState == 0xFFFFFFFFu);
        bool paused = gs.context == 10 || gs.nextContext == 10 || gs.inputState == 2;
        bool hold = !play && (gs.context == 9 || gs.context == 10) && paused;
        Sky_Update(paused ? 0.0f : dt, gs.player);   // the sky's own animation stops in the pause menu too
        float gdt = dt * (float)Time_GetScale();
        Rocket_Update(gdt, gs.player, play, hold);
        Explode_Update(gdt, gs.player, play, hold);
        React_Update(gdt, play);   // people knocked down out of the game's physics
        CarGlass_Update(paused ? 0.0f : gdt);   // broken glass falling
        Tires_Update(paused ? 0.0f : gdt);      // flat tires going down, and how the car drives on them
        Drunk_Car(paused ? 0.0f : gdt, gs, paused);   // drunk driving (drunk.inc)
        float shake = Explode_TakeShake();
        if (shake > 0.0f) {   // an explosion near him: the camera jolts and the pad rumbles
            G.kickPitch.v += (Rnd() - 0.35f) * 4.0f * shake;
            G.kickYaw.v += (Rnd() - 0.5f) * 3.0f * shake;
            G.fovPunch.v += 1.5f * shake;
            Input_Rumble(0.9f * shake, 1.0f * shake, 0.4f + 0.3f * shake);
        }
    }
    if (g_dbgCamSpin != 0 && !G.aiming) {
        float gw[16], fov;
        if (Game_GetGameCamera(gw, &fov)) Game_SetFollowCamHeading(atan2f(gw[8], gw[10]) + g_dbgCamSpin * dt);
    }
    if (g_actLog > 0) {
        g_actLog--;
        static uint32_t s_lastVt = 1;
        uint32_t act = Game_CurrentAction(gs.player), vt = 0;
        if (act) ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)act, &vt, 4, nullptr);
        if (vt != s_lastVt) Log("action: %s (aiming %d camAim %.2f show %.2f inCar %d onFoot %d)", Game_ClassName(act), G.aiming,
                                G.camAim, G.show, gs.inCar, gs.onFoot);
        s_lastVt = vt;
    }
    // debug "fps": frame rate over the next second ("fps <seconds>": a line every second for that long, m3.1)
    if (g_fpsLeft > 0) {
        g_fpsLeft -= dt;
        g_fpsFrames++;
        g_fpsT += dt;
        if (g_fpsT >= 1.0f || g_fpsLeft <= 0) {
            Log("fps: %.1f (aiming %d, from the car %.2f, probe %s, in a car %d)", g_fpsFrames / fmaxf(g_fpsT, 0.001f), G.aiming, DaCamAim(),
                G.hitAge < 0.1f ? "on" : "off", g_drvInCar);
            g_fpsT = 0, g_fpsFrames = 0;
        }
    }
    G.cooldown -= dt;
    // the rocket launcher loads its next rocket once the last one is away (after the
    // countdown: a press queued in the last moment of the reload fired into an empty tube)
    if (G.held == WPN_RPG && !G.rpgLoaded && G.cooldown <= 0.0f && G.show > 0.5f) {
        G.rpgLoaded = true;
        G.reloadT = 0;
        Sound_Play(SND_EQUIP, 0.45f, 1.4f);
    }
    G.reloadT += dt;
    if (rt && !G.rtPrev) G.fireQueued = 0.15f;   // a press during the cooldown still fires
    G.fireQueued -= dt;
    if (active) {
        G.aiming = lt;
        if (G.fireQueued > 0 && G.cooldown <= 0) {
            G.fireQueued = 0;
            // From the hip, every shot keeps the arm up a while longer, and a shot waits for the
            // arm to be (nearly) up: a press while it was coming down used to fire along a
            // barrel still pointing at the ground, and the arm dropped right after a quick shot.
            bool now = G.aim > (G.aiming ? 0.75f : kHipReady) && G.reach.x > 0.6f;
            if (!G.aiming || !now) G.hipTimer = kHipHold;
            if (now)
                Fire();
            else
                G.pendingShot = true;
        }
    } else {
        G.aiming = false;
        G.pendingShot = false;
        G.hipTimer = 0;
        G.fireQueued = 0;
    }
    G.rtPrev = rt;

    bool armUp = active && (G.aiming || G.hipTimer > 0);
    G.sgRaising = armUp;
    if (active && G.aiming && G.camAim < 0.05f) {   // every aim starts level, facing the camera's way
        G.pitch = -0.04f;
        float gw[16], fov;
        if (Game_GetGameCamera(gw, &fov)) G.yaw = atan2f(gw[8], gw[10]);
        if (g_orbitDist > 0.1f && G.haveBody) G.yaw = G.bodyYaw;   // (debug orbit camera in front of him: start along his own facing,
                                                                      // or he turned to the debug camera for a few frames)
        G.bodyAligned = false;
        G.yawVel = G.pitchVel = 0;
        // the camera rig kept the last aim's squeeze (a wall, looking up) while not aiming; start
        // from what the walls allow in the new direction, not from there pulling slowly back out
        G.camFree = G.camSideK = 1e6f;
        if (G.aim < 0.05f)   // (arm already up from a hip shot: let the spring carry it, no drop for a frame)
            G.reach = Spring{G.haveBody ? ReachFor(WrapPi(G.yaw - G.bodyYaw), kBlade, nullptr) : 0.0f, 0.0f};
    }
    if (active && G.aiming) {
        float sx, sy;
        ShapeAimStick(pad.rx, pad.ry, g_cfg.aimDeadzone, &sx, &sy);
        float wantYaw = sx * 2.6f * g_cfg.aimSens;
        float wantPitch = sy * 1.6f * g_cfg.aimSens * (g_cfg.invertY ? -1.0f : 1.0f);
        if (g_dbgYawRate != 0) wantYaw = g_dbgYawRate;   // debug: fake stick turn
        // the view may lead his body only a little; past that, turning waits for him
        if (G.haveBody) {
            float lead = WrapPi(G.yaw - G.bodyYaw);
            if (fabsf(lead) < kSoftLead) G.bodyAligned = true;
            if (G.bodyAligned && lead * wantYaw > 0)
                wantYaw *= 1.0f - Smooth01((fabsf(lead) - kSoftLead) / (kMaxLead - kSoftLead));
        }
        // ease into the up / down limits
        wantPitch *= Smooth01((wantPitch > 0 ? kPitchUp - G.pitch : G.pitch - kPitchDown) / 0.12f);
        auto ramp = [&](float& v, float w) {   // ~45 ms to speed up, ~25 ms to stop
            v += (w - v) * (1.0f - expf(-dt * (fabsf(w) > fabsf(v) ? 22.0f : 40.0f)));
        };
        ramp(G.yawVel, wantYaw);
        ramp(G.pitchVel, wantPitch);
        G.yaw = WrapPi(G.yaw + G.yawVel * dt);
        G.pitch = fminf(kPitchUp, fmaxf(kPitchDown, G.pitch + G.pitchVel * dt));
        // the mouse: straight onto the aim, one to one (raw counts: no smoothing, no acceleration,
        // the same at any frame rate)
        if (kb.mdx != 0 || kb.mdy != 0) {
            float k = 0.022f * (PI / 180.0f) * g_cfg.mouseSens;
            float dyaw = kb.mdx * k, dpitch = -kb.mdy * k * (g_cfg.mouseInvertY ? -1.0f : 1.0f);
            if (G.haveBody) {   // he turns after the view: it may lead him only so far
                // (and while he is still turning to it - or the game has turned him away, a jump, a kick -
                // no further than it is: the view ran round to his face and "stuck" there)
                float before = WrapPi(G.yaw - G.bodyYaw);
                float limit = G.bodyAligned ? g_mouseLead : fmaxf(g_mouseLead, fabsf(before));
                float lead = WrapPi(G.yaw + dyaw - G.bodyYaw);
                if (fabsf(lead) > limit && lead * dyaw > 0) {
                    float room = fmaxf(0.0f, limit - fabsf(before));
                    dyaw = dyaw > 0 ? fminf(dyaw, room) : fmaxf(dyaw, -room);
                }
            }
            G.yaw = WrapPi(G.yaw + dyaw);
            G.pitch = fminf(kPitchUp, fmaxf(kPitchDown, G.pitch + dpitch));
            if (g_mouseLog > 0) {
                g_mouseLog--;
                Log("mouselog: %+.0f %+.0f -> yaw %.3f pitch %.3f lead %.3f", kb.mdx, kb.mdy, G.yaw, G.pitch,
                    G.haveBody ? WrapPi(G.yaw - G.bodyYaw) : 0.0f);
            }
        }
    } else {
        G.yawVel = G.pitchVel = 0;
    }
    // debug "aimat": point the crosshair at the nearest character's chest / vehicle (a test
    // hook for the hit system, like walkto; the camera follows the aim, so it settles in a few frames)
    if (g_dbgAimAt && active && G.aiming && G.camAim > 0.5f && PlayerPos(&pos)) {
        float tp[64][3];
        char names[30][24];
        int types[30];
        int n = g_dbgAimAt == 1 ? Game_ListCharacters(tp, 64) : g_dbgAimAt == 7 ? React_ListActors(g_player, 80.0f, tp, 16)
                : (g_dbgAimAt >= 4 && g_dbgAimAt != 8) ? 0 : Game_ListVehicles(tp, names, 30, types);
        float bestD = 1e9f;
        Vec3 tgt = {};
        for (int i = 0; i < n; i++) {
            if (g_dbgAimAt == 3 && types[i] != 2) continue;   // traffic only
            if (g_dbgAimAt == 8 && strncmp(names[i], g_dbgAimCar, strlen(g_dbgAimCar))) continue;   // by name
            Vec3 c = V3(tp[i][0], tp[i][1] + (g_dbgAimAt == 1 ? 1.0f : g_dbgAimAt == 7 ? 0.0f : 0.2f), tp[i][2]);
            float d = Len(c - pos);
            if (d > (g_dbgAimAt == 1 ? g_dbgAimMin : 1.5f) && d < bestD) bestD = d, tgt = c;
        }
        if (g_dbgAimAt == 4) bestD = 0, tgt = g_dbgAimPoint;
        if (g_dbgAimAt == 9) {
            uint32_t cm = Game_CharacterManager();
            const float* cp = cm ? *(const float* const*)(uintptr_t)(cm + 192 + 4 * g_dbgAimChar) : nullptr;
            if (cp) bestD = 0, tgt = *(const Vec3*)((const uint8_t*)cp + 0x64) + V3(0, 0.9f, 0);
        }
        Vec3 sd;
        if ((g_dbgAimAt == 5 || g_dbgAimAt == 6) && Sky_PlaneDir(g_dbgAimAt - 5, &sd)) bestD = 0, tgt = G.camPos + sd * 100.0f;
        if (bestD < 1e8f) {
            Vec3 d = tgt - G.camPos;
            G.yaw = atan2f(d.x, d.z);
            G.pitch = fminf(kPitchUp, fmaxf(kPitchDown, atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z))));
        }
    }
    // critically damped springs: ease in and out, no jump on the first frame
    bool camUp = active && G.aiming;
    StepTo(G.aimS, armUp ? 1.0f : 0.0f, armUp ? 24.0f : 13.0f, 1.0f, dt);
    StepTo(G.camS, camUp ? 1.0f : 0.0f, camUp ? 17.0f : 12.0f, 1.0f, dt);
    StepTo(G.reach, G.reachTarget, 20.0f, 1.0f, dt);
    // carry pose: full when standing, about half while running (some arm swing comes back)
    // knocked over (a car, a blast): the low-ready hold is built from world up and his heading, so it
    // pointed the arm into the ground while he lay there - let the game's tumble have the arm (the
    // gun stays in his hand)
    StepTo(G.knockW, G.knocked ? 1.0f : 0.0f, 9.0f, 1.0f, dt);
    StepTo(G.carryW, (1.0f - 0.55f * Smooth01((G.speed - 0.4f) / 4.0f)) * (1.0f - Clamp01(G.knockW.x)), 9.0f, 1.0f, dt);
    if (!onFoot) G.aimS = G.camS = Spring{0, 0};   // cutscene, car, pause menu: straight back to the game
    G.aim = (!armUp && G.aimS.x < 0.001f) ? 0.0f : Clamp01(G.aimS.x);
    G.camAim = (!camUp && G.camS.x < 0.001f) ? 0.0f : Clamp01(G.camS.x);

    // While aiming he turns with the aim (characters face -Z of their root joint, hence the
    // flipped heading) - but only while his walking action is in charge; jumps, kicks,
    // getting up or walking to a car door steer him themselves. The game's own follow
    // camera (hidden behind ours) is kept swung round the same way, so letting go hands
    // back to a camera already behind him.
    // Walking while aiming: the left stick moves him (the game's walk, speed and collision) while he
    // keeps facing the aim (Game_AimMove). Debug "aimmove 0" brings back standing still to aim.
    bool aimWalk = g_aimMove >= 2 && Game_AimMoveAvailable();
    if (active && G.aiming) {
        if (Game_WalkerActive(gs.player)) Game_SetDesiredDir(gs.player, WrapPi(G.yaw + PI));
        Game_SetFollowCamHeading(G.yaw);
    }
    g_aimWalking = active && G.aiming && aimWalk;
    Game_AimMove(active && G.aiming && aimWalk ? gs.player : 0, WrapPi(G.yaw + PI), pad.lx, pad.ly);
    // the legs: toward where he is going, relative to where he faces (forward, the sides), or
    // stepping backwards when he walks away from the aim (a little overlap, so a sideways walk
    // doesn't flip between the two)
    {
        float sp = sqrtf(G.vel.x * G.vel.x + G.vel.z * G.vel.z);
        float moving = Smooth01((sp - 0.35f) / 0.9f);
        // (drunk: his staggers are side and back steps under a body facing his way - the same legs; drunk.inc)
        static float drunkLegs = 0.0f;
        extern bool g_dwHybrid;   // (v1.0: no side or back steps - a stagger bends his walk: game.cpp)
        bool staggering = onFoot && Drunk_Staggering() && G.haveBody && !DrunkBody_On() && !g_dwHybrid;   // (the drunk body steps for itself)
        drunkLegs = Clamp01(drunkLegs + (staggering ? dt * 6.0f : -dt * 3.0f));
        bool on = (active && G.aiming && aimWalk && G.haveBody) || drunkLegs > 0.0f;
        float yawT = 0.0f, backT = 0.0f;
        if (on && sp > 0.2f) {
            float rel = WrapPi(atan2f(G.vel.x, G.vel.z) - G.bodyYaw);
            if (fabsf(rel) > 1.80f) G.legBackMode = true;   // past about 103 degrees: backwards
            else if (fabsf(rel) < 1.62f) G.legBackMode = false;   // about 93: a sideways walk faces its way
            // capped: a full quarter turn of the hips under a chest on the aim wrings his waist,
            // so a sideways walk is a crab step with the legs part-way round
            yawT = Clampf(G.legBackMode ? WrapPi(rel - PI) : rel, -kLegYawMax, kLegYawMax) * moving;
            backT = G.legBackMode ? 1.0f : 0.0f;
        }
        StepTo(G.legYaw, yawT, 7.0f, 1.0f, dt);
        StepTo(G.legBack, backT, 14.0f, 1.0f, dt);
        G.legW = (active && G.aiming && aimWalk && G.haveBody) ? 1.0f : fmaxf(Clamp01(G.camAim), drunkLegs);   // letting go: fade out with the aim
        if (!onFoot) G.legYaw = G.legBack = Spring{0, 0};
    }
    G.hipTimer -= dt;
    if (G.pendingShot && G.aim > (G.aiming ? 0.6f : kHipReady) && G.reach.x > 0.6f && G.cooldown <= 0) {
        Fire();
        G.pendingShot = false;
    }
    if (G.pendingShot && !G.aiming && G.hipTimer <= 0) G.pendingShot = false;   // the arm never got there: drop it

    // body recoil (sub-stepped springs: stable at any frame rate), slide, spread, camera kicks
    Step(G.kickWrist, 30.0f, 0.45f, dt);
    Step(G.kickArm, 16.0f, 0.6f, dt);
    Step(G.kickElbow, 22.0f, 0.5f, dt);
    Step(G.kickChest, 10.0f, 0.7f, dt);
    G.slide = fmaxf(0.0f, G.slide - dt / 0.08f);
    G.spread = fmaxf(0.0f, G.spread - dt * 3.0f);
    Step(G.kickPitch, 16.0f, 0.7f, dt);
    Step(G.kickYaw, 18.0f, 0.8f, dt);
    Step(G.fovPunch, 22.0f, 0.6f, dt);
    Step(G.xhKick, 20.0f, 0.45f, dt);
    Step(G.lchBack, 14.0f, 0.55f, dt);
    Step(G.lchFlip, 11.0f, 0.45f, dt);

    int block = 0;
    WORD blockButtons = 0;
    if (armed && onFoot && !wheelOpen) {
        uint32_t used = g_cfg.aimButtons | g_cfg.fireButtons;
        if (used & (BTN_LT | BTN_RT)) block |= GUNBLOCK_TRIGGERS;
        blockButtons = (WORD)(used & 0xFFFF);
    }
    if (active && G.aiming) block |= (aimWalk || g_aimMove == 1 ? 0 : GUNBLOCK_MOVE) | GUNBLOCK_LOOKY | GUNBLOCK_LOOKX;
    {
        double t0 = ProfNow();
        DaUpdate(dt, pad, gs, kb, wheelOpen, held(g_cfg.carAimButtons) || kb.carAim, held(g_cfg.carFireButtons) || kb.carFire, &block, &blockButtons);   // (diagnostic, m3: aiming from the car)
        g_daProf[2] += ProfNow() - t0;
    }
    Input_SetGunBlock(block, blockButtons);

    // effects
    for (int i = 0; i < G.nflash;) {
        if (G.flash[i].veh) {   // (drive-by) a flash at a muzzle out of a moving car's window rides along with it
            Mat4 cf;
            if (DhCarFrame(G.flash[i].veh, &cf)) G.flash[i].p = XformPoint(G.flash[i].lp, cf), G.flash[i].dir = XformDir(G.flash[i].ld, cf);
        }
        G.flash[i].t += g_dbgHoldFx ? 0.0f : dt;   // debug "fxhold": freeze to inspect
        if (G.flash[i].t > 0.07f) G.flash[i] = G.flash[--G.nflash]; else i++;
    }
    for (int i = 0; i < G.nsmoke;) {
        Smoke& s = G.smoke[i];
        s.t += dt;
        s.p = s.p + s.v * dt;
        s.v = s.v * (1.0f - dt * 2.5f) + V3(0, 0.4f * dt, 0);
        if (s.t > s.life) G.smoke[i] = G.smoke[--G.nsmoke]; else i++;
    }
    for (int i = 0; i < G.ntracers;) {
        Tracer& t = G.tracers[i];
        float step = 140.0f * dt;
        t.p = t.p + t.dir * step;
        t.dist += step;
        t.t += dt;
        if (t.dist > t.maxDist) G.tracers[i] = G.tracers[--G.ntracers]; else i++;
    }
    for (int i = 0; i < G.nshells;) {
        Shell& s = G.shells[i];
        s.t += dt;
        s.v.y -= 9.8f * dt;
        s.p = s.p + s.v * dt;
        s.ang += s.spin * dt;
        float rest = s.kind == 1 ? 0.012f : 0.006f;
        if (s.p.y < s.ground + rest && s.v.y < 0) {
            if (s.kind == 1 && !s.landed) {   // a shotgun shell: a hollow tock as it lands
                s.landed = true;
                Vec3 ear = G.camAim > 0.001f ? G.camPos : s.p;
                float gw[16], gf;
                if (G.camAim <= 0.001f && Game_GetGameCamera(gw, &gf)) ear = V3(gw[12], gw[13], gw[14]);
                Sound_Play(SND_SHELL, Clamp01(1.0f - Len(s.p - ear) / 15.0f) * 0.7f, 0.92f + 0.16f * Rnd());
            }
            s.p.y = s.ground + rest;
            s.v.y = -s.v.y * 0.35f;
            s.v.x *= 0.6f;
            s.v.z *= 0.6f;
            s.spin *= 0.6f;
        }
        if (s.t > 1.8f) G.shells[i] = G.shells[--G.nshells]; else i++;
    }
    for (int i = 0; i < G.nchips;) {
        Chip& c = G.chips[i];
        c.t += dt;
        c.v.y -= 9.8f * dt;
        c.p = c.p + c.v * dt;
        c.rot += c.spin * dt;
        if (c.p.y < g_ground + 0.01f && c.v.y < 0) {   // bounce once, then settle
            c.p.y = g_ground + 0.01f;
            c.v = c.v * 0.3f;
            c.v.y = -c.v.y;
        }
        if (c.t > c.life) G.chips[i] = G.chips[--G.nchips]; else i++;
    }
    for (int i = 0; i < G.npuffs;) {
        G.puffs[i].t += dt;
        if (G.puffs[i].t > G.puffs[i].life) G.puffs[i] = G.puffs[--G.npuffs]; else i++;
    }
    for (int i = 0; i < G.nsparks;) {
        G.sparks[i].t += dt;
        if (G.sparks[i].t > G.sparks[i].life) G.sparks[i] = G.sparks[--G.nsparks]; else i++;
    }
}

// ------------------------------------------------------------------ drawing
// The fist closes on the grip with the slide riding above the knuckles, so from behind the
// back of the slide (sights) sits on top of the hand instead of sinking into it.
static float kGunScale = 1.65f;
static bool kDepth = true;

// The gun is lit by the same lights as the characters (the scene's ambient + directional
// lights, read every frame), so it sits in Homer's hand like part of the game. Metal adds a
// soft sky/ground reflection and a small crisp glint; edges darken a touch.
static struct {
    Vec3 amb, dir[4], col[4], sun;
    int n;
} g_light;

static void ReadLights()
{
    float a[3], d[4][3], c[4][3];
    int n = Game_GetLights(a, d, c, 4);
    if (n <= 0) {   // fallback: Level 1's rig
        g_light.amb = V3(0.35f, 0.35f, 0.35f);
        g_light.n = 3;
        g_light.dir[0] = V3(0, -0.5f, -0.866f), g_light.col[0] = V3(0.35f, 0.35f, 0.35f);
        g_light.dir[1] = V3(-0.75f, -0.5f, 0.433f), g_light.col[1] = V3(0.55f, 0.55f, 0.55f);
        g_light.dir[2] = V3(0.75f, -0.5f, 0.433f), g_light.col[2] = V3(0.55f, 0.55f, 0.55f);
    } else {
        g_light.amb = V3(a[0], a[1], a[2]);
        g_light.n = n;
        for (int i = 0; i < n; i++) {
            g_light.dir[i] = V3(d[i][0], d[i][1], d[i][2]);
            g_light.col[i] = V3(c[i][0], c[i][1], c[i][2]);
        }
    }
    int best = 0;   // the strongest light gives the glint
    for (int i = 1; i < g_light.n; i++)
        if (Dot(g_light.col[i], V3(1, 1, 1)) > Dot(g_light.col[best], V3(1, 1, 1))) best = i;
    g_light.sun = g_light.dir[best] * -1.0f;
}

static DWORD Lit(Vec3 col, Vec3 n, float metal, Vec3 V)
{
    Vec3 d = g_light.amb;
    for (int i = 0; i < g_light.n; i++) d = d + g_light.col[i] * Clamp01(-Dot(n, g_light.dir[i]));
    Vec3 c = {col.x * d.x, col.y * d.y, col.z * d.z};
    Vec3 rf = n * (2 * Dot(n, V)) - V;
    c = c + Lerp(V3(0.30f, 0.33f, 0.25f), V3(0.70f, 0.82f, 1.0f), Smooth01((rf.y + 0.2f) / 0.6f)) * (0.18f * metal);
    float g = powf(Clamp01(Dot(n, Norm(g_light.sun + V))), 48.0f);
    c = c + V3(1, 1, 1) * (Smooth01((g - 0.35f) / 0.15f) * 0.40f * metal);
    c = c * (1.0f - 0.30f * Smooth01((0.35f - Clamp01(Dot(n, V))) / 0.35f));
    auto b = [](float x) { return (DWORD)(Clamp01(x) * 255.0f + 0.5f); };
    return 0xFF000000u | (b(c.x) << 16) | (b(c.y) << 8) | b(c.z);
}

// a built mesh, lit like the gun, at `world`
static void DrawLitMesh(Renderer& r, const MeshBuf& m, const Mat4& world, Vec3 viewDir)
{
    static Renderer::MeshV mv[1600];
    int nv = m.nv < 1600 ? m.nv : 1600;
    for (int i = 0; i < nv; i++) {
        const GV& g = m.v[i];
        mv[i].x = g.p.x;
        mv[i].y = g.p.y;
        mv[i].z = g.p.z;
        mv[i].c = Lit(g.col, Norm(XformDir(g.n, world)), g.spec, viewDir);
    }
    r.Mesh(mv, nv, m.idx, m.ni, world.m);
}

void Gun_Draw(Renderer& r)
{
    G.haveGun = false;
    float v[16], p[16];
    if (!g_player || !Game_GetCamera(v, p)) return;
    bool effects = G.nflash || G.nsmoke || G.ntracers || G.nshells || G.nsparks || G.nchips || G.npuffs || Rocket_Any() ||
                   Explode_Any();
    bool drv = g_drv >= 2 && g_drvInCar;   // (diagnostic)
    g_drvHaveGun = false;
    if (G.show < 0.001f && !effects && G.aim < 0.01f && !drv) return;
    Mat4 view = ToMat(v), proj = ToMat(p), vp = Mul(view, proj);
    Mat4 cam = RigidInverse(view);
    Vec3 camFwd = Norm(Row(cam, 2));
    Vec3 viewDir = camFwd * -1.0f;
    ReadLights();

    PoserPose pose;
    bool havePose = Game_ReadPoser(g_player, &pose);
    if (havePose) g_ground = pose.position[1];

    // the game didn't draw him at all this frame (it hides the player when the camera gets within
    // about 1.8 m, e.g. aiming up with the aim camera squeezed against a wall): no weapon floating
    // in the air where his hand would be. (Drawn but not matched - a costume the bind cache missed
    // - still counts as drawn, and the gun follows the game's hand as before.)
    bool unseen = g_skin.active && g_skinNear == 0;
    r.Begin3D(view.m, proj.m, kDepth);
    if (havePose && G.show > 0.001f && G.held == WPN_HANDGUN) {
        // our wrist only if the game drew his hand with it this frame: every piece of him has
        // been drawn by now, so the match counts are final. With no match (a new skeleton the
        // bind cache missed, a costume we can't pose) the hand on screen is the game's own
        bool ourHand = g_skin.active && g_skin.haveHand && g_skinHits[28] > 0;
        Mat4 wrist = ourHand ? g_skin.hand : ToMat(pose.world[28]);
        float pop = EaseOutBack(G.show) * (G.popT < 0.5f ? 1.0f + 0.10f * sinf(G.popT * 26.0f) * expf(-G.popT * 7.0f) : 1.0f);
        // gun axes in wrist space: barrel -> -X (fingers), top -> -Y (thumb), right -> -Z
        Mat4 toWrist = WithTrans(Basis(V3(0, 0, -1), V3(0, -1, 0), V3(-1, 0, 0)), GripInWrist(true));
        Mat4 world = Mul(Mul(ScaleM(kGunScale * PistolScale() * pop), toWrist), wrist);
        G.gunWorld = world;
        G.haveGun = true;
        if (g_buildLog > 0) {
            Vec3 h = Row(wrist, 3);
            Log("draw  f%d gun at hand (%.3f %.3f %.3f) from %s (wrist matches %d)", g_buildFrame, h.x, h.y, h.z,
                ourHand ? "our pose" : "game pose", g_skinHits[28]);
        }
        if (g_logGun > 0) {
            g_logGun--;
            g_logGunDrawn = true;
            Vec3 c = XformPoint(V3(0, 0.03f, 0.05f), world);
            float sx, sy, d;
            bool ok = Project(c, vp, (float)r.W, (float)r.H, &sx, &sy, &d);
            Vec3 cv = XformPoint(c, view);
            Log("gun centre world (%.2f %.2f %.2f) cam (%.2f %.2f %.2f) screen %d (%.0f, %.0f) depth %.4f show %.2f pop %.2f",
                c.x, c.y, c.z, cv.x, cv.y, cv.z, ok, sx, sy, d, G.show, pop);
            Log("  view row3 (%.2f %.2f %.2f) proj _11 %.3f _22 %.3f _33 %.4f _43 %.4f", view.m[12], view.m[13], view.m[14],
                proj.m[0], proj.m[5], proj.m[10], proj.m[14]);
        }
        static Renderer::MeshV mv[900];
        Mat4 slideOff = Translation(V3(0, 0, -0.022f * G.slide));
        for (int i = 0; i < g_gunMesh.nv; i++) {
            const GV& g = g_gv[i];
            Vec3 pl = g.part == 1 ? XformPoint(g.p, slideOff) : g.p;
            mv[i].x = pl.x;
            mv[i].y = pl.y;
            mv[i].z = pl.z;
            mv[i].c = Lit(g.col, Norm(XformDir(g.n, world)), g.spec, viewDir);
        }
        if (!unseen) r.Mesh(mv, g_gunMesh.nv, g_gi, g_gunMesh.ni, world.m);
    }
    if (drv && g_drv == 3) {   // (diagnostic, m2) the pistol in the posed hand
        if (g_drvPosed && g_dhShow > 0.001f) {
            Mat4 world = DhGunWorld();
            static Renderer::MeshV mv[900];
            Mat4 slideOff = Translation(V3(0, 0, -0.022f * G.slide));   // (m4: the slide racks back as it fires)
            for (int i = 0; i < g_gunMesh.nv; i++) {
                const GV& gv = g_gv[i];
                Vec3 pl = gv.part == 1 ? XformPoint(gv.p, slideOff) : gv.p;
                mv[i].x = pl.x, mv[i].y = pl.y, mv[i].z = pl.z;
                mv[i].c = Lit(gv.col, Norm(XformDir(gv.n, world)), gv.spec, viewDir);
            }
            r.Mesh(mv, g_gunMesh.nv, g_gi, g_gunMesh.ni, world.m);
        }
    } else if (drv && havePose) {   // (diagnostic) the pistol in his LEFT hand: the right hand's grip turned 180 degrees about
                             // the wrist's Z (the left arm's bones run along +X where the right's run along -X)
        int wj = g_drvSide ? 28 : 21;
        Mat4 wrist = g_drvPosed ? g_drvWn[wj] : ToMat(pose.world[wj]);
        Vec3 g = GripInWrist(true);
        Mat4 toWrist = g_drvSide ? WithTrans(Basis(V3(0, 0, -1), V3(0, -1, 0), V3(-1, 0, 0)), g)   // his usual right-hand grip
                                 : WithTrans(Basis(V3(0, 0, -1), V3(0, 1, 0), V3(1, 0, 0)), V3(-g.x, -g.y, g.z));
        Mat4 world = Mul(Mul(ScaleM(kGunScale * PistolScale()), toWrist), wrist);
        g_drvGun = world;
        g_drvHaveGun = true;
        static Renderer::MeshV mv[900];
        for (int i = 0; i < g_gunMesh.nv; i++) {
            const GV& gv = g_gv[i];
            mv[i].x = gv.p.x, mv[i].y = gv.p.y, mv[i].z = gv.p.z;
            mv[i].c = Lit(gv.col, Norm(XformDir(gv.n, world)), gv.spec, viewDir);
        }
        r.Mesh(mv, g_gunMesh.nv, g_gi, g_gunMesh.ni, world.m);
    }
    // the rocket launcher on his shoulder (placed by the pose), and the loaded rocket's nose
    G.haveLch = false;
    if (havePose && G.show > 0.001f && G.held == WPN_RPG && g_skin.haveLauncher) {
        float pop = EaseOutBack(G.show) * (G.popT < 0.5f ? 1.0f + 0.10f * sinf(G.popT * 26.0f) * expf(-G.popT * 7.0f) : 1.0f);
        Mat4 world = Mul(ScaleM(g_skin.lchScale * pop), g_skin.launcher);
        G.lchWorld = world;
        G.haveLch = true;
        if (!unseen) DrawLitMesh(r, g_lchMesh, world, viewDir);
        if (G.rpgLoaded && !unseen) {   // the new rocket pops in
            float rp = EaseOutBack(Clamp01(G.reloadT / 0.25f));
            DrawLitMesh(r, g_rktMesh, Mul(Mul(ScaleM(fmaxf(rp, 0.01f)), Translation(V3(0, 0, kRktLoadedZ))), world), viewDir);
        }
    }
    // the shotgun in his hands (placed by the pose; clipped at his pocket while it comes out)
    SgDraw(r, viewDir, unseen);
    if (g_lazyBuild) {   // (no skinned draw of his this frame - off screen: built here, for the item's place)
        g_lazyBuild = false;
        BuildPose(g_skinPose);
    }
    {
        double t0 = ProfNow();
        DnDraw(r, viewDir, unseen);
        BrDraw(r, viewDir, unseen);
        PerfAdd(7, ProfNow() - t0);
    }
    // rockets in flight
    RocketView rocketV[4];
    int nRockets = Rocket_Views(rocketV, 4);
    for (int i = 0; i < nRockets; i++) DrawLitMesh(r, g_rktMesh, rocketV[i].world, viewDir);
    // brass shells
    if (G.nshells) {
        static Renderer::MeshV sv[24];
        static uint16_t si[36];
        static bool built;
        static Vec3 sn[24];
        if (!built) {
            int n = 0, k = 0;
            static const int f[6][4][3] = {
                {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},  {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}},
                {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},  {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
                {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},  {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}};
            static const Vec3 nrm[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (int i = 0; i < 6; i++) {
                for (int j = 0; j < 4; j++) {
                    sv[n] = {f[i][j][0] * 0.0055f * kGunScale, f[i][j][1] * 0.0055f * kGunScale, f[i][j][2] * 0.011f * kGunScale, 0};
                    sn[n++] = nrm[i];
                }
                const int q[6] = {0, 1, 2, 0, 2, 3};
                for (int j = 0; j < 6; j++) si[k++] = (uint16_t)(i * 4 + q[j]);
            }
            built = true;
        }
        for (int s = 0; s < G.nshells; s++) {
            Shell& sh = G.shells[s];
            if (sh.kind == 1) {   // a shotgun shell
                float sc = g_sg.k > 0.1f ? g_sg.k : 1.0f;
                DrawLitMesh(r, g_shellMesh, Mul(Mul(ScaleM(sc), AxisAngle(sh.axis, sh.ang)), Translation(sh.p)), viewDir);
                continue;
            }
            Mat4 w = Mul(AxisAngle(sh.axis, sh.ang), Translation(sh.p));
            for (int i = 0; i < 24; i++) sv[i].c = Lit(V3(0.86f, 0.64f, 0.22f), Norm(XformDir(sn[i], w)), 1.0f, viewDir);
            r.Mesh(sv, 24, si, 36, w.m);
        }
    }
    r.End3D();

    // 2D effects, drawn at their 3D depth (inside the game's scene the world's depth buffer
    // is still here), so Homer and walls hide what's behind them
    bool depthFx = kDepth && r.InGameScene();
    auto proj2 = [&](Vec3 w, float* x, float* y, float* z, float* rhw) {
        float px = w.x * vp.m[0] + w.y * vp.m[4] + w.z * vp.m[8] + vp.m[12];
        float py = w.x * vp.m[1] + w.y * vp.m[5] + w.z * vp.m[9] + vp.m[13];
        float pz = w.x * vp.m[2] + w.y * vp.m[6] + w.z * vp.m[10] + vp.m[14];
        float pw = w.x * vp.m[3] + w.y * vp.m[7] + w.z * vp.m[11] + vp.m[15];
        if (pw < 0.05f) return false;
        *x = (px / pw * 0.5f + 0.5f) * r.W;
        *y = (-py / pw * 0.5f + 0.5f) * r.H;
        *z = Clamp01(pz / pw);
        *rhw = 1.0f / pw;
        return true;
    };
    auto at = [&](float z, float rhw) { r.SpriteDepth(depthFx, z, rhw); };
    auto pixPerMetre = [&](Vec3 w) {
        Vec3 c = XformPoint(w, view);
        return c.z > 0.05f ? (proj.m[5] * r.H * 0.5f) / c.z : 0.0f;
    };
    const Sprite* smoke = r.Find("smoke");
    for (int i = 0; i < G.nsmoke; i++) {
        Smoke& s = G.smoke[i];
        float x, y, z, rhw;
        if (!proj2(s.p, &x, &y, &z, &rhw)) continue;
        float u = s.t / s.life;
        float sz = s.size * (0.6f + u * 1.4f) * pixPerMetre(s.p) / (64.0f * r.k);
        at(z, rhw);
        r.Sprite2(smoke, x, y, sz, Rgba(1, 1, 1, 0.75f * (1.0f - u) * Clamp01(s.t * 12.0f)), false, s.rot + u);
    }
    // rocket trails: cartoon smoke puffs that swell and thin out
    {
        const RocketPuff* tp;
        int nt = Rocket_Trail(&tp);
        for (int i = 0; i < nt; i++) {
            const RocketPuff& q = tp[i];
            float x, y, z, rhw;
            if (!proj2(q.p, &x, &y, &z, &rhw)) continue;
            float u = q.t / q.life;
            float sz = q.size * (1.0f + 2.4f * EaseOutCubic(u)) * pixPerMetre(q.p) / (64.0f * r.k);
            at(z, rhw);
            r.Sprite2(smoke, x, y, sz, Rgba(q.shade, q.shade, q.shade * 1.02f, 0.80f * powf(1.0f - u, 1.3f) * Clamp01(q.t * 25.0f)),
                      false, q.rot + u * 0.8f);
        }
        // the motor: a hot glow lighting the smoke, and a flickering flame at the tail
        const Sprite* fl = r.Find("flash");
        const Sprite* glow = r.Find("blob");
        for (int i = 0; i < nRockets; i++) {
            const RocketView& v = rocketV[i];
            float x, y, z, rhw;
            if (!proj2(v.tail, &x, &y, &z, &rhw)) continue;
            float ppm = pixPerMetre(v.tail), flick = 0.85f + 0.15f * sinf(v.age * 70.0f) + 0.1f * Rnd();
            r.SpriteDepth(false);
            r.Sprite2(glow, x, y, 0.9f * v.scale * ppm / (64.0f * r.k), Rgba(1.0f, 0.55f, 0.15f, 0.45f), false, 0.0f, BLEND_ADD);
            at(z, rhw);
            r.Sprite2(fl, x, y, fmaxf(0.22f * v.scale * flick * ppm / (72.0f * r.k), 0.15f), Rgba(1, 1, 1, 1), false, v.age * 9.0f);
        }
    }
    // explosions: the mod's cartoon part (the game's own fireball plays by itself): a flash with a
    // hot glow, fireball puffs going from yellow through orange to soot, and dark smoke rising
    {
        const BlastFx* bf;
        int nb = Explode_Fx(&bf);
        Vec3 camP = Row(cam, 3);
        auto nearFade = [&](Vec3 p) { return Clamp01((Len(p - camP) - 1.5f) / 3.0f); };   // nothing pasted over the lens
        float capFlash = 0.45f * r.H / (72.0f * r.k), capGlow = 0.9f * r.H / (64.0f * r.k);   // at most this big on screen
        const Sprite* fl = r.Find("flash");
        const Sprite* glow = r.Find("blob");
        for (int pass = 2; pass >= 0; pass--)   // smoke behind the fire, the flash on top
            for (int i = 0; i < nb; i++) {
                const BlastFx& f = bf[i];
                if (f.kind != pass) continue;
                float x, y, z, rhw;
                if (!proj2(f.p, &x, &y, &z, &rhw)) continue;
                float u = Clamp01(f.t / f.life), ppm = pixPerMetre(f.p);
                if (f.kind == 0) {
                    float nf = 0.35f + 0.65f * nearFade(f.p);
                    r.SpriteDepth(false);
                    r.Sprite2(glow, x, y, fminf(f.size * 1.5f * ppm / (64.0f * r.k), capGlow),
                              Rgba(1.0f, 0.62f, 0.22f, 0.65f * (1.0f - u) * nf), false, 0.0f, BLEND_ADD);
                    at(z, rhw);
                    r.Sprite2(fl, x, y, fminf(f.size * (0.55f + 0.5f * EaseOutCubic(u)) * ppm / (72.0f * r.k), capFlash),
                              Rgba(1, 1, 1, Clamp01((1.0f - u) * 2.0f) * nf), false, f.rot);
                } else if (f.kind == 1) {
                    Vec3 c0 = V3(1.0f, 0.93f, 0.48f), c1 = V3(1.0f, 0.46f, 0.12f), c2 = V3(0.33f, 0.29f, 0.27f);
                    Vec3 col = u < 0.4f ? Lerp(c0, c1, u / 0.4f) : Lerp(c1, c2, (u - 0.4f) / 0.6f);
                    at(z, rhw);
                    r.Sprite2(smoke, x, y, f.size * (0.5f + 1.0f * EaseOutCubic(u)) * ppm / (64.0f * r.k),
                              Rgba(col.x, col.y, col.z, 0.95f * (1.0f - u * u) * nearFade(f.p)), false, f.rot + u);
                } else {
                    at(z, rhw);
                    r.Sprite2(smoke, x, y, f.size * (0.6f + 1.6f * EaseOutCubic(u)) * ppm / (64.0f * r.k),
                              Rgba(0.36f, 0.34f, 0.33f, 0.72f * (1.0f - u) * Clamp01(f.t * 4.0f) * nearFade(f.p)), false,
                              f.rot + u * 0.6f);
                }
            }
    }
    const Sprite* tr = r.Find("tracer");
    auto streak = [&](Vec3 a, Vec3 b, float thick, Color c) {   // additive streak from a to b
        float x0, y0, z0, w0, x1, y1, z1, w1;
        if (!proj2(a, &x0, &y0, &z0, &w0) || !proj2(b, &x1, &y1, &z1, &w1)) return;
        float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
        if (len < 1.0f) return;
        at(fminf(z0, z1), fmaxf(w0, w1));
        r.Sprite2(tr, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 1.0f, c, false, atan2f(dy, dx), BLEND_ADD, len / (96.0f * r.k),
                  thick / (20.0f * r.k));
    };
    for (int i = 0; i < G.ntracers; i++) {
        Tracer& t = G.tracers[i];
        Vec3 tail = t.p - t.dir * fminf(2.2f, t.dist + 0.1f);
        float fade = Clamp01(1.0f - t.dist / t.maxDist);
        // a few pixels thick whatever the distance
        float thick = fminf(10.0f, fmaxf(1.5f, 0.03f * pixPerMetre(t.p))) * r.k;
        streak(tail, t.p, thick, Rgba(1, 0.95f, 0.8f, fade));
    }
    // muzzle flash: a warm glow that lights his hand (not depth tested), a flame jet along
    // the barrel, and the star itself - solid for two frames, the second smaller and turned
    const Sprite* fl = r.Find("flash");
    const Sprite* glow = r.Find("blob");
    for (int i = 0; i < G.nflash; i++) {
        Flash& f = G.flash[i];
        float x, y, z, rhw;
        if (!proj2(f.p, &x, &y, &z, &rhw)) continue;
        float ppm = pixPerMetre(f.p);
        bool first = f.t < 0.034f;
        float ga = Clamp01(1.0f - f.t / 0.07f);
        r.SpriteDepth(false);
        r.Sprite2(glow, x, y, f.size * 0.9f * ppm / (64.0f * r.k), Rgba(1.0f, 0.62f, 0.22f, 0.50f * ga), false, 0.0f,
                  BLEND_ADD);
        streak(f.p, f.p + f.dir * ((first ? 0.30f : 0.18f) * f.jet), (first ? 0.06f : 0.04f) * f.jet * ppm,
               Rgba(1.0f, 0.80f, 0.45f, first ? 1.0f : 0.7f));
        at(z, rhw);
        float sz = f.size * (first ? 0.30f : 0.21f) * ppm / (72.0f * r.k);
        r.Sprite2(fl, x, y, fmaxf(sz, 0.25f), Rgba(1, 1, 1, 1), false, f.rot + (first ? 0.0f : 0.6f));
    }
    // bullet impacts: a quick starburst, a dust puff, chips
    for (int i = 0; i < G.npuffs; i++) {
        Puff& pf = G.puffs[i];
        float x, y, z, rhw;
        if (!proj2(pf.p, &x, &y, &z, &rhw)) continue;
        float u = pf.t / pf.life, ppm = pixPerMetre(pf.p);
        at(z, rhw);
        if (pf.t < 0.05f) r.Sprite2(fl, x, y, fmaxf(pf.size * 0.55f * ppm / (72.0f * r.k), 0.12f), Rgba(1, 1, 1, 1), false, pf.rot);
        r.Sprite2(smoke, x, y, pf.size * (0.5f + 1.3f * EaseOutCubic(u)) * ppm / (64.0f * r.k),
                  Rgba(pf.col.x, pf.col.y, pf.col.z, 0.85f * (1.0f - u)), false, pf.rot + u);
    }
    const Sprite* white = r.Find("white");
    for (int i = 0; i < G.nchips; i++) {
        Chip& c = G.chips[i];
        float x, y, z, rhw;
        if (!proj2(c.p, &x, &y, &z, &rhw)) continue;
        float px = fmaxf(1.5f, c.size * pixPerMetre(c.p));
        at(z, rhw);
        r.Sprite2(white, x, y, px / (8.0f * r.k), Rgba(c.col.x, c.col.y, c.col.z, Clamp01((c.life - c.t) / 0.15f)), false, c.rot);
    }
    const Sprite* sp = r.Find("sparkle");
    for (int i = 0; i < G.nsparks; i++) {
        Spark& s = G.sparks[i];
        if (s.t < 0) continue;
        float x, y, z, rhw;
        if (!proj2(s.p, &x, &y, &z, &rhw)) continue;
        float u = s.t / s.life;
        at(z, rhw);
        r.Sprite2(sp, x, y, sinf(PI * Clamp01(u)) * s.size, Rgba(1, 1, 1, Clamp01(2 - 2 * u)), false, s.rot + u * 3);
    }
    r.SpriteDepth(false);
}

// on top of everything (after the HUD): the crosshair while aiming, and right after
// the gun comes out a short hint saying which buttons aim and fire
int g_cinema;   // debug "cinema <0|1|2>": screenshots without our hint bar and crosshair (2: nor the wheel's hints)

static void DaOverlay(Renderer& r);   // (diagnostic, m3)
static void DaRadarOverlay(Renderer& r);
static bool DaWantsProbe();
void Gun_DrawOverlay(Renderer& r)
{
    DaRadarOverlay(r);
    if (g_cinema >= 1) return;
    DaOverlay(r);
    // (v1.0: no "Aim / Fire" hint bar as a weapon comes out - review: "remove the mod-added blue control-hint bars and prompts at
    // the bottom of the screen, including aim/shoot instructions". The controls are in the README)
    if (G.camAim > 0.02f && g_orbitDist <= 0.1f) {
        float a = Clamp01((G.camAim - 0.3f) / 0.5f);
        float sc = 0.55f * (1.0f + 0.30f * G.spread) * (1.0f + 0.35f * G.xhKick.x);
        r.Sprite2(r.Find("crosshair"), r.W * 0.5f, r.H * 0.5f, sc, Rgba(1, 1, 1, a));
    }
}

// the depth probe runs while aiming (and is read back at the next Present)
bool Gun_WantsProbe(float* p10, float* p14)
{
    float v[16], p[16];
    if ((G.camAim < 0.3f && !DaWantsProbe()) || g_orbitDist > 0.1f || !Game_GetCamera(v, p)) return false;
    *p10 = p[10];
    *p14 = p[14];
    return true;
}

void Gun_SetProbe(float dist)
{
    G.hitDist = dist;
    G.hitAge = 0;
}

bool Gun_NeedsDraw()
{
    if (g_drv > 0 && g_drvInCar) return true;   // (diagnostic)
    return G.show > 0.001f || G.aim > 0.01f || G.camAim > 0.01f || G.nflash || G.nsmoke || G.ntracers || G.nshells ||
           G.nsparks || G.nchips || G.npuffs || Rocket_Any() || Explode_Any();
}

// debug commands
bool Gun_DrvCmd(const char* cmd, const char* arg);
bool Gun_DrvCamCmd(const char* cmd, const char* arg);
void Gun_DebugCmd(const char* cmd, const char* arg)
{
    if (Gun_DrvCmd(cmd, arg) || Gun_DrvCamCmd(cmd, arg)) return;   // (diagnostic)
    if (SgDebugCmd(cmd, arg)) return;   // (shotgun.inc)
    if (DnDebugCmd(cmd, arg)) return;   // (donut.inc)
    if (BrDebugCmd(cmd, arg)) return;   // (beer.inc)
    if (DrunkDebugCmd(cmd, arg)) return;   // (drunk.inc)
    if (RagdollDebugCmd(cmd, arg)) return;   // (ragdoll.inc)
    if (DrunkBodyDebugCmd(cmd, arg)) return;   // (drunkbody.inc)
    if (PoseTraceCmd(cmd, arg)) return;   // (posetrace.inc)
    if (!strcmp(cmd, "equip")) {   // equip <0|1|2|3> or <none|handgun|rpg|shotgun>
        int w = !strcmp(arg, "rpg") ? WPN_RPG : !strcmp(arg, "handgun") ? WPN_HANDGUN : !strcmp(arg, "shotgun") ? WPN_SHOTGUN :
                !strcmp(arg, "donut") ? WPN_DONUT : !strcmp(arg, "beer") ? WPN_BEER : !strcmp(arg, "none") ? WPN_NONE : atoi(arg);
        Gun_Equip(w);
    }
    else if (!strcmp(cmd, "aim")) G.forceAim = atoi(arg) != 0;
    else if (!strcmp(cmd, "cinema")) g_cinema = atoi(arg);
    else if (!strcmp(cmd, "dcampos")) {   // (v1.0 A/B) "dcampos 1": the drunk camera writes the game camera's place too, as before
        extern bool g_camPlaceAlways;
        g_camPlaceAlways = atoi(arg) != 0;
        Log("dcampos: %s", g_camPlaceAlways ? "the drunk camera writes the place too (as before)" : "the game's place kept (v1.0)");
    } else if (!strcmp(cmd, "camdist")) {   // "camdist <frames>": how far the game's own camera is from him, each frame
        g_camDistLog = atoi(arg);
    }
    else if (!strcmp(cmd, "aimmove")) g_aimMove = atoi(arg);
    else if (!strcmp(cmd, "mouselead")) g_mouseLead = (float)atof(arg);
    else if (!strcmp(cmd, "mouselog")) g_mouseLog = arg[0] ? atoi(arg) : 30;
    else if (!strcmp(cmd, "movelog")) Game_AimMoveLog(atoi(arg));
    else if (!strcmp(cmd, "legs")) g_legDbg = atoi(arg);
    else if (!strcmp(cmd, "synclog")) g_syncLog = atoi(arg);
    else if (!strcmp(cmd, "legset")) {
        g_legYawSet = 99.0f;
        g_legBackSet = -1.0f;
        sscanf(arg, "%f,%f", &g_legYawSet, &g_legBackSet);
    }
    else if (!strcmp(cmd, "fire")) G.forceFire = 2;
    else if (!strcmp(cmd, "curl")) sscanf(arg, "%f,%f,%f", &kCurl1, &kCurl2, &kThumb);
    else if (!strcmp(cmd, "thumb")) sscanf(arg, "%d,%f,%f", &kThumbAxis, &kThumb, &kThumb2);
    else if (!strcmp(cmd, "gscale")) kGunScale = (float)atof(arg);
    else if (!strcmp(cmd, "gdepth")) kDepth = atoi(arg) != 0;
    else if (!strcmp(cmd, "camcoll")) g_camColl = atoi(arg) != 0;
    else if (!strcmp(cmd, "shotlog")) g_shotLog = atoi(arg);
    else if (!strcmp(cmd, "camlog")) g_camLog = atoi(arg);
    else if (!strcmp(cmd, "rig")) {   // rig <0 down|1 level|2 up>,<dist>,<side>,<height>
        int k;
        float d, sd, h;
        if (sscanf(arg, "%d,%f,%f,%f", &k, &d, &sd, &h) == 4 && k >= 0 && k < 3) kRig[k] = {kRig[k].pitch, d, sd, h};
    }
    else if (!strcmp(cmd, "pitch")) G.pitch = fminf(kPitchUp, fmaxf(kPitchDown, (float)atof(arg)));
    else if (!strcmp(cmd, "skintest")) g_skinTest = atoi(arg);
    else if (!strcmp(cmd, "skinoff")) g_skinOff = atoi(arg) != 0;
    else if (!strcmp(cmd, "glog")) g_logGun = 3;
    else if (!strcmp(cmd, "grip")) sscanf(arg, "%f,%f,%f", &kGrip.x, &kGrip.y, &kGrip.z);
    else if (!strcmp(cmd, "lazybuild")) {   // (lab) "lazybuild 0|1": the item's pose built once a frame (1) or twice (0, as before)
        g_lazyItemBuild = atoi(arg);
        Log("lazybuild: %d", g_lazyItemBuild);
    } else if (!strcmp(cmd, "perf")) {   // (lab) "perf <seconds>": the items' and the drunk body's cost a frame
        g_perf = {};
        g_perf.until = ProfNow() + 1000.0 * (arg[0] ? atof(arg) : 5.0);
    }
    else if (!strcmp(cmd, "gstate")) {
        PoserPose P;
        float v[16], pr[16];
        bool okP = Game_ReadPoser(g_player, &P), okC = Game_GetCamera(v, pr);
        Log("gun state: weapon %s (in hand %s) show %.2f aim %.2f camAim %.2f aiming %d player %08X poser %d (joints %d) camera %d",
            kWeaponNames[G.weapon], kWeaponNames[G.held], G.show, G.aim, G.camAim, G.aiming, g_player, okP, okP ? P.numJoints : 0, okC);
        Log("gun state: action %s", Game_ClassName(Game_CurrentAction(g_player)));
    }
    else if (!strcmp(cmd, "actlog")) g_actLog = atoi(arg);
    else if (!strcmp(cmd, "yaw")) G.yaw = WrapPi(G.yaw + (float)atof(arg));
    else if (!strcmp(cmd, "skinlog")) g_skinLog = atoi(arg);
    else if (!strcmp(cmd, "yawrate")) g_dbgYawRate = (float)atof(arg);
    else if (!strcmp(cmd, "aimtune"))
        sscanf(arg, "%f,%f,%f,%f,%f,%f", &kBlade, &kTwistMax, &kLean, &kReach, &kPoleOut, &kCant);
    else if (!strcmp(cmd, "torso")) g_torsoOn = atoi(arg) != 0;
    else if (!strcmp(cmd, "carry"))   // carry upOut,upFwd,foreFwd,foreDown,foreOut,gunDown,gunOut,cant
        sscanf(arg, "%f,%f,%f,%f,%f,%f,%f,%f", &kCarryUpOut, &kCarryUpFwd, &kCarryForeFwd, &kCarryForeDown,
               &kCarryForeOut, &kCarryGunDown, &kCarryGunOut, &kCarryCant);
    else if (!strcmp(cmd, "camspin")) g_dbgCamSpin = (float)atof(arg);
    else if (!strcmp(cmd, "camhead")) {   // "camhead behind" / "camhead <rad>": the game's own follow camera turned to a heading (behind him: his facing)
        float h = !strcmp(arg, "behind") ? (G.haveBody ? G.bodyYaw : G.yaw) : (float)atof(arg);
        Game_SetFollowCamHeading(h);
        Log("camhead: %.2f", h);
    }
    else if (!strcmp(cmd, "shotline")) {   // (M5 test) "shotline x,y,z,tx,ty,tz": one handgun bullet from a point towards another
        Vec3 a, t;
        if (sscanf(arg, "%f,%f,%f,%f,%f,%f", &a.x, &a.y, &a.z, &t.x, &t.y, &t.z) == 6 && Len(t - a) > 0.01f) {
            Vec3 d = Norm(t - a);
            ShotOrigin o = {};
            o.shooter = g_player, o.body = a - d * 0.2f, o.muzzle = a, o.dir = d, o.aimed = false;
            ShotResult r;
            FireWeapon(kHandgun, o, &r, 1);
            char line[200];
            Trace_Describe(r.hit, line, sizeof(line));
            Log("shotline: %s%s%s, %.2f m along", line, r.hit.glass ? " GLASS" : "", r.hit.tire ? " TIRE" : "", Len(r.to - r.from));
        }
    }
    else if (!strcmp(cmd, "testshot")) {
        int pellets = 6, shots = 1;
        float deg = 4.0f;
        sscanf(arg, "%d,%f,%d", &pellets, &deg, &shots);
        g_testSpec = kHandgun;
        g_testSpec.name = "test";
        g_testSpec.pellets = pellets < 1 ? 1 : (pellets > kMaxPellets ? kMaxPellets : pellets);
        g_testSpec.spread = deg * PI / 180.0f;
        g_testShots = shots;
        Log("testshot: next %d shot(s) with %d pellets in a %.1f degree cone", shots, g_testSpec.pellets, deg);
    }
    else if (!strcmp(cmd, "aimat")) {
        g_dbgAimAt = !strcmp(arg, "npc") ? 1 : !strcmp(arg, "car") ? 2 : !strcmp(arg, "traffic") ? 3 : !strcmp(arg, "plane") ? 5 : !strcmp(arg, "banner") ? 6 : !strcmp(arg, "wasp") ? 7 : 0;
        if (sscanf(arg, "%f,%f,%f", &g_dbgAimPoint.x, &g_dbgAimPoint.y, &g_dbgAimPoint.z) == 3) g_dbgAimAt = 4;
        g_dbgAimMin = 1.5f;
        if (!strncmp(arg, "npc:", 4)) {
            g_dbgAimAt = 1;
            g_dbgAimMin = (float)atof(arg + 4);
        }
        if (!strncmp(arg, "char:", 5)) {
            g_dbgAimAt = 9;
            g_dbgAimChar = atoi(arg + 5) & 63;
        }
        if (!strncmp(arg, "car:", 4)) {
            g_dbgAimAt = 8;
            snprintf(g_dbgAimCar, sizeof(g_dbgAimCar), "%s", arg + 4);
        }
    }
    else if (!strcmp(cmd, "slotdump")) g_slotDump = atoi(arg) + 1;
    else if (!strcmp(cmd, "buildlog")) g_buildLog = atoi(arg) + 1;
    else if (!strcmp(cmd, "viewrefresh")) g_viewRefresh = atoi(arg) != 0;
    else if (!strcmp(cmd, "speed")) Log("speed %.2f m/s carry %.2f", G.speed, G.carryW.x);
    else if (!strcmp(cmd, "fps")) {
        g_fpsLeft = arg[0] ? (float)atof(arg) : 1.0f;
        g_fpsFrames = 0, g_fpsT = 0;
    }
    else if (!strcmp(cmd, "gunslow")) g_gunSlow = arg[0] ? fmaxf(0.02f, (float)atof(arg)) : 1.0f;
    else if (!strcmp(cmd, "orbitbody")) g_orbitBody = arg[0] ? (float)atof(arg) : -1.0f;
    else if (!strcmp(cmd, "orbitjoint")) g_orbitJoint = arg[0] ? atoi(arg) : -1;
    else if (!strcmp(cmd, "orbitup")) g_orbitUp = arg[0] ? (float)atof(arg) : 0.35f;
    else if (!strcmp(cmd, "orbityaw")) g_orbitYaw = !strcmp(arg, "here") ? (G.haveBody ? G.bodyYaw : G.yaw) * 180.0f / PI : arg[0] ? (float)atof(arg) : -999.0f;
    else if (!strcmp(cmd, "probe2")) Log("probe: hit %.2f m (age %.2f s) rig %.2f", G.hitDist, G.hitAge, G.rigDist);
    else if (!strcmp(cmd, "fxhold")) {
        g_dbgHoldFx = atoi(arg) != 0;
        if (!g_dbgHoldFx) G.nflash = 0;
    }

    else if (!strcmp(cmd, "lch"))   // lch restUp,restOut,carryUp,carryOut,blade,gripZ,gripY
        sscanf(arg, "%f,%f,%f,%f,%f,%f,%f", &kLchRestUp, &kLchRestOut, &kLchCarryUp, &kLchCarryOut, &kLchBlade,
               &kLchGripZ, &kLchGripY);
    else if (!strcmp(cmd, "lhand"))   // lhand along,thick,finger,curl,thumb,supZ0,supZ1
        sscanf(arg, "%f,%f,%f,%f,%f,%f,%f", &kLHandAlong, &kLHandThick, &kLFinger, &kLCurl, &kLThumb, &kLchSupZ0,
               &kLchSupZ1);
    else if (!strcmp(cmd, "lscale")) kLchScaleSet = (float)atof(arg);
    else if (!strcmp(cmd, "slowmo")) g_dbgTimeScale = fminf(1.0f, fmaxf(0.02f, arg[0] ? (float)atof(arg) : 1.0f));
    else if (!strcmp(cmd, "orbit")) {
        g_orbitDist = 0;
        sscanf(arg, "%f,%f,%f", &g_orbitDeg, &g_orbitDist, &g_orbitFov);
    }
    else Weapon_DebugCmd(cmd, arg, g_player);
}

// Debug: dots on the player's joints (physics transform vs translation only).
void Gun_DebugMarkers(Renderer& r, uint32_t player)
{
    PoserPose pose;
    float v[16], p[16];
    if (!Game_ReadPoser(player, &pose) || !Game_GetCamera(v, p)) return;
    Mat4 vp = Mul(ToMat(v), ToMat(p));
    for (int i = 0; i < pose.numJoints && i < 33; i++) {
        Vec3 w = Row(ToMat(pose.world[i]), 3);
        float sx, sy, d;
        if (!Project(w, vp, (float)r.W, (float)r.H, &sx, &sy, &d)) continue;
        bool arm = i >= 26 && i <= 32;
        float s = (i == 28 ? 6.0f : 3.5f);
        r.Rect(sx - s, sy - s, sx + s, sy + s, arm ? Rgba(0, 1, 1) : Rgba(1, 1, 1));
    }
    // our aimed arm (what the skin is given) in red, gun centre in yellow
    if (g_skin.active) {
        static const int js[3] = {26, 27, 28};
        for (int k = 0; k < 3; k++) {
            Vec3 w = Row(g_skin.world[js[k]], 3);
            float sx, sy, d;
            if (Project(w, vp, (float)r.W, (float)r.H, &sx, &sy, &d)) r.Rect(sx - 6, sy - 6, sx + 6, sy + 6, Rgba(1, 0, 0));
        }
        if (G.haveGun) {
            Vec3 c = XformPoint(V3(0, 0.03f, 0.05f), G.gunWorld);
            float sx, sy, d;
            if (Project(c, vp, (float)r.W, (float)r.H, &sx, &sy, &d)) r.Rect(sx - 5, sy - 5, sx + 5, sy + 5, Rgba(1, 1, 0));
        }
    }
}

#if WW_DRIVEBY
// ------------------------------------------------------------------ DIAGNOSTIC (driveby-m1): overlay and log
// the driver's seat (joint dl), the driver's door hinge (DoorDRot) and the window opening, in the car's frame,
// from the cars' own art (art/cars/<name>.p3d): +X the car's right, +Z forward
struct DrvCar { const char* name; Vec3 seat, hinge, wmin, wmax; };
static const DrvCar kDrvCars[] = {
    {"famil_v", {-0.437f, -0.186f, -0.174f}, {-1.036f, 0.293f, 0.722f}, {-0.98f, 0.41f, -0.55f}, {-0.98f, 0.93f, 0.72f}},
    {"marge_v", {-0.552f, 0.200f, 0.172f}, {-1.098f, 0.000f, 0.899f}, {-1.00f, 0.89f, -0.06f}, {-1.00f, 1.46f, 0.82f}},
    {"rocke_v", {0.000f, -0.165f, -2.021f}, {-0.585f, 0.155f, -0.890f}, {-0.40f, 0.40f, -1.67f}, {-0.40f, 0.86f, -0.98f}},
    // traffic models have no door joints: the hinge marker sits at the front of the side glass
    {"minivanA", {-0.524f, -0.104f, -0.022f}, {-1.09f, 0.59f, 0.55f}, {-1.07f, 0.59f, -0.55f}, {-1.07f, 1.23f, 0.55f}},
    {"schoolbu", {-0.584f, -0.074f, 0.740f}, {-1.11f, 0.68f, 1.68f}, {-1.10f, 0.68f, 0.41f}, {-1.10f, 1.35f, 1.68f}},
    {"pickupA", {-0.517f, -0.357f, 0.500f}, {-1.03f, 0.32f, 1.10f}, {-1.01f, 0.32f, -0.10f}, {-1.01f, 0.94f, 1.10f}},
    {"glastruc", {-0.819f, -0.293f, 0.434f}, {-1.14f, 0.45f, 1.24f}, {-1.04f, 0.45f, 0.07f}, {-1.04f, 1.10f, 1.24f}},
};
static uint32_t DrvMyCar(const DrvCar** info, Mat4* frame)
{
    float pos[30][3];
    char names[30][24];
    int types[30];
    uint32_t vc = 0;
    int n = Game_ListVehicles(pos, names, 30, types);
    // his car: the one he sits in (his position is its driver's seat), not just any car marked his (a car he has
    // left stays marked when he takes another)
    float me[3];
    if (!Game_PlayerPosition(g_player, me)) return 0;
    int best = -1;
    float bd = 9.0f;   // within 3 m
    for (int i = 0; i < n; i++) {
        float dx = pos[i][0] - me[0], dy = pos[i][1] - me[1], dz = pos[i][2] - me[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bd) bd = d, best = i;
    }
    for (int i = best; i >= 0 && i < n; i = n) {
        (void)types;
        *info = nullptr;
        for (const DrvCar& c : kDrvCars)
            if (!strcmp(names[i], c.name)) *info = &c;
        // the car's transform (Vehicle +184): found again by position among the active cars
        float f[16];
        if (Game_FindVehicleFrame(pos[i], f)) *frame = ToMat(f), vc = 1;
        if (vc) {
            static char last[24];
            if (strcmp(last, names[i])) strcpy(last, names[i]), Log("drv: in '%s'%s", names[i], *info ? "" : " (no seat/window data)");
        }
        return vc;
    }
    return 0;
}
static void Dots(Renderer& r, const Mat4& vp, Vec3 a, Vec3 b, Color c, float s, int n)
{
    for (int i = 0; i <= n; i++) {
        float sx, sy, d;
        if (Project(a + (b - a) * ((float)i / n), vp, (float)r.W, (float)r.H, &sx, &sy, &d)) r.Rect(sx - s, sy - s, sx + s, sy + s, c);
    }
}
#include "driveby_hold_diag.inc"   // (diagnostic, driveby-m2)
void Gun_DrvOverlay(Renderer& r, uint32_t player)
{
    if (g_drv <= 0 || !g_drvInCar) return;
    if (g_drv == 3) {
        if (g_dhOverlay) DhOverlay(r);
        return;
    }
    PoserPose P;
    float v[16], p[16];
    if (!Game_ReadPoser(player, &P) || !Game_GetCamera(v, p)) return;
    Mat4 vp = Mul(ToMat(v), ToMat(p));
    for (int j = 3; j < P.numJoints && j < 33; j++) {
        if (j == 13 || j == 18) continue;
        int pj = g_parent[j];
        Color c = j >= 19 && j <= 25 ? Rgba(0, 1, 1) : j >= 26 ? Rgba(0.3f, 1, 0.3f) : j >= 14 ? Rgba(1, 1, 1) : Rgba(0.6f, 0.6f, 0.6f);
        Dots(r, vp, Row(ToMat(P.world[pj]), 3), Row(ToMat(P.world[j]), 3), c, 1.6f, 8);
        Dots(r, vp, Row(ToMat(P.world[j]), 3), Row(ToMat(P.world[j]), 3), c, 3.5f, 1);
    }
    if (g_drvPosed)
        for (int j = g_drvUpper > 0.0f ? 14 : (g_drvSide ? 26 : 19); j <= (g_drvSide ? 32 : 25); j++) {
            Vec3 a = Row(g_drvWn[g_parent[j]], 3), b = Row(g_drvWn[j], 3);
            Dots(r, vp, a, b, Rgba(1, 0, 1), 2.0f, 10);
        }
    const DrvCar* info = nullptr;
    Mat4 cf;
    if (DrvMyCar(&info, &cf) && info) {
        auto W = [&](Vec3 l) { return XformPoint(l, cf); };
        Dots(r, vp, W(info->seat), W(info->seat), Rgba(1, 1, 0), 6, 1);
        Dots(r, vp, W(info->hinge), W(info->hinge), Rgba(1, 0.5f, 0), 6, 1);
        Vec3 a = info->wmin, b = info->wmax;
        Vec3 q[4] = {{a.x, a.y, a.z}, {a.x, b.y, a.z}, {a.x, b.y, b.z}, {a.x, a.y, b.z}};
        for (int i = 0; i < 4; i++) Dots(r, vp, W(q[i]), W(q[(i + 1) & 3]), Rgba(1, 0.5f, 0), 2.0f, 14);
    }
    char t[160];
    snprintf(t, sizeof(t), "DRV%d %s frame %.0f blend %.2f upper %.2f wrist %.2f | fk err %.4f | L-arm skin hits %d %d %d", g_drv,
             g_drvSrc ? "in_car_idle" : "point_left", g_drvFrame, g_drvBlend, g_drvUpper, g_drvWristStraight, g_drvFkErr, g_skinHits[19], g_skinHits[20], g_skinHits[21]);
    r.Text('s', t, 20, (float)r.H - 20, 0.55f, Rgba(1, 1, 1), Rgba(1, 1, 1), ALIGN_LEFT, 1.0f);
}

static void DrvLog(float dt, uint32_t player)
{
    if (g_drv <= 0 || !g_drvInCar || (g_drvLogT += dt) < 1.0f) return;
    g_drvLogT = 0;
    PoserPose P;
    const DrvCar* info = nullptr;
    Mat4 cf;
    if (!Game_ReadPoser(player, &P) || !DrvMyCar(&info, &cf)) return;
    Mat4 inv = RigidInverse(cf);
    auto C = [&](Vec3 w) { return XformPoint(w, inv); };   // into the car's frame
    Vec3 root = C(Row(ToMat(P.world[3]), 3)), pel = C(Row(ToMat(P.world[4]), 3)), head = C(Row(ToMat(P.world[17]), 3));
    Vec3 sh = C(Row(ToMat(P.world[19]), 3)), el = C(Row(ToMat(P.world[20]), 3)), wr = C(Row(ToMat(P.world[21]), 3));
    Vec3 cpos = C(V3(P.position[0], P.position[1], P.position[2]));
    Log("drv: car frame: char+0x64 %.2f %.2f %.2f | root %.2f %.2f %.2f pelvis %.2f %.2f %.2f head %.2f %.2f %.2f | L shoulder %.2f %.2f %.2f "
        "elbow %.2f %.2f %.2f wrist %.2f %.2f %.2f | fk err %.5f", cpos.x, cpos.y, cpos.z, root.x, root.y, root.z, pel.x, pel.y, pel.z,
        head.x, head.y, head.z, sh.x, sh.y, sh.z, el.x, el.y, el.z, wr.x, wr.y, wr.z, g_drvFkErr);
    if (g_drvPosed) {
        int b0 = g_drvSide ? 26 : 19;
        Vec3 e2 = C(Row(g_drvWn[b0 + 1], 3)), w2 = C(Row(g_drvWn[b0 + 2], 3)), m2 = C(Row(g_drvWn[b0 + 4], 3));
        Vec3 gun = g_drvHaveGun ? C(XformPoint(V3(0, 0.03f, 0.05f), g_drvGun)) : V3(0, 0, 0);
        Vec3 muz = g_drvHaveGun ? C(XformPoint(MUZZLE, g_drvGun)) : V3(0, 0, 0);
        Log("drv: posed (%s f%.0f b%.2f): elbow %.2f %.2f %.2f wrist %.2f %.2f %.2f fingers %.2f %.2f %.2f | gun %.2f %.2f %.2f muzzle %.2f %.2f %.2f%s",
            g_drvSrc ? "idle" : g_drvSide ? "point_right" : "point_left", g_drvFrame, g_drvBlend, e2.x, e2.y, e2.z, w2.x, w2.y, w2.z, m2.x, m2.y, m2.z, gun.x, gun.y,
            gun.z, muz.x, muz.y, muz.z, info ? "" : " (no window data)");
        if (info)
            Log("drv: window plane x %.2f: wrist %+.2f m outside, muzzle %+.2f m outside, wrist height %.2f in [%.2f, %.2f], z %.2f in [%.2f, %.2f]",
                info->wmin.x, info->wmin.x - w2.x, info->wmin.x - muz.x, w2.y, info->wmin.y, info->wmax.y, w2.z, info->wmin.z, info->wmax.z);
    }
    Log("drv: skin hits joints 14-28: %d %d | %d %d %d %d | L %d %d %d %d %d %d %d | R %d %d %d | matched pieces near him %d", g_skinHits[14],
        g_skinHits[15], g_skinHits[16], g_skinHits[17], g_skinHits[3], g_skinHits[4], g_skinHits[19], g_skinHits[20], g_skinHits[21],
        g_skinHits[22], g_skinHits[23], g_skinHits[24], g_skinHits[25], g_skinHits[26], g_skinHits[27], g_skinHits[28], g_skinNear);
}
void Gun_DrvTick(float dt, uint32_t player)
{
    if (g_drv == 3) DhTick(dt, player);
    else DrvLog(dt, player);
}
static bool DaCmd(const char* cmd, const char* arg);   // (m3)
bool Gun_DrvCmd(const char* cmd, const char* arg)
{
    if (DhCmd(cmd, arg) || DaCmd(cmd, arg)) return true;
    if (!strcmp(cmd, "drv")) g_drv = atoi(arg);
    else if (!strcmp(cmd, "drvframe")) g_drvFrame = (float)atof(arg);
    else if (!strcmp(cmd, "drvblend")) g_drvBlend = (float)atof(arg);
    else if (!strcmp(cmd, "drvsrc")) g_drvSrc = atoi(arg);
    else if (!strcmp(cmd, "drvupper")) g_drvUpper = (float)atof(arg);
    else if (!strcmp(cmd, "drvmiss")) g_drvMissLog = atoi(arg);
    else if (!strcmp(cmd, "drvside")) g_drvSide = atoi(arg);
    else if (!strcmp(cmd, "drvwrist")) g_drvWristStraight = (float)atof(arg);
    else return false;
    Log("drv: mode %d frame %.1f blend %.2f source %s", g_drv, g_drvFrame, g_drvBlend, g_drvSrc ? "ndr_in_car_idle" : "nps_point_left");
    return true;
}

// "drvcam <yaw deg>,<dist>,<height>": the camera (drawing only) orbits his seat in the car's frame: yaw 0 in
// front of the car, -90 his window side; "drvcam 0,0" off
static float g_drvCamYaw, g_drvCamDist, g_drvCamH = 0.4f;
static bool DrvCamera()
{
    if (g_drvCamDist <= 0.0f || !g_drvInCar) return false;
    const DrvCar* info = nullptr;
    Mat4 cf;
    Vec3 seat;
    if (g_drv == 3 && g_dhSeat.front) cf = g_dhSeat.cf, seat = g_dhSeat.local;   // (m2) round his own seat, either side
    else if (!DrvMyCar(&info, &cf)) return false;
    else seat = info ? info->seat : V3(-0.5f, 0, 0);
    Vec3 tgt = XformPoint(seat + V3(0, 0.55f, 0), cf);
    float y = g_drvCamYaw * PI / 180.0f;
    Vec3 cp = XformPoint(seat + V3(sinf(y) * g_drvCamDist, 0.55f + g_drvCamH, cosf(y) * g_drvCamDist), cf);
    Vec3 fw = Norm(tgt - cp), rr = Norm(Cross(UP, fw)), uu = Cross(fw, rr);
    float gw[16], fov;
    if (!Game_GetGameCamera(gw, &fov)) return false;
    Game_OverrideCamera(WithTrans(Basis(rr, uu, fw), cp).m, fov * 0.8f, 1.0f);
    return true;
}
bool Gun_DrvCamCmd(const char* cmd, const char* arg)
{
    if (strcmp(cmd, "drvcam")) return false;
    g_drvCamDist = 0;
    sscanf(arg, "%f,%f,%f", &g_drvCamYaw, &g_drvCamDist, &g_drvCamH);
    return true;
}
#include "driveby_aim.inc"   // (diagnostic, driveby-m3)
static float DaCamAim() { return g_da.on ? g_da.camAim : 0.0f; }
// (m3.1) the visibility probe and the radar's kept patch, for wheel.cpp
int Gun_DaVisWant(float (*px)[2], float (*range)[2], int max, int W, int H) { return DaVisWant(px, range, max, W, H, W * 0.5f, H * 0.5f); }
void Gun_DaVisResult(const float* d, int n) { DaVisResult(d, n); }
void Gun_AfterWorld(Renderer& r) { DaKeepRadar(r); }
#else
// (shooting from cars left out of this build: each stand-in does what the real one does with DriveBy=0 - nothing)
void Gun_DrvOverlay(Renderer&, uint32_t) {}
void Gun_DrvTick(float, uint32_t) {}
bool Gun_DrvCmd(const char*, const char*) { return false; }
bool Gun_DrvCamCmd(const char*, const char*) { return false; }
static bool DrvCamera() { return false; }
static bool DaCamera() { return false; }
static float DaCamAim() { return 0.0f; }
static void DaSweep(PoserPose&) {}
static void DaUpdate(float, const Pad&, const GameState&, const Kbm&, bool, bool, bool, int*, WORD*) {}
static void DaFromIni() { Log("DriveBy=1: shooting from cars isn't part of this release (it's planned for a later one) - ignored"); }
static void DaOverlay(Renderer&) {}
static void DaRadarOverlay(Renderer&) {}
static bool DaWantsProbe() { return false; }
int Gun_DaVisWant(float (*)[2], float (*)[2], int, int, int) { return 0; }
void Gun_DaVisResult(const float*, int) {}
void Gun_AfterWorld(Renderer&) {}
#endif
