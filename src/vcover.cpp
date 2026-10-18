// Everything drawn stops a shot (see hit.h). The game's collision is rougher than its pictures: the Simpsons' house
// is a set of boxes with no bay window and no garage side, so bullets went through both; a person's collision
// cylinder is far wider than the person, so a shot past someone's shoulder knocked them down.
//
// While a weapon is aimed, the next shot's pellet pattern is already rolled, and every frame the depth probe
// (render.cpp) reads how far the drawn picture goes along each pellet's ray - three pixels a pellet (its own and two
// beside it, for the surface's slope). A reading is coarse at first (1-150 m in 4 % steps), then tracked to about a
// centimetre (a 1.5 m window round the last reading) for as long as it stays inside it. When the trigger is pulled,
// FireWeapon traces each pellet through the game's collision as before and VCover_Decide compares: something drawn
// in front of what the trace met (and nothing the game can push there) stops the pellet on it.
#include "hit.h"

bool g_vcoverOn = true;
static int g_vcLog;   // debug "vclog <shots>"

static const int kPx = 3;               // pixels a pellet: its own, one to the right, one below
static const float kNeighbour = 4.0f;   // pixels between them
static const float kTrack = 0.75f;      // the fine window: +- this round the last reading (metres of view depth)

static struct {
    int n;                       // pellets in the pattern (0 = off)
    float spread;
    float a[kMaxPellets], b[kMaxPellets];   // each pellet's ray in the camera's terms: F + R a + U b
    int armed;                   // frames left before it switches off unless armed again
    int slots;                   // pixels asked for this frame
    float px[kMaxPellets * kPx][2];
    float rng[kMaxPellets * kPx][2];
    float depth[kMaxPellets * kPx];   // view depth (metres), -1 nothing within 150 m, -3 not known
    bool fine[kMaxPellets * kPx];     // the next reading can use the narrow window
    float view[16], proj[16];    // the camera of the frame the probe was drawn on
    float W, H;
    bool drawnThisFrame;
    bool ready;                  // every reading belongs to this pattern and the frame just drawn
} V;

static uint32_t g_rng = 0x9E3779B9u;
static float Rnd01()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

static void Roll()
{
    for (int i = 0; i < V.n; i++) {
        // evenly over the cone's area (as weapon.cpp's Scatter); a single pellet with no spread: dead centre
        float r = V.spread > 0 ? tanf(V.spread) * sqrtf(Rnd01()) : 0.0f, t = Rnd01() * 6.2831853f;
        V.a[i] = r * cosf(t);
        V.b[i] = r * sinf(t);
    }
    for (int j = 0; j < kMaxPellets * kPx; j++) V.depth[j] = -3.0f, V.fine[j] = false;
    V.ready = false;
}

void VCover_Arm(int pellets, float spread)
{
    if (pellets > kMaxPellets) pellets = kMaxPellets;
    if (pellets != V.n || spread != V.spread) {
        V.n = pellets;
        V.spread = spread;
        Roll();
    }
    V.armed = 3;
}

int VCover_ProbeWant(float (*px)[2], float (*range)[2], int max, int W, int H)
{
    V.drawnThisFrame = false;
    V.slots = 0;
    if (!g_vcoverOn || V.n <= 0 || V.armed <= 0 || W <= 0 || H <= 0) return 0;
    V.armed--;
    if (!Game_GetCamera(V.view, V.proj)) return 0;
    V.W = (float)W;
    V.H = (float)H;
    int k = 0;
    for (int i = 0; i < V.n && k + kPx <= max; i++) {
        float x = floorf(W * 0.5f * (1.0f + V.a[i] * V.proj[0])), y = floorf(H * 0.5f * (1.0f - V.b[i] * V.proj[5]));
        if (fabsf(x - W * 0.5f) < 1.0f && fabsf(y - H * 0.5f) < 1.0f) x += 1;   // (the crosshair's own probe pixel)
        for (int m = 0; m < kPx; m++) {
            float qx = x + (m == 1 ? kNeighbour : 0), qy = y + (m == 2 ? kNeighbour : 0);
            qx = fminf(fmaxf(qx, 0.0f), W - 1.0f);
            qy = fminf(fmaxf(qy, 0.0f), H - 1.0f);
            int j = i * kPx + m;
            V.px[j][0] = px[k][0] = qx;
            V.px[j][1] = px[k][1] = qy;
            if (V.fine[j] && V.depth[j] > 0) {
                V.rng[j][0] = range[k][0] = fmaxf(0.3f, V.depth[j] - kTrack);
                V.rng[j][1] = range[k][1] = V.depth[j] + kTrack;
            } else
                V.rng[j][0] = V.rng[j][1] = range[k][0] = range[k][1] = 0.0f;
            k++;
        }
    }
    V.slots = k;
    V.drawnThisFrame = k > 0;
    return k;
}

void VCover_ProbeResult(const float* depth, int n)
{
    V.ready = false;
    if (!V.drawnThisFrame || n != V.slots) return;
    for (int j = 0; j < n; j++) {
        bool wasFine = V.rng[j][1] > V.rng[j][0];
        float d = depth[j];
        if (wasFine && d < 0) {   // left the window (-2 nearer, -1 further): not known this frame, coarse next
            V.depth[j] = -3.0f;
            V.fine[j] = false;
        } else {
            V.depth[j] = d;       // (coarse: -1 = nothing drawn within 150 m)
            V.fine[j] = d > 0;
        }
    }
    V.ready = true;
}

bool VCover_Ready(int pellets, float spread)
{
    return g_vcoverOn && V.ready && V.n == pellets && V.spread == spread && V.slots == pellets * kPx;
}

// the frame's camera: the eye, its right/up/forward rows, and the view-space ray through a pixel
static void Camera(Vec3* eye, Vec3* R, Vec3* U, Vec3* F)
{
    Mat4 v;
    memcpy(v.m, V.view, 64);
    Mat4 cw = RigidInverse(v);
    *eye = Row(cw, 3);
    *R = Row(cw, 0);
    *U = Row(cw, 1);
    *F = Row(cw, 2);
}

static Vec3 PixelRay(float x, float y, Vec3 R, Vec3 U, Vec3 F)   // view depth 1 along it (not unit)
{
    float nx = x / V.W * 2.0f - 1.0f, ny = 1.0f - y / V.H * 2.0f;
    return R * (nx / V.proj[0]) + U * (ny / V.proj[5]) + F;
}

bool VCover_Pellet(int i, Vec3* eye, Vec3* dir, float* vis, Vec3* normal)
{
    if (i < 0 || i >= V.n) return false;
    Vec3 R, U, F;
    Camera(eye, &R, &U, &F);
    int j = i * kPx;
    Vec3 dv = PixelRay(V.px[j][0], V.px[j][1], R, U, F);
    float k = Len(dv);
    *dir = dv * (1.0f / k);
    float d = V.depth[j];
    *vis = d > 0 ? d * k : d;
    *normal = *dir * -1.0f;
    if (d > 0 && V.depth[j + 1] > 0 && V.depth[j + 2] > 0) {
        Vec3 p0 = *eye + dv * d;
        Vec3 p1 = *eye + PixelRay(V.px[j + 1][0], V.px[j + 1][1], R, U, F) * V.depth[j + 1];
        Vec3 p2 = *eye + PixelRay(V.px[j + 2][0], V.px[j + 2][1], R, U, F) * V.depth[j + 2];
        // only if the three lie on one surface (no edge between them): neighbours within 6 % of the depth
        if (fabsf(V.depth[j + 1] - d) < 0.06f * d + 0.05f && fabsf(V.depth[j + 2] - d) < 0.06f * d + 0.05f) {
            Vec3 n = Cross(p2 - p0, p1 - p0);
            if (Len(n) > 1e-6f) {
                n = Norm(n);
                if (Dot(n, *dir) > 0) n = n * -1.0f;
                if (Dot(n, *dir) < -0.15f) *normal = n;   // (not grazing: a slope read across an edge)
            }
        }
    }
    return true;
}

void VCover_Used()
{
    Roll();
}

// A person drawn at p (not him): the characters' list (CharacterManager +192, 64 pointers, 0 the player), feet at
// +0x64, shown +0x350, in a car +0x128. A rough body: 0.55 m round the feet's vertical, 2.5 m tall.
static uint32_t PersonAt(Vec3 p)
{
    uint32_t cm = Game_CharacterManager();
    if (!cm) return 0;
    uint32_t best = 0;
    float bestD = 0.55f * 0.55f;
    for (int i = 1; i < 64; i++) {
        uint32_t c = *(const uint32_t*)(uintptr_t)(cm + 192 + 4 * i);
        if (!c || !*(const uint8_t*)(uintptr_t)(c + 0x350) || *(const uint32_t*)(uintptr_t)(c + 0x128)) continue;
        Vec3 f = *(const Vec3*)(uintptr_t)(c + 0x64);
        float dx = p.x - f.x, dz = p.z - f.z, d = dx * dx + dz * dz;
        if (d < bestD && p.y > f.y - 0.2f && p.y < f.y + 2.5f) best = c, bestD = d;
    }
    return best;
}

// A car drawn at distance `vis` along the ray: the active vehicles within 5 m of the point, each tested as drawn
static uint32_t CarAt(Vec3 eye, Vec3 dir, float vis, CarHit* ch)
{
    uint32_t cars[30];
    float pos[30][3];
    int n = Game_ListVehiclePtrs(cars, pos, 30);
    Vec3 p = eye + dir * vis;
    for (int i = 0; i < n; i++) {
        Vec3 c = V3(pos[i][0], pos[i][1], pos[i][2]);
        if (Len(c - p) > 5.0f) continue;
        if (CarGlass_Raycast(cars[i], eye, dir, vis + 0.6f, ch) && fabsf(Len(ch->pos - eye) - vis) < 0.35f + 0.02f * vis) return cars[i];
    }
    return 0;
}

int VCover_Decide(Vec3 eye, Vec3 dir, float vis, float startT, const HitResult& h, uint32_t shooter, HitResult* drawnAs)
{
    if (!g_vcoverOn || vis <= 0.0f || vis < startT) return 0;   // (between the camera and his back: not ours)
    float hitT = h.kind != HIT_NONE ? Dot(h.pos - eye, dir) : 1e9f;
    float tol = 0.12f + 0.02f * vis;
    Vec3 p = eye + dir * vis;
    if (vis < hitT - tol) {
        // him (the depth buffer has him too)
        float feet[3];
        if (shooter && Game_PlayerPosition(shooter, feet)) {
            float dx = p.x - feet[0], dz = p.z - feet[2];
            if (dx * dx + dz * dz < 0.8f * 0.8f && p.y > feet[1] - 0.2f && p.y < feet[1] + 2.4f) return 0;
        }
        HitResult d = {};
        d.pos = p;
        d.normal = dir * -1.0f;
        d.dist = vis;
        d.ground = d.volume = -1;
        // a person the trace missed (beside him the aimed trace starts inside their cylinder, and a volume a ray
        // starts in doesn't count): a hit on them
        if (uint32_t who = PersonAt(p)) {
            d.kind = HIT_CHARACTER;
            d.object = who;
            const uint8_t* ss = *(const uint8_t* const*)(uintptr_t)(who + 0x70);
            d.aiRef = ss ? *(const uint32_t*)(ss + 0x0C) : 0x20002;
            if (drawnAs) *drawnAs = d;
            return 4;
        }
        // a car whose collision box the trace missed (a mirror, a bumper): a hit on it as drawn
        CarHit ch;
        if (uint32_t car = CarAt(eye, dir, vis, &ch)) {
            d.kind = HIT_VEHICLE;
            d.object = car;
            d.aiRef = 1;
            d.pos = ch.pos;
            d.normal = ch.normal;
            d.glass = ch.glass;
            if (drawnAs) *drawnAs = d;
            return 5;
        }
        // a prop drawn there (the dynamics list has it): its own shape is traced - leave it to the trace
        uint32_t objs[8];
        if (Trace_ListDynamics(p, 0.5f, shooter, objs, 8) > 0) return 0;
        return 1;   // a building's piece with no collision
    }
    if (h.kind == HIT_CHARACTER && vis > hitT + 0.35f) return 2;   // the cylinder, not the person
    return 0;
}

void VCover_DebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "vcover")) {
        g_vcoverOn = atoi(arg) != 0;
        Log("vcover: %s", g_vcoverOn ? "on" : "off");
    } else if (!strcmp(cmd, "vcstate")) {
        Log("vcover: %d pellets spread %.3f, armed %d, slots %d, ready %d", V.n, V.spread, V.armed, V.slots, V.ready);
        for (int i = 0; i < V.n && i < 4; i++) {
            Vec3 e, d, n;
            float vis;
            VCover_Pellet(i, &e, &d, &vis, &n);
            Log("vcover:  pellet %d px (%.0f %.0f) drawn %.3f m (fine %d) normal (%.2f %.2f %.2f)", i, V.px[i * kPx][0], V.px[i * kPx][1], vis,
                V.fine[i * kPx], n.x, n.y, n.z);
        }
    } else if (!strcmp(cmd, "vclog")) g_vcLog = arg[0] ? atoi(arg) : 1;
}

int VCover_LogShots() { return g_vcLog; }
void VCover_LogUsed() { if (g_vcLog > 0) g_vcLog--; }
