// Cars' tires: shot flat.
//  - Every shot is traced against the wheels of the cars it passes (a short cylinder each, as big as
//    the game's wheel, standing on the car's axle) before whatever the game says it hit: the ground
//    under a car, the car's rough collision box, or its body as drawn, if a tire is in front of it.
//  - A tire that's hit pops: a bang, a puff of the game's tyre smoke and its gas-leak hiss; air keeps
//    puffing out of the hole while it goes down over a few seconds. A rocket's blast blows out the
//    tires near it at once.
//  - Flat is the game's own physics: the wheel's radius (Wheel+12, which the car's suspension reads
//    every frame) is taken down to the rim, and that corner of the car sinks onto it.
//  - On the rim: less grip (Vehicle+708, also read every frame), the car pulls toward its flat side
//    and drags, a flat front wheel shimmies, the rim thumps once a turn and throws sparks at speed.
//  - The drawn tire (each wheel is a draw of its own, at its centre) is squashed against the road.
//  - All of it is put back when the car leaves (traffic cars are reused), is moved (respawned) or is
//    repaired, or is wrecked.
// A Vehicle's wheels: Wheel* at Vehicle+1044..1056. Wheel: +0 its Vehicle, +4 steers, +5 drives,
// +8 its number, +12 radius, +20 suspension offset (up), +28 how far it has turned, +52 on the
// ground, +60 steering angle. Where they sit at rest (the car's frame): Vehicle+888 + 12i. The car's
// SimState: Vehicle+1192 (velocity +0x50, angular velocity +0x5C).
#include "render.h"
#include "math3d.h"
#include "hit.h"

static const float kRim = 0.5f;         // a flat tire's radius on its rim, of the full one
static const float kLeak = 0.45f;       // how fast the air goes (of what's left, per second)
static const float kPopDrop = 0.12f;    // the first drop, at the bang
static const float kBlowout = 0.8f;     // a blast: most of it at once
static const float kGripLoss = 0.16f;   // of the car's grip, per flat tire
static const float kGripMin = 0.38f;
static const float kPull = 1.0f;        // the turn toward a flat side (rad/s^2 per flat tire, from 8 m/s)
static const float kDrag = 0.9f;        // m/s^2 per flat tire
static const uint32_t V_Vehicle = 0x60832C;
enum { PFX_PUFF = 9 };

static bool SafeRead(uint32_t a, void* out, size_t n)
{
    SIZE_T got = 0;
    return a >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)a, out, n, &got) && got == n;
}
static bool SafeWrite(uint32_t a, const void* in, size_t n)
{
    SIZE_T done = 0;
    return a >= 0x10000 && WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)a, in, n, &done) && done == n;
}
template <class T> static T Rd(uint32_t a, T def = T())
{
    T v;
    return SafeRead(a, &v, sizeof(T)) ? v : def;
}

static uint32_t g_rng = 0x7F4A7C15u;
static float Rnd()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

struct WheelInfo {
    uint32_t w;
    bool steer, drive;
    int num;
    float radius, yoff, spin, angle;
    bool touch;
    Vec3 rest, centre;   // the car's frame / the world
};

static bool CarFrame(uint32_t veh, Mat4* m)
{
    float f[16];
    if (!SafeRead(veh + 184, f, 64)) return false;
    memcpy(m->m, f, 64);
    return true;
}

static bool ReadWheel(uint32_t veh, int i, const Mat4& frame, WheelInfo* o)
{
    o->w = Rd<uint32_t>(veh + 1044 + 4 * i);
    uint8_t b[64];
    if (!o->w || !SafeRead(o->w, b, 64) || !SafeRead(veh + 888 + 12 * i, &o->rest, 12)) return false;
    o->steer = b[4], o->drive = b[5];
    o->num = *(int*)(b + 8);
    o->radius = *(float*)(b + 12), o->yoff = *(float*)(b + 20), o->spin = *(float*)(b + 28), o->angle = *(float*)(b + 60);
    o->touch = b[52];
    o->centre = XformPoint(V3(o->rest.x, o->rest.y + o->yoff, o->rest.z), frame);
    return o->radius > 0.05f && o->radius < 3.0f;
}

// The car's physics object (an ArticulatedPhysicsObject, SimState+0x70) keeps its spin as angular
// momentum (+188, the world's axes); each physics step adds its torques to it and works the angular
// velocity out from it (SimState+0x5C = the inverse inertia at +272 times it: a symmetric matrix, xx xy
// xz yy yz zz from +276), so the angular velocity can't be written, only the momentum. A turn is added
// as momentum: the inertia (the inverse's inverse) times the change wanted.
static const uint32_t V_ArticulatedPhysicsObject = 0x5F3B64;
static bool CarAddSpin(uint32_t veh, Vec3 dw)
{
    uint32_t sim = Rd<uint32_t>(veh + 1192), obj = sim ? Rd<uint32_t>(sim + 0x70) : 0;
    float s[6];
    Vec3 L;
    if (!obj || Rd<uint32_t>(obj) != V_ArticulatedPhysicsObject || !SafeRead(obj + 276, s, 24) || !SafeRead(obj + 188, &L, 12)) return false;
    float a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5];   // | a b c ; b d e ; c e f |
    float A = d * f - e * e, B = c * e - b * f, C = b * e - c * d;
    float det = a * A + b * B + c * C;
    if (!(fabsf(det) > 1e-12f)) return false;
    float D = a * f - c * c, E = b * c - a * e, F = a * d - b * b, k = 1.0f / det;
    Vec3 dL = {(A * dw.x + B * dw.y + C * dw.z) * k, (B * dw.x + D * dw.y + E * dw.z) * k, (C * dw.x + E * dw.y + F * dw.z) * k};
    if (!(fabsf(dL.x) + fabsf(dL.y) + fabsf(dL.z) < 1e7f)) return false;
    L = L + dL;
    return SafeWrite(obj + 188, &L, 12);
}

// drunk driving (gun.cpp): his car wanders - a turn added to its spin, a push sideways into its velocity (both through the game's own
// physics state, as the flat tire's pull is: nothing kept, nothing to put back). Only a real Vehicle with physics (not on rails).
bool Tires_DrunkNudge(uint32_t veh, float yawAcc, float sideAcc, float dt)
{
    if (!veh || Rd<uint32_t>(veh) != V_Vehicle || Rd<uint32_t>(veh + 388) == 1 || dt <= 0.0f) return false;
    uint32_t sim = Rd<uint32_t>(veh + 1192);
    float T[16];
    Vec3 vel;
    if (!sim || !SafeRead(veh + 184, T, 64) || !SafeRead(sim + 0x50, &vel, 12)) return false;
    Vec3 right = {T[0], T[1], T[2]}, up = {T[4], T[5], T[6]};
    float yaw = fmaxf(-3.0f, fminf(3.0f, yawAcc)) * dt, side = fmaxf(-4.0f, fminf(4.0f, sideAcc)) * dt;
    bool ok = fabsf(yaw) < 1e-6f || CarAddSpin(veh, up * yaw);
    if (fabsf(side) > 1e-6f) {
        vel = vel + right * side;
        ok = SafeWrite(sim + 0x50, &vel, 12) && ok;
    }
    return ok;
}

// ------------------------------------------------------------------ the cars with a tire gone
struct Tire {
    uint32_t w;      // the game's Wheel
    float r0;        // its radius, full
    float lastR;     // the radius we wrote last (0: never)
    float flat;      // 0 full .. 1 on the rim
    bool popped, blown;
    Vec3 hole, holeN;   // where the shot went in (the car's frame): the air comes out there
    float puffT, sparkT;
};
struct TCar {
    uint32_t veh;
    Tire t[4];
    float grip0, lastGrip;
    float hp;
    int type;        // Vehicle+0xA4: 0 his, 1 AI, 2 traffic
    Vec3 lastPos;
    float shimmy;
    float missing;   // seconds out of the game's list of cars (a traffic car leaving its lane drops out for a moment)
};
static TCar g_tc[16];
static int g_ntc;
static int g_tireLog;   // debug "tirelog <n>": the next n events, and the cars' state each second
static float g_logT;

// still the car we changed: a Vehicle, with the same wheels, and our radius still on them
static bool Intact(const TCar& c)
{
    if (Rd<uint32_t>(c.veh) != V_Vehicle) return false;
    for (int i = 0; i < 4; i++) {
        uint32_t w = Rd<uint32_t>(c.veh + 1044 + 4 * i);
        if (w != c.t[i].w || Rd<uint32_t>(w) != c.veh || Rd<int>(w + 8) != i) return false;
    }
    return true;
}

static void Restore(TCar& c, const char* why)
{
    bool ok = Intact(c);
    if (ok) {
        for (int i = 0; i < 4; i++) {
            const Tire& t = c.t[i];
            if (t.lastR > 0.0f && Rd<float>(t.w + 12) == t.lastR) SafeWrite(t.w + 12, &t.r0, 4);
        }
        if (c.lastGrip > 0.0f && Rd<float>(c.veh + 708) == c.lastGrip) SafeWrite(c.veh + 708, &c.grip0, 4);
    }
    if (g_tireLog > 0) Log("tires: %08X's tires %s (%s)", c.veh, ok ? "put back" : "left (not the same car any more)", why);
}

static TCar* CarOf(uint32_t veh, bool create)
{
    for (int i = 0; i < g_ntc; i++)
        if (g_tc[i].veh == veh) return &g_tc[i];
    if (!create || g_ntc >= 16 || Rd<uint32_t>(veh) != V_Vehicle) return nullptr;
    TCar c = {};
    c.veh = veh;
    Mat4 f;
    if (!CarFrame(veh, &f)) return nullptr;
    for (int i = 0; i < 4; i++) {
        WheelInfo wi;
        if (!ReadWheel(veh, i, f, &wi) || Rd<uint32_t>(wi.w) != veh) return nullptr;
        c.t[i].w = wi.w;
        c.t[i].r0 = wi.radius;
    }
    c.grip0 = Rd<float>(veh + 708);
    c.hp = Rd<float>(veh + 0x4A0);
    c.type = Rd<int>(veh + 0xA4);
    c.lastPos = Row(f, 3);
    g_tc[g_ntc] = c;
    return &g_tc[g_ntc++];
}

static void PlayAt(int snd, Vec3 p, float vol, float pitch)
{
    float d = 15.0f;
    float view[16], proj[16];
    if (Game_GetCamera(view, proj)) {
        Mat4 v;
        memcpy(v.m, view, 64);
        d = Len(p - Row(RigidInverse(v), 3));
    }
    float k = Clamp01f(1.15f - d / 60.0f) * vol;
    if (k > 0.01f) Sound_Play(snd, k, pitch);
}

// A traffic car rides its lane on rails (Vehicle+388: 1), where its wheels don't touch anything: a
// flat one goes over to the physics, as one that's rammed does (Vehicle::SetLocomotion 0x4E9660:
// ESI the car, EDI 0 physics), so it comes down onto its rim and rolls to a stop.
static void ToPhysics(uint32_t veh)
{
    if (!Game_IsEnglish() || Rd<int>(veh + 388, -1) != 1) return;
    if (g_tireLog > 0) Log("tires: %08X leaves its lane (traffic -> physics)", veh);
    __asm__ __volatile__("push %%esi\n\t"
                         "push %%edi\n\t"
                         "mov %%ebx, %%esi\n\t"
                         "xor %%edi, %%edi\n\t"
                         "mov $0x4E9660, %%eax\n\t"
                         "call *%%eax\n\t"
                         "pop %%edi\n\t"
                         "pop %%esi\n\t"
                         :
                         : "b"(veh)
                         : "eax", "ecx", "edx", "memory", "cc");
}

// a tire goes: a shot (the hole at pos) or a blast (blown out at once)
static void Pop(uint32_t veh, int wheel, Vec3 pos, Vec3 normal, bool blast)
{
    TCar* c = CarOf(veh, true);
    if (!c || wheel < 0 || wheel > 3) return;
    Tire& t = c->t[wheel];
    Mat4 f;
    if (!CarFrame(veh, &f)) return;
    Vec3 dirt = XformPoint(pos, FullInverse(f));
    if (t.popped) {   // already flat: a shot into the rim
        if (!blast) {
            Fx_Sparks(pos, normal, 0.35f);
            PlayAt(SND_METAL1 + (int)(Rnd() * 2.99f), pos, 0.6f, 1.15f + Rnd() * 0.15f);
        } else if (!t.blown) {
            t.blown = true;
        }
        return;
    }
    t.popped = true;
    t.blown = blast;
    ToPhysics(veh);
    t.flat = blast ? kBlowout : kPopDrop;
    t.hole = dirt;
    t.holeN = Norm(XformDir(normal, FullInverse(f)));
    t.puffT = 0.05f;
    Fx_Particle(PFX_PUFF, pos + normal * 0.05f, normal, blast ? 0.9f : 0.6f);
    if (!blast) {
        PlayAt(SND_POP, pos, 1.0f, 1.05f + Rnd() * 0.15f);
        PlayAt(SND_HISS, pos, 0.75f, 1.0f + Rnd() * 0.1f);
    }
    if (g_tireLog > 0) {
        g_tireLog--;
        Log("tires: %08X wheel %d %s at %.2f,%.2f,%.2f (radius %.3f)", veh, wheel, blast ? "blown out" : "shot", pos.x, pos.y, pos.z, t.r0);
    }
}

// ------------------------------------------------------------------ a shot through the wheels
// a wheel: a short cylinder on its axle (the tread, then both sides)
static bool RayWheel(Vec3 o, Vec3 d, Vec3 c, Vec3 a, float R, float hw, float* tOut, Vec3* nOut)
{
    Vec3 oc = o - c;
    float oa = Dot(oc, a), da = Dot(d, a);
    Vec3 op = oc - a * oa, dp = d - a * da;
    float best = 1e9f;
    Vec3 bn = {0, 1, 0};
    float A = Dot(dp, dp), B = 2.0f * Dot(op, dp), C = Dot(op, op) - R * R;
    if (A > 1e-8f) {
        float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            float t = (-B - sqrtf(disc)) / (2.0f * A);   // going in
            if (t > 0.0f && fabsf(oa + da * t) <= hw) best = t, bn = Norm(op + dp * t);
        }
    }
    if (fabsf(da) > 1e-6f)
        for (int s = -1; s <= 1; s += 2) {
            float t = ((float)s * hw - oa) / da;
            Vec3 q = op + dp * t;
            if (t > 0.0f && t < best && Dot(q, q) <= R * R) best = t, bn = a * (float)s;
        }
    if (best > 1e8f) return false;
    if (Dot(bn, d) > 0.0f) bn = bn * -1.0f;
    *tOut = best;
    *nOut = bn;
    return true;
}

static uint32_t g_tiresIgnore;   // (drive-by) the car he shoots from: its own tires are never in the way
void Tires_SetIgnore(uint32_t vehicle) { g_tiresIgnore = vehicle; }

bool Tires_Raycast(Vec3 from, Vec3 dir, float maxT, TireHit* out)
{
    uint32_t ptrs[30];
    float pos[30][3];
    int n = Game_ListVehiclePtrs(ptrs, pos, 30);
    float best = maxT;
    bool any = false;
    for (int i = 0; i < n; i++) {
        if (ptrs[i] == g_tiresIgnore) continue;
        Vec3 rel = V3(pos[i][0], pos[i][1], pos[i][2]) - from;
        float along = Dot(rel, dir);
        if (along < -4.0f || along > maxT + 4.0f || Len(rel - dir * along) > 4.0f) continue;   // not near the shot
        uint8_t dead = 0;
        Mat4 f;
        if (Rd<uint32_t>(ptrs[i]) != V_Vehicle || (SafeRead(ptrs[i] + 0x494, &dead, 1) && dead) || !CarFrame(ptrs[i], &f)) continue;
        Vec3 axle = Norm(Row(f, 0));
        const TCar* tc = CarOf(ptrs[i], false);
        for (int k = 0; k < 4; k++) {
            WheelInfo w;
            if (!ReadWheel(ptrs[i], k, f, &w)) continue;
            float R = tc ? tc->t[k].r0 : w.radius;   // (as drawn: a flat one is still that tall at the top)
            float t;
            Vec3 nrm;
            if (RayWheel(from, dir, w.centre, axle, R, fmaxf(0.1f, R * 0.42f), &t, &nrm) && t < best) {
                best = t;
                any = true;
                out->veh = ptrs[i];
                out->wheel = k;
                out->pos = from + dir * t;
                out->normal = nrm;
            }
        }
    }
    return any;
}

void Tires_Shot(uint32_t veh, int wheel, Vec3 pos, Vec3 normal) { Pop(veh, wheel, pos, normal, false); }

// (drive-by tests) where a car's wheel k is: its centre and radius
bool Tires_WheelCentre(uint32_t veh, int k, Vec3* centre, float* radius)
{
    Mat4 f;
    WheelInfo w;
    if (!veh || !CarFrame(veh, &f) || !ReadWheel(veh, k, f, &w)) return false;
    *centre = w.centre;
    if (radius) *radius = w.radius;
    return true;
}

// a blast at c next to a car: the tires within reach blow out
void Tires_Blast(uint32_t veh, Vec3 c, float reach)
{
    Mat4 f;
    if (!CarFrame(veh, &f)) return;
    for (int k = 0; k < 4; k++) {
        WheelInfo w;
        if (ReadWheel(veh, k, f, &w) && Len(w.centre - c) < reach) Pop(veh, k, w.centre + Norm(c - w.centre) * w.radius, Norm(c - w.centre), true);
    }
}

// ------------------------------------------------------------------ each frame (game time)
static void Step(TCar& c, float dt)
{
    Mat4 f;
    if (!CarFrame(c.veh, &f)) return;
    Vec3 right = Norm(Row(f, 0)), up = Norm(Row(f, 1)), fwd = Norm(Row(f, 2));
    uint32_t sim = Rd<uint32_t>(c.veh + 1192);
    Vec3 vel = {}, ang = {};
    bool haveSim = sim && SafeRead(sim + 0x50, &vel, 12) && SafeRead(sim + 0x5C, &ang, 12);
    float vf = Dot(vel, fwd), speed = Len(vel);
    float sumFlat = 0.0f, frontFlat = 0.0f;
    Vec3 dAng = {0, 0, 0};
    for (int i = 0; i < 4; i++) {
        Tire& t = c.t[i];
        if (!t.popped) continue;
        t.flat += (1.0f - t.flat) * (1.0f - expf(-(t.blown ? 3.0f : kLeak) * dt));
        WheelInfo w;
        if (!ReadWheel(c.veh, i, f, &w)) continue;
        // down toward the rim; on it, a thump once a turn (the folded tire going under)
        float r = t.r0 * (1.0f - (1.0f - kRim) * t.flat);
        if (t.flat > 0.8f && speed > 1.5f) r += t.r0 * 0.04f * powf(fmaxf(0.0f, sinf(w.spin)), 10.0f);
        if (fabsf(r - w.radius) > 1e-4f && SafeWrite(t.w + 12, &r, 4)) t.lastR = r;   // (also if the game set it anew)
        sumFlat += t.flat;
        bool front = w.rest.z > 0.0f;
        if (front) frontFlat += t.flat;
        // the drag on its side turns the car toward it (going forward; the other way in reverse)
        if (w.touch) {
            Vec3 side = right * (w.rest.x > 0.0f ? 1.0f : -1.0f);
            float s = Clamp01f((fabsf(vf) - 1.0f) / 7.0f) * (vf > 0.0f ? 1.0f : -1.0f);
            dAng = dAng + Cross(fwd, side) * (kPull * (front ? 1.0f : 0.6f) * t.flat * s * dt);
        }
        // air out of the hole while it goes down
        if (!t.blown && t.flat < 0.93f && (t.puffT -= dt) <= 0.0f) {
            t.puffT = 0.14f + Rnd() * 0.12f;
            Vec3 hole = XformPoint(t.hole, f), out = Norm(XformDir(t.holeN, f));
            Fx_Particle(PFX_PUFF, hole, out, 0.08f + 0.22f * (1.0f - t.flat));
        }
        // the rim on the road at speed
        if (t.flat > 0.85f && w.touch && speed > 7.0f && (t.sparkT -= dt) <= 0.0f) {
            t.sparkT = 0.05f + Rnd() * 0.12f;
            Fx_Sparks(w.centre - up * (r * 0.95f) + right * ((w.rest.x > 0.0f ? 1.0f : -1.0f) * 0.05f),
                      Norm(vel * (-1.0f / speed) + up * 0.6f), 0.25f + 0.25f * Clamp01f((speed - 7.0f) / 15.0f));
        }
    }
    float g = c.grip0 * fmaxf(kGripMin, 1.0f - kGripLoss * sumFlat);
    if (fabsf(g - Rd<float>(c.veh + 708)) > 1e-4f && SafeWrite(c.veh + 708, &g, 4)) c.lastGrip = g;
    if (!haveSim || sumFlat <= 0.0f) return;
    // Standing on it: the game's tires let a car tilted onto a flat creep round on the spot (on a slope,
    // for ever, a few degrees a second), so it is held still: no sliding sideways, no turning. (Below
    // walking pace: moving off and turning aren't held back.)
    if (speed < 0.6f) {
        float k = 1.0f - expf(-8.0f * dt);
        vel = vel - right * (Dot(vel, right) * k);
        SafeWrite(sim + 0x50, &vel, 12);
        CarAddSpin(c.veh, up * (-Dot(ang, up) * k));
        return;
    }
    if (speed < 0.5f) return;
    // a flat front wheel shimmies; the flats drag
    c.shimmy += dt * 11.0f;
    dAng = dAng + up * (sinf(c.shimmy) * 2.2f * frontFlat * Clamp01f((speed - 3.0f) / 10.0f) * dt);
    float dv = fminf(fabsf(vf), kDrag * sumFlat * dt) * (vf > 0.0f ? 1.0f : -1.0f);
    vel = vel - fwd * dv;
    SafeWrite(sim + 0x50, &vel, 12);
    CarAddSpin(c.veh, dAng);
}

void Tires_Update(float dt)
{
    if (!g_ntc) return;
    uint32_t ptrs[30];
    float pos[30][3];
    int n = Game_ListVehiclePtrs(ptrs, pos, 30);
    g_logT += dt;
    bool logNow = g_tireLog > 0 && g_logT >= 1.0f;
    if (logNow) g_logT = 0.0f;
    for (int k = 0; k < g_ntc;) {
        TCar& c = g_tc[k];
        int j = -1;
        for (int i = 0; i < n && j < 0; i++)
            if (ptrs[i] == c.veh) j = i;
        Vec3 p = j >= 0 ? V3(pos[j][0], pos[j][1], pos[j][2]) : c.lastPos;
        float hp = Rd<float>(c.veh + 0x4A0);
        uint8_t dead = 0;
        SafeRead(c.veh + 0x494, &dead, 1);
        if (j < 0 && (c.missing += fmaxf(dt, 0.016f)) < 1.5f) {   // out of the list: wait a little, write nothing
            if (g_tireLog > 0 && c.missing < 0.05f) Log("tires: %08X is out of the game's list of cars", c.veh);
            k++;
            continue;
        }
        if (j >= 0) c.missing = 0.0f;
        // a traffic car he gets into becomes his (its type changes, its hit points are set anew): not a repair
        int type = Rd<int>(c.veh + 0xA4, c.type);
        if (type != c.type) {
            if (g_tireLog > 0) Log("tires: %08X is now type %d (was %d)", c.veh, type, c.type);
            c.type = type;
            c.hp = hp;
        }
        const char* why = j < 0 ? "gone" : Len(p - c.lastPos) > 12.0f ? "moved" : !Intact(c) ? "changed" : dead ? "wrecked"
                        : hp > c.hp + 0.001f ? "repaired" : nullptr;
        if (why) {
            Restore(c, why);
            g_tc[k] = g_tc[--g_ntc];
            continue;
        }
        c.hp = hp;
        c.lastPos = p;
        // the traffic takes a car that has come to rest back onto its lane (0x501BC8: rails again, its type
        // traffic again) - and on rails a flat is nothing: it drove off as if new. One on its rims stays off.
        if (Rd<int>(c.veh + 388, -1) == 1) {
            bool flat = false;
            for (int i = 0; i < 4; i++) flat |= c.t[i].popped;
            if (flat) ToPhysics(c.veh);
        }
        if (dt > 0.0f) Step(c, dt);
        if (logNow) {
            uint32_t sim = Rd<uint32_t>(c.veh + 1192);
            Vec3 v = Rd<Vec3>(sim + 0x50), a = Rd<Vec3>(sim + 0x5C);
            Mat4 f;
            CarFrame(c.veh, &f);
            Vec3 fw = Row(f, 2);
            Log("tires: %08X flat %.2f %.2f %.2f %.2f grip %.2f/%.2f speed %.1f (fwd %.1f) yaw rate %+.2f heading %+.3f", c.veh,
                c.t[0].flat, c.t[1].flat, c.t[2].flat, c.t[3].flat, Rd<float>(c.veh + 708), c.grip0, Len(v), Dot(v, Norm(fw)), a.y,
                atan2f(fw.x, fw.z));
        }
        k++;
    }
}

bool Tires_Any() { return g_ntc > 0; }

// a new level: the cars are gone (nothing is written back)
void Tires_Clear() { g_ntc = 0; }

// ------------------------------------------------------------------ the flat ones, drawn squashed
// Each wheel is a draw of its own, drawn at its centre with its X along the axle. A flat one is
// drawn squashed toward its top (which stays where it was under the wheel arch), its bottom on the
// road where the rim now rides, and a little wider (the sidewalls bulging).
static struct Sq { Vec3 c, up, axle; float r0, r; } g_sq[64];
static int g_nsq, g_sqFrame = -1;
static Mat4 g_viewInv, g_view;

bool Tires_OnDraw(int frame, const float* world16, const float* view16, float* out16)
{
    if (!g_ntc) return false;
    if (frame != g_sqFrame) {
        g_sqFrame = frame;
        g_nsq = 0;
        memcpy(g_view.m, view16, 64);
        g_viewInv = RigidInverse(g_view);
        for (int k = 0; k < g_ntc; k++) {
            Mat4 f;
            if (!CarFrame(g_tc[k].veh, &f)) continue;
            Vec3 up = Norm(Row(f, 1)), axle = Norm(Row(f, 0));
            for (int i = 0; i < 4 && g_nsq < 64; i++) {
                const Tire& t = g_tc[k].t[i];
                WheelInfo w;
                if (!t.popped || t.flat < 0.01f || !ReadWheel(g_tc[k].veh, i, f, &w)) continue;
                g_sq[g_nsq++] = {w.centre, up, axle, t.r0, t.r0 * (1.0f - (1.0f - kRim) * t.flat)};
            }
        }
    }
    if (!g_nsq) return false;
    Mat4 w;
    memcpy(w.m, world16, 64);
    Mat4 m = Mul(w, g_viewInv);
    Vec3 o = Row(m, 3);
    for (int i = 0; i < g_nsq; i++) {
        const Sq& s = g_sq[i];
        Vec3 d = o - s.c;
        if (Dot(d, d) > 0.03f * 0.03f) continue;
        Vec3 ax = Norm(Row(m, 0));
        if (fabsf(Dot(ax, s.axle)) < 0.7f) continue;   // (not the wheel: something else drawn there)
        Vec3 u = Norm(s.up - ax * Dot(s.up, ax));
        float sy = (s.r0 + s.r) / (2.0f * s.r0), sx = 1.0f + 0.25f * (1.0f - s.r / s.r0);
        Vec3 piv = s.c + u * s.r0;
        // p' = piv + (p - piv) A, A = I + (sy-1) u u' + (sx-1) a a'
        Mat4 S = Identity();
        for (int r = 0; r < 3; r++)
            for (int col = 0; col < 3; col++) {
                float ur = r == 0 ? u.x : r == 1 ? u.y : u.z, uc = col == 0 ? u.x : col == 1 ? u.y : u.z;
                float ar = r == 0 ? ax.x : r == 1 ? ax.y : ax.z, ac = col == 0 ? ax.x : col == 1 ? ax.y : ax.z;
                S.m[r * 4 + col] += (sy - 1.0f) * ur * uc + (sx - 1.0f) * ar * ac;
            }
        Vec3 pa = XformDir(piv, S);
        SetRow(S, 3, piv - pa, 1.0f);
        Mat4 res = Mul(Mul(m, S), g_view);
        memcpy(out16, res.m, 64);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ debug
static uint32_t NearestCar(uint32_t player, float* dist, bool trafficOnly = false)
{
    float pp[3];
    uint32_t ptrs[30];
    float pos[30][3];
    if (!player || !SafeRead(player + 0x64, pp, 12)) return 0;
    int n = Game_ListVehiclePtrs(ptrs, pos, 30);
    uint32_t best = 0;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        float d = Len(V3(pos[i][0] - pp[0], pos[i][1] - pp[1], pos[i][2] - pp[2]));
        if (d < bd && (!trafficOnly || Rd<int>(ptrs[i] + 0xA4) == 2)) bd = d, best = ptrs[i];
    }
    if (dist) *dist = bd;
    return best;
}

void Tires_DebugCmd(const char* cmd, const char* arg, uint32_t player)
{
    if (!strcmp(cmd, "wheels")) {   // the nearest car's wheels
        float d;
        uint32_t v = NearestCar(player, &d);
        Mat4 f;
        if (!v || !CarFrame(v, &f)) {
            Log("wheels: no car");
            return;
        }
        char name[24] = "?";
        uint32_t np = Rd<uint32_t>(v + 248);
        if (np) SafeRead(np, name, 23), name[23] = 0;
        Vec3 p = Row(f, 3);
        uint32_t sim = Rd<uint32_t>(v + 1192);
        Vec3 vel = Rd<Vec3>(sim + 0x50), ang = Rd<Vec3>(sim + 0x5C);
        Log("wheels: car %08X '%s' type %d, %.1f m away at %.2f,%.2f,%.2f | grip %.2f hp %.2f | sim %08X vel %.2f %.2f %.2f ang %.2f %.2f %.2f",
            v, name, (int)Rd<uint32_t>(v + 0xA4), d, p.x, p.y, p.z, Rd<float>(v + 708), Rd<float>(v + 0x4A0), sim, vel.x, vel.y, vel.z,
            ang.x, ang.y, ang.z);
        for (int i = 0; i < 4; i++) {
            WheelInfo w;
            if (!ReadWheel(v, i, f, &w)) {
                Log("wheels:   %d: unreadable", i);
                continue;
            }
            Log("wheels:   %d: %08X (its car %08X) num %d steer %d drive %d radius %.3f yoff %+.3f spin %.2f touch %d angle %+.3f | rest "
                "%.2f,%.2f,%.2f | centre %.2f,%.2f,%.2f",
                i, w.w, Rd<uint32_t>(w.w), w.num, w.steer, w.drive, w.radius, w.yoff, w.spin, w.touch, w.angle, w.rest.x, w.rest.y,
                w.rest.z, w.centre.x, w.centre.y, w.centre.z);
        }
        return;
    }
    if (!strcmp(cmd, "tirepop")) {   // "tirepop <wheel>[,blast][,traffic]": the nearest car's (traffic car's)
        int i = atoi(arg);
        float d;
        uint32_t v = NearestCar(player, &d, strstr(arg, "traffic") != nullptr);
        if (v && g_tireLog > 0) Log("tires: tirepop on %08X, %.1f m away, type %d, locomotion %d", v, d, Rd<int>(v + 0xA4), Rd<int>(v + 388));
        Mat4 f;
        WheelInfo w;
        if (v && i >= 0 && i < 4 && CarFrame(v, &f) && ReadWheel(v, i, f, &w))
            Pop(v, i, w.centre + Norm(Row(f, 0)) * (w.rest.x > 0 ? w.radius * 0.4f : -w.radius * 0.4f),
                Norm(Row(f, 0)) * (w.rest.x > 0 ? 1.0f : -1.0f), strstr(arg, "blast") != nullptr);
        return;
    }
    if (!strcmp(cmd, "carspin") || !strcmp(cmd, "carpush")) {   // "carspin <yaw rate>" / "carpush <vx>,<vy>,<vz>": the nearest car's SimState
        uint32_t v = NearestCar(player, nullptr), sim = v ? Rd<uint32_t>(v + 1192) : 0;
        Vec3 x = {};
        if (!sim) return;
        Vec3 old = Rd<Vec3>(sim + (cmd[3] == 's' ? 0x5C : 0x50));
        if (cmd[3] == 's') {   // a change of yaw rate, as momentum
            x.y = (float)atof(arg);
            CarAddSpin(v, x);
            x = old + x;
        } else {
            sscanf(arg, "%f,%f,%f", &x.x, &x.y, &x.z);
            SafeWrite(sim + 0x50, &x, 12);
        }
        Log("tires: %s %08X sim %08X: was %.2f %.2f %.2f, now %.2f %.2f %.2f", cmd, v, sim, old.x, old.y, old.z, x.x, x.y, x.z);
        return;
    }
    if (!strcmp(cmd, "carhit")) {   // (lab) "carhit <speed>": the nearest car sent at the player - his knockdown, to test what he holds
        float pp[3], cp[3];
        uint32_t ptrs[30];
        float pos[30][3];
        uint32_t v = NearestCar(player, nullptr), sim = v ? Rd<uint32_t>(v + 1192) : 0;
        if (!sim || !SafeRead(player + 0x64, pp, 12)) return;
        int n = Game_ListVehiclePtrs(ptrs, pos, 30);
        bool found = false;
        for (int i = 0; i < n && !found; i++)
            if (ptrs[i] == v) cp[0] = pos[i][0], cp[1] = pos[i][1], cp[2] = pos[i][2], found = true;
        if (!found) return;
        Vec3 d = V3(pp[0] - cp[0], 0, pp[2] - cp[2]);
        float L = Len(d), sp = arg[0] ? (float)atof(arg) : 8.0f;
        Log("tires: carhit: the nearest car %08X at (%.2f %.2f %.2f), %.1f m from him", v, cp[0], cp[1], cp[2], L);
        if (L < 0.5f || sp <= 0.0f) return;   // ("carhit 0": where it is, nothing sent)
        Vec3 x = d * (sp / L);
        SafeWrite(sim + 0x50, &x, 12);
        Log("tires: carhit %08X from %.1f m at %.1f m/s", v, L, sp);
        return;
    }
    if (!strcmp(cmd, "physobj")) {   // the nearest car's SimState and physics object, their vtables
        uint32_t v = NearestCar(player, nullptr), sim = v ? Rd<uint32_t>(v + 1192) : 0;
        if (!sim) return;
        uint32_t svt = Rd<uint32_t>(sim);
        typedef void*(__attribute__((thiscall)) * GetObjFn)(void* self, int);
        uint32_t obj = (uint32_t)(uintptr_t)((GetObjFn)(uintptr_t)Rd<uint32_t>(svt + 56))((void*)(uintptr_t)sim, -1);
        uint32_t ovt = Rd<uint32_t>(obj);
        Log("tires: sim %08X vtable %08X (slot 14 %08X, 17 %08X) +0x70 %08X | object %08X (%s) vtable %08X slot 32 %08X", sim, svt,
            Rd<uint32_t>(svt + 56), Rd<uint32_t>(svt + 68), Rd<uint32_t>(sim + 0x70), obj, Game_ClassName(obj), ovt, Rd<uint32_t>(ovt + 128));
        return;
    }
    if (!strcmp(cmd, "tirefix")) {
        for (int k = 0; k < g_ntc; k++) Restore(g_tc[k], "debug");
        g_ntc = 0;
        return;
    }
    if (!strcmp(cmd, "tirelog")) g_tireLog = arg[0] ? atoi(arg) : 20;
}
