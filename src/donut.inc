// Item 1 of the Weapons Lab: the pink sprinkled donut. Included in gun.cpp after shotgun.inc (it uses the arm solver, the
// hand curl and the pocket of the shotgun, the face's eyes and the debris particles).
//
// Picked (the Items page's slot 1, or KeyDonut), he puts away whatever he holds, takes a donut out of his right trouser pocket,
// looks at it, and eats it: four bites, each one taken out of it (the ring is cut where his teeth went, the cake shows inside)
// and chewed, then the last piece popped in whole, a gulp, two pats on his belly, and back to his idle. Nothing else - no
// health, no buff - and as many as he likes. Interrupted (another weapon, a car), the donut goes back in his pocket.
//
// The donut is ours (built here, coloured by vertex, no game files): a fat ring of golden dough with the pale band a fried
// donut has round its middle, a separate shell of pink icing over it that runs down its sides in drips, and 34 sprinkles.
// Donut space: metres at Homer's size (x k), origin in its middle, +Y out of the iced side, the ring round Y in the XZ plane
// (angle 0 = +X, +90 = +Z). His mouth (research/donut, measured on his mesh): the jaw opens about its own Z, Radical's donut
// idle takes it to 37 degrees; the mouth's opening sits under the front of his muzzle.

// ------------------------------------------------------------------ numbers
static const float kDnR = 0.036f, kDnr = 0.019f, kDnFlat = 0.82f;   // ring radius, tube radius, the tube's height share
static const float kDnIce = 0.0022f;                                   // the icing's thickness
// (homer bart lisa marge apu) the donut's size: a cartoon donut - Homer's muzzle is 24 cm across, his hand 16 cm long, and a
// real one (11 cm) looked like a mint in it. x1.9 for Homer; the others' in proportion to their hands (g_skin.handK: Bart's and
// Lisa's are half his), so the grip fitted to his hand fits theirs - then this, each one's own
static const float kDnSizeHomer = 1.90f;
static float kDnScale[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
// the mouth in the head joint's frame (+X forward-ish, -Y up): the opening's middle with the jaw open 25 deg, and which way it faces
// (research/donut/mouth.py on each one's mesh: the lips, the jaw's hinge from Radical's clips; the way it faces square to the lips'
// line, two thirds of the way back to level - Homer's, tuned on him)
static const float kDnMouth[5][3] = {{0.235f, -0.035f, 0.0f}, {0.186f, -0.080f, 0.0f}, {0.196f, -0.065f, 0.0f},
                                     {0.178f, -0.043f, -0.002f}, {0.192f, -0.042f, 0.0f}};
static const float kDnMouthFwd[5][3] = {{1.0f, 0.30f, 0.0f}, {1.0f, 0.31f, 0.0f}, {1.0f, 0.67f, 0.0f}, {1.0f, 0.34f, 0.0f},
                                        {1.0f, 0.61f, 0.0f}};
// where each one's lips meet, seen from the jaw's hinge: this far up from the head's forward (rad; research/donut/mouth.json - the hinge,
// the upper lip and the lower lip shut). The open mouth's middle - half the jaw's opening down from it - is the way into it: a bite
// goes in along it, flat in it (in level, the far side of the bitten piece went down into Homer's chin - dnqa, the chomp's frame)
static const float kDnLipsAng[5] = {0.388f, 0.746f, 0.616f, 0.538f, 0.452f};
static const float kDnLipsHalf[5] = {0.123f, 0.114f, 0.079f, 0.090f, 0.105f};   // (half the lips' width, m: mouth.json)
// how far each one's jaw opens, of Homer's (Radical's own clips: Homer 37 deg at most, Marge 28; Apu's never opens in any)
static const float kDnJawK[5] = {1.0f, 1.0f, 1.0f, 0.80f, 0.85f};
// (the bite as Radical's animators timed one - hom_idle_donut, research/donut: the jaw open to 39 deg in 6 frames as the head goes to
// the food, snapped shut in 3, the head back up over 12, then four chews 10 frames apart, 14 deg down to 7, over 1.3 s)
static float kDnJawBite = 0.62f, kDnJawChew = 0.24f, kDnJawWide = 0.62f;   // jaw open (rad): to bite, chewing's most, the last piece
static float kDnRegripLift = 0.014f;    // his hand lifted off the rim as it changes from the pocket's hold to the eating one (x size)
static const float kDnRegripLiftK[5] = {1.0f, 1.0f, 1.0f, 2.4f, 1.0f};   // (each one's: Marge's hand went 2 cm into the dough half way)
static float kDnPushMargin = 0.010f;     // the donut kept this far off his body (m, x k; < 0: off)
static float kDnBiteDepth = 0.080f;     // how far the donut's edge goes into his mouth for a bite (m, x k, along its open middle): the
                                         // piece his teeth take is behind his lips when they close on it (dnqa: 97-100 % of each piece in
                                         // his mouth as it went; at 0 - the first version - 36-50 %: half of it vanished in front of him)
static float kDnJawDough = 0.30f;       // his jaw as his teeth meet in the dough (rad): it shuts the rest of the way as the donut leaves
static float kDnBite = 1.35f;             // ring angle a bite takes (rad)
static float kDnLean = 0.20f;             // his head going to the donut for a bite (rad, forward about his neck)
// (the can's: the hand's turn shared out to his upper arm / forearm - the donut's elbow is a pure hinge since 2026-10-16, its hand
// turned on its forearm as Radical's are; the can's arm is still solved the old way)
static float kDnTwistUA = 0.15f, kDnTwistFA = 0.35f;
static float kDnPalmOff = 0.022f, kDnPalmLow = 0.30f;  // the pinch: the palm this far off the rim (x size), this far below its middle
static float kDnCurl = 0.55f;                          // the fingers' curl round it
// The grips, fitted to each one's own hand (research/donut/grip_fit.py: the hand's mesh as the game skins it, rigid at this curl, the
// donut placed so that nothing of the hand is in the dough while the thumb and fingers touch it - the thumb on the icing, the fingers
// under it, at its outer edge; the others started from Homer's, so all five hold it alike). A fist like Homer's closed round the ring
// put the dough 3 cm into it, however the fingers curled; Homer's grip scaled to Marge's and Apu's hands floated 2-4 cm off them.
// Two each: eating, and the pocket's (the forearm in the ring's plane, straight out from it: the donut hangs below the fist as it is
// drawn, the arm coming down from above - with the eating grip his wrist went 4 cm into its top edge). The wrist's frame in donut
// space at ring angle 0: rows X, Y, Z, then where (metres at Homer's size, kDnGripK).
static const float kDnGripK = 1.90f;
static const float kDnGrip[5][2][12] = {
    {{0.10779f, 0.76353f, -0.63671f, 0.65142f, -0.53805f, -0.53494f, -0.75103f, -0.35710f, -0.55538f, 0.21508f, 0.04858f, -0.08478f},   // homer
     {0.82214f, 0.27465f, 0.49865f, 0.49096f, -0.78546f, -0.37684f, 0.28817f, 0.55463f, -0.78060f, 0.25016f, -0.05277f, 0.02649f}},
    {{0.15330f, 0.76801f, -0.62182f, 0.60280f, -0.57130f, -0.55700f, -0.78303f, -0.28945f, -0.55053f, 0.20145f, 0.03120f, -0.09305f},   // bart
     {0.89885f, 0.17785f, 0.40055f, 0.35460f, -0.83221f, -0.42624f, 0.25754f, 0.52516f, -0.81110f, 0.25497f, -0.06442f, 0.01144f}},
    {{0.21016f, 0.72303f, -0.65808f, 0.68265f, -0.59037f, -0.43064f, -0.69987f, -0.35873f, -0.61765f, 0.22832f, 0.03358f, -0.06356f},   // lisa
     {0.75801f, 0.36056f, 0.54352f, 0.54006f, -0.81421f, -0.21306f, 0.36572f, 0.45503f, -0.81191f, 0.23937f, -0.05684f, 0.04755f}},
    {{-0.05671f, 0.87502f, -0.48076f, 0.68711f, -0.31514f, -0.65465f, -0.72434f, -0.36746f, -0.58336f, 0.16234f, 0.10587f, -0.07453f},   // marge
     {0.81197f, 0.20815f, 0.54532f, 0.48689f, -0.75681f, -0.43609f, 0.32194f, 0.61960f, -0.71586f, 0.22713f, -0.02576f, 0.05081f}},
    {{0.06510f, 0.91401f, -0.40042f, 0.69193f, -0.33049f, -0.64188f, -0.71903f, -0.23528f, -0.65395f, 0.17272f, 0.08451f, -0.06642f},   // apu
     {0.80621f, 0.41038f, 0.42617f, 0.53889f, -0.80666f, -0.24268f, 0.24418f, 0.42531f, -0.87148f, 0.23252f, -0.01080f, 0.03114f}}};
// ... and the fingers and thumb as they were in the frame each grip was fitted to (joints 29-32 in their parents' frames: rows X, Y,
// Z, then where): the grip is the hand's shape as well as its place. (Curled on top of the game's animation, his fingers followed his
// walk cycle as he walked and opened 1.8 cm off the dough - the donut QA.)
static const float kDnFingers[5][4][12] = {
    {{0.50092f, 0.00000f, -0.86549f, -0.86550f, 0.00004f, -0.50090f, 0.00002f, 1.00000f, 0.00003f, -0.11620f, 0.00000f, 0.00001f},
     {0.45872f, 0.88858f, -0.00001f, -0.88858f, 0.45872f, 0.00001f, 0.00002f, 0.00000f, 1.00000f, -0.04639f, -0.00064f, -0.00008f},   // homer
     {0.55407f, 0.54572f, -0.62865f, 0.74913f, -0.65620f, 0.09054f, -0.36308f, -0.52108f, -0.77243f, -0.06015f, -0.08519f, 0.00268f},
     {0.96969f, 0.23947f, -0.04861f, -0.24227f, 0.96813f, -0.06349f, 0.03186f, 0.07334f, 0.99680f, -0.05203f, -0.00912f, 0.00512f}},
    {{0.50092f, 0.00000f, -0.86549f, -0.86550f, 0.00004f, -0.50090f, 0.00002f, 1.00000f, 0.00003f, -0.05994f, -0.00000f, -0.00001f},
     {0.43397f, 0.90093f, -0.00001f, -0.90093f, 0.43397f, 0.00001f, 0.00002f, 0.00000f, 1.00000f, -0.02256f, 0.00000f, -0.00000f},   // bart
     {0.66071f, 0.75010f, 0.02830f, -0.32257f, 0.31775f, -0.89162f, -0.67780f, 0.57997f, 0.45191f, -0.02861f, -0.04244f, 0.00029f},
     {0.95683f, 0.26453f, 0.12045f, -0.25038f, 0.96060f, -0.12066f, -0.14762f, 0.08530f, 0.98536f, -0.03729f, -0.00000f, 0.00000f}},
    {{0.50092f, -0.00000f, -0.86549f, -0.86550f, 0.00004f, -0.50090f, 0.00002f, 1.00000f, 0.00003f, -0.05975f, -0.00000f, -0.00000f},
     {0.66428f, 0.74748f, -0.00002f, -0.74748f, 0.66428f, 0.00001f, 0.00002f, 0.00000f, 1.00000f, -0.02585f, -0.00000f, 0.00000f},   // lisa
     {0.89208f, 0.45046f, 0.03574f, 0.44372f, -0.88820f, 0.11925f, 0.08540f, -0.09063f, -0.99222f, -0.01091f, -0.03827f, 0.01119f},
     {0.96969f, 0.23947f, -0.04861f, -0.24227f, 0.96813f, -0.06349f, 0.03186f, 0.07334f, 0.99680f, -0.04211f, -0.00000f, 0.00000f}},
    {{0.50092f, 0.00000f, -0.86549f, -0.86550f, 0.00004f, -0.50090f, 0.00002f, 1.00000f, 0.00003f, -0.11936f, -0.00000f, -0.00000f},
     {0.60048f, 0.79964f, -0.00001f, -0.79964f, 0.60048f, 0.00001f, 0.00002f, 0.00000f, 1.00000f, -0.03725f, 0.00000f, -0.00000f},   // marge
     {0.77177f, 0.59488f, -0.22468f, 0.63037f, -0.76229f, 0.14682f, -0.08395f, -0.25499f, -0.96329f, -0.04169f, -0.02794f, 0.00885f},
     {0.96969f, 0.23947f, -0.04861f, -0.24227f, 0.96813f, -0.06349f, 0.03186f, 0.07334f, 0.99680f, -0.05946f, -0.00000f, 0.00000f}},
    {{0.50092f, 0.00000f, -0.86549f, -0.86550f, 0.00004f, -0.50090f, 0.00002f, 1.00000f, 0.00003f, -0.11528f, 0.00001f, 0.00001f},
     {0.80635f, 0.59143f, -0.00002f, -0.59143f, 0.80635f, 0.00001f, 0.00002f, 0.00000f, 1.00000f, -0.05002f, 0.00001f, -0.00001f},   // apu
     {0.79632f, 0.60129f, 0.06575f, 0.58715f, -0.79446f, 0.15524f, 0.14555f, -0.08505f, -0.98569f, -0.04049f, -0.04610f, -0.00242f},
     {0.96969f, 0.23947f, -0.04861f, -0.24227f, 0.96813f, -0.06349f, 0.03186f, 0.07334f, 0.99680f, -0.05643f, 0.00001f, 0.00001f}}};
static float kDnGripSel = 1.0f;                         // 1: the fitted grips; 0: the old pinch (built from kDnPalmOff/Low)
static int g_dnChar = 0;                                // (whose grips: set as each pose is built)
static const Vec3 kDnThumbAim = {-0.7729f, -0.6342f, 0.0216f};   // where his thumb points, in his hand's frame (as fitted)
// timing (s)
static float kDnTReach = 0.32f, kDnTGrab = 0.12f, kDnTPull = 0.60f, kDnTAdmire = 0.55f;
static float kDnTIn = 0.32f, kDnTChomp = 0.10f, kDnTOut = 0.30f, kDnTChew = 1.25f, kDnTSwallow = 0.30f;
static float kDnTPop = 0.30f, kDnTFinal = 1.25f, kDnTGulp = 0.30f, kDnTBack = 0.45f;
static float kDnTAbort = 0.55f;           // put back in the pocket when he's interrupted
static const int kDnBites = 3;            // regular bites before the last piece goes in whole

// ------------------------------------------------------------------ state
static struct {
    bool running, aborting, done;
    float t, abortT;               // seconds into the sequence / into putting it back
    float a0, a1;                  // what is left of the ring (rad): a0 the bitten end
    int bites;                     // bites taken
    bool gone;                     // the last piece is in his mouth
    bool meshDirty;
    Mat4 world;                    // the donut as drawn (scaled donut space -> world)
    bool have, pocket;             // drawn at all; the pocket's clip plane on
    float clip[4];
    float k;                       // its size
    Mat4 head;                     // his posed head (the eyes' lids)
    float jaw, bliss;              // (report / trace) jaw opening, eyes shut
    Vec3 mouthW;                   // (report / trace) his mouth's point in the world
    int phase;                     // (report) which part of the sequence
    float lastT;                   // (events) the timeline's last time
    float push, tip;               // (QA) how far the donut was moved out of his body / tipped off his arm this frame (m, rad)
    Vec3 pushV;                    // (QA) that move
    float pushMs;                  // (QA) what working it out cost this frame (ms)
    float phi;                     // (report) his elbow's swivel this frame (rad)
} g_dn;
static float g_dnFreeze = -1.0f;   // debug "dnfreeze <s>": hold the sequence at that time (-1 off)
static bool Dn_Running() { return g_dn.running; }

// ------------------------------------------------------------------ the model
static GV g_dnv[2600];
static uint16_t g_dni[7200];
static MeshBuf g_dnMesh = {g_dnv, g_dni, 0, 0, 2600, 7200};
static int g_dnMeshSerial;   // (the pose trace: a new donut mesh, a new header)
struct DnSprinkle { float th, ph, ang, len; int col; };
static DnSprinkle g_dnSprinkles[34];
static const Vec3 kDnSprinkleCol[6] = {{0.95f, 0.18f, 0.20f}, {1.0f, 0.86f, 0.12f}, {0.20f, 0.48f, 0.95f},
                                       {0.25f, 0.78f, 0.30f}, {1.0f, 0.98f, 0.94f}, {1.0f, 0.55f, 0.10f}};
static const Vec3 kDnDough = {0.80f, 0.52f, 0.22f}, kDnDoughD = {0.62f, 0.38f, 0.15f}, kDnBand = {0.96f, 0.82f, 0.55f};
static const Vec3 kDnPink = {1.0f, 0.44f, 0.68f}, kDnPinkL = {1.0f, 0.62f, 0.80f}, kDnPinkD = {0.86f, 0.30f, 0.52f};
static const Vec3 kDnCake = {0.99f, 0.90f, 0.66f}, kDnCakeD = {0.92f, 0.78f, 0.50f};

static float DnRand(uint32_t& s) { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; }

// where the icing runs down to on the outside / the inside of the ring (tube angle, rad: 0 the outer equator, pi the inner)
static float DnIceOut(float th) { return -0.20f + 0.16f * sinf(5.0f * th + 1.3f) + 0.07f * sinf(13.0f * th + 0.4f); }
static float DnIceIn(float th) { return 3.14159f + 0.10f - 0.12f * sinf(4.0f * th + 0.4f) - 0.05f * sinf(9.0f * th + 2.0f); }

// a point of the ring's surface (tube offset e: the icing's shell) and its normal
static Vec3 DnPoint(float th, float ph, float e) { float rr = kDnR + (kDnr + e) * cosf(ph); return {rr * cosf(th), (kDnr + e) * kDnFlat * sinf(ph), rr * sinf(th)}; }
static Vec3 DnNormal(float th, float ph) { return Norm(V3(cosf(ph) * cosf(th), sinf(ph) / kDnFlat, cosf(ph) * sinf(th))); }

// A bitten end: where along the ring the surface ends, for each angle round the tube - the teeth's marks (a scalloped edge)
// and an end that is deeper on the outside of the ring, where a mouth takes the most. dir +1: the end at a0 (the piece goes on
// to larger angles), -1: at a1.
static float DnEndAt(float a, int dir, float ph)
{
    float teeth = 0.035f * sinf(ph * 5.0f + 0.6f) + 0.015f * sinf(ph * 11.0f);
    float outer = 0.10f * (0.5f + 0.5f * cosf(ph));
    return a + dir * (outer + teeth);
}

static void DnVert(Vec3 p, Vec3 n, Vec3 col, float spec)
{
    MeshBuf& m = *g_mb;
    if (m.nv < m.maxv) m.v[m.nv++] = {p, n, col, spec, 0};
}
static void DnTri(int a, int b, int c)
{
    MeshBuf& m = *g_mb;
    if (m.ni + 3 > m.maxi) return;
    m.idx[m.ni++] = (uint16_t)a, m.idx[m.ni++] = (uint16_t)b, m.idx[m.ni++] = (uint16_t)c;
}

// the ring from a0 to a1 (whole: a1 - a0 >= 2 pi), its two bitten ends capped with the cake inside
static void BuildDonutMesh(float a0, float a1)
{
    static bool seeded;
    if (!seeded) {   // the sprinkles: the same donut every time
        seeded = true;
        uint32_t s = 0xD0A7u;
        for (auto& sp : g_dnSprinkles) {
            sp.th = DnRand(s) * 6.2832f;
            float lo = DnIceOut(sp.th) + 0.30f, hi = DnIceIn(sp.th) - 0.30f;
            sp.ph = lo + (hi - lo) * DnRand(s);
            sp.ang = DnRand(s) * 3.1416f;
            sp.len = 0.0030f + 0.0012f * DnRand(s);
            sp.col = (int)(DnRand(s) * 5.99f);
        }
    }
    g_mb = &g_dnMesh;
    g_dnMesh.nv = g_dnMesh.ni = 0;
    bool whole = a1 - a0 >= 6.2830f;
    const int M = 12;                                    // round the tube
    int N = (int)ceilf((a1 - a0) / (6.2832f / 28.0f));   // along the ring
    if (N < 3) N = 3;
    auto thAt = [&](int i, float ph) {   // the ring angle of row i at tube angle ph (the ends bitten)
        float u = (float)i / N;
        if (whole) return a0 + (a1 - a0) * u;
        float s = DnEndAt(a0, 1, ph), e = DnEndAt(a1, -1, ph);
        return s + (e - s) * u;
    };
    // the dough: the whole tube, golden, darker underneath, the pale fried band round its outside
    int base = g_dnMesh.nv;
    for (int i = 0; i <= N; i++)
        for (int j = 0; j <= M; j++) {
            float ph = -1.5708f + 6.2832f * j / M;   // (from underneath, round the outside, over the top, down the inside)
            float th = thAt(i, ph);
            float band = Clamp01(1.0f - fabsf(ph + 0.12f) / 0.30f);
            Vec3 col = Lerp(Lerp(kDnDoughD, kDnDough, Clamp01(0.5f + 0.6f * sinf(ph))), kDnBand, band * 0.85f);
            DnVert(DnPoint(th, ph, 0), DnNormal(th, ph), col, 0.15f);
        }
    for (int i = 0; i < N; i++)
        for (int j = 0; j < M; j++) {
            int a = base + i * (M + 1) + j, b = a + M + 1;
            DnTri(a, b, b + 1), DnTri(a, b + 1, a + 1);
        }
    // the icing: a shell from its edge on the outside, over the top, to its edge on the inside, with a lip down to the dough
    const int K = 9;
    int ibase = g_dnMesh.nv;
    for (int i = 0; i <= N; i++) {
        float th0 = thAt(i, 1.0f);
        float lo = DnIceOut(th0), hi = DnIceIn(th0);
        // (the lip: from the dough's surface up to the shell at its outer edge, then the shell, then the lip at the inner edge)
        for (int j = -1; j <= K + 1; j++) {
            float ph = j < 0 ? lo : j > K ? hi : lo + (hi - lo) * j / K;
            float th = thAt(i, ph);
            float e = (j < 0 || j > K) ? 0.0002f : kDnIce;
            float topness = Clamp01(sinf(ph));
            Vec3 col = (j < 0 || j > K) ? kDnPinkD : Lerp(kDnPink, kDnPinkL, topness * topness * 0.55f);
            DnVert(DnPoint(th, ph, e), DnNormal(th, ph), col, 0.85f);
        }
    }
    const int KR = K + 3;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < KR - 1; j++) {
            int a = ibase + i * KR + j, b = a + KR;
            DnTri(a, b, b + 1), DnTri(a, b + 1, a + 1);
        }
    // the bitten ends: a fan of cake, its middle pushed in (a mouthful's hollow), the icing's edge round its top
    if (!whole) {
        for (int end = 0; end < 2; end++) {
            int dir = end == 0 ? 1 : -1;
            int ring = end == 0 ? 0 : N;
            Vec3 c = {0, 0, 0};
            for (int j = 0; j < M; j++) c = c + g_dnv[base + ring * (M + 1) + j].p;
            c = c * (1.0f / M);
            float ta = end == 0 ? a0 : a1;
            Vec3 tang = V3(-sinf(ta), 0, cosf(ta)) * (float)dir;   // into the piece
            c = c + tang * (0.45f * kDnr);
            int cb = g_dnMesh.nv;
            DnVert(c, tang * -1.0f, kDnCakeD, 0.05f);
            for (int j = 0; j <= M; j++) {
                const GV& d = g_dnv[base + ring * (M + 1) + j];
                float ph = -1.5708f + 6.2832f * j / M;
                bool iced = ph > DnIceOut(ta) && ph < DnIceIn(ta) && sinf(ph) > -0.2f;
                Vec3 n = Norm(tang * -1.0f + Norm(d.p - c) * 0.35f);
                DnVert(d.p + (iced ? Norm(DnNormal(ta, ph)) * kDnIce : V3(0, 0, 0)), n, iced ? kDnPinkD : kDnCake, 0.05f);
            }
            for (int j = 0; j < M; j++) DnTri(cb, cb + 1 + j, cb + 2 + j);
        }
    }
    // the sprinkles: little rods lying on the icing (not on what was eaten, nor right at a bitten edge)
    for (const auto& sp : g_dnSprinkles) {
        float th = sp.th;
        if (!whole) {
            float t = th;
            while (t < a0) t += 6.2832f;
            while (t > a0 + 6.2832f) t -= 6.2832f;
            if (t < a0 + 0.10f || t > a1 - 0.10f) continue;
            th = t;
        }
        Vec3 p = DnPoint(th, sp.ph, kDnIce + 0.0007f), n = DnNormal(th, sp.ph);
        Vec3 t1 = Norm(V3(-sinf(th), 0, cosf(th))), t2 = Norm(Cross(n, t1));
        Vec3 ax = Norm(t1 * cosf(sp.ang) + t2 * sinf(sp.ang)), side = Norm(Cross(n, ax));
        Vec3 col = kDnSprinkleCol[sp.col];
        const float w = 0.00075f;
        int sb = g_dnMesh.nv;
        for (int q = 0; q < 8; q++) {   // a box: 4 at the bottom, 4 on top
            float sa = (q & 1) ? 1.0f : -1.0f, sb2 = (q & 2) ? 1.0f : -1.0f, up = (q & 4) ? 1.0f : 0.0f;
            Vec3 v = p + ax * (sa * sp.len) + side * (sb2 * w) + n * (up * 2.0f * w);
            DnVert(v, Norm(n * 0.8f + side * sb2 * 0.6f), col, 0.6f);
        }
        static const int f[5][4] = {{4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
        for (auto& q : f) DnTri(sb + q[0], sb + q[1], sb + q[2]), DnTri(sb + q[0], sb + q[2], sb + q[3]);
    }
    g_mb = &g_gunMesh;
}

// ------------------------------------------------------------------ the sequence
// segments, in order; each a pose to get to and how
// (DN_SWALLOW comes after each bite's chewing; numbered last so the traces' phase numbers before it stay as they were)
enum DnPhase { DN_REACH, DN_GRAB, DN_PULL, DN_ADMIRE, DN_IN, DN_CHOMP, DN_OUT, DN_CHEW, DN_POP, DN_FINAL, DN_GULP, DN_BACK, DN_SWALLOW, DN_END };
static float DnBiteCycle() { return kDnTIn + kDnTChomp + kDnTOut + kDnTChew + kDnTSwallow; }
static float DnIntro() { return kDnTReach + kDnTGrab + kDnTPull + kDnTAdmire; }
static float DnTotal() { return DnIntro() + kDnBites * DnBiteCycle() + kDnTPop + kDnTFinal + kDnTGulp + kDnTBack; }

struct DnAt { int phase, bite; float u, t; };   // which segment, which bite (0..), how far through it (0..1), its time
static DnAt DnPhaseAt(float t)
{
    DnAt a = {DN_END, kDnBites, 1.0f, 0};
    float s = 0;
    auto seg = [&](float dur, int ph, int bite) {
        if (t < s + dur && a.phase == DN_END && t >= s) a = {ph, bite, dur > 0 ? (t - s) / dur : 1.0f, t - s};
        s += dur;
    };
    seg(kDnTReach, DN_REACH, 0), seg(kDnTGrab, DN_GRAB, 0), seg(kDnTPull, DN_PULL, 0), seg(kDnTAdmire, DN_ADMIRE, 0);
    for (int b = 0; b < kDnBites; b++)
        seg(kDnTIn, DN_IN, b), seg(kDnTChomp, DN_CHOMP, b), seg(kDnTOut, DN_OUT, b), seg(kDnTChew, DN_CHEW, b), seg(kDnTSwallow, DN_SWALLOW, b);
    seg(kDnTPop, DN_POP, kDnBites), seg(kDnTFinal, DN_FINAL, kDnBites), seg(kDnTGulp, DN_GULP, kDnBites), seg(kDnTBack, DN_BACK, kDnBites);
    return a;
}
// the ring left after n bites (a0 the bitten end; the first bite cuts it at 0)
static void DnArcAfter(int n, float* a0, float* a1)
{
    if (n <= 0) { *a0 = 0, *a1 = 6.2832f; return; }
    *a0 = 0.5f * kDnBite + (n - 1) * kDnBite;
    *a1 = 6.2832f - 0.5f * kDnBite;
}
// where bite n is taken (ring angle): the first in the middle of the near side, then just inside the bitten end
static float DnBiteAngle(int n)
{
    if (n <= 0) return 0.0f;
    float a0, a1;
    DnArcAfter(n, &a0, &a1);
    return a0 + 0.35f * kDnBite;
}

// The hand on the donut: his palm against its rim from outside at ring angle g, fingers round the rim the way the angle grows,
// thumb over the top (the hand upright, as it hangs - the least turn for his forearm). -> the wrist's frame in donut space.
// (pocket: the pocket's grip, else the eating one)
static Mat4 DnGripRel(bool pocket, float g, float handLen, float kk);
static Mat4 DnGripRel(bool pocket, float g, float handLen, float kk)
{
    if (kDnGripSel >= 0.5f) {
        const float* q = kDnGrip[g_dnChar][pocket ? 1 : 0];
        float c = cosf(g), s = sinf(g), sc = kk / kDnGripK;
        Mat4 h = WithTrans(Basis(V3(q[0], q[1], q[2]), V3(q[3], q[4], q[5]), V3(q[6], q[7], q[8])), V3(q[9], q[10], q[11]) * sc);
        return Mul(h, Basis(V3(c, 0, s), V3(0, 1, 0), V3(-s, 0, c)));   // (turned round the ring to angle g)
    }
    Vec3 out = V3(cosf(g), 0, sinf(g)), tang = V3(-sinf(g), 0, cosf(g));
    Vec3 F = Norm(tang * 0.92f - out * 0.25f + V3(0, -0.12f, 0)), P = out * -1.0f;   // fingers, palm (facing the donut)
    P = Norm(P - F * Dot(P, F));
    Vec3 X = F * -1.0f, Z = P, Y = Cross(Z, X);
    // (a pinch: the palm just off the rim and a little below its middle - the fingers under the dough, the thumb on the icing;
    // closer, his mitten of a hand closed round the ring and the dough went 3.4 cm into his fist - the donut QA)
    Vec3 palm = out * (kDnR + kDnr + kDnPalmOff) * kk + V3(0, -kDnPalmLow * kDnr, 0) * kk;
    Vec3 wrist = palm - F * (0.36f * handLen) - P * (0.016f * kk);
    return WithTrans(Basis(X, Y, Z), wrist);
}

// a donut frame from where its bite point (ring angle th, donut space) goes and which way it faces: its point at angle th
// turned to `toward`, its iced side to `upish`
static Mat4 DnFrameAt(float th, Vec3 toward, Vec3 upish, Vec3 pointW, float kk)
{
    Vec3 A = Norm(upish), U = Norm(toward - A * Dot(toward, A)), C = Cross(U, A);
    Vec3 e1 = V3(cosf(th), 0, sinf(th)), e2 = V3(0, 1, 0), e3 = Cross(e1, e2);
    Mat4 R = Mul(Transpose3(Basis(e1, e2, e3)), Basis(U, A, C));
    Vec3 bl = (e1 * (kDnR + kDnr * 0.55f) + V3(0, kDnr * 0.15f, 0)) * kk;
    return WithTrans(R, pointW - XformDir(bl, R));
}

// a hand frame blended (position and turn)
static Mat4 DnBlendFrame(const Mat4& a, const Mat4& b, float u)
{
    return WithTrans(BlendRot(a, b, u), Lerp(Row(a, 3), Row(b, 3), u));
}

static float DnEase(float u) { u = Clamp01(u); return u * u * (3 - 2 * u); }
static float DnEaseOut(float u) { u = Clamp01(u); return 1 - (1 - u) * (1 - u) * (1 - u); }

// ------------------------------------------------------------------ per frame (Gun_Update)
static int g_dnStarts;   // (donuts started: what is worked out once a donut)
static void DnStart()
{
    g_dnStarts++;
    g_dn.running = true, g_dn.aborting = g_dn.done = false;
    g_dn.t = 0, g_dn.abortT = 0, g_dn.lastT = 0;
    g_dn.bites = 0, g_dn.gone = false;
    DnArcAfter(0, &g_dn.a0, &g_dn.a1);
    g_dn.meshDirty = true;
    Log("donut: out of his pocket");
}
static void DnCrumbs(Vec3 p, Vec3 fwd, int n)
{
    for (int i = 0; i < n && G.nchips < 64; i++) {
        Chip& c = G.chips[G.nchips++];
        float r = Rnd();
        Vec3 col = r < 0.45f ? kDnCake : r < 0.75f ? kDnDough : r < 0.88f ? kDnPink : kDnSprinkleCol[(int)(Rnd() * 5.99f)];
        Vec3 v = fwd * (0.25f + 0.5f * Rnd()) + V3(Rnd() - 0.5f, 0.3f + 0.6f * Rnd(), Rnd() - 0.5f) * 0.7f;
        c = {p + V3(Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f) * 0.02f, v, 0, 1.1f + 0.7f * Rnd(), Rnd() * 6.3f, (Rnd() - 0.5f) * 12.0f,
             0.004f + 0.005f * Rnd(), col};
    }
}
static void DnUpdate(float dt, bool onFoot)
{
    if (G.held != WPN_DONUT) {
        g_dn.running = false, g_dn.done = false;
        g_dnBliss = 0;
        return;
    }
    if (!g_dn.running && G.weapon != WPN_DONUT) g_dn.done = false;   // (eaten or put away: the next pick is a new donut)
    if (!g_dn.running && !g_dn.done && G.weapon == WPN_DONUT) DnStart();
    if (!g_dn.running) return;
    if (!onFoot) {   // (into a car, a cutscene: gone at once)
        g_dn.running = false, g_dn.done = true;
        G.show = 0;
        if (G.weapon == WPN_DONUT) G.weapon = WPN_NONE;
        return;
    }
    if (G.weapon != WPN_DONUT && !g_dn.aborting) {   // another weapon picked: back in the pocket first
        g_dn.aborting = true, g_dn.abortT = 0;
        Log("donut: put away (t %.2f)", g_dn.t);
    }
    if (g_dn.aborting) {
        g_dn.abortT += dt;
        if (g_dn.abortT >= kDnTAbort + 0.25f) {
            g_dn.running = false, g_dn.done = true;
            G.show = 0;
            if (G.weapon == WPN_DONUT) g_dn.done = false;   // (picked again as it went away: a fresh one next frame)
        }
        return;
    }
    if (g_dnFreeze >= 0.0f) g_dn.t = g_dnFreeze;
    else g_dn.t += dt;
    float t = g_dn.t, t0 = g_dn.lastT;
    g_dn.lastT = t;
    // the moments: the hand closing on it in the pocket, each bite (the jaw snapping shut), the last piece in, the gulp, the pats
    float s = kDnTReach + kDnTGrab * 0.6f;
    if (t0 < s && t >= s) Sound_Play(SND_EQUIP, 0.35f, 1.6f);
    for (int b = 0; b < kDnBites; b++) {
        float bt = DnIntro() + b * DnBiteCycle() + kDnTIn + kDnTChomp * 0.70f;   // (as his teeth meet: the piece is in his mouth)
        if (t0 < bt && t >= bt) {
            g_dn.bites = b + 1;
            DnArcAfter(g_dn.bites, &g_dn.a0, &g_dn.a1);
            g_dn.meshDirty = true;
            Sound_Play(SND_BITE, 0.9f, 0.95f + 0.1f * Rnd());
            DnCrumbs(g_dn.mouthW, Norm(Row(g_dn.head, 0)), 7);
        }
        float c0 = DnIntro() + b * DnBiteCycle() + kDnTIn + kDnTChomp + kDnTOut;   // chewing: a soft sound as each chew closes
        for (int i = 0; i < 4; i++) {
            float ct = c0 + (i + 0.95f) * (kDnTChew / 4.0f);
            if (t0 < ct && t >= ct) Sound_Play(SND_CHEW, 0.55f - 0.06f * i, 0.9f + 0.2f * Rnd());
        }
        float sw = c0 + kDnTChew + kDnTSwallow * 0.45f;   // and down it goes
        if (t0 < sw && t >= sw) Sound_Play(SND_GULP, 0.45f, 1.15f + 0.1f * Rnd());
    }
    float pt = DnIntro() + kDnBites * DnBiteCycle() + kDnTPop * 0.75f;
    if (t0 < pt && t >= pt) {
        g_dn.gone = true;
        Sound_Play(SND_BITE, 0.8f, 0.85f);
        DnCrumbs(g_dn.mouthW, Norm(Row(g_dn.head, 0)), 5);
    }
    for (int i = 0; i < 4; i++) {
        float ct = pt + 0.25f + i * 0.26f;
        if (t0 < ct && t >= ct) Sound_Play(SND_CHEW, 0.65f, 0.85f + 0.15f * Rnd());
    }
    float gt = DnIntro() + kDnBites * DnBiteCycle() + kDnTPop + kDnTFinal + kDnTGulp * 0.3f;
    if (t0 < gt && t >= gt) Sound_Play(SND_GULP, 0.9f, 1.0f);
    for (int i = 0; i < 2; i++) {
        float pat = DnIntro() + kDnBites * DnBiteCycle() + kDnTPop + 0.55f + i * 0.30f;
        if (t0 < pat && t >= pat) Sound_Play(SND_PAT, 0.7f, 1.0f + 0.08f * i);
    }
    if (t >= DnTotal()) {   // finished: back to his idle, ready for the next
        g_dn.running = false, g_dn.done = true;
        G.show = 0;
        if (G.weapon == WPN_DONUT) G.weapon = WPN_NONE;
        Log("donut: eaten (%.1f s)", t);
    }
}

// how far the donut (frame D, as drawn at size kk) must move to be `margin` clear of his skin (the parts in `mask`): points round
// what is left of its ring (outer rim, top, bottom), those still in his pocket (below hideY: not drawn) skipped. Each round moves it
// out along the deepest point's way out of him. (Straight from his pocket to his face it came up through his belly; standing in his
// pocket, through his hip - the donut QA.)
static int g_dnPushLog = 0;      // debug "dnpushlog 1": every frame's push and its cost
static int g_dnPushPart = -1;    // (debug: the joint whose skin the deepest point was nearest, and where it was)
static Vec3 g_dnPushAt;
// (while he bites: the inside of his open mouth - behind his lips, within their width and the open jaw's height - is where the donut is
// meant to be; what of it is there isn't pushed. Apu's and Marge's chest skin comes up to their chin: the donut's far side, deep in
// the mouth, read as in their chest and was shoved 7.7 cm down and out of the mouth - into the side of it, a jump each bite)
struct DnTunnel { Vec3 o, f, u, r; float hw, hh; };
static bool DnInTunnel(const DnTunnel* t, Vec3 p)
{
    if (!t) return false;
    Vec3 v = p - t->o;
    return Dot(v, t->f) < 0.01f && fabsf(Dot(v, t->r)) < t->hw && fabsf(Dot(v, t->u)) < t->hh;
}
static Vec3 DnPushOut(const Mat4& D, float kk, float k, float margin, uint64_t mask, float hideY, float a0, float a1,
                      const DnTunnel* tunnel = nullptr)
{
    // (16 round it, the inner rim too: at 12, rim, top and bottom only, his snout came 1.4 cm in between them - the donut QA)
    const int NA = 16;
    Vec3 push = {0, 0, 0};
    for (int it = 0; it < 3; it++) {
        float worst = margin;
        Vec3 wdir = {0, 0, 0};
        for (int i = 0; i < NA; i++) {
            float a = (i + 0.5f) * 6.2832f / NA;
            if (a < a0 || a > a1) continue;   // (bitten away)
            Vec3 e = V3(cosf(a), 0, sinf(a));
            Vec3 pts[4] = {e * (kDnR + kDnr), e * kDnR + V3(0, kDnr * kDnFlat, 0), e * kDnR - V3(0, kDnr * kDnFlat, 0), e * (kDnR - kDnr)};
            for (const Vec3& q : pts) {
                Vec3 p = XformPoint(q * kk, D) + push;
                if (p.y < hideY || DnInTunnel(tunnel, p)) continue;
                Vec3 o;
                float d = BodyDist(p, mask, 0.12f * k, &o, nullptr, true);
                if (d < worst) {
                    worst = d, wdir = o;
                    g_dnPushPart = g_bodyLastT >= 0 ? g_body.tdom[g_bodyLastT] : -1, g_dnPushAt = p;
                }
            }
        }
        if (worst >= margin) break;
        push = push + wdir * (margin - worst);
    }
    float L = Len(push), cap = 0.12f * k;   // (a held donut is never more than a few cm in him: more is a wrong answer, not a fix)
    return L > cap ? push * (cap / L) : push;
}

// how far p is outside his right arm as solved, the arm as two capsules round its bones (Homer's mesh: the upper arm and sleeve
// ~10 cm round the bone, the forearm 8.5 cm at the elbow; the forearm's last 40 % is his hand's, left out: the grip). (His own skin
// as a mesh was no use here: the arm alone is open at both ends, and inside or out flipped by his wrist.) -> distance, way out
static float DnArmDist(Vec3 p, const Mat4* W, float k, Vec3* out)
{
    Vec3 sh = Row(W[26], 3), el = Row(W[27], 3), wr = Row(W[28], 3);
    float best = 1e9f;
    for (int s = 0; s < 2; s++) {
        Vec3 a = s ? el : sh, b = s ? el + (wr - el) * 0.6f : el;
        Vec3 ab = b - a;
        float t = Clamp01(Dot(p - a, ab) / fmaxf(1e-8f, Dot(ab, ab)));
        Vec3 q = a + ab * t, d = p - q;
        float r = (s ? 0.085f - 0.02f * t : 0.105f) * k;
        float dist = Len(d) - r;
        if (dist < best) best = dist, *out = Len(d) > 1e-6f ? d * (1.0f / Len(d)) : V3(0, 1, 0);
    }
    return best;
}

// the donut tipped in his fist (turned about the point his hand holds, `hold` in donut space) until none of it is within `margin` of
// his own arm as solved: lifted beside him, his forearm lay across its top edge (the donut QA). Each round turns it by the smallest
// angle that brings its deepest point out.
static Mat4 DnRotPush(const Mat4& D, const Mat4* W, float kk, float k, Vec3 hold, float margin, float hideY, float a0, float a1,
                      float* turned)
{
    const int NA = 12;
    Mat4 G = D;
    float total = 0.0f;
    for (int it = 0; it < 4; it++) {
        Vec3 piv = XformPoint(hold * kk, G);
        float worst = margin;
        Vec3 wp = piv, wdir = {0, 0, 0};
        for (int i = 0; i < NA; i++) {
            float a = (i + 0.5f) * 6.2832f / NA;
            if (a < a0 || a > a1) continue;
            Vec3 e = V3(cosf(a), 0, sinf(a));
            Vec3 pts[4] = {e * (kDnR + kDnr), e * kDnR + V3(0, kDnr * kDnFlat, 0), e * kDnR - V3(0, kDnr * kDnFlat, 0), e * (kDnR - kDnr)};
            for (const Vec3& q : pts) {
                Vec3 p = XformPoint(q * kk, G);
                if (Len(p - piv) < 0.05f * k || p.y < hideY) continue;   // (in his fist; still in the pocket)
                Vec3 o;
                float d = DnArmDist(p, W, k, &o);
                if (d < worst) worst = d, wp = p, wdir = o;
            }
        }
        if (worst >= margin) break;
        Vec3 r = wp - piv, mv = wdir * (margin - worst + 0.002f * k);
        Vec3 ax = Cross(r, mv);
        float L = Len(ax), rl = Len(r);
        if (L < 1e-7f || rl < 1e-4f) break;
        float ang = fminf(0.30f, Len(mv) / rl);
        Mat4 R = AxisAngle(ax * (1.0f / L), ang);
        G = WithTrans(Mul(Rot3(G), R), piv + XformDir(Row(G, 3) - piv, R));
        total += ang;
    }
    if (turned) *turned = total;
    return G;
}

// ------------------------------------------------------------------ his right arm as Radical's arms are (2026-10-16)
// Review: "his right arm appears inverted or incorrectly rotated, his wrist bends unnaturally, and his arm passes through his belly".
// The arm QA (tools/lab/qa/armqa.py: every frame against Radical's own clips) found why. The donut was placed first - flat in his
// mouth, held at its rim by the grip fitted to his hand - and the arm solved to the hand that left; no arm gives that hand at his mouth.
// The solver shared the hand's turn out to his forearm (35 %) and upper arm (15 %): his elbow 40-58 deg off its hinge (Radical's elbows
// are pure hinges - 0.002 deg in 3844 frames); the rest stayed in his wrist, bent up to 160 deg (Radical's 99th percentile: 85).
// Pulled out of his pocket, his forearm went 22 cm through his belly; patting it, his hand sank 8-10 cm in and his arm locked straight.
// Now the arm leads (research/donut: dnarm.py, pose_design.py, bite_search.py). The elbow is a pure hinge. The poses about his body are
// built from the arm - his wrist where it goes, his elbow at a natural swivel, the hand on the forearm turned as Radical's wrists turn -
// and the donut follows his hand by the grip (rolled round the dough's tube to suit). At his mouth the donut is tipped and held from
// below, his head turned and bowed to it, as Radical's Homer brings his chin to his hand (hom_dialogue_thinking): fitted to his arm.

// the hand's bind axes in his forearm's frame (research/anim armstats: the hand's axis, the palm's normal - down in the bind T-pose -
// and the thumb's way - forward), and the wrist's bind turn on the forearm
static struct { Vec3 h, n0, t0; Mat4 R0; bool ok; } g_dnRest;
static void DnRestFromBind()
{
    Mat4 Be = Rot3(FullInverse(g_invBind[27])), Bw = Rot3(FullInverse(g_invBind[28]));
    Vec3 m = Row(FullInverse(g_invBind[29]), 3) - Row(FullInverse(g_invBind[28]), 3);
    for (int i = 0; i < 3; i++) SetRow(Be, i, Norm(Row(Be, i)), 0), SetRow(Bw, i, Norm(Row(Bw, i)), 0);
    Mat4 BeT = Transpose3(Be);
    Vec3 h = Norm(XformDir(Norm(m), BeT)), n0 = XformDir(V3(0, -1, 0), BeT), t0 = XformDir(V3(0, 0, -1), BeT);
    n0 = Norm(n0 - h * Dot(n0, h));
    t0 = Norm(t0 - h * Dot(t0, h) - n0 * Dot(t0, n0));
    g_dnRest.h = h, g_dnRest.n0 = n0, g_dnRest.t0 = t0, g_dnRest.R0 = Mul(Bw, BeT), g_dnRest.ok = true;
}
// the wrist's turn on the forearm for a hand swung `flex` deg toward the palm and `radial` toward the thumb and twisted `twist` (+ the
// thumb toward the palm: pronation) - the way research/anim measures Radical's wrists (dnarm.wrist_local)
static Mat4 DnWristLocal(float flex, float radial, float twist)
{
    const float D2R = 0.0174533f;
    Vec3 h = g_dnRest.h, n0 = g_dnRest.n0, t0 = g_dnRest.t0;
    Vec3 h1 = Norm(h + n0 * tanf(flex * D2R) + t0 * tanf(radial * D2R)), ax = Cross(h, h1);
    float s = Len(ax);
    Mat4 Sw = s < 1e-6f ? Identity() : AxisAngle(ax * (1.0f / s), atan2f(s, Dot(h, h1)));
    float sgn = Dot(Cross(h, t0), n0) >= 0.0f ? 1.0f : -1.0f;
    return Mul(g_dnRest.R0, Mul(AxisAngle(h, sgn * twist * D2R), Sw));
}
// his hand for a wrist place Wp and an elbow swivel phi (rad): the arm to it (a pure hinge, shotgun.inc SgArmAt), the hand on the
// forearm turned so (deg)
static Mat4 DnHandNat(Vec3 sR, Vec3 Wp, float phi, float flex, float radial, float twist, Vec3 sUp, Vec3 sRt, float a, float b)
{
    Mat4 Rua, Rfa;
    SgArmAt(sR, Wp, a, b, true, sUp, sRt, phi, &Rua, &Rfa);
    Vec3 u = Wp - sR;
    float d = Clampf(Len(u), fabsf(a - b) * 1.05f + 0.01f, (a + b) * 0.995f);
    return WithTrans(Mul(DnWristLocal(flex, radial, twist), Rfa), sR + Norm(u) * d);
}
// his chest's lean from how it stands sober (round 3): the poses about his body are fitted on him standing sober - drunk, his chest
// pitches and rolls 5-10 deg off that (the drunk body) and held to the world his arm went 3-5 cm into his side and belly (the arm QA,
// a donut and a can at 1.1). The poses lean with his chest. (Sober chest pitch, deg forward - homer bart lisa marge apu - measured on
// the item takes, 21-23 / 27 / 26-28 / 15-18 / 24-28 through his idle)
static const float kItemChestPitch[5] = {22.0f, 26.8f, 26.7f, 16.6f, 25.8f};
static Mat4 ItemLean(int ci, Vec3 fB, Vec3 sUp)
{
    float p = kItemChestPitch[ci >= 0 && ci < 5 ? ci : 0] * 0.0174533f;
    Vec3 up0 = Norm(fB * sinf(p) + UP * cosf(p));
    Vec3 ax = Cross(up0, sUp);
    float s = Len(ax), c = Dot(up0, sUp);
    if (s < 1e-5f) return Identity();
    float a = fminf(atan2f(s, c), 0.44f);   // (25 deg at most)
    Mat4 R = AxisAngle(ax * (1.0f / s), a), R2 = AxisAngle(ax * (1.0f / s), -a);
    return Dot(XformDir(up0, R), sUp) >= Dot(XformDir(up0, R2), sUp) ? R : R2;
}
// a pose about his body, arm first: the wrist from his right shoulder (m x his size: his body's forward, up, right), the elbow's swivel
// round the shoulder-wrist line (deg: 0 down, + out), the hand on the forearm (deg), and the grip rolled round the dough's tube (deg)
struct DnArmKey { float fwd, up, side, phi, flex, radial, twist, tau; };
// a pose at his mouth: the grip's angle from the bite point round the ring and its roll round the tube; the donut rolled about the way
// into his mouth (+ its right side up), its far side tipped down, turned about its up; his head turned (+ to his right) and bowed (deg);
// the elbow's swivel (deg)
struct DnMouthKey { float dg, tau, rho, sigma, zeta, yaw, bow, phi; };
// (homer bart lisa marge apu) - fitted to each one's arm and mouth (research/donut/pose_design.py, bite_search.py)
// (homer bart lisa marge apu) - Homer's tuned in the game, the family's as fitted to each (research/donut keys_table.py)
static DnArmKey kDnPocketKeys[5] = {{-0.020f, 0.000f, 0.020f, 50.0f, 0.0f, -10.0f, -30.0f, 0.0f},
    {-0.026f, 0.000f, 0.026f, 60.0f, 0.0f, -30.0f, -30.0f, 0.0f},
    {-0.028f, 0.000f, 0.028f, 70.0f, 10.0f, -20.0f, -45.0f, 0.0f},
    {0.021f, 0.010f, -0.001f, 40.0f, 0.0f, 0.0f, -30.0f, 0.0f},
    {-0.018f, 0.000f, 0.018f, 40.0f, 0.0f, -10.0f, -30.0f, 0.0f}};
static DnArmKey kDnSideKeys[5] = {{0.120f, -0.320f, 0.120f, 40.0f, -5.0f, 5.0f, -30.0f, 0.0f},
    {0.154f, -0.334f, 0.051f, 40.0f, -5.0f, 5.0f, -30.0f, 0.0f},
    {0.161f, -0.323f, 0.040f, 40.0f, -5.0f, 5.0f, -30.0f, 0.0f},
    {0.136f, -0.365f, 0.045f, 40.0f, -5.0f, 5.0f, -30.0f, 0.0f},
    {0.128f, -0.341f, 0.042f, 40.0f, -5.0f, 5.0f, -30.0f, 0.0f}};
static DnArmKey kDnAdmireKeys[5] = {{0.360f, 0.040f, 0.020f, 45.0f, -5.0f, -5.0f, -40.0f, -60.0f},
    {0.385f, 0.103f, 0.026f, 45.0f, -5.0f, 10.0f, -40.0f, -60.0f},
    {0.362f, 0.081f, 0.019f, 45.0f, -5.0f, 10.0f, -60.0f, -120.0f},
    {0.411f, -0.001f, 0.026f, 45.0f, -5.0f, 10.0f, -40.0f, -60.0f},
    {0.393f, 0.085f, -0.081f, 45.0f, -5.0f, -5.0f, -40.0f, -30.0f}};
static DnArmKey kDnBellyKeys[5] = {{0.275f, -0.220f, 0.000f, 50.0f, 5.0f, 10.0f, -30.0f, 0.0f},
    {0.250f, -0.282f, -0.128f, 70.0f, 5.0f, 10.0f, -30.0f, 0.0f},
    {0.242f, -0.221f, -0.100f, 50.0f, -10.0f, 10.0f, -30.0f, 0.0f},
    {0.224f, -0.319f, -0.057f, 70.0f, 5.0f, 10.0f, -30.0f, 0.0f},
    {0.248f, -0.235f, -0.107f, 50.0f, 5.0f, 10.0f, -30.0f, 0.0f}};
static DnArmKey kDnHoldKeys[5][kDnBites] = {{{0.330f, -0.060f, -0.020f, 70.0f, -5.0f, 10.0f, -20.0f, 120.0f}, {0.330f, -0.060f, -0.020f, 70.0f, -5.0f, 10.0f, -20.0f, 120.0f}, {0.330f, -0.060f, -0.020f, 70.0f, -5.0f, 10.0f, -20.0f, 120.0f}},
    {{0.385f, -0.077f, 0.026f, 70.0f, 10.0f, 10.0f, -20.0f, 90.0f}, {0.385f, -0.077f, 0.026f, 70.0f, 10.0f, 10.0f, -20.0f, 90.0f}, {0.385f, -0.077f, 0.026f, 70.0f, 10.0f, 10.0f, -20.0f, 90.0f}},
    {{0.362f, -0.060f, 0.019f, 40.0f, -5.0f, -5.0f, -60.0f, -120.0f}, {0.362f, -0.060f, 0.019f, 40.0f, -5.0f, -5.0f, -60.0f, -120.0f}, {0.362f, -0.060f, 0.019f, 40.0f, -5.0f, -5.0f, -60.0f, -120.0f}},
    {{0.410f, -0.068f, 0.023f, 50.0f, 10.0f, -5.0f, -80.0f, -120.0f}, {0.410f, -0.068f, 0.023f, 50.0f, 10.0f, -5.0f, -80.0f, -120.0f}, {0.410f, -0.068f, 0.023f, 50.0f, 10.0f, -5.0f, -80.0f, -120.0f}},
    {{0.384f, -0.064f, 0.021f, 30.0f, -5.0f, -5.0f, -60.0f, -120.0f}, {0.384f, -0.064f, 0.021f, 30.0f, -5.0f, -5.0f, -60.0f, -120.0f}, {0.320f, 0.000f, -0.064f, 60.0f, -5.0f, 10.0f, -20.0f, 120.0f}}};
static DnMouthKey kDnMouthKeys[5][kDnBites + 1] = {{{210.0f, 120.0f, -30.0f, 0.0f, -20.0f, 20.0f, 10.0f, 36.0f},
     {195.0f, 90.0f, -60.0f, 0.0f, 0.0f, 10.0f, 0.0f, 35.0f},
     {165.0f, 60.0f, -75.0f, 0.0f, 0.0f, 10.0f, 0.0f, 42.0f},
     {-45.0f, 90.0f, -90.0f, 20.0f, 20.0f, 20.0f, 20.0f, 34.0f}},
    {{165.0f, 30.0f, -60.0f, 0.0f, 0.0f, 10.0f, 10.0f, 37.0f},
     {150.0f, 0.0f, -60.0f, 0.0f, 0.0f, 20.0f, 0.0f, 31.5f},
     {150.0f, 0.0f, -60.0f, 0.0f, 0.0f, 20.0f, 0.0f, 31.6f},
     {-45.0f, 120.0f, 0.0f, 20.0f, 20.0f, 20.0f, 0.0f, 27.3f}},
    {{120.0f, -30.0f, -45.0f, 0.0f, 0.0f, 0.0f, 10.0f, 28.9f},
     {120.0f, -30.0f, -45.0f, 0.0f, 0.0f, 0.0f, 10.0f, 29.2f},
     {120.0f, -30.0f, -60.0f, 0.0f, 0.0f, 10.0f, 10.0f, 26.5f},
     {45.0f, -120.0f, 0.0f, 0.0f, -20.0f, 0.0f, 0.0f, 23.8f}},
    {{210.0f, 90.0f, -45.0f, 0.0f, 0.0f, 0.0f, 20.0f, 51.3f},
     {210.0f, 90.0f, -30.0f, 0.0f, 0.0f, 0.0f, 10.0f, 53.5f},
     {165.0f, 30.0f, -60.0f, 0.0f, 0.0f, 20.0f, 20.0f, 54.9f},
     {-45.0f, 120.0f, 0.0f, 20.0f, 20.0f, 20.0f, 0.0f, 33.0f}},
    {{210.0f, 90.0f, -30.0f, 20.0f, -20.0f, 0.0f, 10.0f, 71.8f},
     {210.0f, 60.0f, -90.0f, 0.0f, -20.0f, 10.0f, 20.0f, 32.1f},
     {165.0f, 0.0f, -90.0f, 0.0f, 20.0f, 10.0f, 20.0f, 32.7f},
     {-45.0f, 120.0f, 0.0f, 20.0f, 20.0f, 10.0f, 10.0f, 37.7f}}};
// a pose to blend: his hand, where it holds the ring and how (the grip's ring angle, its roll round the tube, the pocket hold's share),
// the elbow's swivel (rad)
struct DnKey { Mat4 H; float g, tau, pw, phi; };
static DnKey DnKeyBlend(const DnKey& a, const DnKey& b, float u)
{
    DnKey r;
    r.H = DnBlendFrame(a.H, b.H, u);
    r.g = a.g + (b.g - a.g) * u, r.tau = a.tau + (b.tau - a.tau) * u, r.pw = a.pw + (b.pw - a.pw) * u;
    r.phi = a.phi + (b.phi - a.phi) * u;
    return r;
}
// the grip (pocket: the pocket's) rolled round the dough's tube by tau (rad) about the tube's middle line at the grip: the fingers stay
// on the dough (the tube is round, but for its 0.82 flattening)
static Mat4 DnGripRelRoll(bool pocket, float g, float handLen, float kk, float tau)
{
    if (fabsf(tau) < 1e-5f) return DnGripRel(pocket, g, handLen, kk);
    Mat4 at0 = DnGripRel(pocket, 0.0f, handLen, kk);
    Vec3 c0 = V3(kDnR * kk, 0, 0);
    Mat4 h = Mul(Mul(Mul(at0, Translation(c0 * -1.0f)), AxisAngle(V3(0, 0, 1), tau)), Translation(c0));
    float c = cosf(g), s = sinf(g);
    return Mul(h, Basis(V3(c, 0, s), V3(0, 1, 0), V3(-s, 0, c)));
}

// (debug "dnprof 1": where a build's time goes, by section - logged every 120 builds)
static int g_dnProf = 0;
static double g_dnProfT[9], g_dnProfAt;
static int g_dnProfN;
static void DnProf(int k)
{
    if (!g_dnProf) return;
    double now = ProfNow();
    if (k > 0) g_dnProfT[k] += now - g_dnProfAt;
    g_dnProfAt = now;
    if (k == 7 && ++g_dnProfN >= 120) {
        static const char* nm[8] = {"", "start..skin", "keys+phases", "push", "arm", "donut place", "rest", "skinprep+draw"};
        char buf[400];
        int o = 0;
        for (int i = 1; i < 8; i++) o += snprintf(buf + o, sizeof(buf) - o, " %s %.3f", nm[i], g_dnProfT[i] / g_dnProfN);
        Log("dnprof (ms a build):%s", buf);
        memset(g_dnProfT, 0, sizeof(g_dnProfT)), g_dnProfN = 0;
    }
}
// ------------------------------------------------------------------ the pose (BuildPose)
static void BuildPoseDonut(const PoserPose& P)
{
    int n = P.numJoints < 40 ? P.numJoints : 40;
    g_dn.have = false;
    if (!LoadInvBind(P.numJoints) || n < 33) {
        g_skin.active = false;
        return;
    }
    static Mat4 W[40], L[40];
    for (int j = 0; j < n; j++) {
        W[j] = ToMat(P.world[j]);
        L[j] = ToMat(P.object[j]);
    }
    g_skin.handK = Clampf(Len(Row(L[29], 3)) / 0.116f, 0.4f, 1.3f);
    auto Rebuild = [&](int j) { W[j] = Mul(L[j], W[g_parent[j]]); };
    G.bodyYaw = atan2f(-P.world[0][8], -P.world[0][10]);
    G.haveBody = true;
    G.knocked = Norm(Row(W[17], 3) - Row(W[0], 3)).y < 0.8f;   // (the game's knockdown: his head low over his root - he lets go)
    bool roots[40] = {};
    const CharShape* shape = CharShapeNow();
    int ci = shape ? (int)(shape - kCharShapes) : 0;
    if (ci < 0 || ci > 4) ci = 0;
    g_dnChar = ci;
    float k = ShotgunScale(L);
    float kk = kDnSizeHomer * g_skin.handK * kDnScale[ci];
    g_dn.k = kk;
    DnProf(0);
    bool bodyOK = BodyLoad();
    float t = g_dn.t;
    DnAt at = DnPhaseAt(t);
    g_dn.phase = g_dn.aborting ? -1 : at.phase;
    Vec3 fB = HeadingDir(G.bodyYaw), rB = Norm(Cross(UP, fB));
    float handLen = Len(Row(L[29], 3)) * 1.4f;

    // his head: looking at the donut, leaning into the bites, nodding as he chews, the gulp (about his neck)
    Vec3 neck = Row(W[16], 3);
    auto MouthW = [&]() { return XformPoint(V3(kDnMouth[ci][0], kDnMouth[ci][1], kDnMouth[ci][2]), W[17]); };
    float pitch = 0, roll = 0, jaw = 0, bliss = 0;
    float tt = at.t, u = at.u;
    // eyes only for the donut: the game's turns of his head (looking round, at people near him) eased out while he eats - his head
    // turned back to where his body faces, the animation's nod and tilt kept. (Looking at Marge, Homer's head swung 45 degrees and
    // his face met the donut he held up; Bart's torso turned round after a warp while his head stayed, and his first bite missed by
    // 7.5 cm - the donut QA.)
    {
        float wF = g_dn.aborting ? 1.0f - DnEase(g_dn.abortT / kDnTAbort)
                 : at.phase <= DN_GRAB ? 0.0f : at.phase == DN_PULL ? DnEase(u) : at.phase == DN_BACK ? 1.0f - DnEase(u) : 1.0f;
        Vec3 hf = Row(W[17], 0);
        hf = V3(hf.x, 0, hf.z);
        if (wF > 0.0f && Len(hf) > 1e-4f) {
            hf = Norm(hf);
            float yaw0 = atan2f(Dot(Cross(hf, fB), UP), Dot(hf, fB));
            wF *= 1.0f - DnEase((fabsf(yaw0) - 1.2f) / 0.4f);   // (not round more than ~70-90 degrees: no owl)
            float yaw = yaw0 * wF;
            for (float sgn = 1.0f; sgn > -2.0f; sgn -= 2.0f) {   // (the turn that brings his face round to his body's way)
                Mat4 R = AxisAngle(UP, yaw * sgn);
                Vec3 nf = XformDir(hf, R);
                if (Dot(nf, fB) >= Dot(hf, fB) - 1e-4f || sgn < 0.0f) {
                    W[17] = Mul(W[17], Mul(Mul(Translation(neck * -1.0f), R), Translation(neck)));
                    roots[17] = true;
                    break;
                }
            }
        }
    }
    switch (g_dn.aborting ? -1 : at.phase) {
    case DN_PULL: pitch = 0.20f * DnEase(u); jaw = 0.10f * DnEase(u); break;
    case DN_ADMIRE: pitch = 0.20f + 0.03f * sinf(tt * 6.0f); roll = 0.10f * DnEase(u / 0.5f); jaw = 0.10f + 0.04f * DnEase(u); break;
    case DN_IN:   // the donut up to his mouth and his head going to it, his mouth open wide before it gets there
        pitch = 0.12f + kDnLean * DnEase(u);
        roll = at.bite == 0 ? 0.10f * (1 - u) : 0.0f;   // (the admire's tilt, straightened)
        jaw = kDnJawBite * DnEaseOut(u / 0.70f);
        bliss = at.bite >= 1 ? 0.60f * (1.0f - DnEase(u / 0.4f)) : 0.0f;   // (eyes open again from the last swallow)
        break;
    case DN_CHOMP:   // his jaw snapped shut on it (three frames) - his teeth meet in the dough
        pitch = 0.12f + kDnLean + 0.03f * sinf(u * PI);
        jaw = kDnJawBite + (kDnJawDough - kDnJawBite) * DnEase(u / 0.8f);
        break;
    case DN_OUT:   // the bite tugged off: the donut out of his mouth as his jaw shuts on the mouthful, his head back up, a first chew
        pitch = 0.12f + kDnLean - (kDnLean + 0.04f) * DnEase(u);
        jaw = u < 0.35f ? 0.02f + (kDnJawDough - 0.02f) * (1.0f - DnEase(u / 0.35f)) : 0.02f + 0.10f * sinf(Clamp01((u - 0.35f) / 0.6f) * PI);
        bliss = 0.45f * DnEase(u / 0.6f);
        break;
    case DN_CHEW: {   // four chews, each smaller as the mouthful goes down, his head nodding with them, his eyes half shut
        float per = kDnTChew / 4.0f;
        float w = 0.5f - 0.5f * cosf(tt / per * 2.0f * PI);
        float amp = kDnJawChew * (1.0f - 0.45f * Clamp01(tt / kDnTChew));
        jaw = 0.02f + amp * w;
        pitch = 0.08f + 0.04f * w;
        bliss = 0.45f + 0.15f * DnEase(u);
        break;
    }
    case DN_SWALLOW:   // and down it goes: his chin dips and comes up, his eyes shut a moment
        pitch = 0.08f + 0.09f * sinf(u * PI);
        jaw = 0.0f;
        bliss = 0.60f + 0.35f * sinf(u * PI);
        break;
    case DN_POP: pitch = 0.18f * DnEase(u); jaw = kDnJawWide * DnEaseOut(u / 0.55f) * (1.0f - DnEase((u - 0.70f) / 0.30f)) + 0.04f; break;
    case DN_FINAL: {
        float w = 0.5f - 0.5f * cosf(tt * 2.0f * PI * 3.0f);
        jaw = 0.04f + (kDnJawChew + 0.08f) * w * (1.0f - DnEase((u - 0.8f) / 0.2f));
        pitch = 0.06f + 0.05f * w - 0.10f * DnEase(u);
        roll = -0.06f * sinf(u * PI);
        bliss = 0.95f * DnEase(tt / 0.2f);
        break;
    }
    case DN_GULP: pitch = -0.04f - 0.12f * sinf(u * PI); jaw = 0.0f; bliss = 0.95f * (1.0f - DnEase(u)); break;
    default: break;
    }
    if (g_dn.aborting) bliss = 0;
    jaw *= kDnJawK[ci];
    g_dnBliss = bliss;
    g_dn.jaw = jaw, g_dn.bliss = bliss;
    // (at his mouth: his head turned and bowed to the donut in his hand - each bite's own, fitted with his arm - turned in as the donut
    // comes up, held, back as it goes)
    float hYaw = 0.0f, hBow = 0.0f;
    if (!g_dn.aborting) {
        int mk = -1;
        float w = 0.0f;
        if (at.phase == DN_IN) mk = at.bite, w = DnEase(u);
        else if (at.phase == DN_CHOMP) mk = at.bite, w = 1.0f;
        else if (at.phase == DN_OUT) mk = at.bite, w = 1.0f - DnEase(u / 0.7f);
        else if (at.phase == DN_POP) mk = kDnBites, w = DnEase(u / 0.55f);
        else if (at.phase == DN_FINAL) mk = kDnBites, w = 1.0f - DnEase(tt / 0.40f);
        if (mk >= 0) hYaw = kDnMouthKeys[ci][mk].yaw * 0.0174533f * w, hBow = kDnMouthKeys[ci][mk].bow * 0.0174533f * w;
    }
    // (the head turned in place: pitch about his shoulders' line, roll about his facing; then to the donut)
    if (fabsf(pitch) > 1e-4f || fabsf(roll) > 1e-4f || fabsf(hYaw) > 1e-4f || fabsf(hBow) > 1e-4f) {
        Mat4 R = Mul(Mul(AxisAngle(rB, pitch), AxisAngle(fB, roll)), Mul(AxisAngle(rB, hBow), AxisAngle(UP, hYaw)));
        W[17] = Mul(W[17], Mul(Mul(Translation(neck * -1.0f), R), Translation(neck)));
        roots[17] = true;
    }
    // his jaw: open about its own Z (the hinge Radical's animations open it on)
    if (jaw > 1e-4f) {
        Mat4 Lj = WithTrans(Mul(AxisAngle(V3(0, 0, 1), jaw), Rot3(L[18])), Row(L[18], 3));
        W[18] = Mul(Lj, W[17]);
        roots[18] = true;
    } else
        Rebuild(18);
    g_dn.head = W[17];
    Vec3 mouth = MouthW();
    g_dn.mouthW = mouth;

    if (bodyOK) BodySkin(W, n);   // (his skin as posed so far: the head turned; the arms still the game's)

    DnProf(1);
    // ---- where the hand goes
    Vec3 hipR = Row(W[9], 3);
    Vec3 tpocket = hipR + rB * (kSgPocketOut * k) + fB * (kSgPocketFwd * k) + UP * (kSgPocketUp * k);
    // (the pocket's mouth out of his belly: worked out once a donut, and again as one goes back - his pocket doesn't move on him; every
    // build it was up to 12 nearest-skin searches, 2.2 ms of a build's 3.9 - the perf meter)
    static int sPocketSteps = 0, sPocketFor = -1;
    int want = g_dnStarts * 2 + (g_dn.aborting ? 1 : 0);
    if (bodyOK && sPocketFor != want) {
        sPocketFor = want, sPocketSteps = 0;
        for (int it = 0; it < 12; it++) {
            float d = BodyDist(tpocket + rB * (0.012f * k * sPocketSteps) + UP * (0.10f * k), ~(kBodyArmR | kBodyArmL | kBodyHandR | kBodyHandL | kBodyLegs), 0.20f);
            if (d > kSgPocketClear * k) break;
            sPocketSteps++;
        }
    }
    tpocket = tpocket + rB * (0.012f * k * sPocketSteps);
    const float D2R = 0.0174533f;
    Vec3 sUp = Norm(Row(W[15], 0)), sRt = Norm(Row(W[15], 2));
    Rebuild(26);
    Vec3 shR = Row(W[26], 3);
    float aR = Len(Row(L[27], 3)), bR = Len(Row(L[28], 3));
    DnRestFromBind();   // (this character's: the inverse binds are his)
    // (his open mouth's middle: the way in - see kDnLipsAng)
    Vec3 biteFwd, biteUp;
    {
        Vec3 hX = Norm(XformDir(V3(1, 0, 0), W[17])), hUp = Norm(XformDir(V3(0, -1, 0), W[17]));
        float a = kDnLipsAng[ci] - 0.5f * kDnJawBite * kDnJawK[ci];
        biteFwd = Norm(hX * cosf(a) + hUp * sinf(a)), biteUp = Norm(hUp * cosf(a) - hX * sinf(a));
    }
    // the grip: his hand at ring angle g, between the eating hold (pw 0) and the pocket's (pw 1), rolled round the tube by tau. (Half way
    // between the two holds his hand went 2.2 cm into the dough: lifted off it as it changes hold; blended at their own angles, it cut
    // across the ring)
    auto GripRel = [&](float g, float w, float tau) {
        Mat4 e = DnGripRelRoll(false, g, handLen, kk, tau);
        if (w <= 0.0f) return e;
        Mat4 pk = DnGripRelRoll(true, g, handLen, kk, tau);
        if (w >= 1.0f) return pk;
        Mat4 m = DnBlendFrame(e, pk, w);
        return WithTrans(Rot3(m), Row(m, 3) + V3(cosf(g), 0, sinf(g)) * (kDnRegripLift * kDnRegripLiftK[ci] * kk * sinf(w * PI)));
    };
    // a pose about his body, from the arm (DnArmKey): `at` is where its wrist place is measured from (his right shoulder, or the pocket)
    Mat4 lean = ItemLean(ci, fB, sUp);   // (the poses lean with his chest: drunk)
    auto ArmKey = [&](const DnArmKey& a, Vec3 from, float g, float pw, Vec3 extra, float twistAdd) {
        DnKey r;
        Vec3 Wp = from + XformDir(fB * (a.fwd * k) + UP * (a.up * k) + rB * (a.side * k), lean) + extra;
        r.H = DnHandNat(shR, Wp, a.phi * D2R, a.flex, a.radial, a.twist + twistAdd, sUp, sRt, aR, bR);
        r.g = g, r.tau = a.tau * D2R, r.pw = pw, r.phi = a.phi * D2R;
        return r;
    };
    // a pose at his mouth (DnMouthKey): bite b (kDnBites: the last piece, by its middle) in along his open mouth's middle `depth` deep,
    // the donut tipped and turned about the bite point, then tugged `tug` back out
    auto MouthKey = [&](int b, float depth, float tug) {
        const DnMouthKey& m = kDnMouthKeys[ci][b];
        float a0, a1;
        DnArcAfter(b, &a0, &a1);
        float th = b < kDnBites ? DnBiteAngle(b) : 0.5f * (a0 + a1);
        Vec3 pt = mouth - biteFwd * (depth * k);
        Mat4 don = DnFrameAt(th, biteFwd * -1.0f, biteUp, pt, kk);
        Vec3 side = Norm(Cross(biteUp, biteFwd));
        Mat4 R = Mul(Mul(AxisAngle(biteFwd, m.rho * D2R), AxisAngle(side, m.sigma * D2R)), AxisAngle(biteUp, m.zeta * D2R));
        don = Mul(Mul(Mul(don, Translation(pt * -1.0f)), R), Translation(pt));
        if (tug > 0.0f) don = WithTrans(Rot3(don), Row(don, 3) + biteFwd * tug - UP * (0.01f * k));
        DnKey r;
        r.g = th + m.dg * D2R, r.tau = m.tau * D2R, r.pw = 0.0f, r.phi = m.phi * D2R;
        r.H = Mul(DnGripRelRoll(false, r.g, handLen, kk, r.tau), don);
        return r;
    };
    auto BiteG = [&](int b) {   // where his hand holds the ring for bite b (and while he holds it before it)
        float a0, a1;
        DnArcAfter(b, &a0, &a1);
        return (b < kDnBites ? DnBiteAngle(b) : 0.5f * (a0 + a1)) + kDnMouthKeys[ci][b].dg * D2R;
    };
    const float gPocket = 3.1416f;   // (in the pocket his hand has it by its top edge)
    Vec3 pocketAt = tpocket + UP * (0.09f * k);
    // (lifted out of it: a little out from his hip as well - straight up, the path bowed 3 cm into his hip on the way to his side, the
    // arm QA)
    auto PocketKey = [&](float lift) { return ArmKey(kDnPocketKeys[ci], pocketAt, gPocket, 1.0f, UP * lift + rB * (0.5f * lift), 0.0f); };
    auto SideKey = [&]() { return ArmKey(kDnSideKeys[ci], shR, gPocket, 1.0f, V3(0, 0, 0), 0.0f); };
    auto AdmireKey = [&](float wiggle) { return ArmKey(kDnAdmireKeys[ci], shR, BiteG(0), 0.0f, V3(0, 0, 0), wiggle); };
    auto HoldKey = [&](int next) { return ArmKey(kDnHoldKeys[ci][next - 1], shR, BiteG(next), 0.0f, V3(0, 0, 0), 0.0f); };
    auto BellyKey = [&](float pat) { return ArmKey(kDnBellyKeys[ci], shR, 0.0f, 0.0f, fB * (0.045f * pat * k), 0.0f); };   // (a pat: off and back)
    // a pose along four keys (sg 0..3): the hand's place on a Catmull-Rom curve through them (no stop at each), its turn, the grip and
    // the swivel key to key
    auto Path4 = [&](const DnKey* K, float sg) {
        int i = sg < 2.0f ? (sg > 0.0f ? (int)sg : 0) : 2;
        float f = Clamp01(sg - i);
        Vec3 P0 = Row(K[i > 0 ? i - 1 : 0].H, 3), P1 = Row(K[i].H, 3), P2 = Row(K[i + 1].H, 3), P3 = Row(K[i < 2 ? i + 2 : 3].H, 3);
        Vec3 pos = (P1 * 2.0f + (P2 - P0) * f + (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * (f * f) +
                    (P1 * 3.0f - P0 - P2 * 3.0f + P3) * (f * f * f)) * 0.5f;
        DnKey r = DnKeyBlend(K[i], K[i + 1], f);
        r.H = WithTrans(Rot3(r.H), pos);
        return r;
    };
    // (the same by distance along it: eased u 0..1 -> sg - an even speed key to key, not each stretch in the same time: the long
    // one past his belly whipped the donut up at 6 m/s)
    auto PathU = [&](const DnKey* K, float ue) {
        float l[3], tot = 0.0f;
        for (int i = 0; i < 3; i++) tot += (l[i] = Len(Row(K[i + 1].H, 3) - Row(K[i].H, 3)) + 1e-4f);
        float d = Clamp01(ue) * tot;
        for (int i = 0; i < 3; i++) {
            if (d <= l[i] || i == 2) return i + Clamp01(d / l[i]);
            d -= l[i];
        }
        return 3.0f;
    };

    // this frame's: the pose (hand, grip, swivel), its weight (0 the game's arm), the finger curl, whether the donut is out and held
    DnKey cur = PocketKey(0.0f);
    Mat4 D = Identity();
    float wR = 1.0f, curl = 0.0f;
    bool donutShown = false, inHand = false;
    // (the last frame's, for a put-away that starts where his hand was)
    static DnKey sLast;
    static float sLastCurl = 0.0f;
    static bool sLastShown = false;
    if (g_dn.aborting) {   // back into the pocket from wherever it is, then the arm goes back to the game's
        float a = g_dn.abortT;
        static DnKey sFrom;
        static float sCurl, sFromAt = -1.0f;
        static bool sHeld;
        if (sFromAt != t) {   // (once, as it starts: the timeline stands still while it goes back)
            sFromAt = t;
            sFrom = sLast, sCurl = sLastCurl, sHeld = sLastShown && !g_dn.gone;
        }
        float u1 = DnEase(a / kDnTAbort), u2 = DnEase((a - kDnTAbort) / 0.25f);
        if (sHeld) {   // the donut back down into his pocket, then his hand out of it (the pull's way back: beside his belly, down his side;
                       // from near the pocket already, straight there)
            DnKey pk = PocketKey(0.0f), K[4] = {sFrom, SideKey(), PocketKey(0.12f * k), pk};
            if (Len(Row(sFrom.H, 3) - Row(pk.H, 3)) < 0.25f * k) cur = DnKeyBlend(sFrom, pk, u1);
            else cur = Path4(K, PathU(K, u1));
            if (a >= kDnTAbort) cur = pk;
            wR = 1.0f - u2;
            donutShown = inHand = a < kDnTAbort;
            curl = kDnCurl;
        } else {   // (nothing in his hand: the arm just goes back to the game's)
            cur = sFrom;
            wR = 1.0f - DnEase(a / (kDnTAbort + 0.25f));
            curl = sCurl;
        }
    } else {
        switch (at.phase) {
        case DN_REACH: {   // his hand down into his pocket (in from above)
            cur = PocketKey(0.05f * k * (1.0f - DnEase((u - 0.4f) / 0.6f)));
            wR = DnEase(u), curl = 0.2f * u;
            break;
        }
        case DN_GRAB: cur = PocketKey(0.0f); curl = 0.2f + (kDnCurl - 0.2f) * DnEase(u);
            donutShown = u > 0.4f, inHand = true; break;
        case DN_PULL: {   // up out of his pocket and his side, round past his belly - not across it - up in front of his face, his hand
            // sliding round to its eating hold. (Straight from the pocket to his face it came up through his hip and belly; in front of
            // his belly, his forearm went 22 cm through it - the arm QA.)
            DnKey K[4] = {PocketKey(0.0f), PocketKey(0.12f * k), SideKey(), AdmireKey(0.0f)};
            float sg = PathU(K, DnEase(u));
            cur = Path4(K, sg);
            float w = DnEase(sg - 2.0f);   // (the hold changes on the last stretch)
            cur.pw = 1.0f - w;
            cur.g = gPocket + (BiteG(0) - gPocket) * w;
            curl = kDnCurl, donutShown = inHand = true;
            break;
        }
        case DN_ADMIRE:   // (a little turn in his fingers as he looks at it)
            cur = AdmireKey(6.0f * sinf(tt * 4.0f));
            curl = kDnCurl, donutShown = inHand = true;
            break;
        case DN_IN:
        case DN_CHOMP:
        case DN_OUT:
        case DN_CHEW:
        case DN_SWALLOW: {
            int b = at.bite;
            DnKey prev = b == 0 ? AdmireKey(0.0f) : HoldKey(b), bite1 = MouthKey(b, kDnBiteDepth, 0.0f), hold = HoldKey(b + 1);
            // (in: all the way into his open mouth - the piece his teeth take behind his lips; held still as his jaw snaps shut on it)
            // (in: turned to the bite out in front of his mouth, then straight in along it - turning on the way in, the first bite's whole
            // ring swept 6 cm through his muzzle: the donut QA)
            if (at.phase == DN_IN) {
                // (all the way in a little before his jaw shuts on it: arriving as it shut - 12 cm in the last two frames at 18 fps - the
                // first bite's piece went with only 58 % of it in his mouth)
                DnKey pre = MouthKey(b, kDnBiteDepth, (kDnBiteDepth + 0.03f) * k);
                pre.H = WithTrans(Rot3(pre.H), Row(pre.H, 3) - UP * (0.015f * k));
                cur = u < 0.55f ? DnKeyBlend(prev, pre, DnEase(u / 0.55f)) : DnKeyBlend(pre, bite1, DnEase((u - 0.55f) / 0.33f));
            }
            else if (at.phase == DN_CHOMP) cur = bite1;
            else if (at.phase == DN_OUT) {   // a tug away - straight out of his mouth the way it went in (his jaw shutting behind it) - then
                                             // down to his chest
                // (out by the bite's depth and a little more, and down: straight out 11 cm his arm locked straight - the arm QA)
                DnKey tug = MouthKey(b, kDnBiteDepth, (kDnBiteDepth + 0.015f) * k);
                tug.H = WithTrans(Rot3(tug.H), Row(tug.H, 3) - UP * (0.03f * k));
                cur = u < 0.42f ? DnKeyBlend(bite1, tug, DnEaseOut(u / 0.42f)) : DnKeyBlend(tug, hold, DnEase((u - 0.42f) / 0.58f));
            } else if (at.phase == DN_CHEW) {   // held while he chews: a small bob with the chewing
                cur = hold;
                cur.H = WithTrans(Rot3(hold.H), Row(hold.H, 3) + UP * (0.006f * k * sinf(tt / (kDnTChew / 4.0f) * 2.0f * PI)));
            } else
                cur = hold;
            curl = kDnCurl, donutShown = inHand = true;
            break;
        }
        case DN_POP: {
            DnKey prev = HoldKey(kDnBites), in0 = MouthKey(kDnBites, -0.02f, 0.0f), in1 = MouthKey(kDnBites, 0.035f, 0.0f);
            cur = u < 0.55f ? DnKeyBlend(prev, in0, DnEase(u / 0.55f)) : DnKeyBlend(in0, in1, DnEase((u - 0.55f) / 0.30f));
            curl = kDnCurl * (1.0f - 0.6f * DnEase((u - 0.80f) / 0.2f));
            donutShown = inHand = !g_dn.gone;
            break;
        }
        case DN_FINAL: {   // the hand away to his belly; two pats as he chews
            float pat = 0.0f;
            if (tt > 0.45f) pat = fabsf(sinf((tt - 0.45f) * PI / 0.30f)) * (tt < 1.05f ? 1.0f : 0.0f);
            cur = DnKeyBlend(MouthKey(kDnBites, 0.035f, 0.0f), BellyKey(pat), DnEase(tt / 0.40f));
            curl = 0.25f;
            break;
        }
        case DN_GULP: cur = BellyKey(0.0f); curl = 0.25f; break;
        case DN_BACK:   // his hand off his belly, down beside it, then his arm back to the game's (straight from his belly to his side,
                        // the game's arm, his hand went 4 cm through it - the arm QA)
        {
            // (off his belly - forward and out - then down beside it: blended straight, it cut through his side)
            DnKey off = BellyKey(0.0f);
            off.H = WithTrans(Rot3(off.H), Row(off.H, 3) + fB * (0.07f * k) + rB * (0.10f * k));
            DnKey K[4] = {BellyKey(0.0f), off, SideKey(), SideKey()};
            cur = Path4(K, 2.0f * DnEase(u / 0.6f));
            wR = 1.0f - DnEase((u - 0.45f) / 0.55f), curl = 0.25f * (1 - u);
            break;
        }
        default: wR = 0; break;
        }
    }
    DnProf(2);
    Mat4 H = cur.H;
    float gNow = cur.g, pocketW = cur.pw, tauNow = cur.tau;

    // (never more than 93 % of his arm's length - a soft stop from 88 %, the can's governor: at full stretch his arm locked straight.
    // Before the push, which has the last word: after it, the hand pulled back in put the donut's edge 1.6 cm into his collar)
    {
        Vec3 d = Row(H, 3) - shR;
        float L0 = Len(d), lim = (aR + bR) * 0.88f, room = (aR + bR) * 0.05f;
        if (L0 > lim && L0 > 1e-4f) {
            float L1 = lim + room * (1.0f - expf(-(L0 - lim) / room));
            H = WithTrans(Rot3(H), shR + d * (L1 / L0));
        }
    }

    // ---- the donut kept out of his body (not his head: the bites; not his right arm: it holds it)
    g_dn.push = 0;
    if (bodyOK && inHand && donutShown && kDnPushMargin >= 0.0f) {
        float a0, a1;
        DnArcAfter(g_dn.bites, &a0, &a1);
        if (g_dn.gone) a0 = a1;
        bool inPocket = g_dn.aborting || at.phase <= DN_PULL;
        Mat4 Dh = Mul(FullInverse(GripRel(gNow, pocketW, tauNow)), H);
        double t0 = Time_Real();
        // (his head too, but for the bites: Lisa's, ahead of her shoulders, met the donut as she held it up to look at it)
        bool biting = !g_dn.aborting && (at.phase == DN_IN || at.phase == DN_CHOMP || at.phase == DN_OUT || at.phase == DN_POP);
        DnTunnel tun = {mouth, biteFwd, biteUp, Norm(Cross(biteUp, biteFwd)), kDnLipsHalf[ci], 0.08f * k};
        Vec3 push = DnPushOut(Dh, kk, k, kDnPushMargin * k, kBodyTorso | kBodyLegs | kBodyArmL | kBodyHandL | (biting ? 0 : kBodyHead),
                              inPocket ? tpocket.y + 0.035f * k : -1e9f, a0, a1, biting ? &tun : nullptr);
        H = WithTrans(Rot3(H), Row(H, 3) + push);
        g_dn.push = Len(push), g_dn.pushV = push;
        g_dn.pushMs = (float)((Time_Real() - t0) * 1000.0);
        PerfAdd(6, g_dn.pushMs);
        if (g_dnPushLog && (g_dn.push > 0.0005f || g_dn.pushMs > 1.0f))
            Log("donut push: t %.2f phase %d %.1f cm %.2f ms (deepest nearest joint %d)", g_dn.t, at.phase, 100.0f * g_dn.push, g_dn.pushMs,
                g_dnPushPart);
    }
    if (!g_dn.aborting) sLast = cur, sLast.H = H, sLastCurl = curl, sLastShown = donutShown && inHand;

    DnProf(3);
    // ---- the right arm to the hand: a pure hinge elbow (Radical's), at the pose's own swivel - the hand on the forearm is the pose's
    // (blended between poses as they are); out of reach, the arm stops short
    Vec3 pW = Row(H, 3);
    Mat4 Rh = Rot3(H);
    g_dn.phi = cur.phi;
    if (wR > 0.001f) {
        float tw, dev;
        SgArmBlend(W, L, 26, 27, 28, true, shR, pW, Rh, cur.phi, 0.0f, 0.0f, aR, bR, sUp, sRt, wR, &tw, &dev, nullptr, true);
        roots[26] = true;
        bool fitHand = kDnGripSel >= 0.5f && n > 32 && (inHand || (!g_dn.aborting && (at.phase <= DN_GRAB || at.phase == DN_POP)));
        if (fitHand) {   // the fingers and thumb to the grip's own shape (as fitted), from the animation's
            float wF = Clamp01(curl / kDnCurl) * wR;
            for (int j = 29; j <= 32; j++) {
                const float* q = kDnFingers[g_dnChar][j - 29];
                Mat4 Lf = Basis(V3(q[0], q[1], q[2]), V3(q[3], q[4], q[5]), V3(q[6], q[7], q[8]));
                W[j] = Mul(WithTrans(BlendRot(L[j], Lf, wF), Row(L[j], 3)), W[j == 29 || j == 31 ? 28 : j - 1]);
            }
        } else
            CurlHand(W, L, 28, 29, 1.00f * curl * wR, 0.85f * curl * wR, 0.45f * curl * wR, 0.35f * curl * wR);
    }
    DnProf(4);
    // (the donut goes where his hand got to: out of his reach, the arm stops short and the donut floated on beyond it)
    if (inHand) {
        Mat4 hr = Rot3(W[28]);
        for (int i = 0; i < 3; i++) SetRow(hr, i, Norm(Row(hr, i)), 0);
        D = Mul(FullInverse(GripRel(gNow, pocketW, tauNow)), WithTrans(hr, Row(W[28], 3)));
        g_dn.tip = 0;
        if (donutShown && kDnPushMargin >= 0.0f) {   // (clear of his own arm, as solved)
            float a0, a1;
            DnArcAfter(g_dn.bites, &a0, &a1);
            if (g_dn.gone) a0 = a1;
            bool inPocket = g_dn.aborting || at.phase <= DN_PULL;
            Vec3 hold = V3(cosf(gNow), 0, sinf(gNow)) * (kDnR + kDnr);
            D = DnRotPush(D, W, kk, k, hold, kDnPushMargin * k, inPocket ? tpocket.y + 0.035f * k : -1e9f, a0, a1, &g_dn.tip);
        }
    }
    DnProf(5);
    for (int j = 33; j < n; j++) Rebuild(j);
    DnProf(6);
    SkinPrepare(P, W, roots);

    // ---- the donut as drawn
    if (donutShown) {
        g_dn.have = true;
        g_dn.world = WithTrans(Mul(ScaleM(kk), Rot3(D)), Row(D, 3));   // (donut space is metres at Homer's size)
        bool pocket = at.phase <= DN_PULL || g_dn.aborting;
        g_dn.pocket = pocket;
        g_dn.clip[0] = 0, g_dn.clip[1] = 1, g_dn.clip[2] = 0, g_dn.clip[3] = -(tpocket.y + 0.035f * k);
    }
    DnProf(7);
    PtSnapshot();
}

// ------------------------------------------------------------------ drawing
static void DnEnsureMesh()
{
    if (g_dn.meshDirty || !g_dnMesh.nv) {
        BuildDonutMesh(g_dn.a0, g_dn.a1);
        g_dn.meshDirty = false;
        g_dnMeshSerial++;
    }
}
static void DnDraw(Renderer& r, Vec3 viewDir, bool unseen)
{
    if (G.held != WPN_DONUT || !g_dn.have || G.show <= 0.001f || !g_dn.running) return;
    DnEnsureMesh();
    if (unseen) return;
    static Renderer::MeshV mv[2600];
    const Mat4& world = g_dn.world;
    for (int i = 0; i < g_dnMesh.nv; i++) {
        const GV& g = g_dnv[i];
        mv[i].x = g.p.x, mv[i].y = g.p.y, mv[i].z = g.p.z;
        mv[i].c = Lit(g.col, Norm(XformDir(g.n, world)), g.spec, viewDir);
    }
    if (g_dn.pocket) r.Clip3D(g_dn.clip);
    r.Mesh(mv, g_dnMesh.nv, g_dni, g_dnMesh.ni, world.m);
    if (g_dn.pocket) r.Clip3D(nullptr);
}

// ------------------------------------------------------------------ debug
static bool DnDebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "dnfreeze")) {   // "dnfreeze <seconds>" hold the eating at that time ("dnfreeze -1": go on)
        g_dnFreeze = (float)atof(arg);
        Log("dnfreeze: %.2f (running %d, t %.2f)", g_dnFreeze, (int)g_dn.running, g_dn.t);
        return true;
    }
    if (!strcmp(cmd, "dnkey")) {   // "dnkey <pose>[,<field>=<value>...]" this character's arm keys: pose pocket | side | admire | hold1..3 |
                                   // belly (fwd up side phi flex radial twist tau) or bite0..2 | pop (dg tau rho sigma zeta yaw bow phi)
        char buf[300];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char* pose = strtok(buf, ",");
        if (!pose) return true;
        int c = g_dnChar;
        DnArmKey* ak = !strcmp(pose, "pocket") ? &kDnPocketKeys[c] : !strcmp(pose, "side") ? &kDnSideKeys[c]
                     : !strcmp(pose, "admire") ? &kDnAdmireKeys[c] : !strcmp(pose, "belly") ? &kDnBellyKeys[c]
                     : !strncmp(pose, "hold", 4) && pose[4] >= '1' && pose[4] <= '0' + kDnBites ? &kDnHoldKeys[c][pose[4] - '1'] : nullptr;
        DnMouthKey* mk = !strncmp(pose, "bite", 4) && pose[4] >= '0' && pose[4] < '0' + kDnBites ? &kDnMouthKeys[c][pose[4] - '0']
                       : !strcmp(pose, "pop") ? &kDnMouthKeys[c][kDnBites] : nullptr;
        if (!ak && !mk) {
            Log("dnkey: no pose %s", pose);
            return true;
        }
        static const char* an[8] = {"fwd", "up", "side", "phi", "flex", "radial", "twist", "tau"};
        static const char* mn[8] = {"dg", "tau", "rho", "sigma", "zeta", "yaw", "bow", "phi"};
        float* v = ak ? &ak->fwd : &mk->dg;
        const char** nm = ak ? an : mn;
        for (char* p = strtok(nullptr, ","); p; p = strtok(nullptr, ",")) {
            char* eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            for (int i = 0; i < 8; i++)
                if (!strcmp(nm[i], p)) v[i] = (float)atof(eq + 1);
        }
        Log("dnkey %s (char %d): %s %.3f %s %.3f %s %.3f %s %.1f %s %.1f %s %.1f %s %.1f %s %.1f", pose, c, nm[0], v[0], nm[1], v[1], nm[2],
            v[2], nm[3], v[3], nm[4], v[4], nm[5], v[5], nm[6], v[6], nm[7], v[7]);
        return true;
    }
    if (!strcmp(cmd, "dnprof")) {
        g_dnProf = atoi(arg);
        return true;
    }
    if (!strcmp(cmd, "dnpushlog")) {
        g_dnPushLog = atoi(arg);
        return true;
    }
    if (!strcmp(cmd, "dnlog")) {
        DnAt a = DnPhaseAt(g_dn.t);
        Log("donut: push's deepest point nearest joint %d's skin, at %.3f %.3f %.3f (head %.3f %.3f %.3f)", g_dnPushPart, g_dnPushAt.x,
            g_dnPushAt.y, g_dnPushAt.z, g_dn.head.m[12], g_dn.head.m[13], g_dn.head.m[14]);
        Log("donut: t %.2f/%.2f phase %d bite %d u %.2f | left %.2f..%.2f bites %d gone %d | jaw %.2f bliss %.2f | k %.2f | push %.3f (%.3f %.3f %.3f) %.2f ms tip %.2f | swivel %.1f",
            g_dn.t, DnTotal(), a.phase, a.bite, a.u, g_dn.a0, g_dn.a1, g_dn.bites, g_dn.gone, g_dn.jaw, g_dn.bliss, g_dn.k, g_dn.push, g_dn.pushV.x, g_dn.pushV.y, g_dn.pushV.z, g_dn.pushMs, g_dn.tip, g_dn.phi * 57.2958f);
        return true;
    }
    if (!strcmp(cmd, "dntune")) {   // "dntune name=value,..." the donut's numbers
        static const struct { const char* name; float* v; } kv[] = {
            {"jawbite", &kDnJawBite}, {"jawchew", &kDnJawChew}, {"jawwide", &kDnJawWide}, {"depth", &kDnBiteDepth}, {"bite", &kDnBite},
            {"twua", &kDnTwistUA}, {"twfa", &kDnTwistFA}, {"treach", &kDnTReach}, {"tgrab", &kDnTGrab}, {"tpull", &kDnTPull},
            {"tadmire", &kDnTAdmire}, {"tin", &kDnTIn}, {"tchomp", &kDnTChomp}, {"tout", &kDnTOut}, {"tchew", &kDnTChew},
            {"tswallow", &kDnTSwallow}, {"lean", &kDnLean}, {"jawdough", &kDnJawDough},
            {"tpop", &kDnTPop}, {"tfinal", &kDnTFinal}, {"tgulp", &kDnTGulp}, {"tback", &kDnTBack}, {"palmoff", &kDnPalmOff},
            {"palmlow", &kDnPalmLow}, {"curl", &kDnCurl}, {"grip", &kDnGripSel}, {"push", &kDnPushMargin}};
        char buf[400];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        for (char* p = strtok(buf, ","); p; p = strtok(nullptr, ",")) {
            char* eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            for (auto& e : kv)
                if (!strcmp(e.name, p)) *e.v = (float)atof(eq + 1);
        }
        g_dn.meshDirty = true;
        return true;
    }
    return false;
}
