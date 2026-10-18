// The hit system, shared by every weapon: FireWeapon -> TraceShot -> HitResult -> reaction.
// A weapon is a WeaponSpec (data: how far, how many pellets, how hard it hits, how big its
// impacts look) plus where its shot starts (ShotOrigin); FireWeapon does the rest and hands
// back what each pellet hit, for the weapon's own visuals. The handgun (gun.cpp, kHandgun) is
// the reference implementation.
#pragma once
#include "common.h"
#include "math3d.h"

enum HitKind { HIT_NONE, HIT_WORLD, HIT_VEHICLE, HIT_CHARACTER };

struct HitResult {
    HitKind kind;
    Vec3 pos;          // where the shot stops, on the surface
    Vec3 normal;       // the surface's normal there, facing back along the shot
    float dist;        // metres from the start of the trace (FireWeapon: from the muzzle)
    uint32_t object;   // the game object: Vehicle*, Character*, or the DSG of a building or prop; 0 = the ground
    uint32_t aiRef;    // the object's GetAIRef: 1 vehicle, 0x20001 player, 0x20002 pedestrian, 0x10000 static...
    int ground;        // the ground itself was hit: its terrain type (low bits) + 0x80 indoors; -1 otherwise
    int volume;        // the collision volume type hit (1 sphere, 2 cylinder, 3 box, 4 wall, 0 box); -1 = the ground
    bool glass;        // a car's window (carglass.cpp)
    bool tire;         // a car's tire (tires.cpp)
    bool drawn;        // (vcover.cpp) a drawn surface the game's collision doesn't have (object 0, no ground):
                       // the Simpsons' bay window, their garage wall
};

// What was hit, as far as effects care. Buildings don't say what they're made of, so pieces
// are told apart by shape: boxes and walls are hard, round pieces are hedges, trees, bushes.
enum Surface {
    SURF_NONE,
    SURF_HARD,    // roads, metal ground, walls and other box-shaped pieces: dust and sparks
    SURF_WOOD,    // wooden ground: dust, no sparks
    SURF_ROUND,   // round pieces of buildings (hedges, trees, bushes): dust, no sparks
    SURF_GRASS,   // lawns
    SURF_DIRT,    // dirt, sand, gravel
    SURF_WATER,
    SURF_CAR,     // vehicles
    SURF_BODY,    // characters
    SURF_GLASS,   // a car's windows
    SURF_TIRE,    // a car's tires
};

const char* Hit_KindName(HitKind k);
Surface Hit_Surface(const HitResult& h);
bool Surface_IsSoft(Surface s);   // grass, dirt, water: a soft thud, brown dust

// ---------------------------------------------------------------- trace.cpp
// A segment against the game's own collision: buildings, props, vehicles, characters and
// the ground. `ignore` (a Character* or other object) is left out; `radius` is the shot's
// thickness (every volume grows by it). Returns false (kind HIT_NONE) when nothing is in the
// way or the game's collision isn't available.
static const float kTraceRadius = 0.02f;   // a bullet; also used for test traces
bool Trace_Available();
bool TraceShot(Vec3 from, Vec3 to, uint32_t ignore, HitResult* hit, float radius = kTraceRadius);
void Trace_SetIgnore3(uint32_t object);   // (M5) for a moment, one more: a car a shot went right through
void Trace_SetIgnore2(uint32_t object);   // (drive-by) also leave this one out of every trace (0 = none): his own car
void Trace_SetIgnore4(uint32_t object);   // (vcover) for a moment: a person whose collision cylinder a pellet only grazed
void Trace_Describe(const HitResult& h, char* out, int n);   // one line for the log
bool Trace_GroundY(Vec3 p, float* y);   // the terrain's height under p (roads, grass, sidewalks)
int Trace_ListDynamics(Vec3 c, float r, uint32_t ignore, uint32_t* out, int max);   // debug surveys
extern int g_traceLog;   // debug: log the inside of the next n traces

// ---------------------------------------------------------------- weapon.cpp
// How big the game's own effects are where a shot lands: emission bias (1 = full; the dust
// puff at 1 is a car's tyre smoke) and spark strength (about (10..20) x strength streaks).
struct ImpactFx {
    float dust;        // walls, buildings, roads, wood
    float dirt;        // dirt, sand, gravel
    float grass;       // lawns
    float water;
    float own;         // a building's or prop's own particle (mail from a mailbox)
    float sparks;      // off hard surfaces
    float carSparks;   // off cars
};

struct WeaponSpec {
    const char* name;
    float range;           // metres
    float radius;          // the shot's thickness in the traces (metres)
    int pellets;           // rays per shot (each traced and reacting on its own)
    float spread;          // half-angle of the cone the pellets scatter in (radians; 0 = dead on)
    float vehicleDamage;   // per pellet, as a share of a car's full hit points
    float vehicleForce;    // per pellet, the shove (newtons; a car's velocity changes by force / mass)
    float characterForce;  // per pellet, the knock-down push (a pedestrian's mass is 100; the kick uses 400)
    float knockAngle;      // how far up the knock-down and prop pushes point (radians; the kick's is 45 degrees)
    float propForce;       // per pellet, the push on loose props (mailboxes, bins; the kick uses 400)
    bool breaks;           // smashes props the game can break (hydrants, Krusty glass...) like a car or kick does
    bool heat;             // raises the Hit & Run meter (in the game's own amounts)
    float holeSize;        // bullet holes left on buildings, walls and roads (metres across; 0 = none)
    ImpactFx fx;
    float blast;           // an explosion's radius (metres; 0 = a bullet: only what it hits reacts)
    bool group;            // (shotgun) a person or loose prop hit by several pellets reacts once, harder the more hit
                           // (characterForce / propForce are then a full blast's)
};

// Where a shot starts. Aimed shots go through the crosshair (the camera's centre ray);
// from the hip they go along the barrel.
struct ShotOrigin {
    uint32_t shooter;   // Character* firing (left out of the traces)
    Vec3 body;          // a point inside the shooter: a gun poked through a wall hits the wall
    Vec3 muzzle;        // where the bullet leaves the gun
    Vec3 dir;           // from the hip: where it goes from the muzzle (unit)
    bool aimed;
    Vec3 eye, eyeDir;   // aimed: the camera's position and its centre ray (unit)
};

// One pellet: what it hit, the visible bullet path (from the muzzle to the impact or the end
// of the range), and whether the game showed an effect there (the weapon can then leave out
// its own dust).
struct ShotResult {
    HitResult hit;
    Vec3 from, to;
    bool gameFx;
};

enum { kMaxPellets = 16 };

// Fires one shot: every pellet is traced and the thing it hit reacts (Hit_React). Fills
// out[0..n-1] and returns n (the spec's pellets, at most `max`).
int FireWeapon(const WeaponSpec& w, const ShotOrigin& o, ShotResult* out, int max);

// ---------------------------------------------------------------- vcover.cpp
// Everything drawn stops a shot. The game's collision leaves out parts of what is drawn (the Simpsons' bay window and
// the side of their garage have no collision at all), and a person's collision cylinder is much wider than the person.
// While a weapon is aimed, the depth buffer is read along each pellet's ray of the NEXT shot (its pattern is rolled a
// shot ahead), so FireWeapon knows how far the drawn picture goes along every pellet.
void VCover_Arm(int pellets, float spread);   // every frame a weapon is aimed (Present)
int VCover_ProbeWant(float (*px)[2], float (*range)[2], int max, int W, int H);   // inside the scene: pixels to probe
void VCover_ProbeResult(const float* depth, int n);                               // Present: what they read
bool VCover_Ready(int pellets, float spread);   // the pattern was read on the frame just drawn
// pellet i of the pattern: the ray (from the camera that drew the frame), the drawn surface along it (metres, -1
// nothing drawn, -3 not known) and that surface's normal (facing back along the ray)
bool VCover_Pellet(int i, Vec3* eye, Vec3* dir, float* vis, Vec3* normal);
void VCover_Used();   // fired: roll the next pattern
// The rule (FireWeapon and the lab's coverage scan both use it): a camera ray, what is drawn along it (vis), where the
// trace started (startT, metres along the ray) and what it met. 0 keep the trace's hit; 1 stop on the drawn surface
// (nothing the game can push there: a building's missing piece); 2 the trace met a person's cylinder where nothing of
// them is drawn - the pellet goes past them; 4 / 5 a person / a car is drawn there that the trace missed (*drawnAs:
// the hit on them)
int VCover_Decide(Vec3 eye, Vec3 dir, float vis, float startT, const HitResult& h, uint32_t shooter, HitResult* drawnAs = nullptr);
void VCover_DebugCmd(const char* cmd, const char* arg);
int VCover_LogShots();   // debug "vclog <shots>": shots left to log
void VCover_LogUsed();
extern bool g_vcoverOn;   // debug "vcover 0|1" (on by default)
// Where an aimed shot is meant to land: what's under the crosshair, traced from level with the
// shooter (FireWeapon's first step). False = nothing in range (*target = the end of the range).
bool Weapon_AimPoint(const WeaponSpec& w, const ShotOrigin& o, Vec3* target, HitResult* hit);
void Weapon_DebugCmd(const char* cmd, const char* arg, uint32_t player);

static inline float Clamp01f(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ---------------------------------------------------------------- rocket.cpp
// The rocket launcher's rocket: a projectile flying through the world (the game's collision
// tested along every frame's stretch of its flight), exploding where it hits. Game time.
struct RocketPuff {   // the smoke trail
    Vec3 p, v;
    float t, life, size, rot, shade;
};
struct RocketView {   // a rocket in flight, for drawing
    Mat4 world;       // rocket space (+Z forward) -> world, scaled
    Vec3 tail, dir;
    float scale, age;
};
void Rocket_Fire(const WeaponSpec& w, Vec3 body, Vec3 start, Vec3 dir, float scale, uint32_t shooter,
                 const HitResult* drawnStop = nullptr);   // (v1.0: what the crosshair is on as drawn, if collision lacks it)
// run: gameplay; freeze: the game is paused (keep them as they are); neither: take them away
void Rocket_Update(float dt, uint32_t player, bool run, bool freeze);
int Rocket_ProbeWant(float (*px)[2], float (*range)[2], int used, int max, int W, int H);   // (v1.0) inside the scene: pixels ahead of rockets
void Rocket_ProbeResult(const float* depth, int n);                                         // Present: what they read
void Rocket_Clear();
bool Rocket_Any();
int Rocket_Views(RocketView* out, int max);
int Rocket_Trail(const RocketPuff** out);
void Rocket_DebugCmd(const char* cmd, const char* arg);

// ---------------------------------------------------------------- explode.cpp
// An explosion at `at`: h is what the rocket hit there (HIT_NONE = in the air), dir its flight.
void Explode(const WeaponSpec& w, const HitResult& h, Vec3 at, Vec3 dir, uint32_t shooter);
void Explode_Update(float dt, uint32_t player, bool run, bool freeze);
bool Explode_Any();
void Explode_Clear();
struct BlastFx {   // the mod's own part of an explosion's look (flash, fireball puffs), for drawing
    Vec3 p, v;
    float t, life, size, rot;
    int kind;      // 0 flash, 1 fire puff, 2 smoke puff
};
int Explode_Fx(const BlastFx** out);
// a camera kick for the explosions near the player this frame (0 = none), taken once
float Explode_TakeShake();
void Explode_DebugCmd(const char* cmd, const char* arg, uint32_t player);
const WeaponSpec* Gun_RocketSpec();   // gun.cpp: the rocket launcher's spec (for the "boom" test)

// ---------------------------------------------------------------- decal.cpp
// Bullet holes: the newest 64 stay, drawn on the surface's plane in the game's 3D world.
enum { DECAL_HOLE, DECAL_SCORCH, DECAL_GLASS };
void Decal_Add(Vec3 pos, Vec3 normal, float size, int kind = DECAL_HOLE);
void Decal_AddOnVehicle(uint32_t vehicle, Vec3 pos, Vec3 normal, float size, int kind = DECAL_HOLE,
                        bool exact = false);   // rides along with it; exact: already on the visible surface
void Decal_RemoveOnVehicle(uint32_t vehicle, int kind);
void Decal_RemoveOnVehicleIn(uint32_t vehicle, int kind, Vec3 lo, Vec3 hi);   // only those inside a box in the car's frame

// ---------------------------------------------------------------- carglass.cpp
// A car's windows told from its paint (by the pieces the game draws it with), and broken.
struct CarHit { Vec3 pos, normal; bool glass; };
// 0 drawn as it is, 1 left out, 2 drawn with *cutIB instead (a list of *cutN triangles: one window gone from it)
int CarGlass_OnDraw(int frame, IDirect3DDevice8* d, const float* world16, const float* view16, uintptr_t vb, UINT stride,
                    uintptr_t ib, UINT base, D3DPRIMITIVETYPE type, UINT mi, UINT nv, UINT si, UINT pc,
                    IDirect3DIndexBuffer8** cutIB, UINT* cutN, DWORD vs, int paletteUploads);
bool CarGlass_Raycast(uint32_t vehicle, Vec3 from, Vec3 dir, float maxT, CarHit* out);   // the car as drawn, along a shot
void CarGlass_GlassHit(uint32_t vehicle, Vec3 pos, Vec3 normal, Vec3 dir);   // a crack; the third breaks it all
void CarGlass_Shatter(uint32_t vehicle, Vec3 from);   // every window, into shards
int CarGlass_State(uint32_t vehicle);   // (diagnostic) -1 not seen, 0 intact, 1 broken
bool CarGlass_Knows(uint32_t vehicle);  // (M5) drawn just now, every piece's shape read: a raycast that misses it went through an opening
// (drive-by M5) one side window: where it is in the car's frame (+X its right, +Y up, +Z forward)
struct CarWindow { int side; float y0, y1, z0, z1; };   // side -1 the left, +1 the right
int CarGlass_WindowState(uint32_t vehicle, const CarWindow& w);   // -2 not drawn lately, -1 no glass there, 0 intact, 1 broken
bool CarGlass_BreakWindow(uint32_t vehicle, const CarWindow& w, Vec3 fromLocal);   // that window alone, struck from fromLocal (the
                                                                                 // car's frame); false: nothing there to break
bool CarGlass_RaycastLocal(uint32_t vehicle, Vec3 fromL, Vec3 dirL, float maxT, CarHit* out);   // (M7) a line in the car's own frame
                                                                                          // against it as drawn; out->pos in that frame
bool CarGlass_BreakPane(uint32_t vehicle, Vec3 fromL, Vec3 dirL, float maxT, Vec3* whereLocal);   // (M7) the pane a shot from inside
                                                                                           // goes out through (false: paint first, or no glass)
void CarGlass_Update(float dt);
void CarGlass_Draw(class Renderer& r);
bool CarGlass_Any();
void CarGlass_Clear();   // a new level
void CarGlass_DebugCmd(const char* cmd, const char* arg);
// cars' tires (tires.cpp)
struct TireHit { uint32_t veh; int wheel; Vec3 pos, normal; };
bool Tires_Raycast(Vec3 from, Vec3 dir, float maxT, TireHit* out);   // the first tire along a shot, before maxT
void Tires_SetIgnore(uint32_t vehicle);   // (drive-by) a car whose tires are never in the way (0 = none): his own
bool Tires_WheelCentre(uint32_t vehicle, int k, Vec3* centre, float* radius);   // (drive-by tests) where wheel k is
void Tires_Shot(uint32_t vehicle, int wheel, Vec3 pos, Vec3 normal);  // it pops and goes flat
void Tires_Blast(uint32_t vehicle, Vec3 centre, float reach);         // the car's tires within reach blow out
void Tires_Update(float dt);   // each frame (game time): going down, how the car drives on them
bool Tires_OnDraw(int frame, const float* world16, const float* view16, float* out16);   // a flat one: drawn squashed (out)
bool Tires_Any();
void Tires_Clear();   // a new level
void Tires_DebugCmd(const char* cmd, const char* arg, uint32_t player);
bool Hit_TakesHole(const HitResult& h);   // buildings, walls and hard ground (not props, cars, people, grass)
void Decal_Track(uint32_t player);         // a new player Character (a new level) clears them
bool Decal_Any();

// ---------------------------------------------------------------- sky.cpp
// The Duff plane over Evergreen Terrace: a shot into the sky along dir may hit it or its banner
// (1 plane, 2 banner, 0 neither); hit, it races round its circle for a while.
int Sky_Shot(Vec3 dir);
void Sky_Update(float dt, uint32_t player);
bool Sky_PlaneDir(int part, Vec3* dir);   // debug: where the plane (0) or banner (1) is from the camera
void Sky_DebugCmd(const char* cmd, const char* arg);

// ---------------------------------------------------------------- react.cpp
// What the thing hit does about it, with the game's own effects and rules, scaled by the
// weapon's spec. Returns true if the game showed an effect.
bool Hit_React(const WeaponSpec& w, const HitResult& h, Vec3 dir);
// The game's own effects (English release; false = not available)
bool Fx_Particle(int type, Vec3 pos, Vec3 out, float bias);  // a ParticleManager effect spraying along out
bool Fx_Sparks(Vec3 pos, Vec3 dir, float strength);         // the streaks cars throw when they scrape
bool React_IsMovable(uint32_t obj);   // a prop the game can push or break, or a wasp camera (not a building)
void React_DebugCmd(const char* cmd, const char* arg, uint32_t player);
int React_ListActors(uint32_t player, float radius, float (*pos)[3], int max);   // debug: wasp cameras nearby
// for explosions: the same reactions, one object at a time
float React_VehicleDamage(uint8_t* vehicle, Vec3 point, float amount, bool wreck);   // hit points left
void React_VehicleShove(uint8_t* vehicle, Vec3 dir, float force, bool heat);
const char* React_PropHit(const WeaponSpec& w, uint32_t obj, Vec3 dir);   // props and wasp cameras
void React_TriggerEvent(int event, void* param);
float React_HeatMeter();
int React_PanicAround(Vec3 at, float r);   // walkers within r of at run away from it (how many)
void React_Update(float dt, bool run);   // each frame (game time): the knocked-down people the mod is the ground for
void React_Clear();                      // a new level or a jump: forget them
float React_LiftNear(const float* view16, Vec3 viewPos);   // a knocked-down flight to draw: how high (0: none)
bool React_Lifting();   // anyone's flight being drawn now
// how far people run from (metres): where a bullet lands, a rocket's blast, a shot fired
static const float kShotPanic = 10.0f, kBlastPanic = 25.0f, kFirePanic = 8.0f;
