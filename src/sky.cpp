// The Duff plane: the little plane towing a "DRINK DUFF" banner round Evergreen Terrace (Level 1,
// English release). Shoot it or its banner and it opens the throttle: it races round its circle,
// then eases back to its normal speed.
//
// It isn't a game object with collision: it's part of the sky (L1_TERRA.p3d, the "horizon1"
// composite drawable, joint "Plane1", mesh "Plane1Shape" with two primitive groups: the plane,
// 198 vertices / 338 triangles, and the banner and its tow lines, 17 / 27). The sky is drawn first
// in each frame, centred on the camera, and a 133 s looping animation turns Plane1 a full circle
// about its joint, swinging the mesh round at about 2200 units. Pure3D hands Direct3D each piece's
// model*view as the WORLD matrix (scene.cpp), so:
//  - the draw hook recognises the two draws, and rotates the WORLD matrix's rotation part about
//    the sky's vertical axis through the joint by an extra heading (the throttle's lead), which
//    is what turning Plane1 further along its animation looks like - only the plane speeds up,
//    not the turning cloud layers of the same animation;
//  - a shot that reaches the sky (hits nothing in the world) is tested against the plane's and the
//    banner's boxes as last drawn; the sky is camera-centred, so only the shot's direction counts.
#include "hit.h"

// the mesh's two primitive groups, in the joint's frame (from L1_TERRA.p3d)
static const Vec3 kPlaneMin = {-2338.7f, 527.3f, -107.8f}, kPlaneMax = {-1954.6f, 709.1f, 368.0f};
static const Vec3 kBannerMin = {-2189.7f, 558.0f, -920.6f}, kBannerMax = {-2102.9f, 677.6f, -107.8f};
static const float kMargin = 25.0f;                        // about a degree of slack round each box
static const float kLoopSeconds = 4001.0f / 30.0f;         // the animation: 4001 frames at 30 fps
static const float kTopSpeed = 5.0f;                       // throttle wide open: times its normal speed

static struct {
    bool have;          // drawn recently
    double drawnAt;
    Mat4 joint;         // the plane's frame in sky space (camera-centred, world axes), as drawn
    float lead;         // extra heading so far (radians)
    float turn;         // the natural direction of turn (+1 / -1), measured
    float lastAz;       // natural heading of the mesh about the joint, last frame
    double lastAzT;
    float throttle;     // 0..1 (eases toward 1 while held open)
    float hold;         // seconds the throttle stays open
    int hits;
    uint32_t level;     // the player Character the lead belongs to (a new one = a new level)
} S = {false, 0, {}, 0, 1, 0, 0, 0, 0, 0, 0};

int g_skyLog;   // debug "skylog <frames>"

static Mat4 RotY(float a)
{
    Mat4 r = Identity();
    float c = cosf(a), s = sinf(a);
    r.at(0, 0) = c;   // row vectors: x' = x c - z s, z' = x s + z c
    r.at(0, 2) = -s;
    r.at(2, 0) = s;
    r.at(2, 2) = c;
    return r;
}

static Mat4 Rot3x3(const Mat4& m)
{
    Mat4 r = m;
    r.m[3] = r.m[7] = r.m[11] = 0;
    r.m[12] = r.m[13] = r.m[14] = 0;
    r.m[15] = 1;
    return r;
}

static Mat4 Transposed3(const Mat4& m)
{
    Mat4 r = Identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.at(i, j) = m.at(j, i);
    return r;
}

// Called for every indexed draw: if it is one of the Duff plane's two pieces, give the WORLD
// matrix to use (false = leave it). They are told by their vertex and triangle counts, being among
// the first draws of the frame (the sky pass) and the joint about 237 units from the camera.
bool Sky_PlaneDraw(UINT nv, UINT prims, int drawIndex, const float* world, float* out)
{
    bool plane = nv == 198 && prims == 338, banner = nv == 17 && prims == 27;
    if (!(plane || banner) || drawIndex > 40) return false;
    float tl = sqrtf(world[12] * world[12] + world[13] * world[13] + world[14] * world[14]);
    if (tl < 200.0f || tl > 280.0f) return false;
    if (strcmp(Game_VersionName(), "English")) return false;
    float v[16], p[16];
    if (!Game_GetCamera(v, p)) return false;
    Mat4 W, V;
    memcpy(W.m, world, 64);
    memcpy(V.m, v, 64);
    Mat4 Vr = Rot3x3(V), VrT = Transposed3(Vr);
    // the joint in sky space: WORLD = joint * Vr  ->  joint = WORLD * Vr^T
    Mat4 joint = Mul(W, VrT);
    double now = Time_Real();
    if (plane) {
        // measure the natural direction of turn (the drawn heading minus our lead)
        Vec3 c = (kPlaneMin + kPlaneMax) * 0.5f;
        Vec3 off = XformDir(c, joint);
        float az = atan2f(off.x, off.z);
        double dt = now - S.lastAzT;
        if (S.lastAzT > 0 && dt > 0.005 && dt < 0.5) {
            float d = az - S.lastAz;
            while (d > 3.14159265f) d -= 6.2831853f;
            while (d < -3.14159265f) d += 6.2831853f;
            if (fabsf(d) > 1e-6f && fabsf(d) < 0.05f) S.turn = d > 0 ? 1.0f : -1.0f;
        }
        S.lastAz = az;
        S.lastAzT = now;
    }
    // the lead: turn the joint further about the sky's vertical axis through itself
    Mat4 R = RotY(S.lead);
    Mat4 jr = Mul(Rot3x3(joint), R);
    SetRow(jr, 3, Row(joint, 3), 1);
    Mat4 Wn = Mul(jr, Vr);
    SetRow(Wn, 3, Row(W, 3), 1);   // the joint stays where it is
    memcpy(out, Wn.m, 64);
    if (plane) {
        S.joint = jr;
        S.have = true;
        S.drawnAt = now;
    }
    if (g_skyLog > 0 && plane) {
        g_skyLog--;
        Vec3 pc = XformPoint((kPlaneMin + kPlaneMax) * 0.5f, jr);
        Log("sky: plane at (%.0f %.0f %.0f) from the camera, heading lead %.2f rad, turn %+.0f, throttle %.2f, hold %.1f s",
            pc.x, pc.y, pc.z, S.lead, S.turn, S.throttle, S.hold);
    }
    return true;
}

// ray from the camera (the sky's centre) along dir, against a box in the joint's frame
static bool RayBox(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx)
{
    float t0 = 0.0f, t1 = 1e9f;
    const float* O = &o.x;
    const float* D = &d.x;
    const float* A = &mn.x;
    const float* B = &mx.x;
    for (int i = 0; i < 3; i++) {
        float lo = A[i] - kMargin, hi = B[i] + kMargin;
        if (fabsf(D[i]) < 1e-8f) {
            if (O[i] < lo || O[i] > hi) return false;
            continue;
        }
        float a = (lo - O[i]) / D[i], b = (hi - O[i]) / D[i];
        if (a > b) {
            float t = a;
            a = b;
            b = t;
        }
        t0 = fmaxf(t0, a);
        t1 = fminf(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

// A shot that reached the sky, going along dir (world): did it hit the plane or its banner?
// Hit: the throttle opens. Returns 1 the plane, 2 the banner, 0 neither.
int Sky_Shot(Vec3 dir)
{
    if (!S.have || Time_Real() - S.drawnAt > 0.5) return 0;
    // into the joint's frame: origin (the camera, 0) and direction
    Mat4 jt = Transposed3(S.joint);
    Vec3 t = Row(S.joint, 3);
    Vec3 o = XformDir(t * -1.0f, jt), d = XformDir(Norm(dir), jt);
    int what = RayBox(o, d, kPlaneMin, kPlaneMax) ? 1 : RayBox(o, d, kBannerMin, kBannerMax) ? 2 : 0;
    if (what) {
        S.hold = 4.0f;
        S.hits++;
        Log("sky: shot the Duff plane's %s (hit %d): throttle open", what == 1 ? "plane" : "banner", S.hits);
    }
    return what;
}

// once a frame (real seconds; the plane runs on game time, so slow motion slows it too)
void Sky_Update(float dt, uint32_t player)
{
    if (player && player != S.level) {
        S.level = player;
        S.lead = 0;
        S.throttle = S.hold = 0;
        S.have = false;
    }
    if (S.hold > 0) {
        S.hold -= dt;
        S.throttle = fminf(1.0f, S.throttle + dt / 1.0f);    // the engine spools up
    } else if (S.throttle > 0) {
        S.throttle = fmaxf(0.0f, S.throttle - dt / 4.0f);    // and winds back down
    }
    float k = 1.0f + (kTopSpeed - 1.0f) * S.throttle * S.throttle * (3.0f - 2.0f * S.throttle);
    S.lead += S.turn * (6.2831853f / kLoopSeconds) * (k - 1.0f) * dt * (float)Time_GetScale();
    if (S.lead > 6.2831853f) S.lead -= 6.2831853f;
    if (S.lead < -6.2831853f) S.lead += 6.2831853f;
}

// debug: the direction from the camera (world) to the plane (part 0) or the banner (part 1)
bool Sky_PlaneDir(int part, Vec3* dir)
{
    if (!S.have) return false;
    Vec3 c = part ? (kBannerMin + kBannerMax) * 0.5f : (kPlaneMin + kPlaneMax) * 0.5f;
    *dir = Norm(XformPoint(c, S.joint));
    return true;
}

void Sky_DebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "skylog")) g_skyLog = arg[0] ? atoi(arg) : 1;
    else if (!strcmp(cmd, "throttle")) {   // "throttle": open it as if the plane was shot
        S.hold = 4.0f;
        Log("sky: throttle opened by hand");
    }
}
