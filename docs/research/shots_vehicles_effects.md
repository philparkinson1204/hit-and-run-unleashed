# Simpsons Hit & Run (English Simpsons.exe), static RE: ray/line tests, vehicle damage, impact effects

All addresses are absolute VAs (image base 0x400000). This was static analysis only: nothing was launched, attached to, or modified.
Tooling: pefile + capstone scripts in `scratchpad\re\` (`sharlib.py`, `fieldscan.py`, `evrefs.py`).

Confidence tags: **[H]** high (seen directly in code and cross-checked at several call sites), **[M]** medium (inferred from code but only partly cross-checked), **[L]** low (a guess or naming convention).

> **Calling-convention warning.** The exe was built with MSVC LTCG. Many non-virtual functions use custom
> register conventions: arguments arrive in eax/ecx/edx/ebx/esi/edi and sometimes `this` is a stack argument.
> Every signature below was derived from the callee's prologue/epilogue and checked against at least one game call site.
> Virtual functions are always plain `__thiscall`. For functions marked "custom", use the naked cdecl
> wrappers given in section D, and declare ebx/esi/edi as clobbered or save them.

---

## Key singletons / globals found (beyond the ones you already had)

| What | Address | Notes |
|---|---|---|
| IntersectManager* | `0x6C87A4` | GetInstance `0x4B42A0` (`mov eax,[0x6C87A4]; ret`) [H] |
| the world-physics object | `0x6C84D4` | getter `0x4DC6C0`. `+0x14` = `sim::CollisionManager*`. Object size 0x90 [H] |
| ParticleManager* | `0x6C87A0` | GetInstance `0x4B5FE0` creates it lazily. Size 0x1C. `+4` = 27 (type count), `+8` = per-type pools (12 bytes each) [H] |
| BreakablesManager* | `0x6C879C` | GetInstance `0x4B80C0` (lazy create) [H] |
| SparkleManager* (sparks/stars; name is a guess) | `0x6C844C` | getter `0x506A60`. May be NULL before game init [H on addr, L on name] |
| ray "mode" global | `0x642350` (int) | must be **2** for exact sub-volume tests. Static init is 1 = bounding-box only. Every game ray caller sets it to 2 first [H] |
| ray "thickness" global | `0x65C420` (float) | inflates every collision volume by this radius during ray tests [H] |
| CollisionManager "closest only" flag | `0x65C424` (byte) | used only by `CollisionManager::RayIntersect` [H] |

---

# A. Native raycast / line test

## A.1 What exists in the game

There is **no single native function** that returns position, normal and object for an arbitrary segment against everything.
The building blocks are:

1. **`IntersectionList`** (RTTI `.?AVIntersectionList@@`, vtable `0x61593C`, size 0x48). This is the game's "local collision set plus ray tests".
   It is used by the wasp-camera bullets (`Projectile`), the kick action, and all player cameras (FollowCam, PCCam, ReverseCam, ComedyCam).
   It is filled from the world k-d tree via IntersectManager queries **around any point and radius you choose**, so it works at long range.
2. **`sim` ray-vs-collision-volume** `0x516A60`, a plain `__cdecl` function. This is the primitive used by every ray test in the game. It returns the hit distance and the leaf `CollisionVolume*`.
3. **`sim::CollisionManager::RayIntersect`** `0x518C70`, a thiscall. It tests only objects that are currently inside the sim collision areas, which are populated only near the player and active vehicles. Not recommended as the primary path.
4. **`IntersectManager::FindIntersection`** `0x4B59C0`. A **vertical** ray from y=10000 straight down against terrain triangles (IntersectDSG). It returns ground position, normal and terrain type. It is the only way to hit the ground or roads: terrain triangles are **not** in the IntersectionList or the sim collision volumes.

No native function outputs a surface normal except FindIntersection, which does so for terrain only. For volumes the normal must be computed (see A.5).

## A.2 IntersectionList layout [H]

```
+0x00 vtable 0x61593C
+0x04 vector<sim::CollisionObject*> statics    begin=[+0x08] end=[+0x0C]   (StaticPhysDSG etc.)
+0x14 sorted vector<pair<CollisionObject*,DSG* owner>> dynamics  begin=[+0x18] end=[+0x1C], 8-byte elements
+0x28 vector<CollisionObject*> animPhys          begin=[+0x2C] end=[+0x30]   (AnimCollisionEntityDSG: doors, moving platforms)
+0x38 vector<Fence> fences                       begin=[+0x3C] end=[+0x40], 16-byte elements {x1,z1,x2,z2} (infinitely tall 2-D walls)
size 0x48
```
The add helpers **silently drop** entries beyond a cap: statics, dynamics and anim are capped at 50 (`cmp eax,0x32; jae skip`), fences at 100.
So the ray should be traced in chunks (about 10 to 15 m each) rather than with one huge query sphere.

Fill and clear functions also AddRef the CollisionObjects and owners (vtable slot 0). **Always run the destructor** afterwards, or the references leak.

## A.3 IntersectionList API

| Function | Addr | Convention (verified) | Notes |
|---|---|---|---|
| ctor | `0x41AF80` | `__stdcall IntersectionList* ctor(void* this)`, ret 4 | reserves 50/50/50/100 entries with game `operator new`. Kick code: `lea ecx,[esp+70h]; push ecx; call 41AF80` [H] |
| dtor | `0x41B050` | `__thiscall void dtor(this)` | clears (Release) and frees [H] |
| FillStatics (+fences) | `0x41BDF0` | **`__fastcall int (const Vec3* center /*ecx*/, IntersectionList* list /*edx*/, float radius /*stack*/)`**, ret 4 | calls FillFences, then `IntersectManager::FindStaticPhysElems` (`0x4B4C00`). Returns count [H] |
| FillDynamics | `0x41BF60` | **`__stdcall int (IntersectionList* list, const Vec3* center, float radius, int storeOwner, void* excludeDSG)`**, ret 0x14 | `IntersectManager::FindDynaPhysElems` (`0x4B4DF0`). **storeOwner must be 1** or the owner pointers are NULL. Pass the player Character* as exclude [H] |
| FillAnimPhys (+fences) | `0x41C030` | custom: `ecx=center, edi=list, push radius`, ret 4 | [H] |
| RayStatic | `0x41B240` | custom: `esi=start, ebx=end, edi=list, push Vec3* outPos`, ret 4, returns al | statics and fences. **Hard-codes thickness 1.0 m**, outPos = start + dir·(t+1) [H] |
| RayDynamic | `0x41B490` | custom: `edi=start, eax=end; push outObj(DSG**), push outPos, push list`, ret 0xC, al | dynamics, returns the owner DSG. **Thickness 1.0 m** [H] |
| RayAnimPhys | `0x41B620` | custom: `edi=start, ecx=end; stack (list, Vec3* outPos, float radius)`, ret 0xC | radius 0 is replaced by 1.0 [H] |
| RayAll | `0x41B7B0` | custom: `esi=start; stack (list, const Vec3* end, Vec3* outPos, float fenceOffset, float radius)`, ret 0x14 | dynamics, statics and fences, position only. PCCam calls it with radius 0.2 / 0.2 [H] |
| LineOfSightClear | `0x41BAB0` | custom: `eax=start, esi=list; stack (const Vec3* end, float radius, int staticOnly)`, ret 0xC | returns **1 if NOT blocked**. Used by the cameras [H] |

Evidence: the kick action `0x4F34B0` uses the API end to end.
```
004f35bd lea ecx,[esp+70h] / push ecx / call 41AF80        ; IntersectionList on stack
004f35c7 push ebp / push 1 / push 40A00000h(5.0) / push &pos / push &list / call 41BF60   ; FillDynamics(list,pos,5,1,self)
004f361b lea edi,[start] ... lea eax,[end] / push &obj / push &hitPos / push &list / call 41B490 ; RayDynamic
004f3669 call [edx+50h]  ; GetAIRef on hit object: 1=vehicle, 0x20002=NPC, 0x40002=stateprop
004f3691 call [eax+90h]  ; ApplyForce(dir, [0x6C8380]=400.0)
004f36e7 push 12Ah (OBJECT_KICKED) ... 004f36f2 push 5Ah (KICK)
```
The wasp bullet is `Projectile` vtable slot 4 (Update) = `0x41D530`. It has its own IntersectionList at `this+0x10`.
It fills it with dynamics within 10 m (`0x414960`), runs RayDynamic from prevPos (`+0x68`) to curPos (`+0x80`), then calls `Projectile::HandleHit 0x41D8E0`. Statics are tested via `0x41D770`, which calls RayStatic.

## A.4 sim ray-vs-volume primitive (recommended core) [H]

`0x516A60`: **`__cdecl bool SimRayVolume(const Vec3* start, const Vec3* dirNormalized, const Vec3* delta /*end-start*/, float length, RayInfo* io)`**. The caller cleans the stack (`add esp,14h` at all 11 call sites).

```c
struct RayInfo { float t; void* obj; void* vol; };  // 12 bytes
// in : io->vol = root CollisionVolume* (= *(void**)(co+0x20)); io->obj = co (informational)
// out: returns hit; io->t = distance from start along dir (world units) to the volume inflated by [0x65C420];
//      io->vol = LEAF volume hit (compound BBox volumes recurse into sub-volumes).
//      io->obj is NOT reliable after a compound hit (copied from an uninitialised slot), so keep your own co.
```
Required globals: `*(int*)0x642350 = 2;` (otherwise compound volumes only test their AABB) and `*(float*)0x65C420 = r;` (bullet thickness; use 0.0 to 0.05).
Save and restore both. They are only read by ray code, and every game ray caller rewrites them before use [H].
Start-inside behaviour: the sphere test returns `t=0` (see `0x516260`), so ignore hits with t below about 1e-4, as the game's RayAnimPhys does.
Then filter `0 <= t <= length`.

Owner of any volume (works for statics and anim too) [H]:
```
co    = *(void**)(vol + 0x30)          // the volume's collision object (CollisionVolume +0x30)
ss    = *(void**)(co  + 0x18)          // CollisionObject -> SimState
owner = *(void**)(ss  + 0x08)          // SimState AI-ref pointer (Vehicle*, Character*, StaticPhysDSG*, ...)
aiRef = *(int*)  (ss  + 0x0C)          // same value as owner->vtbl[0x50/4]() (GetAIRef)
```
Evidence: `0x4ED740` does `mov eax,[edx+8]; mov eax,[eax+18h]; cmp [eax+8],edi(vehicle)` and then `mov eax,[eax+0Ch]; cmp eax,10000h ...`.

GetAIRef values (vtable slot 0x50, `mov eax,imm; ret`) [H]:
`1` Vehicle · `0x20001` Character (player) · `0x20002` NPCharacter · `0x10000` StaticPhysDSG/DynaPhysDSG/InstStatPhysDSG · `0x20000` InstDynaPhysDSG and InstAnimDynaPhysDSG (props) · `0x40000` AnimCollisionEntityDSG · `0x40002` StatePropDSG · `0x40003` ActorDSG/ProjectileDSG (wasp cameras etc.) · `4` FenceEntityDSG.

Volume layout (from SHARMemory plus the ray code) [H unless noted]: `+0x08` world centre, `+0x14` AABB half size, `+0x20` bounding radius (the sphere radius for spheres), `+0x24` type (0 Collision, 1 Sphere, 2 Cylinder, 3 OBBox, 4 Wall, 5 BBox-container), `+0x30` CollisionObject*, `+0x34` sub-volume list, `+0x38` "visible/enabled" (byte; 0 means it never hits), `+0x39` updated.
- Cylinder (`0x516D5B`): `+0x4C` axis (Vec3), `+0x58` half length, `+0x5C` radius, `+0x60` flatEnd (0 = capsule).
- OBBox (`0x5168D0`): axes at `+0x4C`, `+0x58`, `+0x64`; half extents `+0x70` (Vec3); `+0x7C` nonzero means world-axis aligned [M].

## A.5 Recommended TraceShot

```
TraceShot(A, B, excludeDSG):
  dir = norm(B-A); L = |B-A|
  best = {NONE, t = L}
  save g642350,g65C420; *(int*)0x642350 = 2; *(float*)0x65C420 = 0.02f
  for each chunk [s, e] of ~12 m along A->B:
      c = (P(s)+P(e))/2; r = (e-s)/2 + 1.0
      ctor(list); FillStatics(&c, list, r); FillDynamics(list, &c, r, 1, excludeDSG); FillAnimPhys(list, &c, r)
      for co in statics/anim:   test(co, owner = via SimState)        -> WORLD unless aiRef says otherwise
      for (co, own) in dynamics: test(co, own)                         -> classify by aiRef
      fences (optional, infinite height): 2-D segment test in XZ, normal = perp of the fence
      dtor(list)
      if best found inside this chunk: break
  restore globals
  terrain: march A->min(best.t) with FindIntersection (below). If terrain is crossed earlier, best = TERRAIN
  hit.pos = A + dir*(t + 0.02); hit.normal (see below); hit.object = owner, hit.kind from aiRef
```
`test(co)`: `vol=*(void**)(co+0x20); if(!vol) skip; RayInfo io={0,co,vol}; if(SimRayVolume(&A,&dir,&delta,L,&io) && io.t>1e-4f && io.t<best.t) best=...`.

Classification: aiRef 1 = **vehicle** (owner is `Vehicle*`, same pointer as VehicleCentral entries); 0x20001/0x20002 = **character** (owner `Character*`); everything else = world/prop. Keep the DSG pointer for reactions.

**Terrain** (`IntersectManager::FindIntersection 0x4B59C0`, custom) [H]:
`edi = const Vec3* point; push Vec3* outGroundPos; push Vec3* outNormal; push bool* found; push IntersectManager*; call` (ret 0x10).
It returns `eax` = terrain info [M]: low bits = `IntersectManager::TerrainType` (Road, Grass, Sand, Gravel, Water, Wood, Metal, Dirt) and bit 0x80 = interior. Character code at `0x4F63EB` splits it exactly this way.
The `this` slot is never read.
- Evidence (call site `0x419460`): `push &A; push &B; push &found; call 4B42A0; push eax; lea edi,[pt]; call 4B59C0`. After the call the caller reads `B.y` as the ground height.
- Trick worth knowing: `0x49A4A0` consumes the callee's saved-edi slot (`ret 4`), so the epilogue does not pop edi. edi still equals the point. Declare edi clobbered anyway.
- Ray origin is (x, 10000, z) going down. It returns the first triangle under (x,z) inside the k-d leaf that contains the 3-D point. The tree also splits on Y, so the query point's height matters near bridges [M].
- March: sample P(t) every `clamp((P.y-groundY)*0.8/max(-dir.y,0.05), 0.5, 8)` m. When `P.y < groundY`, bisect about 6 times. The normal comes from the last call, and the terrain type can drive the particle choice.

**Normals**:
- terrain: from FindIntersection [H].
- sphere: `norm(p-c)`.
- cylinder/capsule: radial `norm((p-c)-dot(p-c,a)a)`, or ±a on flat ends.
- OBB: largest `|dot(p-c,a_i)|/half_i`, take `sign*a_i`.
- BBox leaf: the same with world axes and `+0x14`.
- Robust generic fallback: re-test only the hit volume with 2 extra parallel rays offset 2–3 cm perpendicular to dir, then `n = norm(cross(p1-p0,p2-p0))` flipped against dir. If that fails, use `-dir`.

**Muzzle check**: run the same TraceShot from the muzzle to `hit.pos - dir*0.05`, excluding the player. If it hits anything (world, terrain or dynamic), that becomes the impact.
**Camera trace start**: start at `cam + dir*max(0, dot(playerChest-cam, dir) - 0.5)` so walls behind the player (camera clipping) are not hit.

**Cost** (estimates [L]): each Fill is a k-d tree sphere query plus a bounding-sphere filter, roughly tens of µs. SimRayVolume does an AABB reject and an exact test, so about 1 µs per object, 50–150 objects per chunk. FindIntersection walks every triangle in a leaf, possibly 0.05–0.2 ms per call, so keep it to 10–30 calls. That puts a shot at well under a few ms, which is fine for a pistol.

## A.6 Alternatives and why they rank lower
- **Game wrappers only** (RayAll with radius 0.05 for position, plus RayDynamic for the object): little code, but RayDynamic and RayStatic are **1 m fat rays**, so bullets would "hit" cars you missed by up to a metre. You also get no normal or volume. Acceptable only as a quick prototype.
- **`sim::CollisionManager::RayIntersect 0x518C70`**: `__thiscall(CM* = *(void**)(*(void**)0x6C84D4 + 0x14); ResultList* out, const Vec3* start, const Vec3* end, bool flagFilter, int areaIndex /*-1 = all areas*/)`, ret 0x14.
  It fills `{int count; int cap; Entry* data}` with sorted entries `{float t; CollisionObject*; CollisionVolume*}`. When `[0x65C424]=1` it keeps only the closest hit.
  It only sees objects already in collision areas (near the player and vehicles). The result array can grow on the game heap: callers set the heap, as in `0x4094C0`, which also sets 0x642350=2 and 0x65C420 first. [M]
  Used by CharacterAi::GetOut (`0x4096AD`) and character code `0x4F6CF2`.
- **Characters by direct enumeration** (complement): CharacterManager `+192`[64]. Test a capsule (r≈0.35, h≈1.8) at `owner->vt[0x40](Vec3* out)` (GetPosition, thiscall). Use this if NPC collision volumes ever turn out inactive far from the player (untested [L]).

---

# B. Vehicle damage and destruction

## B.1 The damage function: `Vehicle::TriggerDamage` `0x4EDF90` [H]
Custom convention: **`eax = Vehicle*`; `push int clampToThreshold; push float amount; call`** (ret 8). Returns **float normalised health in ST(0)**, which must be popped.
```
004edf93 mov al,[edi+494h] ; VehicleDestroyed -> return 0.0
004edfa7 mov al,[esp+0Ch]  ; clamp flag
  clamp=1: if Norm(HP)<=0.02 -> HP=0 (destroy). else newHP=max(HP-amount,0);
           if Norm(newHP)<0.02 { if PlayerCar(+31Ch) CharacterSheet.UpdateCarHealth;
                                 HP=0.001; TriggerEvent(VEHICLE_DAMAGED=0x64,this); NoDamageTimer(+49Ch)=1.0; return 0.02 }
  clamp=0: HP -= amount
004ee087 if HP<=0 { HP=0; VehicleDestroyed(+494h)=1; ActivateTriggers(false) 0x4E9080; DamageOutResetTimer(+498h)=0 }
004ee0bc TriggerEvent(VEHICLE_DAMAGED, this); norm=HP/MaxHP; if PlayerCar UpdateCarHealth; return norm
```
- Fields: HitPoints `+0x4A0`, MaxHP (DesignerParams.HitPoints) `+0x2EC` (`GetNormalizedHealth 0x4EDF60`: `fld [esp+4]; fdiv [eax+2ECh]`).
- Clamp=1 is the "near death" stage: the car is held at 0.001 HP and the next damaging hit destroys it.
- **It does not check** VehicleCanSustainDamage (`+0x4A4`) or NoDamageTimer (`+0x49C`). The collision handler checks both before calling, so the mod should too.

Callers [H]:
- vehicle collision handler = **Vehicle vtable slot 19 `0x4ECBA0`**: `TriggerDamage(100,…)` for instant kills, otherwise `0x4ED570` (damage plus per-panel visuals from the contact point, only when NoDamageTimer==0)
- `Vehicle::ApplyForce` (slot 36, see B.4)
- `Projectile::HandleHit` (wasp bullet): **traffic only**, `TriggerDamage(0.1, 1)` (`0x41D942: cmp [edi+0A4h],2`)
- kill volumes `0x41E9A0`: `TriggerDamage(FLT_MAX, 0)` plus an explosion breakable

Car HP from `scripts\cars\*.con` `SetHitPoints`: 0.5 (compactA traffic) to 18, typically 1–5 (famil_v = 3.0, mass 1500).

## B.2 Visual damage (smoke, panels, textures)
- **`SyncVisualDamage` `0x4EDCE0`** (custom: `eax=Vehicle*; push float normHealth`, ret 4) [H]. If health <= 0 it marks the car destroyed (do not call it after TriggerDamage killed the car). Otherwise it switches on DamageType `+0x424`:
  - Traffic (3) → `0x4EDC70`: smoke only. `SetEngineSmoke` (`GeometryVehicle(+0xB4)+0x8C` = particle id): damage>0.6 → EngineSmokeLight 13, ≥0.98 → Medium 16, >0.99 → Heavy 14.
  - AI (2) → `0x4EDB50`: damage>0.1 → damage texture on one panel; >0.4 → all four panels plus LightsOffDueToDamage (`geom+0x1D5`); then the same smoke thresholds.
  - User (1) → `0x4ED880`: the panel joint (hood/trunk/doors `+0x454/+0x458/+0x44C/+0x450`) gets a flapping inertial joint plus textures and smoke.
- Smoke is then emitted every frame natively by the GeometryVehicle particle emitter.

## B.3 Destruction / explosion (native, automatic) [H]
`Vehicle per-frame update 0x4EB050` (esi=vehicle, push dt) is called from `0x4DB6C0`, which loops over **VehicleCentral+0xB4, 30 active vehicles**, for the current render layer.
```
004eb57f if (!VehicleDestroyed || AlreadyPlayedExplosion(+496h)) skip
004eb5a3 0x4EE4B0 (destroyed visuals)
 VehicleType(+0A4h)!=User(0) && !GameplayMgr+8:  DamageOutResetTimer+=dt; explode NOW (unless special game mode -> wait 3.0 s)
 VehicleType==User:                               wait until DamageOutResetTimer > 3.0 s
004eb5f4 push esi; call 4EE280      ; EXPLOSION
004eb5f9 AlreadyPlayedExplosion=1
004eb618 TriggerEvent(VEHICLE_DESTROYED=0x61, v); if WasHitByVehicleType(+478h)!=AI(1) TriggerEvent(VEHICLE_DESTROYED_BY_USER=0x62, v)
```
`Explosion 0x4EE280` (stdcall(Vehicle*)):
1. The world-physics object's `0x4DC7D0(area=v+0x198, radius 20, force 20000, edi=pos)` calls `ApplyForce` on dynamics in the vehicle's collision area, which damages nearby cars too.
2. `BreakablesManager::Play(24, &matrix at wheel centroid)`, the explosion visual.
3. `TriggerEvent(BIG_BOOM_SOUND=0xC3, v)`.

**Husk swap happens in event listeners** for VEHICLE_DESTROYED [H]:
- `TrafficManager::HandleEvent 0x4FF900`: `cmp eax,61h → 0x500A20` husk swap for traffic.
- `ParkedCarManager::HandleEvent 0x508FE0`: parked cars.
- `GameplayManager::HandleEvent 0x449220`: mission/AI cars and the player's car, also fires VEHICLE_DESTROYED_SYNC_SOUND.
- Husks come from the pool `0x4D9900` ("huskA").

So **traffic, parked, AI and player cars can all be destroyed natively** once HP reaches 0. The only requirement is that the car is in VehicleCentral's active list. Traffic is (TrafficManager removes and adds via VehicleCentral `0x4DB180/0x4DB060`). Parked cars are very likely in it too [M].
HitnRunManager (`0x4D8B00` switch) reacts to VEHICLE_DESTROYED_BY_USER but **not** to VEHICLE_DAMAGED, so destroying a car raises Hit & Run while damage alone does not [H].

## B.4 Vehicle::ApplyForce (vtable slot 36, offset 0x90; `__thiscall(const Vec3* dir, float force)`) [H]
`0x4EE3C0`:
- `SetLocomotion(Physics) 0x4E9660`. Traffic switches to physics, fires TRAFFIC_GOT_HIT 0xE6 and notifies TrafficManager.
- `linVel += dir*force/mass(+0x2B8)`, then wake (vt 0x8C).
- `TriggerDamage(v, coef*force/20000, 0)` with coef = `[0x6CBC70]`=0.5 normally, or `[0x6CBC74]`=0.0 if UserDrivingCar (`+0x264`). Both are set in Vehicle init `0x4EF221`.

The same slot is `DynaPhysDSG::ApplyForce 0x49FBA0` for props (pushes, mass from CollisionAttributes) and NPCs (`0x4F84A0`). The player Character's slot is a no-op.

## B.5 Safest way to apply N damage from the game thread
```c
void ShootVehicle(uint8_t* v, float dmg, const Vec3* dir) {
    if (!v || v[0x494] /*destroyed*/) return;
    if (!v[0x4A4]) return;                       // VehicleCanSustainDamage false = invulnerable (mission rules)
    // optional nudge + traffic "got hit" reaction. Adds tiny damage 0.5*F/20000:
    // ((void(__thiscall*)(void*,const Vec3*,float))(*(void***)v)[36])(v, dir, 300.f);
    float h = Veh_TriggerDamage(v, dmg, 1);      // clamp=1 gives a 'near death' stage before destruction
    if (h > 0.0f) Veh_SyncVisualDamage(v, h);    // smoke, panels, textures by DamageType
    // destruction, explosion, husk, events all follow natively next frame(s)
}
```
Suggested pistol damage: `dmg = maxHP * 0.08f` (maxHP = `*(float*)(v+0x2EC)`), about 12 hits per car. Fixed values destroy a compact (0.5 HP) in very few shots.
Optional: set `WasHitByVehicle(+0x474)=1` and `WasHitByVehicleType(+0x478)=0` to attribute the kill to the player. The default (3) already fires BY_USER.
Differences:
- Player car: clamp gives a 1 s NoDamageTimer, the CharacterSheet health is updated (it persists), and it explodes after 3 s.
- AI/mission cars: DamageType AI visuals. Mission rules may set VehicleCanSustainDamage=0.
- Traffic: smoke only, explodes immediately, husk via TrafficManager.

---

# C. Native impact effects

## C.1 ParticleManager (per-type pooled particle systems) [H]
- **`ParticleManager::Add(pos)` `0x4B6610`: `__stdcall void(ParticleManager* pm, const ParticleAttributes* a, const Vec3* pos)`**, ret 0xC.
  Get pm from `0x4B5FE0()` (no args, returns eax) or read `[0x6C87A0]`.
  `ParticleAttributes = { int type; float emissionBias /*1.0*/; Vec3 velocity /*unused here*/ }` (20 bytes).
  Game use: `StaticPhysDSG` collision handler `0x4A59B0`, `push &contact; push &attr; call 4B5FE0; push eax; call 4B6610`.
- **`ParticleManager::Add(matrix)` `0x4B6520`** (custom: `eax = const Matrix4x4*; push attr; push pm`, ret 8). Use it to orient the effect, for example with the normal as up axis. Used by `InstDynaPhysDSG::OnCollision 0x4A0B50`.
- Types are `ParticleEnums` (0..26; pm+4 = 27, and the array is not bounds-checked, so validate). Type 8 (Stars) is redirected to SparkleManager stars.
  A type with no loaded systems is a no-op. One-shot systems release themselves when done (`0x4B6650` update).
- Loaded per level from `art\l0X_fx.p3d`: dirt_spray, spray_grass, garbage, mail, shrub, oaktree, FireSpray, smoke_spray, explosion. The wheel and engine emitters always need 9–14, 16, 19 [M].
- Suggested impacts:
  - Road/Wood/Metal → SmokeSpray 9
  - Dirt/Sand/Gravel → DirtSpray 10
  - Grass → GrassSpray 11
  - Water → WaterSpray 12
  - Props/statics → their own particle `*(int*)(*(void**)(dsg+0x30) + 0x48)` (CollisionAttributes::particle; -1 = none), e.g. Shrub, Garbage, Mail

## C.2 Sparks (what cars throw on impacts and grinding) [H]
- **`AddSparks` `0x506FD0`** (custom: `eax = SparkleManager* = [0x6C844C]`; `push float strength; push const Vec3* dir; push const Vec3* pos`, ret 0xC). It spawns `rand(10..20)*strength+1` streaks with random velocity plus `dir`.
  Used by vehicle-vs-vehicle and vehicle-vs-prop impacts (`0x4ED86F`, strength = impulse·0.0002, so about 0.1–1) and wheel grinding on road/metal/gravel (`0x4EB172`, strength 0.1).
  Good for bullet hits on cars and metal: `strength 0.3–0.6`, `dir = normal`.
- **`AddStars` `0x5076A0`** (custom: `eax=SM; push float strength; push const Vec3* pos`, ret 8). Used for fence impacts and ParticleEnums Stars (8). Visual identity (star vs flash) is unconfirmed [M].
- Note: car-vs-building (aiRef 0x10000) impacts spawn **no** effect in `0x4ED740`.

## C.3 Bigger effects
- `BreakablesManager::Play` `0x4B87D0`: `__thiscall(BM* = 0x4B80C0(), int breakableType, const Matrix4x4* m)`, ret 8. Fires HIT_BREAKABLE.
  Type 24 = car explosion (used by `0x4EE280` and `0x41E9A0`). Other types from table `0x605020` (eHydrantBreaking=3, eOakTreeBreaking=15, …). Only types loaded in the level work.
- Breaking a breakable prop natively: `InstDynaPhysDSG` vtable slot 0x94 (`0x4A0C40`, thiscall, no args, returns al). Call it only if the vtable is `0x60CC54` (InstDynaPhysDSG). It plays its breakable and removes the prop. This is what the kick code does after ApplyForce (via dynamic_cast).

---

# D. Native reaction helpers (for the HitResult → reaction stage)
- `owner->vt[0x50/4]()` GetAIRef; `vt[0x40/4](Vec3* out)` GetPosition; `vt[0x58/4]()` GetSimState (`[this+0x70]`, Vehicle `[+0x4A8]`).
- **`vt[0x90/4](const Vec3* dir, float force)` ApplyForce**: props fly, NPCs get knocked, vehicles get a nudge plus small damage, the player gets nothing. The wasp bullet uses force 400 on props; the kick uses 400.
- NPC kick-style reaction: vt[0xD8] (`0x4F84C0`). `Character::Shock(0.5)` `0x4F7A60` (custom `esi=Character*, push float secs`) is what the wasp bullet does to the player: it fires WASP_BULLET_HIT_CHARACTER_STYLIZED_VIOLENCE_FOLLOWS(0) and KICK_NPC_SOUND.
- Events (numbers verified against code usage): KICK 0x5A, VEHICLE_DESTROYED 0x61, …_BY_USER 0x62, …_SYNC_SOUND 0x63, VEHICLE_DAMAGED 0x64, HIT_BREAKABLE 0xC7, BIG_BOOM_SOUND 0xC3, TRAFFIC_GOT_HIT 0xE6, WASP_BULLET_HIT_CHARACTER… 0xFD, OBJECT_KICKED 0x12A, CAMERA_SHAKE 0x12C.

## D.1 Wrappers (clang/llvm-mingw i686)
```c
typedef struct { float x,y,z; } Vec3;
typedef struct { float t; void* obj; void* vol; } RayInfo;
#define TC __attribute__((thiscall))
static void* (__stdcall *IL_Ctor)(void*)                                  = (void*)0x41AF80;
static void  (TC        *IL_Dtor)(void*)                                  = (void*)0x41B050;
static int   (__fastcall*IL_FillStatics)(const Vec3*, void*, float)       = (void*)0x41BDF0;
static int   (__stdcall *IL_FillDynamics)(void*, const Vec3*, float, int, void*) = (void*)0x41BF60;
static unsigned char (__cdecl *SimRayVolume)(const Vec3*, const Vec3*, const Vec3*, float, RayInfo*) = (void*)0x516A60;
static void  (__stdcall *PM_AddPos)(void*, const void* attr, const Vec3*) = (void*)0x4B6610;
static void* (__cdecl   *PM_GetInstance)(void)                            = (void*)0x4B5FE0;

// int IL_FillAnimPhys(void* list, const Vec3* c, float r)   ecx=c, edi=list, push r
__attribute__((naked)) int IL_FillAnimPhys(void* l, const Vec3* c, float r) { __asm__ volatile(
 "push %ebx\n push %esi\n push %edi\n push %ebp\n"
 "mov 20(%esp),%edi\n mov 24(%esp),%ecx\n pushl 28(%esp)\n"
 "mov $0x41C030,%eax\n call *%eax\n pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n"); }

// int IM_FindIntersection(const Vec3* pt, unsigned char* found, Vec3* nrm, Vec3* gpos)
__attribute__((naked)) int IM_FindIntersection(const Vec3* p, unsigned char* f, Vec3* n, Vec3* g) { __asm__ volatile(
 "push %ebx\n push %esi\n push %edi\n push %ebp\n"
 "mov 20(%esp),%edi\n pushl 32(%esp)\n pushl 32(%esp)\n pushl 32(%esp)\n pushl 0x6C87A4\n"
 "mov $0x4B59C0,%eax\n call *%eax\n pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n"); }

// float Veh_TriggerDamage(void* v, float amount, int clamp)   eax=v; push clamp; push amount; ST0 result
__attribute__((naked)) float Veh_TriggerDamage(void* v, float a, int c) { __asm__ volatile(
 "push %ebx\n push %esi\n push %edi\n push %ebp\n"
 "mov 20(%esp),%eax\n pushl 28(%esp)\n pushl 28(%esp)\n"
 "mov $0x4EDF90,%ecx\n call *%ecx\n pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n"); }

// void Veh_SyncVisualDamage(void* v, float normHealth)  eax=v; push h
__attribute__((naked)) void Veh_SyncVisualDamage(void* v, float h) { __asm__ volatile(
 "push %ebx\n push %esi\n push %edi\n push %ebp\n"
 "mov 20(%esp),%eax\n pushl 24(%esp)\n"
 "mov $0x4EDCE0,%ecx\n call *%ecx\n pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n"); }

// void Fx_AddSparks(const Vec3* pos, const Vec3* dir, float strength)  eax=[0x6C844C]
__attribute__((naked)) void Fx_AddSparks(const Vec3* p, const Vec3* d, float s) { __asm__ volatile(
 "push %ebx\n push %esi\n push %edi\n push %ebp\n"
 "mov 0x6C844C,%eax\n test %eax,%eax\n jz 1f\n"
 "pushl 28(%esp)\n pushl 28(%esp)\n pushl 28(%esp)\n"
 "mov $0x506FD0,%ecx\n call *%ecx\n"
 "1: pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n"); }
```
(Offset arithmetic: after the 4 register pushes, `20(%esp)` is arg1 and each later `pushl N(%esp)` shifts the remaining arguments by 4. The pushes are ordered so the callee sees its arguments in the documented order. The callees pop their own stack arguments, so these wrappers act as cdecl to C callers.)
Check `x87` usage: all these game functions use x87 and expect an empty FPU stack (true when called from C). `Veh_TriggerDamage` returns in ST0, which matches the cdecl float return.

## Open items / risks
- NPC and vehicle collision volumes are assumed to be kept current while they are in the k-d tree. This is supported by the kick and wasp code handling those aiRefs, but there is no runtime proof [M]. Use the CharacterManager capsule fallback if needed.
- Parked cars being in VehicleCentral's active list [M]. If a shot-to-0-HP parked car never explodes, check this.
- Fences are infinitely tall in the game's model, so consider ignoring them for the camera trace.
- `AddStars` versus `AddSparks` naming is inferred from usage, not from art [M].
