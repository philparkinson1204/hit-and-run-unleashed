// Reads a few values from the game's memory to decide when the wheel is allowed.
// Addresses/offsets come from SHARMemory (github.com/Hampo/SHARMemory, credits to
// Lucas Cardellini and Hampo). All reads go through ReadProcessMemory so a bad
// pointer during loading can never crash the game.
#include "common.h"

enum { GF_NORMAL_IN_GAME = 9 };
// InputManager active-state: free roam shows up as ACTIVE_ALL (0xFFFFFFFF) after a
// level loads and as ACTIVE_GAMEPLAY (1) after returning from the pause menu.
// Menus (2), Super Sprint (4), first person (8) and camera animations (0x10) block it.
enum : uint32_t { INPUT_ACTIVE_GAMEPLAY = 1, INPUT_ACTIVE_ALL = 0xFFFFFFFFu };

static uint32_t A_GameFlow, A_InputManager, A_CharacterManager, A_RenderManager, A_VehicleCentral;
static uint32_t A_SuperCamCentral, V_WalkerCam;   // English only (found with tools/findobj.py)
static uint32_t V_WalkerLocomotionAction;
static uint32_t V_Arrive, V_Orient, V_PlayAnimationAction, V_ChangeLocomotion, V_CarDoorAction;   // English only (RTTI)
static const char* g_version = "unknown";
static bool g_english;
static void AimMoveInstall();

static bool ReadMem(uint32_t addr, void* out, size_t n)
{
    SIZE_T got = 0;
    return addr >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)addr, out, n, &got) && got == n;
}

template <class T> static T Rd(uint32_t addr, T def = T())
{
    T v;
    return ReadMem(addr, &v, sizeof(T)) ? v : def;
}

void Game_Init()
{
    // version fingerprint, same check SHARMemory uses
    uint32_t sig = Rd<uint32_t>(0x593FFF);
    switch (sig) {
    case 0x4B8B2274:
        g_version = "English";
        A_GameFlow = 0x6C9014; A_InputManager = 0x6C9008; A_CharacterManager = 0x6C8470; A_RenderManager = 0x6C87B4;
        A_VehicleCentral = 0x6C84D8;
        A_SuperCamCentral = 0x6C906C; V_WalkerCam = 0x614C8C; V_WalkerLocomotionAction = 0x61620C;
        V_Arrive = 0x616168; V_Orient = 0x6160C4; V_PlayAnimationAction = 0x6162E4; V_ChangeLocomotion = 0x616510;
        V_CarDoorAction = 0x6161B8;
        break;
    case 0xFAE804C5:
        g_version = "Demo";
        A_GameFlow = 0x6C8FD4; A_InputManager = 0x6C8FC8; A_CharacterManager = 0x6C8430; A_RenderManager = 0x6C8774;
        A_VehicleCentral = 0x6C8498;
        break;
    case 0xC985ED33:
        g_version = "International";
        A_GameFlow = 0x6C8FD4; A_InputManager = 0x6C8FC8; A_CharacterManager = 0x6C8430; A_RenderManager = 0x6C8774;
        A_VehicleCentral = 0x6C8498;
        break;
    case 0xFC468D05:
        g_version = "Best Seller Series";
        A_GameFlow = 0x6C900C; A_InputManager = 0x6C9000; A_CharacterManager = 0x6C8468; A_RenderManager = 0x6C87AC;
        A_VehicleCentral = 0x6C84D0;
        break;
    default:
        break;
    }
    Log("Game version: %s (sig %08X)", g_version, sig);
    g_english = sig == 0x4B8B2274;
    if (g_english) AimMoveInstall();
    else if (A_GameFlow)   // everything that calls into the game's code was found in the English release
        Log("Not the English release: the wheel and the weapons work, but shots and rockets don't hit anything, "
            "and aimed walking and the follow-camera turn are off");
    else
        Log("Unsupported game version: the weapon wheel stays off");
}

bool Game_IsEnglish() { return g_english; }

const char* Game_VersionName()
{
    return g_version;
}

// The world camera: RenderManager -> layers (+0x1C, world layer is index 2) ->
// WorldRenderLayer -> tView (+0xC) -> tPointCamera (+0x18).
// tPointCamera: fov (+0x10, horizontal, radians), aspect (+0x14), near (+0x18),
// far (+0x1C), view matrix world->camera (+0x48), camera->world (+0x88).
// tView also holds the scene lights: ambient (+0x3C), directional (+0x40..+0x48).
uint32_t Game_CameraObject()
{
    if (!A_RenderManager) return 0;
    uint32_t rm = Rd<uint32_t>(A_RenderManager);
    uint32_t layer = rm ? Rd<uint32_t>(rm + 0x1C + 2 * 4) : 0;
    uint32_t tview = layer ? Rd<uint32_t>(layer + 0xC) : 0;
    return tview ? Rd<uint32_t>(tview + 0x18) : 0;
}

// Temporary camera override for the aiming view. The game's camera object is saved and
// replaced at BeginScene (so only drawing sees it) and put back as soon as the world is
// drawn - the game's EndScene (scene.cpp; at Present as well, in case). The game updates
// this camera after drawing and before Present, and a transition (into or out of a car)
// starts that update from what the camera holds: put back only at Present, ours were taken
// as the game's own and built on again the next frame - the aim's place, the drunk lean,
// the lens - out past the sky in a second at 200-460 frames a second (the 2026-10-18
// report). On the way back only what still holds our values is restored; anything the game
// wrote since is kept.
// Fields: fov (+0x10), near (+0x18), view (+0x48), camera->world (+0x88), position (+0xCC), target (+0xD8).
static uint8_t g_camSave[0xE4 - 0x10];
static uint8_t g_camWritten[0xE4 - 0x10];   // the block as we left it
static uint32_t g_camSaved;
static float g_ovView[16], g_ovFov;   // the camera the frame was drawn with (override)
static uint32_t g_ovCam;              // ... the camera object it went through (0: the game's own, this frame)
static float g_ovNear;                // ... and its near plane (the aim brings it in)
void Game_CameraFrameStart() { g_ovCam = 0; }   // (scene.cpp: each frame, before its override)
// Drawing only uses the matrices; position and target are used to cull characters (a
// view cone) and to hide the player when the camera is close to him. Both are written,
// with the position pushed back along the view: the cone then still covers the picture
// and a low camera looking up past the player does not make him vanish.
static const float kCullBack = 1.5f;

// nearBlend 0..1 moves the near plane from the game's (1 m) toward kAimNear, so a camera
// squeezed in by a wall can sit close behind his shoulder without cutting into him. The depth
// buffer is 24-bit: at 0.25 m depth steps are still about 1 cm at 200 m.
static const float kAimNear = 0.25f;

// (v1.0) `place`: also the position and target (+0xCC / +0xD8) - for a camera of our own (the aim, the orbit). Only turning the
// game's camera (the drunk lean and wobble), they're left the game's: its car camera reads +0xCC back - a crash's "unstable" hold
// takes the rod from it, the collision sphere sits on it - and the 1.5 m pushed back along the view came back each time as the
// camera's own place, a step further out each crash: drunk at the wheel near buildings the camera went far away (the play-test)
bool g_camPlaceAlways = false;   // (debug "dcampos 1": the drunk camera writes them too, as before - for an A/B)
bool Game_OverrideCamera(const float* world16, float fov, float nearBlend, bool place)
{
    uint32_t cam = Game_CameraObject();
    if (!cam) return false;
    if (!g_camSaved) {
        if (!ReadMem(cam + 0x10, g_camSave, sizeof(g_camSave))) return false;
        // (other releases: only write into a camera that reads like one - fov, aspect, near / far,
        // the view matrix's last element)
        float* f = (float*)g_camSave;
        if (!g_english && (f[0] <= 0.01f || f[0] > 3.0f || f[1] <= 0.05f || f[2] <= 0.0f || f[3] <= f[2] ||
                           f[(0x48 - 0x10) / 4 + 15] != 1.0f))
            return false;
        g_camSaved = cam;
    }
    float view[16];
    memcpy(view, world16, 64);
    // view = inverse of the rigid camera->world transform
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) view[i * 4 + j] = world16[j * 4 + i];
    for (int c = 0; c < 3; c++)
        view[12 + c] = -(world16[12] * view[c] + world16[13] * view[4 + c] + world16[14] * view[8 + c]);
    view[3] = view[7] = view[11] = 0;
    view[15] = 1;
    memcpy(g_ovView, view, 64);
    g_ovFov = fov;
    float tgt[3] = {world16[12] + world16[8] * 10, world16[13] + world16[9] * 10, world16[14] + world16[10] * 10};
    float pos[3] = {world16[12] - world16[8] * kCullBack, world16[13] - world16[9] * kCullBack,
                    world16[14] - world16[10] * kCullBack};
    SIZE_T done;
    HANDLE me = GetCurrentProcess();
    WriteProcessMemory(me, (LPVOID)(uintptr_t)(cam + 0x10), &fov, 4, &done);
    float gameNear;
    memcpy(&gameNear, g_camSave + (0x18 - 0x10), 4);
    if (nearBlend > 0.0f && gameNear > kAimNear && gameNear < 10.0f) {
        float n = gameNear + (kAimNear - gameNear) * fminf(nearBlend, 1.0f);
        WriteProcessMemory(me, (LPVOID)(uintptr_t)(cam + 0x18), &n, 4, &done);
    }
    WriteProcessMemory(me, (LPVOID)(uintptr_t)(cam + 0x48), view, 64, &done);
    WriteProcessMemory(me, (LPVOID)(uintptr_t)(cam + 0x88), world16, 64, &done);
    if (place || g_camPlaceAlways) {
        WriteProcessMemory(me, (LPVOID)(uintptr_t)(cam + 0xCC), pos, 12, &done);
        WriteProcessMemory(me, (LPVOID)(uintptr_t)(cam + 0xD8), tgt, 12, &done);
    }
    ReadMem(cam + 0x10, g_camWritten, sizeof(g_camWritten));
    g_ovCam = cam;
    memcpy(&g_ovNear, g_camWritten + (0x18 - 0x10), 4);
    return true;
}

void Game_RestoreCamera()
{
    if (!g_camSaved) return;
    uint8_t live[sizeof(g_camSave)];
    if (Game_CameraObject() == g_camSaved && ReadMem(g_camSaved + 0x10, live, sizeof(live))) {
        for (size_t i = 0; i < sizeof(live); i += 4)
            if (!memcmp(live + i, g_camWritten + i, 4)) memcpy(live + i, g_camSave + i, 4);
        SIZE_T done;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)(g_camSaved + 0x10), live, sizeof(live), &done);
    }
    g_camSaved = 0;
}

// the game's own camera this frame (before any override): camera->world + fov
bool Game_GetGameCamera(float* world16, float* fov)
{
    uint32_t cam = Game_CameraObject();
    if (!cam) return false;
    if (g_camSaved == cam) {
        memcpy(fov, g_camSave, 4);
        memcpy(world16, g_camSave + (0x88 - 0x10), 64);
        return true;
    }
    return ReadMem(cam + 0x10, fov, 4) && ReadMem(cam + 0x88, world16, 64);
}

bool Game_GetCamera(float* view16, float* proj16)
{
    uint32_t cam = Game_CameraObject();
    if (!cam) return false;
    float params[4];
    if (!ReadMem(cam + 0x10, params, 16) || !ReadMem(cam + 0x48, view16, 64)) return false;
    if (g_ovCam == cam) {   // put back (and touched by the game) once the world is drawn; use what we drew with
        memcpy(view16, g_ovView, 64);
        params[0] = g_ovFov;
        params[2] = g_ovNear;
    }
    float fov = params[0], aspect = params[1], zn = params[2], zf = params[3];
    if (fov <= 0.01f || fov > 3.0f || aspect <= 0.05f || zf <= zn || view16[15] != 1.0f) return false;
    float sx = 1.0f / tanf(fov * 0.5f);
    memset(proj16, 0, 64);
    proj16[0] = sx;
    proj16[5] = sx * aspect;
    proj16[10] = zf / (zf - zn);
    proj16[11] = 1.0f;
    proj16[14] = -zn * zf / (zf - zn);
    return true;
}

// Player skeleton: Character -> choreo::Puppet (+0x108) -> choreo::Engine (+0x10) ->
// poser::PoseEngine (+0xC). Joint order is the same for every character model in the
// game (Spine_2 15, Shoulder_R 26, Elbow_R 27, Wrist_R 28...).
// The skeleton the skinned mesh is drawn from: poser::Pose (PoseEngine +0xC), joint
// array at +0xC, 192 bytes per joint (same order as the skeleton):
//   +0x00 index, +0x04 parent*, +0x14 object (local) matrix, +0x54 quat, +0x64 dirty,
//   +0x68 world matrix (world space), +0xA8 quat, +0xB8 dirty, +0xBC dirty
bool Game_ReadPoser(uint32_t player, PoserPose* out)
{
    out->ok = false;
    if (!player) return false;
    uint32_t puppet = Rd<uint32_t>(player + 0x108);
    uint32_t engine = puppet ? Rd<uint32_t>(puppet + 0x10) : 0;
    uint32_t poseEngine = engine ? Rd<uint32_t>(engine + 0xC) : 0;
    // PoseEngine+0x8 (a tPose) is NOT his: every character shares that one scratch pose and
    // its skeleton is whoever was set up last. The poser's Pose (+0xC) and its skeleton are his.
    uint32_t pose = poseEngine ? Rd<uint32_t>(poseEngine + 0xC) : 0;
    uint32_t pskel = pose ? Rd<uint32_t>(pose + 0x8) : 0;
    if (!pskel) return false;
    int n = (int)Rd<uint32_t>(pskel + 0x14);
    uint32_t joints = Rd<uint32_t>(pose + 0xC);
    if (n < 33 || n > 64 || !joints) return false;
    out->numJoints = n < PoserPose::MAX ? n : PoserPose::MAX;
    out->jointsAddr = joints;
    static uint8_t raw[PoserPose::MAX * 192];
    if (!ReadMem(joints, raw, (size_t)out->numJoints * 192)) return false;
    for (int i = 0; i < out->numJoints; i++) {
        if (*(int*)(raw + i * 192) != i) return false;   // sanity: index field
        memcpy(out->object[i], raw + i * 192 + 0x14, 64);
        memcpy(out->world[i], raw + i * 192 + 0x68, 64);
    }
    ReadMem(player + 0x64, out->position, 12);
    out->ok = true;
    return true;
}

// the joint array alone (address and count from an earlier Game_ReadPoser): one read
bool Game_ReadPoserJoints(uint32_t joints, int n, PoserPose* out)
{
    out->ok = false;
    if (!joints || n < 33 || n > PoserPose::MAX) return false;
    static uint8_t raw[PoserPose::MAX * 192];
    if (!ReadMem(joints, raw, (size_t)n * 192)) return false;
    for (int i = 0; i < n; i++) {
        if (*(int*)(raw + i * 192) != i) return false;
        memcpy(out->object[i], raw + i * 192 + 0x14, 64);
        memcpy(out->world[i], raw + i * 192 + 0x68, 64);
    }
    out->numJoints = n;
    out->jointsAddr = joints;
    out->ok = true;
    return true;
}

// The on-foot follow camera: SuperCamCentral -> active SuperCam (+0x8C). A WalkerCam
// keeps its target (+0x144) and position (+0x15C) between updates (each followed by a
// velocity) and the unit direction between them (+0xC4); swinging the position around
// the target turns the camera around the player.
static uint32_t WalkerCam()
{
    if (!A_SuperCamCentral) return 0;
    uint32_t scc = Rd<uint32_t>(A_SuperCamCentral);
    uint32_t cam = scc ? Rd<uint32_t>(scc + 0x8C) : 0;
    return cam && Rd<uint32_t>(cam) == V_WalkerCam ? cam : 0;
}

// The game's own camera line test (English only): IntersectionList::LineOfSight (0x41BAB0)
// sweeps a sphere of the given radius from start to end through the colliders in a list
// (statics, then dynamics and animated colliders unless staticsOnly, then fences) and returns
// 1 if nothing is in the way; it does not test the ground. It takes start in EAX and the list
// in ESI, end / radius / staticsOnly on the stack (it pops them). WalkerCam calls it with
// SuperCamCentral's own list (+0x1A8, vtable 0x61593C), which the game refills every frame
// around the player (radius = the follow camera's distance, the player left out); at our
// BeginScene that refill is done for this frame.
__attribute__((naked)) static int LineOfSight(void* list, const float* start, const float* end, float radius,
                                              int staticsOnly)
{
    __asm__ volatile(
        "pushl %esi\n\t"
        "movl 8(%esp), %esi\n\t"        // list
        "movl 12(%esp), %eax\n\t"       // start
        "pushl 24(%esp)\n\t"            // staticsOnly
        "pushl 24(%esp)\n\t"            // radius
        "pushl 24(%esp)\n\t"            // end
        "movl $0x41BAB0, %ecx\n\t"
        "call *%ecx\n\t"                // pops end, radius, staticsOnly
        "movzbl %al, %eax\n\t"
        "popl %esi\n\t"
        "ret\n\t");
}

static uint32_t CameraList(float* coverage)
{
    if (!A_SuperCamCentral) return 0;
    uint32_t scc = Rd<uint32_t>(A_SuperCamCentral);
    if (!scc || Rd<uint32_t>(scc + 0x1A8) != 0x61593C) return 0;
    uint32_t wc = WalkerCam();
    *coverage = wc ? Rd<float>(wc + 0x134) : 0.0f;
    return scc + 0x1A8;
}

// How far a sphere of `radius` gets from `from` toward `to` before something blocks it, as a
// fraction (1 = all the way). -1 if the game's list isn't available. `coverage` gets how far
// from the player's feet the list reaches.
float Game_CameraSweep(const float* from, const float* to, float radius, float* coverage)
{
    float cov = 0;
    uint32_t list = CameraList(&cov);
    if (coverage) *coverage = cov;
    if (!list) return -1.0f;
    if (LineOfSight((void*)(uintptr_t)list, from, to, radius, 0)) return 1.0f;
    float lo = 0.0f, hi = 1.0f;   // lo is known free, hi known blocked
    for (int i = 0; i < 7; i++) {
        float t = 0.5f * (lo + hi), p[3];
        for (int k = 0; k < 3; k++) p[k] = from[k] + (to[k] - from[k]) * t;
        if (LineOfSight((void*)(uintptr_t)list, from, p, radius, 0)) lo = t; else hi = t;
    }
    return lo;
}

bool Game_SetFollowCamHeading(float heading)
{
    uint32_t wc = WalkerCam();
    float d[3], tgt[3], pos[3];
    if (!wc || !ReadMem(wc + 0xC4, d, 12) || !ReadMem(wc + 0x144, tgt, 12) || !ReadMem(wc + 0x15C, pos, 12)) return false;
    float h = sqrtf(d[0] * d[0] + d[2] * d[2]);
    float hd = sqrtf((tgt[0] - pos[0]) * (tgt[0] - pos[0]) + (tgt[2] - pos[2]) * (tgt[2] - pos[2]));
    if (h < 0.05f || h > 1.01f || hd < 0.2f || hd > 50.0f) return false;
    d[0] = sinf(heading) * h;
    d[2] = cosf(heading) * h;
    pos[0] = tgt[0] - sinf(heading) * hd;
    pos[2] = tgt[2] - cosf(heading) * hd;
    // Only where the game's camera could be itself: its collision only pushes out of what it
    // touches, so a camera swung behind a wall stays there, and closer than 2.5 m to its target
    // it flips round. Blocked: leave it where the game has it.
    float cov;
    float t = Game_CameraSweep(tgt, pos, 0.3f, &cov);
    if (t >= 0.0f && t < 1.0f) {
        float free = t * sqrtf(hd * hd + (tgt[1] - pos[1]) * (tgt[1] - pos[1])) - 0.3f;
        if (free < 2.7f) return false;
        float k = free / sqrtf(hd * hd + (tgt[1] - pos[1]) * (tgt[1] - pos[1]));
        for (int i = 0; i < 3; i++) pos[i] = tgt[i] + (pos[i] - tgt[i]) * k;
    }
    SIZE_T done;
    HANDLE me = GetCurrentProcess();
    return WriteProcessMemory(me, (LPVOID)(uintptr_t)(wc + 0xC4), d, 12, &done) &&
           WriteProcessMemory(me, (LPVOID)(uintptr_t)(wc + 0x15C), pos, 12, &done);
}

// Homer's bored fidgets: his WalkerLocomotionAction (Character+0x2A4) counts idle seconds
// at +0x10 and starts a PlayIdleAnimationAction when they reach +0x14 (a random 14-20 s).
// While a fidget plays the walking action isn't running, so he can't turn at all - with the
// gun out (movement blocked while aiming) that froze him mid-aim. Holding the count at 0
// keeps him ready.
void Game_HoldIdleTimer(uint32_t player)
{
    if (!player || !V_WalkerLocomotionAction) return;
    uint32_t wla = Rd<uint32_t>(player + 0x2A4);
    if (!wla || Rd<uint32_t>(wla) != V_WalkerLocomotionAction) return;
    float zero = 0;
    SIZE_T done;
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)(wla + 0x10), &zero, 4, &done);
}

// The character's ActionController (+0x140) runs one action at a time in its first
// sequencer (+0x4, current action at +0x14). Facing writes only make sense while that is
// the WalkerLocomotionAction: jumps, kicks, getting up, idle fidgets and walking to a car
// door steer him themselves.
bool Game_WalkerActive(uint32_t player)
{
    if (!player || !V_WalkerLocomotionAction) return true;   // unknown game version: assume so
    uint32_t ac = Rd<uint32_t>(player + 0x140);
    uint32_t seq = ac ? Rd<uint32_t>(ac + 0x4) : 0;
    uint32_t act = seq ? Rd<uint32_t>(seq + 0x14) : 0;
    return !act || Rd<uint32_t>(act) == V_WalkerLocomotionAction;
}

// the action his ActionController is running now (0 if none or unknown), for logs and checks
uint32_t Game_CurrentAction(uint32_t player)
{
    if (!player) return 0;
    uint32_t ac = Rd<uint32_t>(player + 0x140);
    uint32_t seq = ac ? Rd<uint32_t>(ac + 0x4) : 0;
    return seq ? Rd<uint32_t>(seq + 0x14) : 0;
}

// The game is walking him somewhere itself: getting into a car runs Arrive (to the door), Orient
// (turn to it), PlayAnimationAction (climbing in) and ChangeLocomotion before he counts as in
// the car. (Jumps and kicks are his own and don't count.) Unknown game version: never.
bool Game_ScriptedMove(uint32_t player)
{
    uint32_t act = Game_CurrentAction(player), vt = act ? Rd<uint32_t>(act) : 0;
    return vt && (vt == V_Arrive || vt == V_Orient || vt == V_PlayAnimationAction || vt == V_ChangeLocomotion ||
                  vt == V_CarDoorAction);
}

// The 3D world's lights, from the world layer's tView: an ambient light at +0x3C and up
// to four directional lights from +0x40 (Level 1: three at 30 degrees elevation, 120
// degrees apart). tLight colour (ARGB) at +0x10; a directional light's direction at +0x50.
// Returns the number of directional lights, or -1 if the lights can't be read.
int Game_GetLights(float* ambient3, float (*dir)[3], float (*col)[3], int max)
{
    if (!A_RenderManager) return -1;
    uint32_t rm = Rd<uint32_t>(A_RenderManager);
    uint32_t layer = rm ? Rd<uint32_t>(rm + 0x1C + 2 * 4) : 0;
    uint32_t tview = layer ? Rd<uint32_t>(layer + 0xC) : 0;
    uint32_t amb = tview ? Rd<uint32_t>(tview + 0x3C) : 0;
    if (!amb) return -1;
    auto rgb = [](uint32_t c, float* out) {
        out[0] = ((c >> 16) & 255) / 255.0f;
        out[1] = ((c >> 8) & 255) / 255.0f;
        out[2] = (c & 255) / 255.0f;
    };
    rgb(Rd<uint32_t>(amb + 0x10), ambient3);
    int n = 0;
    for (int i = 0; i < 4 && n < max; i++) {
        uint32_t L = Rd<uint32_t>(tview + 0x40 + 4 * i);
        if (!L) break;
        float d[3];
        if (!ReadMem(L + 0x50, d, 12)) continue;
        float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(len > 0.5f && len < 1.5f)) continue;   // not a directional light
        for (int k = 0; k < 3; k++) dir[n][k] = d[k] / len;
        rgb(Rd<uint32_t>(L + 0x10), col[n]);
        n++;
    }
    return n;
}

bool Game_PlayerPosition(uint32_t player, float* pos3)
{
    return player && ReadMem(player + 0x64, pos3, 12);
}

// CharacterManager+192: 64 character pointers, the player first (SHARMemory). For the test hook.
uint32_t Game_CharacterManager() { return A_CharacterManager ? Rd<uint32_t>(A_CharacterManager) : 0; }

int Game_ListCharacters(float (*pos)[3], int max)
{
    uint32_t cm = A_CharacterManager ? Rd<uint32_t>(A_CharacterManager) : 0;
    int n = 0;
    for (int i = 1; cm && i < 64 && n < max; i++) {
        uint32_t c = Rd<uint32_t>(cm + 192 + 4 * i);
        if (c && Rd<uint8_t>(c + 0x350) && ReadMem(c + 0x64, pos[n], 12)) n++;   // (+0x350: shown - not a parked walker)
    }
    return n;
}

// (diagnostic) the transform (Vehicle +184) of the active car at this position
bool Game_FindVehicleFrame(const float* pos3, float* frame16)
{
    uint32_t vc = A_VehicleCentral ? Rd<uint32_t>(A_VehicleCentral) : 0;
    for (int i = 0; vc && i < 30; i++) {
        uint32_t v = Rd<uint32_t>(vc + 180 + 4 * i);
        float p[3];
        if (v && ReadMem(v + 232, p, 12) && p[0] == pos3[0] && p[1] == pos3[1] && p[2] == pos3[2]) return ReadMem(v + 184, frame16, 64);
    }
    return false;
}

bool Game_NearestVehicleFrame(const float* pos3, float maxDist, float* frame16)
{
    uint32_t vc = A_VehicleCentral ? Rd<uint32_t>(A_VehicleCentral) : 0, best = 0;
    float bd = maxDist * maxDist;
    for (int i = 0; vc && i < 30; i++) {
        uint32_t v = Rd<uint32_t>(vc + 180 + 4 * i);
        float p[3];
        if (!v || !ReadMem(v + 232, p, 12)) continue;
        float d = (p[0] - pos3[0]) * (p[0] - pos3[0]) + (p[1] - pos3[1]) * (p[1] - pos3[1]) + (p[2] - pos3[2]) * (p[2] - pos3[2]);
        if (d < bd) bd = d, best = v;
    }
    return best && ReadMem(best + 184, frame16, 64);
}
int Game_ListVehiclePtrs(uint32_t* ptrs, float (*pos)[3], int max)
{
    uint32_t vc = A_VehicleCentral ? Rd<uint32_t>(A_VehicleCentral) : 0;
    int n = 0;
    for (int i = 0; vc && i < 30 && n < max; i++) {
        uint32_t v = Rd<uint32_t>(vc + 180 + 4 * i);
        if (v && ReadMem(v + 232, pos[n], 12)) ptrs[n++] = v;
    }
    return n;
}

bool Game_VehicleActive(uint32_t v)
{
    uint32_t vc = A_VehicleCentral ? Rd<uint32_t>(A_VehicleCentral) : 0;
    for (int i = 0; vc && v && i < 30; i++)
        if (Rd<uint32_t>(vc + 180 + 4 * i) == v) return true;
    return false;
}

// VehicleCentral (SHARMemory): 30 active-vehicle pointers at +180 (unless a launcher hack
// raises the limit); a Vehicle's transform is at +184 (position = its last row, +232) and its
// name pointer at +248. For the debug test hook.
// types: Vehicle+0xA4 (0 the player's, 1 AI, 2 traffic...), or nullptr
int Game_ListVehicles(float (*pos)[3], char (*names)[24], int max, int* types)
{
    uint32_t vc = A_VehicleCentral ? Rd<uint32_t>(A_VehicleCentral) : 0;
    if (!vc) return 0;
    int n = 0;
    for (int i = 0; i < 30 && n < max; i++) {
        uint32_t v = Rd<uint32_t>(vc + 180 + 4 * i);
        if (!v || !ReadMem(v + 232, pos[n], 12)) continue;
        if (types) types[n] = (int)Rd<uint32_t>(v + 0xA4, 0xFFFFFFFF);
        uint32_t np = Rd<uint32_t>(v + 248);
        if (!np || !ReadMem(np, names[n], 23)) names[n][0] = 0;
        names[n][23] = 0;
        n++;
    }
    return n;
}

// An object's class from its RTTI (vtable[-1] = complete object locator, +12 the type
// descriptor, +8 its name ".?AVVehicle@@"), for logs. "?" if it can't be read.
const char* Game_ClassName(uint32_t obj)
{
    static char name[48];
    uint32_t vt = obj ? Rd<uint32_t>(obj) : 0;
    uint32_t col = vt ? Rd<uint32_t>(vt - 4) : 0;
    uint32_t td = col ? Rd<uint32_t>(col + 12) : 0;
    char raw[48] = {0};
    if (!td || !ReadMem(td + 8, raw, sizeof(raw) - 1) || strncmp(raw, ".?AV", 4)) return "?";
    char* end = strstr(raw, "@@");
    if (end) *end = 0;
    snprintf(name, sizeof(name), "%s", raw + 4);
    return name;
}

// (v1.0) The frame his walk is reckoned in: Character+0x2B4 (SHARMemory's ParentTransform, a 4x4 with the axes as rows) -
// the world's on the ground (the identity), the car's while he stands on one (its turn and its place: on a minivan parked facing
// -Z, X (-1 0 0), Z (0 0 -1)). His desired direction (+0x110) and his walk's motion angle are in it: the mod's world headings
// written as they were turned him by the car's own heading - aiming on a car facing the other way, he faced the camera and the
// aim camera sat over his other shoulder (the play-test). -> the frame's yaw (atan2(x, z) of its Z row); 0 unless it's a plain
// turn about up
float Game_ParentYaw(uint32_t ch)
{
    if (!ch) return 0.0f;
    float m[12];
    if (!ReadMem(ch + 0x2B4, m, sizeof(m))) return 0.0f;
    float r1y = m[5], r2x = m[8], r2z = m[10];
    if (!(fabsf(r1y - 1.0f) < 0.05f && r2x * r2x + r2z * r2z > 0.8f && r2x * r2x + r2z * r2z < 1.2f)) return 0.0f;
    return atan2f(r2x, r2z);
}

// Character::mfDesiredDir: heading (atan2(x, z) of the forward vector). Written before
// the game's update, the character turns to it and keeps it. `heading` is the world's (v1.0: written in his walk's frame - the
// car's if he stands on one: Game_ParentYaw)
void Game_SetDesiredDir(uint32_t player, float heading)
{
    if (!player) return;
    heading = remainderf(heading - Game_ParentYaw(player), 6.2831853f);
    SIZE_T done;
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)(player + 0x110), &heading, 4, &done);
}

// Walking while aiming (English only). The game already keeps two angles apart:
// WalkerLocomotionAction::DoSimulation (0x40AE90, vtable 0x61620C slot 5, thiscall, ret 4)
// turns his facing toward Character::mfDesiredDir (+0x110) at the walking turn rate, then hands
// its locomotion driver (+0x24) a motion angle (driver +0x30, set by 0x5B6620), a facing angle
// (+0x34) and a speed (+0x38). The stick sets mfDesiredDir and the desired speed (+0x124) earlier
// in the frame (Character::UpdateDesiredDirAndSpeed; only while the stick is pushed). The
// action's own fields: speed +0xC, character +0x1C. For the player while aiming we run it with
// the aim as his desired direction (so he turns to the aim exactly as before, and speeds up and
// slows down as the game does), then give the driver back the stick's direction to move in.
// Everything else (speed, acceleration, the walk/run cycle, collision, the ground, steps) stays
// the game's.
static const uint32_t A_WlaSim = 0x40AE90, A_SetMotionAngle = 0x5B6620, A_WlaVtable = 0x61620C;
typedef void(__fastcall* PFN_WlaSim)(uint32_t self, void* edx, float t);
typedef void(__fastcall* PFN_SetAngle)(uint32_t driver, void* edx, float angle);
static PFN_WlaSim o_WlaSim;
static struct {
    uint32_t player;   // whose walk to steer (0: nobody)
    float facing;      // the aim, as a desired direction
    float moveDir;     // the stick's last direction (game angles)
    bool haveMove;     // moveDir is from this aim
    float sx, sy;      // the left stick as the pad has it (-1..1, y up)
    int backFrames;    // frames the stick has been held gently backwards
    int log;           // debug "movelog": frames left
} g_am;

static void Wr(uint32_t addr, const void* v, size_t n)
{
    SIZE_T done;
    WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)addr, v, n, &done);
}

// Drunk walking (drunk.inc sets it each frame): what the game makes of his stick or keys - the way he wants to go (+0x110) and
// how fast (+0x124) - comes through late, through an under-damped spring (he turns late and too far, and back), with a slow wander
// added; a stumble takes over for a moment (a lurch one way, fast), and on the ground he can't walk at all. Pad or keyboard alike.
static struct {
    uint32_t player;
    bool active, freeze;
    float level;
    float lurchHead, lurchSpeed, lurchW;   // a stumble: the way, the speed, how much of it (0 .. 1)
    float head, headVel;                   // the spring's heading and its rate
    bool init;
    float clock;
    struct { float t, dir, want; } hist[128];  // (the delay: 0.35 s at most - 64 frames held 0.27 s at 240 fps)
    int hn;
    float outDir, outWant, inDir, inWant;     // (report)
    float baseDir;                            // what we wrote, before the wander and the stagger (read back: his wish, not ours)
    bool sideStep;                            // this frame: he faces his way, his feet go the stagger's (a side or back step)
    float motionAngle;                        // ... the way they go (the walk's way turned toward the stagger's, as much as it's on)
} g_dw;
static float g_dwSideAt = 0.35f;              // (the stagger's weight from which it's a side/back step, not a turn and a walk)
// (v1.0, 2026-10-17: his legs are Radical's walk under every stagger - the drunk body draws only his falls - so a stagger is never a
// side or back step: the walk's facing and its motion kept together, its legs always go the way he goes (a side step under Radical's
// walk is a skate sideways). A stagger to a side bends his way toward it and quickens him, never past kDwLurchMax; one behind him checks
// him. Standing, none: drunk.inc sends none. And drunk he walks - a brisk walk at most, kDwCap from 0.6, coming and going a little -
// never the jog Radical's blend makes of 1.5 m/s.) Off ("dbwalk 1", the drunk body's own walk) as it was
bool g_dwHybrid = true;
static float kDwCap = 1.25f, kDwLurchMax = 2.2f;

// (lurchHead: the world way, atan2(x, z); the game's walk wants that + pi - as the aiming walk writes it: G.yaw + pi. Without it
// every catch step went away from his lean, not under it, and the lean ran away)
void Game_DrunkWalk(uint32_t player, bool active, float level, float lurchHead, float lurchSpeed, float lurchW, bool freeze)
{
    g_dw.player = player, g_dw.active = active && player, g_dw.level = level;
    g_dw.lurchHead = remainderf(lurchHead + 3.14159265f, 6.2831853f), g_dw.lurchSpeed = lurchSpeed, g_dw.lurchW = lurchW, g_dw.freeze = freeze;
    if (!g_dw.active) g_dw.init = false;
}
void Game_DrunkWalkState(float* inDir, float* inWant, float* outDir, float* outWant)
{
    *inDir = g_dw.inDir, *inWant = g_dw.inWant, *outDir = g_dw.outDir, *outWant = g_dw.outWant;
}

static void DrunkWalkFilter(uint32_t ch, float dt)
{
    float L = g_dw.level;
    float dRaw = Rd<float>(ch + 0x110), wIn = Rd<float>(ch + 0x124);
    // (standing, the game leaves the direction alone: what's there is ours from last frame - the wander and the stagger in it
    // were ours, not his wish; read back as his wish they piled up and he turned on the spot)
    float dIn = g_dw.init && fabsf(remainderf(dRaw - g_dw.outDir, 6.2831853f)) < 1e-4f ? g_dw.baseDir : dRaw;
    // (v1.0) the stagger's way is the world's; the walk's directions are his walk's frame (the car's, standing on one)
    float lurchHead = remainderf(g_dw.lurchHead - Game_ParentYaw(ch), 6.2831853f);
    g_dw.inDir = dIn, g_dw.inWant = wIn;
    if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 30.0f;
    g_dw.clock += dt;
    // the delay: his legs get the stick's orders late (0.25 s at level 1)
    // (a new drunk spell starts it empty: the last spell's orders, its clock stopped since, read as a moment ago - drunk again
    // walking, the game turned him in one frame to where he'd been going when the last spell ended: 47 deg, then back)
    if (!g_dw.init) g_dw.hn = 0;
    g_dw.hist[g_dw.hn % 128] = {g_dw.clock, dIn, wIn};
    g_dw.hn++;
    float delay = 0.25f * fminf(L, 1.4f), dDel = dIn, wDel = wIn;
    for (int i = 1; i < 128 && i <= g_dw.hn; i++) {
        const auto& h = g_dw.hist[(g_dw.hn - i) % 128];
        dDel = h.dir, wDel = h.want;
        if (g_dw.clock - h.t >= delay) break;
    }
    if (!g_dw.init) g_dw.head = dDel, g_dw.headVel = 0.0f, g_dw.init = true;
    // the spring: turns late and too far (level 1: ~0.8 Hz, 40 % of critical damping - a quarter too far, then back)
    float w = 12.0f - 7.0f * fminf(L, 1.0f), z = 1.0f - 0.6f * fminf(L, 1.0f);
    if (wDel > 0.01f) {
        float err = remainderf(dDel - g_dw.head, 6.2831853f);
        g_dw.headVel += (w * w * err - 2.0f * z * w * g_dw.headVel) * dt;
        g_dw.head = remainderf(g_dw.head + g_dw.headVel * dt, 6.2831853f);
    } else {
        g_dw.head = wIn > 0.01f ? dDel : dIn, g_dw.headVel *= expf(-13.4f * dt);   // (standing: no delay to turn to - a warp, a get-up;
                                                                                    // 0.8 a frame at 60 fps, at any frame rate)
    }
    g_dw.baseDir = g_dw.head;
    // the wander: a slow drift either way (17 degrees at level 1)
    float c = g_dw.clock;
    float wander = (0.6f * sinf(6.2831853f * 0.23f * c + 1.3f) + 0.4f * sinf(6.2831853f * 0.51f * c + 4.1f)) * 0.30f * fminf(L, 1.3f);
    float dOut = g_dw.head + wander, wOut = wDel * (1.0f - 0.12f * fminf(L, 1.0f));
    // drunk, he can't run: from a run sober down to a brisk walk by 0.6 (his staggers aren't capped: they're his feet catching him)
    float lu = L / 0.6f < 0.0f ? 0.0f : L / 0.6f > 1.0f ? 1.0f : L / 0.6f;
    float capLo = g_dwHybrid ? kDwCap * (1.0f + 0.10f * fminf(L, 1.0f) * sinf(6.2831853f * 0.21f * c + 0.7f)) : 1.5f;
    float cap = 3.6f - (3.6f - capLo) * lu * lu * (3.0f - 2.0f * lu);
    if (wOut > cap) wOut = cap;
    g_dw.sideStep = false;
    if (g_dw.lurchW > 0.0f && g_dwHybrid) {   // (v1.0: his way bends toward the stagger and he quickens; behind him, he checks)
        float lw = fminf(g_dw.lurchW, 1.0f), off = remainderf(lurchHead - dOut, 6.2831853f);
        if (wDel > 0.01f && !g_dw.freeze) {
            if (fabsf(off) > 1.75f) wOut *= 1.0f - 0.6f * lw;
            else {
                dOut = remainderf(dOut + off * lw * 0.85f, 6.2831853f);
                wOut = wOut + (fminf(g_dw.lurchSpeed, kDwLurchMax) - wOut) * lw;
            }
        }
    } else if (g_dw.lurchW > 0.0f) {   // a stumble takes over: a strong one is a step his feet take on their own - he keeps facing where
        // he was going and side-steps or steps back (the walk's motion set apart from its facing, as the aimed walk does); a light
        // one just bends his way
        // (standing, or a stagger backwards: always a step, never a turn - bending his way to a sway behind him turned him round)
        // (and only a real stagger - a whisper of one, its way noise, set the whole walk going that way and back, frame by frame:
        // the step's way is the walk's turned toward the stagger's as much as the stagger is on)
        float lw = fminf(g_dw.lurchW, 1.0f);
        bool back = fabsf(remainderf(lurchHead - dOut, 6.2831853f)) > 1.75f, standing = wDel <= 0.01f;
        if ((lw >= g_dwSideAt || ((back || standing) && lw > 0.08f)) && !g_dw.freeze) {
            g_dw.sideStep = true, wOut = wOut + (g_dw.lurchSpeed - wOut) * lw;
            g_dw.motionAngle = standing ? lurchHead : remainderf(dOut + remainderf(lurchHead - dOut, 6.2831853f) * lw, 6.2831853f);
        } else {
            dOut = dOut + remainderf(lurchHead - dOut, 6.2831853f) * lw;
            wOut = wOut + (g_dw.lurchSpeed - wOut) * lw;
        }
    }
    if (g_dw.freeze) wOut = 0.0f, dOut = dIn;
    g_dw.outDir = dOut, g_dw.outWant = wOut;
    Wr(ch + 0x110, &dOut, 4);
    Wr(ch + 0x124, &wOut, 4);
}

static void __fastcall h_WlaSim(uint32_t self, void* edx, float t)
{
    uint32_t ch = Rd<uint32_t>(self + 0x1C);
    if (g_dw.active && ch == g_dw.player && ch != g_am.player && Game_WalkerActive(ch)) {   // (drunk, not aiming)
        DrunkWalkFilter(ch, t);
        o_WlaSim(self, edx, t);
        uint32_t drv = Rd<uint32_t>(self + 0x24);
        if (g_dw.sideStep && drv) ((PFN_SetAngle)A_SetMotionAngle)(drv, nullptr, g_dw.motionAngle);
        return;
    }
    if (!g_am.player || ch != g_am.player || !Game_WalkerActive(ch)) {
        o_WlaSim(self, edx, t);
        return;
    }
    float desired = Rd<float>(ch + 0x110), want = Rd<float>(ch + 0x124);
    // (v1.0) the aim's facing is the world's; the game's directions here are his walk's frame (the car's, standing on one)
    float facing = remainderf(g_am.facing - Game_ParentYaw(ch), 6.2831853f);
    // A gentle push more than 90 degrees from where he faces is thrown away by the game
    // (CameraRelativeCharacterController::GetDirection scales it by 0.01 below 0.6 of the stick: a
    // stick springing back when let go). Facing the aim, that is every slow step backwards, so for
    // a push held over two frames do what the game does for any other direction: the camera's
    // way (the follow camera is kept on the aim) and its speed curve (measured: full speed from 0.85
    // of the stick, sqrt(speed / 4) linear in the push from 0.1).
    float mag = sqrtf(g_am.sx * g_am.sx + g_am.sy * g_am.sy), rel = atan2f(g_am.sx, g_am.sy);
    g_am.backFrames = (want < 0.001f && mag > 0.12f && fabsf(rel) > 1.5708f) ? g_am.backFrames + 1 : 0;
    if (g_am.backFrames >= 2) {
        float k = fminf(1.0f, 1.335f * (mag - 0.0985f));
        want = 4.0f * k * k;
        desired = facing + rel;
        Wr(ch + 0x124, &want, 4);
    }
    if (want > 0.0f) {   // the stick is pushed: this frame's direction is the stick's
        g_am.moveDir = desired;
        g_am.haveMove = true;
    }
    // Turbo (the dash button) runs the forward dash cycle at twice the speed and turns him slowly.
    // Aiming, it is kept for running at what he aims at (within ~35 degrees); any other way he
    // runs at his normal top speed and turns normally: the turbo flag (+0x33C) is hidden from the
    // walking action for this call only and put back (the game sets it from the button and
    // starts its turbo event when it changes), and the speed asked for comes down to a run.
    uint8_t turbo = Rd<uint8_t>(ch + 0x33C);
    bool noTurbo = false;
    if (turbo) {
        float rel = fabsf(remainderf(g_am.moveDir - facing, 6.2831853f));
        noTurbo = g_am.haveMove && want > 0.0f && rel > 0.61f;
        if (noTurbo) {
            uint8_t off = 0;
            float run = fminf(want, 4.0f);
            Wr(ch + 0x33C, &off, 1);
            Wr(ch + 0x124, &run, 4);
        }
    }
    Wr(ch + 0x110, &facing, 4);
    o_WlaSim(self, edx, t);
    if (noTurbo) Wr(ch + 0x33C, &turbo, 1);
    uint32_t drv = Rd<uint32_t>(self + 0x24);
    if (drv && g_am.haveMove) ((PFN_SetAngle)A_SetMotionAngle)(drv, nullptr, g_am.moveDir);
    if (g_am.log > 0) {
        g_am.log--;
        float pos[3] = {};
        ReadMem(ch + 0x64, pos, 12);
        Log("movelog: want %.2f stick %.3f aim %.3f | drv motion %.3f facing %.3f speed %.2f | turbo %d | pos %.2f %.2f %.2f dt %.3f",
            want, g_am.moveDir, facing, Rd<float>(drv + 0x30), Rd<float>(drv + 0x34), Rd<float>(drv + 0x38),
            Rd<uint8_t>(ch + 0x33C) | (noTurbo ? 2 : 0), pos[0], pos[1], pos[2], t);
    }
}

static void AimMoveInstall()
{
    uint32_t slot = A_WlaVtable + 5 * 4;
    if (Rd<uint32_t>(slot) != A_WlaSim || Rd<uint32_t>(A_WlaVtable + 6 * 4) != 0x40B020) {
        Log("Aimed walking: walking action not where expected - aiming keeps him standing");
        return;
    }
    o_WlaSim = (PFN_WlaSim)(uintptr_t)A_WlaSim;
    void* hook = (void*)h_WlaSim;
    DWORD old;
    if (!VirtualProtect((void*)(uintptr_t)slot, 4, PAGE_READWRITE, &old)) return;
    *(void**)(uintptr_t)slot = hook;
    VirtualProtect((void*)(uintptr_t)slot, 4, old, &old);
    Log("Aimed walking: hooked the walking action");
}

bool Game_AimMoveAvailable() { return o_WlaSim != nullptr; }

void Game_AimMove(uint32_t player, float facing, float stickX, float stickY)
{
    g_am.sx = stickX;
    g_am.sy = stickY;
    if (player != g_am.player) g_am.haveMove = false;   // a new aim (or none): no direction from before
    g_am.player = player;
    g_am.facing = facing;
}

void Game_AimMoveLog(int frames) { g_am.log = frames; }

void Game_Read(GameState* gs)
{
    memset(gs, 0, sizeof(*gs));
    if (!A_GameFlow) return;
    gs->known = true;
    gs->context = -1;
    gs->nextContext = -1;

    uint32_t gf = Rd<uint32_t>(A_GameFlow);
    if (gf) {
        gs->context = (int)Rd<uint32_t>(gf + 0x8, 0xFFFFFFFF);       // GameFlow::CurrentContext
        gs->nextContext = (int)Rd<uint32_t>(gf + 0xC, 0xFFFFFFFF);   // GameFlow::NextContext
    }
    uint32_t im = Rd<uint32_t>(A_InputManager);
    if (im) gs->inputState = Rd<uint32_t>(im + 0x1C0C);              // InputManager::GameState
    uint32_t cm = Rd<uint32_t>(A_CharacterManager);
    if (cm) {
        uint32_t player = Rd<uint32_t>(cm + 192);                     // Characters[0]
        if (player) {
            gs->hasPlayer = true;
            gs->inCar = Rd<uint8_t>(player + 0x128) != 0;             // Character::InCar
        }
    }
    bool gameplayInput = gs->inputState == INPUT_ACTIVE_GAMEPLAY || gs->inputState == INPUT_ACTIVE_ALL;
    gs->player = gs->hasPlayer ? Rd<uint32_t>(cm + 192) : 0;
    gs->onFoot = gs->context == GF_NORMAL_IN_GAME && gs->nextContext == GF_NORMAL_IN_GAME &&
                 gameplayInput && gs->hasPlayer && !gs->inCar;
}
