// Pose trace (Weapons Lab, animation QA). Every frame: his skeleton as drawn, the weapon as drawn, the camera and the state of
// the animation -> WeaponWheel\<name>.trace, for the offline checks (tools/lab/qa/animqa.py skins his real mesh in every frame and
// measures clipping both ways, the hands on the gun, the stock on the shoulder, stretched skin, snaps and joint limits - through
// whole animations and the transitions between them, not just the poses a screenshot catches).
// A frame is written at the start of the next one (Gun_PreRender), once its last rebuild (Gun_SkinRefresh) is known to be the
// one that was drawn. Frames we don't pose (no weapon out) carry the game's own skeleton. Included in gun.cpp after shotgun.inc.
//   debug "posetrace <name>[,<seconds>]" (default 20 s; "posetrace -1" stops). Format: tools/lab/qa/trace.py
static FILE* g_ptF;
static double g_ptEnd;
static int g_ptFrames;
static uint32_t g_ptSkel = 0xFFFFFFFFu;
static uint32_t g_ptMeshHash;
static int g_ptItem = -1, g_ptDnSerial = -1;   // what the header's mesh is (0 the shotgun, 1 the donut, 2 the can) and which donut mesh
static int g_ptBrKey = -1;                      // (which can mesh: crushed how far, opened)
static bool PtDonut() { return G.held == WPN_DONUT && g_dn.have && g_dn.running; }
static bool PtBeer() { return G.held == WPN_BEER && g_br.have && g_br.running; }
static int PtBrKey() { return (int)(g_br.crush * 50.0f + 0.5f) + (g_br.opened ? 1000 : 0); }
static int PtItem() { return PtDonut() ? 1 : PtBeer() ? 2 : 0; }
extern int g_recIdx;   // (wheel.cpp) the recording's number for the frame just shown (-1: not recorded)

static double PtNow()
{
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (double)q.QuadPart / (double)f.QuadPart;
}

// (a new character - level change, costume - writes a new header: his skeleton's parents and inverse binds, the weapon's mesh)
static void PtHeader(int n)
{
    const CharShape* c = CharShapeNow();
    char name[16] = {0};
    strncpy(name, c ? c->name : "?", 15);
    int ver = 7;   // (2: 40 state values; 3: the pocket's clip plane; 4: 48 state values; 5: 56; 6: the item - 0 the shotgun, 1 the donut;
                   // 7: 64 - the drunk body's)
    fwrite("SGT1", 1, 4, g_ptF);
    fwrite(&ver, 4, 1, g_ptF);
    fwrite(&n, 4, 1, g_ptF);
    fwrite(name, 1, 16, g_ptF);
    int item = PtItem();
    fwrite(&item, 4, 1, g_ptF);
    fwrite(g_parent, 4, n, g_ptF);
    for (int j = 0; j < n; j++) fwrite(g_invBind[j].m, 4, 16, g_ptF);
    // the item's triangles (its vertices come with the frames, as drawn); the donut's change with each bite: a new header then
    const MeshBuf& m = item == 1 ? (DnEnsureMesh(), g_dnMesh) : item == 2 ? (BrEnsureMesh(), g_brMesh) : (BuildShotgun(), g_sgMesh);
    fwrite(&m.nv, 4, 1, g_ptF);
    for (int i = 0; i < m.nv; i++) {   // (the donut: its vertex colour packed as 0xRRGGBB - the QA draws it in its colours)
        const Vec3& c = m.v[i].col;
        float part = item ? (float)(((int)(Clamp01(c.x) * 255) << 16) | ((int)(Clamp01(c.y) * 255) << 8) | (int)(Clamp01(c.z) * 255))
                          : (float)m.v[i].part;
        fwrite(&part, 4, 1, g_ptF);
    }
    fwrite(&m.ni, 4, 1, g_ptF);
    fwrite(m.idx, 2, m.ni, g_ptF);
    g_ptSkel = g_invBindSkel;
    g_ptItem = item, g_ptDnSerial = g_dnMeshSerial, g_ptBrKey = PtBrKey();
    g_ptMeshHash = 0;
}

static float g_ptSnap[64];
static int g_ptSnapAt = -1;
static float DbTraceState(int i);   // (drunkbody.inc)
static void DbTraceGrounds(float* st);   // (drunkbody.inc: the ground under each hand, knee and foot, in shotgun-only slots)
static bool DbTraceGrid(float* origin, float* step, const float** grid, int* serial);   // (drunkbody.inc)
static void PtState(float* out)
{
    float st[64] = {G.show, G.aim, g_sgWA, G.sgPumpT, g_sg.pump, g_sg.k, g_sgStockLen, kSgStockDrop,
                    G.sgFlip.x, G.sgBack.x, G.sgSettle.x, G.sgTwist.x, G.kickChest.x, g_sgWink, G.camAim, G.bodyYaw,
                    G.reach.x, g_sgChinLift, g_sgPush, g_sgReachR, g_sgReachL, G.cooldown, G.speed, G.yaw,
                    G.pitch, g_sgLRollUsed, g_sgTwistR, g_sgTwistL, g_sgWristR, g_sgWristL, G.kickPitch.x, G.fovPunch.x,
                    g_dbgTimeScale, g_gunSlow, G.carryW.x, G.knockW.x, G.legW, g_sgLeftW, g_sgSwing, g_sgPull,
                    g_sgSeat, g_sgSeatPad, g_sgSeatRest, g_body.ok ? 1.0f : 0.0f, G.sgWall.x, G.sgWallA.x, g_sgSwivel[0], g_sgSwivel[1],
                    kSgPadYaw, kSgPadPitch, G.legBack.x, G.legYaw.x, g_dn.t, (float)g_dn.phase, g_dn.jaw, g_dn.bliss,
                    DbTraceState(0), DbTraceState(1), DbTraceState(2), DbTraceState(3), DbTraceState(4), DbTraceState(5),
                    DbTraceState(6), DbTraceState(7)};
    if (PtBeer()) st[52] = g_br.t, st[53] = (float)g_br.phase, st[54] = g_br.jaw, st[55] = g_br.bliss;   // (the can: in the donut's slots)
    DbTraceGrounds(st);
    memcpy(out, st, sizeof(st));
}
static void PtSnapshot()
{
    if (!g_ptF) return;
    PtState(g_ptSnap);
    g_ptSnapAt = g_buildFrame;
}

static void PoseTraceFrame()
{
    if (!g_ptF) return;
    double now = PtNow();
    if (now > g_ptEnd) {
        fclose(g_ptF);
        g_ptF = nullptr;
        Log("posetrace: done, %d frames", g_ptFrames);
        return;
    }
    // the skeleton drawn last frame: ours if we posed him, else the game's (read now: the game moves it after this call)
    static PoserPose game;
    const Mat4* Wj = g_skin.world;
    int n = g_skin.n;
    static Mat4 gw[40];
    bool ours = g_skin.active;
    if (!ours) {
        if (!g_player || !Game_ReadPoser(g_player, &game) || !LoadInvBind(game.numJoints)) return;
        n = game.numJoints < 40 ? game.numJoints : 40;
        for (int j = 0; j < n; j++) gw[j] = ToMat(game.world[j]);
        Wj = gw;
    }
    if (n < 33) return;
    if (PtBeer()) BrEnsureMesh();   // (the tossed cans' draw leaves their mesh in the can's buffer)
    if (g_invBindSkel != g_ptSkel || PtItem() != g_ptItem || (PtDonut() && (DnEnsureMesh(), g_dnMeshSerial) != g_ptDnSerial) ||
        (PtBeer() && PtBrKey() != g_ptBrKey))
        PtHeader(n);
    {   // (the drunk body's ground grid, when it's new: a GRD1 record - origin, step, 11 x 11 heights - before the frames posed on it)
        static int sGridSerial = -1;
        static FILE* sGridFile = nullptr;
        float org[3], step;
        const float* gy;
        int serial;
        if (DbTraceGrid(org, &step, &gy, &serial) && (serial != sGridSerial || sGridFile != g_ptF)) {
            sGridSerial = serial, sGridFile = g_ptF;
            fwrite("GRD1", 1, 4, g_ptF);
            fwrite(org, 4, 3, g_ptF);
            fwrite(&step, 4, 1, g_ptF);
            fwrite(gy, 4, 121, g_ptF);
        }
    }
    fwrite("FRM1", 1, 4, g_ptF);
    fwrite(&now, 8, 1, g_ptF);
    bool br = PtBeer();
    bool sg = (G.held == WPN_SHOTGUN && g_sg.have && G.show > 0.001f) || PtDonut() || br;
    int flags = (ours ? 1 : 0) | (sg ? 2 : 0) | ((PtDonut() ? g_dn.pocket : br ? g_br.pocket : g_sg.pocket) ? 4 : 0) | (G.sgHolster ? 8 : 0) |
                (G.aiming ? 16 : 0) | (g_eyeSplitAt == g_buildFrame ? 32 : 0) | (G.sgRaising ? 64 : 0) | (g_drvInCar ? 128 : 0) |
                (PtDonut() ? 256 : 0) | (br ? 512 : 0);
    int hdr[4] = {g_buildFrame, g_recIdx, G.held, flags};
    fwrite(hdr, 4, 4, g_ptF);
    // the animation's state (the reader names these: tools/lab/qa/trace.py STATE) - as the last build used it when it was a shotgun build
    // (Gun_Update runs between that build and this call: the live values were a frame ahead of the pose)
    float st[64];
    if (g_ptSnapAt == g_buildFrame) memcpy(st, g_ptSnap, sizeof(st));
    else PtState(st);
    fwrite(st, 4, 64, g_ptF);
    bool dn = PtDonut();
    Mat4 gm = dn ? g_dn.world : br ? g_br.world : Mul(ScaleM(g_sg.k > 0.0f ? g_sg.k : 1.0f), g_sg.world);
    fwrite(gm.m, 4, 16, g_ptF);
    float v[16], p[16];
    if (!Game_GetCamera(v, p)) memset(p, 0, sizeof(p));
    fwrite(ours ? g_skinView : v, 4, 16, g_ptF);   // (the view his skin was matched with; the game's camera when we didn't pose him)
    fwrite(p, 4, 16, g_ptF);
    Vec3 me = {0, 0, 0};
    PlayerPos(&me);
    float misc[12] = {G.camPos.x, G.camPos.y, G.camPos.z, G.camFwd.x, G.camFwd.y, G.camFwd.z, me.x, me.y, me.z,
                      G.aimPoint.x, G.aimPoint.y, G.aimPoint.z};
    fwrite(misc, 4, 12, g_ptF);
    float noClip[4] = {0, 1, 0, 1e6f};
    fwrite(dn ? (g_dn.pocket ? g_dn.clip : noClip) : br ? (g_br.pocket ? g_br.clip : noClip) : g_sg.clip, 4, 4, g_ptF);   // (3) the pocket's clip plane: what is "still in the pocket" (a x + b y + c z + d < 0) isn't drawn
    // the weapon's vertices as drawn (model space: the pump slid, the stock fitted), only when they changed
    static float mv[1400][3];
    Mat4 pumpOff = Translation(V3(0, 0, -g_sg.pump));
    uint32_t hsh = 2166136261u;
    static float dv[2600][3];
    int nvm = dn ? g_dnMesh.nv : br ? g_brMesh.nv : g_sgMesh.nv;
    float(*src)[3] = dn || br ? dv : mv;
    for (int i = 0; i < nvm; i++) {
        const GV& g = dn ? g_dnv[i] : br ? g_brv[i] : g_sgv[i];
        Vec3 pl = dn || br ? g.p : g.part == 1 ? XformPoint(g.p, pumpOff) : g.part == 2 ? StockPoint(g.p) : g.p;
        src[i][0] = pl.x, src[i][1] = pl.y, src[i][2] = pl.z;
    }
    const uint8_t* hb = (const uint8_t*)src;
    for (size_t b = 0; b < sizeof(float) * 3 * (size_t)nvm; b++) hsh = (hsh ^ hb[b]) * 16777619u;
    int meshNew = hsh != g_ptMeshHash ? nvm : 0;
    fwrite(&meshNew, 4, 1, g_ptF);
    if (meshNew) fwrite(src, 4, 3 * meshNew, g_ptF), g_ptMeshHash = hsh;
    // the shotgun's spent shells in flight (and on the ground): where, and the kind
    int ns = 0;
    float sh[16][4];
    for (int i = 0; i < G.nshells && ns < 16; i++)
        if (G.shells[i].kind == 1) sh[ns][0] = G.shells[i].p.x, sh[ns][1] = G.shells[i].p.y, sh[ns][2] = G.shells[i].p.z,
                                   sh[ns][3] = G.shells[i].landed ? 1.0f : 0.0f, ns++;
    fwrite(&ns, 4, 1, g_ptF);
    fwrite(sh, 4, 4 * ns, g_ptF);
    fwrite(&n, 4, 1, g_ptF);
    for (int j = 0; j < n; j++) fwrite(Wj[j].m, 4, 16, g_ptF);
    g_ptFrames++;
}

static bool PoseTraceCmd(const char* cmd, const char* arg)
{
    if (strcmp(cmd, "posetrace")) return false;
    if (g_ptF) fclose(g_ptF), g_ptF = nullptr, Log("posetrace: stopped, %d frames", g_ptFrames);
    if (arg[0] == '-' || !arg[0]) return true;
    char name[64] = {0};
    float secs = 20.0f;
    if (sscanf(arg, "%63[^,],%f", name, &secs) < 1) return true;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls%hs.trace", g_modDir, name);
    g_ptF = _wfopen(path, L"wb");
    g_ptSkel = 0xFFFFFFFFu;
    g_ptFrames = 0;
    g_ptEnd = PtNow() + secs;
    Log("posetrace: %s %s.trace for %.1f s", g_ptF ? "recording" : "could not open", name, secs);
    return true;
}
