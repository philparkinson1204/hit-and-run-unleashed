# How it works

> Written during development. Where it mentions the lab's test tools (`tools/lab/...`) or the research scripts that
> measured the characters (`assets/build_charshape.py`, `research/...`), those aren't part of this repository; the numbers
> they produced are in `src/`.

The mod is one 32-bit DLL, `d3d8.dll`, placed in the game folder. Everything it knows about the
game comes from reverse engineering, mostly with the tools in `tools/`. Addresses are for the
**English release**; game-state addresses for the other releases come from SHARMemory and are in
`src/game.cpp`.

## Loading

The game's graphics driver, `pddidx8r.dll`, imports `Direct3DCreate8` from `d3d8.dll`. Windows
looks in the game folder first, so our DLL loads, loads the real `System32\d3d8.dll` (SysWOW64),
and forwards all five exports. Inside `Simpsons.exe` only, it then:

| Hook | How | Why |
|---|---|---|
| `IDirect3D8::CreateDevice` | vtable | get the device |
| device `Reset` / `Present` / `CreateAdditionalSwapChain` | vtable | device lifetime, frame end |
| `IDirect3DSwapChain8::Present` | vtable (slot 3) | **the real frame end**: Pure3D renders through a 1×1 device plus an additional swap chain for the window |
| `BeginScene`, `EndScene`, `Clear`, `SetRenderTarget`, `SetTransform`, `SetVertexShader`, `SetVertexShaderConstant`, `DrawPrimitive`, `DrawIndexedPrimitive` | vtable | camera override, skin swap, world-done callback, debug probe |
| `QueryPerformanceCounter`, `GetTickCount` | import table of `Simpsons.exe` | slow motion |
| `XInputGetState`, `XInputGetStateEx` (ordinal 100) in `xinput1_4.dll` | hot-patch | hide the pad from the game |
| `XInputSetState` | called directly | rumble |
| `DirectSoundCreate8` (dsound.dll) | loaded at runtime | the sound mixer |

The Mod Launcher also loads `d3d8.dll` (to list anti-aliasing modes); in any other process the DLL
is a plain pass-through.

**The Mod Launcher's Direct3D 9 hack skips us.** The launcher's `Hacks.dll` overwrites pddidx8r's
import thunk for `Direct3DCreate8` (`jmp [IAT]` at pddidx8r+0x36432) with a jump into its own
code, in both modes. In normal mode it passes the call on to the import (our proxy). In
Direct3D 9 mode it builds its own Direct3D 8 interface on top of Direct3D 9
(`CD3D9HookedDirect3D8`) and never calls ours, so none of our hooks are installed and no log is
written. In that mode `d3d9.dll` is loaded and the system `d3d8.dll` isn't; in normal mode it's
the other way round.

**Only some DLLs can be proxied.** The game loads these by bare name, so a copy in the game folder
wins: `pddidx8r.dll` and `binkw32.dll` (both already in the game folder), `d3d8.dll` (imported by
pddidx8r) and `winmm.dll` (imported by binkw32). The launcher's `Hacks.dll` also loads
`xinput1_4.dll` by name when its XInput hack is on. `dinput8.dll` and `dsound.dll` are created
through COM (`CoCreateInstance`), which loads them by the full path from the registry, so a copy
in the game folder is ignored (tested with an unmodified copy of `dinput8.dll`).

## Source map

| File | What it does |
|---|---|
| `main.cpp` | proxy exports, ini loading (`Config_Load`, button names), device + present hooks, log |
| `hooks.cpp` | import-table patch, Microsoft hot-patch, vtable patch |
| `timescale.cpp` | virtual clock for slow motion (QPC/GetTickCount run at the chosen speed) |
| `input.cpp` | XInput filter: full block while the wheel is open; while armed, hide our buttons, triggers, sticks; renumber packets; rumble; no-pad polling once a second |
| `game.cpp` | all game memory access: state gate, poser read, render camera override/restore, follow-camera heading, facing, idle-timer hold, walking-action check, scene lights |
| `scene.cpp` | device draw hooks: once-per-frame pre-render (camera + pose), skin palette swap with a fresh pose per upload, "world drawn" callback at the depth-only clear, draw-call probe |
| `wheel.cpp` | the wheel: open/close rules, controls, animations, layout, slot 1 options, equip, sounds, hint bar, drawing order, depth-probe glue |
| `gun.cpp` | the weapons in his hands: which one is out (`WeaponId`), the pistol and rocket launcher models and lighting, equip/aim/fire state, stick shaping, aim camera rig, the handgun's aiming pose and the launcher's (IK, torso, both arms) + skin replacement, recoil, muzzle effects and impacts, drawing rockets, trails and explosions, crosshair, equip hint, debug commands |
| `hit.h` | the hit system's types: `HitResult`, `WeaponSpec`, `ShotOrigin` |
| `trace.cpp` | `TraceShot`: a segment against the game's own collision (buildings, props, vehicles, characters, the ground) |
| `weapon.cpp` | `FireWeapon`: the aim trace and the bullet's own path for any weapon; hit debug commands |
| `react.cpp` | `Hit_React`: reactions to a hit with the game's own effects (world: particles, sparks); `fx`, `sparks`, `reactlog` |
| `rocket.cpp` | the rocket launcher's rocket: a projectile flying through the game's collision, its smoke trail, when it's kept, frozen or taken away |
| `explode.cpp` | explosions: the game's car explosion effect, sound and camera shake, what the blast reaches (line of sight) and how each thing reacts |
| `decal.cpp` | bullet holes and scorch marks: stored, put on the visible surface with the depth probe, drawn in the 3D world |
| `carglass.cpp` | cars as drawn: their pieces (glass told from paint by texture), shots traced against their triangles, cracks, broken windows and shards |
| `tires.cpp` | cars' tires: shots against the wheels, popping, going flat through the game's suspension, how a car drives on flats, the flat tire drawn squashed |
| `sky.cpp` | the Duff plane over Level 1: hit test against the sky's plane and banner, and its throttle (draw hook) |
| `shotgun.inc` | the pump-action shotgun: model, draw out of his pocket, carry, shouldered aim, firing, recoil, pump, shells |
| `face.inc` / `charshape.inc` | his face and body in numbers (eyes, skin colour, torso depth) and the wink |
| `vcover.cpp` | visual cover: the depth buffer read along the next shot's pellet rays; drawn surfaces the collision misses stop a shot |
| `lab.cpp` | the Weapons Lab's test hooks (Debug=1 only): warp, coverage scan, hit/ray/statics/volume dumps (tools/lab) |
| `render.cpp/.h` | small D3D8 sprite/text renderer: atlas, blur, capsules, fonts, hint bars, 3D meshes, depth-tested sprites, the depth probe, screenshots, full state save/restore |
| `sound.cpp` | DirectSound 8 mixer: 4 voices per sound with volume/pitch/pan; `PlaySound` fallback |
| `debug.cpp` | `debug_cmd.txt` reader (only with `Debug=1`) |
| `math3d.h` | row-vector math matching D3D/Pure3D |

## One frame

```
BeginScene  ── first one of the frame:
              Gun_PreRenderCamera: aim point (probe distance), write the aiming camera
              Gun_PreRender:      read the animated skeleton, build the aiming pose
game update (the game updates the skeleton and its camera after our BeginScene)
Clear(target + depth)
draw 3D world        ── bone uploads (SetVertexShaderConstant c17+) are collected; at each skinned
                        draw near the player: re-read the pose/view, rebuild if they moved, work
                        out which joint each slot is and re-upload ours (Gun_SkinRefresh +
                        Gun_SkinResolve)
Clear(depth only)    ── just before it: Wheel_OnWorldDrawn -> depth probe under the crosshair,
                        then Gun_Draw (gun mesh + depth-tested smoke/flash/tracer/impacts)
draw HUD
EndScene
SwapChain::Present   ── Wheel_OnPresent: read the probe, the pad and game state; update wheel and
                        gun, rumble, slow-mo; draw crosshair/hints, blur + wheel;
                        then Game_RestoreCamera
```

## Game state gate

The wheel and gun are allowed only in **free roam on foot** (`Game_Read`):

- `GameFlow` (`0x6C9014`): `+0x8` current context and `+0xC` next context must both be 9 (normal
  in-game).
- `InputManager` (`0x6C9008`): `+0x1C0C` must be 1 (gameplay, after pause) or `0xFFFFFFFF` (all,
  after loading). It is 2 in menus and 0x10 during camera animations.
- `CharacterManager` (`0x6C8470`): `+192` is the player Character; `+0x128` InCar must be 0.

## The player

| Address | Meaning |
|---|---|
| `Character+0x64` | position (world, feet) |
| `Character+0x108` | `choreo::Puppet` → `+0x10` Engine → `+0xC` `poser::PoseEngine` |
| `Character+0x110` | `mfDesiredDir`. Written at Present, the character turns to it. Heading is `atan2(x, z)`; characters face −Z, so pass heading + π. Any value works (the game converts it through a direction vector) |
| `Character+0x10C` | called "FacingDir" in SHARMemory but dead: only zeroed in the constructor. The real facing is −Z of the root joint |
| `Character+0x140` | `ActionController`: `+0x4` sequencer, its current action at `+0x14` |
| `Character+0x2A4` | `WalkerLocomotionAction` (vtable `0x61620C`): `+0x10` idle seconds, `+0x14` when the bored fidget starts (10 + 10·rand s) |
| `PoseEngine+0x8` | a `tPose` **shared by every character**: the game re-points its skeleton (`+0x10`) at each character as it draws them. Never read it outside that character's draw |
| `PoseEngine+0xC` | `poser::Pose` (his own): `+0x8` his `tSkeleton` (joint count at `+0x14`, 33 for Homer), `+0xC` joints of 192 bytes: `+0x14` local matrix, `+0x68` world matrix |
| `tSkeleton+0x18` | joints of `0xF0` bytes: `+0x8` parent, `+0x50` rest (bind) world matrix |

The inverse bind matrices are cached, keyed on the skeleton and joint array pointers, the player, both
joint counts and joint 26's bind matrix, because a level or costume change can reuse the address.

Joint order is the same in all 113 character models: 14 Spine_1, 15 Spine_2, 16 Neck, 17 Head,
19–25 left arm, **26 Shoulder_R, 27 Elbow_R, 28 Wrist_R**, 29/30 Middle_R, 31/32 Thumb_R. Some
costumes add joints after 32 (hair, buttons, wings, a tail).

### How the game turns him

From the disassembly (`Simpsons.exe` English):
- While the walking action runs, `facing += 10·dt·AngleDiff(facing, desired)`: first order,
  always the short way. The rate is 2/s with turbo (dash) and 4/s in the air.
- A full-stick turn while aiming therefore lags about 0.25 rad behind the aim.
- **Any other action stops turning entirely:** a jump, kick, getting up, walking to a car door,
  or a bored fidget (`PlayIdleAnimationAction`). Only stick movement ends a fidget, and aiming
  hides the stick.
  - So the mod holds the idle timer at 0 while the gun is out (`Game_HoldIdleTimer`).
  - It only writes the desired direction while the walking action is the current one
    (`Game_WalkerActive`).

## The aiming pose and the skin swap

Writing the pose data does nothing visible. The skinned mesh is drawn from bone matrices the game
uploads as vertex shader constants at draw time (`c17` onwards, 3 registers per bone, the columns
of a 4×3 matrix):

```
palette[bone] = inverse(bind world[bone]) × world[bone] × view
```

**`BuildPose` (gun.cpp)** rebuilds the skeleton from the game's animated locals every time it's
skinned. It only *computes*; every spring is stepped once a frame in `Gun_Update`.
0. **Low ready** (gun out, any time). The right arm's animated locals are blended (by
   `show × carry weight`) toward a fixed hold: upper arm down and a little out, forearm forward
   and down, barrel forward-down. Two-bone IK, wrist frame from the barrel. The weight drops to
   about half while running. Everything below starts from this.
1. **Where the aim is.** The aim relative to his hips (`rel`) and its pitch.
2. **Chest.** Twists toward the aim (up to 0.7 rad, minus 0.25 for a bladed one-hand stance) and
   leans 35 % of the pitch. Spine_1 takes 35 % and Spine_2 65 %, each a world rotation about the
   joint itself.
3. **Head.** Looks at the target: 80 % of the remaining heading and 60 % of the remaining pitch,
   soft-limited.
4. **Left arm.** Two-bone IK to a relaxed hang beside his belly.
5. **Right arm.** Two-bone IK:
   - The wrist goes on the line from the shoulder to the aim point, at 98.5 % reach (about a
     20° elbow bend), soft-limited in front of the chest.
   - The elbow hinge points down and slightly out. The wrist is set so the barrel lies on the
     aim line, top up, canted 0.25 rad inward.
   - Each joint is blended from the animated local by `aim × reach`.
   - **Reach** fades the pose back when the aim is out of the arm's range relative to the
     chest, e.g. still behind him.
6. **Fist.** The finger and thumb joints curl around the grip.
7. **Recoil.** Four springs: muzzle flip, arm lift, elbow fold, chest lean back.

**The swap.** `SkinPrepare` computes the matrix the game *will* upload for every joint
("expected") and a replacement for every posed joint *and its whole subtree*. The bind pose and
parents come from his own poser skeleton. The game uploads **one bone per
`SetVertexShaderConstant` call** and draws Homer in three pieces with their own palettes (14,
24 and 1 bones). `scene.cpp` collects the uploads and, at the draw, hands the piece to
`Gun_SkinResolve`, then re-uploads the replaced slots:
- Before matching, `Gun_SkinRefresh` re-reads the joint array (only for pieces within 1.5 m of
  one of his joints) and the camera, and rebuilds if either moved since the last build: the game
  updates both *after* our BeginScene. Stale matches made the arm flicker.
- Each slot's candidates are the joints whose expected matrix is within 0.03; true matches are
  within ~1e-4.
- Ties are real: a child at its bind pose relative to its parent (a relaxed finger, a straight
  elbow) uploads exactly its parent's matrix. Each piece, keyed by its vertex/index buffers and
  draw range, keeps a map of slot → joint, learned whenever exactly one joint fits (within
  0.002). The map settles ties; then "a joint appears once per piece"; then the closest match.
- Other characters' pieces never match and never touch the maps.

The gun is drawn on our wrist matrix: grip offset (−0.100, −0.035, 0), barrel along −X of the
wrist, scale 1.65.

## The aiming camera

The render camera is a `tPointCamera`: `RenderManager (0x6C87B4) → +0x24 (world layer) → +0xC tView
→ +0x18`.

| Offset | Field |
|---|---|
| `+0x10` | horizontal field of view |
| `+0x14` | aspect |
| `+0x18` / `+0x1C` | near / far |
| `+0x48` | view matrix |
| `+0x88` | camera→world matrix |
| `+0xCC` | position |
| `+0xD8` | target |

- **Override** (first BeginScene of a frame): save `+0x10..+0xE4`, then write field of view, view,
  camera→world, position and target.
  - Drawing uses only the matrices.
  - **Position and target** feed a character view-cone cull, and a check that hides the player
    when the camera is near him. So the position is written **1.5 m further back** along the view;
    the cone still covers the picture and a low camera never trips the hide.
- **Restore** (after the swap-chain Present): the game updates this camera *during* the frame,
  after our BeginScene. So only the 4-byte words that still hold our values are put back;
  anything the game wrote since is kept. Restoring the whole block froze the game camera for the
  entire aim.
- **Placement:** heading and pitch come from the right stick.
  - A pivot beside his head, with side offset, height and distance that depend on pitch:

    | Pitch | Distance | Side | Height |
    |---|---|---|---|
    | down (−0.55) | 2.05 m | 0.70 m | 1.62 m |
    | level | 2.55 m | 0.78 m | 1.50 m |
    | up (+0.65) | 1.90 m | 0.72 m | 1.38 m |

  - Looking up, the camera slides in along the view rather than dipping under his feet.
  - The feet height is damped while aiming.
  - The field of view narrows by 14 %.
  - The view may lead his body by about 0.75 rad at most.
  - Each shot adds a pitch/yaw kick and a field-of-view punch (springs).
- **Walls** (`AimCameraWorld`), done the way the game's own follow camera does it:
  - A sphere of 0.25 m is swept with the game's camera line test from his head out to the
    shoulder pivot, and from the pivot back to the camera (`Game_CameraSweep`, a binary search
    of 7 steps on the free distance).
  - Blocked: the pivot / camera come in at once. Clear again: they ease back out (rate 3.5/s).
    The camera comes as close as 0.45 m behind the pivot, and when it is squeezed in the
    shoulder offset tightens (down to 60 %) so he stays in the picture.
  - While aiming, the near plane goes from the game's 1 m down to 0.25 m (blended with the
    aim), so a close camera doesn't cut into him or into the wall. The depth buffer is D24X8,
    so depth steps stay about 1 cm at 200 m.
  - The ground isn't part of the test; looking up, the camera already slides in along the view.
- **Blend:** a critically damped spring eases between the game camera and the aim camera
  (rotation by quaternion slerp, position linearly). Hip shots raise the arm but leave the camera
  alone.
- **Overlay maths:** the aim point, gun and debug markers use the exact camera the frame was drawn
  with, never a re-read of the live object.

## The follow camera

`SuperCamCentral` is at `*0x6C906C`; `+0x8C` is the active SuperCam (`+0x0C..+0x44` lists all 16).
The on-foot one is a **WalkerCam** (vtable `0x614C8C`):

| Offset | Field |
|---|---|
| `+0x144` | target |
| `+0x15C` | position (each followed by a velocity) |
| `+0xC4` | unit direction from the camera to the target |

Swinging the position around the target (`Game_SetFollowCamHeading`) turns it, and it stays
there. This is done every frame while aiming, so the camera you get back when you let go is
already behind Homer. It is only swung where the game's camera could be itself: the same
sweep must find at least 2.7 m of room, or it's left where the game has it. (The game's camera
collision only pushes out of what it touches, so a camera swung behind a wall stays there, and
closer than 2.5 m to its target it flips round.)

**How the game keeps its follow camera out of walls** (SuperCamCentral = `*0x6C906C`):
1. **A physics sphere** (SCC+0xC4 / +0xC8) placed at the camera every physics step. Its contacts
   become push-out offsets (SCC+0xD4, count at +0x188, ground offset at +0x18C), which the
   WalkerCam applies in `UpdateForPhysics` (vtable +0x10, 0x42B180).
2. **Line tests** against a list of nearby colliders that SCC refills every frame around the
   player (SCC+0x1A8, an `IntersectionList`, vtable 0x61593C; radius = the camera distance,
   WalkerCam+0x134; the player is left out): resets when the camera is too far or too low,
   and four "whisker" probes that turn or raise the camera away from walls while the stick is
   idle.
3. **Too close:** under near plane + 1.5 m (2.5 m) the WalkerCam jumps to the other side of
   the player.

The line test is `IntersectionList::LineOfSight` (0x41BAB0): start in EAX, the list in ESI,
then end, sphere radius and "statics only" on the stack (it pops 12 bytes); AL = 1 if clear.
It tests statics, dynamics, animated colliders and fences (2D segments), not the ground. The
game also hides the player when the camera is within near + 1.5 m of his bounding sphere
centre (0x4FE560), which is why the override writes the position 1.5 m further back.

Feeding a fake right-stick X also turns it (stick right = heading down, about 2.3 rad/s), but only
up to a limit.

## The hit system

Every weapon goes through the same chain: **FireWeapon → TraceShot → HitResult → reaction**
(`hit.h`). English release only; elsewhere `TraceShot` returns nothing.

**TraceShot** (`trace.cpp`) tests a segment against the game's own collision, with the pieces
the game's kick, wasp-camera bolts and cameras use (all checked in the disassembly against those
call sites):

| What | Address | How it's called |
|---|---|---|
| `IntersectionList` (0x48 bytes, vtable 0x61593C) constructor / destructor | 0x41AF80 / 0x41B050 | stdcall(this) / thiscall |
| fill: buildings and walls (statics, + fences) | 0x41BDF0 | fastcall: ECX centre, EDX list, radius on the stack |
| fill: cars, people, props (dynamics) with their owners | 0x41BF60 | stdcall(list, centre, radius, keepOwners = 1, skip = the player) |
| fill: animated pieces (doors) | 0x41C030 | ECX centre, EDI list, radius on the stack |
| ray against one collision object, exact sub-volumes | 0x516A60 | cdecl(start, unit dir, end − start, length, {t, obj, vol}) |
| ray mode / bullet radius globals (save, set 2 / 0.02, restore) | 0x642350 / 0x65C420 | |
| the ground under a point (terrain type, height, normal) | 0x4B59C0 | EDI point; stack IntersectManager (0x6C87A4), bool* found, normal*, ground point* |

- The list keeps at most 50 objects of each kind per fill, so the segment is done in **10 m
  pieces** (a full list splits its piece in half). Each object is tested against the whole
  segment and the closest hit kept; the walk stops once a hit lies within the pieces done.
- List layout: statics `CollisionObject*` at +0x08..+0x0C, dynamics `{CollisionObject*, owner}`
  at +0x18..+0x1C, animated at +0x2C..+0x30 (begin..end). A `CollisionObject`'s root volume is
  at +0x20 and its `SimState` at +0x18; the `SimState` holds the object at +0x08 and its AI
  reference at +0x0C: **1 vehicle, 0x20001 player, 0x20002 pedestrian**, 0x10000 buildings and
  static props, 0x20000 / 0x40002 moving props, 0x40000 animated pieces.
- **The ground** (roads, grass...) is not a collision object. The segment is walked in steps
  sized by its height above the ground (0.5 to 8 m), the crossing is bisected 8 times and put
  on the ground's plane. The terrain type is in the low bits (0 road, 1 grass, 2 sand, 3 gravel,
  4 water, 5 wood, 6 metal, 7 dirt; the order is from the research, road and grass were seen
  in the game), 0x80 = indoors.
- **Normals** from the leaf volume hit: sphere (type 1) radial; cylinder or capsule (2) radial
  from its axis or its flat end; oriented box (3) the face whose axis the point lies furthest
  along (axes +0x4C/+0x58/+0x64, half sizes +0x70); walls and plain boxes from their bounding
  box (+0x14). Always turned to face back along the shot.
- A trace costs 0.04 to 0.2 ms. The list's storage comes from the game's `operator new`, which at
  Present is heap 8 (plain `malloc`/`free` on PC); it's freed before returning.

**A weapon** is a `WeaponSpec` (data) plus where its shot starts (`ShotOrigin`: shooter, a point
inside him, muzzle, barrel direction, and for aimed shots the camera and its centre ray).

| WeaponSpec field | Meaning | Handgun |
|---|---|---|
| `range`, `radius` | how far, how thick the traces are | 70 m, 0.02 m |
| `pellets`, `spread` | rays per shot, the cone they scatter in | 1, 0 |
| `vehicleDamage`, `vehicleForce` | share of a car's hit points per pellet, its shove | 8 %, 400 |
| `characterForce`, `knockAngle` | the knock-down push and how far up it points (also props) | 500, 45 degrees |
| `propForce`, `breaks` | the push on loose props, whether breakables break | 400, yes |
| `heat` | raises Hit & Run (the game's own amounts) | yes |
| `holeSize` | bullet holes on buildings, walls, roads (metres across; 0 = none) | 0.16 |
| `fx` (`ImpactFx`) | emission bias of dust, dirt, grass, water, a prop's own particle; spark strength off hard things and cars | 0.4, 0.6, 1, 1, 1; 0.25, 0.5 |
| `blast` | an explosion's radius (0 = a bullet: only what it hits reacts) | 0 |

The rocket launcher's spec (`kRocket`): range 150 m, thickness 0.07 m, one rocket; vehicles 100 % of
their hit points (a direct hit wrecks one) and 20000 N (the game's car explosion); people 900 N at
55 degrees; props 4000 N, breaks; Hit & Run; no holes; full-size effects; blast 7 m.

**FireWeapon** (`weapon.cpp`) is the one entry point: for each pellet (its direction scattered
inside `spread`) it traces, then runs `Hit_React`, and returns `ShotResult`s (the HitResult, the
bullet path, whether the game showed an effect) for the weapon's own visuals. Each pellet:
1. **Aimed:** trace the crosshair ray, starting 0.5 m before his depth along the view (things
   between the camera and his back don't count). From the hip: the target is along the barrel.
2. **The bullet's path:** from inside him to the muzzle (a gun through a wall hits the wall),
   then from the muzzle to just short of the target (a car right in front of the gun stops it).
   Otherwise the aimed hit stands.

**Visual cover** (`vcover.cpp`): everything drawn stops an aimed shot. The game's collision is rougher than its
pictures - the Simpsons' house has no collision for its bay window or the side of its garage, so bullets went
through both, and a person's cylinder is much wider than the person (found with the Weapons Lab's coverage scan,
tools/lab/README.md).
- While a hitscan weapon is aimed (`VCover_Arm` every frame), the next shot's pellet pattern is already rolled, and
  the depth probe reads three pixels per pellet (its ray, and 4 px to the right and below for the surface's slope),
  up to 48 probe pixels a frame in all. A reading starts coarse (1-150 m, 4 % steps) and is then tracked in a 1.5 m
  window round the last one (about 1 cm) while it stays inside it.
- `FireWeapon` fires the pellets along exactly those rays (from the camera that drew the frame) and traces each as
  before. `VCover_Decide` then compares the drawn depth with what the trace met:
  1. something drawn in front of it (by more than 12 cm + 2 %), not him, not a person, car or prop: the pellet stops
     there - a world hit with no object (`drawn`), dust, no sparks, a bullet hole;
  2. a person's cylinder met where the picture shows what is behind them (by 35 cm): the pellet goes past them, on
     to whatever is behind (the person is left out of the rest of that pellet's traces, `Trace_SetIgnore4`);
  4. a person drawn there that the trace missed (beside him the aimed trace starts inside their cylinder): a hit on
     them, so they react as from a traced hit; 5. the same for a car (tested as drawn, `CarGlass_Raycast`).
- Drawn surfaces nearer than where the aimed trace starts (between the camera and his back) and inside his own
  outline are ignored. Hip shots (not along a camera ray) use the traces alone.

**Surfaces** (`Hit_Surface`): ground by terrain type (grass; dirt for sand, gravel, dirt; water;
wood; hard for road, metal); buildings by the piece hit (boxes and walls hard, round pieces round:
hedges, trees, bushes); cars; bodies. Used by the game's effects and the gun's own.

**Reactions** (`react.cpp`, `Hit_React`): what the thing hit does about it, with the game's own
effects and rules; every amount comes from the weapon's spec.

| What | Address | How it's called |
|---|---|---|
| `ParticleManager` | `[0x6C87A0]` | +0x04 number of types (27), +0x08 a pool of 12 bytes per type |
| `ParticleManager::Add` | 0x4B6520 | EAX matrix; stack manager, `{int type; float bias; Vec3 vel}` (it pops both). One-shot |
| `SparkleManager` / `AddSparks` | `[0x6C844C]` / 0x506FD0 | EAX manager; stack position*, direction*, strength (it pops them) |
| a building's or prop's own particle | object +0x30, then +0x48 | `StaticPhysDSG` 0x60C89C, `InstStatPhysDSG` 0x60CB74, `DynaPhysDSG` 0x60CE9C, `InstDynaPhysDSG` 0x60CC54, `InstAnimDynaPhysDSG` 0x60CD0C; -1 = none. The sound name is at +0x08 (buildings: "nosound") |

- Particle types as they look in Level 1 (`fx <type>`): 3 twigs and leaves, 4 papers, 6 mail,
  9 white dust puff, 10 dirt burst, 11 grass clippings, 12 water (not in Level 1); 8, 14, 19 dust
  clouds and fires. The sprays come out along the effect's local **-Z**, so the matrix gets
  Z = -normal (Y as close to up as it can be).
- The emission bias scales an effect: the dust puff uses 0.4 (1 is a car's tyre smoke), dirt 0.6,
  grass 1.
- World hits: ground by terrain type (grass 11, sand/gravel/dirt 10, water 12, road/metal/wood a
  puff), buildings and props a puff plus their own particle; sparks (strength 0.25) off roads,
  metal and box-shaped pieces only.

**Vehicles** (`ReactVehicle`). Fields: +0xA4 type (0 the player's, 1 AI, 2 traffic), +0x2B8 mass,
+0x2EC full hit points, +0x424 damage type (1 the player's car, 2 AI, 3 traffic), +0x494 destroyed,
+0x49C grace seconds after a near-death hit, +0x4A0 hit points, +0x4A4 can be damaged, +0x4A8
SimState.

| What | Address | How it's called |
|---|---|---|
| damage at a contact point (the collision handler's) | 0x4ED570 | ECX car, EAX `sim::Collision*`, amount on the stack (it pops it) |
| `TriggerDamage` (called by the above, clamped) | 0x4EDF90 | EAX car; amount, clamp on the stack; health 0..1 in ST0 |
| which side was hit (called by the above) | 0x4EDD90 | the collision's +0x08 / +0x0C CollisionObject whose +0x18 is the car's SimState gives the point at +0x38 / +0x2C |
| `ApplyForce` | vtable slot 36 (0x4EE3C0) | thiscall(dir*, force): physics locomotion, velocity += dir x force / mass, damage 0.5 x force / 20000 unclamped |

- Per hit: sparks; if the car can be damaged and isn't in its grace period: a level shove
  (`vehicleForce`, before the damage) and `vehicleDamage` x full hit points at the hit point, through
  a minimal collision record (a fake CollisionObject with the car's SimState, the point at +0x38).
- The handgun: 8 % (about 12 hits) and 400 N. Destruction, explosion, husk, coins and the game's
  Hit & Run rise for a wrecked traffic or AI car are all native.

**Pedestrians** (`ReactCharacter`, `NPCharacter` vtable 0x607FF4 only). Fields: +0x70 SimState
(+0x6C control, +0x70 physics object), +0x90 ground plane (-1 none), +0x100 controller, +0x128 in
a car, +0x190 AI state manager (+0x04 state; its vtable slot 5 = id: 1 walking about, 5 knocked
down), +0x39D has been hit.

| What | Address | How it's called |
|---|---|---|
| `EventManager::TriggerEvent` | 0x432AD0, manager `[0x6C9018]` | EDX manager; event, parameter on the stack (it pops them) |
| kick force (walking about) / push (down) | vtable slots 54 / 36 | thiscall(dir*, force); velocity += dir x force / 100 |
| controller intention | `NPCController` (0x607DAC) slot 9 | thiscall(int); 16 = cringe, 15 = dodge |
| ground plane take / release | slots 29 / 30 | reference counted; the collision areas call them |

- Per hit (walking about or down, not in a car or a car door): events KICK_NPC_SOUND (0xE1, the
  victim's line) and PEDESTRIAN_SMACKDOWN (0xCD, Homer's taunt), has-been-hit, then **inside the
  game's physics** (a ground plane) a push 45 degrees up along the shot (`characterForce`, the
  handgun 500; the kick is 400), **outside it** the cringe intention, then OBJECT_KICKED (0x12A,
  pedestrians within 12 m panic). The game's flail and get-up follow from the push.
- **Why the check:** physics runs only in the collision areas (about 11 m round the player, and
  round active cars). Pushed outside one, a character falls through the ground.

**Props** (`ReactProp`; the kick's branch for anything that isn't a car or a person):

| Class (vtable) | Push (slot 36) | Break (slot 37) |
|---|---|---|
| `InstDynaPhysDSG` 0x60CC54, `InstAnimDynaPhysDSG` 0x60CD0C | `DynaPhysDSG::ApplyForce` 0x49FBA0, only with a ground plane (+0x90) | 0x4A0C40 / 0x4A0570, only if attributes class (+0x5C) is 4 and break type (+0x4C) isn't -1 |
| `DynaPhysDSG` 0x60CE9C | the same, with a ground plane | - |
| `StatePropDSG` 0x60C97C (phone booths) | 0x4A3AA0: events to its state machine (+0xDC: 8 always, 7 above 400, 10 from 800), then physics except for class 4; class 4 at any range | - |
| buildings (`StaticPhysDSG`, `InstStatPhysDSG`) | no: their slot 36 is `ret 4` | - |

Break types seen in Level 1: 3 hydrant, 14 and 15 small trees, 29 stop sign; 0x13 is Krusty
glass. After the push or break, OBJECT_KICKED (the game's coins and heat for the prop, once per
prop; the kick's thud; panic). Attributes: +0x08 sound name, +0x48 particle, +0x4C break type,
+0x50 mass, +0x5C class (3 loose, 4 breakable, 7 building, 10 lamp post).

**Bullet holes** (`decal.cpp`): `FireWeapon` adds one where `Hit_TakesHole` (hard or wooden ground,
and `StaticPhysDSG` / `InstStatPhysDSG` hits that are hard or wooden) and the spec's `holeSize` > 0.
- Up to 64, newest replacing oldest; cleared when the player Character pointer changes (a new level).
- Drawn first in `Wheel_OnWorldDrawn` (before the gun and its effects, after the depth probe) with
  `Renderer::SpriteQuad`: the four corners projected with the frame's camera, each with its own z/w
  and 1/w, depth-tested, no depth writes; 1 cm off the surface; skipped beyond 80 m or from behind.
- **Visible-surface fix-up:** a new hole is drawn at once at the collision point; the next frame the
  depth probe tests 128 depths evenly spaced from 0.75 m in front of it to 0.25 m behind on its pixel
  (`ProbeDraw` with a per-pixel range) and the hole moves to the depth found. Probe pixels are kept
  apart (the crosshair's is the screen centre). Over the player's screen box, readings nearer than
  0.3 m behind his chest are thrown away.

**Wasp cameras** (`ReactActor`, `ActorDSG` vtable 0x615C1C, AI ref 0x40003): position at +0xCC
(slot 16), shield flag +0x100, state props at +0xDC ("beecamera": event 7 DESTROYED -> state 5,
explosion, coins, removal) and +0xF8. The shot calls slot 36 (`ActorDSG::ApplyForce` 0x4153A0) at
max(propForce, 400): unshielded, DESTROYED; shielded, the first call drops the shield (+0x100
cleared) so a second call is made. Then OBJECT_KICKED, as the kick does. Wasps are pooled:
`ActorManager` +0x18/+0x1C the 20 `SpawnPoint`s (position +0x40, trigger radius +0x4C = 15 m),
+0x28/+0x2C the active `FlyingActor`s (+0x08 their `ActorDSG`) and projectiles.

**The Duff plane** (`sky.cpp`, Level 1): part of the sky model ("horizon1" in `L1_TERRA.p3d`,
joint `Plane1`, mesh `Plane1Shape`), drawn first each frame, camera-centred, on a 133 s loop
(4001 frames at 30 fps) that turns `Plane1` a full circle about its joint.
- `h_DrawIP` (scene.cpp) asks `Sky_PlaneDraw` for every indexed draw; the plane's two draws (198
  vertices / 338 triangles, 17 / 27) among the first 40 of the frame, with the bone 200-280 units
  from the camera, get a new WORLD: the joint (WORLD x view rotation transposed) turned about the
  vertical axis through itself by the throttle's extra heading, times the view rotation again.
- The direction of turn is measured from frame to frame. Throttle: up to 5x over 1 s, held 4 s
  after the last hit, down over 4 s. Game time (slow motion slows it).
- `FireWeapon` calls `Sky_Shot(dir)` for pellets that hit nothing: a ray from the camera (the
  sky's centre) against the plane box (-2338.7, 527.3, -107.8)-(-1954.6, 709.1, 368.0) and the
  banner box (-2189.7, 558.0, -920.6)-(-2102.9, 677.6, -107.8) in the joint's frame, 25 units of
  slack.

**Tires** (`tires.cpp`). A `Vehicle`'s wheels: `Wheel*` at +1044..1056; a Wheel: +0 its car, +4
steers, +5 drives, +8 its number, +12 radius, +20 suspension offset, +28 how far it has turned,
+52 on the ground, +60 steering angle; their rest points (the car's frame) at Vehicle+888 + 12i. The
wheel's centre is rest + (0, offset, 0) in the car's frame (+184), and the game draws each wheel as
a draw of its own exactly there, X along the axle.
- `FireWeapon` traces every pellet against the wheels of the cars it passes (a cylinder of the
  wheel's radius, 0.42 of it each side of the centre) before whatever the trace hit: the ground
  under the car, the car's collision box (up to 1 m behind its face) or the body as drawn.
- Flat is the game's physics: the suspension reads the radius every frame (wheel bottom = rest - radius
  + offset, 0x4EB138), so taking it down to half sinks that corner onto the rim. A shot drops it 12% at
  once, then 45% of what's left per second; a blast (within 3.5 m of a rocket's) 80% at once.
- Grip: Vehicle+708 (TireGrip, read every frame at 0x4E6712) less 16% per flat tire (at least 38%).
- The pull: the car's physics object (`ArticulatedPhysicsObject`, SimState+0x70) keeps its spin as
  angular momentum (+188); each step adds its torques (+312) and works out the angular velocity
  (SimState+0x5C) as the inverse inertia (+272: symmetric, xx xy xz yy yz zz from +276) times it, so
  the turn is added as momentum: 1 rad/s^2 per flat tire toward its side (rear 0.6), full from 8 m/s;
  a flat front wheel shimmies; each flat drags 0.9 m/s^2 (the velocity, SimState+0x50). Under 0.6 m/s
  the car is held still (sideways and turning): the game's tires let a car tilted on a flat creep
  round for ever on a slope.
- A traffic car (Vehicle+388 = 1, on its lane's rails) goes over to the physics when a tire pops
  (`SetLocomotion` 0x4E9660, ESI the car, EDI 0), as a rammed one does; it can drop out of the list
  of cars for a moment then, so a car is only given up after 1.5 s missing. The traffic takes a car
  that has come to rest back onto its lane (0x501BC8: type 2, `SetLocomotion` EDI 1), where a flat is
  nothing at all (it drove off as if new): while it has a flat it is put back onto the physics.
- Put back (radius, grip) when the car leaves for good, jumps over 12 m, is wrecked, or its hit points
  go up (a repair) - but not when he gets into a traffic car (its type changes to his, 0, and its hit
  points are set anew). Before writing anything back, the car must still be that Vehicle with those
  wheels and our values still on them (a reused traffic car); a new level just forgets them.
- Drawn squashed: the flat wheel's draw (its WORLD translation within 3 cm of the centre, X along
  the axle) gets a WORLD scaled along the car's up about the tire's top so its bottom meets the road
  where the rim rides, and 12% wider; the game's tyre-smoke puff (particle 9) at the bang and from
  the hole while it leaks; `car_hit_body_01` and `gag_gas` (soundfx.rcf) are the bang and the hiss.

**Hit & Run** (`Heat`): `HitnRunManager` `[0x6C84E0]` (+0x04 the meter 0..100), `HandleEvent`
0x4D8B00 (vtable slot 1), thiscall(event, param), called directly so no other listener hears it.
- KICK_NPC (0xA8, param the Character): +10 (+0x40); coins (+0x44, 0 by default) only before
  "has been hit" is set. Used once per knock-down (the victim was on their feet).
- VEHICLE_VEHICLE_COLLISION (0x60, param a pointer to `{Vehicle*}`): +5 (+0x1C), only traffic (2)
  and AI (1) cars, not chase cars or the ignored mission car (+0xF0), and only when the decay hold
  (+0xA8) is down to half of the 3 s (so at most every 1.5 s). Used on every hit of a car that
  isn't wrecked.
- Gates inside: off when disabled (+0xB8), indoors or entering (InteriorManager `[0x6C8FF8]`
  +0x28 / +0x08). Clamped to 100; the game's `Update` starts the chase at 100.

## Weapons

`G.weapon` is the weapon picked on the wheel (`WPN_NONE` fists, `WPN_HANDGUN`, `WPN_RPG`);
`G.held` is the one in his hand, the one drawn and posed. Picking another puts the one in his hand
away first (`show` down over 0.14 s), then the new one comes out with its pop. Firing, the pose
(`BuildPose` / `BuildPoseRPG`), drawing and recoil go by `G.held`; equip/aim/camera/input are shared.
The wheel's `SlotWeapon` says what a slot equips: slot 1 fists or the handgun (its D-pad option),
slot 2 the RPG.

## The rocket launcher

**Model** (`BuildLauncher`, `BuildRocket`): coloured boxes and smooth round sections in "launcher
space" (origin on the tube's axis where it rests on his shoulder, +Z out of the muzzle, +Y up):
a 1.26 m army-green tube, flared back end, yellow bands, rubber shoulder pad, ridged sleeve, trigger
grip, sight on the left, and the loaded rocket (warhead, motor, fins) with its red nose out of the
muzzle. Lit like the pistol.

**Pose** (`BuildPoseRPG`): the launcher is placed first and the hands are put on it.
- Its frame comes from his own joints after the torso is posed: `rest` = Shoulder_R + chest up x
  0.17 + his right x 0.03 (x his size); chest up = Spine_2 to Neck; his right = left shoulder to
  right shoulder. Carried it points 0.75 rad above his chest's forward (and a little out); aiming,
  at the aim point; blended by the aim weight. Recoil shoves it back along the tube and kicks the
  muzzle up (two springs).
- **Size** (`LauncherScale`): his reach (upper arm + forearm) and shoulder width against Homer's
  (0.411, 0.381 m), clamped 0.70..1.15: Bart 0.77, Lisa 0.72, Marge 0.81, Homer 1.0, Apu 1.08.
- **Right hand:** on the trigger grip with the pistol's wrist mapping (tube forward = the wrist's
  -X, launcher top = its -Y, grip centre at `kGrip` in wrist space), two-bone IK from Shoulder_R,
  elbow down and out; the pistol's fist.
- **Aiming:** chest and head as for the handgun but bladed left side forward (`kLchBlade` 0.45).
  Left hand palm up under the sleeve (the left palm faces -Z; fingers +X, thumb +Y: the right hand
  mirrored in X and Y, read from the models, so the same curl angles close it), at the furthest
  point along the sleeve his arm reaches (a sphere-line intersection; fades out if none).
- Every character's skeleton gives the numbers; there is no per-character table.

## The rocket

`rocket.cpp`. Fired from the loaded rocket's place in the launcher (launcher z 0.60) towards what's
under the crosshair (`Weapon_AimPoint`, the aimed trace of `FireWeapon`), or along the tube from
the hip. 26 m/s, accelerating at 45 m/s^2 to 46 m/s. Every frame `TraceShot(last, new position)`
with a 0.07 m radius (what the game's wasp bolts do in `Projectile::Update` 0x41D530 with the
dynamics list and RayDynamic): nothing thin is skipped whatever the frame time. A launcher poked
through a wall meets the wall (a trace from his shoulder to the rocket). No hit within 150 m or
5 s: it bursts in the air. Trail: cartoon smoke puffs from the tail, spread along each frame's
stretch (55 a second, up to 200), and a flickering motor flame and glow.

The game's particle effects are not used for the trail: `ParticleManager::Add` (0x4B6520) takes the
first free system in the type's small pool and drops the effect when they're all busy.

**Game time and states** (`Gun_Update` -> `Rocket_Update` / `Explode_Update`):

| State | Rockets and explosion effects |
|---|---|
| gameplay (context 9, input 1 or all) - on foot or in a car | fly, at the game's speed (slow motion slows them) |
| pausing, paused, a menu (context 10 now or next, or input 2) | frozen |
| anything else: cutscenes (input 0x10), context changes | taken away, no explosion |
| a new player Character (level change) | taken away |
| a teleport (over 3 m at over 80 m/s: a mission restart done while paused, a building's door) | taken away |

## The shotgun (Weapons Lab, weapon 1)

`shotgun.inc` (included in gun.cpp; `WPN_SHOTGUN`, wheel slot 3, key `4`). A pump-action shotgun built like the rocket
launcher: the gun is placed first, from his own skeleton and sized to him (`ShotgunScale`: Bart 0.78, Lisa 0.72, Marge
0.82, Homer 1.0, Apu 1.09), and his hands are put on it (right hand on the grip with the pistol's wrist mapping, left hand
palm up under the pump, as the launcher's sleeve).
- **Model** (`BuildShotgun`, shotgun space: origin on the bore at the back of the receiver, +Z to the muzzle): walnut
  stock with a raked grip and a black pad, blued receiver with an ejection port, barrel with a rib and a brass bead, the
  magazine tube, and the grooved pump (part 1: it slides back `kSgPumpTravel`). A spent shell (red, brass head). Compact,
  because Homer's arms are 0.41 m long.
- **The draw** (`show` 0 -> 1 over 1.2 s, `SgPhaseAt`): his right hand goes to his trouser pocket (off the right hip
  joint), the gun comes up out of it muzzle last - a user clip plane at the pocket hides what is still "in" it
  (`Renderer::Clip3D`) - swings up into both hands and he racks the pump. Holster: the same back into the pocket (0.5 s);
  into a car it goes at once.
- **Carried (v3):** at hip-level ready, as a man holds a shotgun (a reference photo): the grip placed from his
  right shoulder joint (down / forward / out `kSgLowDown/Out/Side`, so his 41 cm arm always reaches it), level to the world
  and his heading - not his chest, which is pitched ~20 degrees forward on Homer - the muzzle forward and across to his left
  (`kSgLowAcross`) over the top of his belly; both elbows bent and hanging below the shoulder-hand line. Kept out of his
  torso by its measured profiles (below).
- **Aimed (v3):** the butt in the shoulder pocket - inside his right shoulder joint (`kSgPkIn`), on his chest's skin
  (`ChestDepth`: the Spine_2 torso profile) - the rib on the aim point; chest bladed (`kSgBlade`, left shoulder forward:
  his short arms reach the pump) and leaned forward; head tipped and rolled onto the stock (`kSgHeadDown/Roll`; Homer's
  eye is 44 cm above his shoulder joint, a person's ~25), then backed off until his chin, jaw and nose (`CharShape.jaw`)
  rest on the gun's top, not in it (`SgTopAt`; Bart's chin sank 4 cm without it). Right elbow out and down (a long gun's
  silhouette), left elbow under the gun. The aim camera sits further right and a little lower with the shotgun
  (`kSgCamSide/Up`) so the gun shows past his shoulder.
- **The raise and the lowering** go in an order (`wGun` / `wUp` / `wIn` / `wHead`): raising, the muzzle comes onto the
  line first, the butt rises out in front of the pocket and then slides back into it, his head comes down last, with a
  small settle (`sgSettle`); lowering, the butt leaves the pocket first, the muzzle drops, his head comes up last.
- **Hands (v3):** right hand on the raked grip (the pistol's wrist mapping turned down the rake); its thumb laid over the
  grip (`AimThumb`, aiming the thumb's *mesh* - `CharShape.thumb` - not its joint line: Homer's is 40 degrees off it).
  Left hand round the fore-end: its roll round the fore-end and finger angle are picked each build (a small search) for the
  straightest wrist. The hand's turn on the forearm is shared out to the forearm and the upper arm (`SgTwistArm`): one
  vertex of each of Homer's thumbs is skinned half to the forearm, and a turned wrist dragged it into a spike.
- **Fired** (`kShotgun`: 10 pellets, 4 degree cone, 45 m): through `FireWeapon` with visual cover; each pellet makes its
  own hole, glass crack, tyre hit and share of a car's damage (3.2 % and 420 N each), but a person or loose prop hit by
  several reacts once (`WeaponSpec.group`): 40 % of the blast for one pellet, +15 % a pellet, full from four (1250 N at
  31 degrees: they fly backwards, flailing - the game's own knock-down). Recoil: the gun shoves back into his shoulder
  (`sgBack`) and its muzzle climbs about the butt (`sgFlip`), his chest rocks back and his right shoulder turns back
  (`kickChest`, `sgTwist`: the spine only, his feet stay planted), a big camera kick. A big flash with side flashes, a
  rolling smoke cloud, a streak per pellet. The pump inside the recoil (0.16 s after the shot): back to a hard stop at
  0.14 s, a beat, home at 0.32 s (`PumpOffset`; the two clacks of `sfx_pump.wav` land on those), the gun dipping
  (`kSgPumpDip`) and pushing forward as it slams home (`kSgPumpStretch`), the left hand riding the fore-end, the right
  never leaving the grip; the spent shell leaves the port at 85 % of the back stroke and tocks on the ground. 0.8 s a shot.
- Sounds (synthesised, `build_sounds.py`): the blast, the pump, the shell. Debug: `sgfreeze <0..1>` (hold the draw),
  `sgaim <0..1>` (hold the raise), `sgtune <name>,<value>` (every placement number), `sgpump`, `sglog` (the pose report:
  hands short, gun pushed out of him, wrist turns, elbows from the shoulder-hand line, chin lift), `posedump <name>` (his
  posed skeleton and the gun as drawn -> `WeaponWheel\<name>.json` for the offline pose checker), `orbitjoint <j>` /
  `orbitup <m>` (the debug orbit camera on one of his joints).

### The shotgun, v4: the polish and QA pass (2026-10-14)

Every change below was found and checked by the animation QA (next section), against Radical's own animations
(`research/rig/radical_baseline`, outside the repo).
- **His real body** (`bodymesh.inc`): the player model's mesh is read from the game's own files when his skeleton is first
  seen (`art\chars\<name>_m.p3d`: positions, joints, weights, triangles - nothing of it ships) and skinned on the CPU with
  the pose being built. `BodyDist` gives a point's distance to his skin and whether it is inside him: the nearest triangle
  (two levels of bounding spheres) and a **generalized winding number** over all of him (`BodyWinding`: his ~60 pieces are
  open and overlap - shirt over belly, sleeve over arm - so no single face can say; far pieces as one dipole each). A
  point's side is cached while it can't have changed (`BodySide`). Masks only say which parts count, never which close him.
- **The stock seated on his chest** (`SgSeatStock`): the aimed gun slides along its bore (the aim's line stays) until the
  recoil pad's deepest point is 0.8 cm into his skin and nothing else of the stock is in; then `SgMeshPush` keeps every part
  of the gun out of him (the pad's allowed press eased in with the raise).
- **Elbows** are pure hinges, as in all 368 of Radical's clips (the forearm never turns on the upper arm): the right one
  takes no share of the hand's turn (`kSgTwistFA/UA` 0 - its stretch is now Radical's own for the bend); the left keeps a
  share (`kSgTwistFAL/UAL` .55/.35): cradling the fore-end palm-up needs ~100 degrees of forearm turn, and in the wrist
  alone that drags the vertex of his thumb that is skinned half to the forearm into a 3.5x spike. Each elbow's **swivel**
  round its shoulder-wrist line is solved for the easiest wrist (`SgBestSwivel` / `SgLocalSwivel` near last frame's,
  `SgEase`), within Radical's range (right 26-70 degrees once up, left -11-60), never above the line.
- **Arm transitions** (`SgArmBlend`): between the game's pose and ours the wrist target, hand turn, swivel and twist share
  are interpolated and the arm is solved at each step (blending each joint's rotation folded his arm through itself).
- **The draw** follows a keyed path (`kSgDrawKeys`, designed offline against his mesh with `tools/lab/qa/drawpath.py`):
  out of the pocket hanging along his leg, a pendulum swing beside his belly, level in front of it, into the carry. (A pull
  up to his chest with the gun under his hand had turned his hand ~150 degrees on the forearm; a straight swing went
  through his belly - his belly hangs over his trouser pocket.) The holster runs it backwards, after the gun has come
  down out of any aim (switching weapons while aiming jumped it 70 cm).
- **The aim** is eased (`kSgAimFollow`): the crosshair's point jumps (the depth probe's hit coming and going) and the gun went
  with it; lowering, the last aim is held in his chest's frame so the gun comes down with him as he turns (it swept across
  him). The raise/lower order is an eased weight (`g_sgRaiseW`), not a flag that could blink.
- **Recoil** pivots on the pad's heel as the muzzle climbs (its top stays in the pocket and the toe comes off his shoulder;
  about the toe, the heel and the comb went 4.7 cm into his chest), on its toe as the pump's dip tips it down; only 25 % of
  the shove slides the gun on his shoulder (`kSgBackRel`), the rest is his chest rocking back with it. **Pump:**
  12 cm stroke, the gun dips and cants, his left shoulder goes back with it, and the camera takes a small knock at each clack
  (from the aim camera the fore-end is behind his body).
- **Walls and cars** (not people): a ray along the carried and the aimed gun (as they'd be) against the game's collision
  (`TraceShot`); where the gun would go in, it comes down out of the aim and its muzzle down, low ready
  (`G.sgWall/sgWallA`; muzzle up, port arms, brought the barrel up through his jaw).
- **The left hand's reach**: where the fore-end is out of his arm's reach his left shoulder comes forward (up to 16 cm),
  by as much as the hand has come (`ph.leftW`): in full at once, it jumped 16 cm in a frame as the hand set off in the draw.
- Fist and thumb eased in and out with the pocket (put away, the hand opens over the holster's last sixth, the gun in the
  pocket by then - not in two frames); the closed-eye textures are made when the gun comes out.
- **Walking backwards while aiming** (`LegLayer`, every weapon): the walk cycle's foot tilt is mirrored about the foot's tilt
  at rest (Homer's ankle sits 25 degrees above the ball of his foot: mirroring the bare tilt turned a flat foot 50 degrees
  toes up) and turned about his hips' side axis (a turn between the foot's own directions lost its heading when the foot
  pointed straight down: feet flipped 120-160 degrees in a frame).

### The animation QA (`tools/lab/qa/`, debug `posetrace`)

`posetrace <name>,<s>` (`posetrace.inc`) writes every frame's skeleton as drawn, the weapon as drawn, the camera and the
animation's state (taken where the pose was built) to `WeaponWheel\<name>.trace`. `animqa.py` skins his real mesh in every
frame and reports: the gun inside him (by the part that contains it, intended contact told apart: the pad on his chest, the
comb under his cheek, the carried stock on his side), him inside the gun, one part of him inside another beyond what
Radical's own animations do to the same skin, every skin edge's stretch (and against Radical's worst for that edge), the
elbows off their hinge, the hands' gap to the grip and the fore-end, the pad's contact, one-frame snaps, the camera's near
plane and crosshair, the wink. `qasheets.py` / `render.py` draw the worst frames from ten angles (offline z-buffer renders
of the real mesh, clipping in red, the in-game frame beside them); `look.py` close-ups; `compare.py` before/after tables and
charts; `drawpath.py` designs draw paths. Scenarios: `sg_qa.py` (cycle / move / stress / wall), `sg_try.py` (A/B
tunings), `sg_views.py` (in-game takes from many angles, in sync), `sg_showcase4.py` / `sg_compile4.py`, `sg_family4.py`.
Always build with `tools/lab/buildinstall.sh` (it refuses to install after a failed compile).
Staging matters: every aim starts along the game camera's heading and the lab's `aimat` only takes over once the aim camera
is half in, so `sg_qa.py` turns the follow camera to the aim point first (`camhead`; left 50 degrees off, the gun swung out
and whipped back mid-raise); it holds the stick by a heading in the world (`wstick x,y,heading`: a debug orbit camera round
him turned `lstick` with him and ran him in circles); the movement runs out over the open front lawn (towards the house he
ran onto the porch, where the wall check held the aim down, and backed off its step - which drops any aim: a change of
locomotion is the game's to steer). The trace's spare state slots carry the leg layer's `legBack` / `legYaw`.

## His face and body in numbers (`face.inc`, `charshape.inc`)

Per player model, measured from the bind-pose models by the Weapons Lab's research (only numbers are kept): the eyes'
centres and radii in the head joint's frame, the eye dome's axis, the skin colour of the game's own closed-eye texture,
and how far the torso reaches in front of Spine_2 at each height. The model is told by the head joint's bind pitch (Homer
9.9, Bart 36, Lisa 28.4, Marge 31.5, Apu 18.3 degrees). Head joint frame: +X forward, -Y up, -Z his left.
- **A wink (v3):** the game can't shut one eye (both are one mesh with one texture; its blink swaps the texture). So the
  eye piece itself is split while he winks (`Gun_EyeSplit`, scene.cpp `h_DrawIP`/`h_DrawP`): it is the one-slot piece of
  his skinned on the head joint (`g_skinHeadPiece`), its triangles split by the head's mid-plane into two index lists
  (made once); the right eye is drawn as the game set it (it goes on blinking), the left with a closed-eye texture of ours
  made from the game's open eyeball texture the way its own blink frames are (lids from top and bottom to the pupil's
  middle, a thin lash line: `eyelash`). Shaped and lit as the game draws it, it holds from every side - the v1 drawn lids
  showed the white eyeball past them from the side. (The drawn lids, `WinkDraw`, remain the fallback.)
- **Torso profiles, jaw, thumbs:** `charshape.inc` also has each model's torso as radial profiles round Spine_1 and Spine_2
  (16 directions every 5 cm, ray-cast against the bind mesh: `TorsoReach`, `SgBodyPush`), its chin/jaw/nose extreme points
  in the head frame, and its thumb mesh direction - generated by `assets/build_charshape.py` from the Weapons Lab's research
  (research scripts kept outside this repository; only numbers come in).
  The same folder has the offline pose checker (`posecheck.py`: skins his real mesh in a `posedump`ed pose and measures
  the gun inside him, hands on the gun, stretched edges) and the rig bible (`rig_bible.md`). Costumes share the default model's numbers (most are within 3 mm; h_scuz, h_donu, b_tall, l_cool/flor/jers and
  m_poli differ).

## Explosions

`explode.cpp`, `Explode(spec, hit, point, flight direction, shooter)`. Checked in the disassembly:

| What | Address | How it's called |
|---|---|---|
| `BreakablesManager` | `[0x6C879C]` (getter 0x4B80C0 creates it) | +0x10: 12 bytes per breakable type, the loaded count at +8 |
| `BreakablesManager::Play` | 0x4B87D0 | thiscall(type, const Matrix*), ret 8; takes a pooled copy (reusing the oldest), places it, fires HIT_BREAKABLE (param &type). Type 24 = the car explosion |
| the car explosion | 0x4EE280 | stdcall(Vehicle*): `0x4DC7D0` pushes every dynamic in the car's collision area within 20 m with `ApplyForce(dir, 20000)` (no falloff; edi = position, stack the world-physics object, area, 20, 20000); `Play(24)` at the wheels' centre; BIG_BOOM_SOUND (0xC3, its listeners read the param as a Vehicle) |
| `Vehicle::TriggerDamage` with clamp 0 | 0x4EDF90 | hit points down; at 0 destroyed (+0x494); the car's update then explodes it and fires VEHICLE_DESTROYED and, unless +0x478 is 1 (an AI car hit it last), VEHICLE_DESTROYED_BY_USER (+25 Hit & Run) |
| who hit the car last | +0x474 (byte) / +0x478 | the collision handler writes 1 / the other car's type (+0xA4: 0 the player's) at 0x4ED52B; reset to 0 / 3 |
| `Vehicle::ApplyForce` | 0x4EE3C0 | does **not** check "can be damaged" (+0x4A4): the mod checks it |
| CAMERA_SHAKE (0x12C) | SuperCamCentral::HandleEvent 0x429D20 | param `{int player (0); Vec3 dir; float magnitude; byte 0}`, what a crash sends (0x4ED660: impulse x 1e-5 above 19000) |

What an explosion does:
1. **The look:** `Play(24)` at the blast centre (0.3 m off the surface) - the game's fireball,
   shockwave, debris and smoke; one of the game's three car explosion sounds (by distance);
   CAMERA_SHAKE for the follow camera and a jolt for the aiming camera and the pad within 40 m;
   the mod's cartoon star flash (capped on screen, faded near the camera); a soot scorch decal on
   buildings, walls and roads. Without the game's effect (none loaded) the mod's fireball puffs
   and smoke stand in.
2. **What it hit:** the ground or a wall throws its own particles (full size) and sparks; a car is
   wrecked (TriggerDamage, clamp 0); a person, prop or wasp takes the full blast below.
3. **The blast** (`spec.blast`, 7 m): every dynamic owner within 10 m (the dynamics list, the
   player left out). Each: distance from its bulk (cars 1.2 m, others 0.3-0.4), strength
   `(1 - d / 7) ^ 0.8`, and a line of sight from the blast to it (`TraceShot`): buildings, walls,
   animated pieces and the ground in the way shield it (the ground within a metre of the target
   doesn't); cars, people and props don't.
   - **Cars** (not wrecks, not "can't be damaged"): marked hit by the player (+0x474 = 1, +0x478 =
     0, as a ram), the game's damage on the side facing the blast (85 % x strength of its hit
     points, clamped) or wrecked outright within 2 m, then a shove away and up at 20000 N x
     strength (Vehicle::ApplyForce, which adds its own unclamped damage like the game's car
     explosion), and the +5 for bumping it. The player's own car (type 0) is never wrecked outright.
   - **People:** the bullet's reaction (knocked down inside the game's physics, cower outside;
     the "ow", the taunt, panic, +10) at 900 N x (0.35 + 0.65 x strength).
   - **Props and wasps:** the bullet's reaction with 4000 N x strength; breakables break when the
     strength is over 0.3 (or hit directly); OBJECT_KICKED's coins and heat.
4. **Not touched:** the player himself.

**Wasp shields** (checked by a review agent): `AddShield` is only active in Levels 5-7's scripts.
`ActorDSG::ApplyForce` (0x4153A0) at 400 N or more on a shielded wasp clears the flag (+0x100) at
once (0x415A00 -> 0x415A10) and pops the "beeshield" state prop; a second call in the same frame
sends DESTROYED, which every live "beecamera" state accepts. So one shot (two calls) kills it, as
two kicks do.

## Input

`xinput1_4!XInputGetState` and ordinal 100 are hot-patched. The game reads the pad through the
Mod Launcher's XInput hack.
- **Wheel open:** the game gets an all-zero pad.
- **Armed:** the configured aim/fire buttons and the triggers are removed. While aiming, the left
  stick and both right-stick axes are removed too.
- **Packet numbers:** the game only re-reads when `dwPacketNumber` changes, so the filter assigns
  a new number whenever its output changes (`Renumber`).
- **The mod's own reads** use the original functions: it always sees the real pad.
- **Aiming stick.** A round deadzone (`AimDeadzone`), rescaled to start from zero, then:
  - a curve (0.3·t + 0.7·t³);
  - a snap to pure turn or pure tilt within ~10°;
  - ramped turn speed (2.6 rad/s yaw and 1.6 rad/s pitch at full stick, × `AimSensitivity`);
  - eased pitch limits.
- **Triggers** are read with the backwards-trigger calibration: the resting value is learned
  only on a connected pad and never drifts onto a resting finger.
- **Rumble** goes through `XInputSetState`: two motors fading linearly, sent only when a level
  changes, and forced off when the wheel opens or the window loses focus.

### How the game reads the keyboard and mouse

The game creates DirectInput 8 through COM (`CoCreateInstance(CLSID_DirectInput8,
IID_IDirectInput8A)`), so it uses the ANSI interfaces. `radControllerDirectInput` (one object per
device; the device interface is at `+0x260`, its input points at `+0x264`) reads it like this:

| Where (Simpsons.exe) | Call | When |
|---|---|---|
| `0x562EE6` | `SetProperty(DIPROP_BUFFERSIZE, 1023)` | setup |
| `0x563B41` | `GetDeviceState` (its own data format, 4 bytes per input) | once, after acquiring: seeds each input's starting value |
| `0x563DFD` | `Acquire`, `Poll` | every frame, devices that need polling |
| `0x563E5B` | `GetDeviceData` (20-byte DirectInput 8 records), one record at a time: first with `DIGDD_PEEK`, then the same record for real, handed to the input points; repeat until empty | every frame |
| `0x563F68` | `GetDeviceData(NULL, &INFINITE, 0)` | flush |

All the game's devices share one vtable inside the real `dinput8.dll` (the launcher doesn't wrap
them), so patching its `GetDeviceState` (slot 9) and `GetDeviceData` (slot 10) catches every
device. A device we create ourselves has the same vtable, so no proxy is needed.

### Keyboard and mouse (`kbm.cpp`)

At start-up the mod creates a mouse of its own once, only to reach that shared vtable, and
patches `SetDataFormat` (11), `GetDeviceState` (9) and `GetDeviceData` (10). The hook is in place
before the game sets up its devices, so `SetDataFormat` gives the game's own layout (logged:
`0:X 4:Y 8:Z 12:B0 ... 28:B4`; Wine numbers a mouse's buttons on from its axes, so buttons are
counted in the order listed). Should a device be met first in `GetDeviceData`, the layout is
guessed from `EnumObjects` (4 bytes per object), and the log says so.

- **Reading:** every record the game really reads (not the `DIGDD_PEEK`) adds to the frame's X, Y
  and scroll counts: raw relative counts, exactly what the game gets, with no Windows pointer
  acceleration. Keys and mouse buttons come from `GetAsyncKeyState` while the window has focus.
  The game holds its mouse and keyboard non-exclusive / foreground (logged; `0x6`) and buffers
  1023 records, drained every frame, so neither the async state nor a fast mouse is a problem.
- **Keys by place:** letters, digits and `GRAVE` are bound by their scan code on a US keyboard
  (code `0x200 + scan`), the way the game binds its own keys, and read through the virtual key the
  player's layout has there (`MapVirtualKey`, re-read when the layout changes). On AZERTY the `Q`
  bind is the key marked A; read by virtual key it was the key marked Q, the game's "move left"
  there (and on Dvorak, E was the game's D). Hint labels are what the layout prints on the key.
- **Not ours:** a key that goes down while Alt or a Windows key is held is the system's (Alt+Tab
  opened the wheel), and anything held down outside play (a menu, a cutscene, another window, the
  first frame back in focus: the click that brought the window back fired the gun) counts as up
  until it is let go.
- **Hiding:** a record's value is zeroed (a button reads released) for what is ours: movement while
  the wheel is open or while aiming (else the game's own mouse look would turn the camera as
  well), the scroll wheel on foot, every button while the wheel is open, and the aim/fire buttons
  while a weapon is picked, all through play: on foot, in a car and getting in and out (the game's
  left click gets in and out of cars and its right one is the handbrake: a shot fired as he climbed
  out ran him to the nearest car and in again). `GetDeviceState` gets the same treatment.
- **Adding:** when the game's buffer is empty, a queued record of ours is handed over. `KeyAction`
  (E) and `KeyKick` (F) press and release the game's mouse buttons 0 and 1 this way (only in play,
  never in a menu or the wheel; in a car button 1 is the handbrake); debug `mmove` / `mscroll` move
  the mouse through the same path, and debug `gkey` presses keys on the game's own keyboard (its
  format puts each key at an offset of its own, learnt from `SetDataFormat`).
- **The pointer:** kept in the middle half of the window (ClipCursor) during play with the mouse,
  let go in menus and when the game isn't in front (a watcher thread, the same "in front" test as the
  frame's: the game's window or one it owns). The game hides the pointer itself, from its window
  procedure: on a mouse move over the picture (WM_MOUSEMOVE) it hides it, on one over the frame
  (WM_NCMOUSEMOVE) it shows it, through its switch at 0x444640 (AL shown; the state at 0x651B88,
  ShowCursor only on a change). Coming back over the title bar from another app left it showing all
  through play; so while held it is hidden here too: the game's switch, and the display count put at
  exactly -1 by way of 0 (the display driver is told to hide it again). The first holds are logged.
- **Mouse aim:** counts go straight onto the aim (`0.022°` × `MouseSensitivity` per count),
  no ramp, no smoothing, nothing scaled by the frame time. The stick's lead limit becomes a hard
  stop at 1.2 rad ahead of his body (he turns after the view).
- **Wheel:** the mouse's movement since the wheel opened is a virtual stick, kept within a circle
  of `WheelMouseDistance` counts (so moving back the other way answers at once), and picks a slot
  through the stick code (over half way out, 26° to leave a slot).
- **Hints:** the last device used shows (a key, a click, about 20 counts of mouse within the last
  0.15 s, WASD / Space / Shift; or a pad button, a trigger past 30 %, a stick pushed past 30 % or
  swung), held for 0.25 s. A stick resting off centre doesn't hold them on the pad. No pad at start,
  or the pad unplugged: keyboard.
- **The ini's keys** are logged once the log is open (`Keys: wheel TAB, aim RMB, ...` and the
  keyboard layout), with any word that isn't a key name, a third key for one job, or two jobs on
  one key (the `Q` pair is meant). A comment after a value (`; ...`) is ignored.

## Slow motion

`radTime` converts `QueryPerformanceCounter` on every call, and some code uses `GetTickCount`.
Both imports of `Simpsons.exe` return a virtual clock that advances at `scale` times real time.
`scale` eases toward `SlowMotion` (default 0.3) while the wheel is open. Other modules (sound, the
launcher, our animations) see real time.

## Rendering

- **Atlas:** one 2048×2048 RGBA texture (straight alpha, loaded with GDI+, premultiplied on load,
  mipmapped) plus `atlas.txt`, which lists sprites (`S name x y w h anchorX anchorY scale`) and
  font glyphs (`G font code x y w h xoff yoff advance`).
  - Segments are made by `build_assets.py` from signed distance fields: anti-aliased, with a gold
    tube frame and a navy panel.
  - The greyed "TBD" twins (`seg*l`, `seg*k`) are desaturated copies at 0.66 scale.
  - Fonts are converted from the game's Pure3D texture fonts: Boulder (`B`), Swiss 721 (`W`).
- **Blur:** `CopyRects` of the back buffer, downsampled twice, then 3 four-tap blur passes at
  quarter size, drawn back with a dim and vignette.
- **State:** every draw saves and restores the full D3D8 state (state block plus render target,
  viewport, streams and indices), so the game never notices.
- **Gun mesh:** fixed-function, coloured vertices lit on the CPU with the game's own scene
  lights, drawn with the game's camera and depth inside the game's own scene. The lights are
  read every frame: the world tView's ambient light at `+0x3C` (colour at `+0x10`) and up to
  four directional lights from `+0x40` (direction at `+0x50`). Level 1 has three, at 30°
  elevation and 120° apart. Metal adds a small sky reflection and glint. Colours come from the
  game's character palette.
- **Effects** (smoke, flash, tracers, impacts, sparkles) are 2D sprites, but drawn inside the
  game's scene at their projected depth (z/w, 1/w) and depth-tested against the world, so Homer
  and walls hide them. The muzzle glow is additive and not depth-tested, so it lights his hand.
- **Depth probe.** "What's under the crosshair" without the game's collision:
  - Still inside the game's scene, 128 one-pixel quads at log-spaced distances (1–150 m) are
    depth-tested against the game's depth buffer into a render target of ours, each adding 1
    to the red channel.
  - At Present the pixel is copied back (`CopyRects` to a system-memory surface). The count
    gives the distance in about 4 % steps.
  - That drives where shots land, the tracer length, the impact effects and how far the arm
    converges.
  - It's off for anti-aliased depth buffers. No frame-rate cost was measured.

## The game's own art and sounds (`gamefiles.cpp`)

The mod ships none of the game's files. At start-up it reads what it borrows from the player's copy:

- **Sounds:** `soundfx.rcf`'s index (the entry count at 0x800, (hash, offset, size) records from 0x810,
  the names at 0x2808 in the order of the records sorted by offset); each entry is a 2 KB header and
  16-bit mono PCM at 24 kHz, wrapped in a WAV header in memory.
- **Pictures** (`I` lines in atlas.txt): the HUD's digits, arrows and question mark, PNGs copied into
  their blank places as they are.
- **Fonts** (`T` lines): the letters of `font0_24.p3d` (Boulder) and `font2_12.p3d` (Swiss 721): the
  P3D font chunk (0x22000), its texture pages (PNGs inside 0x19002 chunks) and glyph list (0x22001);
  each letter cut from its page, padded, given a black outline (its alpha spread by a disc) and the
  fill's gradient, with `build_font()`'s float64 maths and numpy's rounding, in double precision
  (the x87 unit is switched from Direct3D's single precision for it). Letters are matched to the
  atlas by character code.
- PNGs are decoded by GDI+ from memory with their colour-space chunks (gAMA, cHRM, sRGB, iCCP)
  dropped: GDI+ would colour-correct by them, PIL (which built the atlas) doesn't.

Anything missing is logged ("Game art: ...", "Sounds: ...") and left blank or silent.

## Other releases and a mod that didn't start

- **Releases:** `Game_Init` recognises the English, International, Best Seller Series and Demo
  executables (SHARMemory's fingerprint). Every call into the game's code, code patch and English
  global is used on the English release only; each way into them checks it (`Game_IsEnglish`), not
  just the callers. Elsewhere the wheel and weapons work without hits, game effects, aimed walking or
  the follow-camera turn, and the aim camera only writes into a camera that reads like one. An
  unknown executable leaves the wheel off. (Audited 2026-10-08.)
- **Not hooked in:** DllMain starts a watcher thread; if the game's window has been up 5 s and
  Direct3DCreate8 never came through us (the Mod Launcher's Direct3D 9 hack, or anything else), it
  says so in the log and a message box.

## Files at runtime (`<game>\WeaponWheel\`)

| File | Purpose |
|---|---|
| `atlas.png` / `atlas.txt` | the art: ours, with blank places for the game's pieces (filled at start-up, below) |
| `sfx_*.wav` | ours: shot, equip, holster, cycle (Fists/Handgun), rocket (the launch). The game's own are read from its `soundfx.rcf` at start-up: open, close, tick, page, locked, hit_hard, hit_soft, boom1-3 (its car explosions) |
| `WeaponWheel.ini` | settings and buttons |
| `WeaponWheel.log` | what the mod saw: version, hooks, game state changes, wheel open/close |
| `README.txt` | player instructions |
| `debug_cmd.txt` | only with `Debug=1`: commands, read and deleted every 0.2 s |
