// The drunk body in the game (Item 2, refined): his walk with his feet placed by his balance, and the fall - teeter, knees, hands,
// face down, the struggle up - in place of the particle ragdoll (ragdoll.inc, no longer used: it bent knees and elbows the wrong way,
// twisted them off their hinges and stretched his skin). The body itself is platform-neutral (dbody*.h, the same code the offline
// harness tools/lab/bodysim runs); this file feeds it from the game and hands its skeleton to the skin.
// Included from gun.cpp after drunk.inc.
//
//   the rig      his skeleton as the game has it (its inverse binds, read from memory: LoadInvBind)
//   the clips    Radical's own (hom_get_up, hom_balence, hom_loco_idle_rest, hom_loco_walk), read from the player's copy of the game at
//                run time (art\chars\homer_a.p3d) - nothing of it ships with the mod
//   the ground   a small grid of the game's own terrain heights round him (Trace_GroundY), probed again as he moves
//   each frame   (Drunk_Update -> DrunkBody_Update) the walker or the fall makes his pose; (Drunk_PoseLayer -> DrunkBody_Pose) it goes
//                into the game's skeleton before anything held is posed on top, the game's own pose kept for matching its upload
//
// v1.0 (2026-10-17, after the Windows play-test - review: "drunk mode is NOT approved... diagnose before another redesign"): the drunk body
// draws him only while he falls and gets up. Walking and standing he's the game's - Radical's own walk and idle, driven by the game's
// own controller, so his feet are always under him - with the drunk sway on top (drunk.inc's pose layer). Its own walk (dbody_walk.h:
// feet placed by his balance while the game moved his body) failed in real play: the game moved him faster and further sideways than
// those feet could step (staggers up to 4 m/s, a car's knock 6-7 m/s), so they dragged behind - he slid, did the splits and sank into
// a squat - and it drew its walk over everything the game did itself (a knockdown, a jump: he glided along sitting down); standing,
// it took two steps a second. Kept for the lab: "dbwalk 1"
#include "dbody_walk.h"
#include "dbody_gen.h"
static bool g_dbProcWalk = false;   // (debug "dbwalk 1": the drunk body's own walk, as before v1.0)
extern bool g_dwHybrid;             // (game.cpp: his staggers bend his walk - no side or back steps; off with "dbwalk 1")
static float kDbHandBack = 0.45f;   // (up: the get-up's last frame into the game's own pose, s)

static struct {
    bool tried, ok;          // the rig and the clips
    uint32_t skel;           // (the skeleton the rig was built for)
    db::Rig rig;
    db::Bank bank;
    db::ClipSet cs;
    db::Walker walker;
    db::Balance bal;         // (the walker's view of drunk.inc's balance: its lean and rate copied in)
    db::Fall fall;
    bool falling, moved;     // a fall in progress; his game body moved to where he'll stand
    float onW;               // the drunk body's weight over the game's own pose (it fades in as drunk mode starts)
    db::Pose pose;           // this frame's
    bool have;
    db::Pose upFrom;         // (up: the get-up's last pose, the walk blended in from it over db::kWalkUpBlend - upT the time so far)
    bool upBlend;
    float upT;
    bool handBack;           // (v1.0, up: the get-up's last pose blended into the game's own over kDbHandBack - handT the time so far)
    float handT;
    bool fadingIn;           // (fading in: the walker's feet kept planted; fading out: his legs handed back to the game's walk)
    int plantWait;           // (frames waited for both his feet to be down before the walker takes them)
    Mat4 W[db::J_MAX];
    Vec3 gameFwd;            // which way the game has him facing (from its own skeleton, last frame)
    bool haveFwd;
    // the ground round him
    float gy[11 * 11];
    Vec3 g0;
    float gstep;
    bool gok;
    int gserial;   // (each new probe: the pose trace writes the grid again)
    int log;
    int walkLog;             // (debug "dbwalklog <n>": reports of the walk left)
    bool off;                // (debug "dbody 0": the game's own walk and the old pose layer)
    int seed;
} g_db;

// ------------------------------------------------------------------ the ground
static float DbGround(void*, float x, float z)
{
    if (!g_db.gok) return g_db.g0.y;
    float fx = (x - g_db.g0.x) / g_db.gstep, fz = (z - g_db.g0.z) / g_db.gstep;
    fx = Clampf(fx, 0.0f, 9.999f), fz = Clampf(fz, 0.0f, 9.999f);
    int ix = (int)fx, iz = (int)fz;
    float ux = fx - ix, uz = fz - iz;
    const float* g = g_db.gy;
    float a = g[iz * 11 + ix], b = g[iz * 11 + ix + 1], c = g[(iz + 1) * 11 + ix], d = g[(iz + 1) * 11 + ix + 1];
    return (a * (1 - ux) + b * ux) * (1 - uz) + (c * (1 - ux) + d * ux) * uz;
}
// an 11 x 11 grid, 0.3 m apart (3 m square), round `c`, probed again when he's moved half a metre from its middle (a probe that
// misses - nothing under it, or far above or below his feet - takes his feet's height). Ground is what a foot can step onto: no more
// than 40 cm above his feet - a hedge's or a wall's top read as ground put a step on it and his legs went straight up through his body
// (the slope test beside a hedge: his ankles half a metre above his hips)
static void DbGroundAround(Vec3 c, float feetY, bool force)
{
    float half = 5 * 0.3f;
    Vec3 mid = g_db.g0 + V3(half, 0, half);
    if (!force && g_db.gok && Len(V3(c.x - mid.x, 0, c.z - mid.z)) < 0.5f) return;
    g_db.gstep = 0.3f;
    g_db.g0 = V3(c.x - half, feetY, c.z - half);
    // (where the terrain has nothing - a pavement or a kerb is a collision object, not terrain - a ray down onto what's there; what
    // still has nothing takes its neighbours' height. Taking his feet's height, the bottom of a sloping lawn read as a 10 cm ledge
    // where it met the pavement: lying downhill he was lifted onto it and his knees floated 12 cm - drqa on the slope test)
    bool ok[121];
    for (int iz = 0; iz < 11; iz++)
        for (int ix = 0; ix < 11; ix++) {
            int i = iz * 11 + ix;
            Vec3 p = V3(g_db.g0.x + ix * 0.3f, feetY + 0.9f, g_db.g0.z + iz * 0.3f);
            float y;
            ok[i] = Trace_GroundY(p, &y) && y < feetY + 0.4f && y > feetY - 1.2f;
            if (!ok[i]) {
                HitResult h;
                Vec3 a = V3(p.x, feetY + 0.4f, p.z), b = V3(p.x, feetY - 1.2f, p.z);
                if (TraceShot(a, b, g_player, &h, 0.02f) && h.kind != HIT_CHARACTER && h.normal.y > 0.6f) y = h.pos.y, ok[i] = true;
            }
            g_db.gy[i] = ok[i] ? y : feetY;
        }
    for (int pass = 0; pass < 10; pass++) {
        bool any = false;
        for (int i = 0; i < 121; i++) {
            if (ok[i]) continue;
            int ix = i % 11, iz = i / 11, n = 0;
            float sum = 0.0f;
            const int nb[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (auto& d : nb) {
                int jx = ix + d[0], jz = iz + d[1];
                if (jx < 0 || jx > 10 || jz < 0 || jz > 10 || !ok[jz * 11 + jx]) continue;
                sum += g_db.gy[jz * 11 + jx], n++;
            }
            if (n) g_db.gy[i] = sum / n, ok[i] = true, any = true;
        }
        if (!any) break;
    }
    g_db.gok = true;
    g_db.gserial++;
}

// ------------------------------------------------------------------ Homer's skin points, from his own files
// (v1.0 RC2) kDbPoints is built from the player's art\chars\homer_m.p3d and homer_a.p3d (dbody_gen.h) on a thread of its
// own as the game starts - some 30 ms, never on a frame - and DbLoad takes it the first time the drunk body loads (long
// done by then). It must come out as v1.0 RC1's table did (kDbRc1Print, that table's fingerprint): the log says if it
// doesn't (another Homer model, say). If it can't be built the drunk body stays off: Homer keeps the lean, as the others.
static const uint64_t kDbRc1Print = 0x99ad904bb4f68535ull;   // (386 points: tests/dbody_gen_test.cpp checks it field by field)
// (v1.0 RC3) on the same thread, the shotgun's chin points for the five models (charshape.inc's jawVert: vertex numbers in
// <name>_m.p3d), checked against v1.0 RC2's compiled-in ones by fingerprint; a model they can't be read from has none (its
// chin then isn't lifted off the gun - no other change)
enum { kJawModels = 5 };
static const uint64_t kJawRc2Print = 0xdb859594914e8830ull;   // (34 points: tests/jaw_gen_test.cpp checks them bit for bit)
static float g_jawPts[kJawModels][8][3];
static int g_jawN[kJawModels];
static char g_jawWhy[200];
static dbgen::Out g_dbPtsOut[DbPointTable::kMax];
static int g_dbPtsN;
static bool g_dbPtsOk;
static char g_dbPtsWhy[160];
static double g_dbPtsMs;
static HANDLE g_dbPtsThread;
static uint8_t* DbReadGameFile(const wchar_t* name, size_t* n)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls..\\art\\chars\\%ls", g_modDir, name);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long z = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* b = z > 0 && z < 16 * 1024 * 1024 ? (uint8_t*)malloc(z) : nullptr;
    *n = b && fread(b, 1, z, f) == (size_t)z ? (size_t)z : 0;
    fclose(f);
    if (!*n) free(b), b = nullptr;
    return b;
}
static DWORD WINAPI DbPointsThread(void*)
{
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
    size_t nm = 0, na = 0;
    uint8_t* bm = DbReadGameFile(L"homer_m.p3d", &nm);
    uint8_t* ba = DbReadGameFile(L"homer_a.p3d", &na);
    if (!bm || !ba) snprintf(g_dbPtsWhy, sizeof(g_dbPtsWhy), "%s isn't there", !bm ? "homer_m.p3d" : "homer_a.p3d");
    else g_dbPtsOk = dbgen::BuildPoints(bm, nm, ba, na, db::kJointNames, g_dbPtsOut, DbPointTable::kMax, &g_dbPtsN, g_dbPtsWhy, sizeof(g_dbPtsWhy));
    static_assert(sizeof(kCharShapes) / sizeof(kCharShapes[0]) == kJawModels, "the five models");
    for (int c = 0; c < kJawModels; c++) {
        const CharShape& s = kCharShapes[c];
        wchar_t file[64];
        swprintf(file, 64, L"%hs_m.p3d", s.name);
        size_t n = 0;
        uint8_t* b = !strcmp(s.name, "homer") && bm ? bm : DbReadGameFile(file, &n);
        if (b == bm) n = nm;
        char why[120] = "";
        if (!b) snprintf(why, sizeof(why), "isn't there");
        else if (dbgen::JawPoints(b, n, s.jawVert, s.njaw, s.meshVerts, g_jawPts[c], why, sizeof(why))) g_jawN[c] = s.njaw;
        if (b != bm) free(b);
        if (!g_jawN[c]) {
            size_t k = strlen(g_jawWhy);
            snprintf(g_jawWhy + k, sizeof(g_jawWhy) - k, "%s%s_m.p3d: %s", k ? "; " : "", s.name, why);
        }
    }
    free(bm), free(ba);
    QueryPerformanceCounter(&t1);
    g_dbPtsMs = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
    return 0;
}
void Gun_StartBodyData()   // (main.cpp's InitOnce: as the game starts)
{
    if (!g_dbPtsThread) g_dbPtsThread = CreateThread(nullptr, 0, DbPointsThread, nullptr, 0, nullptr);
}
static bool g_bodyJoined, g_dbPtsReady;
static void BodyDataJoin()   // (the first time either is wanted: the thread's results taken, and logged)
{
    if (g_bodyJoined) return;
    g_bodyJoined = true;
    if (g_dbPtsThread) WaitForSingleObject(g_dbPtsThread, INFINITE), CloseHandle(g_dbPtsThread), g_dbPtsThread = nullptr;
    else DbPointsThread(nullptr);   // (no thread: build it here, once)
    static_assert(sizeof(dbgen::Out) == sizeof(DbPoint), "the table's entries");
    if (!g_dbPtsOk) Log("dbody: no skin points (%s) - the drunk body is off; Homer keeps the lean", g_dbPtsWhy);
    else {
        memcpy(kDbPoints.pt, g_dbPtsOut, sizeof(DbPoint) * g_dbPtsN);
        kDbPoints.n = g_dbPtsN;
        uint64_t print = dbgen::Fingerprint(g_dbPtsOut, g_dbPtsN);
        Log("dbody: skin points from homer_m.p3d + homer_a.p3d: %d in %.0f ms, %s (fingerprint %016llx)", g_dbPtsN, g_dbPtsMs,
            print == kDbRc1Print ? "the same as v1.0 RC1's" : "NOT v1.0 RC1's - a different Homer model? used as built", (unsigned long long)print);
        g_dbPtsReady = true;
    }
    uint64_t jp = dbgen::JawFingerprint(g_jawPts, g_jawN, kJawModels);
    Log("chin points from the models: homer %d, bart %d, lisa %d, marge %d, apu %d - %s (fingerprint %016llx)%s%s", g_jawN[0], g_jawN[1],
        g_jawN[2], g_jawN[3], g_jawN[4], jp == kJawRc2Print ? "the same as v1.0 RC2's" : "NOT v1.0 RC2's", (unsigned long long)jp,
        g_jawWhy[0] ? " - " : "", g_jawWhy);
}
static bool DbPointsReady()
{
    BodyDataJoin();
    return g_dbPtsReady;
}
static const JawPt* CharJaw(const CharShape* s, int* n)   // (charshape.inc: a model's chin points, from its own file)
{
    BodyDataJoin();
    int c = (int)(s - kCharShapes);
    if (c < 0 || c >= kJawModels) return *n = 0, nullptr;
    *n = g_jawN[c];
    return g_jawPts[c];
}

// ------------------------------------------------------------------ the rig and the clips
static bool DbLoad()
{
    if (g_db.tried && g_db.skel == g_invBindSkel) return g_db.ok;
    g_db.tried = true, g_db.ok = false, g_db.skel = g_invBindSkel;
    if (!DbPointsReady()) return false;
    if (!g_invBindSkel || !db::RigBuild(&g_db.rig, 33, g_parent, g_invBind)) return Log("dbody: no rig"), false;
    if (!g_db.bank.n) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls..\\art\\chars\\homer_a.p3d", g_modDir);
        FILE* f = _wfopen(path, L"rb");
        if (!f) return Log("dbody: no homer_a.p3d"), false;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* b = n > 0 && n < 16 * 1024 * 1024 ? (uint8_t*)malloc(n) : nullptr;
        bool rd = b && fread(b, 1, n, f) == (size_t)n;
        fclose(f);
        bool ok = rd && db::BankLoad(&g_db.bank, b, n);
        free(b);
        if (!ok) return Log("dbody: homer_a.p3d didn't decode"), false;
    }
    g_db.cs.getUp = db::BankFind(g_db.bank, "hom_get_up");
    g_db.cs.balance = db::BankFind(g_db.bank, "hom_balence");
    g_db.cs.idle = db::BankFind(g_db.bank, "hom_loco_idle_rest");
    g_db.cs.walk = db::BankFind(g_db.bank, "hom_loco_walk");
    if (!g_db.cs.getUp || !g_db.cs.balance || !g_db.cs.idle || !g_db.cs.walk) return Log("dbody: Homer's clips missing"), false;
    db::WalkerSetup(&g_db.walker, g_db.rig, g_db.cs);
    g_db.walker.init = false;
    g_db.ok = true;
    Log("dbody: rig (size %.2f), %d clips, walk %.2f m/s, stance %.2f m", g_db.rig.k, g_db.bank.n, g_db.walker.walkSpeed, g_db.walker.halfW * 2);
    {   // (Radical's own knees: the idle's, and the walk's through its stride - what his drunk legs are judged against)
        db::Pose p;
        db::ClipPose(g_db.rig, *g_db.cs.idle, 0, &p);
        float lo = 9, hi = 0;
        for (float fr = 0; fr < g_db.cs.walk->frames - 1; fr += 1.0f) {
            db::Pose q;
            db::ClipPose(g_db.rig, *g_db.cs.walk, fr, &q);
            for (int j : {db::J_KNEE_L, db::J_KNEE_R}) lo = fminf(lo, db::HingeAngle(q.q[j])), hi = fmaxf(hi, db::HingeAngle(q.q[j]));
        }
        Log("dbody: Radical's knees - idle L %.0f R %.0f deg, walk %.0f-%.0f deg; root standing %.3f walking %.3f m; legs %.3f + %.3f m, ankle %.3f m",
            db::HingeAngle(p.q[db::J_KNEE_L]) * 57.3f, db::HingeAngle(p.q[db::J_KNEE_R]) * 57.3f, lo * 57.3f, hi * 57.3f, g_db.walker.rootH,
            g_db.walker.walkRootH, Len(g_db.rig.t[db::J_KNEE_L]), Len(g_db.rig.t[db::J_ANKLE_L]), g_db.walker.ankleH);
    }
    return true;
}
// (only Homer has his clips here; the others keep the game's own pose and the old lean)
static bool DbHomer() { const CharShape* c = CharShapeNow(); return !c || c == &kCharShapes[0]; }

static bool DrunkBody_Falling() { return g_db.falling || g_db.handBack; }   // (v1.0: his walk held till he's the game's again)
static void DrunkBody_Reset()   // (sober at once - a new level: no fall, no drunk walk carried on)
{
    g_db.falling = false, g_db.fall.ph = db::F_OFF, g_db.moved = false;
    g_db.have = false, g_db.walker.init = false, g_db.onW = 0, g_db.upBlend = false, g_db.handBack = false;
}
// (v1.0: on - a fall can start - while he's drunk on his feet and the pose layer has shown us his pose; its own walk: once faded in)
static bool DrunkBody_On() { return g_db.ok && !g_db.off && g_db.have && (!g_dbProcWalk || g_db.onW > 0.001f); }
// (whether he's the drunk body's to draw: v1.0, while he falls and gets up; its own walk, whenever drunk)
static bool DrunkBody_Handles() { return !g_db.off && DbHomer() && DbLoad() && (g_dbProcWalk || g_db.falling || g_db.handBack); }

// ------------------------------------------------------------------ a fall
// over he goes: the way he leans (forward-ish: a lean to a side or behind turns into his feet catching him - drunk.inc only sends
// forward falls here), as fast as he was going
static bool DrunkBody_Fall(Vec3 dir, float speed, float lean, float level)
{
    if (!g_db.ok || !g_db.have || g_db.falling || g_db.off) return false;
    Vec3 me;
    if (!PlayerPos(&me)) return false;
    // room to go down in: he lies ~1.6 m along the way he falls - a wall, a hedge, a fence or a car there (at his knees or his
    // chest) and he'd go through it. The way he's going if it's clear, else the nearest clear way within 50 deg; none - he doesn't go
    // over here (the balance holds him at the edge: he staggers against it)
    if (Trace_Available()) {
        static const float dev[5] = {0.0f, 0.44f, -0.44f, 0.87f, -0.87f};
        Vec3 d0 = Norm(V3(dir.x, 0, dir.z) + V3(0, 0, 1e-5f));
        bool found = false;
        for (int i = 0; i < 5 && !found; i++) {
            Vec3 d = db::QRot(db::QAxis(V3(0, 1, 0), dev[i]), d0);
            bool clear = true;
            for (float hgt : {0.12f, 0.35f, 0.9f}) {   // (a planter's edge is lower than his knees)
                Vec3 from = me + UP * hgt;
                HitResult h;
                if (TraceShot(from, from + d * 1.6f, g_player, &h, 0.08f) && h.kind != HIT_CHARACTER && h.ground < 0) clear = false;
            }
            if (clear) dir = d, found = true;
            if (clear && i) Log("dbody: the way he was falling is blocked - turned %.0f deg", dev[i] * 57.3f);
        }
        if (!found) return Log("dbody: no room to fall - held up"), false;
    }
    DbGroundAround(me + Norm(V3(dir.x, 0, dir.z) + V3(0, 0, 1e-5f)) * 0.6f, me.y, true);
    // (walking: his feet as the walker has them - a swinging one where it was landing)
    Vec3 feet[2], ffw[2];
    for (int s = 0; s < 2; s++) {
        const db::WFoot& F = g_db.walker.f[s];
        feet[s] = F.swing ? F.b : F.at, ffw[s] = F.swing ? F.fb : F.fwd;
    }
    bool walk = g_db.walker.init;
    if (!g_dbProcWalk) {   // (v1.0: his feet as Radical's walk has them this frame - a foot in the air steps down as he teeters)
        Mat4 Wf[db::J_MAX];
        db::FK(g_db.rig, g_db.pose, Wf);
        for (int s = 0; s < 2; s++) {
            Vec3 an = db::Pos(Wf, s ? db::J_ANKLE_R : db::J_ANKLE_L), d = db::Pos(Wf, s ? db::J_BALL_R : db::J_BALL_L) - an;
            feet[s] = V3(an.x, DbGround(nullptr, an.x, an.z), an.z);
            ffw[s] = Len(V3(d.x, 0, d.z)) > 0.01f ? db::FlatN(d) : Norm(V3(dir.x, 0, dir.z) + V3(0, 0, 1e-5f));
        }
        walk = true;
    }
    db::FallStart(&g_db.fall, g_db.rig, g_db.cs, g_db.pose, dir, speed, lean, level, DbGround, nullptr, (uint32_t)(++g_db.seed * 7919 + GetTickCount()),
                  walk ? feet : nullptr, walk ? ffw : nullptr);
    g_db.falling = true, g_db.moved = false;
    Gun_DropForFall();
    Sound_Play(SND_WHOA, 0.9f, 0.95f + 0.1f * Rnd());
    {
        Vec3 n = g_db.fall.tiltN;
        Log("dbody: falls (lean %.2f, speed %.1f, level %.2f) - the ground under him tilts %.1f deg (normal %.3f %.3f %.3f), his way (%.2f %.2f)",
            lean, speed, level, acosf(Clampf(n.y, -1.0f, 1.0f)) * 57.3f, n.x, n.y, n.z, dir.x, dir.z);
    }
    return true;
}

// ------------------------------------------------------------------ each frame (drunk.inc's Drunk_Update, after the balance)
// -> true while he's down (the game's walk held still)
static bool DrunkBody_Update(float dt, bool onFoot, float level)
{
    if (g_db.off || !DbHomer() || !DbLoad()) {
        g_db.have = false, g_db.onW = 0, g_db.upBlend = false;
        return false;
    }
    if (!onFoot && (g_db.falling || g_db.handBack)) {   // (the game put him in a car mid-fall - nothing of the fall carries on when he
                                                        // gets out)
        g_db.falling = false, g_db.fall.ph = db::F_OFF, g_db.moved = false;
        g_db.have = false, g_db.walker.init = false, g_db.onW = 0, g_db.upBlend = false, g_db.handBack = false;
        Log("dbody: fall cancelled (in a car)");
    }
    if (!g_dbProcWalk) {   // v1.0: only his fall is ours - walking and standing, the game's own (and the sway on top)
        if (!onFoot || (!Drunk_Active() && !g_db.falling)) g_db.have = false;   // (the pose layer shows us his pose again when drunk)
        if (g_db.handBack) {
            g_db.handT += dt;
            if (g_db.handT >= kDbHandBack) g_db.handBack = false, g_db.have = false;
            return g_db.handBack;
        }
        if (!g_db.falling) return false;
    }
    bool want = (Drunk_Active() || g_db.falling) && onFoot;
    g_db.onW = Clampf(g_db.onW + (want && g_db.have ? dt / 0.4f : -dt / 0.8f), 0.0f, 1.0f);   // (fading in once his feet are set
                                                                                             // down; out slower, into the game's stride)
    g_db.fadingIn = want;
    Vec3 me;
    if (!want && g_db.onW <= 0.0f) {
        g_db.have = false, g_db.walker.init = false, g_db.upBlend = false;
        return false;
    }
    if (!PlayerPos(&me)) return g_db.falling;
    if (!g_db.falling) DbGroundAround(me, me.y, false);
    Vec3 fwd = g_db.haveFwd ? g_db.gameFwd : HeadingDir(G.bodyYaw);
    if (g_db.falling) {
        db::Pose p = g_db.pose;
        int was = g_db.fall.ph, thuds = g_db.fall.thuds, groans = g_db.fall.groans;
        bool going = db::FallUpdate(&g_db.fall, g_db.rig, g_db.cs, dt, DbGround, nullptr, &p);
        if (g_db.fall.thuds > thuds) {
            Sound_Play(SND_THUD, 0.6f + 0.4f * g_db.fall.impact, 0.9f + 0.1f * Rnd());
            Input_Rumble(0.4f, 0.6f, 0.25f);
        }
        if (g_db.fall.groans > groans) Sound_Play(SND_GROAN, 0.8f, 0.95f + 0.1f * Rnd());
        // as he starts to get up, his game body goes to where he'll stand (unseen: all of him is drawn from the fall) - the camera
        // and the world have him there when he does
        if (!g_db.moved && g_db.fall.ph >= db::F_GETUP) {
            Vec3 at;
            db::Q hq;
            db::FallStandAt(g_db.fall, g_db.rig, g_db.cs, &at, &hq);
            Vec3 f = db::QRot(hq, V3(0, 0, -1));
            Lab_Relocate(at.x, DbGround(nullptr, at.x, at.z), at.z, remainderf(atan2f(f.x, f.z) + PI, 2.0f * PI));
            g_db.moved = true;
            Log("dbody: body moved to (%.2f %.2f) for the stand", at.x, at.z);
        }
        if (g_db.log && g_db.fall.ph != was) Log("dbody: %s at %.2f s", db::kFallPhase[g_db.fall.ph], g_db.fall.t);
        if (going) g_db.pose = p, g_db.have = true;
        else if (!g_dbProcWalk) {   // (v1.0) up: the game's own pose takes him from here, from Radical's last get-up frame
            g_db.falling = false;
            g_db.upFrom = g_db.pose, g_db.handBack = true, g_db.handT = 0;
            Log("dbody: up (%.1f s after the fall) - back to the game's own walk", g_db.fall.t);
            return true;
        } else {   // up: the walk takes him from here
            g_db.falling = false;
            db::Q hq;
            Vec3 at;
            db::FallStandAt(g_db.fall, g_db.rig, g_db.cs, &at, &hq);
            db::WalkerPlant(&g_db.walker, g_db.rig, g_db.pose, db::FlatN(db::QRot(hq, V3(0, 0, -1))), DbGround, nullptr);
            g_db.upFrom = g_db.pose, g_db.upBlend = true, g_db.upT = 0;   // (the walk from Radical's last get-up frame, not in one frame)
            Log("dbody: up (%.1f s after the fall)", g_db.fall.t);
        }
        if (g_db.falling) {
            db::FK(g_db.rig, g_db.pose, g_db.W);
            return true;
        }
    }
    // walking (or standing)
    if (!g_db.walker.init) {
        if (!g_db.have) return false;   // (the walker starts from the game's own pose: DrunkBody_Pose plants it)
    }
    g_db.bal.lean = g_drunk.lean, g_db.bal.vel = g_drunk.leanVel, g_db.bal.clock = g_drunk.clock;
    g_db.bal.h = 0.78f * g_db.rig.k;
    db::WalkIn in;
    in.root = me, in.fwd = fwd, in.vel = g_drunk.rootVel, in.lean = g_drunk.lean, in.leanVel = g_drunk.leanVel, in.level = level, in.dt = dt;
    db::Pose p;
    db::WalkerUpdate(&g_db.walker, g_db.rig, g_db.cs, g_db.bal, in, DbGround, nullptr, &p);
    if (g_db.upBlend) {
        db::BlendIntoWalk(g_db.rig, g_db.upFrom, g_db.upT / db::kWalkUpBlend, &p);
        g_db.upT += dt;
        if (g_db.upT >= db::kWalkUpBlend) g_db.upBlend = false;
    }
    g_db.pose = p, g_db.have = true;
    db::FK(g_db.rig, g_db.pose, g_db.W);
    if (g_db.walkLog > 0 && (int)(g_drunk.clock * 4.0f) != (int)((g_drunk.clock - dt) * 4.0f)) {
        g_db.walkLog--;
        const db::Walker& w = g_db.walker;
        Vec3 left = db::LeftOf(w.bodyF), a = w.f[0].swing ? w.f[0].b : w.f[0].at, b = w.f[1].swing ? w.f[1].b : w.f[1].at;
        Vec3 pel = Row(g_db.W[db::J_PELVIS], 3);
        Log("dbody walk: L %.2f speed %.2f hips %.3f (target %.3f, legs allow %.3f, wanted %.3f; standing %.3f) | knees L %.0f R %.0f deg | "
            "feet apart across %.2f along %.2f m (swing %d%d) | pelvis over the feet %.2f m | steps %d (across %d)",
            level, Len(db::Flat(w.bodyV)), w.pelY, w.dbg[0], w.dbg[1], w.dbg[3], w.rootH, db::HingeAngle(g_db.pose.q[db::J_KNEE_L]) * 57.3f,
            db::HingeAngle(g_db.pose.q[db::J_KNEE_R]) * 57.3f, Dot(db::Flat(a - b), left), Dot(db::Flat(a - b), w.bodyF), w.f[0].swing ? 1 : 0,
            w.f[1].swing ? 1 : 0, pel.y - w.dbg[5], w.steps, w.crosses);
    }
    return false;
}

// ------------------------------------------------------------------ into the game's skeleton (drunk.inc's Drunk_PoseLayer)
// -> true if it posed him (all of him; the game's own pose kept in g_drunkOrig for matching the upload)
// (its own walk, "dbwalk 1": the walker's pose - blended with the game's own while it fades in; false while it doesn't draw him)
static bool DbWalkPose(PoserPose& P, db::Pose* po)
{
    db::Pose& out = *po;
    if (!g_db.have) {
        // the first frame: the walker's feet set down where the game has his - once both are down (mid-stride, the one in the air
        // was set down at once: snapped to the ground, the leg twisted to get it there; the drink comes on slowly - a step's wait
        // is nothing)
        if (!g_db.walker.init && (Drunk_Active() || g_db.falling)) {
            db::Pose gp;
            for (int j = 4; j < 33; j++) gp.q[j] = db::RowQ(ToMat(P.object[j]));
            gp.rq = db::RowQ(ToMat(P.world[3])), gp.rp = Row(ToMat(P.world[3]), 3);
            Mat4 Wg[db::J_MAX];
            db::FK(g_db.rig, gp, Wg);
            bool down = true;
            for (int an : {db::J_ANKLE_L, db::J_ANKLE_R}) {
                Vec3 a = db::Pos(Wg, an);
                if (a.y - DbGround(nullptr, a.x, a.z) > g_db.rig.ankleH + 0.03f) down = false;
            }
            if (down || g_db.falling || ++g_db.plantWait > 40) {
                db::WalkerPlant(&g_db.walker, g_db.rig, gp, g_db.gameFwd, DbGround, nullptr);
                g_db.pose = gp, g_db.have = true, g_db.plantWait = 0;
            }
        }
        return false;
    }
    if (g_db.onW <= 0.001f) return false;
    g_drunkOrig = P;
    // blended with the game's own pose while it fades in (each joint in its parent's frame; his root in the world)
    out = g_db.pose;
    if (g_db.onW < 0.999f && !g_db.falling) {
        db::Pose gp;
        for (int j = 4; j < 33; j++) gp.q[j] = db::RowQ(ToMat(P.object[j]));
        gp.rq = db::RowQ(ToMat(P.world[3])), gp.rp = Row(ToMat(P.world[3]), 3);
        db::Blend(g_db.rig, gp, g_db.pose, db::Smooth(g_db.onW), &out);
        // (fading in, his feet stay where the walker has them, planted, while the rest of him blends - joint turns blended leg by
        // leg let the feet drift through the ground. Fading out - sober again - the game's own walk has his legs back as it goes:
        // kept planted to the end, they snapped to the game's stride at the last frame)
        Mat4 Wp[db::J_MAX], Wo[db::J_MAX];
        for (int s = 0; s < 2 && g_db.fadingIn; s++) {
            if (s == 0) db::FK(g_db.rig, g_db.pose, Wp);
            int hp = s ? db::J_HIP_R : db::J_HIP_L, kn = s ? db::J_KNEE_R : db::J_KNEE_L, an = s ? db::J_ANKLE_R : db::J_ANKLE_L;
            int ba = s ? db::J_BALL_R : db::J_BALL_L;
            for (int j : {hp, kn, an}) out.q[j] = g_db.pose.q[j];   // (from the walker's own leg: its roll - a blend of the game's
                                                                    // thigh roll and the walker's twisted the ankle when re-aimed)
            db::FK(g_db.rig, out, Wo);
            Vec3 H = db::Pos(Wp, hp), K = db::Pos(Wp, kn), A = db::Pos(Wp, an);
            Vec3 line = Norm(A - H), bend = K - H - line * Dot(K - H, line);
            db::IKHinge(g_db.rig, &out, Wo, hp, kn, an, A, Len(bend) > 1e-4f ? Norm(bend) : V3(0, 0, 1), db::kKneeMax);
            db::SetWorldQ(g_db.rig, &out, an, db::WorldQ(g_db.rig, g_db.pose, an));
            out.q[ba] = g_db.pose.q[ba];
        }
    }
    return true;
}

static bool DrunkBody_Pose(PoserPose& P)
{
    int n = P.numJoints < 40 ? P.numJoints : 40;
    if (n < 33 || !DbHomer() || !DbLoad()) return false;
    // which way the game has him facing (its own Motion_Root's -Z), for the walker next frame
    Vec3 f = V3(-P.world[0][8], 0, -P.world[0][10]);
    if (Len(f) > 0.1f) g_db.gameFwd = Norm(f), g_db.haveFwd = true;
    if (g_db.off) return false;
    db::Pose out;
    if (!g_dbProcWalk) {   // v1.0: his fall, and the hand-back to the game's own pose as he's up
        if (!g_db.falling && !g_db.handBack) return false;   // (the game's own: the sway goes on top - DrunkBody_SeePose keeps it)
        g_drunkOrig = P;
        out = g_db.pose;
        if (g_db.handBack) {   // (all of him, his root too: the game's body was moved to where he stands as he got up)
            db::Pose gp;
            for (int j = 4; j < 33; j++) gp.q[j] = db::RowQ(ToMat(P.object[j]));
            gp.rq = db::RowQ(ToMat(P.world[3])), gp.rp = Row(ToMat(P.world[3]), 3);
            db::Blend(g_db.rig, g_db.upFrom, gp, db::Smooth(Clampf(g_db.handT / kDbHandBack, 0.0f, 1.0f)), &out);
        }
    } else if (!DbWalkPose(P, &out))
        return false;
    static Mat4 W[db::J_MAX];
    db::FK(g_db.rig, out, W);
    // the jaw slack (drunk.inc's), on top
    if (g_drunk.jaw > 0.002f && !g_db.falling) {
        Mat4 L18 = Mul(W[18], FullInverse(W[17]));
        Mat4 Lj = WithTrans(Mul(AxisAngle(V3(0, 0, 1), g_drunk.jaw), Rot3(L18)), Row(L18, 3));
        W[18] = Mul(Lj, W[17]);
    }
    for (int j = 0; j < n; j++) {
        if (j < 33) memcpy(P.world[j], W[j].m, 64);
        else {   // (joints past Homer's 33 - none on him - follow their parents)
            Mat4 Lj = Mul(ToMat(g_drunkOrig.world[j]), FullInverse(ToMat(g_drunkOrig.world[g_parent[j]])));
            Mat4 Wj = Mul(Lj, ToMat(P.world[g_parent[j]]));
            memcpy(P.world[j], Wj.m, 64);
        }
        if (j > 0) {
            Mat4 Lj = Mul(ToMat(P.world[j]), FullInverse(ToMat(P.world[g_parent[j]])));
            memcpy(P.object[j], Lj.m, 64);
        }
        g_drunkPosed[j] = true;
    }
    g_drunkSwayed = true;
    G.haveBody = true;
    return true;
}
// (v1.0) his pose as drawn this frame - the game's own with the sway on it (drunk.inc's pose layer) - kept for a fall to start from
static void DrunkBody_SeePose(const PoserPose& P)
{
    if (g_db.off || g_dbProcWalk || g_db.falling || g_db.handBack || P.numJoints < 33 || !DbHomer() || !DbLoad()) return;
    for (int j = 4; j < 33; j++) g_db.pose.q[j] = db::RowQ(ToMat(P.object[j]));
    g_db.pose.rq = db::RowQ(ToMat(P.world[3])), g_db.pose.rp = Row(ToMat(P.world[3]), 3);
    g_db.have = true;
}
static bool DrunkBody_Pelvis(Vec3* p)   // (the debug orbit camera's target: the body as drawn, falling or not - no jump as he stands)
{
    if (!DrunkBody_On() || (!g_dbProcWalk && !g_db.falling && !g_db.handBack)) return false;
    *p = Row(g_db.W[db::J_PELVIS], 3);
    return true;
}

// (posetrace.inc's drunk-body slots: trace.py dkLevel, dkPhase, dkPhaseT, dkGround, dkFootL (the get-up's clip frame), dkFootR (the
// swinging feet: 1 left, 2 right), dkHandL (the lean, rad), dkHandR (how much his walk is steered under him))
// (for the QA: while the drunk body draws him, the ground under each of his contacts - hands, knees, feet - in slots the shotgun
// alone uses (2-4, 6-8): the lawn and the kerb aren't flat, and one ground height under his middle misjudged them by 5 cm)
// (the pose trace: the ground grid his drunk body is posed on, when it's in use - so the QA can judge every point of him against the
// ground under it, not against the ground under his root: lying along a slope he read as 11 cm "into the ground", drqa)
static bool DbTraceGrid(float* origin, float* step, const float** grid, int* serial)
{
    if (!g_db.have || g_db.onW <= 0.001f || !g_db.gok || g_db.off) return false;
    origin[0] = g_db.g0.x, origin[1] = g_db.g0.y, origin[2] = g_db.g0.z;
    *step = g_db.gstep, *grid = g_db.gy, *serial = g_db.gserial;
    return true;
}
static void DbTraceGrounds(float* st)
{
    if (!g_db.have || g_db.onW <= 0.001f || !g_db.gok || g_db.off) return;
    static const int js[6] = {db::J_WR_L, db::J_WR_R, db::J_KNEE_L, db::J_KNEE_R, db::J_BALL_L, db::J_BALL_R};
    static const int slot[6] = {2, 3, 4, 6, 7, 8};
    for (int k = 0; k < 6; k++) {
        Vec3 p = Row(g_db.W[js[k]], 3);
        st[slot[k]] = DbGround(nullptr, p.x, p.z);
    }
}
static float DbTraceState(int i)
{
    switch (i) {
    case 0: return g_drunk.level;
    case 1: return g_db.falling ? (float)g_db.fall.ph : 0.0f;
    case 2: return g_db.falling ? g_db.fall.pt : 0.0f;
    case 3: {
        Vec3 me;
        return g_db.gok && PlayerPos(&me) ? DbGround(nullptr, Row(g_db.W[db::J_PELVIS], 3).x, Row(g_db.W[db::J_PELVIS], 3).z) : 0.0f;
    }
    case 4: return g_db.falling ? g_db.fall.sf : 0.0f;
    case 5: return (float)((g_db.walker.f[0].swing ? 1 : 0) + (g_db.walker.f[1].swing ? 2 : 0));
    case 6: return Len(g_drunk.lean);
    case 7: return g_drunk.stepW;
    }
    return 0.0f;
}

// ------------------------------------------------------------------ debug
static bool DrunkBodyDebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "dbody")) {   // "dbody 0|1": the new drunk body off (the game's walk, the old lean) / on
        g_db.off = arg[0] && atoi(arg) == 0;
        Log("dbody: %s", g_db.off ? "off" : "on");
        return true;
    }
    if (!strcmp(cmd, "dblog")) {
        g_db.log = arg[0] ? atoi(arg) : 1;
        Log("dbody: ok %d have %d onW %.2f falling %d phase %s walker steps %d (across %d)", g_db.ok, g_db.have, g_db.onW, g_db.falling,
            g_db.falling ? db::kFallPhase[g_db.fall.ph] : "-", g_db.walker.steps, g_db.walker.crosses);
        return true;
    }
    if (!strcmp(cmd, "dbfall")) {   // "dbfall <deg rel. to his facing>,<speed>": over he goes (forward is 0)
        float deg = 0, spd = 1.0f;
        sscanf(arg, "%f,%f", &deg, &spd);
        Vec3 f = g_db.haveFwd ? g_db.gameFwd : HeadingDir(G.bodyYaw);
        float a = atan2f(f.x, f.z) + deg * PI / 180.0f;
        if (!DrunkBody_Fall(V3(sinf(a), 0, cosf(a)), spd, 0.3f, fmaxf(g_drunk.level, 0.8f))) Log("dbody: can't fall now");
        return true;
    }
    if (!strcmp(cmd, "dbslope")) {   // "dbslope <lying>,<feet>": on a slope, lying along the ground (1) and the feet on its tilt (1); 0 = as before
        float a = 1, b = 1;
        sscanf(arg, "%f,%f", &a, &b);
        db::kSlopeTilt = a, db::kWalkGroundTilt = b;
        Log("dbslope: lying %.0f, feet %.0f", a, b);
        return true;
    }
    if (!strcmp(cmd, "dbtrip")) {   // "dbtrip <strength>[,<deg rel. to his facing>]": his foot catches - a kick to his balance (0.06 a
                                    // stumble he catches, 0.16 one he doesn't)
        float k = 0.10f, deg = 0;
        sscanf(arg, "%f,%f", &k, &deg);
        Vec3 f = g_db.haveFwd ? g_db.gameFwd : HeadingDir(G.bodyYaw);
        float a = atan2f(f.x, f.z) + deg * PI / 180.0f;
        g_drunk.tripDir = V3(sinf(a), 0, cosf(a)), g_drunk.tripT = k;
        g_drunk.stumbles += 1.0f;
        Log("dbody: trip %.2f", k);
        return true;
    }
    if (!strcmp(cmd, "dbwalk")) {   // "dbwalk 0|1": walking and standing drunk, the game's own walk with the sway (v1.0) / the drunk
                                    // body's own walk, his feet placed by his balance (as before v1.0)
        g_dbProcWalk = arg[0] && atoi(arg) != 0;
        g_dwHybrid = !g_dbProcWalk;
        g_db.have = false, g_db.walker.init = false, g_db.onW = 0, g_db.upBlend = false;
        Log("dbwalk: %s", g_dbProcWalk ? "the drunk body's own walk" : "the game's own walk, the sway on top");
        return true;
    }
    if (!strcmp(cmd, "dwtune")) {   // "dwtune name=value,...": the drunk body's own walk (dbody_walk.h, "dbwalk 1"); alone, lists them
        static const struct { const char* name; float* v; } kv[] = {
            {"ahead", &db::kWalkAhead}, {"upblend", &db::kWalkUpBlend}, {"rollup", &db::kWalkRollUp}, {"rollt", &db::kWalkRollT},
            {"handback", &kDbHandBack}};
        char buf[200];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        for (char* p = strtok(buf, ","); p; p = strtok(nullptr, ",")) {
            char* eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            for (auto& e : kv)
                if (!strcmp(e.name, p)) *e.v = (float)atof(eq + 1);
        }
        char line[600] = {0};
        int m = 0;
        for (auto& e : kv)
            if (m < (int)sizeof(line) - 1) m += snprintf(line + m, sizeof(line) - m, " %s=%.3f", e.name, *e.v);
        Log("dwtune:%s", line);
        return true;
    }
    if (!strcmp(cmd, "dbwalklog")) {   // "dbwalklog <n>": the walk 4 times a second for n reports - his hips, knees and stance
        g_db.walkLog = arg[0] ? atoi(arg) : 20;
        return true;
    }
    if (!strcmp(cmd, "dbfail")) {   // "dbfail -1|0|1": the push-up gives way by chance / never / always
        g_db.fall.forceFail = atoi(arg);
        Log("dbody: push-up gives way %s", g_db.fall.forceFail < 0 ? "by chance" : g_db.fall.forceFail ? "always" : "never");
        return true;
    }
    return false;
}
