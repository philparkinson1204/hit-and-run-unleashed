# SHAR (English Simpsons.exe, base 0x400000) - character hit reactions and the Hit & Run meter

Static analysis only (pefile + capstone over the exe on disk; the game was never launched or attached).
Helper scripts are in the scratchpad: `sharlib.py` (RTTI vtable index, xrefs, jump-table decoder), `addl.py` (every
`EventManager::AddListener` call mapped to event IDs), `trig.py` (every `TriggerEvent` call site), `listeners.py`.

Confidence tags: **[H]** read directly from code; **[M]** inferred from code plus naming or context; **[L]** educated guess.

Many internal functions use non-standard register conventions (the exe was built with whole-program optimisation).
This report states the exact register or stack use for each function that matters.

---------------------------------------------------------------------------------------------------------------------

## 0. Quick reference (all English addresses)

| Thing | Address / offset | Convention / notes | Conf |
|---|---|---|---|
| EventManager instance | `[0x6C9018]` (`GetInstance` 0x4329A0 is `mov eax,[6C9018]; ret`) | | H |
| `EventManager::TriggerEvent` | 0x432AD0 | **edx = EventManager\***, stack: `event`, `param`; callee pops 8 (`ret 8`); returns the listener count; keeps ebx/esi/edi/ebp | H |
| `EventManager::AddListener` | 0x4329E0 | ebx = EventManager\*, edi = event, stack: `EventListener*`; `ret 4` | H |
| `EventManager::RemoveListener` | 0x432A60 | ebx = EM, edi = event, eax = listener | H |
| Listener table | EM + event\*16 (std::vector: begin +4, end +8); 0x14B = **331 slots** (matches the SHARMemory enum count, so the ID mapping is right) | each listener gets `vtbl[1](event,param)` (__thiscall) | H |
| HitnRunManager instance | `[0x6C84E0]` (GetInstance 0x4D8650) | vtable 0x608D3C | H |
| `HitnRunManager::HandleEvent` | 0x4D8B00 (= vtbl[1]) | `__thiscall(this, int event, void* param)`, `ret 8` | H |
| `HitnRunManager::Init` (AddListener list) | 0x4D88B0 | esi = this | H |
| `HitnRunManager::Update` | 0x4D9360 | stack `(this, int elapsedMs)`, `ret 8`; called from GameplayContext::OnUpdate | H |
| `HitnRunManager::Reset` (ResetHitAndRun) | 0x4D8A90 | eax = this | H |
| Set meter (script SetHitAndRunMeter) | 0x426690 | `__thiscall(float)`: only does `[this+4] = v` (generic folded setter) | H |
| InteriorManager instance | `[0x6C8FF8]` (getter 0x43A850); `+0x08` EntryRequested, `+0x28` In | | H |
| GameplayManager instance | `[0x6C8998]` (getter 0x448080); ChaseManager(i) = 0x44A8D0 (eax = i, ecx = GPM; `[GPM + 0x340 + i*0x54]`) | | H/M |
| CoinManager getter | 0x505300 | | M |
| CharacterTune KickingForce / SlamForce | `[0x6C8380]` = 400.0, `[0x6C837C]` = 800.0 (defaults set at 0x4FA898) | | H |
| Character vtable / NPCharacter vtable | 0x607E8C (54 slots) / 0x607FF4 (55 slots) | | H |
| `Character::GetAIRef` (vtbl[20]) | player = **0x20001**, NPC = **0x20002** (vehicle = 1, props = 0x40002 etc.) | | H |
| `Character::GetSimState` (vtbl[22]) | 0x4A5820: `mov eax,[ecx+0x70]` | | H |
| `DynaPhysDSG::AddToSimulation` (vtbl[35]) | 0x49FB60 | `__thiscall(void)`; calls SimState::SetControl(1) and adds ground-plane collision | H |
| NPCharacter `ApplyForce` (vtbl[36]) | 0x4F84A0 | `__thiscall(const rmt::Vector* dir, float force)`, `ret 8`; skipped if InCar; the **player's slot 36 is a no-op** (0x401D80 `ret 8`) | H |
| NPCharacter `ApplyKickForce` (vtbl[54], NPC only) | 0x4F84C0 | same signature; ApplyForce, then seeds the rest-test smoothers | H |
| `DynaPhysDSG::ApplyForce` (body) | 0x49FBA0 | same signature; can be called directly on any DynaPhysDSG, including the player | H |
| `sim::SimState::SetControl(int)` | 0x518020 | `__thiscall(int)`, `ret 4`; 0 = AI/animation, 1 = physics simulation | H |
| `Character::Kick` | 0x4F34B0 | `__stdcall(Character* kicker)` (`ret 4`) | H |
| `Character::Slam` (stomp) | 0x4F37A0 | `__stdcall(Character*)` | H |
| `Character::Shock` (wasp zap) | 0x4F7A60 | **esi = Character\***, stack `float seconds`, `ret 4` | H |
| `CharacterController::SetIntention` | controller vtbl[9] (controller = `[char+0x100]`) | `__thiscall(int)`; Dodge = 15, Cringe = 16 | H |
| ProjectileDSG on-hit (wasp bolt) | 0x41D8E0 | eax = projectile, edi = hit object | H |

Character field offsets used below. The offsets were confirmed against code; the names come from SHARMemory Character.cs.

`+0x64` position · `+0x70` sim::SimState\* · `+0x80/+0x84` PastLinear smoother (value, factor) · `+0x88/+0x8C` PastAngular smoother ·
`+0x90` GroundPlaneIndex · `+0x98` CollidedWithVehicle · `+0xE4` **IsNPC** · `+0x100` CharacterController\* · `+0x104` CharacterRenderable\* ·
`+0x108` choreo::Puppet\* · `+0x10C` FacingDir · `+0x110` DesiredDir · `+0x118` Velocity · `+0x128` **InCar** ·
`+0x140` ActionController\* · `+0x15C` TargetVehicle · `+0x190` **CharacterAi::StateManager\*** · `+0x2A4` WalkerLocomotionAction ·
`+0x2A8` JumpAction · `+0x2B4` ParentTransform · `+0x360` Busy · `+0x368` TimeLeftToShock · `+0x36C` IsBeingShocked ·
`+0x39D` **HasBeenHit** · `+0x3A4` TooFarToUpdate.

sim::SimState fields: `+0x0C` AIRef · `+0x10` transform (position at `+0x40`) · **`+0x50` linear velocity** · **`+0x5C` angular velocity** ·
**`+0x6C` control (0 = AI, 1 = simulation)** · `+0x70` physics object · `+0x74` collision object. **[H]** (from SetControl, ResetVelocities at 0x517980, and ApplyForce.)

---------------------------------------------------------------------------------------------------------------------

## A. Character hit reactions

### A.0 The one native knock-down mechanism (shared by kick, car hit and wasp bolt)

There is **no KnockDown / Stumble / Ragdoll class**. All `.?AV...Action@@` classes:
GetUpAction 0x615FF8, FlailAction 0x616020, CringeAction 0x61609C, DodgeAction 0x6160EC, KickAction 0x61647C, JumpAction,
SurfAction, InCarAction, CarDoorAction, Accel/Decel/Reverse/SteerAction, Walker­LocomotionAction, PlayAnimation/HoldAnimation/
PlayIdleAnimationAction, ReleaseDoorsAction, DelayAction, TriggerEventAction, ChangeNPCControllerState. **[H]**

Knock-down means "the character's SimState goes into physics control with a velocity". The flow:

1. Something changes velocity at `SimState+0x50` (and optionally spin at `+0x5C`), then calls `SimState::SetControl(1)`,
   usually through `AddToSimulation` (vtbl[35]) or `ApplyForce`. **[H]**
2. Every update, `Character` vtbl[37] (0x4F4290) runs:
   ```
   004f43e9: mov ecx,[edi+0x70]        ; SimState
   004f43ec: cmp dword ptr [ecx+0x6c],1 ; physics control?
   004f43f2: mov eax,[edi+0x190]       ; StateManager
   004f43f8: call 0x40a6b0             ; GetState()
   004f43fd: cmp eax,5                 ; already InSim?
   004f4408: call 0x4f8570             ; -> SetState<CharacterAi::InSim>
   ```
   **[H]**
3. `CharacterAi::InSim::Enter` (0x40A170) clears the ActionController and queues **FlailAction** ("flail" anim, looping,
   sets Busy at +0x360). It then queues **GetUpAction** ("get_up" anim; the new heading comes from the previous sim transform). **[H]**
4. `FlailAction::Update` (0x40DCD0) finishes when `SimState->control == 0`. The physics "rest test" (DynaPhysDSG/Character
   vtbl[32], 0x49F800) sets control back to 0. It compares the smoothed speeds at +0x80 and +0x88 with thresholds: NPC linear < 0.5
   and angular < 0.7; player < 1.0 and < 1.5. The body must also be near the ground. It then calls SetControl(0), ResetVelocities and vtbl[33]. **[H]**
5. GetUp plays. If the character is hit again during GetUp (control goes back to 1), `GetUpAction::Update` (0x40DDE0)
   queues Flail and GetUp again. When the actions finish, the state goes back to Loco. **[H]** (The state-switch detail is **[M]**.)

CharacterAi state IDs come from each state's vtbl[5]: **NoState 0, Loco 1, InCar 2, GetIn 3, GetOut 4, InSim 5**. **[H]**
To read the state: `sm = *(char**)(c+0x190); st = *(void**)(sm+4); id = vtbl(st)[5](st)` (thiscall, no args).
`sm+8` holds the pending state. **[H]**

ActionController (`[c+0x140]`): `+4` = executing Sequencer\* (so the current action is `seq+0x14`, matching your note),
`+8` = Sequencer\* used to queue new sequences, with inline sequencers at +0x0C and +0x4E0.
BeginSequence 0x40E5B0 (eax = seq), AddAction 0x40E5F0 (eax = seq, ecx = action, stack: start, duration = -1.0f, `ret 8`),
EndSequence 0x40E5E0. Actions are allocated from a game pool (0x445490 with ebx = 0x6CF6B8). **Do not build actions yourself.**
Use the physics path, which the game turns into Flail/GetUp on its own. **[H]**

### A.1 Player kick hits a pedestrian

* Callers: `KickAction::Update` (vtbl[6], 0x40DFC0), JumpAction (jump-kick), and ActionButton::DestroyObject all call
  **`Character::Kick` 0x4F34B0 (`__stdcall(Character* kicker)`)**. **[H]**
* Inside: if `InteriorManager.In` or `EntryRequested` is set, nothing happens. Otherwise it gets the kicker's position and facing
  (0x40A790), tilts the facing **45 degrees upward** (rotation around cross(up, facing)), collects nearby dynamic objects
  (IntersectionList fill 0x41BF60, radius 5.0, excluding the kicker), and picks the hit object with a segment test (0x41B490). **[H]**
  (The exact geometry of the search is **[M]**.)
* The code switches on the victim's `GetAIRef()`:
  ```
  004f3669: call [edx+0x50]          ; victim->GetAIRef()
  004f366f: sub eax,0x20001 / je 0x4f372d   ; 0x20002 = NPC
  004f372d: push esi; push 0xA8  -> TriggerEvent(KICK_NPC, victim)
  004f373f: push esi; push 0xE1  -> TriggerEvent(KICK_NPC_SOUND, victim)
  004f3751: push 0;   push 0xCD  -> TriggerEvent(PEDESTRIAN_SMACKDOWN, 0)
  004f3764: mov eax,[esi+0x190]
  004f376a: mov byte ptr [esi+0x39d],1   ; HasBeenHit (set AFTER the events -> coins only on first hit)
  004f3771: call 0x40a6b0                 ; AI state
  004f3776: push [0x6c8380]               ; KickingForce (400)
  004f377d: push &kickDir
  004f3781: cmp eax,1                     ; Loco?
  004f378f: call [eax+0xd8]   (Loco)  else  call [eax+0x90]   ; NPCharacter vtbl[54] / vtbl[36]
  004f36df: push esi; push 0x12A -> TriggerEvent(OBJECT_KICKED, victim)
  004f36f1: push ebp; push 0x5A  -> TriggerEvent(KICK, kicker)
  ```
  **[H]**
* `NPCharacter::ApplyForce` (vtbl[36], 0x4F84A0) does nothing if `InCar` (+0x128). Otherwise it calls `DynaPhysDSG::ApplyForce`
  (0x49FBA0). That function: if SimState control is 0 it resets the velocities; mass = `CollisionAttributes(+0x30)->mass(+0x50)`, or
  **100 when there are no CollisionAttributes** (characters appear to have none, so the kick gives Δv = 400/100 = 4 m/s) **[M]**;
  then `vel(+0x50) += dir * force/mass`; then vtbl[35] AddToSimulation, which calls SetControl(1) and enables ground-plane collision
  if `+0x90 >= 0`. **[H]**
* `ApplyKickForce` (vtbl[54], 0x4F84C0) does the same, then writes |linear velocity| to +0x80 and |angular velocity| to +0x88.
  This seeds the rest-test smoothers so the body is not judged "at rest" right away. **Prefer it for NPCs in Loco.** **[H]**
* Resulting state: InSim, Flail, GetUp (A.0). This works only on NPCs: the kick code acts only on AIRef 0x20002, and the
  player's vtbl[36] is a no-op. **[H]**
* Stomp (`Character::Slam` 0x4F37A0, radius 2.0) fires the same KICK_NPC / KICK_NPC_SOUND / PEDESTRIAN_SMACKDOWN /
  OBJECT_KICKED events and sets HasBeenHit, but **applies no force itself**. Then STOMP(kicker). **[H]**

### A.2 Car hits a pedestrian (PLAYER_CAR_HIT_NPC, PC_NPC_COLLISION)

Code: `Character::PreReactToCollision` (Character/NPCharacter vtbl[18], 0x4F55C0, `__thiscall(SimState* other, sim::Collision&)`). **[H]**
* PC_NPC_COLLISION (0xDE) fires when the player character (AIRef 0x20001) bumps an NPC on foot (param = NPC). **[H]**
* Vehicle (AIRef 1) vs character, where the character is in Loco or InSim and `vehicle+0x148` (speed) > 1.0:
  * If the character is an NPC and the car is the player's car: TriggerEvent(PLAYER_MAKES_LIGHT_OF_CAR_HITTING_NPC 0xE0, 0),
    then TriggerEvent(PLAYER_CAR_HIT_NPC 0xDF, npc), then HasBeenHit = 1. **[H]**
  * For any car: `SimState::SetControl(1)` plus ground-plane registration. Only for **traffic** cars (`vehicle+0xA4 == 2`) is velocity also set:
    `lin += collisionNormal + carVelocity(+0x118) + (0, (6..11)*k, 0)` and `ang += (6..11)*k` on each axis, where `k = carSpeed/28`.
    Other cars push the body through the normal collision response. **[H]**
  * Player victim (AIRef 0x20001) hit by an **AI** car (type 1) and not InCar: the code checks the normal against the facing,
    calls vtbl[35] AddToSimulation (player knocked into InSim), and if the car is a chase car (ChaseManager 0x504760)
    fires HIT_AND_RUN_CAUGHT. So **the player can be knocked down natively**. **[H]**

### A.3 Wasp camera bolts (closest native "bullet hits character")

`ProjectileDSG` hit handler 0x41D8E0 (eax = projectile, edi = hit object). It is called from the projectile update (0x41D530) after
an IntersectionList segment test (0x41B490). Bolts **ignore the player while the player is jumping** (0x41DB60 checks AIRef 0x20001 and
IsJump at +0x33D). **[H]**
* **Player (0x20001):** `CoinManager::LoseCoins(5, playerPos)` (0x505BC0), then **`Character::Shock(0.5f)`** (0x4F7A60). Shock sets
  TimeLeftToShock (+0x368) = 0.5, IsBeingShocked (+0x36C) = 1 and `renderable(+0x104)->+0x50 = 1` (the X-ray zap look). While shocked,
  desired movement is forced to zero (0x4F43B2), so the character is stunned. A timer clears it (0x4F5020); for the player it also drives a
  0..1 intensity on 0x4A8E60()+0x6C (probably rumble). Shock fires WASP_BULLET_HIT_CHARACTER... (0xFD, param 0) and KICK_NPC_SOUND
  (0xE1, param = character). **No knock-down and no Hit&Run.** **[H]**
* **NPC (0x20002), a no-event knock-down:**
  ```
  0041d96b: call [edx+0x58]                 ; npc->GetSimState()
  0041d979: add esi,0x50                    ; &linVel
  0041d984: dir = (0, 0.7071, 0.7071) rotated by the projectile matrix (0x560360)   ; forward + 45 deg up
  0041d9aa: vel += dir * 10.0
  0041d9e6: call [edx+0x8c]                 ; npc->AddToSimulation()
  ```
  The result is InSim, Flail, GetUp. Nothing is triggered: no heat, sound or dialog. **[H]**
* Traffic car: `Vehicle::TriggerDamage(0.1, true)` (0x4EDF90). Props: `vtbl[36] ApplyForce(dir, 400)`, except crates and vending machines. **[H]**

### A.4 HIT_HEAD and flinches

* **HIT_HEAD (0xE2)** fires only from `Character` vtbl[39] (0x4F4DD0). That is the ground/ceiling probe: during a jump, a ceiling hit
  fires HIT_HEAD(character) and stops the upward motion. **No listeners register for it**, and it is not a hit reaction. **[H]**
* **Cringe / Dodge** are the native "flinch" moves. `CharacterController` intention values (SHARMemory enum, confirmed by code):
  Jump 4, Attack 6, **Dodge 15, Cringe 16**. The Loco state (0x4078C0) reads `controller+0x0C`, then copies `+0x10` into `+0x0C`. Its effects:
  * 16 means queue [ChangeNPCControllerState(3) if NPC], then **CringeAction** ("cringe"), then [ChangeNPCControllerState(0) if NPC]. **[H]**
  * 15 means ChangeNPCControllerState(2), then **DodgeAction** ("dodge", sets Busy), then ChangeNPCControllerState(0). **[H]**
  * The game uses this from `NPCController` vtbl[19] (0x4F9940): a 50/50 Cringe (0x4FA0E0) or Dodge (0x4FA1B0; fires PEDESTRIAN_DODGE)
    when a car comes at an NPC. **[H]**
  * How to call it: `ctrl = *(void**)(c+0x100); vtbl(ctrl)[9](ctrl, 16)`. The base setter needs `Active` (+0x14). The player's setter
    (0x4F8960) refuses while InSim. It takes effect on the next Loco update. **[M]** (Whether the NPC AI overwrites the intention in the same frame was not traced.)

### A.5 Recommended "you were shot" for the mod (game thread, with a direction)

Gates for any victim: the pointer must be a live entry in CharacterManager (`[0x6C8470]+0xC0`, 64 pointers); `*(void**)c` is
0x607FF4 (NPC) or 0x607E8C (player); `!c[0x128]` (not InCar); `ss = [c+0x70]` is non-null and `[ss+0x70]` (physics object) is non-null;
the AI state is **Loco (1)** or **InSim (5)** (skip InCar, GetIn and GetOut). SetControl(1) silently refuses if the physics object reports
zero mass. Very distant NPCs (TooFarToUpdate, more than about 25 m) only change state once they are updated again. **[H]/[M]**

```c
typedef void (__attribute__((thiscall)) *ApplyForce_t)(void* self, const float* dir3, float force);
typedef int  (__attribute__((thiscall)) *GetId_t)(void* state);
typedef void (__attribute__((thiscall)) *Void_t)(void* self);

static int AiState(char* c){ char* sm=*(char**)(c+0x190); if(!sm) return -1;
    void* st=*(void**)(sm+4); if(!st) return -1; return ((GetId_t)(*(void***)st)[5])(st); }

// NPC: identical to what the kick does (Δv = force/mass; mass is about 100 -> 400 gives about 4 m/s; 1000-1500 is a stronger shot)
static void ShotKnockNPC(char* npc, const float shotDir[3], float force){
    if (*(void**)npc != (void*)0x607FF4 || npc[0x128]) return;
    char* ss=*(char**)(npc+0x70); if(!ss || !*(void**)(ss+0x70)) return;
    int st=AiState(npc); if(st!=1 && st!=5) return;
    float h=sqrtf(shotDir[0]*shotDir[0]+shotDir[2]*shotDir[2]); if(h<1e-4f) h=1e-4f;
    float d[3]={ 0.7071f*shotDir[0]/h, 0.7071f, 0.7071f*shotDir[2]/h };   // 45 deg up, like the kick and the wasp bolt
    void** vt=*(void***)npc;
    ((ApplyForce_t)vt[st==1 ? 54 : 36])(npc, d, force);   // 54 = ApplyKickForce (seeds rest smoothers)
}

// Wasp-bolt style (no mass dependence): vel += dir*10; AddToSimulation
static void ShotKnockNPC_Projectile(char* npc, const float d[3], float speed /*10*/){
    char* ss=*(char**)(npc+0x70); float* v=(float*)(ss+0x50);
    v[0]+=d[0]*speed; v[1]+=d[1]*speed; v[2]+=d[2]*speed;
    ((Void_t)(*(void***)npc)[35])(npc);
}

// Player knock-down (vtbl[36] is a no-op, so call the DynaPhysDSG body directly). [M]: the game itself puts the
// player in InSim via AddToSimulation when an AI car hits him, but an impulse-driven player knock-down is untested.
static void ShotKnockPlayer(char* pc, const float d[3], float force){
    ((ApplyForce_t)0x49FBA0)(pc, d, force);
    char* ss=*(char**)(pc+0x70);                    // seed rest smoothers like ApplyKickForce does
    float* v=(float*)(ss+0x50), *w=(float*)(ss+0x5C);
    *(float*)(pc+0x80)=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    *(float*)(pc+0x88)=sqrtf(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
}

// Light hit / flinch without knock-down
static void ShotFlinch(char* c){ void* ctrl=*(void**)(c+0x100);
    if(ctrl) ((void(__attribute__((thiscall))*)(void*,int))(*(void***)ctrl)[9])(ctrl, 16 /*Cringe*/); }

// Stun / zap (wasp look). esi-based convention, so it needs inline asm. It fires event 0xFD (zap sound + dialog) and 0xE1 (victim dialog).
static void Shock(void* c, float secs){ union{float f; unsigned u;} s={secs};
    __asm__ __volatile__("pushl %[s]\n\tcall *%[fn]" : "+S"(c)
        : [s]"r"(s.u), [fn]"r"((void*)0x4F7A60) : "eax","ecx","edx","memory","cc"); }
```

Native side effects to add on purpose (choose per shot):
* `HnR_HandleEvent(KICK_NPC, npc)` adds heat only (+10, see B). Call it **before** `npc[0x39D]=1` if you want first-hit coins (KickNPCCoins defaults to 0).
* `TriggerEvent(KICK_NPC, npc)` adds the heat plus the "car_hit_pedestrian" SFX.
* `TriggerEvent(KICK_NPC_SOUND, npc)` makes the victim say its "HitByW" (hit by walker) line (DialogCoordinator table, 2.5 s rate limit).
* `TriggerEvent(OBJECT_KICKED, npc)` makes **pedestrians within 12 m of the player panic** (PedestrianManager sets NPCController state 6, run
  away), and wasp cameras with the ATTRACTION behaviour home in. No heat, because characters have no CollisionAttributes **[M]**.
* `TriggerEvent(PEDESTRIAN_SMACKDOWN, 0)` makes the player taunt ("Char" line) and attracts wasps.
* Then set `npc[0x39D] = 1` (HasBeenHit) like the kick does.

---------------------------------------------------------------------------------------------------------------------

## B. Hit & Run meter (HitnRunManager)

### B.1 Object and defaults (constructor 0x4D8680; no tune file, only script commands change a few fields) [H]

| off | field | default |
|---|---|---|
| +0x04 | CurrHitAndRun | 0 (**range 0..100**, clamped at the end of HandleEvent: `[0x604D2C]=100`) |
| +0x08 / +0x0C / +0x10 | DecayRatePerSecond / WhileSpawning / Inside | 1.0 / 6.0 / 10.0 (level scripts: `SetHitAndRunDecay(1.0..3.0)`) |
| +0x14 / +0x18 | VehicleDestroyed delta / coins | 25 / 10 |
| +0x1C | VehicleHitDelta | 5 |
| +0x20 / +0x24 | HitBreakable delta / coins | 7.5 / 1 |
| +0x28 / +0x2C | HitMoveable delta / coins | 7.5 / 1 |
| +0x30 / +0x34 | KrustyGlass delta / coins | 0 / 5 |
| +0x38 / +0x3C | ColaProp delta / coins | 0 / 10 |
| +0x40 / +0x44 | **KickNPC delta / coins** | **10 / 0** |
| +0x48 / +0x4C | **PlayerCarHitNPC delta / coins** | **13 / 0** |
| +0x50 | BustedCoins | 50 |
| +0x54 / +0x58 | SwitchSkin / ChangeVehicle delta | -100 / -100 |
| +0x60..+0x9F, +0xA0 | PrevHits[16] ring + index (objects already counted) | |
| +0xA4 / +0xA5 | ChaseOn / SpawnOn | |
| +0xA8 / +0xAC / +0xB0 | DecayDelayMS (countdown) / DecayDelay / DecayDelayWhileSpawning | 0 / **3000 ms** / 0 |
| +0xB4 | NumChaseCars | 1 (scripts: 1-2) |
| +0xB8 / +0xB9 | **HitnRunDisabled** / DecayDisabled | |
| +0xBC/+0xC0/+0xC4/+0xC8 | CopTicketDistance / OnFoot / SpeedThreshold / TimeThreshold | 10 / 10 / 30 / 0.75 s |
| +0xF0 | ignored vehicle (mission vehicle), set by 0x4D98E0 | |
| +0xF4 | throb threshold | 0.78 |

Script commands (table at 0x44F000): DisableHitAndRun 0x454860, EnableHitAndRun 0x454890, SetHitAndRunMeter 0x454900,
SetNumChaseCars 0x4548C0, ResetHitAndRun 0x4548B0, SetHitAndRunDecay 0x4548E0. `SetHitNRun` is a no-op stub. **[H]**

### B.2 Registered events (Init 0x4D88B0) and what HandleEvent does with them [H]

**Gate before any increase** (0x4D8B7D): skip if `HitnRunDisabled` (+0xB8), or `InteriorManager.In` (+0x28), or `EntryRequested` (+0x08).
The decay-hold value is `SpawnOn ? DecayDelayWhileSpawning : DecayDelay`. Increases write it to +0xA8, so decay is held for 3 s
(0 s during a chase).

| Event (ID) | Param the handler dereferences | Effect |
|---|---|---|
| VEHICLE_DESTROYED_BY_USER (98, 0x62) | Vehicle\* | skip chase cars, skip non-AI/traffic (`+0xA4` must be 1 or 2), skip the ignored vehicle; **+25**, 10 coins at the car |
| VEHICLE_VEHICLE_COLLISION (96, 0x60) | ptr to struct, `[0]` = Vehicle\* | same filters, plus **rate limit**: only if `DecayDelayMS <= hold/2` (at least 1.5 s since the last increase); **+5** |
| HIT_MOVEABLE (200, 0xC8) | sim::SimState\* (`+0x6C` = 0, `+8` owner, owner+0x30 CollisionAttributes) | dedupe via PrevHits; KrustyGlass (type 0x13), Moveable (-1, except hedges) or Breakable delta and coins |
| OBJECT_KICKED (298, 0x12A) | CollisionEntityDSG\* (needs +0x30 attributes, vtbl+0x40 position) | as HIT_MOVEABLE, minus "waspray" |
| COLAPROP_DESTROYED (122, 0x7A) | object (vtbl+0x20 pos) | +0, 10 coins |
| **KICK_NPC (168, 0xA8)** | **Character\*** (reads +0x39D, puppet position) | **+10** (always); coins (0) only if `!HasBeenHit` |
| **PLAYER_CAR_HIT_NPC (223, 0xDF)** | **Character\*** (+0xE4, +0x190, +0x39D) | **+13** if IsNPC and AI state is not InSim; coins (0) if `!HasBeenHit` |
| SWITCH_SKIN (311) | - | -100 |
| ENTERING_TRAFFIC_CAR / PLAYER_CAR (108/109) | Vehicle\* (`+0x10C` id) | -100 when the vehicle ID changes |
| HIT_AND_RUN_CAUGHT (257) | - | if ChaseOn: remove chase cars, meter = 0, ChaseOn = SpawnOn = 0, **lose 50 coins**, run the "busted" fade sequence |
| CHASE_VEHICLE_SPAWNED (215) | - | ChaseOn = 1 |
| ENTER/EXIT_INTERIOR_START (110/114), CONVERSATION_START (289) | - | HitnRunDisabled = 1, chase AI paused |
| ENTER/EXIT_INTERIOR_END (113/115), CONVERSATION_SKIP/DONE (290/291) | - | HitnRunDisabled = 0, chase AI resumed |
| MISSION_START (147), MISSION_SUCCESS (156) | - | meter = 0, chase stopped (SUCCESS also re-enables Hit&Run) |
| MISSION_FAILURE/RESET, LEVEL_START | registered | (default path, only the clamp) |

Evidence (KICK_NPC case):
```
004d8ee8: fld  dword ptr [ebp+0x40]   ; KickNPCDelta
004d8eef: fadd dword ptr [ebp+4]
004d8ef6: mov  [ebp+0xa8], eax        ; decay hold
004d8efc: fstp dword ptr [ebp+4]
004d8eff: mov  al,[edx+0x39d]         ; victim->HasBeenHit
004d8f07: jne  skip_coins
...
004d9148: clamp CurrHitAndRun to [0, 100]
```

### B.3 Update (0x4D9360, every gameplay frame; skipped entirely while HitnRunDisabled) [H]

* Meter **>= 100** and not spawning: SpawnOn = 1, `ChaseManager(0)->vtbl[7](NumChaseCars)`, flags set, **TriggerEvent(HIT_AND_RUN_START 256)**.
  Listeners: HUD (0x47FB00) and MusicPlayer. This only happens if the level created a ChaseManager (CreateChaseManager in leveli.mfk; all 7 levels do).
* Meter **< 5** means SpawnOn = 0 (no new chase cars).
* Throb: when the meter rises above **78** (0.78 × 100) it fires HIT_AND_RUN_METER_THROB (259; "rage_warning" SFX). This re-arms once the meter drops below 60.
* ChaseOn with no chase cars left and SpawnOn = 0: ChaseOn = 0 and HIT_AND_RUN_EVADED (258).
* Busting: a chase car within CopTicketDistance while the player is below the speed threshold for 0.75 s (in a car) or 1.5 s
  (on foot, using CopTicketDistanceOnFoot) fires HIT_AND_RUN_CAUGHT.
* HUD bar updated when the value changes by more than 0.5 (0x480B20).
* Decay: while DecayDelayMS > 0 it counts down by elapsed ms. Otherwise, if not DecayDisabled, it subtracts `rate*ms/1000`.
  `rate` = Inside (10/s) if InteriorManager.In or EntryRequested, else WhileSpawning (6/s) if SpawnOn, else DecayRatePerSecond (1-3/s by level).
  At 0, chase cars are dismissed.

### B.4 Every listener of the candidate events (for side effects)

Built from all 373 AddListener calls (constant event IDs) plus the data-driven ones: the dialog table at 0x64E1A0 and GoTo, GoOutside and
Delivery objectives that register locator events only. **[H]** for registrations; handler effects **[H]** unless marked.

| Event | Listeners and effect |
|---|---|
| KICK_NPC 168 | HitnRunManager (+10, coins); SoundFXGameplayLogic ("car_hit_pedestrian") |
| KICK_NPC_SOUND 225 | DialogCoordinator ("HitByW" line, 2.5 s rate limit) **[M]** for speaker selection |
| PEDESTRIAN_SMACKDOWN 205 | DialogCoordinator ("Char"); AttractionBehaviour (wasp cameras home in) |
| OBJECT_KICKED 298 | HitnRunManager (needs CollisionAttributes); SoundFX (kick thud from attributes); PedestrianManager (peds within 12 m panic); AttractionBehaviour |
| PLAYER_CAR_HIT_NPC 223 | HitnRunManager (+13); SoundFX (collision SFX); DialogCoordinator ("HitByC"); PedestrianManager (panic); AttractionBehaviour |
| PC_NPC_COLLISION 222 | AttractionBehaviour only |
| KICK 90 | AttractionBehaviour only |
| HIT_MOVEABLE 200 | HitnRunManager only |
| HIT_BREAKABLE 199 | AttractionBehaviour; DialogCoordinator ("Break"); TutorialManager/BootupContext-created listener (0x468450) **[M]**. **Not** HitnRunManager |
| VEHICLE_VEHICLE_COLLISION 96 | HitnRunManager; **CollectDumpedObjective and DeliveryObjective (mission objectives: can make you dump or lose cargo)** |
| VEHICLE_DESTROYED_BY_USER 98 | HitnRunManager only (fired natively at 0x4EB62F when `vehicle+0x478` is non-zero) |
| WASP_BULLET_HIT_CHARACTER 253 | SoundFX ("character_zapped"); DialogCoordinator |
| HIT_AND_RUN_START 256 | HUD, MusicPlayer |

### B.5 Recommendation for gunfire heat

1. **Best: call `HitnRunManager::HandleEvent` directly** (`__thiscall` 0x4D8B00 / vtbl[1]). You get the exact native rules (disabled
   and interior gates, decay hold, rate limit, coins, clamp to 100) and **no other listener is notified**: no SFX, dialog, panic,
   wasps or mission objectives.
   ```c
   typedef void (__attribute__((thiscall)) *HnrEv_t)(void*, int, void*);
   static void HnrEvent(int ev, void* p){ void* h=*(void**)0x6C84E0; if(h) ((HnrEv_t)0x4D8B00)(h, ev, p); }
   HnrEvent(168, npc);                   // shot an NPC: +10 (coins only if npc[0x39D]==0; default 0 coins)
   HnrEvent(223, npc);                   // stronger hit (knock-down): +13, only if npc is not already InSim
   void* box[1]={veh}; HnrEvent(96, box); // shot a traffic/AI car: +5, rate-limited to one per 1.5 s, skips chase/user cars
   HnrEvent(98, veh);                    // you destroyed a car: +25 and 10 coins (skip if the game already fires 98 for it)
   ```
2. **Arbitrary amounts** (for example the "gun fired" noise): write CurrHitAndRun yourself with the same gates. Update() then handles
   START at 100, throb, decay and HUD on the next frame.
   ```c
   static void AddHeat(float d){
       char* h=*(char**)0x6C84E0; if(!h || h[0xB8]) return;              // missing or HitnRunDisabled
       char* im=*(char**)0x6C8FF8; if(im && (im[0x28]||im[0x08])) return; // interior / entering
       float v=*(float*)(h+4)+d; v=v<0?0:(v>100?100:v); *(float*)(h+4)=v;
       *(float*)(h+0xA8)= h[0xA5] ? *(float*)(h+0xB0) : *(float*)(h+0xAC); // decay hold like the game
   }
   ```
3. **TriggerEvent** only when you want the extra native flavour (SFX, dialog, ped panic). Never pass param 0 to KICK_NPC,
   PLAYER_CAR_HIT_NPC, OBJECT_KICKED or HIT_MOVEABLE (they dereference it). **Avoid TriggerEvent for event 96** (mission cargo objectives).
   ```c
   static unsigned TriggerEvent(int ev, void* param){
       void* em=*(void* volatile*)0x6C9018; unsigned r; if(!em) return 0;
       __asm__ __volatile__("pushl %[p]\n\tpushl %[e]\n\tcall *%[fn]"
           : "=a"(r), "+d"(em) : [p]"r"(param), [e]"r"(ev), [fn]"r"((void*)0x432AD0) : "ecx","memory","cc");
       return r;  // use register operands only: the pushes move esp
   }
   ```
   (SHARMemory's injected stub does the same: push param, push event, `call GetInstance`, `mov edx,eax`, `call TriggerEvent`.)

Suggested amounts (meter 0-100; chase at 100; decay 1-3 per second after a 3 s hold):
- shot fired near people: +1 to +3 (AddHeat);
- NPC hit: +10 (KICK_NPC semantics);
- NPC knocked down: +13 (PLAYER_CAR_HIT_NPC semantics);
- car hit: +5 (rate-limited);
- car destroyed: +25.

About 8-10 NPC hits start a chase, which matches kicking.

---------------------------------------------------------------------------------------------------------------------

## C. Bonus leads (not asked, useful for TraceShot) [M]

* `IntersectionList` (vtable 0x61593C; ctor 0x41AF80). The fill function 0x41BF60 (`this, &center, radius, bool, exclude`, `ret 0x14`)
  collects dynamic objects. 0x41B490 = closest segment hit: **eax = &end, edi = &start**, stack `(list, Vector* outHit, void** outObj)`,
  `ret 0xC`, returns bool. The kick and the wasp projectile use these to find what they hit.
* Wasp bolts damage traffic cars with `Vehicle::TriggerDamage` 0x4EDF90 (eax = vehicle, stack `float amount, bool`), which fires VEHICLE_DAMAGED.
