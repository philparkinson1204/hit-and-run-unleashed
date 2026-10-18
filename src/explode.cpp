// Explosions: the rocket launcher's rocket going off. The look and the rules are the game's own
// (English Simpsons.exe, checked in the disassembly; docs/HOW_IT_WORKS.md "Explosions"):
//  - The look: the game's car explosion, breakable effect 24 (BreakablesManager::Play 0x4B87D0,
//    what Vehicle's explosion 0x4EE280 plays), where the rocket went off; one of the game's three
//    car explosion sounds; the follow camera's crash shake (CAMERA_SHAKE, the struct a car crash
//    sends); a jolt for the aiming camera and the pad; a cartoon flash and fireball puffs of the
//    mod's own, like the handgun's muzzle flash; a scorch mark on walls and roads.
//  - What it hit: a car is wrecked through the game's own path (TriggerDamage with no second
//    chance, as the kill volumes do; the car's own update then explodes it, swaps in the husk,
//    spills coins and raises Hit & Run); a person, prop or wasp camera takes the full blast; the
//    ground or a wall throws its own particles and sparks.
//  - Around it: everything moving within the blast radius that the blast can reach - the game's
//    collision between them is tested, so a wall or the ground in the way shields it (cars, people
//    and props don't) - reacts through the same reactions a bullet uses, scaled down with distance:
//    cars take the game's damage at the side facing the blast and a shove as hard as the game's own
//    car explosion (20000 N at the centre), people are knocked flying (or cower outside the game's
//    physics), props are pushed or broken, wasp cameras explode.
//  - Rules kept: cars a mission protects ("can't be damaged") are left alone, because
//    Vehicle::ApplyForce doesn't check that flag (0x4EE3C0); the car he is driving is never wrecked
//    outright (it gets the game's clamped damage); the player himself isn't hurt.
#include "hit.h"

static const uint32_t A_BreakablesManager = 0x6C879C;
static const uint32_t V_Vehicle = 0x60832C, V_NPCharacter = 0x607FF4, V_ActorDSG = 0x615C1C;
static const uint32_t V_InstDynaPhysDSG = 0x60CC54, V_InstAnimDynaPhysDSG = 0x60CD0C, V_DynaPhysDSG = 0x60CE9C,
                      V_StatePropDSG = 0x60C97C;
enum { BRK_CAR_EXPLOSION = 24, EV_CAMERA_SHAKE = 0x12C };
static const float kWreckRadius = 2.0f;   // cars this close to the blast (beyond their body) are wrecked

typedef void(__attribute__((thiscall)) * BmPlayFn)(void* bm, int type, const Mat4* m);
typedef void(__attribute__((thiscall)) * GetPositionFn)(void* self, Vec3* out);

static BlastFx g_fx[64];
static int g_nfx;
static float g_shake;          // the strongest camera jolt waiting for gun.cpp
static uint32_t g_fxPlayer;
static uint32_t g_rng = 0x1B873593u;
static int g_boomLog;          // debug "boomlog <explosions>"
static bool g_noNative;        // debug "boomnative 0": leave out the game's breakable effect

static float Rnd()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

static bool English() { return Game_IsEnglish(); }

// The game's car explosion effect at `p`, upright. Its pool holds the level's loaded copies (the
// manager's +0x10: 12 bytes per breakable type, the count at +8); none loaded = nothing plays.
static int BreakablePool(int type)
{
    const uint8_t* bm = *(const uint8_t* const*)A_BreakablesManager;
    const uint8_t* arr = bm ? *(const uint8_t* const*)(bm + 0x10) : nullptr;
    return arr ? *(const int*)(arr + type * 12 + 8) : -1;
}

static bool PlayCarExplosion(Vec3 p)
{
    void* bm = *(void**)A_BreakablesManager;
    if (!bm || BreakablePool(BRK_CAR_EXPLOSION) <= 0) return false;
    Mat4 m = Identity();
    SetRow(m, 3, p, 1);
    ((BmPlayFn)0x4B87D0)(bm, BRK_CAR_EXPLOSION, &m);
    return true;
}

static void AddFx(int kind, Vec3 p, Vec3 v, float size, float life)
{
    BlastFx& f = g_nfx < 64 ? g_fx[g_nfx++] : g_fx[(int)(Rnd() * 63.0f)];
    f = {p, v, 0.0f, life, size, Rnd() * 6.2831853f, kind};
}

// Is the object at `target` within the blast's reach from `c`? The game's collision between them:
// buildings, walls, animated pieces and the ground shield it; cars, people and props don't.
static HitResult g_lastBlock;   // what shielded the last object that the blast didn't reach (debug)
static bool Reaches(Vec3 c, Vec3 target, uint32_t shooter, uint32_t obj)
{
    HitResult& b = g_lastBlock;
    if (!TraceShot(c, target, shooter, &b, 0.05f)) return true;
    if (b.object == obj && b.ground < 0) return true;
    if (b.ground < 0 && (b.kind == HIT_VEHICLE || b.kind == HIT_CHARACTER || b.aiRef == 0x20000 || b.aiRef == 0x40002 ||
                         b.aiRef == 0x40003))
        return true;
    // the wall is behind it, or it's the ground right at its feet (a prop's base, a slope)
    return b.dist >= Len(target - c) - (b.ground >= 0 ? 1.0f : 0.3f);
}

// a push away from the blast, tipped up
static Vec3 AwayUp(Vec3 c, Vec3 p, float up)
{
    Vec3 d = p - c;
    d.y = 0;
    float l = Len(d);
    d = l > 0.05f ? d * (1.0f / l) : V3(1, 0, 0);
    return Norm(d + V3(0, up, 0));
}

// A scorch mark is a flat square: on a fence, a post or next to a building's corner the part past
// the edge hung in the air. It fits if there is a surface right behind each of its corners (a
// little inside them: the soot fades out at the edge).
static bool ScorchFits(Vec3 at, Vec3 n, float size, uint32_t shooter)
{
    Vec3 a = Norm(Cross(fabsf(n.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), n)), b = Cross(n, a);
    float r = 0.36f * size;
    for (int k = 0; k < 4; k++) {
        Vec3 q = at + a * (k & 1 ? r : -r) + b * (k & 2 ? r : -r);
        HitResult t;
        if (!TraceShot(q + n * 0.3f, q - n * 0.3f, shooter, &t, 0.0f)) return false;
    }
    return true;
}

// (v1.0) how far the blast's centre is above ground right under it - the terrain, or a flat top to stand on (a roof, a
// car's): 1e9 if there's none within 2.5 m
static float AboveGround(Vec3 c, uint32_t shooter)
{
    float best = 1e9f, gy;
    if (Trace_GroundY(c + V3(0, 0.1f, 0), &gy) && gy <= c.y + 0.1f) best = fmaxf(0.0f, c.y - gy);
    HitResult d;
    if (TraceShot(c, c - V3(0, 2.5f, 0), shooter, &d, 0.0f) && d.normal.y > 0.7f) best = fminf(best, c.y - d.pos.y);
    return best;
}
static float kRingAbove = 1.8f;   // (v1.0) the game's blast, ring and all, only with ground this close under it (a person's or a car's height: theirs is on the ground)

void Explode(const WeaponSpec& w, const HitResult& h, Vec3 at, Vec3 dir, uint32_t shooter)
{
    double t0 = Time_Real();
    Vec3 n = h.kind != HIT_NONE ? h.normal : dir * -1.0f;
    Vec3 c = at + n * 0.3f;   // the blast's centre, just off the surface
    bool english = English();

    // ---- the look and the sound
    // (v1.0) the game's car explosion brings a shockwave ring that lies flat: only with ground right under the blast, and
    // set on it (high on a wall, or bursting in the air, the ring hung there in mid-air - the play-test); higher up the
    // mod's own fireball and smoke stand in, as they do without the game's effect
    float above = AboveGround(c, shooter);
    bool native = english && !g_noNative && above <= kRingAbove && PlayCarExplosion(c - V3(0, above, 0));
    // the mod's cartoon part on top, like the handgun's muzzle flash: a star flash. The game's
    // effect brings the fireball, debris and smoke; without it (none loaded) the mod's own fireball
    // puffs and smoke stand in
    AddFx(0, c, V3(0, 0, 0), 3.2f, 0.14f);
    for (int i = 0; i < (native ? 0 : 9); i++) {
        Vec3 r = Norm(V3(Rnd() - 0.5f, Rnd() * 0.8f + 0.1f, Rnd() - 0.5f) + n * 0.4f);
        AddFx(1, c + r * 0.4f, r * (2.5f + Rnd() * 2.5f) + V3(0, 1.0f, 0), 1.1f + Rnd() * 0.5f, 0.45f + Rnd() * 0.2f);
    }
    for (int i = 0; i < (native ? 0 : 7); i++) {
        Vec3 r = Norm(V3(Rnd() - 0.5f, Rnd() * 0.5f + 0.2f, Rnd() - 0.5f) + n * 0.3f);
        AddFx(2, c + r * 0.6f, r * (1.0f + Rnd() * 1.5f) + V3(0, 1.4f, 0), 1.4f + Rnd() * 0.8f, 1.6f + Rnd() * 1.0f);
    }
    if (!native && english) {   // (v1.0: high up, the game's own black smoke over the mod's fireball - its car explosion's, ringless)
        Fx_Particle(19, c, n, 1.0f);
        Fx_Particle(14, c + n * 0.3f, n, 1.0f);
    }
    float pp[3] = {0, 0, 0};
    bool havePlayer = Game_PlayerPosition(shooter, pp);
    Vec3 me = V3(pp[0], pp[1] + 1.0f, pp[2]);
    float dMe = havePlayer ? Len(me - c) : 50.0f;
    Sound_Play(SND_BOOM1 + (int)(Rnd() * 2.99f), Clamp01f(1.25f - dMe / 110.0f) * 0.95f + 0.05f, 0.95f + 0.1f * Rnd());
    float prox = Clamp01f(1.0f - dMe / 40.0f);
    if (prox > 0.0f) {
        g_shake = fmaxf(g_shake, prox * prox * 1.2f + 0.15f);
        if (english) {   // the follow camera's own crash shake (what a big crash sends)
            struct {
                int player;
                Vec3 dir;
                float magnitude;
                uint8_t flag, pad[3];
            } ev = {0, Norm(me - c), 0.25f + 0.9f * prox, 0, {0, 0, 0}};
            React_TriggerEvent(EV_CAMERA_SHAKE, &ev);
        }
    }
    if (!english) return;

    // ---- what it hit
    uint32_t direct = h.kind != HIT_NONE && h.ground < 0 ? h.object : 0;
    if (h.kind == HIT_WORLD) {
        // the ground or a wall throws its own particles and sparks, full size (a prop or a wasp
        // camera hit gets its push or break below, with everything else in the blast)
        HitResult hw = h;
        uint32_t vt = direct ? *(const uint32_t*)(uintptr_t)direct : 0;
        bool movable = vt == V_InstDynaPhysDSG || vt == V_InstAnimDynaPhysDSG || vt == V_DynaPhysDSG ||
                       vt == V_StatePropDSG || vt == V_ActorDSG;
        if (movable) hw.object = 0;   // its particles, not its push (that comes with the blast)
        WeaponSpec ws = w;
        ws.propForce = 0;
        ws.breaks = false;
        ws.heat = false;
        Hit_React(ws, hw, dir);
        Fx_Sparks(at, n, 1.0f);
        if (Hit_TakesHole(h)) {   // a scorch mark, as big as the surface takes (see ScorchFits)
            float size = ScorchFits(at, n, 2.2f, shooter) ? 2.2f : ScorchFits(at, n, 1.2f, shooter) ? 1.2f : 0.0f;
            if (size > 0) Decal_Add(at, n, size, DECAL_SCORCH);
        }
    } else if (h.kind == HIT_VEHICLE) {
        Fx_Sparks(at, n, 1.0f);
    }

    // ---- everything moving in the blast
    float pre = React_HeatMeter();
    int cars = 0, wrecked = 0, people = 0, props = 0, shielded = 0;
    uint32_t objs[50];
    int no = Trace_ListDynamics(c, w.blast + 3.0f, shooter, objs, 49);   // (room for the one it hit)
    bool listed = false;
    for (int i = 0; i < no; i++) listed |= objs[i] == direct;
    if (direct && !listed && no < 50) objs[no++] = direct;
    for (int i = 0; i < no; i++) {
        uint32_t o = objs[i];
        uint8_t* ob = (uint8_t*)(uintptr_t)o;
        uint32_t vt = *(const uint32_t*)ob;
        Vec3 p;
        float body;   // how far its own bulk reaches towards the blast
        if (vt == V_Vehicle) {
            p = *(const Vec3*)(ob + 0xE8);   // its position (Vehicle's slot 8 returns this + 0xE8)
            body = 1.2f;
        } else if (vt == V_NPCharacter) {
            p = *(const Vec3*)(ob + 0x64) + V3(0, 0.7f, 0);
            body = 0.3f;
        } else if (vt == V_InstDynaPhysDSG || vt == V_InstAnimDynaPhysDSG || vt == V_DynaPhysDSG || vt == V_StatePropDSG ||
                   vt == V_ActorDSG) {
            ((GetPositionFn)(*(void***)ob)[16])(ob, &p);
            if (vt != V_ActorDSG) p = p + V3(0, 0.5f, 0);   // a prop's position is its base; a wasp's its middle
            body = 0.4f;
        } else {
            continue;   // the player, buildings
        }
        float d = fmaxf(0.0f, Len(p - c) - body);
        float f = o == direct ? 1.0f : powf(Clamp01f(1.0f - d / w.blast), 0.8f);
        if (f <= 0.0f) continue;
        if (o != direct && !Reaches(c, p, shooter, o)) {
            shielded++;
            if (g_boomLog > 0) {
                char line[200];
                Trace_Describe(g_lastBlock, line, sizeof(line));
                Log("boom:   %s %08X %.1f m: shielded by %s", Game_ClassName(o), o, d, line);
            }
            continue;
        }
        if (g_boomLog > 0) Log("boom:   %s %08X %.1f m, strength %.2f%s", Game_ClassName(o), o, d, f, o == direct ? " (hit)" : "");
        Vec3 push = AwayUp(c, p, 0.0f);
        if (vt == V_Vehicle) {
            if (ob[0x494] || !ob[0x4A4]) continue;   // already a wreck / a mission protects it
            bool his = *(const int*)(ob + 0xA4) == 0;   // the player's own car: the game's clamped damage only
            bool wreck = !his && (o == direct || d < kWreckRadius);
            Vec3 side = p + Norm(c - p) * 1.0f;          // the side facing the blast (which panel)
            float hp0 = *(const float*)(ob + 0x4A0);
            React_VehicleDamage(ob, side, *(const float*)(ob + 0x2EC) * w.vehicleDamage * 0.85f * f, wreck);
            React_VehicleShove(ob, AwayUp(c, p, 0.35f), w.vehicleForce * f, w.heat);
            if (o == direct || d < 5.0f) CarGlass_Shatter(o, c);   // the windows go
            if (!wreck && !ob[0x494]) Tires_Blast(o, c, 3.5f);       // and the tires next to it
            if (g_boomLog > 0)
                Log("boom:     hit points %.2f -> %.2f of %.2f%s, shove %.0f N", hp0, *(const float*)(ob + 0x4A0),
                    *(const float*)(ob + 0x2EC), wreck ? " (wrecked)" : "", w.vehicleForce * f);
            cars++;
            wrecked += ob[0x494] != 0;
        } else if (vt == V_NPCharacter) {
            HitResult hc = {};
            hc.kind = HIT_CHARACTER;
            hc.object = o;
            hc.pos = p;
            hc.normal = push * -1.0f;
            hc.ground = hc.volume = -1;
            WeaponSpec ws = w;
            ws.characterForce = w.characterForce * (0.35f + 0.65f * f);   // everyone in it goes down
            Hit_React(ws, hc, push);
            people++;
        } else {
            WeaponSpec ws = w;
            ws.propForce = w.propForce * f;
            ws.breaks = w.breaks && (o == direct || f > 0.3f);
            React_PropHit(ws, o, push);
            props++;
        }
    }
    // and everyone walking about within earshot runs from it
    if (w.heat) React_PanicAround(c, kBlastPanic);
    if (g_boomLog > 0) {
        g_boomLog--;
        char line[200];
        Trace_Describe(h, line, sizeof(line));
        Log("boom: at (%.1f %.1f %.1f) on %s | game effect %s (pool %d; ground %.2f m under it) | %d moving things near: %d cars "
            "(%d wrecked), %d people, %d props, %d shielded | Hit & Run %.1f -> %.1f | %.2f m from him, shake %.2f | %.2f ms",
            c.x, c.y, c.z, line, native ? "played" : "no", BreakablePool(BRK_CAR_EXPLOSION), above < 1e8f ? above : -1.0f, no, cars, wrecked, people,
            props, shielded, pre, React_HeatMeter(), dMe, g_shake, (Time_Real() - t0) * 1000.0);
    }
}

void Explode_Update(float dt, uint32_t player, bool run, bool freeze)
{
    if (player != g_fxPlayer) {
        g_nfx = 0;
        g_fxPlayer = player;
    }
    if (!run) {
        if (!freeze) g_nfx = 0;
        return;
    }
    for (int i = 0; i < g_nfx;) {
        BlastFx& f = g_fx[i];
        f.t += dt;
        f.p = f.p + f.v * dt;
        f.v = f.v * (1.0f - fminf(1.0f, dt * (f.kind == 2 ? 1.2f : 3.0f))) + V3(0, (f.kind == 2 ? 0.8f : 1.5f) * dt, 0);
        if (f.t > f.life) g_fx[i] = g_fx[--g_nfx]; else i++;
    }
}

bool Explode_Any() { return g_nfx > 0; }
void Explode_Clear() { g_nfx = 0; }

int Explode_Fx(const BlastFx** out)
{
    *out = g_fx;
    return g_nfx;
}

float Explode_TakeShake()
{
    float s = g_shake;
    g_shake = 0;
    return s;
}

void Explode_DebugCmd(const char* cmd, const char* arg, uint32_t player)
{
    if (!strcmp(cmd, "boomlog")) g_boomLog = arg[0] ? atoi(arg) : 1;
    else if (!strcmp(cmd, "heat")) Log("heat: Hit & Run meter %.1f", React_HeatMeter());
    else if (!strcmp(cmd, "shieldtest")) {
        // "shieldtest": find a spot he can see where a building stands between the blast and
        // something moving within its radius, and set off an explosion there (with boomlog): that
        // thing must come out "shielded", the rest reached
        float pp[3];
        if (!Game_PlayerPosition(player, pp)) return;
        Vec3 eye = V3(pp[0], pp[1] + 1.5f, pp[2]);
        uint32_t objs[50];
        int no = Trace_ListDynamics(eye, 30.0f, player, objs, 50);
        for (int i = 0; i < no; i++) {
            uint8_t* ob = (uint8_t*)(uintptr_t)objs[i];
            uint32_t vt = *(const uint32_t*)ob;
            Vec3 p;
            if (vt == V_Vehicle) p = *(const Vec3*)(ob + 0xE8);
            else if (vt == V_NPCharacter) p = *(const Vec3*)(ob + 0x64) + V3(0, 0.7f, 0);
            else if (vt == V_InstDynaPhysDSG || vt == V_InstAnimDynaPhysDSG || vt == V_StatePropDSG) {
                ((GetPositionFn)(*(void***)ob)[16])(ob, &p);
                p = p + V3(0, 0.5f, 0);
            } else continue;
            for (int k = 0; k < 24; k++)
                for (float r = 3.0f; r <= 5.5f; r += 1.25f) {
                    float a = k * 6.2831853f / 24.0f;
                    Vec3 c = p + V3(sinf(a) * r, 0.6f, cosf(a) * r);
                    HitResult seen;
                    if (TraceShot(eye, c, player, &seen, 0.05f)) continue;   // he can't see it: maybe inside a wall
                    if (Reaches(c, p, player, objs[i]) || g_lastBlock.ground >= 0 || g_lastBlock.aiRef != 0x10000) continue;
                    char line[200];
                    Trace_Describe(g_lastBlock, line, sizeof(line));
                    Log("shieldtest: blast at (%.1f %.1f %.1f), %s %08X %.1f m away behind %s", c.x, c.y, c.z,
                        Game_ClassName(objs[i]), objs[i], Len(p - c), line);
                    HitResult h = {};
                    h.kind = HIT_NONE;
                    h.ground = h.volume = -1;
                    h.pos = c;
                    h.normal = V3(0, 1, 0);
                    g_boomLog = 1;
                    Explode(*Gun_RocketSpec(), h, c, V3(0, -1, 0), player);
                    return;
                }
        }
        Log("shieldtest: no spot found (%d moving things within 30 m)", no);
    }
    else if (!strcmp(cmd, "boomat")) {   // "boomat <x>,<y>,<z>": an explosion in the air at that point
        HitResult h = {};
        h.kind = HIT_NONE;
        h.ground = h.volume = -1;
        Vec3 p;
        if (sscanf(arg, "%f,%f,%f", &p.x, &p.y, &p.z) != 3) return;
        h.pos = p;
        h.normal = V3(0, 1, 0);
        Explode(*Gun_RocketSpec(), h, p, V3(0, -1, 0), player);
    }
    else if (!strcmp(cmd, "boomnative")) g_noNative = atoi(arg) == 0;
    else if (!strcmp(cmd, "boom")) {
        // "boom": an explosion where the crosshair points (a rocket that arrived there)
        float view[16], proj[16];
        if (!Game_GetCamera(view, proj)) return;
        Mat4 v;
        memcpy(v.m, view, 64);
        Mat4 cw = RigidInverse(v);
        Vec3 eye = Row(cw, 3), fwd = Norm(Row(cw, 2));
        HitResult h;
        if (!TraceShot(eye, eye + fwd * 150.0f, player, &h)) {
            Log("boom: nothing under the crosshair");
            return;
        }
        Explode(*Gun_RocketSpec(), h, h.pos, fwd, player);
    }
}
