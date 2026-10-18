// The pump-action shotgun (Weapons Lab, weapon 1). Included in gun.cpp after the rocket launcher's pose: it uses the
// gun's state (G), the skin swap (g_skin, SkinPrepare), the mesh builders and the pose helpers.
//
// How it goes:
//  - Drawn: his right hand goes to his trouser pocket, closes on something and pulls - and a whole shotgun comes up out of
//    the pocket, muzzle last (a clip plane at the pocket hides what is still "in" it), swings up into both hands and
//    he racks the pump.
//  - Carried: two-handed, low across his belly, the right hand on the grip at his right side and the left resting out on the
//    barrel at his left (it slides back onto the pump to rack and to aim).
//  - Aimed: shouldered - the butt seated in the pocket inside his right shoulder, his chest bladed, his head tipped and
//    rolled down so his jaw rests on the comb, and (face.inc) his left eye shut on the eyeball itself. The raise goes in an
//    order: the muzzle onto the line, the butt up and back into the pocket, his head down last; lowering, the reverse.
//  - Fired: ten pellets in a cone through the shared hit system (FireWeapon, visual cover); a big flash and smoke; the
//    gun slams back into his shoulder, the muzzle climbs, his shoulders and chest rock back with his feet planted; inside
//    the recoil he racks the pump (his left hand rides the fore-end back and home, the gun dips and stays shouldered, his
//    right hand stays on the grip) and a spent red shell flies out late in the back stroke.
//  - Put away: down and back into the pocket.
// Everything comes from his own skeleton (his shoulders, hips, arm lengths) and the gun is sized to him, so the same
// pose fits Bart, Lisa, Marge and Apu.

// ------------------------------------------------------------------ the model
// shotgun space: origin on the bore's axis at the back of the receiver, +Z out of the muzzle, +Y up, +X to his right;
// metres at Homer's size. Compact, because Homer's arms are short (0.41 m from shoulder to wrist): butt to the pump's
// back end is 0.39 m.
static const float kSgMuzzleZ = 0.62f;
static const Vec3 kSgGrip = {0, -0.074f, -0.066f};      // the right hand's grip (the middle of the raked pistol grip)
static const Vec3 kSgButt = {0, -0.042f, -0.255f};      // the middle of the recoil pad
static const float kSgPumpZ0 = 0.135f, kSgPumpZ1 = 0.300f;   // the pump at rest (it slides back kSgPumpTravel)
static const float kSgPumpY = -0.030f, kSgPumpHY = 0.026f;   // its centre line and half height
static float kSgPumpTravel = 0.120f;
static const Vec3 kSgPort = {0.026f, 0.006f, 0.068f};   // the ejection port, on his side
static const Vec3 kSgRear = {0, 0.034f, 0.012f};        // the rib at the back of the receiver (the eye looks along it)

static GV g_sgv[1400];
static uint16_t g_sgi[2400];
static MeshBuf g_sgMesh = {g_sgv, g_sgi, 0, 0, 1400, 2400};
static GV g_shv[160];
static uint16_t g_shi[260];
static MeshBuf g_shellMesh = {g_shv, g_shi, 0, 0, 160, 260};

// a hexahedron from its 8 corners (0-3 the near face, 4-7 the far one, each -x-y, +x-y, +x+y, -x+y), flat shaded
static void SgHex(const Vec3 c[8], Vec3 col, float spec, int part)
{
    static const int f[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    Vec3 ctr = {0, 0, 0};
    for (int i = 0; i < 8; i++) ctr = ctr + c[i];
    ctr = ctr * 0.125f;
    for (int i = 0; i < 6; i++) {
        Vec3 p[4] = {c[f[i][0]], c[f[i][1]], c[f[i][2]], c[f[i][3]]};
        Vec3 n = Norm(Cross(p[1] - p[0], p[2] - p[0]));
        if (Dot(n, (p[0] + p[2]) * 0.5f - ctr) < 0) n = n * -1.0f;
        AddQuad(p, n, col, spec, part);
    }
}
// a box from z0 to z1 whose cross-section (centre cy0 -> cy1, half sizes) changes along it
static void SgTaper(float z0, float z1, float cy0, float hx0, float hy0, float cy1, float hx1, float hy1, Vec3 col, float spec, int part)
{
    Vec3 c[8] = {{-hx0, cy0 - hy0, z0}, {hx0, cy0 - hy0, z0}, {hx0, cy0 + hy0, z0}, {-hx0, cy0 + hy0, z0},
                 {-hx1, cy1 - hy1, z1}, {hx1, cy1 - hy1, z1}, {hx1, cy1 + hy1, z1}, {-hx1, cy1 + hy1, z1}};
    SgHex(c, col, spec, part);
}
// a round section along Z (as AddFrustum) belonging to a moving part
static void SgTube(float cx, float cy, float z0, float r0, float z1, float r1, int segs, Vec3 col, float spec, int part, int caps = 0)
{
    for (int i = 0; i < segs; i++) {
        float a0 = 2 * PI * i / segs, a1 = 2 * PI * (i + 1) / segs;
        Vec3 d0 = {cosf(a0), sinf(a0), 0}, d1 = {cosf(a1), sinf(a1), 0}, c = {cx, cy, 0};
        Vec3 p[4] = {c + d0 * r0 + V3(0, 0, z0), c + d1 * r0 + V3(0, 0, z0), c + d1 * r1 + V3(0, 0, z1), c + d0 * r1 + V3(0, 0, z1)};
        const Vec3 n[4] = {d0, d1, d1, d0};
        AddQuadN(p, n, col, spec, part);
        if (caps & 1) {
            Vec3 q[4] = {c + V3(0, 0, z0), c + d1 * r0 + V3(0, 0, z0), c + d0 * r0 + V3(0, 0, z0), c + V3(0, 0, z0)};
            AddQuad(q, V3(0, 0, -1), col, spec, part);
        }
        if (caps & 2) {
            Vec3 q[4] = {c + V3(0, 0, z1), c + d0 * r1 + V3(0, 0, z1), c + d1 * r1 + V3(0, 0, z1), c + V3(0, 0, z1)};
            AddQuad(q, V3(0, 0, 1), col, spec, part);
        }
    }
}

static void BuildShotgun()
{
    if (g_sgMesh.nv) return;
    g_mb = &g_sgMesh;
    Mat4 I = Identity();
    // colours as the wheel's icon: warm walnut, blued steel, a black rubber pad
    Vec3 wood = {0.55f, 0.28f, 0.10f}, woodL = {0.66f, 0.37f, 0.16f}, woodD = {0.33f, 0.15f, 0.05f};
    Vec3 steel = {0.23f, 0.25f, 0.29f}, steelL = {0.42f, 0.45f, 0.50f}, steelD = {0.12f, 0.13f, 0.15f};
    Vec3 black = {0.05f, 0.05f, 0.06f}, brass = {0.88f, 0.68f, 0.26f}, rubber = {0.11f, 0.10f, 0.10f};
    // receiver: a slab with a rib on top, the ejection port on his side, the loading port underneath
    AddBox({0, -0.008f, 0.066f}, {0.024f, 0.040f, 0.066f}, I, steel, 0.8f, 0);
    AddBox({0, 0.0335f, 0.066f}, {0.011f, 0.0025f, 0.066f}, I, steelL, 1.0f, 0);
    AddBox({0.0247f, 0.006f, 0.068f}, {0.0009f, 0.014f, 0.030f}, I, black, 0.1f, 0);
    AddBox({0, -0.0485f, 0.080f}, {0.016f, 0.0009f, 0.040f}, I, black, 0.1f, 0);
    for (int i = 0; i < 3; i++)   // a row of rivets on the left side (the camera sees his right: these are for the orbit shots)
        AddBox({-0.0247f, -0.020f, 0.030f + i * 0.035f}, {0.0009f, 0.003f, 0.003f}, I, steelL, 1.0f, 0);
    // barrel with a sight rib, the muzzle ring and bore, a brass bead
    AddFrustum(0, 0, 0.132f, 0.0175f, kSgMuzzleZ - 0.012f, 0.0170f, 12, steel, 0.9f);
    AddFrustum(0, 0, kSgMuzzleZ - 0.012f, 0.0195f, kSgMuzzleZ, 0.0195f, 12, steelD, 0.7f, 2);
    AddFrustum(0, 0, kSgMuzzleZ + 0.0005f, 0.0120f, kSgMuzzleZ + 0.0010f, 0.0120f, 12, black, 0.0f, 2);
    AddBox({0, 0.0185f, (0.132f + kSgMuzzleZ) * 0.5f}, {0.0040f, 0.0018f, (kSgMuzzleZ - 0.132f) * 0.5f}, I, steelL, 1.0f, 0);
    AddBox({0, 0.0225f, kSgMuzzleZ - 0.010f}, {0.0040f, 0.0040f, 0.0040f}, I, brass, 1.0f, 0);
    // the magazine tube under the barrel, its cap, and the band that holds the two
    AddFrustum(0, -0.0335f, 0.132f, 0.0145f, 0.515f, 0.0145f, 10, steelD, 0.7f);
    AddFrustum(0, -0.0335f, 0.515f, 0.0160f, 0.535f, 0.0150f, 10, steel, 0.8f, 2);
    AddBox({0, -0.0165f, 0.500f}, {0.0170f, 0.0330f, 0.0070f}, I, steel, 0.8f, 0);
    // the pump (part 1: it slides): a fat wooden forend, rounded off top and bottom, with grooves for the fingers
    float zc = (kSgPumpZ0 + kSgPumpZ1) * 0.5f, hz = (kSgPumpZ1 - kSgPumpZ0) * 0.5f;
    AddBox({0, kSgPumpY, zc}, {0.0300f, kSgPumpHY - 0.006f, hz}, I, wood, 0.45f, 1);
    AddBox({0, kSgPumpY + kSgPumpHY - 0.004f, zc}, {0.0230f, 0.0040f, hz - 0.004f}, I, woodL, 0.5f, 1);
    AddBox({0, kSgPumpY - kSgPumpHY + 0.004f, zc}, {0.0230f, 0.0040f, hz - 0.004f}, I, woodD, 0.4f, 1);
    for (int s = -1; s <= 1; s += 2)
        for (int i = 0; i < 6; i++)
            AddBox({s * 0.0303f, kSgPumpY - 0.004f, kSgPumpZ0 + 0.025f + i * 0.023f}, {0.0009f, 0.012f, 0.0030f}, I, woodD, 0.2f, 1);
    // trigger guard and trigger under the receiver
    AddBox({0, -0.064f, 0.104f}, {0.0050f, 0.016f, 0.0040f}, I, steel, 0.7f, 0);
    AddBox({0, -0.080f, 0.064f}, {0.0050f, 0.0040f, 0.044f}, I, steel, 0.7f, 0);
    AddBox({0, -0.060f, 0.062f}, {0.0030f, 0.012f, 0.0030f}, AxisAngle({1, 0, 0}, -0.25f), steelD, 0.5f, 0);
    // the stock: a raked pistol grip for his right hand, then the butt stock growing deeper towards the pad
    {
        Vec3 g[8] = {{-0.0170f, -0.118f, -0.105f}, {0.0170f, -0.118f, -0.105f}, {0.0170f, -0.036f, -0.010f}, {-0.0170f, -0.036f, -0.010f},
                     {-0.0170f, -0.118f, -0.060f}, {0.0170f, -0.118f, -0.060f}, {0.0170f, -0.036f, 0.020f}, {-0.0170f, -0.036f, 0.020f}};
        // (near face = the grip's back edge, far = its front edge)
        SgHex(g, wood, 0.40f, 0);
        AddBox({0, -0.121f, -0.083f}, {0.0180f, 0.0035f, 0.026f}, I, woodD, 0.3f, 0);   // the grip's cap
    }
    // (part 2: the butt stock - its length and drop are fitted to him, StockPoint)
    SgTaper(-0.002f, -0.245f, -0.007f, 0.0200f, 0.033f, -0.044f, 0.0230f, 0.058f, wood, 0.40f, 2);   // the stock
    SgTaper(-0.010f, -0.240f, 0.024f, 0.0120f, 0.003f, 0.012f, 0.0150f, 0.003f, woodL, 0.5f, 2);    // the comb's shine
    SgTaper(-0.245f, -0.265f, -0.044f, 0.0235f, 0.059f, -0.044f, 0.0235f, 0.059f, rubber, 0.15f, 2);  // the recoil pad
    Log("Shotgun model: %d vertices, %d triangles", g_sgMesh.nv, g_sgMesh.ni / 3);

    // a spent shell: red plastic with a brass head, +Z out of its mouth, origin in its middle
    g_mb = &g_shellMesh;
    Vec3 red = {0.82f, 0.10f, 0.08f}, redL = {0.96f, 0.30f, 0.22f};
    SgTube(0, 0, -0.008f, 0.0118f, 0.035f, 0.0118f, 8, red, 0.5f, 0, 2);
    AddBox({0, 0.0118f, 0.012f}, {0.0040f, 0.0007f, 0.020f}, I, redL, 0.6f, 0);
    SgTube(0, 0, -0.035f, 0.0128f, -0.008f, 0.0128f, 8, brass, 1.0f, 0, 1);
    Log("Shell model: %d vertices, %d triangles", g_shellMesh.nv, g_shellMesh.ni / 3);
}

// ------------------------------------------------------------------ tuning (Homer's; the others are fitted from their own bodies)
// (debug "sgtune <name>,<value>" changes any of these live; "sgtune" alone lists them)
// the draw out of his pocket
static float kSgPocketOut = 0.085f, kSgPocketFwd = 0.075f, kSgPocketUp = 0.045f;   // the trouser pocket from his right hip joint (x k)
static float kSgPullUp = 0.18f, kSgPullFwd = 0.08f;   // how far the pull brings the hand up / forward (x k)
static float kSgPullOut = 0.06f;                      // ... and out from his side (x k)
static float kSgDrawPath = 1.0f;                      // the draw and holster along the keyed path (kSgDrawKeys; 0: the old pull and swing)
static float kSgSink = 0.16f;                         // the pocket's mouth risen over the stock as it goes in / comes out (x k)
static float kSgPocketClear = 0.05f;                  // the pocket's mouth this far out of his belly's skin (x k)
static float kSgSwingOut = 0.10f, kSgSwingFwd = 0.12f; // the swing up into his hands goes round his belly: out and forward of the straight way
// the gun fitted to him (a rig control): the butt stock's length (share) and drop (rad) - his shoulder sits far back in his round
// chest and his arms are short, so the stock is shorter and drops more than a real one, to bring the rib up towards his face
static float kSgStockLen = 0.66f, kSgStockDrop = 0.22f;   // (0.66: the pump comes within his left arm's reach - research/anim)
// ... per model (a character-specific rig control: homer, bart, lisa, marge, apu - CharShape's order): fitted in the lab so his
// pump hand is kSgReachFit of his arm's reach from his shoulder when he aims (the "stock fit" in the sglog report solves it);
// Homer's arms are short for his shoulders, the others' stock is longer. (0: kSgStockLen)
static float kSgStockFit[5] = {0.66f, 1.09f, 0.94f, 1.10f, 1.15f};   // (fitted 2026-10-14: scenarios/sg_stockfit.py)
static float kSgReachFit = 0.89f;   // (Homer's tuned aim: pump arm bend ~55, Radical's 30-55)
static float g_sgStockLen = 0.66f;  // the stock length in use (this build: his model's)
static float g_sgStockSolve;        // (the pose report) the stock length that would put his pump hand at kSgReachFit (0 = not aimed)
// the shoulder pocket the butt sits in when he aims: inside his shoulder joint (share of its width), up from it (x k), and on his
// chest's front surface (his measured shape) plus the pad's thickness
static float kSgPkIn = 0.72f, kSgPkUp = 0.000f, kSgPkSkin = 0.020f;   // (pose checker: the pad 0.5 cm into his chest)
// the stance aimed: chest bladed (left side forward, about 30 degrees: a defensive stance - the gun runs across in front of him,
// not out on his right) and leaned forward over his feet ("nose over toes"); his head tipped forward and rolled over onto the stock
// (Homer's eye is 44 cm above his shoulder joint - a person's is 25 - so his cheek can only meet the comb with a real tilt)
static float kSgBlade = 0.60f, kSgAimLean = 0.18f;   // (0.60: his left shoulder forward - his short arms reach the pump)
static float kSgHeadDown = 0.28f, kSgHeadRoll = 0.32f;
// rig controls: his shoulders come forward to the gun (protraction, x k); the left one further if the pump is out of reach
static float kSgShRFwd = 0.030f, kSgShRUp = 0.012f, kSgShLFwd = 0.100f, kSgShLMax = 0.160f;
// the hands: the right one on the raked grip (turned down from the pistol's hold, rad), the left under the pump (share along it)
static float kSgGripPitch = -0.45f, kSgHoldAlong = 0.00f;   // (the rear of the pump: Homer's arms are 41 cm)
// the right elbow, where it isn't placed for a straight wrist (kSgRStraight): aimed, out from his chest (share) and down (share)
static float kSgElbowOut = 0.60f, kSgElbowDown = 0.80f;   // (Radical's grip arm: swivel 30-60 with a straight trigger wrist)
// the right fist round the grip (finger joints, thumb joints: rad; the pistol's 1.25 / 1.05 / -1.0 / -0.6)
static float kSgRCurl1 = 1.25f, kSgRCurl2 = 1.05f, kSgRThumb1 = 0.0f, kSgRThumb2 = 0.0f;
// the left hand on the fore-end (support hand): the palm on its lower left (rolled from straight under it, rad), the fingers
// across under it towards his right and a little forward (rad), wrapping up its right side; the thumb along its left side
// pointing to the muzzle. Along: the palm's centre from the wrist (share of the hand); thick: the wrist's depth under the skin (m)
static float kSgLRoll = 0.55f, kSgLFwd = 0.30f, kSgLAlong = 0.45f, kSgLThick = 0.030f;
static float kSgLCurl1 = 0.95f, kSgLCurl2 = 0.75f, kSgLThumb1 = 0.0f, kSgLThumb2 = 0.0f;   // (thumbs: AimThumb lays them)
static float kSgLElbowIn = -0.70f;    // the left elbow under the gun, a little in (+) or out (-): Radical's pump arm swivel 20-35 out
// the thumbs laid along the gun (their curl alone swings them in a plane that leaves Homer's long thumb sticking up): the left
// one along the fore-end's left side towards the muzzle, the right one over the grip to its left side (weights, 0 = as curled)
static float kSgLThumbAim = 0.0f, kSgRThumbAim = 1.0f;   // (left: 0 - with the wrist straight its own lie is right, and aiming it dragged out the vertex it shares with the forearm)
// the hands' turn on the forearms shared out to the forearm and the upper arm (SgTwistArm): what's left stays in the wrist.
// 0: none - the elbows stay pure hinges, as every one of Radical's 368 animations keeps them (research/rig/radical_baseline: the
// forearm never turns on the upper arm, the twist is all in the wrist, up to ~90 degrees). A share turned the forearm on its own
// bone, which the elbow's skin isn't weighted for: 16-63 degrees off the hinge stretched the elbow to 2.5-2.8x, where Radical's own
// bends of the same angle stretch it to 2.0-2.1x (the pose QA, tools/lab/qa)
static float kSgTwistFA = 0.0f, kSgTwistUA = 0.0f;      // (right arm: its hand turns 20-40 degrees on it - the wrist takes it)
// (left arm: cradling the fore-end palm-up turns the hand ~110 degrees on a hinged forearm; in the wrist alone that drags the vertex
// of his thumb that is skinned half to the forearm into a 3.5x spike (Radical's own animations do it too: 3.6x in hom_victory_large).
// Shared out, the elbow stretches 1.8x where Radical's same bend gives 1.6x and the thumb stays 1.8x: the lesser evil)
static float kSgTwistFAL = 0.55f, kSgTwistUAL = 0.35f;   // (A/B by the pose QA: .75 put the elbow 66 deg off its hinge, 1.26x Radical's stretch; .55: 53 deg, 1.16x, thumb 2.3x)
static float kSgLTwistCost = 0.30f;   // the left hand's hold search: a hold that needs less turning wins (per rad of twist)
// the elbows placed for a straight wrist (the forearm running into the hand as in his bind pose): share of the pole, left / right
static float kSgLStraight = 0.0f, kSgRStraight = 0.0f;   // (0: the elbows hang naturally - straight-wristed ones went up: "inverted elbow")
static float kSgLRollSearch = 1.0f;   // the left hand turns round the fore-end to the straightest wrist (0 = always kSgLRoll)
static float g_sgLRollUsed;           // (the pose report) the roll it picked
static float g_sgTwistR, g_sgTwistL, g_sgWristR, g_sgWristL;   // (the pose report) the twist found, the wrists' turn left (rad)
static const float kSgPumpHX = 0.030f;   // the fore-end's half width
// his head over the stock as he aims: moved towards the gun (m, x k) - his jaw is far from the comb, his neck short
static float kSgHeadShift = 0.0f;
// the raise lands with a little overshoot of the muzzle (rad/s kick of a spring) - weight
static float kSgSettle = 1.2f;
// the pump: the muzzle dips as the fore-end comes back (rad at the back), the gun pushes forward as it slams home (m, x k)
static float kSgPumpDip = 0.08f, kSgPumpStretch = 0.015f;
// ... the gun rolls a little towards his left hand as it racks (rad, at the back of the stroke), and his left shoulder goes back with
// the fore-end (m, x k): the stroke reads at the game's speed, not just in slow motion (review)
static float kSgPumpCant = 0.10f, kSgPumpShoulder = 0.040f;
// ... and the camera takes a small knock at each of the two clacks (pitch kick, rad/s) - from the aim camera behind his shoulder the
// fore-end is hidden by his body: the stroke is felt rather than seen there, as the shot is
static float kSgPumpCamBack = 0.9f, kSgPumpCamHome = 0.6f;
// the recoil's shove: the share of it that slides the gun on his shoulder (the pad compressing); the rest is his chest rocking back,
// which carries the seated stock with it (it slid the gun ~5 cm into his chest - the pose QA)
static float kSgBackRel = 0.25f;
// near a wall, a fence, a car: the gun comes down out of the aim and its muzzle goes down (low ready), rather than into it - a ray
// from the back of the receiver to kSgWallReach past the muzzle, against the game's own collision
static float kSgWallTilt = 0.95f, kSgWallReach = 0.08f, kSgWallOn = 1.0f;
// carried at the hip (review: "that's not how you hold a shotgun" of a high port - the gun upright before his chest, his left arm
// wrung across to it): his grip down / forward / out from his right shoulder joint (x k), the muzzle below level (dip, rad) and
// in towards his left across his belly (across, rad). (A low ready - muzzle down in front - his body hid from the follow camera.)
// (2026-10-17, review: "nobody holds it like that" of his left hand on the pump with the gun at 0.85 across - its forearm went through
// his belly to reach the pump in front of his middle. Turned further across, the gun's front end comes back to his left side, where
// his left hand rests on the barrel: the arm hangs outside him)
static float kSgLowDown = 0.14f, kSgLowOut = 0.22f, kSgLowDip = 0.15f, kSgLowAcross = 1.20f;   // (across his belly: both elbows bent)
static float kSgLowSide = 0.02f;
// carried, his left hand rests out on the barrel ahead of the pump (gun space z of the hold, x k) - not on the pump: g_sgOnPump (eased
// in SgUpdate) slides it back onto the pump for whatever needs it there (the draw's grab and rack, the raise and the aim, the rack
// after a shot) and out again after. Ahead of the pump the hand closes round the barrel over the magazine tube: narrower and deeper
// than the pump's box (centre, half width, half height)
static float kSgCarryHold = 0.43f;   // (behind the barrel band at 0.50: his fingers clear of it)
static const float kSgBarY = -0.0155f, kSgBarHX = 0.0175f, kSgBarHY = 0.0330f;
static float g_sgOnPump = 1.0f;   // 0 his left hand out on the barrel (carried) .. 1 on the pump
static float kSgBellyMargin = 0.015f;   // the gun kept this far outside his torso (his measured shape)
// the raise: the muzzle comes up ahead of the butt reaching his shoulder (how far ahead)
static float kSgRaiseLead = 1.35f;
static float kSgAimFollow = 20.0f;   // the aim's direction followed at this rate (1/s): quick, never a snap
static float kSgDrawTime = 1.20f, kSgHolsterTime = 0.50f;
static float kSgScaleSet;             // debug: a fixed size (0 = his own)
static float g_sgFreeze = -1.0f;      // debug "sgfreeze <0..1>": hold the draw at that point (-1 = off)
static float g_sgAimFreeze = -1.0f;   // debug "sgaim <0..1>": hold the raise at that point (-1 = off)
static int g_sgLog;                   // debug "sglog <frames>"
static float g_sgReachR, g_sgReachL;  // (the pose report) how far each hand fell short of the gun this build (m; 0 = on it)
static float g_sgPush;               // (the pose report) how far the gun was pushed out of his torso this build (m)
static float g_sgChinLift;           // (the pose report) how far his head was tipped back to rest his chin on the stock (rad)
static float kSgChinGap = 0.005f;    // his chin this far above the gun's top (gun space, m)
static float kSgNeckShare = 0.45f;   // the share of his head's turn his neck takes
static Vec3 g_sgAimLocal;   // the aim's direction in his chest's frame while he aims (lowering, the gun comes down with his body)
static bool g_sgAimed;      // ... this lowering follows an aim
static float g_sgRaiseW = 1.0f;   // 1 raising (or held up), 0 lowering: eased from G.sgRaising in SgUpdate
static float g_sgWA, g_sgLeftW, g_sgSwing, g_sgPull;   // (the pose trace) the aim pose's weight, the draw's left hand / swing / pull this build

// the butt stock's points as drawn: z stretched about the receiver's back by kSgStockLen, then the stock turned down by
// kSgStockDrop about its front (shotgun space)
// ... and its butt plate turned to lie flat on his chest (a fitted pad: his chest curves away from the gun's line at the pocket - the
// plain plate touched it at one corner, the far one 4.6 cm off, the pose QA): the stock's last 2.5 cm turn about the pad's middle,
// towards his left (yaw) and down (pitch), rad
static float kSgPadYaw = 0.0f, kSgPadPitch = 0.0f;
static Vec3 StockPoint(Vec3 p)
{
    if (p.z < -0.238f && (kSgPadYaw != 0.0f || kSgPadPitch != 0.0f)) {
        float w = Clampf((-0.238f - p.z) / 0.007f, 0.0f, 1.0f);
        Vec3 c = V3(0, -0.044f, -0.255f);
        Mat4 R = Mul(AxisAngle(V3(1, 0, 0), kSgPadPitch * w), AxisAngle(V3(0, 1, 0), kSgPadYaw * w));
        p = c + XformDir(p - c, R);
    }
    const float z0 = -0.02f, y0 = -0.02f;
    float z = z0 + (p.z - z0) * g_sgStockLen, y = p.y - y0;
    float c = cosf(kSgStockDrop), sn = sinf(kSgStockDrop);
    return V3(p.x, y0 + y * c + (z - z0) * sn, z0 - y * sn + (z - z0) * c);
}
static Vec3 ButtPoint() { return StockPoint(V3(0, -0.044f, -0.258f)); }   // the middle of the recoil pad

static float ShotgunScale(const Mat4* L)
{
    if (kSgScaleSet > 0.01f) return kSgScaleSet;
    float reach = Len(Row(L[27], 3)) + Len(Row(L[28], 3));
    float width = Len(Row(L[26], 3) - Row(L[19], 3));
    return Clampf(0.55f * reach / 0.411f + 0.45f * width / 0.381f, 0.70f, 1.12f);
}

// The draw, as shares of it (show 0 -> 1 over kSgDrawTime) - or the holster (show 1 -> 0 over kSgHolsterTime):
struct SgPhase {
    float reach;    // the right hand going to the pocket (0 the animation's, 1 at the pocket)
    float pull;     // the gun drawn up out of the pocket, hanging muzzle down from his hand
    float swing;    // swung up into both hands (1 = carried)
    float pump;     // racking the pump at the end of the draw (0..1, -1 not yet)
    float leftW;    // the left hand on the pump
    bool shown;     // the gun drawn at all
    bool pocket;    // the pocket's clip plane is on
    float sunk;     // the pocket's mouth risen over the stock's last part (0..1 of kSgSink): drawing, it comes up out of his fist
};
static SgPhase SgPhaseAt(float show, bool holster)
{
    SgPhase f = {};
    float s = g_sgFreeze >= 0.0f ? g_sgFreeze : show;
    if (!holster || g_sgFreeze >= 0.0f) {
        // (the pull is short and the swing starts inside it: a hand pulled up to his chest with the gun hanging under it had to turn
        // ~150 degrees on its forearm - the pose QA; now the gun swings out of the pocket as his hand comes up)
        f.reach = Smooth01(s / 0.18f);
        f.pull = Smooth01((s - 0.17f) / 0.23f);
        f.swing = kSgDrawPath > 0.0f ? Smooth01((s - 0.16f) / 0.48f) : Smooth01((s - 0.28f) / 0.34f);   // (the path: grab -> carry)
        f.pump = s >= 0.68f ? Clamp01((s - 0.68f) / 0.30f) : -1.0f;
        f.leftW = Smooth01((s - 0.50f) / 0.15f);
        f.shown = s >= 0.15f;
        f.pocket = s < (kSgDrawPath > 0.0f ? 0.45f : 0.64f);
        f.sunk = 1.0f - Smooth01((s - 0.15f) / 0.12f);
    } else {   // back into the pocket: swing down (1 -> 0.45), then down into it (0.45 -> 0)
        f.reach = Smooth01(s / 0.10f);
        f.pull = Smooth01((s - 0.05f) / 0.30f);
        f.swing = kSgDrawPath > 0.0f ? Smooth01((s - 0.10f) / 0.72f) : Smooth01((s - 0.22f) / 0.58f);   // (the path back to the pocket)
        f.pump = -1.0f;
        f.leftW = Smooth01((s - 0.62f) / 0.30f);
        f.shown = s > 0.06f;
        f.pocket = s < (kSgDrawPath > 0.0f ? 0.32f : 0.55f);
        f.sunk = 1.0f - Smooth01((s - 0.06f) / 0.14f);
    }
    if (show >= 0.999f && g_sgFreeze < 0.0f)
        f.reach = f.pull = f.swing = f.leftW = 1.0f, f.pump = -1.0f, f.shown = true, f.pocket = false, f.sunk = 0.0f;
    return f;
}
// the draw's and the holster's shares blended by an eased weight (SgUpdate): put away mid-draw or drawn again mid-holster, the two
// sets of curves at the same show differ - switching between them jumped the gun 40 cm along its path in a frame and the left hand
// off the pump (the pose QA's stress test: weapons switched mid-draw)
static float g_sgHolW;   // the holster's curves' weight (0 drawing, 1 putting away)
static SgPhase SgPhaseNow(float show)
{
    float w = g_sgFreeze >= 0.0f ? 0.0f : g_sgHolW;
    SgPhase d = SgPhaseAt(show, false), h = SgPhaseAt(show, true);
    if (w <= 0.001f) return d;
    if (w >= 0.999f) return h;
    SgPhase f = d;
    f.reach = d.reach + (h.reach - d.reach) * w;
    f.pull = d.pull + (h.pull - d.pull) * w;
    f.swing = d.swing + (h.swing - d.swing) * w;
    f.leftW = d.leftW + (h.leftW - d.leftW) * w;
    f.sunk = d.sunk + (h.sunk - d.sunk) * w;
    f.pump = w < 0.5f ? d.pump : h.pump;
    if (show < 0.999f) {   // (the thresholds moved with it: no pocket flickering on for a frame)
        f.shown = show >= 0.15f + (0.06f - 0.15f) * w;
        f.pocket = show < (kSgDrawPath > 0.0f ? 0.45f + (0.32f - 0.45f) * w : 0.64f + (0.55f - 0.64f) * w);
    }
    return f;
}

// the pump's offset (share of its travel) for a rack that started t seconds ago: pulled back hard (it stops with a clack
// against the receiver at kSgRackBack, a little bounce), a beat, pushed forward (a clack at kSgRackHome, a little bounce).
// The strokes are slow enough to read on screen (4 frames back, 3 forward at 30 fps; a real one is half that) and the
// sound (assets/build_sounds.py pump) has its clacks at the same two moments.
static const float kSgRackBack = 0.14f, kSgRackFwd = 0.20f, kSgRackHome = 0.32f, kSgRackEnd = 0.40f;
static float PumpOffset(float t)
{
    if (t < 0.0f || t > kSgRackEnd) return 0.0f;
    if (t < kSgRackBack) {
        float f = t / kSgRackBack;
        return f * f * (2.2f - 1.2f * f);   // (fast all the way to the stop)
    }
    if (t < kSgRackFwd) return 1.0f - 0.04f * sinf(PI * (t - kSgRackBack) / (kSgRackFwd - kSgRackBack));
    if (t < kSgRackHome) {
        float f = (t - kSgRackFwd) / (kSgRackHome - kSgRackFwd);
        return 1.0f - f * f * (2.2f - 1.2f * f);
    }
    return 0.03f * sinf(PI * (t - kSgRackHome) / (kSgRackEnd - kSgRackHome));
}
// the forward push of the whole gun as the fore-end slams home (0..1..0 over the bounce)
static float PumpStretch(float t)
{
    if (t < kSgRackHome - 0.03f || t > kSgRackEnd + 0.06f) return 0.0f;
    return sinf(PI * (t - (kSgRackHome - 0.03f)) / (kSgRackEnd + 0.09f - kSgRackHome));
}

// a gun frame: rotation (rows X, Y, D) and where the given point of the gun goes
static Mat4 SgFrame(Vec3 D, Vec3 upHint, Vec3 at, Vec3 gunPoint, float k)
{
    D = Norm(D);
    Vec3 Y = upHint - D * Dot(upHint, D);
    Y = Len(Y) > 0.15f ? Norm(Y) : Norm(Cross(D, V3(1, 0, 0)));
    Vec3 X = Norm(Cross(Y, D));
    Y = Cross(D, X);
    Mat4 R = Basis(X, Y, D);
    return WithTrans(R, at - XformDir(gunPoint * k, R));
}
static Mat4 SgBlend(const Mat4& a, const Mat4& b, float t)
{
    return WithTrans(BlendRot(Rot3(a), Rot3(b), t), Lerp(Row(a, 3), Row(b, 3), t));
}

// The draw's path - keyed the way an animator keys it and designed against his real body offline (tools/lab/qa/drawpath.py: no part
// of the gun inside him anywhere along it): the grip's place from his right hip joint (his right, up, forward; x k), the muzzle's and
// the gun's top's directions, at four keys; the fifth is the carry itself. Out of the pocket hanging along his leg, tipping out, a
// pendulum swing forward beside his belly, level in front of it, into his hands. (A pull up to his chest with the gun hanging under his
// hand turned the hand ~150 degrees on its forearm; a straight swing across went through his belly - the pose QA.)
static const struct { float t; Vec3 off, muzzle, top; } kSgDrawKeys[4] = {
    {0.00f, {0.17f, 0.05f, -0.02f}, {0.15f, -1.0f, 0.05f}, {0.25f, 0.0f, 1.0f}},
    {0.30f, {0.21f, 0.17f, 0.00f}, {0.30f, -1.0f, 0.20f}, {0.30f, 0.0f, 1.0f}},
    {0.55f, {0.24f, 0.24f, 0.13f}, {0.35f, -0.55f, 1.0f}, {0.10f, 1.0f, 0.45f}},
    {0.80f, {0.17f, 0.28f, 0.25f}, {-0.15f, -0.20f, 1.0f}, {0.0f, 1.0f, 0.10f}},
};
// the gun on the draw's path at t (0 the grab .. 1 the carry FL), his hip joint, his heading's right and forward
static Mat4 SgDrawPathFrame(float t, Vec3 hip, Vec3 rB, Vec3 fB, const Mat4& FL, float k)
{
    auto W = [&](Vec3 v) { return rB * v.x + UP * v.y + fB * v.z; };
    Vec3 P[5];
    Mat4 Rk[5];
    for (int i = 0; i < 4; i++) {
        P[i] = hip + W(kSgDrawKeys[i].off) * k;
        Rk[i] = Rot3(SgFrame(Norm(W(kSgDrawKeys[i].muzzle)), Norm(W(kSgDrawKeys[i].top)), V3(0, 0, 0), V3(0, 0, 0), 1.0f));
    }
    P[4] = XformPoint(kSgGrip * k, FL);
    Rk[4] = Rot3(FL);
    const float ts[5] = {kSgDrawKeys[0].t, kSgDrawKeys[1].t, kSgDrawKeys[2].t, kSgDrawKeys[3].t, 1.0f};
    t = Clamp01(t);
    int j = 0;
    while (j < 3 && t >= ts[j + 1]) j++;
    float u = Clamp01((t - ts[j]) / (ts[j + 1] - ts[j]));   // (linear within a key's span: eased per span, the gun stopped at every key)
    // the grip: Catmull-Rom through the five places (uniform in the key index)
    float x = (j + u);
    int i = (int)floorf(x);
    if (i > 3) i = 3;
    float w = x - i;
    Vec3 p0 = P[i > 0 ? i - 1 : 0], p1 = P[i], p2 = P[i + 1], p3 = P[i + 2 < 5 ? i + 2 : 4];
    Vec3 g = (p1 * 2.0f + (p2 - p0) * w + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * (w * w) + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * (w * w * w)) * 0.5f;
    Mat4 R = BlendRot(Rk[j], Rk[j + 1], u);
    return WithTrans(R, g - XformDir(kSgGrip * k, R));
}

static struct {
    Mat4 world;        // the gun (unscaled frame; points are x g_sg.k)
    float k;           // its size for him
    float pump;        // the pump's slide back (m, at his size)
    bool have, pocket;
    float clip[4];     // the pocket's plane (keep a x + b y + c z + d >= 0)
    Mat4 head;         // his head joint as posed (for the wink)
    float wink;        // how far his left eye is shut (0..1)
    bool haveHead;
} g_sg;

// (g_sgWink / g_sgWinkTarget: face.inc)

// ------------------------------------------------------------------ the pose
// A joint moved (world `delta`) with everything under it: its offset from its parent changes (a rig control - the shoulders
// coming forward; the skin round the joint follows, as it does when the game moves it)
static void ShiftJoint(Mat4* W, Mat4* L, int j, Vec3 delta)
{
    int par = g_parent[j];
    Vec3 dl = XformDir(delta, Transpose3(W[par]));
    SetRow(L[j], 3, Row(L[j], 3) + dl, 1);
    W[j] = Mul(L[j], W[par]);
}

// the gun's top along it (gun space, as drawn): the stock's comb (fitted to him: StockPoint), the receiver's rib, the barrel's rib
static float SgTopAt(float z)
{
    if (z > 0.132f) return 0.0205f;
    if (z > -0.02f) return 0.036f;
    Vec3 a = StockPoint(V3(0, 0.027f, -0.010f)), b = StockPoint(V3(0, 0.015f, -0.240f));
    float t = Clampf((z - a.z) / (b.z - a.z), 0.0f, 1.0f);
    return a.y + (b.y - a.y) * t;
}
// his skin's distance from a spine joint's axis (CharShape torso profiles: measured from his mesh, research/rig/torso_profile.py) at
// height h along the joint's +X and at angle ang round it (0 = its +Y); 0 where there is no torso
static float TorsoReach(const CharShape* c, int which, float h, float ang)
{
    if (!c || !c->torsoR[which]) return 0.0f;
    const int na = 16;
    int nh = c->torsoNH[which];
    float fi = (h - c->torsoH0[which]) / 0.05f;
    if (fi < 0.0f || fi > nh - 1) return 0.0f;
    int i = (int)floorf(fi);
    if (i > nh - 2) i = nh - 2;
    float ti = fi - i, fa = ang / (2 * PI) * na;
    fa -= floorf(fa / na) * na;
    int a = (int)floorf(fa) % na, a1 = (a + 1) % na;
    float ta = fa - floorf(fa);
    const float* r = c->torsoR[which];
    float v[4] = {r[i * na + a], r[i * na + a1], r[(i + 1) * na + a], r[(i + 1) * na + a1]};
    float w[4] = {(1 - ti) * (1 - ta), (1 - ti) * ta, ti * (1 - ta), ti * ta}, sum = 0, ws = 0;
    for (int k = 0; k < 4; k++)
        if (v[k] > 0.0f) sum += v[k] * w[k], ws += w[k];
    return ws > 0.25f ? sum / ws : 0.0f;   // (holes - no torso - don't average in; mostly holes: none)
}
// how far, and which way, the gun must move so none of `pts` is inside his torso: each point against both profiles (Spine_1: belly
// and hips, Spine_2: chest), each in its joint's posed frame, pushed straight out from the spine to the skin (+ margin)
static Vec3 SgBodyPush(const CharShape* c, const Mat4* W, const Vec3* pts, int n, float margin)
{
    Vec3 push = {0, 0, 0};
    for (int which = 0; c && which < 2; which++) {
        const Mat4& Wj = W[which ? 15 : 14];
        Mat4 inv = FullInverse(Wj);
        for (int i = 0; i < n; i++) {
            Vec3 q = XformPoint(pts[i], inv);
            float rho = sqrtf(q.y * q.y + q.z * q.z);
            if (rho < 1e-4f) continue;
            float R = TorsoReach(c, which, q.x, atan2f(q.z, q.y));
            if (R <= 0.0f || rho >= R + margin) continue;
            Vec3 d = XformDir(V3(0, q.y / rho, q.z / rho), Wj) * (R + margin - rho);
            if (Len(d) > Len(push)) push = d;
        }
    }
    return push;
}
// the depth of his chest's skin in front of Spine_2 at a height and a sideways offset in its frame (the profile, solved round)
static float ChestDepth(const CharShape* c, float h, float lat)
{
    float y = 0.25f;
    for (int it = 0; it < 4; it++) {
        float R = TorsoReach(c, 1, h, atan2f(lat, y));
        if (R <= fabsf(lat)) return 0.0f;
        y = sqrtf(R * R - lat * lat);
    }
    return y;
}
static double SgNow()   // (seconds: the solvers below pick up from their last answer)
{
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (double)q.QuadPart / (double)f.QuadPart;
}
// ---- against his real skin (bodymesh.inc: his mesh from the game's files, skinned with this build's pose)
// aimed: the recoil pad pressed this far into his skin at its deepest (a shirt and a shoulder give a little), nothing else of the
// stock deeper than kSgSeatTol; carried and in the draw: the gun kept this far out of him
static float kSgSeat = 1.0f, kSgSeatPress = 0.008f, kSgSeatTol = 0.002f, kSgMeshMargin = 0.004f;
static float g_sgSeat;          // (the pose report) how far the gun slid along its bore to seat the pad (m, + forward)
static float g_sgSeatPad;       // ... the pad's deepest point then (m, - into him)
static float g_sgSeatRest;      // ... the rest of the stock's deepest (m)
// the stock's points (gun space, as modelled - StockPoint fits them): the pad's back face (a 3 x 3 grid: 0-8), then the stock's
// edges at four stations and the grip's back
static Vec3 g_sgStockPts[48];
static int g_sgNStock, g_sgNPad;
static void SgStockPoints()
{
    if (g_sgNStock) return;
    int n = 0;
    for (int iy = 0; iy < 3; iy++)
        for (int ix = 0; ix < 3; ix++) g_sgStockPts[n++] = V3((ix - 1) * 0.020f, -0.044f + (iy - 1) * 0.050f, -0.265f);
    g_sgNPad = n;
    for (int s = 0; s < 4; s++) {   // (the taper: z -0.002 -> -0.245, centre -0.007 -> -0.044, half width 0.020 -> 0.023, height 0.033 -> 0.058)
        float z = -0.05f - s * 0.062f, t = (-0.002f - z) / 0.243f;
        float cy = -0.007f + (-0.044f + 0.007f) * t, hx = 0.020f + 0.003f * t, hy = 0.033f + 0.025f * t;
        const float c[8][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int i = 0; i < 8; i++) g_sgStockPts[n++] = V3(c[i][0] * hx, cy + c[i][1] * hy, z);
    }
    g_sgNStock = n;
}
// a stock point in the world for the gun frame F (unscaled) at size k
static Vec3 SgStockWorld(int i, const Mat4& F, float k) { return XformPoint(StockPoint(g_sgStockPts[i]) * k, F); }

// how far the gun must slide along its bore (+ forward, away from his shoulder) for the pad to press kSgSeatPress into his skin
// with nothing else of the stock in deeper than kSgSeatTol: a few Newton steps on the deepest points (each moves by its skin's
// normal along the bore)
static float SgSeatStock(const Mat4& F, float k)
{
    SgStockPoints();
    Vec3 D = Norm(Row(F, 2));
    // (his whole body but what holds the gun: the inside test needs his pieces closed - the shirt alone is open at the neck, where
    // the comb sank 3 cm into his chest unseen)
    const uint64_t mask = ~(kBodyArmL | kBodyHandL | kBodyHandR | (1ull << 27));
    static float s_last;
    static double s_at;
    double now = SgNow();
    float s = now - s_at < 0.3 ? s_last : 0.0f;   // (from the last build's: one or two steps finish it)
    s_at = now;
    static BodySide side[48];   // (each point inside or out: kept while it can't have changed)
    for (int it = 0; it < 3; it++) {
        Mat4 Fs = WithTrans(Rot3(F), Row(F, 3) + D * s);
        float padMin = 1e9f, restMin = 1e9f, padN = 1.0f, restN = 1.0f;
        for (int i = 0; i < g_sgNStock; i++) {
            Vec3 o;
            float d = BodyDist(SgStockWorld(i, Fs, k), mask, 0.12f * k, &o, &side[i]);
            if (d > 1e9f) continue;
            float along = fmaxf(0.35f, Dot(D, o));   // (how much of a slide along the bore moves this point out of his skin)
            if (i < g_sgNPad) {
                if (d < padMin) padMin = d, padN = along;
            } else if (d < restMin) restMin = d, restN = along;
        }
        if (padMin > 1e8f && restMin > 1e8f) break;   // (no skin near the stock: nothing to seat on)
        float want = padMin < 1e8f ? (-kSgSeatPress * k - padMin) / padN : -0.02f * k;   // (the pad onto his skin)
        if (restMin < 1e8f) want = fmaxf(want, (-kSgSeatTol * k - restMin) / restN);   // (... but nothing else into him)
        s = Clampf(s + Clampf(want, -0.03f * k, 0.03f * k), -0.06f * k, 0.10f * k);
        g_sgSeatPad = padMin, g_sgSeatRest = restMin;
        if (fabsf(want) < 0.001f) break;
    }
    s_last = s;
    return s;
}

// the gun kept out of his torso by his real skin: the stock's points, the grip, the receiver's and the fore-end's undersides; the
// deepest one in decides the push (a few rounds: one push can bring another point in)
// (padPress: how far the pad may press in - eased in with the raise: switched on at once, the push jumped the gun 33 m/s in a frame)
static Vec3 SgMeshPush(const Mat4& F, float k, float margin, uint64_t mask, float padPress = 0.0f)
{
    SgStockPoints();
    static const Vec3 extra[] = {{0, -0.121f, -0.083f}, {0.017f, -0.118f, -0.105f}, {-0.017f, -0.118f, -0.105f}, {0, -0.084f, 0.06f},
                                 {0.024f, -0.048f, 0.010f}, {-0.024f, -0.048f, 0.010f}, {0.024f, -0.048f, 0.130f}, {-0.024f, -0.048f, 0.130f},
                                 {0.030f, -0.056f, 0.150f}, {-0.030f, -0.056f, 0.150f}, {0.030f, -0.056f, 0.290f}, {-0.030f, -0.056f, 0.290f},
                                 {0, -0.048f, 0.40f}, {0, -0.048f, 0.52f}};
    const int ne = sizeof(extra) / sizeof(extra[0]);
    Vec3 push = {0, 0, 0};
    static BodySide side[64];
    for (int it = 0; it < 2; it++) {
        Mat4 Fp = WithTrans(Rot3(F), Row(F, 3) + push);
        float worst = margin;
        Vec3 wdir = {0, 0, 0};
        Vec3 fistR = XformPoint(kSgGrip * k, Fp);
        for (int i = 0; i < g_sgNStock + ne; i++) {
            Vec3 p = i < g_sgNStock ? SgStockWorld(i, Fp, k) : XformPoint(extra[i - g_sgNStock] * k, Fp);
            if (Len(p - fistR) < 0.06f * k) continue;   // (in his fist)
            Vec3 o;
            float d = BodyDist(p, mask, 0.20f * k, &o, &side[i]);   // (looking far enough to find a point deep in his belly)
            if (i < g_sgNPad) d += padPress;   // (the pad on his shoulder: the seat's, pressed in as far as that)
            if (d < worst) worst = d, wdir = o;
        }
        if (worst >= margin) break;
        push = push + wdir * (margin - worst);
    }
    return push;
}

// the gun turned about a point his hand holds (gun space) until none of it is inside him (the parts in `mask`): while it hangs from
// his right hand in the draw, its stock went up through his upper arm and side - the pose QA). Each round turns it by the smallest
// angle that brings its deepest point out to `margin`.
static Mat4 SgRotPush(const Mat4& F, float k, Vec3 hold, float margin, uint64_t mask, float hideY, float* turned = nullptr, float reach = 0.22f)
{
    SgStockPoints();
    static const Vec3 extra[] = {{0, -0.048f, 0.130f}, {0.024f, -0.048f, 0.130f}, {-0.024f, -0.048f, 0.130f}, {0, 0.034f, 0.060f},
                                 {0.030f, -0.056f, 0.290f}, {-0.030f, -0.056f, 0.290f}, {0, -0.034f, 0.40f}, {0, 0.020f, 0.45f},
                                 {0, -0.034f, 0.52f}, {0, 0.020f, 0.60f}};
    const int ne = sizeof(extra) / sizeof(extra[0]);
    Mat4 G = F;
    float total = 0.0f;
    for (int it = 0; it < 4; it++) {
        Vec3 piv = XformPoint(hold * k, G);
        float worst = margin;
        Vec3 wp = piv, wdir = {0, 0, 0};
        for (int i = 0; i < g_sgNStock + ne; i++) {
            Vec3 p = i < g_sgNStock ? SgStockWorld(i, G, k) : XformPoint(extra[i - g_sgNStock] * k, G);
            if (Len(p - piv) < 0.07f * k || p.y < hideY) continue;   // (his fist round the grip; still in the pocket: not drawn)
            Vec3 o;
            float d = BodyDist(p, mask, reach * k, &o);
            if (d < worst) worst = d, wp = p, wdir = o;
        }
        if (worst >= margin) break;
        Vec3 r = wp - piv, mv = wdir * (margin - worst + 0.002f * k);
        Vec3 ax = Cross(r, mv);
        float L = Len(ax), rl = Len(r);
        if (L < 1e-7f || rl < 1e-4f) break;
        float ang = fminf(0.35f, Len(mv) / rl);
        Mat4 R = AxisAngle(ax * (1.0f / L), ang);
        G = WithTrans(Mul(Rot3(G), R), piv + XformDir(Row(G, 3) - piv, R));
        total += ang;
    }
    if (turned) *turned = total;
    return G;
}

// a thumb (base joint t0 and the joint after it, turned together about the base) so the thumb as drawn points along `want`
// (world), by w: `mesh` is its mesh tip's direction in the base joint's frame (CharShape: Homer's thumb mesh is 40 degrees off
// the line to the next joint - aiming that line left it sticking straight up; Marge's and Apu's hang off the next joint)
static void AimThumb(Mat4* W, int t0, Vec3 mesh, Vec3 want, float w)
{
    Vec3 b = Row(W[t0], 3), cur = XformDir(mesh, W[t0]);
    if (w <= 0.001f || Len(cur) < 1e-4f || Len(want) < 1e-4f) return;
    cur = Norm(cur), want = Norm(want);
    Vec3 ax = Cross(cur, want);
    float sn = Len(ax);
    if (sn < 1e-5f) return;
    Mat4 R = AxisAngle(ax * (1.0f / sn), atan2f(sn, Dot(cur, want)) * w);
    for (int j = t0; j <= t0 + 1; j++)
        W[j] = WithTrans(Mul(Rot3(W[j]), R), b + XformDir(Row(W[j], 3) - b, R));
}

// The hand's turn about the forearm, taken up by the forearm and the upper arm rolling about their own bones (as a real arm
// turns its palm over): measured against the bind pose's hand-to-forearm relation, shareFA of it rolls the forearm and shareUA
// the upper arm (each about its bone, which doesn't move it). Why: one vertex of each of Homer's thumbs is skinned half to the
// forearm, and a hand turned on its forearm pulled it out into a long spike. Returns the hand's remaining turn on the forearm
// (rad); *twist = the twist that was found.
static float SgTwistArm(Mat4& Rua, Mat4& Rfa, const Mat4& Rhand, int jHand, int jFore, float shareUA, float shareFA, float* twist,
                        const float* nearTw = nullptr)
{
    Mat4 bindRel = Mul(Transpose3(Rot3(g_invBind[jHand])), Rot3(g_invBind[jFore]));   // the hand's axes in the forearm's, at bind
    auto Twist = [&](const Mat4& Rf) {   // the forearm's roll that would put it back in the bind relation with the hand
        Mat4 ideal = Mul(Transpose3(bindRel), Rot3(Rhand));
        Vec3 x = Norm(Row(Rf, 0)), a = Row(Rf, 1), b = Row(ideal, 1);
        b = b - x * Dot(b, x);
        return Len(b) < 1e-4f ? 0.0f : atan2f(Dot(Cross(a, b), x), Dot(a, b));
    };
    float t = Twist(Rfa);
    // (continuous with the last frame's - a turn going past half a turn read as minus half a turn and the shares flipped the elbow
    // the other way: 83 degrees off its hinge in one frame, the pose QA - and no more than 150 degrees of it shared out)
    if (nearTw) t = *nearTw + WrapPi(t - *nearTw);
    t = Clampf(t, -2.6f, 2.6f);
    if (twist) *twist = t;
    if (shareUA != 0.0f) Rua = WithTrans(Mul(Rot3(Rua), AxisAngle(Norm(Row(Rua, 0)), t * shareUA)), Row(Rua, 3));
    if (shareFA != 0.0f) Rfa = WithTrans(Mul(Rot3(Rfa), AxisAngle(Norm(Row(Rfa, 0)), t * shareFA)), Row(Rfa, 3));
    Mat4 dev = Mul(Mul(Rot3(Rhand), Transpose3(Rot3(Rfa))), Transpose3(bindRel));   // what's left: the hand's turn off the bind
    float c = (dev.at(0, 0) + dev.at(1, 1) + dev.at(2, 2) - 1.0f) * 0.5f;
    return acosf(Clampf(c, -1.0f, 1.0f));
}

// where his elbow would be for a straight wrist: the forearm running into the hand as it does in his bind pose, the wrist at pW
// with the hand's world rotation Rhand (Lhand: the wrist's local transform, its offset = the forearm's length)
static Vec3 SgStraightElbow(const Mat4& Rhand, Vec3 pW, int jHand, int jFore, const Mat4& Lhand)
{
    Mat4 bindRel = Mul(Transpose3(Rot3(g_invBind[jHand])), Rot3(g_invBind[jFore]));
    Mat4 Rfa = Mul(Transpose3(bindRel), Rot3(Rhand));
    return pW - XformDir(Row(Lhand, 3), Rfa);
}

// ---- the elbows placed for the wrist (an animator's rule: the elbow goes where the wrist is comfortable). An elbow's swivel round
// the line from its shoulder to its wrist (0 = straight down, + = out from his middle) is chosen for the least turn left in the wrist
// - the elbows are pure hinges, as in every one of Radical's animations, so the hand's turn on the forearm is the wrist's - kept near
// the designed swivel, within a person's range (never up over the line: the review's "inverted elbow"), and with the arm's skin out of his
// belly and chest (his real mesh). Smoothed in time: a better swivel is eased into, never snapped to.
static float kSgSwivelOpt = 1.0f;                          // 0: the designed elbow poles only (and the left hand's discrete roll search)
static float kSgSwivelPull = 0.3f;                         // how firmly the designed swivel holds (cost per rad^2; the wrist's turn: rad^2)
static float kSgSwivelOffW = 4.0f;                         // the elbow turned off its hinge (a shared twist) costs this much more than the wrist
// the ranges (rad): the grip arm 10-70 degrees (Radical's 30-60, a little room), the pump arm -45 (tucked under the gun) to +60
static float kSgSwivelMinR = 0.45f, kSgSwivelMaxR = 1.22f, kSgSwivelMinL = -0.20f, kSgSwivelMaxL = 1.05f;
static float kSgSwivelMin = -1.20f, kSgSwivelMax = 1.60f;  // (the full range: the draw, the carry)
static float kSgArmBodyW = 0.0f, kSgArmBodyR = 0.035f;   // (0: off - his forearms rest on his belly in the game's own poses too)   // his elbow and forearm kept this far (x k) from his belly's skin (cost per m^2)
static float kSgSwivelTau = 0.07f;                         // easing to a new best (s)
static float g_sgSwivel[2], g_sgSwivel0[2];                // (the pose report) the swivel used and the designed one, right / left (rad)
// the frame an elbow's swivel is measured in, round the shoulder-wrist line u: down (0) and out from his middle (+90 degrees)
static void SgSwivelFrame(Vec3 u, Vec3 sUp, Vec3 sRt, bool right, Vec3* dn, Vec3* out)
{
    Vec3 d0 = sUp * -1.0f - u * Dot(sUp * -1.0f, u);
    *dn = Len(d0) > 1e-3f ? Norm(d0) : Norm(Cross(u, sRt));
    Vec3 o = Norm(Cross(u, *dn));
    *out = Dot(o, sRt) * (right ? 1.0f : -1.0f) >= 0.0f ? o : o * -1.0f;
}
static float SgSwivelOf(Vec3 pole, Vec3 S, Vec3 Wp, Vec3 sUp, Vec3 sRt, bool right)
{
    Vec3 dn, out;
    SgSwivelFrame(Norm(Wp - S), sUp, sRt, right, &dn, &out);
    return atan2f(Dot(pole, out), Dot(pole, dn));
}
// an arm (shoulder S, wrist to be at Wp, bones a and b) with its elbow at swivel phi: the upper arm's and forearm's frames as pure
// hinges (the right arm's bones point back to their parents, the left's on to their children: the skeleton's own convention)
static Vec3 SgArmAt(Vec3 S, Vec3 Wp, float a, float b, bool right, Vec3 sUp, Vec3 sRt, float phi, Mat4* Rua, Mat4* Rfa)
{
    Vec3 u = Wp - S;
    float d = Len(u);
    u = d > 1e-4f ? u * (1.0f / d) : V3(0, 0, 1);
    d = Clampf(d, fabsf(a - b) * 1.05f + 0.01f, (a + b) * 0.995f);
    Vec3 dn, out;
    SgSwivelFrame(u, sUp, sRt, right, &dn, &out);
    Vec3 hinge;
    Vec3 e = TwoBone(S, u, d, a, b, dn * cosf(phi) + out * sinf(phi), dn, &hinge);
    Vec3 w = S + u * d;
    if (right) *Rua = FrameXZ(Norm(S - e), hinge), *Rfa = FrameXZ(Norm(e - w), hinge);
    else *Rua = FrameXZ(Norm(e - S), hinge), *Rfa = FrameXZ(Norm(w - e), hinge);
    return e;
}
struct SgArmProblem {
    Vec3 S, Wp, sUp, sRt;
    float a, b;
    bool right;
    Mat4 Rhand;
    int jHand, jFore;
    float shUA, shFA, phi0;
    bool body;
    float k, lo, hi;   // his size; the swivel's range (rad)
};
static float SgArmCost(const SgArmProblem& P, float phi, float* twOut = nullptr)
{
    Mat4 Rua, Rfa;
    Vec3 e = SgArmAt(P.S, P.Wp, P.a, P.b, P.right, P.sUp, P.sRt, phi, &Rua, &Rfa);
    float tw, dev = SgTwistArm(Rua, Rfa, P.Rhand, P.jHand, P.jFore, P.shUA, P.shFA, &tw);
    float off = (P.shFA - P.shUA) * tw;   // (a twist shared out to the forearm turns the elbow off its hinge by about this)
    float c = dev * dev + kSgSwivelOffW * off * off + kSgSwivelPull * (phi - P.phi0) * (phi - P.phi0);
    if (P.body && g_body.ok) {   // his elbow and forearm out of his belly (the upper arm against his side is how he is built: his
                                 // sleeve is 9 cm round the bone and sits in his chest in the game's own poses too)
        Vec3 w = P.S + Norm(P.Wp - P.S) * fminf(Len(P.Wp - P.S), (P.a + P.b) * 0.995f);
        const Vec3 pts[3] = {e, e + (w - e) * 0.35f, e + (w - e) * 0.7f};
        for (int i = 0; i < 3; i++) {
            float rad = kSgArmBodyR * P.k, dd = BodyDist(pts[i], kBodyTorso, 0.15f);
            if (dd < rad) c += kSgArmBodyW * (rad - dd) * (rad - dd);
        }
    }
    if (twOut) *twOut = tw;
    return c;
}
// the best swivel near the last one (a parabola through three costs round it): the pose changes little from one build to the next.
// A full scan (SgBestSwivel) runs when there is no recent value or every kSgRescan seconds.
static float SgLocalSwivel(const SgArmProblem& P, float last)
{
    const float h = 0.10f;
    float lo = P.hi > P.lo ? P.lo : kSgSwivelMin, hi = P.hi > P.lo ? P.hi : kSgSwivelMax;
    float x = Clampf(last, lo + h, hi - h);
    float c0 = SgArmCost(P, x - h), c1 = SgArmCost(P, x), c2 = SgArmCost(P, x + h);
    float den = c0 - 2.0f * c1 + c2, step;
    if (den > 1e-6f) step = Clampf(0.5f * h * (c0 - c2) / den, -1.5f * h, 1.5f * h);
    else step = c0 < c2 ? -1.5f * h : c2 < c1 ? 1.5f * h : 0.0f;
    return Clampf(x + step, lo, hi);
}
static float kSgRescan = 0.25f;
// the best swivel: a scan of the range, then golden-section round the best of it
static float SgBestSwivel(const SgArmProblem& P)
{
    const int N = 12;
    float lo = P.hi > P.lo ? P.lo : kSgSwivelMin, hi = P.hi > P.lo ? P.hi : kSgSwivelMax, best = P.phi0, bc = 1e30f;
    for (int i = 0; i <= N; i++) {
        float p = lo + (hi - lo) * i / N, c = SgArmCost(P, p);
        if (c < bc) bc = c, best = p;
    }
    float x0 = fmaxf(lo, best - (hi - lo) / N), x1 = fminf(hi, best + (hi - lo) / N);
    for (int it = 0; it < 10; it++) {
        float m1 = x1 - 0.618f * (x1 - x0), m2 = x0 + 0.618f * (x1 - x0);
        if (SgArmCost(P, m1) < SgArmCost(P, m2)) x1 = m2;
        else x0 = m1;
    }
    return 0.5f * (x0 + x1);
}
// eased in time (per arm / value): a new best is approached over kSgSwivelTau; after a pause (a new draw) it is taken at once
static float g_sgEaseVal[4];
static double g_sgEaseAt[4], g_sgScanAt[4];
// whether a value's search should start fresh (a full scan): none in the last 0.3 s, or the last full scan kSgRescan ago
static bool SgFresh(int slot)
{
    double now = SgNow();
    if (now - g_sgEaseAt[slot] > 0.3 || now - g_sgScanAt[slot] > kSgRescan) {
        g_sgScanAt[slot] = now;
        return true;
    }
    return false;
}
static float SgEase(int slot, float want)
{
    float* val = g_sgEaseVal;
    double* at = g_sgEaseAt;
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    double now = (double)q.QuadPart / (double)f.QuadPart, dt = now - at[slot];
    at[slot] = now;
    if (dt > 0.3 || dt < 0.0) return val[slot] = want;
    float a = 1.0f - expf(-(float)dt * g_gunSlow / kSgSwivelTau);
    return val[slot] += (want - val[slot]) * a;
}

// An arm on its way between the game's own pose (w 0) and ours (w 1): its wrist goes from the game's place to ours, its hand turns from
// the game's to ours, its elbow's swivel and the twist shared to it go between the two, and the arm is solved (pure hinge) at each
// step. (Blending each joint's own rotation turned the arm through itself on the way: his left upper arm folded 13 cm into itself as
// the holster let go of the fore-end - the pose QA.) The last bit (w under 0.12) hands over to the game's own joints.
static void SgArmBlend(Mat4* W, const Mat4* L, int sh, int el, int wr, bool right, Vec3 S, Vec3 pW, const Mat4& Rh, float phi, float shUA,
                       float shFA, float a, float b, Vec3 sUp, Vec3 sRt, float w, float* twist, float* wristDev, Mat4* RuaOut = nullptr,
                       bool steadyG = false)
{
    Vec3 pG = Row(W[wr], 3), eG = Row(W[el], 3);
    Vec3 u = Norm(pG - S), off = (eG - S) - u * Dot(eG - S, u);
    float e = Clamp01(w);
    float phiG = Len(off) > 0.015f ? SgSwivelOf(off, S, pG, sUp, sRt, right) : phi;   // (a straight arm: any swivel is the game's)
    // (steadyG, the donut and the can: the game's arm all but straight - swinging as he walks, drunk - has no swivel to speak of: its
    // elbow's way off the line turned 70 deg in a frame and the blend into the item's arm swung his elbow through his side for a frame
    // (the arm QA, a donut after a can). The game's swivel counts as his arm bends)
    if (steadyG) phiG = phi + WrapPi(phiG - phi) * Clamp01((Len(off) / (a + b) - 0.04f) / 0.08f);
    Vec3 pT = Lerp(pG, pW, e);
    Mat4 RhT = BlendRot(Rot3(W[wr]), Rot3(Rh), e);
    float phiT = phiG + WrapPi(phi - phiG) * e;
    Mat4 Rua, Rfa;
    SgArmAt(S, pT, a, b, right, sUp, sRt, phiT, &Rua, &Rfa);
    static float lastTw[40];
    static double lastAt[40];
    double now = SgNow();
    bool recent = now - lastAt[wr] < 0.3;
    float es = Smooth01(e / 0.4f);   // (the twist shared out early in the hand's way: half shared, the wrist's half spiked the thumb)
    float tw, dev = SgTwistArm(Rua, Rfa, RhT, wr, el, shUA * es, shFA * es, &tw, recent ? &lastTw[wr] : nullptr);
    lastTw[wr] = tw, lastAt[wr] = now;
    if (twist) *twist = tw;
    if (wristDev) *wristDev = dev;
    float wf = Smooth01(w / 0.12f);
    int par = g_parent[sh];
    W[sh] = BlendLocal(L[sh], Mul(Rua, Transpose3(W[par])), W[par], wf);
    W[el] = BlendLocal(L[el], Mul(Rfa, Transpose3(Rua)), W[sh], wf);
    W[wr] = BlendLocal(L[wr], Mul(RhT, Transpose3(Rfa)), W[el], wf);
    if (RuaOut) *RuaOut = Rua;
}

// (the pose report) where an elbow is from the line between its shoulder and wrist, in cm in his chest's frame: below it (+ = down,
// as an elbow should hang; - = up, inverted), out from it (+ = away from his middle), back from it (+ = behind)
static float ElbowOff(const Mat4* W, int sh, int el, int wr, Vec3 up, Vec3 rt, Vec3 fwd, int what)
{
    Vec3 s = Row(W[sh], 3), e = Row(W[el], 3), w = Row(W[wr], 3), d = Norm(w - s);
    Vec3 off = (e - s) - d * Dot(e - s, d);
    float side = Dot(s - Row(W[15], 3), rt) >= 0.0f ? 1.0f : -1.0f;   // (his right shoulder: out is +right; left: out is -right)
    return 100.0f * (what == 0 ? -Dot(off, up) : what == 1 ? Dot(off, rt) * side : -Dot(off, fwd));
}

// (the pose report) an arm measured as the research measured Radical's own animations (research/anim, radical_style.json): elbow
// swivel round the shoulder-wrist line (0 = straight down, +90 = straight out, -90 = in toward him, 180 = up), bend (0 = straight),
// shoulder-to-wrist reach (cm), upper-arm elevation (0 hanging, 90 horizontal) and azimuth (0 to the side, 90 forward), and the
// hand's angle off the forearm (deg; Homer's bind 0.9)
static void ArmStats(const Mat4* W, int sh, int el, int wr, int mb, Vec3 up, Vec3 rt, Vec3 fwd, float* o)
{
    Vec3 S = Row(W[sh], 3), E = Row(W[el], 3), Wr = Row(W[wr], 3), M = Row(W[mb], 3);
    float side = Dot(S - Row(W[15], 3), rt) >= 0.0f ? 1.0f : -1.0f;
    Vec3 out = rt * side, a = Norm(Wr - S), off = (E - S) - a * Dot(E - S, a);
    Vec3 dn = (up * -1.0f) - a * Dot(up * -1.0f, a);
    float sw = 0.0f;
    if (Len(dn) > 0.3f) {
        dn = Norm(dn);
        Vec3 ou = Norm(Cross(a, dn));
        if (Dot(ou, out) < 0.0f) ou = ou * -1.0f;
        sw = atan2f(Dot(off, ou), Dot(off, dn)) * 57.2958f;
    }
    Vec3 ua = Norm(E - S), fa = Norm(Wr - E), hd = Norm(M - Wr);
    Vec3 h = (E - S) - up * Dot(E - S, up);
    o[0] = sw;
    o[1] = acosf(Clampf(Dot(ua, fa), -1.0f, 1.0f)) * 57.2958f;
    o[2] = Len(Wr - S) * 100.0f;
    o[3] = acosf(Clampf(Dot(ua, up * -1.0f), -1.0f, 1.0f)) * 57.2958f;
    o[4] = atan2f(Dot(h, fwd), Dot(h, out)) * 57.2958f;
    o[5] = acosf(Clampf(Dot(hd, fa), -1.0f, 1.0f)) * 57.2958f;
}

// Built every time he is skinned, from the game's animated skeleton:
//  1. the torso: aimed, the chest turned a little (left side forward) and leaned forward over his feet; the recoil rocks it
//     back and turns the right shoulder back (the spine only: his feet stay planted)
//  2. the head: looks at the target, then tips forward and over onto the stock
//  3. the shoulders come forward to the gun (protraction)
//  4. the gun: hanging from his hand as it comes out of the pocket -> carried at low ready (blended about the grip, which his hand
//     holds) -> raised into his shoulder (blended about the butt, which pivots there: the muzzle first, then the butt into the
//     pocket); the recoil shoves it back and kicks the muzzle up; the pump slides along it
//  5. the right hand on the grip, the left under the pump, each by two-bone IK from its shoulder, the elbows placed so the wrists
//     stay natural; out of reach, the gun comes back into his shoulder (right) or his shoulder reaches further (left)
static void PtSnapshot();   // (posetrace.inc: the state as this build used it, for the trace)
static void BuildPoseShotgun(const PoserPose& P)
{
    float show = Clamp01(G.show), aim = g_sgAimFreeze >= 0.0f ? g_sgAimFreeze : G.aim;
    int n = P.numJoints < 40 ? P.numJoints : 40;
    g_sg.have = false;
    if (!LoadInvBind(P.numJoints)) {
        g_skin.active = false;
        return;
    }
    static Mat4 W[40], L[40];
    for (int j = 0; j < n; j++) {
        W[j] = ToMat(P.world[j]);
        L[j] = ToMat(P.object[j]);
    }
    g_skin.handK = n > 29 ? Clampf(Len(Row(L[29], 3)) / 0.116f, 0.4f, 1.3f) : 1.0f;
    auto Rebuild = [&](int j) { W[j] = Mul(L[j], W[g_parent[j]]); };
    G.bodyYaw = atan2f(-P.world[0][8], -P.world[0][10]);
    G.haveBody = true;
    G.knocked = Norm(Row(W[17], 3) - Row(W[0], 3)).y < 0.8f;
    bool roots[40] = {};
    G.reachTarget = 1.0f;
    float k = ShotgunScale(L);
    bool torso = g_torsoOK && g_torsoOn;
    SgPhase ph = SgPhaseNow(show);
    g_sgLeftW = ph.leftW, g_sgSwing = ph.swing, g_sgPull = ph.pull;
    float rackT = ph.pump >= 0.0f ? ph.pump * 0.30f * kSgDrawTime : G.sgPumpT;   // (the draw's rack in real seconds: its sound's clacks)
    float rack = PumpOffset(rackT);
    bool ready = show >= 0.999f && g_sgFreeze < 0.0f;
    const CharShape* shape = CharShapeNow();
    bool bodyOK = BodyLoad();   // (his real mesh: bodymesh.inc)
    {   // his model's stock length (the table; sgtune "stocklen" sets his entry)
        int ci = shape ? (int)(shape - kCharShapes) : -1;
        g_sgStockLen = ci >= 0 && ci < 5 && kSgStockFit[ci] > 0.0f ? kSgStockFit[ci] : kSgStockLen;
    }
    g_sgStockSolve = 0.0f;

    // what he aims at, eased (the gun has weight): seen from his chest, its direction follows the crosshair's at kSgAimFollow per second
    // and its distance in proportion - the crosshair's point jumps (the depth probe's hit coming and going, near walls and far sky; the
    // aim swinging to the camera's heading as it starts) and the gun went with it, 26 degrees in one frame (the pose QA)
    Vec3 aimP = G.aimPoint;
    {
        static Vec3 sDir;
        static float sDist;
        static double sAt;
        Vec3 c = Row(W[15], 3), d = G.aimPoint - c;
        float L = fmaxf(0.5f, Len(d));
        double now = SgNow();
        float dt = (float)(now - sAt) * g_gunSlow;
        if (now - sAt > 0.3 || dt < 0.0f || Len(sDir) < 0.5f) sDir = d * (1.0f / L), sDist = L;
        else {
            float a = 1.0f - expf(-dt * kSgAimFollow);
            sDir = Norm(sDir + (d * (1.0f / L) - sDir) * a);
            sDist = expf(logf(sDist) + (logf(L) - logf(sDist)) * a);
        }
        sAt = now;
        aimP = c + sDir * sDist;
    }

    if (!G.aiming && G.aim < 0.002f) g_sgAimed = false;   // (the lowering after an aim is over: a quick shot from the hip holds nothing)

    // 1. the torso
    float wA = 0.0f, chestYaw = 0.0f, aimPitch = 0.0f;
    if (ready && aim > 0.001f && G.haveAim) {
        Vec3 D0 = Norm(aimP - Row(W[15], 3));
        G.reachTarget = ReachFor(WrapPi(Heading(D0) - G.bodyYaw), kSgBlade, &chestYaw);
        wA = aim * Clamp01(G.reach.x) * (1.0f - Clamp01(G.sgWallA.x));   // (a wall where the aimed gun would be: lowered)
        aimPitch = asinf(Clampf(D0.y, -1, 1));
    }
    g_sgWA = wA;
    // the raise and the lowering in an order, nothing arriving at once: raising, the muzzle comes up onto the line first (the
    // front hand leads), the butt slides into the pocket, and his head comes down onto the stock last; lowering, the muzzle
    // drops about the pocket first and his head comes up off the stock last
    // (the raise's order and the lowering's are blended by an eased weight, never switched: the flag behind it blinked off for a frame
    // or two mid-raise and the gun swung 20 degrees back and forth - the pose QA)
    float rw = g_sgAimFreeze >= 0.0f ? 1.0f : g_sgRaiseW;
    auto RL = [&](float r, float l) { return l + (r - l) * rw; };
    float wGun = RL(Smooth01(wA * kSgRaiseLead), Smooth01((wA - 0.25f) / 0.75f));
    float wPos = RL(Smooth01((wA - 0.15f) / 0.85f), Smooth01((wA - 0.10f) / 0.90f));
    float wHead = RL(Smooth01((wA - 0.35f) / 0.65f), Smooth01(wA / 0.60f));
    float lean = (SoftClamp(aimPitch * kLean, kLeanLo, kLeanHi) + kSgAimLean) * wA + G.kickChest.x * 0.9f;
    float twist = chestYaw * wA - G.sgTwist.x;
    if (torso && (fabsf(lean) > 1e-4f || fabsf(twist) > 1e-4f)) {
        float twisted = 0;
        for (int j = 14; j <= 15; j++) {
            if (j == 15) Rebuild(15);
            float sh = j == 14 ? kTwist1 : 1 - kTwist1;
            W[j] = RotateInPlace(W[j], YawPitchAt(G.bodyYaw + twisted, twist * sh, lean * sh));
            twisted += twist * sh;
        }
        for (int j = 16; j <= 18; j++) Rebuild(j);
        roots[14] = true;
    }
    Mat4 headW0 = W[17];   // (his head before it is turned for the gun: the neck takes a share of the turn, below)
    // 2. the head: at the target, then over towards the gun, forward and rolled onto the stock (about his neck)
    Vec3 neck = Row(W[16], 3);
    if (torso && wA > 0.001f) {
        if (kSgHeadShift != 0.0f && wHead > 0.001f) {   // (a rig control: his neck moved over towards the gun)
            ShiftJoint(W, L, 16, Norm(Row(W[15], 2)) * (kSgHeadShift * k * wHead));
            Rebuild(17);
            neck = Row(W[16], 3);
        }
        Vec3 fH = Norm(Row(W[17], 0)), dH = Norm(aimP - Row(W[17], 3));
        float hy = SoftClamp(WrapPi(Heading(dH) - Heading(fH)), -0.9f, 0.9f) * kHeadYaw * wA;
        float hp = SoftClamp((aimPitch - lean) * kHeadPitch, -0.45f, 0.40f) * wA;
        W[17] = RotateInPlace(W[17], YawPitchAt(Heading(fH), hy, hp));
        Vec3 sUp0 = Norm(Row(W[15], 0)), sF0 = Norm(Row(W[15], 1)), sR0 = Norm(Row(W[15], 2));
        float down = kSgHeadDown * wHead - G.sgFlip.x * 0.35f * wHead;   // (the recoil tips it back with the comb)
        Mat4 R = Mul(AxisAngle(sR0, down), AxisAngle(Norm(sF0 - sUp0 * Dot(sF0, sUp0)), kSgHeadRoll * wHead));
        W[17] = Mul(W[17], Mul(Mul(Translation(neck * -1.0f), R), Translation(neck)));
        Rebuild(18);
        roots[14] = true;
    }
    Rebuild(19);
    Rebuild(26);
    // his frame now: Spine_2 (+X up, +Y forward, +Z his right), shoulders
    Vec3 s2 = Row(W[15], 3), sUp = Norm(Row(W[15], 0)), sFwd = Norm(Row(W[15], 1)), sRt = Norm(Row(W[15], 2));
    Vec3 fB = HeadingDir(G.bodyYaw), rB = Norm(Cross(UP, fB));   // his hips' forward / right, level (the pocket of his trousers)
    // 3. the shoulders come forward to the gun (the left one further below, if the pump is out of its reach)
    if (wA > 0.001f) ShiftJoint(W, L, 26, (sFwd * kSgShRFwd + sUp * kSgShRUp) * (k * wA));
    float shL = kSgShLFwd * k * Clamp01(wA + 0.4f * ph.leftW * (1.0f - wA)) - kSgPumpShoulder * k * rack;   // (back with the pump)
    if (fabsf(shL) > 1e-4f) ShiftJoint(W, L, 19, sFwd * shL);
    Vec3 sR = Row(W[26], 3), sL = Row(W[19], 3);
    for (int j = 20; j <= 25; j++) Rebuild(j);   // (the arms as the game has them, from their shifted shoulders)
    for (int j = 27; j <= 32; j++) Rebuild(j);
    if (bodyOK) BodySkin(W, n);   // his skin as posed so far (the torso, the head; the arms are still the game's)
    g_sgSeat = g_sgSeatPad = g_sgSeatRest = 0.0f;
    double tBody0 = ProfNow();

    // 4. the gun
    //  the pocket: on his chest's front surface (his measured shape) inside his right shoulder joint
    float latR = Dot(sR - s2, sRt), hR = Dot(sR - s2, sUp);
    float pkLat = latR * (1.0f - kSgPkIn), pkUp = hR + kSgPkUp * k;
    //  (his chest's skin there: the Spine_2 torso profile, measured from his mesh; in front of the shoulder joint if none)
    float depth = shape ? ChestDepth(shape, pkUp, pkLat) : 0.0f;
    if (depth < 0.02f) depth = Dot(sR - s2, sFwd) + 0.10f * k;
    Vec3 pocket = s2 + sRt * pkLat + sUp * pkUp + sFwd * (depth + kSgPkSkin);
    Vec3 butt = ButtPoint();
    //  the trouser pocket, and the gun hanging from his hand as it comes out (muzzle down, its top to the front, tipping forward)
    Vec3 hipR = n > 9 ? Row(W[9], 3) : Row(W[0], 3);
    Vec3 tpocket = hipR + rB * (kSgPocketOut * k) + fB * (kSgPocketFwd * k) + UP * (kSgPocketUp * k);
    if (bodyOK) {   // (his belly hangs over his trouser pocket: the pocket's mouth is moved out to the side until it is clear of his skin -
                    // the gun came out of it straight into his belly, the pose QA)
        for (int it = 0; it < 12; it++) {
            float d = BodyDist(tpocket + UP * (0.10f * k), ~(kBodyArmR | kBodyArmL | kBodyHandR | kBodyHandL | kBodyLegs), 0.20f);
            if (d > kSgPocketClear * k) break;
            tpocket = tpocket + rB * (0.012f * k);
        }
    }
    Vec3 gripH = tpocket + UP * (kSgPullUp * k * ph.pull) + fB * (kSgPullFwd * k * ph.pull) + rB * (kSgPullOut * k * ph.pull);
    Mat4 FH = SgFrame(Norm(UP * -1.0f + fB * (0.25f * ph.pull) + rB * 0.10f), fB, gripH, kSgGrip, k);
    //  carried at the hip: his right hand on the grip low at his right side, in front of his belly (placed from his shoulder joint,
    //  so his short arm always reaches it), the stock along his forearm, the muzzle forward - a little in across his belly and a
    //  little down - and his left hand under the fore-end: the way a man holds a shotgun ready (a reference photo)
    //  (level to the world and his heading, not his chest: Homer's chest joint is pitched ~20 degrees forward, which tipped the
    //  muzzle into the ground)
    Vec3 flat = Norm(fB * cosf(kSgLowAcross) - rB * sinf(kSgLowAcross));
    Vec3 dLow = Norm(flat * cosf(kSgLowDip) - UP * sinf(kSgLowDip));
    Vec3 gripLow = sR - UP * (kSgLowDown * k) + fB * (kSgLowOut * k) + rB * (kSgLowSide * k);
    Mat4 FL = SgFrame(dLow, UP, gripLow, kSgGrip, k);
    //  the draw: from hanging to low ready about the grip (his hand holds it all the way)
    Mat4 F = FL;
    if (kSgDrawPath > 0.0f && ph.swing < 1.0f) {   // the draw's keyed path (ph.swing is its time here)
        F = SgDrawPathFrame(ph.swing, hipR, rB, fB, FL, k);
    } else if (ph.swing < 1.0f) {   // (the grip on an arc out round the front of his belly - straight across, the gun went through it)
        Vec3 gH = XformPoint(kSgGrip * k, FH), gL = XformPoint(kSgGrip * k, FL);
        Vec3 gM = Lerp(gH, gL, 0.5f) + rB * (kSgSwingOut * k) + fB * (kSgSwingFwd * k);
        float t = ph.swing;
        Vec3 g = Lerp(Lerp(gH, gM, t), Lerp(gM, gL, t), t);
        Mat4 R = t <= 0.0f ? Rot3(FH) : BlendRot(Rot3(FH), Rot3(FL), t);
        F = WithTrans(R, g - XformDir(kSgGrip * k, R));
    }
    //  while it hangs from his hand (the draw's pull and swing, the holster's), turned in his fist to keep it out of his arm and side
    float drawTurn = 0.0f;
    if (kSgDrawPath <= 0.0f && bodyOK && ph.shown && ph.swing < 1.0f && kSgMeshMargin >= 0.0f) {   // (a straight push out of his belly first)
        Vec3 out = SgMeshPush(F, k, kSgMeshMargin * k, ~(kBodyArmR | kBodyArmL | kBodyHandR | kBodyHandL | kBodyLegs));
        F = WithTrans(Rot3(F), Row(F, 3) + out);
    }
    if (kSgDrawPath <= 0.0f && bodyOK && ph.shown && ph.swing < 1.0f && kSgMeshMargin >= 0.0f)
        F = SgRotPush(F, k, kSgGrip, kSgMeshMargin * k, ~(kBodyArmL | kBodyHandL | kBodyHandR), ph.pocket ? tpocket.y + 0.03f * k : -1e9f,
                      &drawTurn);
    //  aimed: the butt in the pocket, the rib pointing at what he aims at; raised about the butt (the muzzle comes up first)
    if (wA > 0.001f) {
        Vec3 dA = Norm(aimP - (pocket + sUp * 0.08f * k));
        Mat4 FA = SgFrame(dA, UP, pocket, butt, k);
        for (int it = 0; it < 2; it++) {
            dA = Norm(aimP - XformPoint(kSgRear * k, FA));
            FA = SgFrame(dA, UP, pocket, butt, k);
        }
        // lowering, the gun comes down with his body: its last aim held in his chest's frame (he turns to where he walks as he lets
        // go of the aim, and the gun swept across in front of him - through his left arm - towards his new heading: the pose QA)
        {
            Vec3 loc = V3(Dot(dA, sRt), Dot(dA, sUp), Dot(dA, sFwd));
            if (G.aiming) g_sgAimLocal = loc, g_sgAimed = true;
            else if (g_sgAimed) {
                dA = Norm(sRt * g_sgAimLocal.x + sUp * g_sgAimLocal.y + sFwd * g_sgAimLocal.z);
                FA = SgFrame(dA, UP, pocket, butt, k);
            }
        }
        if (bodyOK && kSgSeat > 0.0f) {   // the pad seated on his real skin: slid along the bore (the aim's line stays)
            g_sgSeat = SgSeatStock(FA, k) * kSgSeat;
            FA = WithTrans(Rot3(FA), Row(FA, 3) + Norm(Row(FA, 2)) * g_sgSeat);
        }
        // the butt mounts as a shooter's does: up to the pocket's height while still out in front of it, then back into it
        // ("clear the armpit, then the cheek"); lowering, out of the pocket first, then down
        Vec3 b0 = XformPoint(butt * k, F), b1 = XformPoint(butt * k, FA), dB = b1 - b0;
        Vec3 dIn = sFwd * Dot(dB, sFwd), dUp = dB - dIn;
        float wUp = RL(Smooth01((wA - 0.05f) / 0.60f), Smooth01(wA / 0.60f));
        float wIn = RL(Smooth01((wA - 0.45f) / 0.50f), Smooth01((wA - 0.55f) / 0.45f));
        wPos = wIn;
        Mat4 R = BlendRot(Rot3(F), Rot3(FA), wGun);
        F = WithTrans(R, b0 + dUp * wUp + dIn * wIn - XformDir(butt * k, R));
    }
    //  never inside him: the stock, the grip and the fore-end's underside kept outside his torso (his belly is 38 cm deep in front
    //  of his chest joint: the carry's butt and the draw's swing went into it); fading out as the butt seats in the pocket
    if (bodyOK && kSgMeshMargin >= 0.0f && !(kSgDrawPath > 0.0f && ph.swing < 1.0f)) {   // (his real skin; the draw's path is clear by design)
        Vec3 push = SgMeshPush(F, k, kSgMeshMargin * k, ~(kBodyArmR | kBodyArmL | kBodyHandR | kBodyHandL),
                               wPos * (kSgSeatPress + kSgMeshMargin + 0.006f) * k);   // (raised: the pad presses in, as the seat has it)
        F = WithTrans(Rot3(F), Row(F, 3) + push);
        g_sgPush = Len(push);
    } else if (shape && kSgBellyMargin >= 0.0f) {
        Vec3 pts[7] = {XformPoint(butt * k, F), XformPoint(StockPoint(V3(0, -0.10f, -0.258f)) * k, F),
                       XformPoint(StockPoint(V3(0, 0.012f, -0.258f)) * k, F), XformPoint(StockPoint(V3(0, -0.04f, -0.15f)) * k, F),
                       XformPoint(V3(0, -0.121f, -0.083f) * k, F), XformPoint(V3(0, -0.084f, 0.06f) * k, F),
                       XformPoint(V3(0, kSgPumpY - kSgPumpHY, kSgPumpZ0) * k, F)};
        Vec3 push = SgBodyPush(shape, W, pts, 7, kSgBellyMargin * k) * (1.0f - wPos);
        F = WithTrans(Rot3(F), Row(F, 3) + push);
        g_sgPush = Len(push);
    }
    //  about the butt: the recoil's muzzle flip and the raise's settle (up), the pump's dip as the fore-end comes back (down); then
    //  the recoil shoves it straight back into his shoulder, and the fore-end slamming home pushes it forward a little
    //  (the muzzle up about the pad's heel - its top stays in the pocket and its toe comes off his shoulder, as a recoiling stock does;
    //  about the toe, the heel and the comb went 4.7 cm into his chest, the pose QA - the pump's dip, the muzzle down, about the toe)
    {
        Vec3 D = Row(F, 2), X = Row(F, 0);
        float up = G.sgFlip.x + G.sgSettle.x - kSgPumpDip * rack;
        Vec3 pivot = XformPoint(StockPoint(up >= 0.0f ? V3(0, 0.012f, -0.262f) : V3(0, -0.094f, -0.262f)) * k, F);
        Mat4 R = Mul(AxisAngle(X, -up), AxisAngle(Norm(D), kSgPumpCant * rack));
        F = WithTrans(Mul(Rot3(F), R), pivot + XformDir(Row(F, 3) - pivot, R) -
                                           D * ((G.sgBack.x * kSgBackRel - kSgPumpStretch * PumpStretch(rackT)) * k));
    }
    //  a wall where the gun would be: the muzzle down about his right hand (low ready - muzzle up, port arms, brought the barrel up
    //  through his jaw: the pose QA, walking up to the house); the rays (SgUpdate) go along the carried gun and the aimed one as
    //  they'd be without it (along the final gun, lowering it cleared the ray and it came straight back)
    G.sgRayL = Mul(ScaleM(k), FL);
    G.sgRayAOn = false;
    if (ready && aim > 0.001f && G.haveAim) {
        Vec3 dA0 = Norm(aimP - (pocket + sUp * 0.08f * k));
        G.sgRayA = Mul(ScaleM(k), SgFrame(dA0, UP, pocket, butt, k));
        G.sgRayAOn = true;
    }
    if (G.sgWall.x > 0.001f) {
        Vec3 piv = XformPoint(kSgGrip * k, F);
        Mat4 R = AxisAngle(Norm(Row(F, 0)), kSgWallTilt * Smooth01(G.sgWall.x));
        F = WithTrans(Mul(Rot3(F), R), piv + XformDir(Row(F, 3) - piv, R));
    }
    //  his chin rests ON the stock, not in it: the head's tilt down is backed off (about his neck) until his chin, jaw and nose
    //  (CharShape.jaw, measured from his mesh) clear the gun's top - Homer's jaw sits beside the comb and keeps the whole tilt; Bart's
    //  chin hangs over the gun and sank 3.7 cm into it. (Every frame: the recoil's muzzle flip lifts the comb into his face - and
    //  his head goes back with it.)
    g_sgChinLift = 0.0f;
    int nChin = 0;
    const JawPt* chin = shape ? CharJaw(shape, &nChin) : nullptr;   // (v1.0 RC3: read from his own model as the game started)
    if (shape && nChin > 0 && wHead > 0.001f && torso) {
        Mat4 invF = FullInverse(F);
        Vec3 sRh = Norm(Row(W[15], 2));
        for (int it = 0; it < 4; it++) {
            float pen = 0.0f, arm = 0.1f;
            for (int i = 0; i < nChin; i++) {
                Vec3 w = XformPoint(V3(chin[i][0], chin[i][1], chin[i][2]), W[17]);
                Vec3 g = XformPoint(w, invF) * (1.0f / k);
                if (fabsf(g.x) > 0.035f || g.z < -0.27f || g.z > kSgMuzzleZ) continue;   // (beside the gun, behind it or past it)
                float top = SgTopAt(g.z) + kSgChinGap;
                if (g.y < top && top - g.y > pen) pen = top - g.y, arm = Len(w - neck);
            }
            if (pen <= 0.0f) break;
            float ang = asinf(Clampf(pen * k / fmaxf(arm, 0.05f), 0.0f, 1.0f)) * 1.1f;
            Mat4 R = AxisAngle(sRh, -ang);
            W[17] = Mul(W[17], Mul(Mul(Translation(neck * -1.0f), R), Translation(neck)));
            Rebuild(18);
            g_sgChinLift += ang;
        }
    }
    //  his neck takes a share of his head's turn (a neck bends along its length: turned at the head alone, the skin between the
    //  two - weighted half to each - stretched to 1.8x, the pose checker found); about the neck joint, as the head was turned
    if (kSgNeckShare > 0.0f && torso && wA > 0.001f) {
        Mat4 Dh = Mul(Transpose3(Rot3(headW0)), Rot3(W[17]));   // the head's turn in world space (row vectors: W0 * Dh)
        Mat4 Dn = BlendRot(Identity(), Dh, kSgNeckShare);
        Vec3 nk = Row(W[16], 3);
        W[16] = Mul(W[16], Mul(Mul(Translation(nk * -1.0f), Dn), Translation(nk)));
    }
    float pumpBack = kSgPumpTravel * rack;
    g_sg.pump = pumpBack;
    static float s_bodyMs;
    s_bodyMs = (float)(ProfNow() - tBody0);

    // 5. the hands
    Vec3 X = Row(F, 0), Y = Row(F, 1), D = Row(F, 2);
    float aR = Len(Row(L[27], 3)), bR = Len(Row(L[28], 3)), aL = Len(Row(L[20], 3)), bL = Len(Row(L[21], 3));
    //  right: the pistol's hold on the grip, turned down the grip's rake; out of reach, the gun comes back into his shoulder
    Mat4 Rg = AxisAngle(X, kSgGripPitch);
    Vec3 hx = XformDir(D * -1.0f, Rg), hy = XformDir(Y * -1.0f, Rg);
    Mat4 R28 = Basis(hx, hy, Cross(hx, hy));
    Vec3 pWt = XformPoint(kSgGrip * k, F) - XformDir(GripInWrist(false), R28);
    float over = Len(pWt - sR) - (aR + bR) * 0.985f;
    if (over > 0.0f) {
        float back = fminf(over, 0.06f * k);
        F = WithTrans(Rot3(F), Row(F, 3) - D * back);
        pWt = pWt - D * back;
    }
    g_sgReachR = fmaxf(0.0f, Len(pWt - sR) - (aR + bR) * 0.995f);
    {
        Vec3 u = pWt - sR;
        float d = Len(u);
        u = d > 1e-4f ? u * (1.0f / d) : sFwd;
        d = Clampf(d, fabsf(aR - bR) * 1.05f + 0.01f, (aR + bR) * 0.995f);
        // the elbow: where the forearm would run straight into the hand, blended with out to his side (aimed) / down (carried)
        Vec3 straight = Norm(SgStraightElbow(R28, pWt, 28, 27, L[28]) - sR);
        Vec3 natural = Norm(sRt * (0.45f + (kSgElbowOut - 0.45f) * wA) - sUp * (0.85f - (0.85f - kSgElbowDown) * wA));
        Vec3 pole = Norm(straight * kSgRStraight + natural * (1.0f - kSgRStraight));
        Vec3 hinge;
        Vec3 pE = TwoBone(sR, u, d, aR, bR, pole, sFwd * -1.0f, &hinge);
        Vec3 fore = Norm(sR + u * d - pE);
        Mat4 R26 = FrameXZ(Norm(sR - pE), hinge), R27 = FrameXZ(fore * -1.0f, hinge);
        g_sgSwivel0[0] = SgSwivelOf(natural, sR, pWt, sUp, sRt, true);
        g_sgSwivel[0] = g_sgSwivel0[0];
        if (kSgSwivelOpt > 0.0f) {   // the elbow where the wrist is easiest (and his arm out of his belly)
            // (Radical's grip-arm range once the gun is up; lower down - in the draw, the carry - his elbow may go back)
            float lo = kSgSwivelMinR + (kSgSwivelMin - kSgSwivelMinR) * (1.0f - wA), hi = kSgSwivelMaxR + (kSgSwivelMax - kSgSwivelMaxR) * (1.0f - wA);
            SgArmProblem A = {sR, pWt, sUp, sRt, aR, bR, true, R28, 28, 27, kSgTwistUA, kSgTwistFA, g_sgSwivel0[0], true, k, lo, hi};
            g_sgSwivel[0] = SgEase(0, SgFresh(0) ? SgBestSwivel(A) : SgLocalSwivel(A, g_sgEaseVal[0]));
            pE = SgArmAt(sR, pWt, aR, bR, true, sUp, sRt, g_sgSwivel[0], &R26, &R27);
        }
        float wR = ph.reach;
        if (kSgSwivelOpt > 0.0f)   // (the arm solved all the way between the game's pose and ours)
            SgArmBlend(W, L, 26, 27, 28, true, sR, pWt, R28, g_sgSwivel[0], kSgTwistUA, kSgTwistFA, aR, bR, sUp, sRt, wR, &g_sgTwistR, &g_sgWristR);
        else {
            g_sgWristR = SgTwistArm(R26, R27, R28, 28, 27, kSgTwistUA, kSgTwistFA, &g_sgTwistR);
            W[26] = BlendLocal(L[26], Mul(R26, Transpose3(W[15])), W[15], wR);
            W[27] = BlendLocal(L[27], Mul(R27, Transpose3(R26)), W[26], wR);
            W[28] = BlendLocal(L[28], Mul(R28, Transpose3(R27)), W[27], wR);
        }
        roots[26] = true;
        // the fist closes on the grip as his hand gets into the pocket and opens as it lets go there (eased: the QA caught his fingers
        // and thumb snapping in one frame from the game's hand to the fist and back; putting it away, as the stock's last part goes
        // down into it and after - not over the holster's last 6 %, which opened his hand in two frames)
        float sf = g_sgFreeze >= 0.0f ? g_sgFreeze : show;
        float hw = g_sgFreeze >= 0.0f ? 0.0f : g_sgHolW;   // (the draw's and the holster's, blended as the phase is)
        float fist = show >= 0.999f && g_sgFreeze < 0.0f ? 1.0f
                                                         : Smooth01((sf - 0.07f) / 0.08f) * (1.0f - hw) + Smooth01((sf - 0.02f) / 0.12f) * hw;
        CurlHand(W, L, 28, 29, kSgRCurl1 * fist, kSgRCurl2 * fist, kSgRThumb1 * fist, kSgRThumb2 * fist);
        if (fist > 0.001f && n > 32)   // (over the grip to its left side)
            AimThumb(W, 31, shape ? V3(-shape->thumb[0], -shape->thumb[1], -shape->thumb[2]) : V3(-1, 0, 0), D * 0.6f - X * 0.7f - Y * 0.2f,
                     kSgRThumbAim * fist);
    }
    //  left: the palm on the fore-end, the fingers round under it and up its right side, the thumb along its left side towards the
    //  muzzle (a support hand's grip); the elbow hangs under the gun (never above the line from shoulder to hand - review: "inverted
    //  elbow"), and the hand turns round the fore-end to meet the forearm as it comes (its roll and finger angle picked from a few,
    //  for the straightest wrist - a bent one dragged Homer's thumb out into a spike); out of reach, his shoulder reaches forward
    g_sgReachL = 0.0f;
    if (ph.leftW > 0.001f) {
        float handLen = Len(Row(L[22], 3));
        float z0 = (kSgPumpZ0 + 0.015f) * k - pumpBack * k, z1 = (kSgPumpZ1 - 0.02f) * k - pumpBack * k;
        Vec3 poleL = Norm(sUp * -1.0f + sRt * kSgLElbowIn - D * 0.25f);
        // where along the gun the hand holds it: out on the barrel (carried) or on the pump, as g_sgOnPump has it - and what it closes
        // round there: the pump's box, or the barrel over the magazine tube once the hand is off the pump's front end (eased over 4 cm)
        float zHold = kSgCarryHold * k + (z0 + (z1 - z0) * kSgHoldAlong - kSgCarryHold * k) * g_sgOnPump;
        float secY = kSgPumpY, secHX = kSgPumpHX, secHY = kSgPumpHY;
        auto Section = [&]() {
            float b = Smooth01((zHold / k - (kSgPumpZ1 - pumpBack) + 0.01f) / 0.04f);
            secY = kSgPumpY + (kSgBarY - kSgPumpY) * b, secHX = kSgPumpHX + (kSgBarHX - kSgPumpHX) * b;
            secHY = kSgPumpHY + (kSgBarHY - kSgPumpHY) * b;
        };
        Section();
        // the hand on the fore-end for a roll (rad: 0 palm straight under, + rolled round to its left side) and finger angle
        auto HandAt = [&](float roll, float fwdA, Mat4* R21o, Vec3* pWo, Vec3* offo) {
            Vec3 zl = Norm(X * -sinf(roll) + Y * -cosf(roll));     // out of the fore-end through the back of his hand (palm faces -Z)
            Vec3 round = Norm(X * cosf(roll) - Y * sinf(roll));    // round under it, towards his right
            Vec3 fingers = Norm(round * cosf(fwdA) + D * sinf(fwdA));
            Vec3 xl = Norm(fingers - zl * Dot(fingers, zl));
            *R21o = Basis(xl, Cross(zl, xl), zl);                   // fingers +X, thumb +Y (left hand)
            Vec3 touch = Y * (secY * k) + (X * (-secHX * sinf(roll)) + Y * (-secHY * cosf(roll))) * k;
            *offo = touch - xl * (kSgLAlong * handLen) + zl * (kSgLThick * k);
            *pWo = Row(F, 3) + *offo + D * zHold;
        };
        auto Solve = [&](Vec3 pW, Vec3 shoulder, Mat4* R19o, Mat4* R20o) {
            Vec3 u = pW - shoulder;
            float d = Len(u);
            u = d > 1e-4f ? u * (1.0f / d) : sFwd;
            d = Clampf(d, fabsf(aL - bL) * 1.05f + 0.01f, (aL + bL) * 0.995f);
            Vec3 hinge;
            Vec3 pE = TwoBone(shoulder, u, d, aL, bL, poleL, sFwd * -1.0f, &hinge);
            *R19o = FrameXZ(Norm(pE - shoulder), hinge);
            *R20o = FrameXZ(Norm(shoulder + u * d - pE), hinge);
        };
        Mat4 R21;
        Vec3 pWl, off;
        float roll = kSgLRoll, fwdA = kSgLFwd;
        float phiL0 = 0.0f;
        if (kSgSwivelOpt > 0.0f) {   // the hand's roll round the fore-end and the elbow's swivel, each the easiest for the wrist
            float bestC = 1e30f, bestR = kSgLRoll;
            bool fresh = SgFresh(2);
            float r0 = g_sgEaseVal[2];
            for (int ir = 0; ir <= (fresh ? 14 : 2); ir++) {   // (each roll with its own best swivel; near the last roll between scans)
                float r = fresh ? -0.40f + 0.13f * ir : Clampf(r0 + (ir - 1) * 0.08f, -0.40f, 1.42f);
                Mat4 Rh;
                Vec3 pw, of;
                HandAt(r, kSgLFwd, &Rh, &pw, &of);
                SgArmProblem A = {sL, pw, sUp, sRt, aL, bL, false, Rh, 21, 20, kSgTwistUAL, kSgTwistFAL,
                                  SgSwivelOf(poleL, sL, pw, sUp, sRt, false), false, k, kSgSwivelMinL, kSgSwivelMaxL};
                float c = SgArmCost(A, fresh ? SgBestSwivel(A) : SgLocalSwivel(A, g_sgEaseVal[1])) + 0.15f * fabsf(r - kSgLRoll);
                if (c < bestC) bestC = c, bestR = r;
            }
            roll = SgEase(2, bestR);
        } else if (kSgLRollSearch > 0.0f) {   // the straightest wrist: rolls from palm-under to the fore-end's left side, three finger angles
            float best = 1e9f;
            for (int ir = 0; ir <= 10; ir++)
                for (int jf = -1; jf <= 1; jf++) {
                    float r = -0.25f + 0.15f * ir, f = kSgLFwd + 0.30f * jf;
                    Mat4 Rh, R19c, R20c;
                    Vec3 pw, of;
                    HandAt(r, f, &Rh, &pw, &of);
                    Solve(pw, sL, &R19c, &R20c);
                    float tw, dev = SgTwistArm(R19c, R20c, Rh, 21, 20, kSgTwistUAL, kSgTwistFAL, &tw);
                    dev += 0.15f * fabsf(r - kSgLRoll) + kSgLTwistCost * fabsf(tw);   // (between near-equal ones, the designed hold; less turn)
                    if (dev < best) best = dev, roll = r, fwdA = f;
                }
        }
        HandAt(roll, fwdA, &R21, &pWl, &off);
        g_sgLRollUsed = roll;
        float overL = Len(pWl - sL) - (aL + bL) * 0.985f;
        if (overL > 0.0f && shL < kSgShLMax * k) {   // reach further: the shoulder forward, towards the hand
            // (as far as the hand has come: in full at once, his shoulder jumped 16 cm in a frame as the hand set off for the
            // fore-end in the draw - the pose QA's one-frame snap)
            float more = fminf(overL, kSgShLMax * k - shL) * ph.leftW;
            ShiftJoint(W, L, 19, Norm(pWl - sL) * more);
            sL = Row(W[19], 3);
        }
        // still short: the hand back along the gun until it reaches (the pump's back edge at most)
        for (int it = 0; it < 48 && zHold > z0 + 1e-4f && Len(pWl - sL) > (aL + bL) * 0.985f; it++) {
            zHold = fmaxf(z0, zHold - 0.01f * k);
            Section();
            HandAt(roll, fwdA, &R21, &pWl, &off);
        }
        g_sgReachL = fmaxf(0.0f, Len(pWl - sL) - (aL + bL) * 0.995f);
        if (wA > 0.95f) {   // (report) the stock length that would put the pump hand at kSgReachFit of his reach: the receiver moved along
                            // the gun by s (|v + D s| = target), the butt staying in the pocket
            Vec3 v = pWl - sL;
            float t = kSgReachFit * (aL + bL), vd = Dot(v, D), disc = vd * vd - Dot(v, v) + t * t;
            if (disc >= 0.0f) g_sgStockSolve = g_sgStockLen + (-vd + sqrtf(disc)) / (0.238f * k);
        }
        Mat4 R19, R20;
        Solve(pWl, sL, &R19, &R20);
        phiL0 = SgSwivelOf(poleL, sL, pWl, sUp, sRt, false);
        g_sgSwivel0[1] = g_sgSwivel[1] = phiL0;
        if (kSgSwivelOpt > 0.0f) {
            SgArmProblem A = {sL, pWl, sUp, sRt, aL, bL, false, R21, 21, 20, kSgTwistUAL, kSgTwistFAL, phiL0, true, k, kSgSwivelMinL,
                              kSgSwivelMaxL};
            g_sgSwivel[1] = SgEase(1, SgFresh(1) ? SgBestSwivel(A) : SgLocalSwivel(A, g_sgEaseVal[1]));
            SgArmAt(sL, pWl, aL, bL, false, sUp, sRt, g_sgSwivel[1], &R19, &R20);
        }
        float wL = ph.leftW;
        if (kSgSwivelOpt > 0.0f)
            SgArmBlend(W, L, 19, 20, 21, false, sL, pWl, R21, g_sgSwivel[1], kSgTwistUAL, kSgTwistFAL, aL, bL, sUp, sRt, wL, &g_sgTwistL, &g_sgWristL);
        else {
            g_sgWristL = SgTwistArm(R19, R20, R21, 21, 20, kSgTwistUAL, kSgTwistFAL, &g_sgTwistL);
            W[19] = BlendLocal(L[19], Mul(R19, Transpose3(W[15])), W[15], wL);
            W[20] = BlendLocal(L[20], Mul(R20, Transpose3(R19)), W[19], wL);
            W[21] = BlendLocal(L[21], Mul(R21, Transpose3(R20)), W[20], wL);
        }
        CurlHand(W, L, 21, 22, kSgLCurl1 * wL, kSgLCurl2 * wL, kSgLThumb1 * wL, kSgLThumb2 * wL);
        if (n > 25)   // (along the fore-end's left side, to the muzzle)
            AimThumb(W, 24, shape ? V3(shape->thumb[0], shape->thumb[1], shape->thumb[2]) : V3(1, 0, 0), D + X * 0.15f + Y * 0.05f, kSgLThumbAim * wL);
        roots[19] = true;
    }
    if (!roots[19])
        for (int j = 20; j <= 25; j++) Rebuild(j);
    for (int j = 33; j < n; j++) Rebuild(j);
    LegLayer(W, L, roots, n);
    SkinPrepare(P, W, roots);
    g_sg.world = F;
    g_sg.k = k;
    g_sg.have = ph.shown;
    g_sg.pocket = ph.pocket;
    //  (the pocket's mouth rises over the stock's last part as the holster ends - it shrinks down into his fist - and comes down from
    //  over it as the draw begins: its last 13 cm stood out of the pocket and vanished in one frame, appeared in one)
    g_sg.clip[0] = 0, g_sg.clip[1] = 1, g_sg.clip[2] = 0, g_sg.clip[3] = -(tpocket.y + kSgSink * k * ph.sunk);
    g_sg.head = W[17];
    g_sg.haveHead = true;
    PtSnapshot();
    // the wink: his left eye shuts in the last part of the raise (only when he aims, not for a shot from the hip)
    g_sgWinkTarget = Smooth01((wA - 0.55f) / 0.35f) * Clamp01(G.camAim * 2.0f);
    if (g_sgLog > 0 || g_buildLog > 0) {
        if (g_sgLog > 0) g_sgLog--;
        Vec3 o = Row(F, 3), hR = Row(W[28], 3), hL = Row(W[21], 3), bW = XformPoint(butt * k, F);
        Log("sg: show %.2f (%s) swing %.2f pump %.2f left %.2f | aim %.2f wA %.2f k %.2f | gun (%.2f %.2f %.2f) dir (%.2f %.2f %.2f) | "
            "butt %.3f m from the pocket | hands short R %.3f L %.3f | shoulder L forward %.3f | hand R (%.2f %.2f %.2f) L (%.2f %.2f %.2f) | "
            "out of his body %.3f",
            show, G.sgHolster ? "holster" : "draw", ph.swing, ph.pump, ph.leftW, aim, wA, k, o.x, o.y, o.z, D.x, D.y, D.z, Len(bW - pocket),
            g_sgReachR, g_sgReachL, Dot(sL - Row(Mul(L[19], W[15]), 3), sFwd), hR.x, hR.y, hR.z, hL.x, hL.y, hL.z, g_sgPush);
        {   // against Radical's own arms (research/anim): pump arm swivel 20-35, bend 30-55, reach 33-38 cm; grip arm swivel 30-60,
            // bend 55-110, reach 26-33 cm; elevation 40-75, azimuth 50-80; trigger wrist 5-25, pump hand <= 55
            float aL[6], aR[6];
            ArmStats(W, 19, 20, 21, 22, sUp, sRt, sFwd, aL);
            ArmStats(W, 26, 27, 28, 29, sUp, sRt, sFwd, aR);
            Log("sgarm: L(pump) swivel %.0f bend %.0f reach %.1f elev %.0f az %.0f hand %.0f | R(grip) swivel %.0f bend %.0f reach %.1f "
                "elev %.0f az %.0f hand %.0f | stock %.3f, fit for reach %.2f: %.3f", aL[0], aL[1], aL[2], aL[3], aL[4], aL[5], aR[0], aR[1], aR[2],
                aR[3], aR[4], aR[5], g_sgStockLen, kSgReachFit, g_sgStockSolve);
        }
        if (shape) {   // the fit, in cm: his right eye over the rib, his jaw over the comb; the pocket / pad / eye / jaw from his right
                       // shoulder joint in his chest's frame (right, up, forward)
            Vec3 eyeR = XformPoint(V3(shape->eye[0], shape->eye[1], -shape->eye[2]), W[17]), jaw = Row(W[18], 3);
            Vec3 rib = XformPoint(kSgRear * k, F), comb = XformPoint(StockPoint(V3(0, 0.024f, -0.07f)) * k, F);
            Vec3 sR0 = Row(W[26], 3), e = eyeR - rib, jw = jaw - comb;
            auto C = [&](Vec3 p) { Vec3 q = p - sR0; return V3(Dot(q, sRt) * 100, Dot(q, sUp) * 100, Dot(q, sFwd) * 100); };
            Vec3 cp = C(pocket), cb = C(bW), ce = C(eyeR), cj = C(jaw), cr = C(rib);
            Log("sgm: eye R over the rib %.1f up %.1f side %.1f along | jaw over the comb %.1f up %.1f side %.1f along | from the R "
                "shoulder (right up fwd): pocket %.1f %.1f %.1f pad %.1f %.1f %.1f rib %.1f %.1f %.1f eye %.1f %.1f %.1f jaw %.1f %.1f %.1f | "
                "chest yaw %.2f lean %.2f | feet->shoulder R %.1f eye %.1f | hand twist R %.0f L %.0f deg, wrist off bind R %.0f L %.0f deg | "
                "chin lift %.0f deg | elbow R below %.0f out %.0f back %.0f, L below %.0f out %.0f back %.0f cm (from the shoulder-wrist line) | "
                "L hand roll %.2f | seat %.1f cm (pad %.1f, rest %.1f cm) | swivel R %.0f (designed %.0f) L %.0f (%.0f) deg | gun placing %.2f ms",
                Dot(e, Y) * 100, Dot(e, X) * 100, Dot(e, D) * 100, Dot(jw, Y) * 100, Dot(jw, X) * 100, Dot(jw, D) * 100, cp.x, cp.y, cp.z,
                cb.x, cb.y, cb.z, cr.x, cr.y, cr.z, ce.x, ce.y, ce.z, cj.x, cj.y, cj.z, twist, lean, (sR0.y - Row(W[0], 3).y) * 100,
                (eyeR.y - Row(W[0], 3).y) * 100, g_sgTwistR * 57.3f, g_sgTwistL * 57.3f, g_sgWristR * 57.3f, g_sgWristL * 57.3f, g_sgChinLift * 57.3f,
                ElbowOff(W, 26, 27, 28, sUp, sRt, sFwd, 0), ElbowOff(W, 26, 27, 28, sUp, sRt, sFwd, 1), ElbowOff(W, 26, 27, 28, sUp, sRt, sFwd, 2),
                ElbowOff(W, 19, 20, 21, sUp, sRt, sFwd, 0), ElbowOff(W, 19, 20, 21, sUp, sRt, sFwd, 1), ElbowOff(W, 19, 20, 21, sUp, sRt, sFwd, 2),
                g_sgLRollUsed, g_sgSeat * 100, g_sgSeatPad * 100, g_sgSeatRest * 100, g_sgSwivel[0] * 57.3f, g_sgSwivel0[0] * 57.3f,
                g_sgSwivel[1] * 57.3f, g_sgSwivel0[1] * 57.3f, s_bodyMs);
        }
    }
}

// ------------------------------------------------------------------ firing
// Ten pellets in a 4 degree cone. Each hits on its own (holes, glass, tyres, a car's damage add up: a full blast is about a
// third of a car's hit points and shoves it hard), but a person or a loose prop hit by several reacts once, by how many hit
// (WeaponSpec.group): one pellet knocks someone down a bit harder than the pistol, a full blast sends them flying.
static const WeaponSpec kShotgun = {
    "shotgun",
    45.0f, 0.025f,          // range, a pellet's thickness
    10, 0.070f,             // pellets, cone half-angle (4 degrees)
    0.032f, 420.0f,         // vehicles, per pellet: share of hit points, shove
    1250.0f, 0.55f,         // people: the push of a full blast (grouped), 31 degrees up: they go flying backwards
    900.0f, true,           // props: a full blast's push, breaks breakables
    true,                   // Hit & Run
    0.075f,                 // pellet holes
    {0.22f, 0.30f, 0.55f, 0.6f, 0.6f, 0.12f, 0.25f},   // dust, dirt, grass, water, own, sparks, car sparks (each pellet)
    0.0f,                   // not a blast
    true,                   // (group) people and props hit by several pellets react once
};

static void FireShotgun()
{
    if (!G.haveSg) return;
    G.cooldown = 0.80f;
    G.sgPumpT = -0.16f;   // the rack starts inside the recoil, once the kick has peaked and the muzzle is coming down
    G.sgEject = true;
    // recoil: the gun slams back into his shoulder and the muzzle climbs, his chest rocks back and his right shoulder
    // goes back, the camera takes a big kick
    G.sgBack.v += 1.6f;
    G.sgFlip.v += 8.0f;   // (with its spring below: the muzzle up ~16 degrees, peaking in ~2 frames)
    G.kickChest.v += 3.6f;
    G.sgTwist.v += 2.8f;
    G.kickPitch.v += 3.6f;
    G.kickYaw.v += (Rnd() - 0.5f) * 1.2f;
    G.fovPunch.v += 3.2f;
    G.xhKick.v += 28.0f;
    Input_Rumble(1.0f, 1.0f, 0.32f);
    Sound_Play(SND_SHOTGUN, 1.0f, 0.95f + 0.08f * Rnd());
    Vec3 me;
    if (PlayerPos(&me)) React_PanicAround(me, kFirePanic + 7.0f);
    Mat4 Wg = G.sgWorld;
    Vec3 muzzle = XformPoint(V3(0, 0, kSgMuzzleZ), Wg), barrel = Norm(XformDir(V3(0, 0, 1), Wg));
    Vec3 dir = barrel;
    if (G.haveAim && G.aim > 0.5f) {
        Vec3 toAim = Norm(G.aimPoint - muzzle);
        if (Dot(toAim, barrel) > 0.85f) dir = toAim;
    }
    ShotOrigin so = {};
    Vec3 feet;
    so.shooter = g_player;
    so.body = PlayerPos(&feet) ? feet + UP * 1.25f : muzzle - barrel * 0.6f;
    so.muzzle = muzzle;
    so.dir = dir;
    so.aimed = G.aiming && G.camAim > 0.5f;
    so.eye = G.camPos;
    so.eyeDir = G.camFwd;
    ShotResult res[kMaxPellets];
    int n = Trace_Available() ? FireWeapon(kShotgun, so, res, kMaxPellets) : 0;
    if (g_shotLog > 0) {
        g_shotLog--;
        int hits[4] = {};
        for (int i = 0; i < n; i++) hits[res[i].hit.kind]++;
        Log("shot: shotgun %s, %d pellets: %d world, %d vehicle, %d character, %d nothing", so.aimed ? "aimed" : "hip", n, hits[HIT_WORLD],
            hits[HIT_VEHICLE], hits[HIT_CHARACTER], hits[HIT_NONE]);
    }
    // the blast: a big star, a long flame jet, two side flashes, a cloud of smoke rolling forward
    float s = Len(Row(Wg, 0));
    if (G.nflash < 6) G.flash[G.nflash++] = {muzzle + dir * 0.04f, dir, 0, Rnd() * 2 * PI, 2.3f * s, 2.4f};
    Vec3 side = Norm(Cross(UP, dir));
    for (int k2 = -1; k2 <= 1 && G.nflash < 6; k2 += 2)
        G.flash[G.nflash++] = {muzzle + dir * 0.10f + side * (0.05f * k2 * s), Norm(dir + side * (0.8f * k2)), 0, Rnd() * 2 * PI, 0.9f * s, 0.9f};
    for (int i = 0; i < 7 && G.nsmoke < 64; i++)
        G.smoke[G.nsmoke++] = {muzzle + dir * (0.08f + i * 0.07f), dir * (3.2f - i * 0.38f) + V3(Rnd() - 0.5f, 0.5f + Rnd() * 0.3f, Rnd() - 0.5f) * 0.7f,
                               0, 0.65f + Rnd() * 0.45f, Rnd() * 2 * PI, (0.30f + i * 0.05f) * s};
    // each pellet: a thin streak to where it went, its own small impact (the first three make a sound)
    int sounds = 0;
    for (int i = 0; i < n; i++) {
        const ShotResult& r = res[i];
        Vec3 path = r.to - r.from, td = dir;
        float d = Len(path);
        if (d > 0.05f && Dot(path, dir) > 0.0f) td = path * (1.0f / d);
        if (r.hit.kind != HIT_NONE) Impact(r.hit.pos, r.hit.normal, Hit_Surface(r.hit), r.gameFx, 0.55f, sounds++ < 3);
        if (G.ntracers < 24) G.tracers[G.ntracers++] = {muzzle + td * 0.15f, td, 0, 0, d};
    }
}

// a spent shell out of the port, to his right and up, tumbling
static void SgEjectShell()
{
    if (!G.haveSg || G.nshells >= 16) return;
    Mat4 Wg = G.sgWorld;
    Vec3 port = XformPoint(kSgPort, Wg), right = Norm(XformDir(V3(1, 0, 0), Wg)), d = Norm(XformDir(V3(0, 0, 1), Wg));
    Shell& sh = G.shells[G.nshells++];
    sh.kind = 1;
    sh.landed = false;
    sh.p = port;
    sh.v = right * (2.2f + Rnd() * 0.8f) + UP * (2.4f + Rnd() * 0.7f) - d * 0.6f;
    sh.axis = Norm(V3(Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f));
    sh.ang = 0;
    sh.spin = 14 + Rnd() * 10;
    sh.t = 0;
    sh.ground = g_ground;
}

// once a frame (Gun_Update): the pump's rack and its sound, the shell, the recoil springs, the wink
static void SgUpdate(float dt, bool aimed)
{
    BuildShotgun();
    // put away (or another weapon): nothing of the last pose kept - the holster's last build left the gun "shown" in his pocket,
    // and the next draw's first frame drew it there before a new pose was built (its stock flashed at his hand for a frame)
    if (G.held != WPN_SHOTGUN || G.show <= 0.001f) g_sg.have = false;
    {   // the draw / holster curves' weight: set outright where they agree (from all the way out or all the way in), eased between
        static float lastShow;
        float want = G.sgHolster ? 1.0f : 0.0f;
        if (G.show <= 0.001f || lastShow >= 0.999f || lastShow <= 0.001f) g_sgHolW = want;
        else g_sgHolW += (want - g_sgHolW) * (1.0f - expf(-dt * 10.0f));
        lastShow = G.show;
    }
    if (G.sgPumpT < 1.0f) {
        float before = G.sgPumpT;
        G.sgPumpT += dt;
        if (before < 0.0f && G.sgPumpT >= 0.0f) Sound_Play(SND_PUMP, 0.9f, 0.97f + 0.06f * Rnd());
        if (before < kSgRackBack && G.sgPumpT >= kSgRackBack) G.kickPitch.v -= kSgPumpCamBack, G.fovPunch.v += 0.25f;   // (clack: back)
        if (before < kSgRackHome && G.sgPumpT >= kSgRackHome) G.kickPitch.v += kSgPumpCamHome, G.fovPunch.v -= 0.2f;    // (clack: home)
        if (G.sgEject && before < 0.85f * kSgRackBack && G.sgPumpT >= 0.85f * kSgRackBack) {   // (the hull clears the port late in the stroke)
            G.sgEject = false;
            SgEjectShell();
        }
    }
    {   // his left hand: on the pump for whatever needs it there - the draw (the grab, the rack), the raise and the aim (a shot from the
        // hip holds the gun up too), the rack after a shot - else out on the barrel. Lowering, it goes back out over the second half of
        // the way down (held on the pump all the way, its forearm went through his belly as the gun came down to the carry). Quick onto
        // the pump (a rack starts 0.16 s after the shot), slower back out; putting the gun away, the hand lets go from where it is
        bool drawing = G.show < 0.999f && !G.sgHolster;
        bool racking = G.sgPumpT > -0.5f && G.sgPumpT < kSgRackEnd + 0.06f;
        float want = drawing || racking || G.sgRaising || g_sgFreeze >= 0.0f || g_sgAimFreeze >= 0.0f ? 1.0f : Smooth01((G.aim - 0.35f) / 0.40f);
        if (G.held != WPN_SHOTGUN || G.show <= 0.001f) g_sgOnPump = 1.0f;   // (a new draw starts on the pump)
        else if (!G.sgHolster) g_sgOnPump += (want - g_sgOnPump) * (1.0f - expf(-dt * (want > g_sgOnPump ? 22.0f : 9.0f)));
    }
    Step(G.sgBack, 16.0f, 0.55f, dt);
    Step(G.sgFlip, 16.0f, 0.45f, dt);   // (Radical's landings snap in 1-2 frames and overshoot their rest ~10-40 %: research/anim)
    Step(G.sgTwist, 10.0f, 0.55f, dt);
    {   // the raise / lower weight: a flag held for a moment before it counts, then eased (the pose blends the two orders by it)
        static float held;
        held = G.sgRaising ? fminf(fmaxf(held, 0.0f) + dt, 1.0f) : fmaxf(fminf(held, 0.0f) - dt, -1.0f);
        float want = held > 0.06f ? 1.0f : held < -0.06f ? 0.0f : g_sgRaiseW;
        g_sgRaiseW += (want - g_sgRaiseW) * (1.0f - expf(-dt * 12.0f));
    }
    static float lastAim;   // the raise lands: a little kick that settles (weight)
    if (G.sgRaising && lastAim < 0.9f && G.aim >= 0.9f) G.sgSettle.v += kSgSettle;
    lastAim = G.aim;
    Step(G.sgSettle, 15.0f, 0.5f, dt);
    // the wall: rays along the carried and the aimed gun as they'd be, from the back of the receiver to just past the muzzle
    auto Into = [&](const Mat4& Wf) {   // how far into something the gun would go (0..1 of a third of its length)
        Vec3 a = XformPoint(kSgRear, Wf), b = XformPoint(V3(0, 0, kSgMuzzleZ + kSgWallReach), Wf);
        HitResult h;
        // (walls, fences, cars - not people: a gun held on someone standing close is aimed at them, not raised away)
        if (!TraceShot(a, b, g_player, &h, 0.03f) || h.kind == HIT_NONE || h.kind == HIT_CHARACTER) return 0.0f;
        float L = Len(b - a), d = Len(h.pos - a), sz = Len(Row(Wf, 0));
        return Clamp01((L - d) / (0.30f * (sz > 0.1f ? sz : 1.0f)));
    };
    float wantL = 0.0f, wantA = 0.0f;
    if (kSgWallOn > 0.0f && G.held == WPN_SHOTGUN && G.show > 0.5f && g_player && Trace_Available()) {
        wantL = Into(G.sgRayL);
        if (G.sgRayAOn) wantA = Into(G.sgRayA);
    }
    StepTo(G.sgWallA, wantA, wantA > G.sgWallA.x ? 16.0f : 7.0f, 1.0f, dt);   // (out of the aim fast, back into it slowly)
    StepTo(G.sgWall, fmaxf(wantL, wantA * Clamp01(1.0f - G.aim)), wantL > G.sgWall.x ? 16.0f : 7.0f, 1.0f, dt);
    G.sgWallA.x = Clamp01(G.sgWallA.x), G.sgWall.x = Clamp01(G.sgWall.x);
    // the left eye shuts late in the raise (the pose sets the target from it), opens as the gun comes down
    float want = aimed ? g_sgWinkTarget : 0.0f;
    g_sgWink += (want - g_sgWink) * (1.0f - expf(-dt * (want > g_sgWink ? 22.0f : 14.0f)));
    if (fabsf(want - g_sgWink) < 0.002f) g_sgWink = want;
}

// ------------------------------------------------------------------ drawing
void Wink_Draw(Renderer& r, const Mat4& head, float k, float shut, Vec3 viewDir);   // (wink.inc)
static void SgDraw(Renderer& r, Vec3 viewDir, bool unseen)
{
    G.haveSg = false;
    if (G.held != WPN_SHOTGUN || !g_sg.have || G.show <= 0.001f) return;
    Mat4 world = Mul(ScaleM(g_sg.k), g_sg.world);
    G.sgWorld = world;
    G.haveSg = true;
    if (unseen) return;
    static Renderer::MeshV mv[1400];
    Mat4 pumpOff = Translation(V3(0, 0, -g_sg.pump));
    for (int i = 0; i < g_sgMesh.nv; i++) {
        const GV& g = g_sgv[i];
        Vec3 pl = g.part == 1 ? XformPoint(g.p, pumpOff) : g.part == 2 ? StockPoint(g.p) : g.p;
        mv[i].x = pl.x, mv[i].y = pl.y, mv[i].z = pl.z;
        mv[i].c = Lit(g.col, Norm(XformDir(g.n, world)), g.spec, viewDir);
    }
    if (g_sg.pocket) r.Clip3D(g_sg.clip);
    r.Mesh(mv, g_sgMesh.nv, g_sgi, g_sgMesh.ni, world.m);
    if (g_sg.pocket) r.Clip3D(nullptr);
    // his left eye shut along the sights: on the eyeball itself (face.inc, Gun_EyeSplit); only if that couldn't be done this
    // frame, the drawn lids over it
    if (g_sg.haveHead && g_skinHits[17] > 0 && g_eyeSplitAt != g_buildFrame) WinkDraw(r, g_sg.head, g_sgWink, viewDir);
}

// ------------------------------------------------------------------ debug
static bool SgDebugCmd(const char* cmd, const char* arg)
{
    if (FaceDebugCmd(cmd, arg)) return true;
    if (!strcmp(cmd, "posedump")) {   // "posedump <name>": his posed skeleton, the game's inverse binds and the gun as drawn ->
                                      // WeaponWheel\<name>.json, for the offline pose checker (research/rig/posecheck.py)
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls%hs.json", g_modDir, arg[0] ? arg : "pose");
        FILE* f = _wfopen(path, L"w");
        if (!f) return true;
        const CharShape* c = CharShapeNow();
        fprintf(f, "{\"char\": \"%s\", \"frame\": %d, \"show\": %.3f, \"aim\": %.3f, \"k\": %.4f, \"pump\": %.4f,\n", c ? c->name : "?", g_buildFrame,
                G.show, G.aim, g_sg.k, g_sg.pump);
        auto M = [&](const char* key, const Mat4& m, bool comma) {
            fprintf(f, "\"%s\": [", key);
            for (int i = 0; i < 16; i++) fprintf(f, "%.6f%s", m.m[i], i < 15 ? ", " : "");
            fprintf(f, "]%s\n", comma ? "," : "");
        };
        M("gun", g_sg.world, true);
        fprintf(f, "\"joints\": [\n");
        for (int j = 0; j < g_skin.n; j++) {
            fprintf(f, " [");
            for (int i = 0; i < 16; i++) fprintf(f, "%.6f%s", g_skin.world[j].m[i], i < 15 ? ", " : "");
            fprintf(f, "]%s\n", j + 1 < g_skin.n ? "," : "");
        }
        fprintf(f, "],\n\"invbind\": [\n");
        for (int j = 0; j < g_skin.n; j++) {
            fprintf(f, " [");
            for (int i = 0; i < 16; i++) fprintf(f, "%.6f%s", g_invBind[j].m[i], i < 15 ? ", " : "");
            fprintf(f, "]%s\n", j + 1 < g_skin.n ? "," : "");
        }
        fprintf(f, "],\n\"gun_verts\": [\n");   // gun space (x k, then the gun matrix), the pump slid and the stock fitted as drawn
        Mat4 pumpOff = Translation(V3(0, 0, -g_sg.pump));
        for (int i = 0; i < g_sgMesh.nv; i++) {
            const GV& g = g_sgv[i];
            Vec3 p = g.part == 1 ? XformPoint(g.p, pumpOff) : g.part == 2 ? StockPoint(g.p) : g.p;
            fprintf(f, " [%.5f, %.5f, %.5f, %d]%s\n", p.x, p.y, p.z, g.part, i + 1 < g_sgMesh.nv ? "," : "");
        }
        fprintf(f, "],\n\"gun_tris\": [");
        for (int i = 0; i < g_sgMesh.ni; i++) fprintf(f, "%d%s", g_sgi[i], i + 1 < g_sgMesh.ni ? ", " : "");
        fprintf(f, "]}\n");
        fclose(f);
        Log("posedump: %s.json (%d joints, gun %d vertices)", arg[0] ? arg : "pose", g_skin.n, g_sgMesh.nv);
    } else if (!strcmp(cmd, "sgfreeze")) {   // "sgfreeze <0..1>": hold the draw there (-1 = off)
        g_sgFreeze = arg[0] ? (float)atof(arg) : -1.0f;
        Log("sg: draw frozen at %.2f", g_sgFreeze);
    } else if (!strcmp(cmd, "sgaim")) {   // "sgaim <0..1>": hold the raise there (with the aim button held; -1 = off)
        g_sgAimFreeze = arg[0] ? (float)atof(arg) : -1.0f;
        Log("sg: raise frozen at %.2f", g_sgAimFreeze);
    } else if (!strcmp(cmd, "sglog")) g_sgLog = arg[0] ? atoi(arg) : 1;
    else if (!strcmp(cmd, "sgpump")) {
        G.sgPumpT = 0.0f - 1e-4f;
        G.sgEject = atoi(arg) != 0;
    } else if (!strcmp(cmd, "sgtune")) {
        struct { const char* name; float* v; } t[] = {
            {"pocketout", &kSgPocketOut}, {"pocketfwd", &kSgPocketFwd}, {"pocketup", &kSgPocketUp}, {"pullup", &kSgPullUp},
            {"pullfwd", &kSgPullFwd}, {"stocklen", &kSgStockLen}, {"stockdrop", &kSgStockDrop}, {"pkin", &kSgPkIn}, {"pkup", &kSgPkUp},
            {"pkskin", &kSgPkSkin}, {"blade", &kSgBlade}, {"aimlean", &kSgAimLean}, {"headdown", &kSgHeadDown}, {"headroll", &kSgHeadRoll},
            {"shrfwd", &kSgShRFwd}, {"shrup", &kSgShRUp}, {"shlfwd", &kSgShLFwd}, {"shlmax", &kSgShLMax}, {"grippitch", &kSgGripPitch},
            {"holdalong", &kSgHoldAlong}, {"carryhold", &kSgCarryHold}, {"lowdown", &kSgLowDown}, {"lowout", &kSgLowOut},
            {"lowdip", &kSgLowDip}, {"lowside", &kSgLowSide}, {"lowacross", &kSgLowAcross}, {"raiselead", &kSgRaiseLead}, {"drawtime", &kSgDrawTime},
            {"holstertime", &kSgHolsterTime}, {"sink", &kSgSink}, {"scale", &kSgScaleSet}, {"pumptravel", &kSgPumpTravel}, {"camside", &kSgCamSide},
            {"camup", &kSgCamUp}, {"camback", &kSgCamBack}, {"lfinger", &kLFinger}, {"lcurl", &kLCurl}, {"lthumb", &kLThumb},
            {"lalong", &kLHandAlong}, {"lthick", &kLHandThick}, {"elbowout", &kSgElbowOut}, {"elbowdown", &kSgElbowDown},
            {"rcurl1", &kSgRCurl1}, {"rcurl2", &kSgRCurl2}, {"rthumb1", &kSgRThumb1}, {"rthumb2", &kSgRThumb2}, {"sglroll", &kSgLRoll},
            {"sglfwd", &kSgLFwd}, {"sglalong", &kSgLAlong}, {"sglthick", &kSgLThick}, {"sglcurl1", &kSgLCurl1}, {"sglcurl2", &kSgLCurl2},
            {"sglthumb1", &kSgLThumb1}, {"sglthumb2", &kSgLThumb2}, {"lelbowin", &kSgLElbowIn}, {"headshift", &kSgHeadShift},
            {"settle", &kSgSettle}, {"pumpdip", &kSgPumpDip}, {"pumpstretch", &kSgPumpStretch}, {"lthumbaim", &kSgLThumbAim},
            {"rthumbaim", &kSgRThumbAim}, {"bellymargin", &kSgBellyMargin}, {"twistfa", &kSgTwistFA}, {"twistua", &kSgTwistUA}, {"twistfal", &kSgTwistFAL}, {"twistual", &kSgTwistUAL},
            {"lstraight", &kSgLStraight}, {"rstraight", &kSgRStraight}, {"chingap", &kSgChinGap},
            {"lrollsearch", &kSgLRollSearch}, {"neckshare", &kSgNeckShare}, {"seat", &kSgSeat}, {"seatpress", &kSgSeatPress},
            {"seattol", &kSgSeatTol}, {"meshmargin", &kSgMeshMargin}, {"ltwistcost", &kSgLTwistCost}, {"swivelopt", &kSgSwivelOpt}, {"swivelpull", &kSgSwivelPull},
            {"swivelmin", &kSgSwivelMin}, {"swivelmax", &kSgSwivelMax}, {"armbodyw", &kSgArmBodyW}, {"swiveltau", &kSgSwivelTau}, {"pumpcant", &kSgPumpCant}, {"pumpshoulder", &kSgPumpShoulder},
            {"backrel", &kSgBackRel}, {"pullout", &kSgPullOut}, {"pocketclear", &kSgPocketClear}, {"swingout", &kSgSwingOut},
            {"swingfwd", &kSgSwingFwd}, {"walltilt", &kSgWallTilt}, {"pumpcamback", &kSgPumpCamBack}, {"pumpcamhome", &kSgPumpCamHome}, {"padyaw", &kSgPadYaw}, {"padpitch", &kSgPadPitch}, {"aimfollow", &kSgAimFollow}, {"drawpath", &kSgDrawPath}, {"wallreach", &kSgWallReach}, {"wallon", &kSgWallOn}, {"swiveloffw", &kSgSwivelOffW}, {"swivelminr", &kSgSwivelMinR},
            {"swivelmaxr", &kSgSwivelMaxR}, {"swivelminl", &kSgSwivelMinL}, {"swivelmaxl", &kSgSwivelMaxL}, {"armbodyr", &kSgArmBodyR}};
        char name[32] = {0};
        float v = 0;
        if (sscanf(arg, "%31[^,],%f", name, &v) == 2) {
            for (auto& e : t)
                if (!strcmp(e.name, name)) *e.v = v;
            const CharShape* c = CharShapeNow();   // (the stock length is per model: his entry)
            if (!strcmp(name, "stocklen") && c && c - kCharShapes < 5) kSgStockFit[c - kCharShapes] = v;
        }
        char line[4000];
        int m = 0;
        for (auto& e : t) m += snprintf(line + m, sizeof(line) - m, " %s=%.3f", e.name, *e.v);
        Log("sgtune:%s", line);
    } else return false;
    return true;
}
