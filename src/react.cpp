// Reactions: what the thing a shot hit does about it, using the game's own effects and rules.
// The same rules for every weapon; how much (damage, force, effect sizes, Hit & Run or not)
// comes from its WeaponSpec. The world (the game's particles, sparks), vehicles (the game's own
// damage), pedestrians (the kick's reactions) and the Hit & Run meter.
//
// English Simpsons.exe; checked in the disassembly (docs/research/shots_vehicles_effects.md
// section C) against the game's own callers: buildings and props spray their particles when
// a car hits them, and cars throw sparks when they scrape.
#include "hit.h"

// ------------------------------------------------------------------ the game's effects
// ParticleManager (created with the level): +0x04 the number of particle types, +0x08 one pool
// of 12 bytes per type. A type whose effect the level didn't load does nothing.
static const uint32_t A_ParticleManager = 0x6C87A0;
// SparkleManager: the streaks cars throw when they scrape
static const uint32_t A_SparkleManager = 0x6C844C;

struct ParticleAttr {
    int type;
    float bias;   // emission bias, 1 = normal
    Vec3 vel;     // unused here
};

// ParticleManager::Add (0x4B6520): the matrix in EAX, then the manager and the attributes on the
// stack (it pops both). The effect plays once at the matrix and frees itself.
__attribute__((naked)) static void PM_Add(void* pm, const ParticleAttr* attr, const Mat4* m)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 28(%esp), %eax\n\t"   // matrix
        "pushl 24(%esp)\n\t"        // attr
        "pushl 24(%esp)\n\t"        // pm
        "movl $0x4B6520, %ecx\n\t"
        "call *%ecx\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// SparkleManager::AddSparks (0x506FD0): the manager in EAX; position, direction and strength on
// the stack (it pops them). About (10..20) x strength + 1 streaks fly off along dir.
__attribute__((naked)) static void SM_AddSparks(void* sm, const Vec3* pos, const Vec3* dir, float strength)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 20(%esp), %eax\n\t"   // sm
        "pushl 32(%esp)\n\t"        // strength
        "pushl 32(%esp)\n\t"        // dir
        "pushl 32(%esp)\n\t"        // pos
        "movl $0x506FD0, %ecx\n\t"
        "call *%ecx\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// Vehicle damage at a contact point (0x4ED570, what the game's car collision handler calls):
// the car in ECX, a sim::Collision* in EAX, the amount on the stack (it pops it). It calls
// TriggerDamage (0x4EDF90, clamped: the car is held at "near death" once before it can be
// destroyed), then damages the panel on the side that was hit, by the car's damage type
// (1 the player's car: flapping hood/trunk/doors; 2 AI cars: damage textures; 3 traffic: smoke).
// The side comes from the collision's contact point (0x4EDD90): +0x08 / +0x0C the two
// CollisionObjects; whichever has the car's SimState (+0x18) gives the point at +0x38 / +0x2C.
__attribute__((naked)) static void Veh_DamageAt(void* vehicle, void* collision, float amount)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 20(%esp), %ecx\n\t"   // vehicle
        "movl 24(%esp), %eax\n\t"   // collision
        "pushl 28(%esp)\n\t"        // amount
        "movl $0x4ED570, %edx\n\t"
        "call *%edx\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// Vehicle::TriggerDamage (0x4EDF90): the car in EAX, amount and clamp on the stack (it pops
// them), the car's health 0..1 back in ST0. clamp = 0: hit points simply go down, and at 0 the car
// is marked destroyed (+0x494); its own update then explodes it (0x4EE280), swaps in the husk and
// fires VEHICLE_DESTROYED(_BY_USER). The kill volumes call it like that (0x41E9A0).
__attribute__((naked)) static float Veh_TriggerDamage(void* vehicle, float amount, int clamp)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 20(%esp), %eax\n\t"   // vehicle
        "pushl 28(%esp)\n\t"        // clamp
        "pushl 28(%esp)\n\t"        // amount
        "movl $0x4EDF90, %ecx\n\t"
        "call *%ecx\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// EventManager::TriggerEvent (0x432AD0): the manager in EDX; the event and its parameter on the
// stack (it pops them). Every listener gets it, as when the game fires it.
__attribute__((naked)) static unsigned TriggerEventRaw(void* em, int event, void* param)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 20(%esp), %edx\n\t"   // em
        "pushl 28(%esp)\n\t"        // param
        "pushl 28(%esp)\n\t"        // event
        "movl $0x432AD0, %eax\n\t"
        "call *%eax\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// the events the reactions fire (the game's numbers)
enum { EV_PEDESTRIAN_SMACKDOWN = 0xCD, EV_KICK_NPC_SOUND = 0xE1, EV_OBJECT_KICKED = 0x12A };

// (English Simpsons.exe only: each way into the game's code checks it, not just the callers)
static bool English() { return Game_IsEnglish(); }

static void TriggerEvent(int event, void* param)
{
    if (!English()) return;
    void* em = *(void**)0x6C9018;   // EventManager
    if (em) TriggerEventRaw(em, event, param);
}
void React_TriggerEvent(int event, void* param) { TriggerEvent(event, param); }

typedef void(__attribute__((thiscall)) * ApplyForceFn)(void* self, const Vec3* dir, float force);
typedef int(__attribute__((thiscall)) * StateIdFn)(void* state);

// A frame at p with its Y axis along n (any X/Z round it)
static Mat4 FrameAt(Vec3 p, Vec3 n)
{
    Vec3 y = Norm(n);
    Vec3 x = Norm(Cross(fabsf(y.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), y));
    Vec3 z = Cross(x, y);
    Mat4 m = Identity();
    SetRow(m, 0, x, 0);
    SetRow(m, 1, y, 0);
    SetRow(m, 2, z, 0);
    SetRow(m, 3, p, 1);
    return m;
}

// A frame at p with its Z axis along f and Y as close to up as it can be
static Mat4 FrameZ(Vec3 p, Vec3 f)
{
    Vec3 z = Norm(f);
    Vec3 x = Norm(Cross(fabsf(z.y) < 0.95f ? V3(0, 1, 0) : V3(0, 0, 1), z));
    Vec3 y = Cross(z, x);
    Mat4 m = Identity();
    SetRow(m, 0, x, 0);
    SetRow(m, 1, y, 0);
    SetRow(m, 2, z, 0);
    SetRow(m, 3, p, 1);
    return m;
}

static int g_dbgFrame;   // debug "fx <type>,<frame>": 0 Z = -normal (what reactions use), 1 Y = normal, 2 Z = normal

// The sprays (dust, dirt, grass) come out along the effect's local -Z, like the spray behind a
// wheel (checked in the game with "fx <type>,<frame>"): Z = -out sends them out of the surface.
bool Fx_Particle(int type, Vec3 pos, Vec3 out, float bias)
{
    if (!English()) return false;
    uint8_t* pm = *(uint8_t**)A_ParticleManager;
    if (!pm || type < 0 || type >= *(int*)(pm + 4)) return false;
    ParticleAttr a = {type, bias, {0, 0, 0}};
    Mat4 m = g_dbgFrame == 1 ? FrameAt(pos, out) : g_dbgFrame == 2 ? FrameZ(pos, out) : FrameZ(pos, out * -1.0f);
    PM_Add(pm, &a, &m);
    return true;
}

bool Fx_Sparks(Vec3 pos, Vec3 dir, float strength)
{
    if (!English()) return false;
    void* sm = *(void**)A_SparkleManager;
    if (!sm) return false;
    SM_AddSparks(sm, &pos, &dir, strength);
    return true;
}

// ------------------------------------------------------------------ the world
// ParticleManager types, as they look in Level 1 ("fx <type>"): 3 twigs and leaves, 4 papers,
// 9 a white dust puff (tyre smoke), 10 a dirt burst with pebbles, 11 grass clippings, 12 water
// (not loaded in Level 1); 8, 14 and 19 are dust clouds and fires (explosions). One-shot each.
enum { PFX_PUFF = 9, PFX_DIRT = 10, PFX_GRASS = 11, PFX_WATER = 12 };

// The particle a building or prop sprays when a car hits it (leaves from a hedge...): its
// CollisionAttributes (+0x30), particle type at +0x48, -1 = none. Only for the classes whose
// collision handlers read it there (StaticPhysDSG 0x4A59B0, InstDynaPhysDSG 0x4A0B50).
static int OwnParticle(uint32_t obj)
{
    if (!obj) return -1;
    switch (*(const uint32_t*)obj) {
    case 0x60C89C:   // StaticPhysDSG
    case 0x60CB74:   // InstStatPhysDSG
    case 0x60CE9C:   // DynaPhysDSG
    case 0x60CC54:   // InstDynaPhysDSG
    case 0x60CD0C:   // InstAnimDynaPhysDSG
        break;
    default:
        return -1;
    }
    const uint8_t* attr = *(const uint8_t* const*)(obj + 0x30);
    return attr ? *(const int*)(attr + 0x48) : -1;
}

static int g_reactLog;   // debug "reactlog <shots>"

// ------------------------------------------------------------------ props
// What the kick does to a prop it hits (Character::Kick 0x4F34B0, the "anything else" branch):
// pushes it (ApplyForce, vtable slot 36), breaks it if it can break (slot 37, InstDynaPhysDSG
// 0x4A0C40: if its collision attributes say class 4 (+0x5C) with a break type (+0x4C, -1 none),
// the BreakablesManager plays the break - glass, a hydrant's fountain, a snapping tree - and the
// prop is queued for removal), then fires OBJECT_KICKED: the Hit & Run meter's own coins and heat
// for it (5 coins for Krusty glass, break type 0x13; heat and a coin for others; each prop
// counted once), the kick's thud, and pedestrians nearby panic.
// Pushes only go to classes whose slot 36 takes (dir, force) - buildings' is a different
// function - and only where the game simulates the prop (a ground plane, +0x90): outside its
// collision areas a pushed prop would fall through the ground, as pedestrians did. Breaking
// detaches the prop's physics first (slot 24), so the mod checks the attributes itself and only
// calls it when the break will happen.
// State props (StatePropDSG, e.g. phone booths) push differently (0x4A3AA0): first they send
// their own state machine (+0xDC) "hit" events - 8 always, 7 above 400 N, 10 from 800 N - which
// is how a phone booth smashes, then the physics push, which the game skips for class 4. So a
// class-4 state prop takes the push at any range (events only); others only in the game's physics.
static const uint32_t V_InstDynaPhysDSG = 0x60CC54, V_InstAnimDynaPhysDSG = 0x60CD0C, V_DynaPhysDSG = 0x60CE9C,
                      V_StatePropDSG = 0x60C97C, V_ActorDSG = 0x615C1C;
typedef void(__attribute__((thiscall)) * GetPositionFn)(void* self, Vec3* out);
typedef bool(__attribute__((thiscall)) * BreakFn)(void* self);
typedef void*(__attribute__((thiscall)) * GetSimStateFn)(void* self);

// Wasp cameras are actors (ActorDSG, AI ref 0x40003) with a state prop, "beecamera" in the
// level's lNN_fx.p3d: in states 0-4 its event 7, DESTROYED, goes to state 5, which shows the
// explosion and calls SPAWN_20_COINS, SPAWN_5_COINS, REMOVE_COLLISION_VOLUME and REMOVE_FROM_WORLD.
// ActorDSG::ApplyForce (slot 36, 0x4153A0) is the kick's way in: at 400 or more, an actor whose
// +0x100 is clear gets DESTROYED (and the push); one with +0x100 set (a shield, in later levels)
// has its own state machine (+0xF8) moved on and +0x100 cleared, so a second call destroys it.
// A shot is that, twice when it takes two: one shot, one wasp.
static const char* ReactActor(const WeaponSpec& w, uint8_t* o, Vec3 dir)
{
    if (!w.breaks) return "not a breaking weapon";
    Vec3 flat = Norm(V3(dir.x, 0.0f, dir.z));
    Vec3 push = flat * cosf(w.knockAngle) + V3(0.0f, sinf(w.knockAngle), 0.0f);
    float force = fmaxf(w.propForce, 400.0f);
    bool shield = o[0x100] != 0;
    ((ApplyForceFn)(*(void***)o)[36])(o, &push, force);
    if (shield) ((ApplyForceFn)(*(void***)o)[36])(o, &push, force);
    return shield ? "destroyed (shield and all)" : "destroyed";
}

static const char* ReactProp(const WeaponSpec& w, const HitResult& h, Vec3 dir)
{
    uint8_t* o = (uint8_t*)(uintptr_t)h.object;
    if (!o || h.ground >= 0) return nullptr;
    uint32_t vt = *(const uint32_t*)o;
    if (vt == V_ActorDSG) {
        const char* what = ReactActor(w, o, dir);
        if (w.heat) TriggerEvent(EV_OBJECT_KICKED, o);   // as the kick does (heat for it, panic)
        return what;
    }
    bool inst = vt == V_InstDynaPhysDSG || vt == V_InstAnimDynaPhysDSG;
    if (!inst && vt != V_DynaPhysDSG && vt != V_StatePropDSG) return nullptr;   // buildings don't move
    const uint8_t* attr = *(const uint8_t* const*)(o + 0x30);
    const char* what = "nothing (no physics here)";
    if (w.breaks && inst && attr && *(const int*)(attr + 0x5C) == 4 && *(const int*)(attr + 0x4C) != -1) {
        what = ((BreakFn)(*(void***)o)[37])(o) ? "broken" : "didn't break";
    } else if (w.propForce > 0) {
        void* ss = ((GetSimStateFn)(*(void***)o)[22])(o);
        bool eventsOnly = vt == V_StatePropDSG && attr && *(const int*)(attr + 0x5C) == 4;
        if (ss && *(void* const*)((const uint8_t*)ss + 0x74) && (eventsOnly || *(const int*)(o + 0x90) >= 0)) {
            Vec3 flat = Norm(V3(dir.x, 0.0f, dir.z));
            Vec3 push = flat * cosf(w.knockAngle) + V3(0.0f, sinf(w.knockAngle), 0.0f);
            ((ApplyForceFn)(*(void***)o)[36])(o, &push, w.propForce);
            what = eventsOnly ? "hit (its state machine)" : "pushed";
        }
    }
    if (w.heat) TriggerEvent(EV_OBJECT_KICKED, o);
    return what;
}

// A shot hits the world: the ground sprays what it's made of; buildings and props give a dust
// puff and their own particle if they have one (mail from a mailbox). Sparks fly off hard things
// (Hit_Surface: the road, metal, walls and other box-shaped pieces), not off hedges, trees and
// bushes. How big each is comes from the weapon (ImpactFx). Returns true if the game showed
// something.
static bool ReactWorld(const WeaponSpec& w, const HitResult& h, Vec3 dir)
{
    Surface s = Hit_Surface(h);
    int type = PFX_PUFF;
    float bias = w.fx.dust;
    switch (s) {
    case SURF_GRASS: type = PFX_GRASS; bias = w.fx.grass; break;
    case SURF_DIRT: type = PFX_DIRT; bias = w.fx.dirt; break;
    case SURF_WATER: type = PFX_WATER; bias = w.fx.water; break;
    default: break;   // hard, wood, round: a dust puff
    }
    int own = h.ground < 0 ? OwnParticle(h.object) : -1;
    bool shown = Fx_Particle(type, h.pos, h.normal, bias);
    if (own >= 0 && own != type) shown |= Fx_Particle(own, h.pos, h.normal, w.fx.own);
    if (s == SURF_HARD) shown |= Fx_Sparks(h.pos, h.normal, w.fx.sparks);
    const char* prop = ReactProp(w, h, dir);
    if (g_reactLog > 0) {
        g_reactLog--;
        const uint8_t* attr = h.object && h.ground < 0 ? *(const uint8_t* const*)(h.object + 0x30) : nullptr;
        Log("react: world %s, surface %d, particle %d, own %d, sparks %d -> %s; prop: %s (class %d, break type %d)",
            Game_ClassName(h.object), s, type, own, s == SURF_HARD, shown ? "shown" : "nothing", prop ? prop : "-",
            attr ? *(const int*)(attr + 0x5C) : -1, attr ? *(const int*)(attr + 0x4C) : -1);
    }
    return shown;
}

// ------------------------------------------------------------------ Hit & Run
// HitnRunManager ([0x6C84E0]) HandleEvent (vtable slot 1, 0x4D8B00, thiscall(event, param)),
// called directly: the meter follows the game's own rules (off indoors, in conversations and
// when a mission turns it off; the 3 s hold before it decays; the clamp at 100; a chase at 100)
// and no other listener hears it (VEHICLE_VEHICLE_COLLISION also reaches mission cargo
// objectives). The amounts are the game's: KICK_NPC +10 (what a kick adds), and
// VEHICLE_VEHICLE_COLLISION +5 at most every 1.5 s for traffic and AI cars, not chase cars or
// the mission's own car (what bumping one adds). Wrecking a car adds its +25 by itself.
typedef void(__attribute__((thiscall)) * HnrEventFn)(void* hnr, int event, void* param);
enum { EV_VEHICLE_VEHICLE_COLLISION = 0x60, EV_KICK_NPC = 0xA8 };

static float HeatMeter()
{
    if (!English()) return -1.0f;
    const uint8_t* h = *(const uint8_t* const*)0x6C84E0;
    return h ? *(const float*)(h + 4) : -1.0f;
}

static void Heat(int event, void* param)
{
    if (!English()) return;
    void* h = *(void**)0x6C84E0;
    if (h) ((HnrEventFn)0x4D8B00)(h, event, param);
}
float React_HeatMeter() { return HeatMeter(); }

// ------------------------------------------------------------------ vehicles
// Vehicle fields (checked in TriggerDamage 0x4EDF90 and the collision handler 0x4ECBA0):
// +0x2B8 mass, +0x2EC full hit points, +0x424 damage type, +0x494 destroyed, +0x49C seconds
// left of the grace period after a near-death hit, +0x4A0 hit points, +0x4A4 can be damaged
// (missions clear it), +0x4A8 SimState. Vtable slot 36 is ApplyForce (0x4EE3C0): physics
// locomotion (traffic leaves its lane, as when rammed), velocity += dir * force / mass, and a
// little damage of its own (half of force / 20000, not clamped).
static const uint32_t V_Vehicle = 0x60832C;

static bool ReactVehicle(const WeaponSpec& w, const HitResult& h, Vec3 dir)
{
    uint8_t* v = (uint8_t*)(uintptr_t)h.object;
    bool shown = !h.glass && !h.tire && Fx_Sparks(h.pos, h.normal, w.fx.carSparks);   // metal: always sparks (glass, tires: no)
    if (!v || *(const uint32_t*)v != V_Vehicle) return shown;
    float hp = *(const float*)(v + 0x4A0), full = *(const float*)(v + 0x2EC), heat = HeatMeter();
    const char* what = "damaged";
    if (v[0x494]) what = "already destroyed";
    else if (!v[0x4A4]) what = "can't be damaged";
    else if (*(const float*)(v + 0x49C) > 0.0f) what = "in its grace period";
    else {
        // a shove along the shot (level), for the jolt and so traffic reacts like it was hit.
        // First: its own little damage isn't clamped, so after the damage below it could skip
        // the game's "near death" second chance.
        Vec3 push = Norm(V3(dir.x, 0.0f, dir.z));
        if (w.vehicleForce > 0 && Len(push) > 0.5f) ((ApplyForceFn)(*(void***)v)[36])(v, &push, w.vehicleForce);
        // the game's own damage at the hit point; destruction, the explosion and the burnt-out
        // husk follow by themselves on the next frames
        uint8_t co[0x20] = {}, col[0x48] = {};
        *(uint32_t*)(co + 0x18) = *(const uint32_t*)(v + 0x4A8);
        *(uint8_t**)(col + 0x08) = co;
        *(Vec3*)(col + 0x38) = h.pos;
        v[0x474] = 1;               // hit by the player (as React_VehicleDamage does for rockets): if
        *(int*)(v + 0x478) = 0;     // it's wrecked, the game counts it for Hit & Run even after an AI car bumped it
        if (!v[0x494]) Veh_DamageAt(v, col, full * w.vehicleDamage);
    }
    if (w.heat && !v[0x494]) {   // like bumping it
        void* box[1] = {v};
        Heat(EV_VEHICLE_VEHICLE_COLLISION, box);
    }
    if (g_reactLog > 0) {
        g_reactLog--;
        Log("react: vehicle %s (%s), hit points %.3f -> %.3f of %.2f, damage type %d, Hit & Run %.1f -> %.1f",
            Game_ClassName(h.object), what, hp, *(const float*)(v + 0x4A0), full, *(const int*)(v + 0x424), heat,
            HeatMeter());
    }
    return shown;
}

// For explosions (explode.cpp). The game's damage at a point on the car (clamped: a car brought
// to the edge is held at "near death" once), or wrecked outright (TriggerDamage, no clamp). Either
// way it's marked as hit by the player's car (+0x474 = 1, +0x478 = 0: what the game's collision
// handler writes when he rams it, 0x4ED52B), so its wreck counts for Hit & Run the same way; the
// game only leaves that out when an AI car hit it last. Callers check "destroyed" (+0x494) and
// "can be damaged" (+0x4A4) first. Returns the hit points left.
float React_VehicleDamage(uint8_t* v, Vec3 point, float amount, bool wreck)
{
    if (!English()) return 0.0f;
    v[0x474] = 1;
    *(int*)(v + 0x478) = 0;
    if (wreck) {
        Veh_TriggerDamage(v, *(const float*)(v + 0x4A0) + *(const float*)(v + 0x2EC) + 1.0f, 0);
    } else {
        uint8_t co[0x20] = {}, col[0x48] = {};
        *(uint32_t*)(co + 0x18) = *(const uint32_t*)(v + 0x4A8);
        *(uint8_t**)(col + 0x08) = co;
        *(Vec3*)(col + 0x38) = point;
        Veh_DamageAt(v, col, amount);
    }
    return *(const float*)(v + 0x4A0);
}

// a shove (Vehicle::ApplyForce: physics, velocity += dir x force / mass, and half of
// force / 20000 in unclamped damage, as in the game's own car explosion) and the Hit & Run of
// bumping it (+5, at most every 1.5 s)
void React_VehicleShove(uint8_t* v, Vec3 dir, float force, bool heat)
{
    if (!English()) return;
    ((ApplyForceFn)(*(void***)v)[36])(v, &dir, force);
    if (heat && !v[0x494]) {
        void* box[1] = {v};
        Heat(EV_VEHICLE_VEHICLE_COLLISION, box);
    }
}

const char* React_PropHit(const WeaponSpec& w, uint32_t obj, Vec3 dir)
{
    if (!English()) return nullptr;
    HitResult h = {};
    h.kind = HIT_WORLD;
    h.object = obj;
    h.ground = h.volume = -1;
    return ReactProp(w, h, dir);
}

static int AiState(const uint8_t* c)
{
    const uint8_t* sm = *(const uint8_t* const*)(c + 0x190);
    void* st = sm ? *(void* const*)(sm + 4) : nullptr;
    return st ? ((StateIdFn)(*(void***)st)[5])(st) : -1;
}

// ------------------------------------------------------------------ pedestrians run away
// NPCController "panic" (0x4F8F40, eax = the controller; keeps edi): its state 6, running at 3 m/s
// along +0x1E8, which it points away from the player. The game does it to the pedestrians within
// 12 m of him when he kicks something or knocks someone over with a car. The mod also does it round
// where a shot lands, where a rocket goes off and where he fires, pointed away from there. Only the
// PedestrianManager's own walkers ([0x6C8464]: up to 21 entries of 12 bytes at +0x104 - the
// pedestrian, whose first field is its controller, then the next entry's index at +0x108; the first
// index at +0x1F8; a controller in use has +0x224 set), as the game does it: people a mission put
// somewhere (Marge at her door) have the same controller but aren't walkers, and stay put.
static const uint32_t V_NPCharacter = 0x607FF4;
static const uint32_t V_NPCController = 0x607DAC, V_Pedestrian = 0x6078CC;   // (a walker's is a Pedestrian, an NPCController)
static const uint32_t A_PedestrianManager = 0x6C8464;

static bool IsNpcController(const uint8_t* ctrl)
{
    uint32_t vt = ctrl ? *(const uint32_t*)ctrl : 0;
    return vt == V_NPCController || vt == V_Pedestrian;
}

// (the walkers' list is read with checked reads: a wrong guess about it once took the game down)
static bool SafeRead(uint32_t a, void* out, size_t n)
{
    SIZE_T got = 0;
    return a >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)a, out, n, &got) && got == n;
}

static uint32_t SafeU32(uint32_t a)
{
    uint32_t v = 0;
    return SafeRead(a, &v, 4) ? v : 0;
}

static void PanicController(void* ctrl)
{
    __asm__ __volatile__("call *%[fn]" : "+a"(ctrl) : [fn] "r"((void*)0x4F8F40) : "ecx", "edx", "memory", "cc");
}

static int g_panicLog;   // debug "paniclog <n>"

int React_PanicAround(Vec3 at, float r)
{
    if (!English() || r <= 0.0f) return 0;
    uint32_t pm = SafeU32(A_PedestrianManager);
    if (!pm) return 0;
    int n = 0, seen = 0;
    int i = (int)SafeU32(pm + 0x1F8);
    for (int guard = 0; i >= 0 && i < 20 && guard < 20; guard++) {
        uint32_t ped = SafeU32(pm + 0x104 + 12 * i);
        i = (int)SafeU32(pm + 0x108 + 12 * i);
        // (a walker's record is one of the seven of 76 bytes at +0x20C; its first field the controller)
        if (ped < pm + 0x20C || ped >= pm + 0x20C + 7 * 76 || (ped - pm - 0x20C) % 76) continue;
        uint8_t* ctrl = (uint8_t*)(uintptr_t)SafeU32(ped);
        uint32_t vt = ctrl ? SafeU32((uint32_t)(uintptr_t)ctrl) : 0;
        uint8_t inUse = 0;
        if ((vt != V_NPCController && vt != V_Pedestrian) || !SafeRead((uint32_t)(uintptr_t)ctrl + 0x224, &inUse, 1) || !inUse)
            continue;
        const uint8_t* c = (const uint8_t*)(uintptr_t)SafeU32((uint32_t)(uintptr_t)ctrl + 8);
        if (!c || SafeU32((uint32_t)(uintptr_t)c) != V_NPCharacter || c[0x128]) continue;
        seen++;
        // not someone just knocked down: running got him straight back up (a rocket's victim ran off
        // instead of flying); he runs once he's back on his feet (React_Update)
        const uint8_t* ss = (const uint8_t*)(uintptr_t)SafeU32((uint32_t)(uintptr_t)c + 0x70);
        if ((ss && SafeU32((uint32_t)(uintptr_t)ss + 0x6C) == 1) || AiState(c) == 5) continue;
        Vec3 pos = *(const Vec3*)(c + 0x64);
        Vec3 away = V3(pos.x - at.x, 0.0f, pos.z - at.z);
        float d = Len(away);
        if (d > r) continue;
        PanicController(ctrl);
        if (d > 0.2f) *(Vec3*)(ctrl + 0x1E8) = away * (1.0f / d);   // (right on top of it: away from him, the game's)
        n++;
    }
    if (g_panicLog > 0) {
        g_panicLog--;
        Log("panic: %d of %d walkers within %.0f m of (%.1f %.1f %.1f) run", n, seen, r, at.x, at.y, at.z);
    }
    return n;
}

static void DumpWalkers()
{
    uint32_t pm = SafeU32(A_PedestrianManager);
    Log("walkers: manager %08X, head %d, count %d", pm, (int)SafeU32(pm + 0x1F8), (int)SafeU32(pm + 0x1F4));
    for (int i = 0; pm && i < 21; i++)
        Log("walkers:   node %2d: %08X next %d prev %d", i, SafeU32(pm + 0x104 + 12 * i), (int)SafeU32(pm + 0x108 + 12 * i),
            (int)SafeU32(pm + 0x10C + 12 * i));
    for (int k = 0; pm && k < 7; k++) {
        uint32_t rec = pm + 0x20C + 76 * k, ctrl = SafeU32(rec), c = ctrl ? SafeU32(ctrl + 8) : 0;
        uint8_t inUse = 0;
        SafeRead(ctrl + 0x224, &inUse, 1);
        Log("walkers:   record %d at %08X: controller %08X (vtable %08X, in use %d, state %d) character %08X (vtable %08X)", k, rec,
            ctrl, ctrl ? SafeU32(ctrl) : 0, inUse, ctrl ? (int)SafeU32(ctrl + 0x40) : -1, c, c ? SafeU32(c) : 0);
    }
}

// ------------------------------------------------------------------ knocked down out of the game's physics
// The game simulates bodies only inside its collision areas (about 11 m round the player and round
// each active car). Pushed outside one, a character still flies - the game moves him - but there is
// no ground under him there and he fell through the map; so far away a shot made him cower at most
// (and past 25 m not even that showed). Now the mod is his ground: each frame (at Present, after the
// game's step) a body below the terrain is put back on it, its fall stopped and its slide slowed, and
// once he has lain still a moment he is handed back the way the game's own rest test does it
// (SimState::SetControl(0), the SimState's slot 12, his slot 33): the game ends its flail and plays
// its get-up. The game's own knock-down, at any distance.
typedef void(__attribute__((thiscall)) * SetControlFn)(void* simState, int control);
typedef void(__attribute__((thiscall)) * MethodFn)(void* self);
static const float kGamePhysicsReach = 9.0f;   // (its collision area round the player is about 11 m)
static const float kLyingHeight = 0.3f;        // a body lying down: its sim position this far over the ground
struct Carried {
    uint8_t* c;
    float t, still;   // seconds since the push; seconds lying still
    float h, vy;      // the flight the game won't show (it keeps him on the ground there): drawn by lifting his bones
    Vec3 from;        // what hit him: once he's up he runs from it
    bool distant;     // knocked down out of the game's physics (else the game flies him; the mod only catches him)
    bool caught;      // he sank through the ground and was put back: the game's physics has let go of him
    float gameUp;     // how high the game itself has him (inside a collision area it does fly him)
    float y0;         // the ground he stood on (when the terrain test finds none, or only a bridge above)
};
static Carried g_carried[12];
static int g_ncarried;
static int g_carryLog;   // debug "carrylog <frames>"

static uint8_t* CharacterAt(int i)
{
    uint32_t cm = Game_CharacterManager();
    return cm ? *(uint8_t* const*)(uintptr_t)(cm + 192 + 4 * i) : nullptr;
}

static bool OnCharacterList(const uint8_t* c)
{
    uint32_t cm = Game_CharacterManager();
    for (int i = 1; cm && i < 64; i++)
        if (*(const uint32_t*)(uintptr_t)(cm + 192 + 4 * i) == (uint32_t)(uintptr_t)c) return true;
    return false;
}

static float GroundUnder(const Carried& k, Vec3 p)
{
    float y;
    if (!Trace_GroundY(p, &y) || y > k.y0 + 1.5f) y = k.y0;   // (none, or a bridge deck over where he stood)
    return y;
}

// room to carry him; false: none left (out of the game's physics he then only cowers)
static bool Carry(uint8_t* c, Vec3 from, bool distant)
{
    const uint8_t* ss = *(const uint8_t* const*)(c + 0x70);
    if (!ss) return false;
    for (int i = 0; i < g_ncarried; i++)
        if (g_carried[i].c == c) {
            g_carried[i].t = g_carried[i].still = 0.0f;
            g_carried[i].from = from;
            g_carried[i].distant |= distant;
            return true;
        }
    if (g_ncarried >= 12) return false;
    Carried& k = g_carried[g_ncarried];
    k = {};
    k.c = c;
    k.from = from;
    k.distant = distant;
    k.y0 = (*(const Vec3*)(c + 0x64)).y;   // his feet
    g_ncarried++;
    return true;
}

void React_Update(float dt, bool run)
{
    if (!run || dt <= 0.0f) return;   // (paused: the game doesn't move them either)
    for (int i = 0; i < g_ncarried;) {
        Carried& k = g_carried[i];
        uint8_t* c = k.c;
        uint8_t* ss = OnCharacterList(c) && *(const uint32_t*)c == V_NPCharacter ? *(uint8_t**)(c + 0x70) : nullptr;
        // gone, or back in the game's hands (its rest test)
        bool done = !ss || *(const int*)(ss + 0x6C) != 1;
        if (!done) {
            Vec3& p = *(Vec3*)(ss + 0x40);
            Vec3& v = *(Vec3*)(ss + 0x50);
            Vec3& w = *(Vec3*)(ss + 0x5C);
            // Far off, the game mostly keeps him on the terrain itself (each step his height goes back to
            // the ground and his fall is stopped: he slides, flailing, and doesn't fly), but now and then
            // one sank through it: put back on top, lying (his sim position then is his middle, about 0.3 m
            // up). And the slide has no grip there: it is slowed here.
            // Close by too: a body knocked out of the game's collision area round the player (shoved
            // away from him) loses its ground there and fell through the map.
            // Caught as soon as his middle is lower than a body lying there has it (by then he was already
            // half under: a rocket's victim was seen sinking to the waist before he came back up), or
            // would be by the next step at the speed he's falling: put down at lying height instead.
            float ground = GroundUnder(k, p);
            float floor = ground + kLyingHeight * 0.4f;
            if (p.y < floor || (v.y < 0.0f && p.y + v.y * dt < floor)) {
                p.y = ground + kLyingHeight;
                if (v.y < 0.0f) v.y = 0.0f;
                k.caught = true;
            }
            bool ours = k.distant || k.caught;   // (the game still has him: its own landing, slide and get-up)
            // his flight, up and back down with a little bounce (drawn by React_LiftOf)
            k.h += k.vy * dt;
            k.vy -= 9.8f * dt;
            if (k.h <= 0.0f) {
                k.h = 0.0f;
                k.vy = k.vy < -3.0f ? -k.vy * 0.3f : 0.0f;
            }
            bool down = p.y <= ground + kLyingHeight + 0.15f && k.h <= 0.0f;
            if (down && ours) {
                float f = expf(-dt * 2.5f);
                v.x *= f;
                v.z *= f;
                w = w * f;
            }
            k.gameUp = fmaxf(0.0f, p.y - ground);   // (inside a collision area the game flies him itself)
            float speed = Len(v);
            k.still = down && speed < 0.8f ? k.still + dt : 0.0f;
            k.t += dt;
            if (g_carryLog > 0) {
                g_carryLog--;
                Log("carry: %08X t %.2f pos (%.2f %.2f %.2f) ground %.2f speed %.2f still %.2f ai %d flight %.2f", (uint32_t)(uintptr_t)c,
                    k.t, p.x, p.y, p.z, ground, speed, k.still, AiState(c), k.h);
            }
            if (ours && ((k.still > 0.5f && k.t > 1.6f) || k.t > 5.0f)) {   // lying still a moment: the game's get-up
                ((SetControlFn)0x518020)(ss, 0);
                ((MethodFn)(*(void***)ss)[12])(ss);
                ((MethodFn)(*(void***)c)[33])(c);
                done = true;
                // and then he runs from what hit him (a walker: the game's panic)
                uint8_t* ctrl = *(uint8_t**)(c + 0x100);
                if (IsNpcController(ctrl) && *(const uint32_t*)ctrl == V_Pedestrian) {
                    PanicController(ctrl);
                    Vec3 away = V3(p.x - k.from.x, 0.0f, p.z - k.from.z);
                    if (Len(away) > 0.2f) *(Vec3*)(ctrl + 0x1E8) = Norm(away);
                }
            }
        }
        if (done)
            g_carried[i] = g_carried[--g_ncarried];
        else
            i++;
    }
}

void React_Clear() { g_ncarried = 0; }

bool React_Lifting()
{
    for (int i = 0; i < g_ncarried; i++)
        if (g_carried[i].h > 0.01f) return true;
    return false;
}

// his flight's height for the bones being drawn near `viewPos` (view space): 0 for anyone else
float React_LiftNear(const float* view16, Vec3 viewPos)
{
    Mat4 v;
    memcpy(v.m, view16, 64);
    for (int i = 0; i < g_ncarried; i++) {
        const Carried& k = g_carried[i];
        if (k.h <= 0.01f) continue;
        Vec3 c = XformPoint(*(const Vec3*)(k.c + 0x64) + V3(0, 0.8f, 0), v);
        if (Len(c - viewPos) < 1.6f) return fmaxf(0.0f, k.h - k.gameUp);   // only what the game doesn't show
    }
    return 0.0f;
}

static void Launch(uint8_t* c, float up)
{
    for (int i = 0; i < g_ncarried; i++)
        if (g_carried[i].c == c) g_carried[i].vy = fmaxf(g_carried[i].vy, up);
}

// ------------------------------------------------------------------ characters
// What the kick does to a pedestrian (Character::Kick 0x4F34B0, checked in the disassembly):
// events KICK_NPC_SOUND (the victim's "hit by walker" line) and
// PEDESTRIAN_SMACKDOWN (the player's taunt), its Hit & Run (once per knock-down, not per bullet),
// HasBeenHit (+0x39D) = 1, the push, then
// OBJECT_KICKED (pedestrians within 12 m panic). The push is an upward-angled velocity change of
// force / mass (characters have no collision attributes: mass 100): ApplyKickForce (vtable
// slot 54, also primes the "at rest" smoothers so he doesn't get up at once) while walking about,
// ApplyForce (slot 36) when already down. The game turns that into its own flail and get-up.
// The force and the angle (the kick's is 45 degrees) come from the weapon.
// Character fields: +0x70 SimState (+0x70 its physics object), +0x90 its ground plane (-1 none),
// +0x100 its controller, +0x128 in a car, +0x190 the AI state manager (+0x04 the state; vtable
// slot 5 its id: 1 walking about, 5 knocked down).
//
// Physics only happens inside the game's collision areas: about 11 m round the player and round
// each active car. A character inside one has a ground plane there (+0x90); outside, a push
// sends him through the ground and under the map (seen in the game). So a pedestrian outside
// them cowers instead: the controller's intention (vtable slot 9) 16, Cringe, which the walking
// state plays as the game's own "cringe" (what a pedestrian does when a car comes at him).
typedef void(__attribute__((thiscall)) * SetIntentionFn)(void* ctrl, int intention);


static bool ReactCharacter(const WeaponSpec& w, const HitResult& h, Vec3 dir)
{
    uint8_t* c = (uint8_t*)(uintptr_t)h.object;
    if (!c || *(const uint32_t*)c != V_NPCharacter) return false;   // pedestrians only
    const uint8_t* ss = *(const uint8_t* const*)(c + 0x70);
    float heat = HeatMeter();
    int st = -1;
    const char* what = "knocked down";
    if (c[0x128]) what = "in a car";
    else if (!ss || !*(void* const*)(ss + 0x70)) what = "no physics";
    else if ((st = AiState(c)) != 1 && st != 5) what = "busy (a car door)";
    else {
        TriggerEvent(EV_KICK_NPC_SOUND, c);
        TriggerEvent(EV_PEDESTRIAN_SMACKDOWN, nullptr);
        // near the player the game's physics catches him; further out the mod is his ground too (see
        // Carry) - unless it is carrying a dozen already: then he cowers on his feet. (A ground
        // plane is no sign: walkers far off have one and still fell through the map.)
        const uint8_t* me = CharacterAt(0);   // (the character list's first is the player)
        bool inPhysics = me && Len(*(const Vec3*)(c + 0x64) - *(const Vec3*)(me + 0x64)) < kGamePhysicsReach;
        bool down = Carry(c, h.pos - dir * 2.0f, !inPhysics) || inPhysics;
        // once per knock-down, like a kick (before "has been hit": first-hit coins); a cowering one
        // only for the first hit (every bullet into a cowering pedestrian used to add another +10)
        if (w.heat && st == 1 && (down || !c[0x39D])) Heat(EV_KICK_NPC, c);
        c[0x39D] = 1;
        if (down) {
            Vec3 flat = Norm(V3(dir.x, 0.0f, dir.z));
            Vec3 push = flat * cosf(w.knockAngle) + V3(0.0f, sinf(w.knockAngle), 0.0f);
            ((ApplyForceFn)(*(void***)c)[st == 1 ? 54 : 36])(c, &push, w.characterForce);
            if (!inPhysics) Launch(c, w.characterForce / 100.0f * sinf(w.knockAngle));   // (the push's rise: mass 100)
            if (g_carryLog > 0 && ss) {
                const Vec3* v = (const Vec3*)(ss + 0x50);
                Log("carry: %08X pushed %.0f N: vel (%.2f %.2f %.2f) control %d", (uint32_t)(uintptr_t)c, w.characterForce, v->x,
                    v->y, v->z, *(const int*)(ss + 0x6C));
            }
            what = st == 5 ? "knocked again (already down)" : inPhysics ? "knocked down" : "knocked down (out of the game's physics: the mod is his ground)";
        } else {
            void* ctrl = *(void* const*)(c + 0x100);
            if (st == 1 && IsNpcController((const uint8_t*)ctrl)) ((SetIntentionFn)(*(void***)ctrl)[9])(ctrl, 16);
            what = "cowers (no room to carry another)";
        }
        TriggerEvent(EV_OBJECT_KICKED, c);
    }
    if (g_reactLog > 0) {
        g_reactLog--;
        Log("react: character %s %08X, state %d: %s, Hit & Run %.1f -> %.1f", Game_ClassName(h.object), h.object, st,
            what, heat, HeatMeter());
    }
    return false;
}

bool React_IsMovable(uint32_t obj)
{
    if (!obj) return false;
    switch (*(const uint32_t*)(uintptr_t)obj) {
    case 0x615C1C:   // ActorDSG (wasp cameras)
    case 0x60CC54:   // InstDynaPhysDSG
    case 0x60CD0C:   // InstAnimDynaPhysDSG
    case 0x60CE9C:   // DynaPhysDSG
    case 0x60C97C:   // StatePropDSG
        return true;
    }
    return false;
}

bool Hit_React(const WeaponSpec& w, const HitResult& h, Vec3 dir)
{
    if (!English()) return false;
    bool shown = false;
    switch (h.kind) {
    case HIT_WORLD:
        shown = ReactWorld(w, h, dir);
        break;
    case HIT_VEHICLE:
        shown = ReactVehicle(w, h, dir);
        break;
    case HIT_CHARACTER:
        shown = ReactCharacter(w, h, dir);
        break;
    default:
        return false;
    }
    // the people near where a bullet lands run from it (a rocket's blast does its own, wider)
    if (w.blast <= 0.0f && w.heat) React_PanicAround(h.pos, kShotPanic);
    return shown;
}

// ------------------------------------------------------------------ debug
// actors (wasp cameras) in the game's dynamics list near the player: their positions (slot 16)
int React_ListActors(uint32_t player, float radius, float (*pos)[3], int max)
{
    float p[3];
    if (!Game_PlayerPosition(player, p)) return 0;
    uint32_t objs[50];
    int n = Trace_ListDynamics(V3(p[0], p[1], p[2]), radius, player, objs, 50), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        uint8_t* o = (uint8_t*)(uintptr_t)objs[i];
        if (*(const uint32_t*)o != V_ActorDSG) continue;
        Vec3 q;
        ((GetPositionFn)(*(void***)o)[16])(o, &q);
        pos[k][0] = q.x, pos[k][1] = q.y, pos[k][2] = q.z;
        k++;
    }
    return k;
}

void React_DebugChars(uint32_t player, float radius)
{
    float pp[3];
    if (!English() || !Game_PlayerPosition(player, pp)) return;
    Vec3 me = V3(pp[0], pp[1], pp[2]);
    int shown = 0;
    for (int i = 1; i < 64; i++) {
        uint8_t* c = CharacterAt(i);
        if (!c) continue;
        Vec3 p = *(const Vec3*)(c + 0x64);
        float d = Len(p - me);
        if (d > radius) continue;
        const uint8_t* ss = *(const uint8_t* const*)(c + 0x70);
        const uint8_t* ctrl = *(const uint8_t* const*)(c + 0x100);
        bool npcCtrl = IsNpcController(ctrl);
        uint32_t objs[50];
        int n = Trace_ListDynamics(p + V3(0, 0.8f, 0), 2.0f, player, objs, 50);
        bool listed = false;
        for (int k = 0; k < n; k++) listed |= objs[k] == (uint32_t)(uintptr_t)c;
        Log("chars: %2d %08X (%.1f %.1f %.1f) %5.1f m | ai %d, ground plane %d, area %d, far %d, visible %d, in view %d, "
            "ambient %d, role %d, sim control %d, in dynamics %d | controller %s %08X state %d panic %d",
            i, (uint32_t)(uintptr_t)c, p.x, p.y, p.z, d, AiState(c), *(const int*)(c + 0x90), *(const int*)(c + 0xF0),
            c[0x3A4], c[0x350], c[0x99], c[0x378], *(const int*)(c + 0x3AC), ss ? *(const int*)(ss + 0x6C) : -1, listed,
            !ctrl ? "none" : *(const uint32_t*)ctrl == V_Pedestrian ? "walker" : npcCtrl ? "NPC" : "other", ctrl ? *(const uint32_t*)ctrl : 0, npcCtrl ? *(const int*)(ctrl + 0x40) : -1,
            npcCtrl ? ctrl[0x1E0] : -1);
        shown++;
    }
    Log("chars: %d within %.0f m", shown, radius);
}

void React_DebugCmd(const char* cmd, const char* arg, uint32_t player)
{
    if (!strcmp(cmd, "chars")) {   // "chars [radius]": every character near the player and its state
        React_DebugChars(player, arg[0] ? (float)atof(arg) : 200.0f);
        return;
    }
    if (!strcmp(cmd, "panicat")) {   // "panicat x,y,z,r": pedestrians within r of the point run from it
        Vec3 at = {};
        float r = 10.0f;
        sscanf(arg, "%f,%f,%f,%f", &at.x, &at.y, &at.z, &r);
        Log("panicat: %d walkers within %.0f m of (%.1f %.1f %.1f)", React_PanicAround(at, r), r, at.x, at.y, at.z);
        return;
    }
    if (!strcmp(cmd, "reactlog")) {
        g_reactLog = arg[0] ? atoi(arg) : 1;
        return;
    }
    if (!strcmp(cmd, "knocknpc")) {   // "knocknpc <i>[,<force>,<degrees up>]": character i takes a hit from the player's way (no trace)
        int idx = 0;
        float force = 500.0f, deg = 45.0f;
        sscanf(arg, "%d,%f,%f", &idx, &force, &deg);
        uint8_t* c = CharacterAt(idx);
        const uint8_t* me = CharacterAt(0);
        if (!c || !me || c == me) return;
        WeaponSpec w = {};
        w.name = "test";
        w.characterForce = force;
        w.knockAngle = deg * 3.14159265f / 180.0f;
        HitResult h = {};
        h.kind = HIT_CHARACTER;
        h.object = (uint32_t)(uintptr_t)c;
        h.pos = *(const Vec3*)(c + 0x64) + V3(0, 1.0f, 0);
        h.ground = h.volume = -1;
        Vec3 dir = Norm(h.pos - *(const Vec3*)(me + 0x64));
        h.normal = dir * -1.0f;
        int was = g_reactLog;
        g_reactLog = 1;
        ReactCharacter(w, h, dir);
        g_reactLog = was;
        React_PanicAround(h.pos, kShotPanic);
        return;
    }
    if (!strcmp(cmd, "walkers")) {   // the PedestrianManager's list, read with checked reads
        DumpWalkers();
        return;
    }
    if (!strcmp(cmd, "paniclog")) {
        g_panicLog = arg[0] ? atoi(arg) : 5;
        return;
    }
    if (!strcmp(cmd, "carrylog")) {
        g_carryLog = arg[0] ? atoi(arg) : 60;
        return;
    }
    if (!strcmp(cmd, "wasps")) {   // "wasps [radius]": the actors (wasp cameras) near the player
        float pos[16][3];
        int n = React_ListActors(player, arg[0] ? (float)atof(arg) : 60.0f, pos, 16);
        for (int i = 0; i < n; i++) Log("wasps: actor at (%.1f %.1f %.1f)", pos[i][0], pos[i][1], pos[i][2]);
        Log("wasps: %d", n);
        return;
    }
    if (!strcmp(cmd, "props")) {   // "props [radius]": the loose props near the player, and which can break
        float p[3];
        if (!Game_PlayerPosition(player, p)) return;
        uint32_t objs[50];
        int n = Trace_ListDynamics(V3(p[0], p[1], p[2]), arg[0] ? (float)atof(arg) : 20.0f, player, objs, 50);
        for (int i = 0; i < n; i++) {
            const uint8_t* o = (const uint8_t*)(uintptr_t)objs[i];
            uint32_t vt = *(const uint32_t*)o;
            if (vt != V_InstDynaPhysDSG && vt != V_InstAnimDynaPhysDSG && vt != V_DynaPhysDSG && vt != V_StatePropDSG) continue;
            const uint8_t* attr = *(const uint8_t* const*)(o + 0x30);
            const float* q = (const float*)(o + 0x64);
            Log("props: %s %08X at (%.1f %.1f %.1f) %.1f m, class %d, break type %d, particle %d, sound '%.24s', ground plane %d",
                Game_ClassName(objs[i]), objs[i], q[0], q[1], q[2],
                sqrtf((q[0] - p[0]) * (q[0] - p[0]) + (q[2] - p[2]) * (q[2] - p[2])), attr ? *(const int*)(attr + 0x5C) : -1,
                attr ? *(const int*)(attr + 0x4C) : -1, attr ? *(const int*)(attr + 0x48) : -1,
                attr ? (const char*)(attr + 8) : "", *(const int*)(o + 0x90));
        }
        Log("props: %d dynamics within range", n);
        return;
    }
    bool fx = !strcmp(cmd, "fx"), sparks = !strcmp(cmd, "sparks"), hole = !strcmp(cmd, "hole");
    if (!fx && !sparks && !hole) return;
    // at what's in the middle of the screen
    float view[16], proj[16];
    if (!Game_GetCamera(view, proj)) return;
    Mat4 v;
    memcpy(v.m, view, 64);
    Mat4 cw = RigidInverse(v);
    Vec3 eye = Row(cw, 3), fwd = Norm(Row(cw, 2));
    HitResult h;
    if (!TraceShot(eye, eye + fwd * 100.0f, player, &h)) {
        Log("%s: nothing under the crosshair", cmd);
        return;
    }
    if (hole) {   // "hole [size]": a bullet hole where the crosshair points, nothing else
        Decal_Add(h.pos, h.normal, arg[0] ? (float)atof(arg) : 0.14f);
        Log("hole at %.2f m", h.dist);
    } else if (fx) {   // "fx <type>[,<frame>[,<bias>]]"
        int type = 0;
        float bias = 1.0f;
        g_dbgFrame = 0;
        sscanf(arg, "%d,%d,%f", &type, &g_dbgFrame, &bias);
        uint8_t* pm = *(uint8_t**)A_ParticleManager;
        bool ok = Fx_Particle(type, h.pos, h.normal, bias);
        g_dbgFrame = 0;
        Log("fx %d at %.2f m: %s (types %d)", type, h.dist, ok ? "added" : "refused", pm ? *(int*)(pm + 4) : -1);
    } else {   // "sparks <strength>"
        float s = arg[0] ? (float)atof(arg) : 0.5f;
        Log("sparks %.2f at %.2f m: %s", s, h.dist, Fx_Sparks(h.pos, h.normal, s) ? "added" : "refused");
    }
}
