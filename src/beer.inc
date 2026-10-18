// Item 2 of the Weapons Lab: a can of beer - and the drunk mode it starts (drunk.inc). Included from gun.cpp after donut.inc; it
// shares the donut's helpers (DnEase, DnBlendFrame, the fitted fingers, the body push, the mouths measured on each character).
//
// Pick BEER (the wheel's Items page) or key 6: out of his pocket, the tab cracked with a hiss and a spray of foam, up to his mouth,
// his head going back as the can tips up - gulp, gulp, gulp - the last drops shaken out, "ahh", a burp, the can crushed in his fist
// and tossed over his shoulder (it bounces and rolls, and lies there a while). Each can adds to how drunk he is (drunk.inc).

// ------------------------------------------------------------------ numbers
static const float kBrR = 0.033f, kBrH = 0.122f;   // the can: radius, height (m at size 1; x its size as drawn)
static const float kBrSizeHomer = 1.55f;           // Homer's can: a cartoon tall-boy in his 16 cm hand; the others by their hands
static float kBrScale[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
// The grip (research/beer/can_grip.py, release hardening): the can in each one's right hand, fitted WITH the fingers - the can's place
// in the hand and the turns of the fingers and thumb found together on the hand's real mesh (skinned as the game skins it), so that
// the palm, the fingers' inside and the thumb are on the can (Homer: 3, 2 and 1 mm off it, 41 % of the fingers' inside on it) and
// nothing of the hand is in it, every finger joint inside the range Radical's own clips turn it through. The first grip had been fitted
// against the donut's closed fist (the fingers' pose fixed): the only place a can fits that is OUTSIDE the fist, against its knuckles -
// the can's middle 21 cm from the wrist joint - and as his wrist tipped the can up to drink, that lever swung it down under his hand
// (the review). Now 10 cm. The hand round the can's lower half, its top half free above his fist - as Radical's animators had
// Krusty hold the Buzz Cola can in the Level 1 intro (movies/fmv1A.rmv). The can's turn about its own axis: its opening (+X) on his
// wrist's side - the side that faces him as he lifts it, thumb up. Rows: his wrist's frame in can space (X, Y, Z, then where; metres at Homer's size kBrGripK, x kk / kBrGripK).
static const float kBrGripK = 1.55f;
static const float kBrGrip[5][12] = {
    {1.00000f, 0.00310f, -0.00000f, 0.00307f, -0.99161f, 0.12920f, 0.00040f, -0.12920f, -0.99162f, 0.03202f, -0.03792f, 0.09578f},   // homer
    {1.00000f, -0.00019f, 0.00000f, -0.00018f, -0.96375f, 0.26681f, -0.00005f, -0.26681f, -0.96375f, 0.03607f, 0.01255f, 0.10514f},   // bart (no beer for the kids: kept for kBeerForKids)
    {0.99998f, -0.00627f, 0.00000f, -0.00612f, -0.97631f, 0.21629f, -0.00136f, -0.21629f, -0.97633f, 0.05789f, 0.06779f, 0.09585f},   // lisa (no beer for the kids: kept for kBeerForKids)
    {1.00000f, -0.00142f, -0.00000f, -0.00141f, -0.99516f, -0.09825f, 0.00014f, 0.09825f, -0.99516f, 0.07326f, -0.04981f, 0.06544f},   // marge
    {1.00000f, -0.00185f, 0.00000f, -0.00177f, -0.95513f, 0.29618f, -0.00055f, -0.29618f, -0.95513f, 0.06521f, -0.04696f, 0.09046f},   // apu
};
// ... and the fingers and thumb as fitted (joints 29-32 in their parents' frames: rows X, Y, Z, then where - the donut's format)
static const float kBrFingers[5][4][12] = {
    {{0.10338f, 0.16488f, -0.98088f, -0.96309f, -0.22984f, -0.14014f, -0.24855f, 0.95916f, 0.13503f, -0.11619f, -0.00000f, 0.00000f},
     {0.59819f, 0.80135f, 0.00000f, -0.80135f, 0.59819f, 0.00000f, -0.00000f, 0.00000f, 1.00000f, -0.04639f, -0.00064f, -0.00009f},
     {-0.30272f, 0.39081f, -0.86927f, 0.90860f, -0.15704f, -0.38701f, -0.28776f, -0.90698f, -0.30755f, -0.06014f, -0.08519f, 0.00268f},
     {0.97950f, 0.20147f, -0.00000f, -0.20147f, 0.97950f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, -0.05203f, -0.00911f, 0.00512f}},   // homer
    {{0.21041f, 0.21120f, -0.95453f, -0.97205f, -0.05882f, -0.22729f, -0.10415f, 0.97567f, 0.19292f, -0.05994f, -0.00000f, -0.00001f},
     {0.34205f, 0.93968f, 0.00000f, -0.93968f, 0.34205f, -0.00000f, 0.00000f, -0.00000f, 1.00000f, -0.02256f, -0.00000f, 0.00000f},
     {0.38593f, 0.87255f, -0.29951f, -0.49495f, 0.46982f, 0.73095f, 0.77851f, -0.13385f, 0.61319f, -0.02861f, -0.04244f, 0.00029f},
     {0.99766f, 0.06841f, 0.00000f, -0.06841f, 0.99766f, 0.00000f, 0.00000f, -0.00000f, 1.00000f, -0.03729f, 0.00000f, -0.00000f}},   // bart
    {{0.59784f, -0.06716f, -0.79880f, -0.80122f, -0.01870f, -0.59808f, 0.02523f, 0.99757f, -0.06499f, -0.05975f, 0.00000f, 0.00000f},
     {0.47035f, 0.88248f, -0.00000f, -0.88248f, 0.47035f, -0.00000f, 0.00000f, 0.00000f, 1.00000f, -0.02585f, -0.00000f, 0.00000f},
     {0.92761f, 0.25727f, -0.27082f, 0.18763f, 0.30600f, 0.93336f, 0.32300f, -0.91661f, 0.23558f, -0.01091f, -0.03827f, 0.01119f},
     {0.94896f, 0.31540f, 0.00000f, -0.31540f, 0.94896f, -0.00000f, -0.00000f, 0.00000f, 1.00000f, -0.04210f, 0.00000f, 0.00000f}},   // lisa
    {{0.58116f, 0.14197f, -0.80131f, -0.81154f, 0.17428f, -0.55770f, 0.06047f, 0.97441f, 0.21650f, -0.11936f, -0.00000f, -0.00000f},
     {0.65924f, 0.75193f, -0.00000f, -0.75193f, 0.65924f, -0.00000f, -0.00000f, 0.00000f, 1.00000f, -0.03725f, 0.00000f, -0.00000f},
     {-0.11680f, 0.97395f, 0.19435f, 0.24282f, 0.21775f, -0.94532f, -0.96301f, -0.06323f, -0.26193f, -0.04168f, -0.02793f, 0.00885f},
     {0.23590f, 0.95438f, 0.18304f, -0.97170f, 0.23410f, 0.03171f, -0.01258f, -0.18534f, 0.98259f, -0.05946f, 0.00000f, 0.00001f}},   // marge
    {{0.42335f, 0.18035f, -0.88783f, -0.90504f, 0.03989f, -0.42345f, -0.04095f, 0.98279f, 0.18011f, -0.11528f, 0.00001f, -0.00000f},
     {0.72407f, 0.68973f, 0.00000f, -0.68973f, 0.72407f, 0.00000f, -0.00000f, 0.00000f, 1.00000f, -0.05002f, 0.00002f, 0.00001f},
     {0.67285f, 0.72879f, 0.12706f, 0.46277f, -0.28065f, -0.84088f, -0.57716f, 0.62459f, -0.52610f, -0.04049f, -0.04609f, -0.00242f},
     {0.91383f, 0.37978f, 0.14385f, -0.38221f, 0.92400f, -0.01138f, -0.13724f, -0.04458f, 0.98953f, -0.05641f, -0.00000f, -0.00000f}},   // apu
};

// ------------------------------------------------------------------ his right arm as Radical's arms are (round 3, 2026-10-17)
// Review (after the donut's arm): "the elbow twisting up to 50 deg off its hinge and the forearm penetrating Homer's body by 9 cm - use the
// arm-led approach developed for the donut". The arm QA (tools/lab/qa/armqa.py) on the release candidate: the can was placed first -
// beside his thigh, in front of his chest, at his mouth, over his shoulder - and the arm solved to the hand that left, the hand's turn
// shared out to the forearm (35 %) and upper arm (15 %): his elbow 6-50 deg off its hinge through the whole can (Radical's: 0.002), his
// forearm 9 cm through his belly on the pull, his arm locked straight holding it in front of him (the "ahh", the burp), his shoulder
// raised 17-28 deg past Radical's range draining it, his elbow 23 deg in toward his belly. Now the arm leads, as for the donut: the
// elbow a pure hinge; the poses about his body built from the arm (DnArmKey: his wrist's place, the elbow's swivel, the hand on the
// forearm turned as Radical's wrists turn) with the can in his hand by the fitted grip; at his mouth the can's rim is at his lower lip
// and tipped as before (29 deg as he starts, 117 as he drains it, his head going back) - where round his face it comes from, how it
// leans, his head's turn and the elbow's swivel fitted to his arm (research/beer: brarm.py, drink_design.py, drink_refine.py,
// body_design.py; each one's own), the can never sliding in his hand: one place round it for the whole can (kBrTau).
// the drink at six points through it (kBrMouthU; the shake held on the last - its own keys flapped his elbow 60 deg): where round
// his face the can comes from (deg, + to his right of his face's way), its lean about the way in (deg, + its top to his right), his
// head's turn (+ to his right) and bow on top of the drink's own (deg), the elbow's swivel (deg). (Four keys at first: the can's place
// swung 40 deg between two of them, unchecked - in the game it went 3.2 cm into his muzzle there: research/beer drink_dp2.py fits six
// and checks the blend every 5 %)
struct BrMouthKey { float zeta, sigma, yaw, bow, phi, px, py, pz, lo, ld; };   // (px py pz: the elbow's way from his shoulder - forward,
                                                                    // up, right - as fitted: what the drink blends, not phi; lo ld: the
                                                                    // rim's place moved from his lower lip - kBrLipOfs)
static const int kBrMouthStages = 6;
static const float kBrMouthU[kBrMouthStages] = {0.0f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f};
// the poses about his body, arm first (DnArmKey: the wrist from his right shoulder - the pocket's from the pocket - m x his size, his
// body's forward, up, right; the elbow's swivel; the hand on the forearm; the last field unused): in the pocket, out beside his hip,
// up in front of his chest (opened, looked at), lower in front of him (the "ahh", the burp), up in front of his chin (crushed), up by
// his head (the toss's wind-up) and flicked back over his shoulder, and on the way down after it out in front of his belly (straight
// from by his ear to his side, his hand passed his shoulder: his elbow folded to 156 deg, past Radical's deepest - the arm QA)
// kBrTau: his hand's place round the can from the fitted grip's (deg)
// (homer bart lisa marge apu) - fitted to each one's arm (research/beer br_keys_table.py; Bart's and Lisa's: Homer's, no beer for them)
static float kBrTau[5] = {15.0f, 15.0f, 15.0f, -15.0f, -15.0f};
static BrMouthKey kBrMouthKeys[5][kBrMouthStages] = {{{10.0f, 30.0f, 0.0f, 0.0f, 19.3f, 0.9522f, -0.2932f, -0.0859f}, {20.0f, 30.0f, 0.0f, -5.7f, 15.0f, 0.9928f, -0.1090f, -0.0499f}, {40.0f, 30.0f, 8.6f, 0.0f, 21.5f, 0.9374f, 0.1030f, 0.3326f}, {60.0f, 40.0f, 17.2f, -5.7f, 23.6f, 0.6619f, 0.5952f, 0.4557f}, {90.0f, 40.0f, 8.6f, -5.7f, 43.0f, 0.5015f, 0.7760f, 0.3826f}, {90.0f, 40.0f, 17.2f, -5.7f, 17.2f, 0.3693f, 0.8118f, 0.4523f}},
    {{10.0f, 30.0f, 0.0f, 0.0f, 19.3f, 0.9522f, -0.2932f, -0.0859f}, {20.0f, 30.0f, 0.0f, -5.7f, 15.0f, 0.9928f, -0.1090f, -0.0499f}, {40.0f, 30.0f, 8.6f, 0.0f, 21.5f, 0.9374f, 0.1030f, 0.3326f}, {60.0f, 40.0f, 17.2f, -5.7f, 23.6f, 0.6619f, 0.5952f, 0.4557f}, {90.0f, 40.0f, 8.6f, -5.7f, 43.0f, 0.5015f, 0.7760f, 0.3826f}, {90.0f, 40.0f, 17.2f, -5.7f, 17.2f, 0.3693f, 0.8118f, 0.4523f}},
    {{10.0f, 30.0f, 0.0f, 0.0f, 19.3f, 0.9522f, -0.2932f, -0.0859f}, {20.0f, 30.0f, 0.0f, -5.7f, 15.0f, 0.9928f, -0.1090f, -0.0499f}, {40.0f, 30.0f, 8.6f, 0.0f, 21.5f, 0.9374f, 0.1030f, 0.3326f}, {60.0f, 40.0f, 17.2f, -5.7f, 23.6f, 0.6619f, 0.5952f, 0.4557f}, {90.0f, 40.0f, 8.6f, -5.7f, 43.0f, 0.5015f, 0.7760f, 0.3826f}, {90.0f, 40.0f, 17.2f, -5.7f, 17.2f, 0.3693f, 0.8118f, 0.4523f}},
    {{10.0f, 20.0f, -8.6f, -3.0f, 27.2f, 0.7021f, -0.7031f, 0.1122f}, {10.0f, 20.0f, 0.0f, 0.0f, 31.5f, 0.7633f, -0.5701f, 0.3038f}, {10.0f, 10.0f, 0.0f, -6.0f, 27.2f, 0.8883f, -0.2143f, 0.4062f}, {20.0f, 20.0f, 0.0f, -11.7f, 32.9f, 0.8986f, 0.1572f, 0.4096f}, {10.0f, 20.0f, 8.6f, -11.7f, 48.7f, 0.7746f, 0.4486f, 0.4457f}, {30.0f, 30.0f, 17.2f, -11.7f, 32.9f, 0.6482f, 0.4501f, 0.6142f}},
    {{40.0f, 40.0f, -8.6f, 0.0f, 21.5f, 0.7474f, -0.6573f, -0.0970f}, {40.0f, 40.0f, 0.0f, 0.0f, 23.6f, 0.8240f, -0.5526f, 0.1248f}, {40.0f, 40.0f, 0.0f, 0.0f, 25.8f, 0.9592f, -0.1574f, 0.2350f}, {40.0f, 40.0f, 0.0f, 0.0f, 43.0f, 0.9161f, 0.3151f, 0.2480f}, {40.0f, 40.0f, 0.0f, 0.0f, 68.8f, 0.8097f, 0.5161f, 0.2794f}, {40.0f, 40.0f, 0.0f, 0.0f, 77.3f, 0.7719f, 0.5651f, 0.2914f}}};
static DnArmKey kBrPocketKeys[5] = {{-0.040f, 0.000f, 0.100f, 40.0f, -50.0f, 5.0f, -30.0f, 0.0f},
    {-0.040f, 0.000f, 0.100f, 40.0f, -50.0f, 5.0f, -30.0f, 0.0f},
    {-0.040f, 0.000f, 0.100f, 40.0f, -50.0f, 5.0f, -30.0f, 0.0f},
    {-0.046f, 0.046f, -0.000f, 10.0f, -50.0f, 20.0f, -10.0f, 0.0f},
    {-0.043f, -0.021f, 0.064f, 25.0f, -50.0f, 20.0f, -30.0f, 0.0f}};
static DnArmKey kBrSideKeys[5] = {{0.040f, -0.220f, 0.140f, 20.0f, -35.0f, -25.0f, -30.0f, 0.0f},
    {0.040f, -0.220f, 0.140f, 20.0f, -35.0f, -25.0f, -30.0f, 0.0f},
    {0.040f, -0.220f, 0.140f, 20.0f, -35.0f, -25.0f, -30.0f, 0.0f},
    {0.046f, -0.205f, 0.160f, 35.0f, -20.0f, -10.0f, -30.0f, 0.0f},
    {0.043f, -0.234f, 0.149f, 20.0f, -20.0f, 5.0f, -30.0f, 0.0f}};
static DnArmKey kBrFrontKeys[5] = {{0.280f, -0.140f, 0.080f, 50.0f, -20.0f, -25.0f, -50.0f, 0.0f},
    {0.280f, -0.140f, 0.080f, 50.0f, -20.0f, -25.0f, -50.0f, 0.0f},
    {0.280f, -0.140f, 0.080f, 50.0f, -20.0f, -25.0f, -50.0f, 0.0f},
    {0.228f, -0.160f, 0.091f, 35.0f, 10.0f, -10.0f, -30.0f, 0.0f},
    {0.256f, -0.149f, 0.043f, 35.0f, -5.0f, -10.0f, -30.0f, 0.0f}};
static DnArmKey kBrRestKeys[5] = {{0.220f, -0.200f, 0.100f, 35.0f, -35.0f, -25.0f, -30.0f, 0.0f},
    {0.220f, -0.200f, 0.100f, 35.0f, -35.0f, -25.0f, -30.0f, 0.0f},
    {0.220f, -0.200f, 0.100f, 35.0f, -35.0f, -25.0f, -30.0f, 0.0f},
    {0.251f, -0.182f, 0.114f, 35.0f, 10.0f, 5.0f, -30.0f, 0.0f},
    {0.234f, -0.213f, 0.107f, 35.0f, 10.0f, 20.0f, -30.0f, 0.0f}};
static DnArmKey kBrCrushKeys[5] = {{0.300f, -0.040f, 0.060f, 65.0f, -5.0f, -25.0f, -70.0f, 0.0f},
    {0.300f, -0.040f, 0.060f, 65.0f, -5.0f, -25.0f, -70.0f, 0.0f},
    {0.300f, -0.040f, 0.060f, 65.0f, -5.0f, -25.0f, -70.0f, 0.0f},
    {0.296f, -0.046f, 0.023f, 50.0f, 10.0f, -25.0f, -50.0f, 0.0f},
    {0.320f, -0.043f, 0.021f, 50.0f, 10.0f, -10.0f, -50.0f, 0.0f}};
static DnArmKey kBrDownKeys[5] = {{0.140f, -0.140f, 0.180f, 35.0f, -20.0f, -10.0f, -30.0f, 0.0f},
    {0.140f, -0.140f, 0.180f, 35.0f, -20.0f, -10.0f, -30.0f, 0.0f},
    {0.140f, -0.140f, 0.180f, 35.0f, -20.0f, -10.0f, -30.0f, 0.0f},
    {0.160f, -0.160f, 0.160f, 35.0f, -20.0f, -10.0f, -50.0f, 0.0f},
    {0.149f, -0.149f, 0.149f, 35.0f, -20.0f, -10.0f, -50.0f, 0.0f}};
static DnArmKey kBrTossKeys[5][2] = {{{0.100f, 0.100f, 0.220f, 45.0f, -20.0f, 20.0f, 10.0f, 0.0f}, {0.040f, 0.240f, 0.100f, 45.0f, -20.0f, -10.0f, -50.0f, 0.0f}},
    {{0.100f, 0.100f, 0.220f, 45.0f, -20.0f, 20.0f, 10.0f, 0.0f}, {0.040f, 0.240f, 0.100f, 45.0f, -20.0f, -10.0f, -50.0f, 0.0f}},
    {{0.100f, 0.100f, 0.220f, 45.0f, -20.0f, 20.0f, 10.0f, 0.0f}, {0.040f, 0.240f, 0.100f, 45.0f, -20.0f, -10.0f, -50.0f, 0.0f}},
    {{0.114f, 0.114f, 0.205f, 45.0f, -20.0f, -10.0f, -10.0f, 0.0f}, {-0.046f, 0.319f, 0.160f, 45.0f, -20.0f, -10.0f, -50.0f, 0.0f}},
    {{0.107f, 0.107f, 0.192f, 45.0f, -20.0f, -10.0f, -50.0f, 0.0f}, {0.043f, 0.298f, 0.107f, 45.0f, -20.0f, -10.0f, -70.0f, 0.0f}}};
// (each body key's elbow way from his shoulder - forward, up, right - as fitted; the order: pocket, side, front, rest, crush, toss0, toss1, down)
static float kBrArmWays[5][8][3] = {{{-0.1217f, -0.8941f, 0.4310f}, {-0.4046f, -0.8744f, 0.2678f}, {0.5370f, -0.6183f, 0.5739f}, {0.2516f, -0.8238f, 0.5080f}, {0.7165f, -0.2842f, 0.6371f}, {-0.1567f, 0.1105f, 0.9815f}, {0.2379f, 0.5006f, 0.8324f}, {-0.0551f, -0.7188f, 0.6931f}},
    {{-0.1217f, -0.8941f, 0.4310f}, {-0.4046f, -0.8744f, 0.2678f}, {0.5370f, -0.6183f, 0.5739f}, {0.2516f, -0.8238f, 0.5080f}, {0.7165f, -0.2842f, 0.6371f}, {-0.1567f, 0.1105f, 0.9815f}, {0.2379f, 0.5006f, 0.8324f}, {-0.0551f, -0.7188f, 0.6931f}},
    {{-0.1217f, -0.8941f, 0.4310f}, {-0.4046f, -0.8744f, 0.2678f}, {0.5370f, -0.6183f, 0.5739f}, {0.2516f, -0.8238f, 0.5080f}, {0.7165f, -0.2842f, 0.6371f}, {-0.1567f, 0.1105f, 0.9815f}, {0.2379f, 0.5006f, 0.8324f}, {-0.0551f, -0.7188f, 0.6931f}},
    {{-0.1914f, -0.9789f, 0.0715f}, {-0.5531f, -0.7767f, 0.3015f}, {0.1350f, -0.8317f, 0.5386f}, {0.1847f, -0.8196f, 0.5423f}, {0.6196f, -0.5378f, 0.5718f}, {-0.1988f, 0.0041f, 0.9800f}, {-0.1033f, 0.4759f, 0.8734f}, {-0.1580f, -0.7968f, 0.5832f}},
    {{-0.3982f, -0.8862f, 0.2370f}, {-0.6099f, -0.7777f, 0.1523f}, {0.2257f, -0.8277f, 0.5138f}, {0.0758f, -0.8314f, 0.5505f}, {0.6372f, -0.5073f, 0.5801f}, {-0.3954f, 0.0062f, 0.9185f}, {0.1219f, 0.4416f, 0.8889f}, {-0.3227f, -0.7310f, 0.6012f}}};
// the rim's place at each key moved out along the mouth's way (lo) and down the face toward the chin (ld), m x the size (round 4,
// Review: "correct the can passing through her jaw during the final drinking motion"). Marge's upper muzzle overhangs her lower lip by
// ~6 cm (research/donut mouth.json): the rim placed as Homer's sat the can's top up in her muzzle - its lid 3.0-3.3 cm inside it as
// she drained it and shook out the last drops, 3.8 in her jaw as she lowered it (brqa). Moved down onto her mouth's line, her upper
// lip over the lid, her head's bow and the can's way in a little changed, the elbow on its way and the grip as they were (research/beer
// lip_fit.py: judged as brqa judges the game - jaw_strict.py; drink_dp2's face check had read it 1.2-1.7). Homer's and Apu's as they
// were: Homer's lid under his upper lip 1.5 cm, Apu's 0.9 - in their mouths' line.
static float kBrLipOfs[5][kBrMouthStages][2] = {{{0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}},
    {{0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}},
    {{0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}},
    {{-0.0100f, 0.0400f}, {-0.0100f, 0.0400f}, {0.0000f, 0.0400f}, {0.0200f, 0.0600f}, {0.0200f, 0.0400f}, {0.0200f, 0.0400f}},
    {{0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}, {0.0000f, 0.0000f}}};
static float BrLerpF(float a, float b, float u) { return a + (b - a) * u; }
static BrMouthKey BrMouthLerp(const BrMouthKey& a, const BrMouthKey& b, float u)
{
    return {BrLerpF(a.zeta, b.zeta, u), BrLerpF(a.sigma, b.sigma, u), BrLerpF(a.yaw, b.yaw, u), BrLerpF(a.bow, b.bow, u), BrLerpF(a.phi, b.phi, u),
            BrLerpF(a.px, b.px, u), BrLerpF(a.py, b.py, u), BrLerpF(a.pz, b.pz, u)};
}
// the drink's key at u (0 .. 1 through it; the shake: 1)
static BrMouthKey BrMouthAt(int ci, float u)
{
    const BrMouthKey* K = kBrMouthKeys[ci >= 0 && ci < 5 ? ci : 0];
    u = Clamp01(u);
    int i = 0;
    while (i < kBrMouthStages - 2 && u > kBrMouthU[i + 1]) i++;
    float t = Clamp01((u - kBrMouthU[i]) / (kBrMouthU[i + 1] - kBrMouthU[i]));
    BrMouthKey r = BrMouthLerp(K[i], K[i + 1], t);
    const float(*O)[2] = kBrLipOfs[ci >= 0 && ci < 5 ? ci : 0];
    r.lo = BrLerpF(O[i][0], O[i + 1][0], t), r.ld = BrLerpF(O[i][1], O[i + 1][1], t);
    return r;
}

// timing (s)
static float kBrTReach = 0.32f, kBrTGrab = 0.12f, kBrTPull = 0.55f, kBrTOpen = 0.75f, kBrTRaise = 0.35f, kBrTDrink = 2.40f;
static float kBrTShake = 0.50f, kBrTLower = 0.40f, kBrTAhh = 0.55f, kBrTBurp = 0.60f, kBrTCrush = 0.50f, kBrTToss = 0.45f;
static float kBrTBack = 0.45f, kBrTAbort = 0.55f;
static float kBrGulps = 5.0f;                      // gulps over the drink
static float kBrHeadBack = 0.40f;                  // how far his head goes back as he drains it (rad)
static float kBrHeadTurn = 0.35f;                  // his head turned toward the can as he drinks (rad, toward his right hand)
static float kBrTipStart = 0.50f, kBrTipMax = 2.05f;   // the can's tip from upright as the drink starts / as he drains it (rad)
static float kBrReach = 0.92f;                     // the most of his arm's length the drink may ask for (his wrist from his shoulder)
static float kBrPerCan = 0.27f;                    // how much drunker a can makes him (drunk.inc: 0 sober .. 1 legless)

enum BrPhase { BR_REACH, BR_GRAB, BR_PULL, BR_OPEN, BR_RAISE, BR_DRINK, BR_SHAKE, BR_LOWER, BR_AHH, BR_BURP, BR_CRUSH, BR_TOSS,
               BR_BACK, BR_END };
static float BrLen(int ph)
{
    static float* const k[BR_END] = {&kBrTReach, &kBrTGrab, &kBrTPull, &kBrTOpen, &kBrTRaise, &kBrTDrink, &kBrTShake, &kBrTLower,
                                     &kBrTAhh, &kBrTBurp, &kBrTCrush, &kBrTToss, &kBrTBack};
    return ph >= 0 && ph < BR_END ? *k[ph] : 0.0f;
}
static float BrStartOf(int ph)
{
    float s = 0;
    for (int i = 0; i < ph; i++) s += BrLen(i);
    return s;
}
static float BrTotal() { return BrStartOf(BR_END); }
struct BrAt { int phase; float u, t; };
static BrAt BrPhaseAt(float t)
{
    float s = 0;
    for (int i = 0; i < BR_END; i++) {
        float L = BrLen(i);
        if (t < s + L || i == BR_END - 1) return {i, Clamp01((t - s) / fmaxf(L, 1e-4f)), t - s};
        s += L;
    }
    return {BR_END - 1, 1.0f, 0.0f};
}

// ------------------------------------------------------------------ state
static struct {
    bool running, aborting, done;
    float t, abortT, lastT;
    bool opened;            // the tab pulled
    float fill;             // what is left in it (1 full .. 0)
    float crush;            // 0 .. 1
    bool meshDirty;
    Mat4 world;             // the can as drawn (scaled can space -> world)
    bool have, pocket;
    float clip[4];
    float k;                // its size
    int phase;
    Mat4 head;
    Vec3 mouthW;
    float jaw, bliss;
    Vec3 lastC, vel;        // (the toss: where the can was last frame, its velocity)
    float tip, govern;      // (report) the can's tip at his mouth; how much of the wanted drink his reach allowed (1 all)
    float phi;              // (report) the elbow's swivel the pose asks for (rad)
    bool released;
} g_br;
static float g_brFreeze = -1.0f;   // debug "brfreeze <s>"
static bool Br_Running() { return g_br.running; }
static Mat4 g_brLastH;              // (the last frame's hand: a put-away starts there)
static bool g_brLastShown = false;
// the cans he has tossed: they fly, bounce and roll, then lie there for a while
struct BrCan { bool live; Vec3 p, v, w; Mat4 R; float age, k, crush, groundY; int bounces; bool still; };
static BrCan g_brCans[8];

// ------------------------------------------------------------------ the model
static GV g_brv[900];
static uint16_t g_bri[2400];
static MeshBuf g_brMesh = {g_brv, g_bri, 0, 0, 900, 2400};
static int g_brMeshSerial;
static const Vec3 kBrRed = {0.80f, 0.07f, 0.09f}, kBrRedD = {0.55f, 0.04f, 0.06f}, kBrWhite = {0.96f, 0.95f, 0.92f};
static const Vec3 kBrSilver = {0.80f, 0.81f, 0.85f}, kBrSilverD = {0.55f, 0.56f, 0.60f}, kBrHole = {0.08f, 0.06f, 0.05f};

// the can's skin at angle a, height share y (-0.5 .. 0.5), crushed c (0 .. 1): -> point (size 1)
static Vec3 BrBodyPoint(float a, float y, float c)
{
    float H = kBrH, r = kBrR;
    float neck = y > 0.40f ? 1.0f - 0.18f * Clamp01((y - 0.40f) / 0.10f) : y < -0.44f ? 1.0f - 0.10f * Clamp01((-0.44f - y) / 0.06f) : 1.0f;
    // crushed: squashed to 55 % of its height, its middle pinched in folds (fist-shaped), the ends left round
    float mid = 1.0f - fabsf(y) * 2.0f;   // 1 at the middle, 0 at the ends
    float fold = c * (0.30f * mid + 0.10f) * (0.6f + 0.4f * cosf(5.0f * a + 3.0f * y)) * (0.7f + 0.3f * sinf(3.0f * a));
    float rr = r * neck * (1.0f - fold);
    float yy = y * H * (1.0f - 0.45f * c) + c * 0.012f * sinf(4.0f * a) * mid;
    return {rr * cosf(a), yy, rr * sinf(a)};
}
static Vec3 BrBodyColour(float a, float y)
{
    if (y > 0.42f || y < -0.44f) return y > 0.46f || y < -0.47f ? kBrSilverD : kBrSilver;
    if (y > -0.16f && y < 0.22f) {   // the white band, a red "label" swoosh across it
        float sw = 0.03f + 0.07f * sinf(2.0f * a + 0.6f);
        return fabsf(y - sw) < 0.05f ? kBrRed : kBrWhite;
    }
    return (y > 0.30f || y < -0.30f) ? kBrRedD : kBrRed;
}
static void BrVert(Vec3 p, Vec3 n, Vec3 col, float spec)
{
    MeshBuf& m = *g_mb;
    if (m.nv < m.maxv) m.v[m.nv++] = {p, n, col, spec, 0};
}
static void BrTri(int a, int b, int c)
{
    MeshBuf& m = *g_mb;
    if (m.ni + 3 > m.maxi) return;
    m.idx[m.ni++] = (uint16_t)a, m.idx[m.ni++] = (uint16_t)b, m.idx[m.ni++] = (uint16_t)c;
}
static void BuildCanMesh(float crush, bool opened)
{
    MeshBuf* saved = g_mb;
    g_mb = &g_brMesh;
    g_brMesh.nv = g_brMesh.ni = 0;
    const int NA = 18;
    static const float ys[] = {-0.50f, -0.47f, -0.44f, -0.30f, -0.16f, -0.08f, 0.0f, 0.08f, 0.16f, 0.22f, 0.30f, 0.42f, 0.46f, 0.50f};
    const int NY = sizeof(ys) / sizeof(ys[0]);
    // the side: a vertex per ring and angle (its colour by where it is; flat-ish shading from the ring's normal)
    for (int j = 0; j < NY; j++)
        for (int i = 0; i <= NA; i++) {
            float a = 6.2832f * i / NA;
            Vec3 p = BrBodyPoint(a, ys[j], crush);
            Vec3 n = Norm(V3(cosf(a), (ys[j] > 0.42f ? 0.5f : ys[j] < -0.44f ? -0.4f : 0.0f), sinf(a)));
            BrVert(p, n, BrBodyColour(a + 0.5f / NA, ys[j]), (ys[j] > 0.42f || ys[j] < -0.44f) ? 0.8f : 0.35f);
        }
    for (int j = 0; j + 1 < NY; j++)
        for (int i = 0; i < NA; i++) {
            int a = j * (NA + 1) + i, b = a + 1, c = a + NA + 1, d = c + 1;
            BrTri(a, c, b), BrTri(b, c, d);
        }
    // the lid (a shallow dish inside the rim) and the bottom (a dome)
    for (int e = 0; e < 2; e++) {
        float y = e ? 0.50f : -0.50f, sgn = e ? 1.0f : -1.0f;
        int c0 = g_brMesh.nv;
        Vec3 ctr = BrBodyPoint(0, y, crush);
        BrVert(V3(0, ctr.y - sgn * 0.004f, 0), V3(0, sgn, 0), e ? kBrSilver : kBrSilverD, 0.9f);
        for (int i = 0; i <= NA; i++) {
            float a = 6.2832f * i / NA;
            Vec3 p = BrBodyPoint(a, y, crush) * 0.97f;
            p.y = ctr.y;
            BrVert(p, V3(0, sgn, 0), e ? kBrSilver : kBrSilverD, 0.9f);
        }
        for (int i = 0; i < NA; i++) e ? BrTri(c0, c0 + 2 + i, c0 + 1 + i) : BrTri(c0, c0 + 1 + i, c0 + 2 + i);
    }
    // the opening (dark, towards +X: the side he drinks from) and the tab (lifted once opened)
    float ytop = BrBodyPoint(0, 0.50f, crush).y + 0.0012f;
    {
        int c0 = g_brMesh.nv;
        float cx = kBrR * 0.48f, rx = kBrR * 0.26f, rz = kBrR * 0.17f;
        Vec3 col = opened ? kBrHole : kBrSilverD;
        BrVert(V3(cx, ytop, 0), V3(0, 1, 0), col, 0.2f);
        for (int i = 0; i <= 10; i++) {
            float a = 6.2832f * i / 10;
            BrVert(V3(cx + rx * cosf(a), ytop, rz * sinf(a)), V3(0, 1, 0), col, 0.2f);
        }
        for (int i = 0; i < 10; i++) BrTri(c0, c0 + 2 + i, c0 + 1 + i);
    }
    {
        int c0 = g_brMesh.nv;
        float lift = opened ? 0.012f : 0.0f;
        Vec3 a = V3(-kBrR * 0.05f, ytop + 0.001f, -kBrR * 0.14f), b = V3(-kBrR * 0.05f, ytop + 0.001f, kBrR * 0.14f);
        Vec3 c = V3(-kBrR * 0.50f, ytop + 0.001f + lift, kBrR * 0.12f), d = V3(-kBrR * 0.50f, ytop + 0.001f + lift, -kBrR * 0.12f);
        Vec3 n = Norm(Cross(b - a, d - a));
        BrVert(a, n, kBrSilver, 1.0f), BrVert(b, n, kBrSilver, 1.0f), BrVert(c, n, kBrSilver, 1.0f), BrVert(d, n, kBrSilver, 1.0f);
        BrTri(c0, c0 + 2, c0 + 1), BrTri(c0, c0 + 3, c0 + 2);
        BrTri(c0, c0 + 1, c0 + 2), BrTri(c0, c0 + 2, c0 + 3);   // (both sides)
    }
    g_mb = saved;
    g_brMeshSerial++;
}
static void BrEnsureMesh()
{
    static float lastCrush = -1.0f;
    static bool lastOpen = false;
    if (g_br.meshDirty || fabsf(lastCrush - g_br.crush) > 0.02f || lastOpen != g_br.opened || g_brMesh.nv == 0) {
        BuildCanMesh(g_br.crush, g_br.opened);
        lastCrush = g_br.crush, lastOpen = g_br.opened, g_br.meshDirty = false;
    }
}

// ------------------------------------------------------------------ the hand on the can
static int g_brChar = 0;
// his wrist's frame in can space (the fitted grip, by the size of this one's hand)
static Mat4 BrHandInCan(float kk)
{
    int ci = g_brChar >= 0 && g_brChar < 5 ? g_brChar : 0;
    const float* q = kBrGrip[ci];
    Mat4 rel = WithTrans(Basis(V3(q[0], q[1], q[2]), V3(q[3], q[4], q[5]), V3(q[6], q[7], q[8])), V3(q[9], q[10], q[11]) * (kk / kBrGripK));
    // (turned round the can's axis: the fingers stay on its side - it's round)
    return fabsf(kBrTau[ci]) > 1e-3f ? Mul(rel, AxisAngle(V3(0, 1, 0), kBrTau[ci] * 0.0174533f)) : rel;
}
// a can frame: its axis (bottom -> top) along `axis`, its +X (the side he drinks from) towards `face`, a point of it (can space,
// size 1) at `pointW`
static Mat4 BrFrameAt(Vec3 axis, Vec3 face, Vec3 localPt, Vec3 pointW, float kk)
{
    Vec3 Y = Norm(axis), X = face - Y * Dot(face, Y);
    if (Len(X) < 1e-4f) X = Cross(Y, V3(0, 0, 1));
    X = Norm(X);
    Vec3 Z = Cross(X, Y);
    Mat4 R = Basis(X, Y, Z);
    return WithTrans(R, pointW - XformDir(localPt * kk, R));
}

// ------------------------------------------------------------------ per frame (Gun_Update)
static int g_brStarts;   // (cans started: what is worked out once a can)
static void BrStart()
{
    g_brStarts++;
    g_br.running = true, g_br.aborting = g_br.done = false;
    g_br.t = g_br.abortT = g_br.lastT = 0;
    g_br.opened = false, g_br.fill = 1.0f, g_br.crush = 0.0f, g_br.released = false;
    g_br.meshDirty = true;
    Log("beer: out of his pocket");
}
static void BrSpray(Vec3 p, Vec3 up, int n, bool foam)
{
    for (int i = 0; i < n && G.nchips < 64; i++) {
        Chip& c = G.chips[G.nchips++];
        Vec3 col = foam ? (Rnd() < 0.7f ? V3(0.98f, 0.97f, 0.92f) : V3(0.95f, 0.80f, 0.35f)) : V3(0.90f, 0.72f, 0.20f);
        Vec3 v = up * (0.6f + 0.9f * Rnd()) + V3(Rnd() - 0.5f, Rnd() * 0.3f, Rnd() - 0.5f) * 0.9f;
        c = {p + V3(Rnd() - 0.5f, 0, Rnd() - 0.5f) * 0.015f, v, 0, 0.5f + 0.5f * Rnd(), Rnd() * 6.3f, (Rnd() - 0.5f) * 8.0f,
             0.004f + 0.006f * Rnd(), col};
    }
}
static void BrUpdateCans(float dt);
static void BrUpdate(float dt, bool onFoot)
{
    BrUpdateCans(dt);
    if (G.held != WPN_BEER) {
        g_br.running = false, g_br.done = false;
        return;
    }
    if (!g_br.running && G.weapon != WPN_BEER) g_br.done = false;
    if (!g_br.running && !g_br.done && G.weapon == WPN_BEER) BrStart();
    if (!g_br.running) return;
    if (!onFoot) {   // (into a car: gone at once)
        g_br.running = false, g_br.done = true;
        G.show = 0;
        if (G.weapon == WPN_BEER) G.weapon = WPN_NONE;
        return;
    }
    if (G.weapon != WPN_BEER && !g_br.aborting) {
        g_br.aborting = true, g_br.abortT = 0;
        Log("beer: put away (t %.2f)", g_br.t);
    }
    if (g_br.aborting) {
        g_br.abortT += dt;
        if (g_br.abortT >= kBrTAbort + 0.25f) {
            g_br.running = false, g_br.done = true;
            G.show = 0;
            if (G.weapon == WPN_BEER) g_br.done = false;
        }
        return;
    }
    if (g_brFreeze >= 0.0f) g_br.t = g_brFreeze;
    else g_br.t += dt;
    float t = g_br.t, t0 = g_br.lastT;
    g_br.lastT = t;
    auto at = [&](int ph, float share) { float s = BrStartOf(ph) + BrLen(ph) * share; return t0 < s && t >= s; };
    if (at(BR_GRAB, 0.6f)) Sound_Play(SND_EQUIP, 0.30f, 1.3f);
    if (at(BR_OPEN, 0.55f)) {   // the tab: a hiss and a spray of foam
        g_br.opened = true;
        Sound_Play(SND_PSSHT, 1.0f, 0.95f + 0.1f * Rnd());
        Mat4 w = g_br.world;
        Vec3 up = Norm(Row(w, 1));
        BrSpray(XformPoint(V3(kBrR * 0.5f, kBrH * 0.5f, 0), w), up, 14, true);
    }
    // drinking: the can empties, a gulp at each swallow (and the drink goes to his head as it goes down)
    float ds = BrStartOf(BR_DRINK), dl = BrLen(BR_DRINK);
    if (t > ds && t0 < ds + dl) {
        float u1 = Clamp01((t - ds) / dl), u0 = Clamp01((t0 - ds) / dl);
        g_br.fill = 1.0f - u1;
        Drunk_AddDrink(kBrPerCan * (u1 - u0));
        for (int i = 0; i < (int)kBrGulps; i++) {
            float gs = ds + dl * (i + 0.6f) / kBrGulps;
            if (t0 < gs && t >= gs) Sound_Play(SND_GULP, 0.75f, 1.05f + 0.1f * Rnd() - 0.04f * i);
        }
    }
    if (at(BR_SHAKE, 0.2f) || at(BR_SHAKE, 0.55f)) Sound_Play(SND_GULP, 0.35f, 1.4f);
    if (at(BR_AHH, 0.15f)) Sound_Play(SND_AHH, 0.8f, 1.0f);
    if (at(BR_BURP, 0.30f)) {
        Sound_Play(SND_BURP, 1.0f, 0.92f + 0.12f * Rnd());
        for (int i = 0; i < 3 && G.npuffs < 24; i++) {   // (a little puff from his mouth)
            Puff& p = G.puffs[G.npuffs++];
            p = {g_br.mouthW + Norm(Row(g_br.head, 0)) * 0.05f, 0, 0.6f + 0.2f * i, 0.05f + 0.02f * i, Rnd() * 6.3f, V3(0.82f, 0.86f, 0.70f)};
        }
    }
    if (at(BR_CRUSH, 0.42f)) Sound_Play(SND_CRUNCH, 1.0f, 0.95f + 0.1f * Rnd());
    {
        float cs = BrStartOf(BR_CRUSH), cl = BrLen(BR_CRUSH);
        if (t >= cs) g_br.crush = DnEaseOut(Clamp01((t - cs - 0.28f * cl) / (cl * 0.40f)));   // (as his fist squeezes: brqa / the takes)
    }
    if (t >= BrTotal()) {
        g_br.running = false, g_br.done = true;
        G.show = 0;
        if (G.weapon == WPN_BEER) G.weapon = WPN_NONE;
        g_drunk.cans++;
        Log("beer: finished (%.1f s), %d cans, drunk %.2f (+%.2f to come)", t, g_drunk.cans, g_drunk.level, g_drunk.pending);
    }
}

// the tossed cans: a body each (gravity, a spin; bounces off the ground with a clank, rolls, comes to rest)
static void BrToss(const Mat4& world, Vec3 vel, float k, float crush)
{
    int best = 0;
    float oldest = -1.0f;
    for (int i = 0; i < 8; i++) {
        if (!g_brCans[i].live) { best = i; oldest = 1e9f; break; }
        if (g_brCans[i].age > oldest) oldest = g_brCans[i].age, best = i;
    }
    BrCan& c = g_brCans[best];
    c.live = true, c.still = false, c.bounces = 0, c.age = 0, c.k = k, c.crush = crush;
    c.p = Row(world, 3), c.v = vel;
    c.R = Rot3(world);
    for (int i = 0; i < 3; i++) SetRow(c.R, i, Norm(Row(c.R, i)), 0);
    c.w = V3(Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f) * 14.0f + Cross(UP, Norm(vel)) * 9.0f;
    Vec3 feet;
    c.groundY = PlayerPos(&feet) ? feet.y : c.p.y - 1.0f;
    float gy;
    if (Trace_GroundY(c.p + V3(0, 0.5f, 0), &gy) && gy < c.p.y + 0.5f && gy > c.groundY - 1.5f) c.groundY = gy;
}
static void BrUpdateCans(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < 8; i++) {
        BrCan& c = g_brCans[i];
        if (!c.live) continue;
        c.age += dt;
        if (c.age > 90.0f) { c.live = false; continue; }   // (tidied away after a minute and a half)
        if (c.still) continue;
        c.v.y -= 9.81f * dt;
        c.p = c.p + c.v * dt;
        float wl = Len(c.w);
        if (wl > 1e-4f) c.R = Mul(c.R, AxisAngle(c.w * (1.0f / wl), wl * dt));
        float rest = c.groundY + kBrR * c.k * 0.95f;
        if (c.p.y < rest) {   // the ground: a bounce (clank), friction; lying on its side it rolls
            c.p.y = rest;
            if (c.v.y < -0.6f) {
                Sound_Play(SND_CLANK, Clampf(-c.v.y * 0.35f, 0.15f, 0.9f), 0.9f + 0.25f * Rnd());
                c.bounces++;
            }
            c.v.y = -c.v.y * 0.35f;
            c.v.x *= 0.80f, c.v.z *= 0.80f;
            c.w = c.w * 0.75f;
            // (settling on its side: its axis turned towards level, the spin turned into a roll about its axis)
            Vec3 ax = Norm(Row(c.R, 1));
            Vec3 lev = Norm(V3(ax.x, 0, ax.z) + V3(0.0001f, 0, 0));
            Vec3 corr = Cross(ax, lev);
            c.R = Mul(c.R, AxisAngle(Norm(corr + V3(0, 0, 1e-5f)), asinf(Clampf(Len(corr), 0.0f, 1.0f)) * 0.5f));
            if (Len(c.v) < 0.12f && fabsf(c.v.y) < 0.2f) c.still = true, c.v = {0, 0, 0}, c.w = {0, 0, 0};
        }
    }
}

// ------------------------------------------------------------------ the pose (BuildPose)
static void BuildPoseBeer(const PoserPose& P)
{
    int n = P.numJoints < 40 ? P.numJoints : 40;
    g_br.have = false;
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
    g_brChar = ci;
    g_dnChar = ci;   // (the donut's fitted fingers: the same fist)
    float k = ShotgunScale(L);
    float kk = kBrSizeHomer * g_skin.handK * kBrScale[ci];
    g_br.k = kk;
    bool bodyOK = BodyLoad();
    float t = g_br.t;
    BrAt at = BrPhaseAt(t);
    g_br.phase = g_br.aborting ? -1 : at.phase;
    Vec3 fB = HeadingDir(G.bodyYaw), rB = Norm(Cross(UP, fB));
    float tt = at.t, u = at.u;

    // ---- his head: eyes on the can (the game's own turns of his head eased out, as for the donut), back as he drains it
    Vec3 neck = Row(W[16], 3);
    {
        float wF = g_br.aborting ? 1.0f - DnEase(g_br.abortT / kBrTAbort)
                 : at.phase <= BR_GRAB ? 0.0f : at.phase == BR_PULL ? DnEase(u) : at.phase == BR_BACK ? 1.0f - DnEase(u) : 1.0f;
        Vec3 hf = Row(W[17], 0);
        hf = V3(hf.x, 0, hf.z);
        if (wF > 0.0f && Len(hf) > 1e-4f) {
            hf = Norm(hf);
            float yaw0 = atan2f(Dot(Cross(hf, fB), UP), Dot(hf, fB));
            wF *= 1.0f - DnEase((fabsf(yaw0) - 1.2f) / 0.4f);
            for (float sgn = 1.0f; sgn > -2.0f; sgn -= 2.0f) {
                Mat4 R = AxisAngle(UP, yaw0 * wF * sgn);
                if (Dot(XformDir(hf, R), fB) >= Dot(hf, fB) - 1e-4f || sgn < 0.0f) {
                    W[17] = Mul(W[17], Mul(Mul(Translation(neck * -1.0f), R), Translation(neck)));
                    roots[17] = true;
                    break;
                }
            }
        }
    }
    float pitch = 0, roll = 0, yawH = 0, jaw = 0, bliss = 0;
    float drinkU = 0.0f;   // (0 .. 1 through the drink)
    float tip = -1.0f, shakeA = 0.0f;   // the can's tip while it's at his mouth (rad; -1: not drinking), the shake of the last drops
    const float pitch0 = 0.06f;         // (his head as the can meets his mouth: dipped a little to meet it)
    switch (g_br.aborting ? -1 : at.phase) {
    case BR_PULL: pitch = 0.18f * DnEase(u); break;
    // (every curve below starts where the one before it ended: a step at a phase's start was a jump in one frame - the jaw 0.16 as the
    // tab hissed, the eyes at the shake, the "ahh" and the burp, the can 4 cm as it was opened. The burp's own jolt stays)
    case BR_OPEN:
        pitch = 0.18f + (0.04f + 0.03f * sinf(tt * 7.0f) * (1.0f - DnEase((u - 0.8f) / 0.2f))) * DnEase(u / 0.25f);
        jaw = u > 0.55f ? (0.12f * (1.0f - DnEase((u - 0.55f) / 0.45f)) + 0.04f) * DnEase((u - 0.55f) / 0.10f) : 0.0f;
        break;
    case BR_RAISE:   // up to his mouth from his right: his head turns to meet it, his mouth opens
        pitch = 0.22f + (pitch0 - 0.22f) * DnEase(u);
        yawH = kBrHeadTurn * DnEase(u);
        jaw = 0.04f + 0.06f * DnEase(u);
        tip = kBrTipStart;
        break;
    case BR_DRINK: {   // tipped up as he drains it, his head going back with it; a gulp at each swallow (the jaw and his head work)
        drinkU = u;
        float g = fmodf(u * kBrGulps, 1.0f);
        float e = DnEase(u);
        pitch = pitch0 - (pitch0 + kBrHeadBack) * DnEase(u * 1.1f) + 0.03f * sinf(g * 6.2832f);
        yawH = kBrHeadTurn;
        jaw = 0.10f + 0.05f * sinf(g * 6.2832f);
        bliss = 0.6f * DnEase(u / 0.3f);
        tip = kBrTipStart + (kBrTipMax - kBrTipStart) * e;
        break;
    }
    case BR_SHAKE:   // the last drops shaken out
        pitch = -kBrHeadBack - 0.06f * sinf(u * PI);
        yawH = kBrHeadTurn;
        jaw = 0.10f + 0.06f * DnEase(u / 0.3f) * (1.0f - DnEase((u - 0.7f) / 0.3f));
        bliss = 0.6f + 0.2f * DnEase(u / 0.4f);
        drinkU = 1.0f;
        tip = kBrTipMax + 0.08f * sinf(u * PI);
        shakeA = sinf(u * 6.2832f * 3.0f) * (1.0f - u);
        break;
    case BR_LOWER:
        pitch = -kBrHeadBack * (1.0f - DnEase(u));
        yawH = kBrHeadTurn * (1.0f - DnEase(u));
        jaw = 0.10f * (1.0f - u);
        bliss = 0.8f;
        tip = kBrTipMax;
        break;
    case BR_AHH: pitch = -0.10f * sinf(u * PI); jaw = 0.22f * sinf(Clamp01(u * 1.4f) * PI); bliss = 0.8f + 0.15f * DnEase(u / 0.3f); break;
    case BR_BURP: {   // a jolt forward, the jaw thrown open
        float b = Clamp01((u - 0.25f) / 0.25f);
        pitch = u < 0.25f ? -0.06f * DnEase(u / 0.25f) : 0.18f * sinf(b * PI) * (1.0f - Clamp01((u - 0.5f) / 0.5f));
        jaw = u < 0.25f ? 0.0f : 0.42f * sinf(Clamp01((u - 0.25f) / 0.45f) * PI);
        bliss = u < 0.3f ? 0.95f + (0.4f - 0.95f) * DnEase(u / 0.12f) : 0.4f * (1.0f - DnEase((u - 0.3f) / 0.15f));
        break;
    }
    case BR_CRUSH: pitch = 0.20f * DnEase(u / 0.4f); jaw = 0.06f * DnEase(u / 0.3f); break;
    case BR_TOSS: pitch = 0.20f * (1.0f - DnEase(u)); roll = -0.10f * sinf(u * PI); jaw = 0.06f * (1.0f - DnEase(u / 0.4f)); break;
    default: break;
    }
    // (put away part-way: his head eased back from the turn the can had it in - it went back to his own in one frame, 9-13 deg drinking,
    // and Marge's face met her can there: round 4)
    static float sHeadP = 0, sHeadR = 0, sHeadY = 0, sHeadJ = 0;
    float headP = 0, headR = 0, headY = 0;   // (the turn SetHead gave last)
    if (g_br.aborting) {
        float abW = 1.0f - DnEase(g_br.abortT / 0.3f);
        pitch = sHeadP * abW, roll = sHeadR * abW, yawH = sHeadY * abW, jaw = sHeadJ * abW;
    }
    float jawRaw = jaw;
    if (g_br.aborting) bliss = 0;
    jaw *= kDnJawK[ci];
    g_dnBliss = bliss;   // (face.inc: the eyes shut - the donut's)
    g_br.jaw = jaw, g_br.bliss = bliss;
    const float D2R = 0.0174533f;
    // (at his mouth: the drink's own key - where round his face the can comes from, how it leans, his head turned and bowed to it, the
    // elbow's swivel - turned in as it comes up, held, back as it goes down)
    BrMouthKey mk = BrMouthAt(ci, 0.0f);
    float mw = 0.0f;
    if (!g_br.aborting) {
        if (at.phase == BR_RAISE) mw = DnEase(u);
        else if (at.phase == BR_DRINK) mk = BrMouthAt(ci, u), mw = 1.0f;
        else if (at.phase == BR_SHAKE || at.phase == BR_LOWER) mk = BrMouthAt(ci, 1.0f), mw = at.phase == BR_SHAKE ? 1.0f : 1.0f - DnEase(u);
    }
    float hYaw = mk.yaw * D2R * mw, hBow = mk.bow * D2R * mw;
    // his head turned in place (pitch about his shoulders' line, roll about his facing, a turn toward his right hand) and where his
    // mouth is then
    Mat4 head0 = W[17];
    Vec3 mouth = {0, 0, 0}, mfw = {1, 0, 0}, mdn = {0, -1, 0};
    auto SetHead = [&](float p, float r, float y) {
        p += hBow, y += hYaw;
        headP = p, headR = r, headY = y;
        if (fabsf(p) > 1e-4f || fabsf(r) > 1e-4f || fabsf(y) > 1e-4f) {
            Mat4 R = Mul(Mul(AxisAngle(rB, p), AxisAngle(fB, r)), AxisAngle(UP, y));
            W[17] = Mul(head0, Mul(Mul(Translation(neck * -1.0f), R), Translation(neck)));
            roots[17] = true;
        } else
            W[17] = head0;
        mouth = XformPoint(V3(kDnMouth[ci][0], kDnMouth[ci][1], kDnMouth[ci][2]), W[17]);
        mfw = Norm(XformDir(V3(kDnMouthFwd[ci][0], kDnMouthFwd[ci][1], kDnMouthFwd[ci][2]), W[17]));
        mdn = Norm(XformDir(V3(-kDnMouthFwd[ci][1], kDnMouthFwd[ci][0], 0.0f), W[17]));   // (down the face, toward the chin)
    };
    Vec3 shR = Row(W[26], 3);
    float aR = Len(Row(L[27], 3)), bR = Len(Row(L[28], 3));
    Mat4 rel = BrHandInCan(kk);
    auto HandAt = [&](const Mat4& can) { return Mul(rel, can); };
    auto CanOfHand = [&](const Mat4& hand) { return Mul(FullInverse(rel), hand); };
    // drinking: the can's rim (its drinking side, at the top) at his lower lip, coming to his mouth from his right-front, tipped up by
    // `tip` (rad from upright) - a rigid turn of the upright can about the line across its tipping, so its frame never flips (the old
    // one was made from its axis and his face's way, which are the same line as it passes level - it could flip mid-drink)
    auto Drink = [&](float tipA, float shake) {
        Vec3 fH = Norm(V3(mfw.x, 0, mfw.z) + fB * 0.001f);
        Vec3 fA = Norm(XformDir(fH, AxisAngle(UP, mk.zeta * D2R)));
        Vec3 X0 = fA * -1.0f, Y0 = UP;
        Mat4 Rt = Mul(AxisAngle(X0, 0.12f * shake + mk.sigma * D2R), AxisAngle(Norm(Cross(Y0, X0)), tipA));
        Vec3 X = XformDir(X0, Rt), Y = XformDir(Y0, Rt);
        Vec3 lip = mouth - UP * (0.012f * k) + mfw * (0.004f * k) + (mfw * mk.lo + mdn * mk.ld) * k;
        return BrFrameAt(Y, X, V3(kBrR * 0.92f, kBrH * 0.5f, 0), lip, kk);
    };
    SetHead(pitch, roll, yawH);
    // (the drink within his reach: a short-armed cartoon - Homer's mouth is 42 cm from his right shoulder, his arm 41 cm - asked for
    // more, the arm went straight and the can swung down under his chin (the review). Too far: the tip and his head's going back
    // eased toward where the drink started until his wrist is within kBrReach of his arm's length)
    g_br.govern = 1.0f;
    // (eased toward its answer, not jumped to it: drunk, his swaying shoulder sat at the limit and frame to frame the answer jumped -
    // the can turned 22 deg about his lip in a frame, his hand 15 cm: the arm QA. Out of his reach for a moment instead, the arm's own
    // stop (below) keeps it short of straight)
    static float sGov = 1.0f;
    static double sGovAt = 0.0;
    double nowG = Time_Real();
    float dtG = Clampf((float)(nowG - sGovAt), 0.0f, 0.1f);
    sGovAt = nowG;
    if (tip >= 0.0f) {
        float lim = kBrReach * (aR + bR), tipW = tip, pitW = pitch;
        auto reach = [&]() { return Len(Row(HandAt(Drink(tip, shakeA)), 3) - shR); };
        float want = 1.0f;
        if (reach() > lim) {
            float lo = 0.0f, hi = 1.0f;
            for (int it = 0; it < 8; it++) {
                float sg = 0.5f * (lo + hi);
                SetHead(pitch0 + sg * (pitW - pitch0), roll, yawH);
                tip = kBrTipStart + sg * (tipW - kBrTipStart);
                if (reach() > lim) hi = sg;
                else lo = sg;
            }
            want = lo;
        }
        sGov = want < sGov ? fmaxf(want, sGov - 2.5f * dtG) : fminf(want, sGov + 1.5f * dtG);
        SetHead(pitch0 + sGov * (pitW - pitch0), roll, yawH);
        tip = kBrTipStart + sGov * (tipW - kBrTipStart);
        g_br.govern = sGov;
    } else
        sGov = 1.0f;
    if (jaw > 1e-4f) {
        Mat4 Lj = WithTrans(Mul(AxisAngle(V3(0, 0, 1), jaw), Rot3(L[18])), Row(L[18], 3));
        W[18] = Mul(Lj, W[17]);
        roots[18] = true;
    } else
        Rebuild(18);
    if (!g_br.aborting) sHeadP = headP, sHeadR = headR, sHeadY = headY, sHeadJ = jawRaw;
    g_br.head = W[17];
    g_br.mouthW = mouth;
    g_br.tip = tip;
    if (bodyOK) BodySkin(W, n);

    // ---- where the hand goes
    Vec3 hipR = Row(W[9], 3);
    Vec3 tpocket = hipR + rB * (kSgPocketOut * k) + fB * (kSgPocketFwd * k) + UP * (kSgPocketUp * k);
    // (the pocket's mouth out of his belly: worked out once a can, and again as one goes back - as the donut's; every build it was up to
    // 12 nearest-skin searches, 2 ms a build - the perf meter)
    static int sPocketSteps = 0, sPocketFor = -1;
    int want = g_brStarts * 2 + (g_br.aborting ? 1 : 0);
    if (bodyOK && sPocketFor != want) {
        sPocketFor = want, sPocketSteps = 0;
        for (int it = 0; it < 12; it++) {
            float d = BodyDist(tpocket + rB * (0.012f * k * sPocketSteps) + UP * (0.10f * k), ~(kBodyArmR | kBodyArmL | kBodyHandR | kBodyHandL | kBodyLegs), 0.20f);
            if (d > kSgPocketClear * k) break;
            sPocketSteps++;
        }
    }
    tpocket = tpocket + rB * (0.012f * k * sPocketSteps);
    Vec3 sUp = Norm(Row(W[15], 0)), sRt = Norm(Row(W[15], 2));
    Rebuild(26);
    Vec3 sR = Row(W[26], 3);
    DnRestFromBind();   // (this character's: the inverse binds are his)
    // a pose about his body from the arm (DnArmKey): the wrist's place from `from` (his right shoulder, or the pocket), plus `extra`
    // (the elbow toward the key's own way, as fitted: its swivel number alone is a different elbow wherever the swivel's reference turns
    // fast - his hand up by his head, the toss)
    enum { BW_POCKET, BW_SIDE, BW_FRONT, BW_REST, BW_CRUSH, BW_TOSS0, BW_TOSS1, BW_DOWN };
    Mat4 lean = ItemLean(ci, fB, sUp);   // (the poses lean with his chest: drunk - donut.inc)
    auto ArmKey = [&](const DnArmKey& a, Vec3 from, Vec3 extra, int way) {
        DnKey r = {};
        Vec3 Wp = from + XformDir(fB * (a.fwd * k) + UP * (a.up * k) + rB * (a.side * k), lean) + extra;
        const float* q = kBrArmWays[ci][way];
        Vec3 pole = XformDir(fB * q[0] + UP * q[1] + rB * q[2], lean);
        r.phi = Len(pole) > 0.1f ? SgSwivelOf(pole, sR, Wp, sUp, sRt, true) : a.phi * D2R;
        r.H = DnHandNat(sR, Wp, r.phi, a.flex, a.radial, a.twist, sUp, sRt, aR, bR);
        return r;
    };
    // the drink's: his hand holding the can at his mouth, the elbow toward the key's own way (as a swivel at this wrist: with his hand
    // above his shoulder the swivel's reference turns fast and a swivel number alone put his elbow 50-80 deg elsewhere a frame later)
    auto MouthKey = [&](float tipA, float shake) {
        DnKey r = {};
        r.H = HandAt(Drink(tipA, shake));
        Vec3 pole = XformDir(fB * mk.px + UP * mk.py + rB * mk.pz, lean);
        r.phi = Len(pole) > 0.1f ? SgSwivelOf(pole, sR, Row(r.H, 3), sUp, sRt, true) : mk.phi * D2R;
        return r;
    };
    // (the elbow's way from his shoulder in a pose, and a blend of two poses by those ways - the hand by DnBlendFrame, the elbow turned
    // from one way to the other, the swivel then read at the blended wrist; blending swivel numbers swung it round as the reference
    // turned under them: the toss, the way back, the drink's end - the arm QA)
    auto ElbowWay = [&](const DnKey& K) {
        Mat4 Ru, Rf;
        return Norm(SgArmAt(sR, Row(K.H, 3), aR, bR, true, sUp, sRt, K.phi, &Ru, &Rf) - sR);
    };
    auto KeyBlend = [&](const DnKey& A, const DnKey& B, float t) {
        DnKey r = DnKeyBlend(A, B, t);
        Vec3 pa = ElbowWay(A), pb = ElbowWay(B), p = pa * (1.0f - t) + pb * t;
        if (Len(p) < 0.2f) p = pa * (1.0f - t) + (pb + Norm(Cross(pa, pb) + sRt * 1e-3f) * 0.2f) * t;   // (opposite ways: round the side)
        r.phi = SgSwivelOf(Norm(p), sR, Row(r.H, 3), sUp, sRt, true);
        return r;
    };
    Vec3 pocketAt = tpocket + UP * (0.09f * k);
    // (in his pocket, lifted a little out of it as well - up, the path bowing out from his hip)
    auto PocketKey = [&](float lift) { return ArmKey(kBrPocketKeys[ci], pocketAt, UP * lift + rB * (0.5f * lift), BW_POCKET); };
    auto SideKey = [&]() { return ArmKey(kBrSideKeys[ci], shR, V3(0, 0, 0), BW_SIDE); };
    auto FrontKey = [&](Vec3 extra) { return ArmKey(kBrFrontKeys[ci], shR, extra, BW_FRONT); };
    auto RestKey = [&](Vec3 extra) { return ArmKey(kBrRestKeys[ci], shR, extra, BW_REST); };
    auto CrushKey = [&]() { return ArmKey(kBrCrushKeys[ci], shR, V3(0, 0, 0), BW_CRUSH); };
    auto TossKey = [&](int i) { return ArmKey(kBrTossKeys[ci][i], shR, V3(0, 0, 0), i ? BW_TOSS1 : BW_TOSS0); };
    auto DownKey = [&]() { return ArmKey(kBrDownKeys[ci], shR, V3(0, 0, 0), BW_DOWN); };
    // (along four keys by distance: the donut's Catmull-Rom path - no stop at each, an even speed)
    auto Path4 = [&](const DnKey* K, float ue) {
        float l[3], tot = 0.0f;
        for (int i = 0; i < 3; i++) tot += (l[i] = Len(Row(K[i + 1].H, 3) - Row(K[i].H, 3)) + 1e-4f);
        float d = Clamp01(ue) * tot, sg = 3.0f;
        for (int i = 0; i < 3; i++) {
            if (d <= l[i] || i == 2) { sg = i + Clamp01(d / l[i]); break; }
            d -= l[i];
        }
        int i = sg < 2.0f ? (sg > 0.0f ? (int)sg : 0) : 2;
        float f = Clamp01(sg - i);
        Vec3 P0 = Row(K[i > 0 ? i - 1 : 0].H, 3), P1 = Row(K[i].H, 3), P2 = Row(K[i + 1].H, 3), P3 = Row(K[i < 2 ? i + 2 : 3].H, 3);
        Vec3 pos = (P1 * 2.0f + (P2 - P0) * f + (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * (f * f) +
                    (P1 * 3.0f - P0 - P2 * 3.0f + P3) * (f * f * f)) * 0.5f;
        DnKey r = KeyBlend(K[i], K[i + 1], f);
        Vec3 way = ElbowWay(r);
        r.H = WithTrans(Rot3(r.H), pos);
        r.phi = SgSwivelOf(way, sR, pos, sUp, sRt, true);   // (the elbow's way kept as the curve moves the wrist off the chord)
        return r;
    };

    DnKey cur = PocketKey(0.0f);
    Mat4 D = Identity();
    float wR = 1.0f, curl = 0.0f;
    bool shown = false, inHand = false;
    static DnKey sLast;   // (the last frame's: a put-away starts there)
    static int sLastPhase = BR_REACH;
    static float sLastU = 0.0f;
    if (g_br.aborting) {   // back into the pocket from where it was (a crushed can: let go, the arm down by his belly to the game's)
        static DnKey sFrom;
        static float sFromAt = -1.0f;
        static bool sHeld, sLow, sAtMouth;
        float a = g_br.abortT;
        if (sFromAt != t) {
            sFromAt = t, sFrom = sLast, sHeld = g_brLastShown && g_br.crush < 0.5f;
            sLow = sLastPhase < BR_PULL || (sLastPhase == BR_PULL && sLastU < 0.6f);   // (still on its way up beside his hip)
            sAtMouth = sLastPhase >= BR_RAISE && sLastPhase <= BR_LOWER;
        }
        float u1 = DnEase(a / kBrTAbort), u2 = DnEase((a - kBrTAbort) / 0.25f);
        if (sHeld) {   // (the pull's way back: beside his belly, down his side; from near the pocket already, straight there; still on its
                       // way up beside his hip, straight back down it - up to beside his belly first, his upper arm folded 7 cm into his
                       // side: the arm QA)
            DnKey pk = PocketKey(0.0f);
            if (Len(Row(sFrom.H, 3) - Row(pk.H, 3)) < 0.25f * k) cur = KeyBlend(sFrom, pk, u1);
            else if (sLow) {
                DnKey K[4] = {sFrom, PocketKey(0.12f * k), PocketKey(0.04f * k), pk};
                cur = Path4(K, u1);
            } else {
                DnKey K[4] = {sFrom, SideKey(), PocketKey(0.12f * k), pk};
                cur = Path4(K, u1);
            }
            // (put away from his mouth: out of it first, along his mouth's way - his lips round the rim, the way to the pocket drew the
            // can's top through Marge's lip for two frames, 2 cm: round 4)
            if (sAtMouth) cur.H = WithTrans(cur.H, Row(cur.H, 3) + mfw * (0.05f * k * sinf(PI * Clamp01(u1 / 0.6f))));
            if (a >= kBrTAbort) cur = pk;
            wR = 1.0f - u2;
            shown = inHand = a < kBrTAbort;
        } else {   // (let go: down in front of his belly and his side as after the toss, then the game's arm - straight to the game's
                   // hanging arm from in front of his chest, his hand went 5 cm through his belly: the arm QA)
            DnKey K[4] = {sFrom, DownKey(), SideKey(), SideKey()};
            cur = Path4(K, DnEase(a / kBrTAbort));
            wR = 1.0f - DnEase((a - 0.45f * kBrTAbort) / (0.55f * kBrTAbort + 0.25f));
        }
        curl = 0.55f;
    } else {
        switch (at.phase) {
        case BR_REACH: cur = PocketKey(0.05f * k * (1.0f - DnEase((u - 0.4f) / 0.6f))); wR = DnEase(u); curl = 0.2f * u; break;
        case BR_GRAB: cur = PocketKey(0.0f); curl = 0.2f + 0.35f * DnEase(u); shown = u > 0.4f, inHand = true; break;
        case BR_PULL: {   // up out of the pocket and his side, round past his belly - not across it - up in front of his chest
            DnKey K[4] = {PocketKey(0.0f), PocketKey(0.14f * k), SideKey(), FrontKey(V3(0, 0, 0))};
            cur = Path4(K, DnEase(u));
            curl = 0.55f, shown = inHand = true;
            break;
        }
        case BR_OPEN: {   // held up in front of him, he looks at it - the thumb flicks the tab (the hiss), a little lift as it sprays
            float lift = u > 0.55f ? 0.03f * sinf(Clamp01((u - 0.55f) / 0.3f) * PI) : 0.0f;
            cur = FrontKey(UP * (lift * k));
            curl = 0.55f, shown = inHand = true;
            break;
        }
        case BR_RAISE: cur = KeyBlend(FrontKey(V3(0, 0, 0)), MouthKey(tip, 0), DnEase(u)); curl = 0.55f; shown = inHand = true; break;
        case BR_DRINK: cur = MouthKey(tip, 0); curl = 0.55f; shown = inHand = true; break;
        case BR_SHAKE: cur = MouthKey(tip, shakeA); curl = 0.55f; shown = inHand = true; break;
        case BR_LOWER: cur = KeyBlend(MouthKey(tip, 0), RestKey(V3(0, 0, 0)), DnEase(u)); curl = 0.55f; shown = inHand = true; break;
        case BR_AHH: cur = RestKey(fB * (-0.01f * k * DnEase(u)) + UP * ((-0.03f * sinf(u * PI) - 0.01f * DnEase(u)) * k)); curl = 0.55f;
            shown = inHand = true; break;
        case BR_BURP: cur = RestKey(fB * (-0.01f * k) + UP * (-0.01f * k)); curl = 0.55f; shown = inHand = true; break;
        case BR_CRUSH:   // brought up in front of his chin and squeezed flat in his fist
            cur = KeyBlend(RestKey(fB * (-0.01f * k) + UP * (-0.01f * k)), CrushKey(), DnEase(u / 0.35f));
            curl = 0.55f + 0.30f * DnEase((u - 0.25f) / 0.45f);
            shown = inHand = true;
            break;
        case BR_TOSS: {   // up by his head and flicked back over his shoulder: let go two thirds of the way, the hand following through
            // (the flick over three frames: eased out over two, his wrist turned 54 deg and his elbow 57 in one - the arm QA)
            if (u < 0.33f) cur = KeyBlend(CrushKey(), TossKey(0), DnEase(u / 0.33f));
            else if (u < 0.66f) cur = KeyBlend(TossKey(0), TossKey(1), DnEase((u - 0.33f) / 0.33f));
            else cur = KeyBlend(TossKey(1), TossKey(0), DnEase((u - 0.66f) / 0.34f) * 0.4f);
            curl = u < 0.66f ? 0.85f : 0.85f - 0.65f * DnEase((u - 0.66f) / 0.14f);   // (let go: the hand opens - over a moment)
            shown = inHand = u < 0.66f;
            break;
        }
        case BR_BACK: {   // down in front of his belly, to his side, then his arm back to the game's
            DnKey from = KeyBlend(TossKey(1), TossKey(0), 0.4f), K[4] = {from, DownKey(), SideKey(), SideKey()};
            cur = Path4(K, DnEase(u / 0.7f));
            wR = 1.0f - DnEase((u - 0.45f) / 0.55f), curl = 0.2f * (1.0f - u);
            break;
        }
        default: wR = 0; break;
        }
    }
    Mat4 H = cur.H;

    // (never more than 93 % of his arm's length - a soft stop from 88 %, the donut's: at full stretch his arm locked straight)
    {
        Vec3 d = Row(H, 3) - sR;
        float L0 = Len(d), lim = (aR + bR) * 0.88f, room = (aR + bR) * 0.05f;
        if (L0 > lim && L0 > 1e-4f) H = WithTrans(Rot3(H), sR + d * ((lim + room * (1.0f - expf(-(L0 - lim) / room))) / L0));
    }
    // ---- kept off his body (but for his head while he drinks)
    if (bodyOK && inHand && shown && kDnPushMargin >= 0.0f) {
        bool drinking = !g_br.aborting && at.phase >= BR_RAISE && at.phase <= BR_LOWER;
        Mat4 Dh = CanOfHand(H);
        Vec3 push = {0, 0, 0};
        uint64_t mask = kBodyTorso | kBodyLegs | kBodyArmL | kBodyHandL | (drinking ? 0 : kBodyHead);
        float hideY = (g_br.aborting || at.phase <= BR_PULL) ? tpocket.y + 0.035f * k : -1e9f;
        for (int it = 0; it < 3; it++) {   // (points round the can's side and its ends)
            float worst = kDnPushMargin * k;
            Vec3 wdir = {0, 0, 0};
            for (int i = 0; i < 8; i++)
                for (int j = 0; j < 3; j++) {
                    float a = 6.2832f * i / 8, y = (j - 1) * 0.5f;
                    Vec3 p = XformPoint(V3(kBrR * cosf(a), kBrH * y, kBrR * sinf(a)) * kk, Dh) + push;
                    if (p.y < hideY) continue;
                    Vec3 o;
                    float d = BodyDist(p, mask, 0.12f * k, &o, nullptr, true);
                    if (d < worst) worst = d, wdir = o;
                }
            if (worst >= kDnPushMargin * k) break;
            push = push + wdir * (kDnPushMargin * k - worst);
        }
        float L2 = Len(push), cap = 0.12f * k;
        if (L2 > cap) push = push * (cap / L2);
        H = WithTrans(Rot3(H), Row(H, 3) + push);
    }
    if (!g_br.aborting) g_brLastH = H, g_brLastShown = shown && inHand, sLast = cur, sLast.H = H, sLastPhase = at.phase, sLastU = u;

    // ---- the right arm to the hand: a pure hinge elbow (Radical's) toward the pose's own elbow way (its swivel read again at the wrist
    // the reach governor and the push left) - the hand on the forearm is the pose's
    Vec3 pW = Row(H, 3);
    Mat4 Rh = Rot3(H);
    if (wR > 0.001f && Len(pW - Row(cur.H, 3)) > 1e-4f) cur.phi = SgSwivelOf(ElbowWay(cur), sR, pW, sUp, sRt, true);
    g_br.phi = cur.phi;
    if (wR > 0.001f) {
        float tw, dev;
        SgArmBlend(W, L, 26, 27, 28, true, sR, pW, Rh, cur.phi, 0.0f, 0.0f, aR, bR, sUp, sRt, wR, &tw, &dev, nullptr, true);
        roots[26] = true;
        // the fingers and thumb: the shape fitted round the can, squeezed tighter to crush it (a curl about each finger joint's
        // own Z, the way Radical's clips curl them), opened to let go
        float wF = Clampf(curl / 0.55f, 0.0f, 1.0f) * wR, extra = fmaxf(0.0f, curl - 0.55f);
        for (int j = 29; j <= 32; j++) {
            const float* q = kBrFingers[ci][j - 29];
            Mat4 Lf = Basis(V3(q[0], q[1], q[2]), V3(q[3], q[4], q[5]), V3(q[6], q[7], q[8]));
            Mat4 Lr = BlendRot(L[j], Lf, wF);
            if (extra > 0.0f && (j == 29 || j == 30)) Lr = Mul(AxisAngle(V3(0, 0, 1), extra * (j == 29 ? 0.9f : 0.7f)), Lr);
            W[j] = Mul(WithTrans(Lr, Row(L[j], 3)), W[j == 29 || j == 31 ? 28 : j - 1]);
        }
        // (the thumb flicks the tab open)
        if (!g_br.aborting && at.phase == BR_OPEN && u > 0.35f && u < 0.75f) {
            float f = sinf(Clamp01((u - 0.35f) / 0.4f) * PI) * 0.55f;
            W[32] = Mul(WithTrans(Mul(Rot3(Mul(W[32], FullInverse(W[31]))), AxisAngle(V3(0, 0, 1), f)), Row(L[32], 3)), W[31]);
        }
    }
    // (the can goes where his hand got to)
    if (inHand) {
        Mat4 hr = Rot3(W[28]);
        for (int i = 0; i < 3; i++) SetRow(hr, i, Norm(Row(hr, i)), 0);
        D = CanOfHand(WithTrans(hr, Row(W[28], 3)));
    }
    for (int j = 33; j < n; j++) Rebuild(j);
    SkinPrepare(P, W, roots);

    // ---- the can as drawn; the toss lets go of it
    if (shown) {
        g_br.have = true;
        Mat4 world = WithTrans(Mul(ScaleM(kk), Rot3(D)), Row(D, 3));
        static double lastAt = 0.0;
        double now = Time_Real();
        Vec3 c = Row(D, 3);
        if (now - lastAt > 1e-3 && now - lastAt < 0.2) g_br.vel = (c - g_br.lastC) * (float)(1.0 / (now - lastAt));
        g_br.lastC = c, lastAt = now;
        g_br.world = world;
        bool pocket = at.phase <= BR_PULL || g_br.aborting;
        g_br.pocket = pocket;
        g_br.clip[0] = 0, g_br.clip[1] = 1, g_br.clip[2] = 0, g_br.clip[3] = -(tpocket.y + 0.035f * k);
        g_br.released = false;
    } else if (!g_br.aborting && at.phase == BR_TOSS && !g_br.released) {
        g_br.released = true;
        Mat4 R = Rot3(g_br.world);
        for (int i = 0; i < 3; i++) SetRow(R, i, Norm(Row(R, i)), 0);
        Vec3 v = g_br.vel * 1.1f - fB * 1.2f + UP * 1.4f;
        BrToss(WithTrans(R, Row(g_br.world, 3)), v, kk, g_br.crush);
        Sound_Play(SND_HOLSTER, 0.4f, 1.4f);
    }
    PtSnapshot();
}

// ------------------------------------------------------------------ drawing
static void BrDrawCan(Renderer& r, const Mat4& world, Vec3 viewDir, bool clip, const float* plane)
{
    static Renderer::MeshV mv[900];
    for (int i = 0; i < g_brMesh.nv; i++) {
        const GV& g = g_brv[i];
        mv[i].x = g.p.x, mv[i].y = g.p.y, mv[i].z = g.p.z;
        mv[i].c = Lit(g.col, Norm(XformDir(g.n, world)), g.spec, viewDir);
    }
    if (clip) r.Clip3D(plane);
    r.Mesh(mv, g_brMesh.nv, g_bri, g_brMesh.ni, world.m);
    if (clip) r.Clip3D(nullptr);
}
static void BrDraw(Renderer& r, Vec3 viewDir, bool unseen)
{
    if (unseen) return;
    // the tossed cans (each crushed as it was: the mesh rebuilt for them, then back to the one in his hand)
    bool any = false;
    for (int i = 0; i < 8; i++) any = any || g_brCans[i].live;
    if (any) {
        for (int i = 0; i < 8; i++) {
            const BrCan& c = g_brCans[i];
            if (!c.live) continue;
            BuildCanMesh(c.crush, true);
            BrDrawCan(r, WithTrans(Mul(ScaleM(c.k), c.R), c.p), viewDir, false, nullptr);
        }
        g_br.meshDirty = true;
    }
    if (G.held != WPN_BEER || !g_br.have || G.show <= 0.001f || !g_br.running) return;
    BrEnsureMesh();
    BrDrawCan(r, g_br.world, viewDir, g_br.pocket, g_br.clip);
}

// ------------------------------------------------------------------ debug
static bool BrDebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "brfreeze")) {   // "brfreeze <seconds>" hold the drinking at that time ("brfreeze -1": go on)
        g_brFreeze = (float)atof(arg);
        return true;
    }
    if (!strcmp(cmd, "brlog")) {
        BrAt a = BrPhaseAt(g_br.t);
        Log("beer: t %.2f/%.2f phase %d u %.2f | fill %.2f crush %.2f opened %d | jaw %.2f bliss %.2f | k %.2f | tip %.2f govern %.2f | swivel %.0f", g_br.t,
            BrTotal(), a.phase, a.u, g_br.fill, g_br.crush, (int)g_br.opened, g_br.jaw, g_br.bliss, g_br.k, g_br.tip, g_br.govern, g_br.phi * 57.2958f);
        return true;
    }
    if (!strcmp(cmd, "brkey")) {   // "brkey <pose>[,<field>=<value>...]" this character's arm keys: pose pocket | side | front | rest |
                                   // crush | toss0 | toss1 | down (fwd up side phi flex radial twist) or drink0..5 (zeta sigma yaw bow phi px py pz: u 0, .25, .5, .75, .9, 1) | tau=<deg>
        char buf[300];
        strncpy(buf, arg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char* pose = strtok(buf, ",");
        if (!pose) return true;
        int c = g_brChar >= 0 && g_brChar < 5 ? g_brChar : 0;
        if (!strncmp(pose, "tau=", 4)) {
            kBrTau[c] = (float)atof(pose + 4);
            Log("brkey (char %d): tau %.1f", c, kBrTau[c]);
            return true;
        }
        DnArmKey* ak = !strcmp(pose, "pocket") ? &kBrPocketKeys[c] : !strcmp(pose, "side") ? &kBrSideKeys[c]
                     : !strcmp(pose, "front") ? &kBrFrontKeys[c] : !strcmp(pose, "rest") ? &kBrRestKeys[c]
                     : !strcmp(pose, "crush") ? &kBrCrushKeys[c] : !strcmp(pose, "toss0") ? &kBrTossKeys[c][0]
                     : !strcmp(pose, "toss1") ? &kBrTossKeys[c][1] : !strcmp(pose, "down") ? &kBrDownKeys[c] : nullptr;
        BrMouthKey* mkp = !strncmp(pose, "drink", 5) && pose[5] >= '0' && pose[5] < '0' + kBrMouthStages ? &kBrMouthKeys[c][pose[5] - '0'] : nullptr;
        if (!ak && !mkp) {
            Log("brkey: no pose %s", pose);
            return true;
        }
        static const char* an[7] = {"fwd", "up", "side", "phi", "flex", "radial", "twist"};
        static const char* mn[8] = {"zeta", "sigma", "yaw", "bow", "phi", "px", "py", "pz"};
        float* v = ak ? &ak->fwd : &mkp->zeta;
        const char** nm = ak ? an : mn;
        int nf = ak ? 7 : 8;
        for (char* p = strtok(nullptr, ","); p; p = strtok(nullptr, ",")) {
            char* eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            for (int i = 0; i < nf; i++)
                if (!strcmp(nm[i], p)) v[i] = (float)atof(eq + 1);
        }
        char out[200];
        int o = 0;
        for (int i = 0; i < nf; i++) o += snprintf(out + o, sizeof(out) - o, " %s %.3f", nm[i], v[i]);
        Log("brkey %s (char %d):%s", pose, c, out);
        return true;
    }
    if (!strcmp(cmd, "brtune")) {   // "brtune name=value,..."
        static const struct { const char* name; float* v; } kv[] = {
            {"treach", &kBrTReach}, {"tgrab", &kBrTGrab}, {"tpull", &kBrTPull}, {"topen", &kBrTOpen}, {"traise", &kBrTRaise},
            {"tdrink", &kBrTDrink}, {"tshake", &kBrTShake}, {"tlower", &kBrTLower}, {"tahh", &kBrTAhh}, {"tburp", &kBrTBurp},
            {"tcrush", &kBrTCrush}, {"ttoss", &kBrTToss}, {"tback", &kBrTBack}, {"gulps", &kBrGulps}, {"headback", &kBrHeadBack},
            {"headturn", &kBrHeadTurn}, {"tipstart", &kBrTipStart}, {"tipmax", &kBrTipMax}, {"reach", &kBrReach},
            {"percan", &kBrPerCan}};
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
        return true;
    }
    return false;
}
