# SHAR (English Simpsons.exe) - WalkerCam wall/obstruction handling and how to reuse it

Static analysis only (pefile + capstone). Image base 0x400000. All addresses are VAs in the English
exe. "LTCG" = the compiler passed arguments in arbitrary registers for internal functions; those
conventions are spelled out for each callable function.

Confidence tags: **[H]** high (read directly from code and consistent across call sites),
**[M]** medium (code is clear, the name or meaning is inferred), **[L]** low.

---------------------------------------------------------------------------------------------------

## 0. TL;DR

* SHAR's on-foot camera uses **three** obstruction mechanisms, all driven by `SuperCamCentral` (SCC):
  1. **A sim collision sphere for the camera** (a physics object in the sim library). SCC owns one
     `sim::SphereVolume`/`CollisionObject`. Every physics substep it is placed at
     *camera position + unit(target - camera) x radius*, where radius = the active cam's near plane
     (1.0 m). The world physics manager collides it in its own collision area. A collision-solver
     callback turns each contact into a push-out vector (normal x depth x 2.05), and SCC hands the
     list to the cam. `WalkerCam::UpdateForPhysics` then moves the camera by those offsets and
     recomputes its distance. A separate **ground offset** pushes the sphere out of the terrain.
  2. **Ray tests against a per-frame "IntersectionList"** (SCC+0x1A8). The list caches the static
     colliders, dynamics, animated colliders and fences near the player.
     `IntersectionList::LineOfSight` / `TestIntersectionStatics` are used for:
     * camera resets (camera too far, or too far below the player: snap to the minimum distance,
       or to the first hit, or to 2 m above the head);
     * four "whisker" probes that auto-rotate or raise the camera away from walls while the stick
       is idle.
  3. **A "too close" rule**: if the camera distance drops below `nearPlane + 1.5` (= 2.5 m), the
     WalkerCam **flips 180 degrees** to the other side of the player and fires `CAMERA_CHANGE`.
     That same 2.5 m figure is the threshold of the separate render check that **stops drawing the
     player** when the camera is within `near + 1.5` of his bounding-sphere centre.
* **There is no single native "safe camera position" function.** The reusable, game-thread-safe
  building blocks are the IntersectionList functions:
  * `LineOfSight` (0x41BAB0), whose sweep radius you choose;
  * `TestIntersectionStatics` (0x41B240), which returns a hit point;
  * either **SCC's own list** (`*(DWORD*)0x6C906C + 0x1A8`, refilled every frame around the
    player), or **a private list** (ctor, fill and dtor, the exact pattern the game itself uses at
    0x459C20 and 0x4F34B0).
* **Driving WalkerCam to do it for us is not practical.** At aim distances (about 2-2.6 m) its
  "too close" rule flips the camera every frame, its offsets are applied relative to the current
  render-camera position, and its whisker logic steers heading and elevation.
* **Recommended:** sweep pivot -> desired camera with `LineOfSight` (radius 0.2-0.3,
  statics only), binary-searching the free distance. Pull in instantly and ease back out. Keep the
  camera at least about 1.3 m from Homer (near plane 1.0). When there is no room, swap shoulders or
  raise the camera (the analogue of WalkerCam's flip).

---------------------------------------------------------------------------------------------------

## 1. Objects and layouts

### 1.1 The camera table / SuperCamCentral (SCC)  [H]
* `0x6C906C` -> the camera table, which is just `SuperCamCentral[4]`, each 0x1F4 bytes. The getter
  `0x42A2D0` (`EAX`=player index, `ECX`=mgr) returns `mgr + i*0x1F4`. So for player 0,
  SCC* == `*(DWORD*)0x6C906C` (matches the known fact).
  ```
  0042a270: mov eax, [0x6c906c] ; ret
  0042a2d9: imul eax, eax, 0x1f4 ; add eax, ecx ; ret
  0042a226: mov edi, 0x7d0        ; 4*0x1F4 allocation in the singleton creator
  ```
* SCC fields used here:

  | Offset | Meaning |
  |---|---|
  | +0x00 / +0x04 | vtables 0x614E5C / 0x614E4C |
  | +0x08 | **sim collision-area index** of the camera, from the world-physics object's 0x4DD2D0 ("not enough camera collision indices") |
  | +0x0C..+0x88 | SuperCam* slots (32 slots; `RegisterSuperCam` 0x429290 scans up to 0x20) |
  | +0x8C | active SuperCam; +0x90 its index; +0x94 number registered |
  | +0xAC | **the tPointCamera** shared by every registered SuperCam (`SuperCam::SetCamera` 0x427960 stores it at SuperCam+0x4C). This is the render camera. |
  | +0xB0 | ISuperCamTarget* (the player) |
  | +0xC4 | `sim::CollisionObject*` of the camera sphere, named `"camera_with_collision_area_index_%d"` |
  | +0xC8 | `sim::SphereVolume*` (created with radius 2.0; +0x08 = centre) |
  | +0xCC | sim state object; its +0x0C = **0x40001** (camera tag), its +0x08 = SCC* (back pointer) |
  | +0xD0 | float 1.05: collision-offset scale (offset *= 1+1.05) |
  | +0xD4 | `Vector3 offsets[15]` (camera collision offsets) |
  | +0x188 | offset count |
  | +0x18C | `Vector3 groundOffset` |
  | +0x198 | SCC's own controller |
  | +0x1A8 | **IntersectionList (0x48 bytes)**, built in the SCC ctor: `0042865a: push eax(=esi+0x1a8) ; call 0x41af80` |
  | +0x1F0 | flags byte (bit 1 = invert camera X, bit 3 = ...) |

### 1.2 SuperCam base (fields relevant here)  [H unless noted]
Ctor at 0x426F50.

| Offset | Meaning |
|---|---|
| +0x08 | flags: 1 = first update, 2 = cut/snap, 0x200/8 = transition, 0x10000 = "pushed by collision" (WalkerCam), 0x4000, 0x2000 ... |
| +0x44 | SuperCamController* (stick input; `0x435DA0` `EAX`=channel -> `st0`) |
| +0x4C | tPointCamera* |
| +0x50 | FOV (default 1.36345) |
| +0x54 | aspect |
| +0x58 | **near plane** (1.0; re-set to 1.0 by SCC when a cam is selected, `004299b9`) |
| +0x5C | far plane (8000) |
| +0x6C / +0x78 | position / target used during transitions |
| +0xBC | player index |
| +0xC4 | unit direction camera -> target (known) |
| +0xD0 | roll |
| +0xE0 | ISuperCamTarget* (WalkerCam) |

`SuperCam::SetCameraValues` 0x427410: `__stdcall(SuperCam* this, unsigned ms, Vector pos (by value), Vector targ (by value), const Vector* up)`, `ret 0x24`. At the end it writes them into the tPointCamera:
```
004276ef: call 0x5934c0      ; tCamera+0xCC = position (clears +0xC8 "matrices valid")
004276fc: call 0x593460      ; tCamera+0xD8 = target
00427755: call 0x593490      ; tCamera+0xE4 = up
004277e7: call [edx+0xc]     ; SetFOV(+0x50, +0x54)  -> tCamera+0x10/+0x14
004277f3: call [edx+0x14]    ; SetNearPlane(+0x58)   -> tCamera+0x18   (vt 0x549130: mov [ecx+0x18],arg)
004277ff: call [edx+0x1c]    ; SetFarPlane(+0x5C)    -> tCamera+0x1C
```
`SuperCam::GetCameraPosition` 0x4271A0 (`ECX`=this, `EAX`=out): returns `tCamera+0xCC`, or
`this+0x6C` while flags & (0x200|8).

### 1.3 SuperCam vtable slots (WalkerCam vt 0x614C8C)  [H for the addresses, M for the names]

| Slot | Offset | WalkerCam | Base | Meaning |
|---|---|---|---|---|
| 3 | +0x0C | 0x42AD30 | pure | `Update(unsigned ms)` |
| 4 | +0x10 | **0x42B180** | no-op `ret 4` | **`UpdateForPhysics(ms)`**: applies collision results |
| 5 | +0x14 | 0x42ABC0 | | `GetName()` -> "WALKER_CAM" |
| 6 | +0x18 | 0x401150 | | `GetType()` -> 8 |
| 8 | +0x20 | 0x42B580 | | `SetTarget(ISuperCamTarget*)`: loads WalkerCamDataChunk (overrides +0xFC, +0x100) |
| 11 | +0x2C | **0x42ABD0** | `ret 0xC` | **`SetCollisionOffset(const Vector* offs, unsigned n, const Vector& ground)`**: stores to +0x180/+0x184/+0x188 |
| 12 | +0x30 | 0x42ABA0 | returns 2.0 | **collision-sphere radius**; WalkerCam returns +0x58 (near plane) |
| 13 | +0x34 | 0x42ABB0 | returns 1.0 | **IntersectionList fill radius**; WalkerCam returns +0x134 (current distance) |
| 19 | +0x4C | 0x427F00 | | registers/initialises a cam |
| 20 | +0x50 | 0x4269D0 | | creates the controller (0x427B00) |

### 1.4 WalkerCam fields  [H for usage, M for names]
Ctor at 0x42AC10. An embedded WalkerCamData sits at +0xE4 (vt 0x614C84; defaults set at 0x42AB10).

| Offset | Default | Meaning |
|---|---|---|
| +0xE8 | 0.08 | lag for magnitude/elevation smoothing (x timeMod = ms/16) |
| +0xEC / +0xF0 / +0xF4 | 0.3487 / 1.2222 / 0.022 | FOV params (FOV = 70 deg, lag) |
| +0xF8 | 4.26 | **min magnitude** (distance) |
| +0xFC | 9.0 | **max magnitude** (per-character chunk override) |
| +0x100 | 1.36 rad | default elevation phi, from +Y (per-character override) |
| +0x104 | | default rotation |
| +0x108 | 6.63 | default magnitude |
| +0x10C | 0.4 | rotation speed |
| +0x110 | (0,1,0) | **target offset** (world space; target = player pos + this, `0x42CA30`) |
| +0x11C | 0.044 | target lag |
| +0x124 | 2.44 | look-up height |
| +0x130 | | FOV velocity |
| +0x134 | | **current magnitude** (camera <-> target distance) |
| +0x138 | | magnitude velocity |
| +0x13C / +0x140 | | elevation phi / velocity |
| +0x144 / +0x150 | | target pos / vel (known) |
| +0x15C / +0x168 | | camera pos / vel (known) |
| +0x174 / +0x17C | | "airborne" flag / frozen camera Y while jumping |
| +0x180 / +0x184 / +0x188 | | collision offsets ptr / count / ground offset (from SetCollisionOffset) |
| +0x194 | | frame counter of the last collision |
| +0x1A4..+0x1AC | | nearby-pedestrian tracking (re-scanned every 1500 ms, `0x42CA90`) |
| +0x1B0 | | stick X captured when the collision started |
| +0x1B4 / +0x1C0 | | camera pos / magnitude saved when the collision ended |
| +0x1C4 | | optional look-at override |

### 1.5 IntersectionList (RTTI `.?AVIntersectionList@@`, vt 0x61593C, size 0x48)  [H for layout]

| Offset | Contents |
|---|---|
| +0x04..+0x10 | `vector<sim::CollisionObject*>` **statics** (begin +0x08, end +0x0C), at most 50 |
| +0x14 | map of **dynamics** (DynaPhys; key CollisionObject*, value owner), at most 50 |
| +0x28..+0x34 | `vector<CollisionObject*>` **anim-collision** objects (begin +0x2C), at most 50 |
| +0x38..+0x44 | `vector<16-byte 2D segment>` **fences** (x0,z0,x1,z1; begin +0x3C), at most 100 |

Entries are AddRef'd on insert and Released by Clear/dtor.

---------------------------------------------------------------------------------------------------

## 2. Per-frame code path (on foot)

The world-physics substep at 0x4DD560 runs this sequence (first substep `bl`=1):
```
004dd618: call 0x42a300   -> 0x428CD0 SuperCamCentral::Update(SCC, ms, firstSubstep)
004dd62e: mov ebx,[SCC0+8] ; camera collision-area index
004dd63b: call 0x42a2f0   -> 0x429080 SCC::PreCollisionPrep(SCC)       (ground offset, count=0)
004dd64b: call 0x518fb0   ; sim: detect collisions in area ebx
004dd659: call 0x518540   ; sim: solve -> solver agent 0x4DC1D0 -> SCC::AddCameraCollisionOffset
004dd66f: call 0x429bb0   ; SCC::PostPhysics(esi=SCC, push ms)
```

### 2.1 SCC::Update 0x428CD0  [H]
Custom convention: stack `(SCC*, ms, bool first)`, `ret 0xC`.
* **First substep only:** rebuild the IntersectionList around the player.
  ```
  00428f09: mov eax, edi(=SCC+0x1a8) ; call 0x41b100   ; Clear
  00428f1d: call [eax+4]          ; target->GetPosition(&pos)
  00428f28: call [eax+0x34]       ; radius = activeCam->vt+0x34 (WalkerCam: +0x134 magnitude)
  00428f35: call 0x41bdf0         ; FillIntersectionListStatics(ecx=&pos, edx=list, radius)  (also fences)
  00428f6b/98: call 0x41bf60      ; FillIntersectionListDynamics(list,&pos,radius,0,exclude=player or car)
  00428fb0: call 0x41c030         ; FillIntersectionListAnimPhys(ecx=&pos, edi=list, radius)  (+fences)
  ```
* `activeCam->Update(ms)` (vt+0x0C).
* Place the collision sphere for WalkerCam (type 8) or type 0x17:
  ```
  00428fd1: call 0x4271a0         ; p = camera position (tCamera+0xCC)
  00428ffe: call 0x427260         ; d = tCamera.target - tCamera.position
  00429007: call 0x405430         ; normalize
  00429012: call [eax+0x30]       ; r = near plane (1.0)
  ...                             ; p += d*r     (camera sits on the sphere's rear pole)
  0042905b: call 0x428c80         ; sphere centre = p ; CollisionObject flags |= 3 ; update bbox
  00429071: call 0x428cc0         ; sphere radius = r   (jmp 0x519C70)
  ```

### 2.2 SCC::PreCollisionPrep 0x429080 (ground offset)  [H for flow, M for the ground query]
* Clears `SCC+0x188` (count) and `+0x18C` (ground offset).
* Calls `IntersectManager` (singleton `0x6C87A4`) `0x4B59C0`, a vertical ground probe:
  `EDI`=&pos, stack `(IntersectManager*, bool* found, Vector* outNormal, Vector* outPoint)`, `ret 0x10` [M/L].
* If `groundY - (sphere.y - radius) > 0`: `groundOffset = normal * depth / |normal.y|`.

### 2.3 Collision -> offsets  [H]
* Solver agent `WorldCollisionSolverAgentManager` (vt 0x60895C), slot 5 = 0x4DC1D0. If either
  collision object's state has tag `0x40001`, it builds `offset = normal * depth` (sign-corrected)
  and calls `SCC::AddCameraCollisionOffset` **0x429180** (`EDI`=SCC, `ESI`=&offset). Then it
  returns 1 (no physical response).
* 0x429180 drops the offset if the count is already 15 or the offset is about zero. It also drops
  it if it is nearly parallel to an existing offset (dot >= 0.7). Otherwise it scales by
  `1+SCC+0xD0` (= 2.05) and appends to `SCC+0xD4[count++]`.

### 2.4 SCC::PostPhysics 0x429BB0  [H]
```
00429bc2: push SCC+0x18c ; push [SCC+0x188] ; push SCC+0xd4 ; call [eax+0x2c]   ; SetCollisionOffset
00429bdc: jmp  [eax+0x10]                                                       ; UpdateForPhysics(ms)
```

### 2.5 WalkerCam::UpdateForPhysics 0x42B180 (thiscall, ms)  [H]
1. `ground = +0x188`, `n = +0x184`.
2. If n == 0 and the ground offset is about zero: if the 0x10000 state ends, save the position and
   magnitude to +0x1B4/+0x1C0 and return.
3. **If n > 0:** call `0x42C330`.
   * n == 1: `0x42C3C0` sets `camPos(+0x15C) = currentTCameraPos + offs[0]`, sets velocity 0, and
     sets magnitude `+0x134 = |target - camPos|`.
   * n > 1: `0x42C4A0` applies each offset separately, averages the results, then smooths toward
     the average (cubic spring).
   * Then set flag 0x10000 and remember stick X in +0x1B0. While the stick keeps pushing the same
     way, Update zeroes the rotation input, so the camera does not grind into the wall.
4. **If there is a ground offset:** `camPos += ground`, then recompute magnitude.
5. **Reset path** (`0042b2e7..0042b51e`). It runs if `magnitude > 1.2*maxMag`, or the camera is
   more than `minMag` below the target, or the cut flag is set:
   * `desired = target + unit(cam-target)*minMag`
   * `LineOfSight(target, desired, 0.1, staticsOnly=1)` clear -> `cam = desired`, `mag = minMag`.
   * Else `TestIntersectionStatics(target, desired, &hit)`; if `|hit-target| >= minMag` ->
     `cam = hit`, `mag = |hit-target|`.
   * Otherwise `cam = target + (0,2,0)`, `mag = 2`.
   ```
   0042b3c2: push 1 ; push 0x3dcccccd ; push &desired ; lea eax,&target ; mov esi,SCC+0x1a8 ; call 0x41bab0
   0042b433: call 0x41b240        ; esi=&target, ebx=&desired, edi=list, push &hit
   0042b50e: mov [ebp+0x134], 2.0 ; fallback: above the head
   ```
6. `SetCameraValues(this, 0, cam(+0x15C), target(+0x144), 0)`.

### 2.6 WalkerCam::Update 0x42AD30 -> 0x42B730 (position solver)  [H for flow]
* 0x42B730 (`EAX`=this, stack ms, timeMod): converts (camera - target) to spherical coordinates
  (`0x560B50`: cart->sph, `0x560AC0`: sph->cart). It integrates rotation from the stick and
  springs magnitude toward `clamp(..., +0xF8, +0xFC)` and phi toward +0x100.
* **Whisker probes** (`0042baf5..0042bead`). They run only when stick X is idle (`0x42C9D0`), or
  while collision flag 0x10000 is set and the stick is unchanged (`0x42C8F0`). Each probe calls
  `LineOfSight(radius 0.1, staticsOnly=0)`. Points are given in the camera basis
  `M = (target-pos, up)` (built at 0x55FE70):
  * from cam + M*(+0.8, 0, near) to target + M*(+0.5, 0, -0.5): if blocked, rotation -= 0.25;
  * the mirrored left probe: if blocked, rotation += 0.25;
  * an up probe (0, +0.8, near) and a fourth probe from target+(0,0.5,0): if the up probe is clear
    but the fourth is blocked, desired phi -= 0.2 (the camera rises).
  ```
  0042bc1d: call 0x41bab0 ; test al,al ; jne +  ; fsub 0.25 (rotate)
  0042c0fc: fsub [0x617c88]=0.2                 ; raise elevation
  ```
* **"Too close" flip** (`0042c185`):
  ```
  0042c185: fld [edi+0x58] ; fadd 1.5 ; fcomp [ebp(=+0x134)] ; test ah,0x41 ; jne normal
            ; else: rod.x=-rod.x, rod.z=-rod.z  (180 deg around Y), cam = target+rod+groundOffset,
            ;       mag = minMag, velocities 0, fire event 0xBE (= CAMERA_CHANGE per SHARMemory Events)
  0042c249: call 0x432ad0          ; EventManager::TriggerEvent(0xBE, this)
  ```
  If collision has squeezed the camera to under `near + 1.5` = **2.5 m**, WalkerCam jumps to the
  opposite side of Homer.
* Otherwise the camera position is cubic-spring smoothed toward target + rod (lag +0xE8*timeMod).
  Then `SetCameraValues`.

---------------------------------------------------------------------------------------------------

## 3. Reusable primitives (callable from our DLL on the game thread)

All of these were checked against each function's prologue and epilogue and against at least two
game call sites.

| Function | VA | Convention | Returns | Conf |
|---|---|---|---|---|
| `IntersectionList::LineOfSight(start,end,radius,staticsOnly)` | **0x41BAB0** | `EAX`=const Vector* start, `ESI`=IntersectionList*, stack: `[1]`=const Vector* end, `[2]`=float radius, `[3]`=bool staticsOnly; **callee pops 12** (`ret 0xC`). Preserves EBX/EBP/EDI (pushes them); ESI read-only; ECX/EDX clobbered. | `AL`=1 if **clear**, 0 if blocked | H |
| `IntersectionList::TestIntersectionStatics(start,end,&hit)` | **0x41B240** | `ESI`=&start, `EBX`=&end, `EDI`=list, stack `[1]`=Vector* outHit; `ret 4`. EBX/ESI/EDI read-only, EBP saved. | `AL`=1 if hit (outHit written) | H |
| `IntersectionList::FillIntersectionListStatics(center,radius)` | **0x41BDF0** | `ECX`=const Vector* center, `EDX`=list, stack `[1]`=float radius; `ret 4`. Saves EBX/EBP/ESI/EDI. Clears and refills statics **and fences**. | EAX = count | H |
| `FillIntersectionListDynamics` | 0x41BF60 | `__stdcall(list, const Vector* pos, float radius, bool, DSG* exclude)`, `ret 0x14` | count | H |
| `FillIntersectionListAnimPhys` | 0x41C030 | `ECX`=&pos, `EDI`=list, stack radius; `ret 4` (also refills fences) | count | M |
| `IntersectionList::IntersectionList()` | **0x41AF80** | `__stdcall(this)` (`push this`; `ret 4`). Reserves 50/50/50/100 entries with the game allocator 0x445600 (current game heap). | EAX=this | H |
| `IntersectionList::~IntersectionList()` | **0x41B050** | `__thiscall` (`ECX`=this), no args | - | H |
| `IntersectionList::Clear()` | 0x41B100 | `EAX`=this (saves EBX itself) | - | H |
| `TestIntersectionDynamics` | 0x41B490 | `EDI`=&start, `EAX`=&end, stack (list, Vector* out, DSG** outObj); `ret 0xC` | AL | M |

Semantics:
* **LineOfSight** sweeps a sphere of radius `radius` (stored in sim global `0x65C420`; it also sets
  `0x642350=2`) from start to end. It checks the statics vector, then the dynamics map and
  anim-collision vector (unless `staticsOnly`), then the fence segments. Fences are a **2D (XZ)
  segment-crossing test**: they count regardless of height. **Terrain/ground is not tested.**
* **TestIntersectionStatics** checks statics plus fences only, with a **fixed sweep radius of 1.0**
  (`[0x6066B0]`). It returns `start + dir*(t + 1.0)`, the contact point on the surface for a
  head-on hit. Because the swept sphere is 1 m thick, it is **too conservative for tight
  side-walls**: a wall up to about 1 m beside the segment counts as a hit. The game only calls it
  *after* LineOfSight has said "blocked". [M]
* Evidence that LineOfSight's radius argument is `[esp+4c]` and statics-only is `[esp+50]`:
  ```
  0041bafe: mov eax,[esp+0x4c] ; ... mov [0x65c420], eax      ; radius
  0041bb6c: mov al,[esp+0x50]  ; test al,al ; jne 0x41bc2b    ; skip dynamics/anim
  0041bc7e: xor al,al ; ret 0xc    /  0041bc8b: mov al,1 ; ret 0xc
  ```

### 3.1 Option A (preferred): reuse SCC's list (no allocation, no lifetime issues)
* `list = *(DWORD*)0x6C906C + 0x1A8` (player 0).
* It is valid in gameplay whenever the SCC exists. Check that `[SCC+0x8C]` has vtable 0x614C8C to
  know the WalkerCam is the one filling it.
* Contents are rebuilt each frame on the first physics substep:
  * centre = player position (`target->vt+4`, i.e. the feet);
  * radius = WalkerCam magnitude `[WalkerCam+0x134]` (normally 4.26-9);
  * the player is excluded from dynamics.
* At BeginScene all substeps are done, so the list matches this frame's player position, and the
  game does not touch it again until the next frame's SCC::Update.
* Coverage caveat: if WalkerCam has been squeezed to about 2.5 m, small objects 2.5-3.1 m from the
  feet may be missing (large static volumes are still found). If
  `|camDesired - playerPos| > [WalkerCam+0x134]`, use option B.

### 3.2 Option B: private list per frame (exactly what the game does at 0x459C20 and 0x4F34B0)
```
ctor(&il)                       ; 0x41AF80
FillStatics(&mid, &il, halfLen + 1.0f)   ; 0x41BDF0, mid = (P+C)/2   (0x459F97 uses mid and |P-C|/2)
... LineOfSight / TestIntersectionStatics on &il ...
dtor(&il)                       ; 0x41B050
```
* Construct and destroy within the same frame; do not keep it across frames. It holds AddRef'd
  sim objects, and its vector storage comes from the game heap that is current at construction.
* Cost per frame: a handful of small allocations, plus an 800-byte temporary in FillStatics.

### 3.3 Thunks for the mod (llvm-mingw clang, i686, AT&T syntax, cdecl on our side)
```cpp
// returns 1 if the swept segment is clear
__attribute__((naked)) static int IL_LineOfSight(void* list, const float* start, const float* end,
                                                 float radius, int staticsOnly) {
    __asm__ volatile(
        "pushl %esi\n\t"
        "movl  8(%esp), %esi\n\t"     // list
        "movl  12(%esp), %eax\n\t"    // start
        "pushl 24(%esp)\n\t"          // staticsOnly   (push [esp+X] uses esp before the decrement)
        "pushl 24(%esp)\n\t"          // radius
        "pushl 24(%esp)\n\t"          // end
        "movl  $0x41BAB0, %ecx\n\t"
        "call  *%ecx\n\t"             // callee pops 12
        "movzbl %al, %eax\n\t"
        "popl  %esi\n\t"
        "ret\n\t");
}
// returns 1 on hit, writes hit point (surface point; sweep radius fixed 1.0)
__attribute__((naked)) static int IL_TestStatics(void* list, const float* start, const float* end,
                                                 float* outHit) {
    __asm__ volatile(
        "pushl %ebx\n\t" "pushl %esi\n\t" "pushl %edi\n\t"
        "movl  16(%esp), %edi\n\t"    // list
        "movl  20(%esp), %esi\n\t"    // start
        "movl  24(%esp), %ebx\n\t"    // end
        "pushl 28(%esp)\n\t"          // outHit
        "movl  $0x41B240, %eax\n\t"
        "call  *%eax\n\t"             // callee pops 4
        "movzbl %al, %eax\n\t"
        "popl %edi\n\t" "popl %esi\n\t" "popl %ebx\n\t"
        "ret\n\t");
}
__attribute__((naked)) static int IL_FillStatics(void* list, const float* center, float radius) {
    __asm__ volatile(
        "movl  4(%esp), %edx\n\t"     // list
        "movl  8(%esp), %ecx\n\t"     // center
        "pushl 12(%esp)\n\t"          // radius
        "movl  $0x41BDF0, %eax\n\t"
        "call  *%eax\n\t"             // callee pops 4, saves ebx/esi/edi/ebp
        "ret\n\t");
}
__attribute__((naked)) static void IL_Construct(void* list) {
    __asm__ volatile("pushl 4(%esp)\n\t" "movl $0x41AF80, %eax\n\t" "call *%eax\n\t" "ret\n\t");
}
__attribute__((naked)) static void IL_Destroy(void* list) {
    __asm__ volatile("movl 4(%esp), %ecx\n\t" "movl $0x41B050, %eax\n\t" "call *%eax\n\t" "ret\n\t");
}
// private list storage: alignas(8) static unsigned char g_il[0x48];  (or a stack local)
```
Guard these behind the same English-version check the mod already uses (all addresses are
English-only).

### 3.4 Suggested aim-camera algorithm, mirroring WalkerCam's constants
1. Start the sweep from a point that is surely free: Homer's head pivot (`Character+0x64` +
   (0, 1.4-1.6, 0)).
   * If the side offset is large, do two legs: head -> shoulder point, then shoulder -> camera.
     This mirrors WalkerCam's whiskers, which start near the target.
2. Free distance = **binary search with `IL_LineOfSight(list, P, P + dir*t, r=0.2-0.3, staticsOnly=1)`**,
   6-8 iterations. This is a native sphere-cast with your own radius.
   * Optionally seed it with `IL_TestStatics` when LineOfSight first says "blocked".
   * `staticsOnly=1` matches WalkerCam's reset and FollowCam usage. 0 would also pull in for cars
     and NPCs.
3. `camDist = clamp(free - margin(0.2), minDist, desired)`.
   * **Pull in instantly**; ease out with lag (WalkerCam uses a cubic spring, `0x420450`, with lag
     about 0.08*ms/16).
4. Keep **camDist >= near + 0.3 (about 1.3 m)**: the near plane is **1.0 m** (`tCamera+0x18`), so
   anything closer clips Homer's mesh.
   * If there is not enough room, do what WalkerCam does in spirit: swap shoulder (its analogue is
     the 180-degree flip at < 2.5 m) or raise the camera (its whisker "raise elevation").
   * Alternatively, temporarily write a smaller near plane into `tCamera+0x18` during the override.
     The game re-sends near/far to pddi each frame from the camera (vt+0x64 at 0x593AC0), but depth
     precision gets worse (z-fighting risk) [M].
5. Ground: LineOfSight ignores terrain. Keep the mod's existing "never below feet + 0.35 m" clamp,
   or probe the ground with `0x4B59C0` (convention [L], see 2.2).
6. Run it at BeginScene, right before writing the override. That is the same thread and the same
   moment the mod already uses; the spatial queries only read the world tree.

---------------------------------------------------------------------------------------------------

## 4. "Drive the game's WalkerCam instead?" - not recommended  [H for the facts]
* Its knobs exist: min/max/default magnitude `+0xF8/+0xFC/+0x108`, elevation `+0x100`,
  world-space target offset `+0x110`, heading via `+0x15C/+0xC4` (the mod already does this). You
  could read back `+0x15C` (collided position) and `+0x134` (collided distance) after
  UpdateForPhysics.
* But:
  1. The "too close" rule flips the camera 180 degrees whenever distance < `near+1.5` = 2.5 m. The
     aim cam lives at 1.9-2.6 m, so it would flip constantly and fire `CAMERA_CHANGE` events.
  2. Collision offsets are applied to the **render camera's current position** (`0x4271A0` reads
     `tCamera+0xCC`). That is shared state the mod overrides.
  3. Whiskers auto-rotate and raise the camera while the stick is idle.
  4. The target offset is world-space, not camera-relative.
* Cheap heuristic only: since the mod already turns WalkerCam to the aim heading, `[WalkerCam+0x134]`
  after the frame is the game's own "how much room is behind Homer on this heading" value.
  `min(aimDist, k*mag)` can serve as a fallback hint. [M] It does not account for the shoulder offset.

---------------------------------------------------------------------------------------------------

## 5. Related systems

### 5.1 Player hidden when the camera is close  [H]
`0x4FE560`: `__stdcall(CharacterRenderable* r, Vector sphereCentre (by value), X)`, `ret 0x14`.
Called from Character::Display (Character vt slot 3, 0x4F7BC0) at `0x4F807C`, with
`Character+0x54` = StaticPhysDSG bounding-sphere centre (`+0x64` is the position).
```
004fe5a1: call 0x42a2d0 ; mov eax,[eax+0xac]        ; SCC(0)->camera (tPointCamera)
004fe5b0: lea ecx,[eax+0xcc]                         ; camera POSITION field
...       d2 = |centre - camPos|^2
004fe5fc: call [edx+0x18]                            ; tCamera::GetNearPlane (fld [ecx+0x18])
004fe5ff: fadd 1.5 ; square ; fcomp ; jnp skip       ; if d2 < (near+1.5)^2 -> not drawn
004fe6cd: fcomp 900 / 6400                           ; LOD 2/1/0 by distance (30 m, 80 m)
```
So the hide test is `|tCamera+0xCC - Character+0x54| < tCamera+0x18 (1.0) + 1.5`. This explains
the mod's `kCullBack = 1.5 m` trick. It is the same 2.5 m number as WalkerCam's flip threshold.
There is **no fade/alpha** path; it is all or nothing.

### 5.2 Character view-cone cull
Uses position and target (`+0xCC`/`+0xD8`), as the mod found empirically. Not located precisely.

### 5.3 Near plane and FOV
* WalkerCam near plane = 1.0 (SuperCam+0x58, forced to 1.0 on cam selection at `0x4299AD`), far 8000.
* tPointCamera vtable 0x5F8994: +0x0C SetFOV(fov, aspect), +0x10 GetFOV, +0x14 SetNear, +0x18
  GetNear, +0x1C SetFar, +0x50 SphereVisible(&pt, r), +0x64 SetupProjection (pushes near/far/fov/
  aspect to pddi).
* `tCamera+0xC8` = "matrices valid" byte. The setters clear it and the getters rebuild via vt+0x68.

### 5.4 Not found
No "look-through" or transparency system for walls or the player.

---------------------------------------------------------------------------------------------------

## 6. Uncertain / not verified
* The exact Minkowski sweep semantics of the sim ray test `0x516A60` (sphere/cylinder/OBB inflated
  by `[0x65C420]`; `t*len` returned) [M].
* Which world objects the camera sphere's collision area contains (it is managed by
  the world-physics object; not traced) [L]. In practice WalkerCam is pushed off buildings and walls.
* The ground probe `0x4B59C0` argument roles [L].
* The `IntersectManager` query helpers used by the fills [M]:
  * statics 0x4B4C00
  * fences 0x4B49C0
  * dynamics 0x4B4DF0
  * anim 0x4B4FF0

## 7. Scripts (scratchpad)
* `sharre.py`: PE/RTTI/disassembly helpers; full-coverage linear sweep
* `rawcall.py`: raw E8/E9 xrefs
* `t2.py`: vtable comparison
* `t3.py`: function disassembly
* Dumps: `walker_update.txt`, `f_42b730.txt`, `f_42b180.txt`, `f_428cd0.txt`, `f_427410.txt`,
  `f_followcam.txt`, `f_chardisplay.txt`
