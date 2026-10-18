// Homer's extreme skin points: for each joint, the farthest points of the skin it carries, in its own frame, each with the
// second joint it's skinned to (j2), that joint's share of the two (w2) and the point in j2's frame - what dbody.h's
// SkinDepth / JointDepth, the get-up's lift and the palms, knees and feet stand on. Built as the game starts from the
// player's own art\chars\homer_m.p3d and homer_a.p3d (dbody_gen.h; drunkbody.inc starts it and checks it): none of it
// ships with the mod. Empty until then - the drunk body stays off without it.
#pragma once
struct DbPoint { int j; float p[3]; int j2; float w2; float p2[3]; };
struct DbPointTable {
    enum { kMax = 1024 };
    DbPoint pt[kMax];
    int n = 0;
    const DbPoint* begin() const { return pt; }
    const DbPoint* end() const { return pt + n; }
};
static DbPointTable kDbPoints;
