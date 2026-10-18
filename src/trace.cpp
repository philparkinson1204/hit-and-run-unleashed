// TraceShot: a segment against the game's own collision (English Simpsons.exe only).
//
// The game has no single "ray against everything" call, but it has the pieces, and the
// kick, the wasp-camera bolts and the cameras use them the same way. Every address and
// calling convention below was checked in the disassembly against those call sites (see
// docs/research/shots_vehicles_effects.md, section A):
//  - IntersectionList (0x48 bytes, built on our stack like the kick does): filled around a
//    point and radius with the collision objects of buildings and walls (statics), of
//    vehicles, characters and props (dynamics, each with its owner), and of animated pieces
//    like doors. A fill keeps at most 50 of each, so the segment is walked in short pieces.
//  - SimRayVolume (0x516A60): the primitive every ray test in the game uses. A ray against
//    one collision object, down to the exact sub-volume; returns the distance and the volume.
//  - The ground (roads, grass, dirt) is not a collision object. IntersectManager::
//    FindIntersection (0x4B59C0) gives the terrain height, normal and type under a point,
//    so the segment is marched for where it dips below the ground.
// Everything here runs on the game's thread (at Present), like the camera line test. The
// list's storage comes from the game's operator new; at Present that is heap 8, which on PC
// is plain malloc/free (radMemoryAllocatorMalloc), and the list is freed before returning.
#include "hit.h"

int g_traceLog;

const char* Hit_KindName(HitKind k)
{
    static const char* names[] = {"nothing", "world", "vehicle", "character"};
    return k >= HIT_NONE && k <= HIT_CHARACTER ? names[k] : "?";
}

// The ground says what it is (its terrain type: 0 road, 1 grass, 2 sand, 3 gravel, 4 water,
// 5 wood, 6 metal, 7 dirt); buildings and props only by the shape of the piece hit.
Surface Hit_Surface(const HitResult& h)
{
    switch (h.kind) {
    case HIT_NONE: return SURF_NONE;
    case HIT_VEHICLE: return h.glass ? SURF_GLASS : h.tire ? SURF_TIRE : SURF_CAR;
    case HIT_CHARACTER: return SURF_BODY;
    default: break;
    }
    if (h.drawn) return SURF_WOOD;   // (vcover) something drawn with no collision: dust, no sparks (it doesn't say what it is)
    if (h.ground >= 0) {
        switch (h.ground & 0x7F) {
        case 1: return SURF_GRASS;
        case 2: case 3: case 7: return SURF_DIRT;
        case 4: return SURF_WATER;
        case 5: return SURF_WOOD;
        default: return SURF_HARD;   // road, metal
        }
    }
    return h.volume == 0 || h.volume == 3 || h.volume == 4 ? SURF_HARD : SURF_ROUND;
}

bool Surface_IsSoft(Surface s) { return s == SURF_GRASS || s == SURF_DIRT || s == SURF_WATER; }

// The ground, and buildings (StaticPhysDSG, InstStatPhysDSG: they never move), where hard
bool Hit_TakesHole(const HitResult& h)
{
    Surface s = Hit_Surface(h);
    if (s != SURF_HARD && s != SURF_WOOD) return false;
    if (h.ground >= 0 || h.drawn) return true;
    uint32_t vt = h.object ? *(const uint32_t*)(uintptr_t)h.object : 0;
    return vt == 0x60C89C || vt == 0x60CB74;
}

// ------------------------------------------------------------------ the game's functions
struct RayInfo {
    float t;     // out: distance along the ray to the (inflated) volume
    void* obj;   // in: the collision object (the game doesn't rely on it after a hit)
    void* vol;   // in: the object's root volume; out: the leaf volume hit
};

typedef void*(__stdcall* IlCtor)(void* list);
typedef void(__attribute__((thiscall)) * IlDtor)(void* list);
typedef int(__fastcall* IlFillStatics)(const Vec3* centre, void* list, float radius);
typedef int(__stdcall* IlFillDynamics)(void* list, const Vec3* centre, float radius, int keepOwners, uint32_t skip);
typedef uint8_t(__cdecl* SimRayVolumeFn)(const Vec3* start, const Vec3* dir, const Vec3* delta, float len, RayInfo* io);

static const IlCtor IL_Ctor = (IlCtor)0x41AF80;                        // stdcall(this), ret 4
static const IlDtor IL_Dtor = (IlDtor)0x41B050;                        // thiscall
static const IlFillStatics IL_FillStatics = (IlFillStatics)0x41BDF0;   // ecx centre, edx list, radius on the stack
static const IlFillDynamics IL_FillDynamics = (IlFillDynamics)0x41BF60;
static const SimRayVolumeFn SimRayVolume = (SimRayVolumeFn)0x516A60;

// FillAnimPhys (0x41C030): centre in ECX, the list in EDI, radius on the stack (it pops it).
__attribute__((naked)) static int IL_FillAnimPhys(void* list, const Vec3* centre, float radius)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 20(%esp), %edi\n\t"   // list
        "movl 24(%esp), %ecx\n\t"   // centre
        "pushl 28(%esp)\n\t"        // radius
        "movl $0x41C030, %eax\n\t"
        "call *%eax\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// IntersectManager::FindIntersection (0x4B59C0): the point in EDI; on the stack the
// IntersectManager, bool* found, Vec3* normal, Vec3* groundPos (it pops all four). It casts
// a ray down from y = 10000 at the point's x, z through the terrain in the point's k-d tree
// leaf. Returns the terrain info: the type in the low bits, 0x80 = indoors.
__attribute__((naked)) static int FindGround(const Vec3* point, uint8_t* found, Vec3* normal, Vec3* groundPos)
{
    __asm__ volatile(
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebp\n\t"
        "movl 20(%esp), %edi\n\t"   // point
        "pushl 32(%esp)\n\t"        // groundPos
        "pushl 32(%esp)\n\t"        // normal
        "pushl 32(%esp)\n\t"        // found
        "pushl 0x6C87A4\n\t"        // IntersectManager*
        "movl $0x4B59C0, %eax\n\t"
        "call *%eax\n\t"
        "popl %ebp\n\t"
        "popl %edi\n\t"
        "popl %esi\n\t"
        "popl %ebx\n\t"
        "ret\n\t");
}

// Globals every game ray test sets first (and nothing else reads): the test mode (2 = the
// exact sub-volumes; the static default 1 only tests their bounding boxes) and a radius
// every volume grows by.
static volatile int* const g_rayMode = (volatile int*)0x642350;
static volatile float* const g_rayThick = (volatile float*)0x65C420;
static const uint32_t A_IntersectManager = 0x6C87A4;

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static const float kPiece = 10.0f;   // metres of the segment per list fill
float g_traceFillPad = 0.5f;        // (lab) how far past the piece's own half length the fill reaches
static const int kListCap = 50;      // what a fill keeps of each kind

bool Trace_Available()
{
    return !strcmp(Game_VersionName(), "English") && *(volatile uint32_t*)A_IntersectManager != 0;
}

// ------------------------------------------------------------------ objects
struct Ray {
    Vec3 a, dir, delta;
    float len;
};

struct Best {
    float t;
    uint8_t* co;      // sim::CollisionObject
    uint8_t* vol;     // the leaf CollisionVolume hit
    uint32_t owner;   // the dynamics list's owner (0 for statics and animated pieces)
};

struct Stats {
    int pieces, statics, dynamics, anims, full, ground;
};

// (drive-by) a second object left out of every trace while it's set: the car he shoots from - by the dynamics list's
// owner or the collision object's own (SimState +0x08), so no piece of it stops a shot; everything else still does
static uint32_t g_traceIgnore2;
void Trace_SetIgnore2(uint32_t object) { g_traceIgnore2 = object; }
// (M5) and, for a moment, a car a shot has gone right through (in at a broken window, out at another opening)
static uint32_t g_traceIgnore3;
void Trace_SetIgnore3(uint32_t object) { g_traceIgnore3 = object; }
// (vcover) and a person a pellet passes beside (their collision cylinder is wider than they are drawn)
static uint32_t g_traceIgnore4;
void Trace_SetIgnore4(uint32_t object) { g_traceIgnore4 = object; }

// one collision object: +0x20 its root volume (the list's pointers are the game's own, live)
static void TestObject(uint8_t* co, uint32_t owner, const Ray& ray, Best* best)
{
    if (!co) return;
    const uint32_t ignored[3] = {g_traceIgnore2, g_traceIgnore3, g_traceIgnore4};
    for (uint32_t ig : ignored) {
        if (!ig) continue;
        if (owner == ig) return;
        const uint8_t* ss = *(const uint8_t**)(co + 0x18);
        if (ss && *(const uint32_t*)(ss + 0x08) == ig) return;
    }
    void* root = *(void**)(co + 0x20);
    if (!root) return;
    RayInfo io = {0.0f, co, root};
    if (!SimRayVolume(&ray.a, &ray.dir, &ray.delta, ray.len, &io)) return;
    // starting inside a volume reads as t = 0: not a hit (the game's own tests skip it too)
    if (!(io.t > 1e-4f) || io.t >= best->t || !io.vol) return;
    best->t = io.t;
    best->co = co;
    best->vol = (uint8_t*)io.vol;
    best->owner = owner;
}

// The objects around the part of the ray from s to e (metres), tested against the whole ray.
static void TracePiece(const Ray& ray, float s, float e, uint32_t ignore, Best* best, Stats* st, int depth)
{
    Vec3 c = ray.a + ray.dir * ((s + e) * 0.5f);
    float r = (e - s) * 0.5f + g_traceFillPad;
    alignas(16) uint8_t list[0x48];
    IL_Ctor(list);
    IL_FillStatics(&c, list, r);
    IL_FillDynamics(list, &c, r, 1, ignore);   // 1: keep each object's owner
    IL_FillAnimPhys(list, &c, r);
    // statics: CollisionObject* at +0x08..+0x0C; dynamics: {CollisionObject*, owner} pairs at
    // +0x18..+0x1C; animated: CollisionObject* at +0x2C..+0x30 (begin / end)
    uint8_t** sb = *(uint8_t***)(list + 0x08);
    uint8_t** se = *(uint8_t***)(list + 0x0C);
    uint32_t* db = *(uint32_t**)(list + 0x18);
    uint32_t* de = *(uint32_t**)(list + 0x1C);
    uint8_t** ab = *(uint8_t***)(list + 0x2C);
    uint8_t** ae = *(uint8_t***)(list + 0x30);
    int ns = sb ? (int)(se - sb) : 0, nd = db ? (int)(de - db) / 2 : 0, na = ab ? (int)(ae - ab) : 0;
    if ((ns >= kListCap || nd >= kListCap || na >= kListCap) && depth < 3 && e - s > 2.0f) {
        // the fill dropped some: do this piece in halves
        IL_Dtor(list);
        st->full++;
        float m = (s + e) * 0.5f;
        TracePiece(ray, s, m, ignore, best, st, depth + 1);
        TracePiece(ray, m, e, ignore, best, st, depth + 1);
        return;
    }
    st->pieces++;
    st->statics += ns;
    st->dynamics += nd;
    st->anims += na;
    for (int i = 0; i < ns; i++) TestObject(sb[i], 0, ray, best);
    for (int i = 0; i < nd; i++) TestObject((uint8_t*)(uintptr_t)db[2 * i], db[2 * i + 1], ray, best);
    for (int i = 0; i < na; i++) TestObject(ab[i], 0, ray, best);
    IL_Dtor(list);   // also releases the references the fill took
}

// ------------------------------------------------------------------ the ground
static bool GroundAt(Vec3 p, float* y, Vec3* n, int* info, Stats* st)
{
    uint8_t found = 0;
    Vec3 nn = {0, 1, 0}, g = p;
    int r = FindGround(&p, &found, &nn, &g);
    st->ground++;
    if (!found) return false;
    *y = g.y;
    if (n) *n = nn;
    if (info) *info = r;
    return true;
}

// Where the ray first goes below the ground before maxT. The step is how far the ray can go
// before it could reach flat ground at its slope (at most 8 m), then a bisection and the
// ground's plane for the exact point.
static bool MarchGround(const Ray& ray, float maxT, float* tHit, Vec3* nHit, int* info, Stats* st)
{
    float t = 0.0f, prev = -1.0f;
    for (int calls = 0; calls < 40; calls++) {
        Vec3 p = ray.a + ray.dir * t;
        float gy;
        Vec3 gn;
        int gi;
        bool have = GroundAt(p, &gy, &gn, &gi, st);
        if (have && p.y < gy) {
            if (prev < 0) return false;   // starts under the ground (an underpass): no ground test
            float lo = prev, hi = t;
            for (int k = 0; k < 8; k++) {
                float mid = 0.5f * (lo + hi);
                Vec3 q = ray.a + ray.dir * mid;
                float qy;
                if (GroundAt(q, &qy, nullptr, nullptr, st) && q.y < qy) hi = mid; else lo = mid;
            }
            Vec3 q = ray.a + ray.dir * hi;
            if (!GroundAt(q, &gy, &gn, &gi, st)) gn = V3(0, 1, 0), gy = q.y;
            float tt = hi, dn = Dot(ray.dir, gn);
            if (dn < -1e-3f) {   // onto the ground's plane under that point
                float tp = Dot(V3(q.x, gy, q.z) - ray.a, gn) / dn;
                if (tp > lo - 0.25f && tp < hi + 0.25f) tt = tp;
            }
            *tHit = fmaxf(0.0f, tt);
            *nHit = gn;
            *info = gi;
            return true;
        }
        if (t >= maxT) return false;
        float h = have ? p.y - gy : 4.0f;
        float step = Clampf(h * 0.8f / fmaxf(-ray.dir.y, 0.05f), 0.5f, 8.0f);
        prev = t;
        t = fminf(t + step, maxT);
    }
    return false;
}

// ------------------------------------------------------------------ normals
// The leaf volume's surface normal at p (the volume layout is the one SimRayVolume reads:
// centre +0x08, box half sizes +0x14, type +0x24; cylinder axis +0x4C, half length +0x58,
// radius +0x5C, flat ends +0x60; box axes +0x4C/+0x58/+0x64, half sizes +0x70, +0x7C set =
// lined up with the world).
static Vec3 VolumeNormal(const uint8_t* vol, Vec3 p, Vec3 dir)
{
    Vec3 c = *(const Vec3*)(vol + 0x08);
    Vec3 d = p - c;
    Vec3 n = dir * -1.0f;
    auto box = [&](const Vec3* ax, Vec3 half) {
        float h[3] = {half.x, half.y, half.z}, best = -1.0f;
        for (int i = 0; i < 3; i++) {
            float k = Dot(d, ax[i]) / fmaxf(h[i], 1e-3f);
            if (fabsf(k) > best) {
                best = fabsf(k);
                n = ax[i] * (k >= 0 ? 1.0f : -1.0f);
            }
        }
    };
    static const Vec3 world[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    switch (*(const int*)(vol + 0x24)) {
    case 1:   // sphere
        n = Norm(d);
        break;
    case 2: {   // cylinder or capsule
        Vec3 ax = Norm(*(const Vec3*)(vol + 0x4C));
        float half = *(const float*)(vol + 0x58), along = Dot(d, ax);
        if (vol[0x60] && fabsf(along) > half - 0.01f) n = ax * (along > 0 ? 1.0f : -1.0f);
        else n = Norm(d - ax * Clampf(along, -half, half));
        break;
    }
    case 3:   // oriented box
        if (*(const uint32_t*)(vol + 0x7C)) box(world, *(const Vec3*)(vol + 0x70));
        else {
            Vec3 ax[3] = {*(const Vec3*)(vol + 0x4C), *(const Vec3*)(vol + 0x58), *(const Vec3*)(vol + 0x64)};
            box(ax, *(const Vec3*)(vol + 0x70));
        }
        break;
    default:   // walls and plain boxes: their bounding box
        box(world, *(const Vec3*)(vol + 0x14));
        break;
    }
    if (Len(n) < 0.5f) n = dir * -1.0f;
    if (Dot(n, dir) > 0) n = n * -1.0f;   // facing back along the shot
    return n;
}

// ------------------------------------------------------------------ the trace
bool TraceShot(Vec3 from, Vec3 to, uint32_t ignore, HitResult* hit, float radius)
{
    memset(hit, 0, sizeof(*hit));
    hit->kind = HIT_NONE;
    hit->ground = hit->volume = -1;
    Ray ray;
    ray.a = from;
    ray.delta = to - from;
    ray.len = Len(ray.delta);
    if (ray.len < 1e-3f || !Trace_Available()) return false;
    ray.dir = ray.delta * (1.0f / ray.len);
    double t0 = g_traceLog > 0 ? Time_Real() : 0;

    int mode = *g_rayMode;
    float thick = *g_rayThick;
    *g_rayMode = 2;
    *g_rayThick = radius;
    Best best = {ray.len, nullptr, nullptr, 0};
    Stats st = {};
    // piece by piece; a hit inside or before this piece can't be beaten by a later one
    for (float s = 0.0f; s < ray.len; s += kPiece) {
        float e = fminf(s + kPiece, ray.len);
        TracePiece(ray, s, e, ignore, &best, &st, 0);
        if (best.co && best.t <= e) break;
    }
    *g_rayMode = mode;
    *g_rayThick = thick;

    float tg;
    Vec3 ng;
    int info;
    bool ground = MarchGround(ray, best.t, &tg, &ng, &info, &st) && tg < best.t;
    if (ground) {
        hit->kind = HIT_WORLD;
        hit->dist = tg;
        hit->pos = ray.a + ray.dir * tg;
        hit->normal = Dot(ng, ray.dir) > 0 ? ng * -1.0f : ng;
        hit->ground = info;
    } else if (best.co) {
        hit->dist = best.t;
        hit->pos = ray.a + ray.dir * best.t;
        hit->normal = VolumeNormal(best.vol, hit->pos, ray.dir);
        hit->volume = *(const int*)(best.vol + 0x24);
        // the owner: CollisionObject +0x18 its SimState; SimState +0x08 the object, +0x0C its AI ref
        const uint8_t* ss = *(const uint8_t**)(best.co + 0x18);
        uint32_t ssOwner = ss ? *(const uint32_t*)(ss + 0x08) : 0;
        hit->aiRef = ss ? *(const uint32_t*)(ss + 0x0C) : 0;
        hit->object = best.owner ? best.owner : ssOwner;
        if (g_traceLog > 0 && best.owner && ssOwner != best.owner)
            Log("trace: list owner %08X but sim state owner %08X", best.owner, ssOwner);
        if (g_traceLog > 0) {
            const float* f = (const float*)best.vol;
            Log("trace: volume %08X type %d centre (%.2f %.2f %.2f) half (%.2f %.2f %.2f) r %.2f | +4C (%.2f %.2f %.2f) "
                "+58 (%.2f %.2f %.2f) +64 (%.2f %.2f %.2f) +70 (%.2f %.2f %.2f) +7C %08X",
                (uint32_t)(uintptr_t)best.vol, hit->volume, f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[19], f[20],
                f[21], f[22], f[23], f[24], f[25], f[26], f[27], f[28], f[29], f[30], *(const uint32_t*)(best.vol + 0x7C));
        }
        if (hit->aiRef == 1) hit->kind = HIT_VEHICLE;
        else if (hit->aiRef == 0x20001 || hit->aiRef == 0x20002) hit->kind = HIT_CHARACTER;
        else hit->kind = HIT_WORLD;
    }
    if (g_traceLog > 0) {
        g_traceLog--;
        char line[200];
        Trace_Describe(*hit, line, sizeof(line));
        Log("trace %.1f m: %s | %d pieces (%d full), objects %d/%d/%d, %d ground tests, %.2f ms", ray.len, line,
            st.pieces, st.full, st.statics, st.dynamics, st.anims, st.ground, (Time_Real() - t0) * 1000.0);
    }
    return hit->kind != HIT_NONE;
}

bool Trace_GroundY(Vec3 p, float* y)
{
    if (!Trace_Available()) return false;
    Stats st = {};
    return GroundAt(p, y, nullptr, nullptr, &st);
}

// The owners in the game's dynamics list around c (cars, characters, props), for debug surveys.
int Trace_ListDynamics(Vec3 c, float r, uint32_t ignore, uint32_t* out, int max)
{
    if (!Trace_Available()) return 0;
    alignas(16) uint8_t list[0x48];
    IL_Ctor(list);
    IL_FillDynamics(list, &c, r, 1, ignore);
    uint32_t* db = *(uint32_t**)(list + 0x18);
    uint32_t* de = *(uint32_t**)(list + 0x1C);
    int n = 0;
    for (uint32_t* p = db; p && p < de && n < max; p += 2)
        if (p[1]) out[n++] = p[1];
    IL_Dtor(list);
    return n;
}

// (lab) the static collision objects (buildings, walls, fences) and animated pieces round c, logged: owner, its
// class and name key, the root volume's type, centre and half sizes - what a trace has to go on there
void Trace_LogStatics(Vec3 c, float r)
{
    if (!Trace_Available()) return;
    alignas(16) uint8_t list[0x48];
    IL_Ctor(list);
    IL_FillStatics(&c, list, r);
    IL_FillAnimPhys(list, &c, r);
    uint8_t** sb = *(uint8_t***)(list + 0x08);
    uint8_t** se = *(uint8_t***)(list + 0x0C);
    uint8_t** ab = *(uint8_t***)(list + 0x2C);
    uint8_t** ae = *(uint8_t***)(list + 0x30);
    int ns = sb ? (int)(se - sb) : 0, na = ab ? (int)(ae - ab) : 0;
    Log("statics round (%.1f %.1f %.1f) r %.1f: %d statics, %d animated", c.x, c.y, c.z, r, ns, na);
    for (int i = 0; i < ns + na; i++) {
        uint8_t* co = i < ns ? sb[i] : ab[i - ns];
        if (!co) continue;
        const uint8_t* ss = *(const uint8_t**)(co + 0x18);
        uint32_t owner = ss ? *(const uint32_t*)(ss + 0x08) : 0;
        const uint8_t* vol = *(const uint8_t**)(co + 0x20);
        uint32_t uid[2] = {0, 0};
        if (owner) memcpy(uid, (const void*)(uintptr_t)(owner + 8), 8);
        if (!vol) continue;
        const float* f = (const float*)vol;
        Log("  %s %08X %s uid %08X%08X | co %08X root vol %08X type %d centre (%.2f %.2f %.2f) half (%.2f %.2f %.2f) | +28 %08X +2C %08X +30 %08X +34 %08X",
            i < ns ? "static" : "anim", owner, Game_ClassName(owner), uid[1], uid[0], (uint32_t)(uintptr_t)co,
            (uint32_t)(uintptr_t)vol, *(const int*)(vol + 0x24), f[2], f[3], f[4], f[5], f[6], f[7],
            *(const uint32_t*)(vol + 0x28), *(const uint32_t*)(vol + 0x2C), *(const uint32_t*)(vol + 0x30), *(const uint32_t*)(vol + 0x34));
    }
    IL_Dtor(list);
}

void Trace_Describe(const HitResult& h, char* out, int n)
{
    if (h.kind == HIT_NONE) {
        snprintf(out, n, "nothing");
        return;
    }
    if (h.drawn) {
        snprintf(out, n, "drawn surface (no collision) at %.2f m, (%.2f %.2f %.2f) n (%.2f %.2f %.2f)", h.dist, h.pos.x, h.pos.y, h.pos.z,
                 h.normal.x, h.normal.y, h.normal.z);
        return;
    }
    if (h.ground >= 0) {
        snprintf(out, n, "ground (terrain %d%s) at %.2f m, (%.2f %.2f %.2f) n (%.2f %.2f %.2f)", h.ground & 0x7F,
                 h.ground & 0x80 ? " indoors" : "", h.dist, h.pos.x, h.pos.y, h.pos.z, h.normal.x, h.normal.y,
                 h.normal.z);
        return;
    }
    snprintf(out, n, "%s %s %08X ai %X vol %d at %.2f m, (%.2f %.2f %.2f) n (%.2f %.2f %.2f)", Hit_KindName(h.kind),
             Game_ClassName(h.object), h.object, h.aiRef, h.volume, h.dist, h.pos.x, h.pos.y, h.pos.z, h.normal.x,
             h.normal.y, h.normal.z);
}
