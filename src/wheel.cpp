// The weapon / item wheel: open rules, controls, animation and layout.
//
//   Hold LB (on foot, in normal gameplay)  -> wheel opens on the Weapons page,
//                                              background blurs, game slows down
//   Either stick (or the mouse)             -> highlight a slot
//   D-pad left/right                        -> round the page that way (from the top: left goes left)
//   The scroll wheel                        -> the next / previous slot by number
//   RB (or Q)                               -> switch Weapons <-> Items
//   Release LB (or Tab)                     -> close and equip the highlighted slot
// (v1.0) Weapons: fists (1) up, the handgun (2) left, the RPG (3) down, the shotgun (4) right - a quarter each, numbered round to
// the left (the agreed order). Items: the donut (5) left, the beer (6) right - a half each (each pick: another one). Slot numbers are
// the keys that equip them. Before: an 8-slot ring with fists and the handgun sharing slot 1 (flipped with the D-pad or the
// scroll wheel) and the rest "?" to come.
//
// Layout numbers are reference pixels for 1080p and match assets/mockup.py.
#include "render.h"
#include "hit.h"
#include <xinput.h>

static const float R_OUT = 330.0f, R_IN = 150.0f;
static const float PI = 3.14159265f;
static const char* TAB_NAMES[2] = {"WEAPONS", "ITEMS"};

static Renderer g_r;
static HWND g_hwnd;
static bool g_failed;
static bool g_markers;

void Gun_DebugMarkers(Renderer& r, uint32_t player);
void Gun_DrvOverlay(Renderer& r, uint32_t player);   // (diagnostic)
void Gun_DrvTick(float dt, uint32_t player);
void Gun_Draw(Renderer& r);
void Gun_DrawOverlay(Renderer& r);
bool Gun_DrunkGhost(float W, float* alpha, float* dx, float* dy);   // (drunk.inc: double vision this frame)
extern int g_cinema;   // debug "cinema 2": no hint bar under the wheel either (screenshots)
int g_recIdx = -1;     // debug "rec": the recording's number for the frame just shown (-1: not recorded; the pose trace notes it)
bool Gun_WantsProbe(float* p10, float* p14);
void Gun_SetProbe(float dist);
void Decal_Draw(Renderer& r);
bool Decal_Any();
bool CarGlass_Any();
void CarGlass_Draw(Renderer& r);
int Decal_ProbeWant(float (*px)[2], float (*range)[2], int max, int W, int H, uint32_t player);
int Gun_DaVisWant(float (*px)[2], float (*range)[2], int max, int W, int H);   // (diagnostic, m3.1)
void Gun_DaVisResult(const float* d, int n);
void Gun_AfterWorld(Renderer& r);
void Decal_ProbeResult(const float* depth, int n);
bool Lab_Wants();                                   // (lab.cpp, Debug=1) a coverage scan in progress
void Lab_OnWorldDrawn(Renderer& r);
void Lab_OnPresent(Renderer& r, uint32_t player);
static bool g_probeGun;      // this frame's probe: slot 0 is the crosshair
static int g_probeCover;     // and this many last: the next shot's pellet rays (vcover.cpp)
static int g_probeDecals;    // and this many after it are bullet holes
static int g_probeRockets;   // (v1.0) and this many after the drive-by's: ahead of rockets in flight (rocket.cpp)
static int g_probeDaVis;     // and this many after those: his head, hand and pistol from the car (m3.1)
static uint32_t g_probePlayer;
static bool g_gunInWorld;   // the gun was drawn into this frame's 3D world
float g_dbgTimeScale = 1.0f;   // debug "slowmo <scale>": game speed with the wheel shut

// the game finished its 3D world (it is about to clear depth for the HUD)
void Wheel_OnWorldDrawn(IDirect3DDevice8* dev)
{
    (void)dev;
    bool gun = Gun_NeedsDraw();
    if (g_failed || !g_r.loaded || (!gun && !Decal_Any() && !CarGlass_Any() && !Lab_Wants())) return;
    if (g_r.BeginInScene()) {
        g_r.k = (float)g_r.H / 1080.0f * g_cfg.uiScale;
        // before the gun draws (it writes depth): how far the world is under the crosshair,
        // and where the visible surface is under new bullet holes
        float p10, p14, px[Renderer::kProbeMax][2], range[Renderer::kProbeMax][2] = {};
        g_probeGun = Gun_WantsProbe(&p10, &p14);
        int k = 0;
        if (g_probeGun) {
            px[0][0] = g_r.W * 0.5f;
            px[0][1] = g_r.H * 0.5f;
            k = 1;
        }
        g_probeDecals = Decal_ProbeWant(px + k, range + k, 4 - k, g_r.W, g_r.H, g_probePlayer);
        g_probeDaVis = Gun_DaVisWant(px + k + g_probeDecals, range + k + g_probeDecals, 4 - k - g_probeDecals, g_r.W, g_r.H);
        g_probeRockets = Rocket_ProbeWant(px, range, k + g_probeDecals + g_probeDaVis, 8, g_r.W, g_r.H);   // (v1.0) ahead of rockets
        int used = k + g_probeDecals + g_probeDaVis + g_probeRockets;
        g_probeCover = VCover_ProbeWant(px + used, range + used, Renderer::kProbeMax - used, g_r.W, g_r.H);
        if ((g_probeDecals || g_probeDaVis || g_probeRockets || g_probeCover) && !g_probeGun) {
            float v[16], pj[16];
            if (Game_GetCamera(v, pj)) p10 = pj[10], p14 = pj[14];
            else g_probeDecals = g_probeDaVis = g_probeRockets = g_probeCover = 0;
        }
        if (used + g_probeCover > 0 && !g_r.ProbeDraw(px, used + g_probeCover, p10, p14, range))
            g_probeDecals = g_probeDaVis = g_probeRockets = g_probeCover = 0;
        if (g_cfg.debug) Lab_OnWorldDrawn(g_r);   // (lab) a coverage scan's batch, before anything of ours writes depth
        Decal_Draw(g_r);   // bullet holes first: the gun, smoke and flashes go over them
        CarGlass_Draw(g_r);   // broken glass
        if (gun) Gun_Draw(g_r);
        g_r.End();
        g_gunInWorld = gun;
        Gun_AfterWorld(g_r);   // (m3.1: the picture under the radar, before the HUD)
    }
}

struct Spark {
    float x, y, t, life, rot, spin, size;
};

static struct {
    bool open;
    float openT;          // 0..1 open/close animation
    float introT;         // seconds since opened
    int tab, shownTab;    // logical page / page currently drawn
    float flipT;          // tab flip 0..1 (1 = finished)
    float tabT;           // 0 = weapons tab lit, 1 = items tab lit
    int sel;
    int lastSel[2];
    float pop[8];         // (each slot's highlight, eased)
    float bump;
    double timeScale;
    bool lbPrev, rbPrev, dlPrev, drPrev;
    bool byKey;          // opened with the keyboard's wheel key
    float mvx, mvy;      // the mouse as a stick in the wheel: counts moved, kept within a circle
    double last;
    Spark sparks[24];
    int nsparks;
    uint32_t rng;
    GameState lastGs;
    bool lastGsValid;
    int openFrames;       // frame-rate check while open
    double openStart;
} W;

// what a slot holds (v1.0: four weapons, two items, nothing to come). `key`: the number key that equips it, shown on the slot
struct SlotInfo {
    const char* name;
    const char* icon;
    const char* desc;
    bool locked;
    int key;
};
static const int kSlots[2] = {4, 2};   // slots a page: Weapons, Items
static SlotInfo SlotAt(int tab, int i)
{
    if (tab == 0) {
        static const SlotInfo w[4] = {{"FISTS", "icon_hand", "Unarmed", false, 1}, {"HANDGUN", "icon_gun", "Pistol", false, 2},
                                      {"RPG", "icon_rpg", "Rocket launcher", false, 3}, {"SHOTGUN", "icon_shotgun", "Pump-action", false, 4}};
        return w[i & 3];
    }
    if (i == 0) return {"DONUT", "icon_donut", "Pink, sprinkled", false, 5};
    if (Gun_BeerAllowed()) return {"BEER", "icon_beer", "Ice cold", false, 6};
    return {"BEER", "icon_beer", "Grown-ups only", true, 6};   // (not for Bart or Lisa: there, but dimmed)
}
// the weapon a slot equips (WPN_*), -1 = none
static int SlotWeapon(int tab, int i)
{
    if (tab == 1) return i == 0 ? WPN_DONUT : Gun_BeerAllowed() ? WPN_BEER : -1;   // (each pick, another one)
    static const int w[4] = {WPN_NONE, WPN_HANDGUN, WPN_RPG, WPN_SHOTGUN};
    return w[i & 3];
}
// where a slot sits: its direction on screen (degrees, 0 to the right, clockwise) - and its art: the quarter or half that way up,
// mirrored for the left (the art is lit from the top, so only left and right mirror)
static float SlotAngle(int tab, int i)
{
    static const float w[4] = {-90.0f, 180.0f, 90.0f, 0.0f};   // fists up, the handgun left, the RPG down, the shotgun right
    if (tab == 0) return w[i & 3];
    return i == 0 ? 180.0f : 0.0f;                             // the donut left, the beer right
}
static void SlotArt(int tab, int i, char* seg, char* glow, bool* mirror)
{
    if (tab == 0) {
        int k = i == 3 ? 1 : i;   // (q0 up, q1 right, q2 down; the handgun's is the right one mirrored)
        sprintf(seg, "q%d", k), sprintf(glow, "qglow%d", k);
        *mirror = i == 1;
    } else {
        sprintf(seg, "h0"), sprintf(glow, "hglow0");
        *mirror = i == 0;
    }
}
// the page slot holding a weapon (-1 none)
static int SlotOf(int weapon)
{
    switch (weapon) {
    case WPN_NONE: return 0;
    case WPN_HANDGUN: return 1;
    case WPN_RPG: return 2;
    case WPN_SHOTGUN: return 3;
    case WPN_DONUT: return 0;
    case WPN_BEER: return 1;
    }
    return -1;
}

static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float Smooth(float t) { t = Clamp01(t); return t * t * (3 - 2 * t); }
static float EaseOutBack(float t)
{
    t = Clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}
static float EaseOutCubic(float t) { t = Clamp01(t); return 1 - powf(1 - t, 3); }
static float Rand01()
{
    W.rng = W.rng * 1664525u + 1013904223u;
    return (float)((W.rng >> 8) & 0xFFFF) / 65535.0f;
}

// ------------------------------------------------------------------ device events
void Wheel_OnDevice(IDirect3DDevice8* dev)
{
    if (g_r.loaded) g_r.Release();
    g_failed = false;
    W.timeScale = 1.0;
    W.rng = GetTickCount();
    Sound_Init();
    if (!g_r.Load(dev)) {
        g_failed = true;
        Log("Wheel disabled: assets failed to load");
    }
}

void Wheel_OnLostDevice() { g_r.OnLost(); }
void Wheel_OnResetDevice() {}

// ------------------------------------------------------------------ logic
static void OpenWheel()
{
    W.open = true;
    W.introT = 0;
    W.tab = W.shownTab = 0;   // always starts on Weapons
    W.tabT = 0;
    W.flipT = 1;
    W.sel = W.lastSel[0] < kSlots[0] ? W.lastSel[0] : 0;
    W.bump = 1;
    W.nsparks = 0;
    W.openFrames = 0;
    W.openStart = Time_Real();
    W.mvx = W.mvy = 0;
    long calls, filtered;
    Input_Stats(&calls, &filtered);   // reset counters
    Sound_Play(SND_OPEN, 0.8f);
    Log("Wheel open");
}

static void CloseWheel(const char* why, bool chosen)
{
    if (!W.open) return;
    W.open = false;
    // (the page on screen: for the first half of a page flip W.tab is already the new page while
    // the old one, and its highlighted slot, are still showing)
    W.lastSel[W.shownTab] = W.sel;
    // letting go of the wheel button takes what the highlighted slot holds, with the equip / holster sound (a dimmed one - the
    // kids' beer - just closes)
    int want = chosen ? SlotWeapon(W.shownTab, W.sel) : -1;
    if (want >= 0 && want != Gun_Weapon())
        Gun_Equip(want);
    else
        Sound_Play(SND_CLOSE, 0.7f);
    double secs = Time_Real() - W.openStart;
    long calls, filtered;
    Input_Stats(&calls, &filtered);
    Log("Wheel close (%s), %s slot %d - open %.1fs at %.0f fps, game pad reads %ld (blocked %ld)", why,
        TAB_NAMES[W.shownTab], W.sel + 1, secs, secs > 0 ? W.openFrames / secs : 0.0, calls, filtered);
}

static void SpawnSparks(int slot, float cx, float cy, float k)
{
    float ang = SlotAngle(W.tab, slot) * PI / 180.0f;
    for (int i = 0; i < 3 && W.nsparks < 24; i++) {
        Spark& s = W.sparks[W.nsparks++];
        float a = ang + (Rand01() - 0.5f) * 0.7f;
        float r = (R_OUT - 10.0f + Rand01() * 40.0f) * k;
        s.x = cx + cosf(a) * r;
        s.y = cy + sinf(a) * r;
        s.t = -i * 0.05f;
        s.life = 0.42f + Rand01() * 0.15f;
        s.rot = Rand01() * PI;
        s.spin = (Rand01() - 0.5f) * 6.0f;
        s.size = 0.45f + Rand01() * 0.35f;
    }
}

static void LogStateChanges(const GameState& gs)
{
    if (W.lastGsValid && gs.context == W.lastGs.context && gs.nextContext == W.lastGs.nextContext &&
        gs.inputState == W.lastGs.inputState && gs.inCar == W.lastGs.inCar && gs.hasPlayer == W.lastGs.hasPlayer)
        return;
    Log("Game state: context=%d next=%d input=0x%X player=%d inCar=%d -> wheel %s", gs.context, gs.nextContext,
        gs.inputState, gs.hasPlayer, gs.inCar, gs.onFoot ? "ALLOWED" : "blocked");
    W.lastGs = gs;
    W.lastGsValid = true;
}

// ------------------------------------------------------------------ drawing
static void DrawWheel(float fade)
{
    Renderer& r = g_r;
    const float k = r.k;
    const float cx = r.W * 0.5f, cy = r.H * 0.5f + 14.0f * k;

    float intro = W.open ? W.introT : 10.0f;
    float scale = W.open ? 0.82f + 0.18f * EaseOutBack(intro / 0.26f) : 0.94f + 0.06f * W.openT;
    float alpha = W.open ? Clamp01(intro / 0.10f) : W.openT;
    float flip = fabsf(cosf(PI * W.flipT));
    if (flip < 0.03f) flip = 0.03f;

    // ---- slots
    r.SetGroup(cx, cy, flip, 1.0f);
    int nslots = kSlots[W.shownTab & 1];
    for (int pass = 0; pass < 2; pass++) {   // selected slot last so it sits on top
        for (int i = 0; i < nslots; i++) {
            bool isSel = (i == W.sel);
            if ((pass == 1) != isSel) continue;
            char seg[16], glow[16], n[16];
            bool mirror;
            SlotArt(W.shownTab, i, seg, glow, &mirror);
            float local = W.open ? Clamp01((intro - i * 0.03f) / 0.20f) : 1.0f;
            float segScale = scale * (0.70f + 0.30f * EaseOutBack(local));
            float segAlpha = alpha * Clamp01(local * 2.0f);
            float pop = W.pop[i];
            float ang = SlotAngle(W.shownTab, i) * PI / 180.0f;
            float dx = cosf(ang), dy = sinf(ang);
            float px = cx + dx * 7.0f * k * pop, py = cy + dy * 7.0f * k * pop;
            float s = segScale * (1.0f + 0.04f * pop);
            SlotInfo si = SlotAt(W.shownTab, i);
            // (dimmed - the kids' beer: the same panel, darker; its own colours drained by the tint)
            Color tint = si.locked ? Rgba(0.50f, 0.52f, 0.60f, segAlpha) : Rgba(1, 1, 1, segAlpha);
            if (pop > 0.01f) {
                Color gc = si.locked ? Rgba(0.75f, 0.78f, 0.85f, 0.45f * pop * segAlpha) : Rgba(1, 1, 1, 0.85f * pop * segAlpha);
                r.Sprite2(r.Find(glow), px, py, s * 1.02f, gc, mirror);
            }
            snprintf(n, sizeof(n), "%sn", seg);
            r.Sprite2(r.Find(n), px, py, s, tint, mirror);
            if (pop > 0.01f) {
                snprintf(n, sizeof(n), "%ss", seg);
                r.Sprite2(r.Find(n), px, py, s, Rgba(tint.r, tint.g, tint.b, segAlpha * pop), mirror);
            }
            // the slot's key (the game's gold digits), icon, name - in the middle of its panel
            float rc = ((R_IN + R_OUT) * 0.5f + 2.0f) * s * k;
            float tx = px + dx * rc, ty = py + dy * rc;
            float da = segAlpha * (si.locked ? 0.55f : 1.0f);
            // (the bigger panels - a quarter, a half - take bigger icons than the old 45-degree slots did)
            float big = W.shownTab == 0 ? 1.25f : 1.6f, spread = W.shownTab == 0 ? 1.08f : 1.22f;
            snprintf(n, sizeof(n), "digit%d", si.key);
            r.Sprite2(r.Find(n), tx, ty - 47.0f * spread * k * s, (0.60f + 0.12f * pop) * s, Rgba(1, 1, 1, da * (0.8f + 0.2f * pop)));
            float iconY = ty - 4.0f * k * s;
            float isz = (0.78f + 0.12f * pop) * s * big;
            r.Sprite2(r.Find("blob"), tx, iconY + 4.0f * k * s * big, 0.95f * s * big, Rgba(0, 0, 0, segAlpha * 0.30f));
            Color ic = si.locked ? Rgba(0.55f, 0.55f, 0.60f, da) : Rgba(1, 1, 1, segAlpha);
            r.Sprite2(r.Find(si.icon), tx, iconY, isz, ic, false, 0.0f, BLEND_PREMUL, 1.0f, 1.0f);
            // the name: pale blue on navy; white on the gold of the highlighted slot, with its shadow kept (v1.0: it went near-black
            // there - review: "make the highlighted text white or otherwise consistently high-contrast"), as the tabs' names are
            Color lc = {0.76f + (1.0f - 0.76f) * pop, 0.82f + (1.0f - 0.82f) * pop, 1.0f, da * (0.82f + 0.18f * pop)};
            if (si.locked) lc = {0.62f, 0.64f, 0.72f, lc.a};
            r.Text('W', si.name, tx, ty + 47.0f * spread * k * s, 0.70f * s, lc, lc, ALIGN_CENTER, 0.55f + 0.35f * pop);
        }
    }

    // ---- hub
    r.Sprite2(r.Find("hub"), cx, cy, scale, Rgba(1, 1, 1, alpha));
    SlotInfo hs = SlotAt(W.shownTab, W.sel);
    const char* title = hs.name;
    float tw = r.TextWidth('B', title, 1.0f);
    float tsc = fminf(1.0f, 196.0f * k / fmaxf(tw, 1.0f)) * scale * (1.0f + 0.07f * W.bump);
    float ta = alpha * (hs.locked ? 0.7f : 1.0f);
    r.Text('B', title, cx, cy - 6.0f * k * scale, tsc, Rgba(1.0f, 0.98f, 0.62f, ta), Rgba(0.98f, 0.62f, 0.08f, ta),
           ALIGN_CENTER, 0.6f);
    // (just what it is - review: no "Slot 4 - " in front, the slot's own number says it)
    r.Text('W', hs.desc, cx, cy + 32.0f * k * scale, 0.80f * scale, Rgba(1, 1, 1, alpha), Rgba(0.86f, 0.9f, 1, alpha),
           ALIGN_CENTER, 0.5f);
    bool equipped = SlotWeapon(W.shownTab, W.sel) == Gun_Weapon();
    const char* status = hs.locked ? "Not for kids" : equipped ? "Equipped" : "Release to equip";
    Color stc = hs.locked ? Rgba(0.62f, 0.66f, 0.80f, alpha)
                          : equipped ? Rgba(0.55f, 1.0f, 0.55f, alpha) : Rgba(0.66f, 0.73f, 0.98f, alpha);
    r.Text('W', status, cx, cy + 60.0f * k * scale, 0.76f * scale, stc, stc, ALIGN_CENTER, 0.5f);
    r.ClearGroup();

    // ---- tabs (slide down)
    float slide = (1.0f - EaseOutCubic(intro / 0.28f)) * 26.0f * k;
    float ty = cy - R_OUT * k - 52.0f * k - slide;
    float tabw = 232.0f * k, gap = 18.0f * k;
    for (int j = 0; j < 2; j++) {
        float tx = cx + (j - 0.5f) * (tabw + gap);
        float lit = j == 0 ? 1.0f - W.tabT : W.tabT;
        r.Capsule("taboff", tx, ty, tabw, 1.0f, Rgba(1, 1, 1, alpha));
        r.Capsule("tabon", tx, ty, tabw, 1.0f, Rgba(1, 1, 1, alpha * lit));
        Color tc = {0.78f + 0.22f * lit, 0.82f + 0.18f * lit, 1.0f, alpha * (0.85f + 0.15f * lit)};
        r.Text('B', TAB_NAMES[j], tx, ty + 15.0f * k, 0.60f * (1.0f + 0.04f * lit), tc, tc, ALIGN_CENTER, 0.0f);
    }
    float wob = 1.0f + 0.05f * sinf((float)W.introT * 6.0f);
    r.Sprite2(r.Find("arrow_l"), cx - (tabw + gap * 0.5f) - 40.0f * k, ty, 1.15f * wob, Rgba(1, 1, 1, alpha));
    r.Sprite2(r.Find("arrow_r"), cx + (tabw + gap * 0.5f) + 40.0f * k, ty, 1.15f * wob, Rgba(1, 1, 1, alpha));

    // (v1.0: no hint bar under the wheel - review: "remove the mod-added blue control-hint bars and prompts". The controls are in
    // the README)

    // ---- sparkles
    const Sprite* sp = r.Find("sparkle");
    for (int i = 0; i < W.nsparks; i++) {
        Spark& s = W.sparks[i];
        if (s.t < 0) continue;
        float u = s.t / s.life;
        float sz = sinf(PI * Clamp01(u)) * s.size;
        r.Sprite2(sp, s.x, s.y, sz, Rgba(1, 1, 1, alpha * Clamp01(2.0f - u * 2.0f)), false, s.rot);
    }
    (void)fade;
}

// ------------------------------------------------------------------ per frame
void Wheel_OnPresent(IDirect3DDevice8* dev, IDirect3DSurface8* target, HWND hwnd)
{
    if (g_failed || !g_r.loaded) return;
    g_hwnd = hwnd;
    Sound_SetWindow(hwnd);
    double now = Time_Real();
    float dt = W.last > 0 ? (float)(now - W.last) : 0.016f;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    W.last = now;

    Pad pad;
    Input_Poll(&pad);
    GameState gs;
    Game_Read(&gs);
    LogStateChanges(gs);

    Scene_EndFrame();
    DebugCmds dbg = {};
    dbg.tab = -1;
    if (g_cfg.debug) {
        Debug_Poll(&dbg);
        Debug_TestTick(dt, gs);
    }
    if (g_cfg.debug || g_cfg.driveBy) Gun_DrvTick(dt, gs.player);   // (diagnostic; m3.1: also with DriveBy=1)
    if (dbg.probe > 0) Scene_Probe(dbg.probe);
    if (dbg.markers >= 0) g_markers = dbg.markers != 0;
    Debug_ApplyPokes('p');
    Gun_SetPlayer(gs.onFoot || gs.hasPlayer ? gs.player : 0);
    // (sparkles spawn before anything is drawn this frame: from the target itself - the renderer's
    // size is still 0 x 0 before its first frame, and the first open's sparkles went to the corner)
    float sparkW = (float)g_r.W, sparkH = (float)g_r.H;
    {
        D3DSURFACE_DESC td;
        if (SUCCEEDED(target->GetDesc(&td)) && td.Height) {
            Scene_SetAspect((float)td.Width / (float)td.Height);
            sparkW = (float)td.Width;
            sparkH = (float)td.Height;
        }
    }
    float sparkK = sparkH / 1080.0f * g_cfg.uiScale;

    HWND fg = GetForegroundWindow();
    bool focused = fg && (fg == g_hwnd || GetAncestor(fg, GA_ROOTOWNER) == GetAncestor(g_hwnd, GA_ROOTOWNER));
    if (Kbm_DebugFocus() >= 0) focused = Kbm_DebugFocus() != 0;
    bool allowed = (gs.onFoot && focused) || dbg.forceOpen;
    bool gameplay = gs.context == 9 && gs.nextContext == 9 && (gs.inputState == 1 || gs.inputState == 0xFFFFFFFFu);
    Kbm_Update(dt, pad, focused, gameplay && focused);
    const Kbm& kb = Kbm_Get();
    bool padLb = (pad.buttons & (g_cfg.wheelButtons & 0xFFFF)) != 0;   // WheelButton (LB by default)
    bool lb = padLb || kb.wheel;                                         // or KeyWheel (Tab)
    bool held = lb || dbg.forceOpen;

    if (!W.open) {
        if (allowed && ((lb && !W.lbPrev) || dbg.forceOpen)) {
            OpenWheel();
            W.byKey = kb.wheel && !padLb;
        }
        else if (lb && !W.lbPrev)
            Log("LB ignored: context=%d next=%d input=0x%X player=%d inCar=%d focused=%d", gs.context,
                gs.nextContext, gs.inputState, gs.hasPlayer, gs.inCar, focused);
    } else if (!held) {
        // (keys read as up once the window loses focus: that closes it without taking a slot)
        bool ok = focused && (pad.connected || W.byKey);
        CloseWheel(!focused ? "focus lost" : W.byKey ? "key released" : ok ? "LB released" : "pad disconnected", ok);
    } else if (!allowed) {
        CloseWheel(!gs.onFoot ? "game state" : "focus lost", false);
    }
    W.lbPrev = lb;

    // ---- controls while open
    bool rb = (pad.buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    bool dl = (pad.buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    bool dr = (pad.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    if (W.open) {
        // either stick selects (whichever is pushed further); a real stick always
        // wins over the debug "stick" command
        float lmag = sqrtf(pad.lx * pad.lx + pad.ly * pad.ly);
        float rmag = sqrtf(pad.rx * pad.rx + pad.ry * pad.ry);
        float rx = rmag >= lmag ? pad.rx : pad.lx;
        float ry = rmag >= lmag ? pad.ry : pad.ly;
        // the mouse: its movement since the wheel opened, as a stick (kept within a circle, so
        // turning back the other way answers at once)
        W.mvx += kb.mdx;
        W.mvy -= kb.mdy;
        float R = g_cfg.wheelMouse, mm = sqrtf(W.mvx * W.mvx + W.mvy * W.mvy);
        if (mm > R) W.mvx *= R / mm, W.mvy *= R / mm, mm = R;
        if (mm / R > fmaxf(lmag, rmag)) {
            rx = W.mvx / R;
            ry = W.mvy / R;
        }
        if (dbg.haveStick && fmaxf(lmag, rmag) < 0.3f && mm / R < 0.3f) {
            rx = sinf(dbg.stickAngle * PI / 180.0f);
            ry = cosf(dbg.stickAngle * PI / 180.0f);
        }
        float mag = sqrtf(rx * rx + ry * ry);
        int nsl = kSlots[W.shownTab & 1];
        auto pick = [&](int idx) {
            W.sel = idx;
            W.bump = 1;
            SpawnSparks(idx, sparkW * 0.5f, sparkH * 0.5f + 14.0f * sparkK, sparkK);
            Sound_Play(SND_TICK, 0.8f, 1.0f + 0.05f * idx);   // a little higher around the wheel
        };
        if (mag > 0.5f && W.flipT >= 1.0f) {
            // the slot whose sector the push points into - a quarter each (Weapons), a half each (Items); past the current one's
            // edge by a few degrees before it changes (no flicker on the line between two)
            float ang = atan2f(rx, ry) * 180.0f / PI;   // 0 = up, clockwise
            if (ang < 0) ang += 360.0f;
            float sector = 360.0f / nsl;
            int best = W.sel;
            float bestDiff = 1e9f;
            for (int i = 0; i < nsl; i++) {
                float centre = SlotAngle(W.shownTab, i) + 90.0f;   // (screen degrees from the right -> from up)
                float diff = fabsf(fmodf(ang - centre + 720.0f + 180.0f, 360.0f) - 180.0f);
                if (diff < bestDiff) bestDiff = diff, best = i;
            }
            float cur = fabsf(fmodf(ang - (SlotAngle(W.shownTab, W.sel) + 90.0f) + 720.0f + 180.0f, 360.0f) - 180.0f);
            if (best != W.sel && cur > sector * 0.5f + 4.0f) pick(best);
        }
        // the D-pad: round the page that way (right = clockwise; the numbers go round to the left, so right is the previous one);
        // the scroll wheel: the next (toward you) / previous slot by number, as with the wheel shut
        int cyc = (dl && !W.dlPrev) ? 1 : (dr && !W.drPrev) ? -1 : kb.scroll ? (kb.scroll > 0 ? -1 : 1) : dbg.cycle;
        if (cyc && W.flipT >= 1.0f) pick((W.sel + cyc + nsl) % nsl);
        bool sw = (rb && !W.rbPrev) || kb.page || (dbg.tab >= 0 && dbg.tab != W.tab);
        if (sw) {
            Sound_Play(SND_PAGE, 0.8f);
            W.lastSel[W.shownTab] = W.sel;   // (W.sel is the page on screen's, even mid-flip)
            W.tab = dbg.tab >= 0 ? dbg.tab : 1 - W.tab;
            W.flipT = 0;
        }
    }
    W.rbPrev = rb;
    W.dlPrev = dl;
    W.drPrev = dr;
    Input_SetBlock(W.open);

    // keyboard and mouse, wheel shut: weapon keys, the scroll wheel (down = next), the last weapon
    {
        static int s_cur = WPN_NONE, s_prev = WPN_HANDGUN;
        if (Gun_Weapon() != s_cur) s_prev = s_cur, s_cur = Gun_Weapon();
        if (!W.open && gs.onFoot && focused) {
            int want = kb.pick;
            // (from the donut or the beer: as from fists - the beer's number put the scroll on the RPG)
            int cur = Gun_Weapon() < WPN_COUNT ? Gun_Weapon() : WPN_NONE;
            if (want < 0 && kb.scroll) want = (cur + (kb.scroll < 0 ? 1 : WPN_COUNT - 1)) % WPN_COUNT;
            if (want < 0 && kb.last) want = s_prev;
            if (want >= 0 && want != Gun_Weapon()) {
                Gun_Equip(want);
                if (SlotOf(want) >= 0) W.lastSel[want >= WPN_DONUT ? 1 : 0] = SlotOf(want);   // the wheel opens on it
                Log("Weapon key: %d", want);
            }
        }
    }

    // ---- animation
    W.openT = W.open ? fminf(1.0f, W.openT + dt / 0.18f) : fmaxf(0.0f, W.openT - dt / 0.13f);
    if (W.open) {
        W.introT += dt;
        W.openFrames++;
    }
    if (W.flipT < 1.0f) {
        float before = W.flipT;
        W.flipT = fminf(1.0f, W.flipT + dt / 0.24f);
        if (before < 0.5f && W.flipT >= 0.5f) {
            W.shownTab = W.tab;
            W.sel = W.lastSel[W.tab] < kSlots[W.tab & 1] ? W.lastSel[W.tab] : 0;
            W.bump = 1;
        }
    }
    float tabTarget = (float)W.tab;
    W.tabT += (tabTarget - W.tabT) * (1.0f - expf(-dt * 16.0f));
    for (int i = 0; i < 8; i++) {
        float target = (W.open && i == W.sel) ? 1.0f : 0.0f;
        W.pop[i] += (target - W.pop[i]) * (1.0f - expf(-dt * 20.0f));
    }
    W.bump = fmaxf(0.0f, W.bump - dt * 4.0f);
    for (int i = 0; i < W.nsparks;) {
        Spark& s = W.sparks[i];
        s.t += dt;
        s.rot += s.spin * dt;
        if (s.t > s.life)
            W.sparks[i] = W.sparks[--W.nsparks];
        else
            i++;
    }

    // ---- slow motion
    float probe[Renderer::kProbeMax];
    g_probePlayer = gs.player;
    extern double g_daProf[6];
    LARGE_INTEGER pq0, pq1, pqf;
    QueryPerformanceCounter(&pq0);
    int probeOther = (g_probeGun ? 1 : 0) + g_probeDecals + g_probeDaVis + g_probeRockets;
    bool probeRead = g_r.ProbeRead(probe, probeOther + g_probeCover);
    QueryPerformanceCounter(&pq1);
    QueryPerformanceFrequency(&pqf);
    g_daProf[3] += 1000.0 * (double)(pq1.QuadPart - pq0.QuadPart) / (double)pqf.QuadPart;
    if (probeRead) {
        if (g_probeGun) Gun_SetProbe(probe[0]);
        Decal_ProbeResult(probe + (g_probeGun ? 1 : 0), g_probeDecals);
        Gun_DaVisResult(probe + (g_probeGun ? 1 : 0) + g_probeDecals, g_probeDaVis);
    }
    Rocket_ProbeResult(probe + (g_probeGun ? 1 : 0) + g_probeDecals + g_probeDaVis, probeRead ? g_probeRockets : 0);
    VCover_ProbeResult(probe + probeOther, probeRead ? g_probeCover : 0);
    g_probeGun = false;
    g_probeDecals = g_probeDaVis = g_probeRockets = g_probeCover = 0;
    if (g_cfg.debug) Lab_OnPresent(g_r, gs.player);
    Gun_Update(dt, pad, gs, W.open);
    // the game's mouse: nothing while the wheel is open; the scroll wheel is ours on foot; aiming
    // turns our camera, not the game's; with a weapon picked, its fire / aim buttons are ours all through
    // play - in a car and getting in and out too: the game's left click gets in and out of cars, so a
    // shot fired as he climbed out ran him to the nearest car and in again, over and over (E does that)
    Kbm_SetHide(W.open || Gun_Aiming(), gs.onFoot, Gun_IsEquipped() && gameplay, W.open);
    // the pointer stays in the window while playing with the mouse: on a Mac a pointer over the Dock
    // sent the game no movement at all ("can't control the rocket") and swelled the Dock up, and a
    // click there never reached it; menus, the pause menu and another app have it back
    Kbm_GrabPointer(focused && kb.active && gameplay, g_hwnd);
    Input_RumbleUpdate(dt, focused && gs.onFoot && !W.open);

    double wantScale = W.open ? (double)g_cfg.slowMotion : (double)g_dbgTimeScale;
    W.timeScale += (wantScale - W.timeScale) * (1.0 - exp(-dt * 12.0));
    if (fabs(W.timeScale - wantScale) < 0.003) W.timeScale = wantScale;
    Time_SetScale(W.timeScale);

    // ---- draw
    bool wheelVisible = W.openT > 0.0f || W.open;
    bool gunVisible = Gun_NeedsDraw();
    // drunk: double vision over the game's picture (its HUD too), under ours - not over the wheel or a menu (drunk.inc)
    float ghA = 0.0f, ghX = 0.0f, ghY = 0.0f;
    bool ghost = gameplay && !wheelVisible && Gun_DrunkGhost(sparkW, &ghA, &ghX, &ghY);
    if (wheelVisible || g_markers || gunVisible || ghost) {
        if (g_r.Begin(target)) {
            g_r.k = (float)g_r.H / 1080.0f * g_cfg.uiScale;
            if (gunVisible && !g_gunInWorld) Gun_Draw(g_r);   // fallback: no depth-only clear seen
            if (ghost) g_r.DrawGhost(ghA, ghX, ghY);
            if (gunVisible) Gun_DrawOverlay(g_r);
            if (g_markers) Gun_DebugMarkers(g_r, gs.player);
            if (g_cfg.debug) Gun_DrvOverlay(g_r, gs.player);   // (diagnostic)
            if (wheelVisible) {
                float fade = Smooth(W.openT);
                bool blurred = g_r.CaptureBlur(g_cfg.blur);
                g_r.DrawBackdrop(fade, g_cfg.dim, blurred);
                DrawWheel(fade);
            }
            g_r.End();
        }
    }
    g_gunInWorld = false;
    if (dbg.screenshot) {
        bool ok = g_r.Screenshot(target, dbg.shotPath);
        Log("Screenshot %ls -> %s", dbg.shotPath, ok ? "ok" : "failed");
    }
    // debug "rec": every frame (with everything of ours on it) at half size, and when each was shown
    static FILE *recF, *recT;
    static double recEnd, rec0;
    static int recN;
    static bool recFull;
    if (dbg.rec != 0.0f) {
        if (recF) fclose(recF), fclose(recT), recF = recT = nullptr, Log("rec: stopped, %d frames", recN);
        if (dbg.rec > 0.0f) {
            wchar_t p[MAX_PATH];
            swprintf(p, MAX_PATH, L"%lsrec.raw", g_modDir);
            recF = _wfopen(p, L"wb");
            swprintf(p, MAX_PATH, L"%lsrec.txt", g_modDir);
            recT = _wfopen(p, L"w");
            if (!recF || !recT) {
                if (recF) fclose(recF);
                if (recT) fclose(recT);
                recF = recT = nullptr;
            }
            rec0 = now, recEnd = now + dbg.rec, recN = 0, recFull = dbg.recFull;
            Log("rec: %s for %.1f s", recF ? "recording" : "could not open rec.raw", dbg.rec);
        }
    }
    g_recIdx = -1;
    if (recF) {
        int w, h;
        if (g_r.RecordFrame(target, recF, &w, &h, recFull)) g_recIdx = recN, fprintf(recT, "%d %d %d %.4f\n", recN++, w, h, now - rec0);
        if (now >= recEnd) fclose(recF), fclose(recT), recF = recT = nullptr, Log("rec: done, %d frames", recN);
    }
    (void)dev;
}
