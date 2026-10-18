// SHAR Weapon Wheel - shared declarations.
// Loaded into Simpsons.exe as a d3d8.dll proxy (see main.cpp).
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d8.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define WW_VERSION "1.0.0"
// Shooting from cars (in development, off unless DriveBy=1) is left out of the v1.0 build: 0 compiles it out, with the
// game animation data its prototype carried; what the rest of the mod calls of it are stand-ins doing what DriveBy=0 did
#ifndef WW_DRIVEBY
#define WW_DRIVEBY 0
#endif

// ---------------------------------------------------------------- config/log
struct Bind { int code[2]; const char* label; };   // keys, mouse buttons (0x101-0x105), keys by place (0x200+): kbm.cpp
struct Config {
    bool enabled = true;
    float slowMotion = 0.30f;   // game speed while the wheel is open (1 = normal)
    float blur = 1.0f;          // background blur strength, 0 = off
    float dim = 0.72f;          // background brightness while open
    float uiScale = 1.0f;       // wheel size multiplier
    bool debug = false;         // enables WeaponWheel\debug_cmd.txt + verbose log
    // controller buttons: XINPUT_GAMEPAD_* bits, plus BTN_LT / BTN_RT for the triggers
    uint32_t wheelButtons = 0x0100;            // LB
    uint32_t aimButtons = 0x0004 | (1u << 16); // D-pad left or LT
    uint32_t fireButtons = 1u << 17;           // RT
    // in a car (drive-by, in development): LT / RT are the brake and the gas there
    uint32_t carAimButtons = 0x0100;           // LB
    uint32_t carFireButtons = 0x0200;          // RB
    bool driveBy = false;       // (in development) aiming from the car on without debug commands
    int driveByCam = 1;         // a closed car's camera: 1 beside the window, 0 over the roof
    bool driveByLean = false;   // (prototype) in a closed car he leans out of the window
    bool driveBySmash = true;   // his window's glass in: he elbows it out before the raise (off: it breaks at once)
    bool driveByFullAim = true; // (M7) aim all the way round (past his window's arc the pistol goes to his other hand, in the car)
    float aimSens = 1.0f;       // right stick speed while aiming
    bool invertY = false;       // right stick up = look down
    float aimDeadzone = 0.20f;  // right stick, round (both axes together)
    float volume = 0.6f;        // our sound effects
    // keyboard and mouse
    bool kbm = true;
    float mouseSens = 1.0f;     // aiming: 0.022 degrees per mouse count at 1.0
    bool mouseInvertY = false;
    float wheelMouse = 100.0f;  // mouse counts to pick a slot in the wheel
    bool aimClick = true;       // a quick click of the aim key keeps aiming until the next click
    Bind keyWheel, keyAim, keyFire, keyFists, keyHandgun, keyRPG, keyShotgun, keyDonut, keyBeer, keyLast, keyPage, keyAction, keyKick;
    Bind keyCarAim, keyCarFire;   // in a car (drive-by)
};
enum : uint32_t { BTN_LT = 1u << 16, BTN_RT = 1u << 17 };
uint32_t ParseButtons(const char* text, uint32_t def);
void ButtonNames(uint32_t mask, char* out, int n);
int ButtonList(uint32_t mask, const char** out, int max);   // on-screen labels, one per button
extern Config g_cfg;
extern wchar_t g_modDir[MAX_PATH];   // "<game>\WeaponWheel\" (trailing slash)

void Config_Load();
void Log_Open();
void Log(const char* fmt, ...);

// ---------------------------------------------------------------- hooks.cpp
void* PatchIAT(HMODULE module, const char* dll, const char* func, void* hook);
bool HotPatch(void* target, void* hook, void** original);
void* PatchVTable(void* object, int index, void* hook);

// ---------------------------------------------------------------- gamefiles.cpp
// the game's own art and sounds, from the player's copy of the game (none of it ships with the mod)
bool Png_LoadFile(const wchar_t* path, uint32_t** pixels, int* w, int* h);   // straight ARGB, malloc'd
bool GameArt_Fill(const wchar_t* atlasTxt, uint32_t* px, int w, int h);       // the atlas's blank game pieces
int GameFile_Sounds(const char* const* names, int count, BYTE** wavs, DWORD* sizes);   // WAVs from soundfx.rcf

// ---------------------------------------------------------------- timescale.cpp
void Time_Install();
void Time_SetScale(double scale);   // affects the game only
double Time_GetScale();
double Time_Real();                 // real seconds (unscaled)

// ---------------------------------------------------------------- input.cpp
struct Pad {
    bool connected;
    WORD buttons;          // XINPUT_GAMEPAD_* bits
    float lx, ly, rx, ry;  // -1..1, y up
    BYTE lt, rt;
};
void Input_Install();
bool Input_Poll(Pad* out);
void Input_SetBlock(bool block);   // hide pad input from the game while the wheel is open
enum { GUNBLOCK_TRIGGERS = 1, GUNBLOCK_MOVE = 2, GUNBLOCK_LOOKY = 4, GUNBLOCK_LOOKX = 8 };
void Input_SetGunBlock(int flags, WORD buttons); // while armed: our buttons/triggers; aiming: no walking
void Input_Stats(long* calls, long* filtered);   // game pad reads since last call (debug)
void Input_Rumble(float lo, float hi, float seconds);   // kick both motors, fading out over `seconds`
void Input_RumbleUpdate(float dt, bool allowed);        // once per frame; not allowed = motors off
// debug test hook: a pretend controller merged into the real one (sticks -1..1, y up)
void Input_SetFake(bool on, WORD buttons, BYTE lt, BYTE rt, float lx, float ly, float rx, float ry);
void Input_DrunkSteer(float level, float speed);   // drunk driving: the left stick's X late, springy, wandering (0: off)
bool Tires_DrunkNudge(uint32_t veh, float yawAcc, float sideAcc, float dt);   // drunk driving: his car wanders (yaw rad/s^2, sideways m/s^2)
void Input_DebugInfo(char* out, int n);

// ---------------------------------------------------------------- kbm.cpp
struct Kbm {
    bool active;           // keyboard/mouse was used last (the hints show its keys)
    bool wheel, aim, fire; // held
    bool carAim, carFire;  // the in-car aim / fire keys held (KeyCarAim / KeyCarFire)
    float mdx, mdy;        // mouse movement this frame (raw counts, y down)
    int scroll;            // scroll wheel notches this frame (+ = away)
    int pick;              // weapon key pressed this frame (WPN_*), -1 = none
    bool last, page;       // last-weapon / switch-page key pressed this frame
};
void Kbm_Install();
void Kbm_ParseBind(const char* job, const char* text, Bind* b, const char* def);
void Kbm_LogBinds();   // the binds in use, and what was wrong with the ini's (after the log opens)
void Kbm_Update(float dt, const Pad& pad, bool focused, bool gameplay);
const Kbm& Kbm_Get();
void Kbm_SetHide(bool move, bool scroll, bool aimFireButtons, bool allButtons);   // what the game's mouse doesn't get
void Kbm_AimAllowed(bool allowed);   // a weapon is out on foot, wheel shut (else a click-to-aim is forgotten)
void Kbm_CarAimAllowed(bool allowed);   // the same for the car's aim key: aiming from the car is possible
bool Kbm_DebugCmd(const char* cmd, const char* arg);
void Kbm_GrabPointer(bool on, HWND window);   // keep the pointer in the window (aiming / picking with the mouse)
int Kbm_DebugFocus();   // debug "focus": -1 none, else pretend the window has (1) or lost (0) focus

// ---------------------------------------------------------------- game.cpp
struct GameState {
    bool known;            // supported game version detected
    int context;           // GameFlow current context (9 = NormalInGame)
    int nextContext;
    uint32_t inputState;   // InputManager active state bits (1 = gameplay)
    bool hasPlayer;
    bool inCar;
    bool onFoot;           // the wheel is allowed
    uint32_t player;       // Character* of the player
};
void Game_Init();
void Game_Read(GameState* gs);
const char* Game_VersionName();
bool Game_IsEnglish();   // the release everything was found in (others: no hits, no game effects)

struct PoserPose {
    enum { MAX = 48 };
    bool ok;
    int numJoints;
    uint32_t jointsAddr;
    float object[MAX][16];   // local, relative to the parent joint
    float world[MAX][16];    // world space (what the skin is drawn with)
    float position[3];
};
bool Game_ReadPoser(uint32_t player, PoserPose* out);
bool Game_GetCamera(float* view16, float* proj16);   // world camera (row-major D3D matrices)
uint32_t Game_CameraObject();
bool Game_OverrideCamera(const float* world16, float fov, float nearBlend = 0, bool place = true);   // drawing only, until restored
                                                                                                    // (place: its position and target too)
void Game_RestoreCamera();
bool Game_GetGameCamera(float* world16, float* fov);         // the game's camera, ignoring our override
bool Game_PlayerPosition(uint32_t player, float* pos3);
bool Game_SetFollowCamHeading(float heading);   // turn the on-foot camera around the player
float Game_CameraSweep(const float* from, const float* to, float radius, float* coverage);   // free fraction (game's camera line test)
void Game_HoldIdleTimer(uint32_t player);
bool Game_WalkerActive(uint32_t player);
uint32_t Game_CurrentAction(uint32_t player);       // the action he is running now (0: none)
bool Game_ScriptedMove(uint32_t player);            // the game walks/animates him (to a car door, climbing in)
int Game_GetLights(float* ambient3, float (*dir)[3], float (*col)[3], int max);   // the world's lights       // his walking action is in charge (not a jump, kick, get-up...)
bool Game_ReadPoserJoints(uint32_t joints, int n, PoserPose* out);   // just the joint array (fast)       // no bored fidgets (they stop him turning)
void Game_SetDesiredDir(uint32_t player, float heading);   // (a world heading: written in his walk's frame)
float Game_ParentYaw(uint32_t ch);                             // (v1.0) his walk's frame's yaw: 0 on the ground, the car's on one
bool Game_AimMoveAvailable();                       // walking while aiming is possible (English)
void Game_AimMove(uint32_t player, float facing, float stickX, float stickY);   // steer his walk: face `facing`, move with the stick (0: off)
void Game_AimMoveLog(int frames);                   // debug "movelog"
// drunk walking (game.cpp): his walk's way and speed filtered (late, an under-damped spring, a wander); a stumble's lurch takes over
// by lurchW; freeze: he can't walk at all
void Game_DrunkWalk(uint32_t player, bool active, float level, float lurchHead, float lurchSpeed, float lurchW, bool freeze);
void Game_DrunkWalkState(float* inDir, float* inWant, float* outDir, float* outWant);
bool Lab_Relocate(float x, float y, float z, float heading);   // (lab.cpp) put him there at once; heading: atan2(x, z) of his facing + pi
int Game_ListVehicles(float (*pos)[3], char (*names)[24], int max, int* types = nullptr);   // the level's active vehicles
bool Game_VehicleActive(uint32_t vehicle);   // still in the game's active list
int Game_ListVehiclePtrs(uint32_t* ptrs, float (*pos)[3], int max);   // the active vehicles and where they are
int Game_ListCharacters(float (*pos)[3], int max);   // everyone but the player
uint32_t Game_CharacterManager();   // its character list: 64 pointers at +192 (0 the player)
bool Game_FindVehicleFrame(const float* pos3, float* frame16);   // (diagnostic)
bool Game_NearestVehicleFrame(const float* pos3, float maxDist, float* frame16);   // (test hook "route")
const char* Game_ClassName(uint32_t obj);            // RTTI class name, for logs

// ---------------------------------------------------------------- scene.cpp
void Scene_HookDevice(IDirect3DDevice8* dev);
void Scene_EndFrame();
void Scene_Probe(int frames);
void Scene_CarProbe(int frames);   // debug: log the draws on vehicles
void Scene_TexDump(const char* arg);   // debug: "texdump <hex address>,<file.bmp>"
void Scene_SetAspect(float aspect);
bool Scene_GetCamera(D3DMATRIX* view, D3DMATRIX* proj);
void Scene_OwnBegin(bool own);

// ---------------------------------------------------------------- gun.cpp
// What the player has out. The wheel picks one; the weapon in hand changes over once the old
// one is put away (holster, then draw).
enum WeaponId { WPN_NONE, WPN_HANDGUN, WPN_RPG, WPN_SHOTGUN, WPN_COUNT, WPN_DONUT = WPN_COUNT, WPN_BEER };   // (the donut: an item, held like a
                                                                                                     // weapon but outside the weapons' cycle)
void Gun_SetPlayer(uint32_t player);
void Gun_Equip(int weapon);   // WPN_NONE = fists
int Gun_Weapon();             // the weapon picked (WPN_NONE = fists)
void Gun_StartBodyData();     // (as the game starts: the drunk body's skin points, from the player's own files)
bool Gun_BeerAllowed();       // whether the player may have the beer (not Bart or Lisa - decided 2026-10-15)
void Scene_HudHide(bool hide); // (lab) the game's HUD hidden for footage
bool Gun_IsEquipped();        // any weapon picked
bool Gun_Aiming();            // aiming, or the aim camera still easing out
float Gun_StickHeading();     // (lab "wstick") the heading the left stick moves him by this frame
void Gun_Update(float dt, const Pad& pad, const GameState& gs, bool wheelOpen);
bool Gun_NeedsDraw();
void Gun_DebugCmd(const char* cmd, const char* arg);

// ---------------------------------------------------------------- sound.cpp
enum {
    SND_SHOT, SND_EQUIP, SND_HOLSTER, SND_TICK, SND_CYCLE,
    SND_OPEN, SND_CLOSE, SND_PAGE, SND_LOCKED, SND_HIT_HARD, SND_HIT_SOFT,
    SND_ROCKET, SND_BOOM1, SND_BOOM2, SND_BOOM3, SND_METAL1, SND_METAL2, SND_METAL3, SND_GLASS1, SND_GLASS2,
    SND_POP, SND_HISS,
    SND_SHOTGUN, SND_PUMP, SND_SHELL, SND_BITE, SND_CHEW, SND_GULP, SND_PAT, SND_PSSHT, SND_BURP, SND_CRUNCH, SND_CLANK, SND_AHH, SND_HICCUP, SND_WHOA, SND_THUD, SND_GROAN, SND_SNORE,
    SND_COUNT
};
void Sound_Init();
void Sound_SetWindow(HWND hwnd);   // starts DirectSound (needs the game window)
void Sound_Play(int id, float vol = 1.0f, float pitch = 1.0f, float pan = 0.0f);

// ---------------------------------------------------------------- wheel.cpp
void Wheel_OnDevice(IDirect3DDevice8* dev);
void Wheel_OnPresent(IDirect3DDevice8* dev, IDirect3DSurface8* target, HWND hwnd);
void Wheel_OnLostDevice();
void Wheel_OnResetDevice();
extern float g_dbgTimeScale;   // debug "slowmo": the game's speed while the wheel is shut (1 = normal)

// ---------------------------------------------------------------- debug.cpp
struct DebugCmds {
    bool forceOpen;        // open the wheel without LB / game checks
    bool haveStick;
    float stickAngle;      // degrees, 0 = up, clockwise
    int tab;               // -1 = no change
    bool screenshot;
    wchar_t shotPath[MAX_PATH];
    int probe;             // log this many frames of draw calls
    int markers;           // -1 = no change, 0/1 = joint debug dots
    int cycle;             // 0 = none, -1/+1 = D-pad left/right in the wheel
    float rec;             // "rec <seconds>": record that long (0 = no change, < 0 = stop)
    bool recFull;          // "rec <seconds> full": every pixel (half size otherwise)
};
void Debug_Poll(DebugCmds* cmds);
void Debug_ApplyPokes(char when);   // 'p' at Present, 'b' at the game's BeginScene
void Debug_TestTick(float dt, const GameState& gs);   // the pretend controller's timed presses and walkto
void Debug_SetDrive(bool on, float steer, float gas, float brake);   // (test) a driver for his car over the pretend pad (m6 "pace")
