// FireWeapon: where a shot goes and what it hits, for any weapon. The weapon says where the
// shot starts (ShotOrigin) and what it is (WeaponSpec); every pellet is traced through the
// game's collision (TraceShot) and the thing it hit reacts (Hit_React, react.cpp). The weapon
// gets back each pellet's HitResult and bullet path for its own visuals.
#include "hit.h"

static int g_hitLog;   // debug "hitlog <shots>": every trace of the next shots
static const float kAcross = 8.0f;   // (M5) past a car's collision box, how far its drawn shape is looked for: right across it
static uint32_t g_rng = 0x2545F491u;

static float Rnd01()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

// `d` turned by a random angle up to `cone` (radians), evenly over the cone's area
static Vec3 Scatter(Vec3 d, float cone)
{
    if (cone <= 0.0f) return d;
    Vec3 a = Norm(Cross(fabsf(d.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), d));
    Vec3 b = Cross(d, a);
    float r = tanf(cone) * sqrtf(Rnd01()), t = Rnd01() * 6.2831853f;
    return Norm(d + a * (r * cosf(t)) + b * (r * sinf(t)));
}

// (vcover) a hit on something drawn that the game's collision doesn't have
static HitResult DrawnHit(Vec3 eye, Vec3 dir, float vis, Vec3 normal)
{
    HitResult h = {};
    h.kind = HIT_WORLD;
    h.pos = eye + dir * vis;
    h.normal = normal;
    h.dist = vis;
    h.ground = h.volume = -1;
    h.drawn = true;
    return h;
}

// One pellet, `aimDir` replacing the origin's crosshair ray (aimed) or barrel direction (hip). Aimed, `vis` is how far
// the drawn picture goes along the ray (vcover.cpp: -1 nothing drawn, -3 not known) and `visN` its slope; *vc says
// what that changed (VCover_Decide's 1 or 2, 0 nothing).
static HitResult TraceBullet(const WeaponSpec& w, const ShotOrigin& o, Vec3 aimDir, Vec3* from, Vec3* to, float vis, Vec3 visN,
                             int* vc)
{
    *vc = 0;
    // 1. Where the shot is meant to land. Aimed: what is under the crosshair, traced from
    //    level with him (anything between the camera and his back must not catch it).
    //    From the hip: straight along the barrel.
    HitResult aim;
    bool aimHit = false;
    Vec3 target;
    if (o.aimed) {
        float skip = fmaxf(0.0f, Dot(o.body - o.eye, aimDir) - 0.5f);
        Vec3 a = o.eye + aimDir * skip;
        Vec3 b = a + aimDir * w.range;
        aimHit = TraceShot(a, b, o.shooter, &aim, w.radius);
        target = aimHit ? aim.pos : b;
        // (m4) a car under the crosshair: its collision box is rough (it takes in the wheels), so the point is where
        // the crosshair's line meets the car as drawn, or one of its tires if the line meets that first (a tire behind
        // the body isn't what the crosshair is on). The bullet then goes from the muzzle to that point - from a gun well
        // off the camera's line (out of a car's window) the box's face isn't what the crosshair is on - and a clear
        // muzzle line falls back on it as the aimed hit, not on the box's face
        // (m5) The line is followed right across the car: in at a broken window it goes on to the far side; in at one
        // opening and out at another (nothing of the drawn car on it), what the crosshair is on is past the car.
        for (int k = 0; k < 3 && aimHit && aim.kind == HIT_VEHICLE && aim.object; k++) {
            float farT = Len(aim.pos - a) + kAcross;
            TireHit th;
            CarHit ch;
            bool tire = Tires_Raycast(a, aimDir, farT, &th) && th.veh == aim.object;
            bool body = CarGlass_Raycast(aim.object, a, aimDir, farT, &ch);
            if (tire && (!body || Dot(th.pos - a, aimDir) <= Dot(ch.pos - a, aimDir) + 0.02f)) aim.pos = th.pos, aim.normal = th.normal;
            else if (body) aim.pos = ch.pos, aim.normal = ch.normal;
            else if (CarGlass_Knows(aim.object)) {
                uint32_t car = aim.object;
                Vec3 in = aim.pos;
                Trace_SetIgnore3(car);
                aimHit = TraceShot(in, b, o.shooter, &aim, w.radius);
                Trace_SetIgnore3(0);
                if (g_hitLog > 0) Log("hit: the crosshair's line goes right through car %08X: past it, %s", car, aimHit ? "something" : "nothing");
                target = aimHit ? aim.pos : b;
                continue;
            }
            target = aim.pos + aimDir * 0.02f;
            break;
        }
        // (vcover) what is drawn along the crosshair's line: a building's piece with no collision stops it; a
        // person's cylinder where nothing of them is drawn lets it past (and on to whatever is behind)
        HitResult as;
        *vc = VCover_Decide(o.eye, aimDir, vis, skip, aim, o.shooter, &as);
        if (*vc == 2) {
            Trace_SetIgnore4(aim.object);   // (until the bullet's own path is done, below)
            Vec3 from2 = aim.pos;
            aimHit = TraceShot(from2, b, o.shooter, &aim, w.radius);
            target = aimHit ? aim.pos : b;
            int again = VCover_Decide(o.eye, aimDir, vis, skip, aim, o.shooter, &as);
            if (again == 1) *vc = 3;   // past them, into a drawn wall
            else if (again >= 4) aim = as, aimHit = true, target = aim.pos;   // past them, into someone drawn behind
        }
        if (*vc == 1 || *vc == 3) {
            aim = DrawnHit(o.eye, aimDir, vis, visN);
            aimHit = true;
            target = aim.pos;
        } else if (*vc >= 4) {   // a person / car the trace missed: hit them where they're drawn
            aim = as;
            aimHit = true;
            target = aim.pos + aimDir * 0.02f;
        }
    } else {
        target = o.muzzle + aimDir * w.range;
    }
    // 2. The bullet's own path: from inside him out to the muzzle (a gun poked through a
    //    wall hits that wall), then from the muzzle to the target, so a wall or car right
    //    in front of the gun stops it even when the crosshair is past it.
    HitResult hit;
    if (!TraceShot(o.body, o.muzzle, o.shooter, &hit, w.radius)) {
        Vec3 d = target - o.muzzle;
        float len = Len(d);
        d = len > 1e-4f ? d * (1.0f / len) : aimDir;
        if (aimHit) len -= 0.05f;   // stop just short of the aimed hit (it is the same surface)
        if (!(len > 0.05f && TraceShot(o.muzzle, o.muzzle + d * len, o.shooter, &hit, w.radius)) && aimHit) hit = aim;
    }
    Trace_SetIgnore4(0);
    *from = o.muzzle;
    *to = hit.kind != HIT_NONE ? hit.pos : target;
    hit.dist = Len(*to - o.muzzle);
    return hit;
}

bool Weapon_AimPoint(const WeaponSpec& w, const ShotOrigin& o, Vec3* target, HitResult* hit)
{
    float skip = fmaxf(0.0f, Dot(o.body - o.eye, o.eyeDir) - 0.5f);
    Vec3 a = o.eye + o.eyeDir * skip;
    Vec3 b = a + o.eyeDir * w.range;
    bool got = TraceShot(a, b, o.shooter, hit, w.radius);
    *target = got ? hit->pos : b;
    return got;
}

// (shotgun) pellets on one person or loose prop: they react once, after all the pellets are traced
struct PelletGroup {
    uint32_t obj;
    int count, first;
    Vec3 dir;
};
static bool Groupable(const HitResult& h)
{
    if (h.kind == HIT_CHARACTER) return h.object != 0;
    return h.kind == HIT_WORLD && h.object && h.ground < 0 && !h.drawn && React_IsMovable(h.object);
}

int FireWeapon(const WeaponSpec& w, const ShotOrigin& o, ShotResult* out, int max)
{
    PelletGroup groups[kMaxPellets];
    int ngroups = 0;
    int n = w.pellets < 1 ? 1 : w.pellets;
    if (n > max) n = max;
    // (vcover) aimed: the pellets go along the rays whose drawn depth was read on the frame just shown
    bool vc = o.aimed && VCover_Ready(n, w.spread);
    int vcStops = 0, vcPast = 0;
    for (int i = 0; i < n; i++) {
        double t0 = Time_Real();
        if (g_hitLog > 0) g_traceLog += 3;
        ShotResult& r = out[i];
        ShotOrigin oi = o;
        Vec3 aimDir, visN = {};
        float vis = -3.0f;
        if (vc) VCover_Pellet(i, &oi.eye, &aimDir, &vis, &visN);
        else aimDir = Scatter(o.aimed ? o.eyeDir : o.dir, w.spread);
        int vcd = 0;
        r.hit = TraceBullet(w, oi, aimDir, &r.from, &r.to, vis, visN, &vcd);
        vcStops += vcd == 1 || vcd == 3 || vcd >= 4;
        vcPast += vcd >= 2;
        if (VCover_LogShots() > 0 && o.aimed)
            Log("vcover: pellet %d/%d drawn %.2f m -> %s, hit %s at %.2f m from the muzzle", i + 1, n, vis,
                vcd == 1 ? "stopped on the drawn surface" : vcd == 2 ? "past a person's cylinder" : vcd == 3 ? "past a person, into a drawn surface" :
                vcd == 4 ? "a person the trace missed" : vcd == 5 ? "a car the trace missed" : "trace kept",
                Hit_KindName(r.hit.kind), Len(r.to - r.from));
        Vec3 path = r.to - r.from;
        float len = Len(path);
        Vec3 rd = len > 1e-4f ? path * (1.0f / len) : aimDir;
        if (Dot(rd, aimDir) < 0.0f) rd = aimDir;   // hit between his chest and the muzzle: the shot still goes forward
        // a car: where on it, as drawn, and whether it's a window (its collision box is rougher)
        r.hit.glass = false;
        CarHit ch;
        bool onCar = false;
        for (int k = 0; k < 3 && r.hit.kind == HIT_VEHICLE && r.hit.object; k++) {
            onCar = CarGlass_Raycast(r.hit.object, r.from, rd, len + kAcross, &ch);
            if (onCar || !CarGlass_Knows(r.hit.object)) break;
            // (m5) nothing of the drawn car on the shot's line: in at one opening (a broken window, an open top), out at
            // another - it goes on past the car
            uint32_t car = r.hit.object;
            HitResult past;
            Trace_SetIgnore3(car);
            bool got = TraceShot(r.to, r.from + rd * w.range, o.shooter, &past, w.radius);
            Trace_SetIgnore3(0);
            if (got) r.hit = past, r.to = past.pos;
            else r.hit.kind = HIT_NONE, r.hit.object = 0, r.to = r.from + rd * w.range;
            len = Len(r.to - r.from);
            if (g_hitLog > 0) Log("hit: the shot goes right through car %08X: on %.2f m", car, len);
        }
        if (onCar) {
            r.hit.pos = r.to = ch.pos;
            r.hit.normal = ch.normal;
            r.hit.glass = ch.glass;
        }
        // a tire in the way first (tires.cpp): before the body as drawn, the ground under the car, or
        // behind the face of the car's rough collision box
        float reach = r.hit.kind == HIT_NONE ? len : Len(r.to - r.from) + (r.hit.kind == HIT_VEHICLE && !onCar ? 1.0f : 0.02f);
        TireHit th;
        r.hit.tire = false;
        if (Tires_Raycast(r.from, rd, reach, &th)) {
            r.hit.kind = HIT_VEHICLE;
            r.hit.object = th.veh;
            r.hit.pos = r.to = th.pos;
            r.hit.normal = th.normal;
            r.hit.glass = false;
            r.hit.tire = true;
            r.hit.ground = r.hit.volume = -1;
            r.hit.aiRef = 1;
            Tires_Shot(th.veh, th.wheel, th.pos, th.normal);
        }
        if (w.group && Groupable(r.hit)) {   // (reacts below, once for all the pellets on it)
            int g = 0;
            while (g < ngroups && groups[g].obj != r.hit.object) g++;
            if (g == ngroups) groups[ngroups++] = {r.hit.object, 0, i, V3(0, 0, 0)};
            groups[g].count++;
            groups[g].dir = groups[g].dir + rd;
            r.gameFx = false;
        } else
            r.gameFx = r.hit.kind != HIT_NONE && Hit_React(w, r.hit, rd);
        if (r.hit.kind == HIT_NONE) Sky_Shot(aimDir);   // into the sky: the Duff plane?
        if (w.holeSize > 0 && Hit_TakesHole(r.hit)) Decal_Add(r.hit.pos, r.hit.normal, w.holeSize);
        else if (w.holeSize > 0 && r.hit.kind == HIT_VEHICLE && r.hit.object && !r.hit.tire) {   // a car's: rides along
            if (r.hit.glass)
                CarGlass_GlassHit(r.hit.object, r.hit.pos, r.hit.normal, rd);
            else
                Decal_AddOnVehicle(r.hit.object, r.hit.pos, r.hit.normal, w.holeSize * 0.7f, DECAL_HOLE, onCar);
        }
        if (g_hitLog > 0) {
            if (i == n - 1) g_hitLog--;
            g_traceLog = 0;
            char line[200];
            Trace_Describe(r.hit, line, sizeof(line));
            Log("hit (%s %d/%d, %s): %s | %.2f ms", w.name, i + 1, n, o.aimed ? "aimed" : "hip", line,
                (Time_Real() - t0) * 1000.0);
        }
    }
    // the people and props several pellets hit: one reaction each, as hard as the share of the blast that hit them
    // (one pellet a little harder than the pistol's bullet, four or more the full blast)
    for (int g = 0; g < ngroups; g++) {
        WeaponSpec gw = w;
        float gain = fminf(1.0f, 0.40f + 0.15f * (float)groups[g].count);
        gw.characterForce *= gain;
        gw.propForce *= gain;
        ShotResult& r = out[groups[g].first];
        r.gameFx = Hit_React(gw, r.hit, Norm(groups[g].dir));
        if (g_hitLog > 0 || VCover_LogShots() > 0)
            Log("hit: %d pellets on %s %08X: one reaction at %.0f%% (%.0f N)", groups[g].count, Hit_KindName(r.hit.kind), groups[g].obj,
                gain * 100.0f, r.hit.kind == HIT_CHARACTER ? gw.characterForce : gw.propForce);
    }
    if (vc) VCover_Used();
    if (VCover_LogShots() > 0 && o.aimed) {
        Log("vcover: shot of %d pellets, %s, %d stopped on drawn surfaces, %d went past a person's cylinder", n,
            vc ? "pattern read" : "no reading (traces only)", vcStops, vcPast);
        VCover_LogUsed();
    }
    return n;
}

// debug commands (after the gun's own)
extern int g_decalLog;

void Weapon_DebugCmd(const char* cmd, const char* arg, uint32_t player)
{
    VCover_DebugCmd(cmd, arg);
    React_DebugCmd(cmd, arg, player);
    CarGlass_DebugCmd(cmd, arg);
    Tires_DebugCmd(cmd, arg, player);
    Sky_DebugCmd(cmd, arg);
    Rocket_DebugCmd(cmd, arg);
    Explode_DebugCmd(cmd, arg, player);
    if (!strcmp(cmd, "decallog")) g_decalLog = arg[0] ? atoi(arg) : 1;
    if (!strcmp(cmd, "hitlog")) g_hitLog = arg[0] ? atoi(arg) : 1;
    else if (!strcmp(cmd, "tracelog")) g_traceLog = arg[0] ? atoi(arg) : 1;
    else if (!strcmp(cmd, "trace")) {
        // "trace [metres]": from the camera the frame was drawn with, straight out of the
        // middle of the screen, leaving the player out
        float view[16], proj[16];
        if (!Game_GetCamera(view, proj)) {
            Log("trace: no camera");
            return;
        }
        Mat4 v;
        memcpy(v.m, view, 64);
        Mat4 cw = RigidInverse(v);
        Vec3 eye = Row(cw, 3), fwd = Norm(Row(cw, 2));
        float range = arg[0] ? (float)atof(arg) : 100.0f;
        HitResult h;
        int keep = g_traceLog;
        g_traceLog = 1;
        TraceShot(eye, eye + fwd * range, player, &h);
        g_traceLog = keep;
    }
}
