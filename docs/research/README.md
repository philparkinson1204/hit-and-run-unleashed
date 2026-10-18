# Research notes

Read-only static analyses of `Simpsons.exe` (English release), made on 2026-10-04 by analysis
agents with Python, pefile and capstone. Nothing here was run in the game when it was written.

| File | Covers | Status |
|---|---|---|
| [camera_collision.md](camera_collision.md) | How the follow camera avoids walls; the IntersectionList line test | The line test (0x41BAB0) and SuperCamCentral's list are verified and used by the aim camera (step 3) |
| [shots_vehicles_effects.md](shots_vehicles_effects.md) | Ray tests for shots, vehicle damage and destruction, particle effects | Section A (ray tests, the ground, owners, AI refs, volume layout) verified and used by `TraceShot` (step 1). Section C: `ParticleManager::Add`, `AddSparks` and the attributes' particle verified and used (step 2); particle types identified by sight (HOW_IT_WORKS.md). Section B: `TriggerDamage`, its gates and `ApplyForce` verified; the collision handler's damage-at-contact helper (0x4ED570, not in the notes) is what's used (step 3) |
| [characters_heat.md](characters_heat.md) | Native knockdown reactions (kick, car, wasp bolts), the Hit & Run meter | Section A (the kick's reactions, pushes, AI state, events, cringe) verified and used (step 4); one gap: a push outside the game's collision areas makes a character fall through the ground. Section B (Hit & Run): `HandleEvent`, its gates and the KICK_NPC and VEHICLE_VEHICLE_COLLISION cases verified and used (step 5) |

Each claim carries a confidence tag ([H] high, [M] medium, [L] low). Check an address in the
disassembly before relying on it.

Round 6 (the rocket launcher) checked more in the disassembly itself; the results are in
HOW_IT_WORKS.md "Explosions" and "The rocket": the car explosion (0x4EE280) and its area push
(0x4DC7D0), `BreakablesManager::Play` (0x4B87D0), `TriggerDamage` with no clamp and who gets the
blame for a wreck (+0x474 / +0x478), `Vehicle::ApplyForce` not checking "can be damaged",
CAMERA_SHAKE's parameter, the wasp bolts' `Projectile::Update` (0x41D530), the particle pools
(0x4B6520), and the wasp shields (`ActorDSG::ApplyForce` 0x4153A0).
