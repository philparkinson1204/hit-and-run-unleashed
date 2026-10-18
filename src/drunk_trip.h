// The natural trips' timing (drunk.inc), on its own so the trip-rate test (tools/lab/unit/trip_rate.cpp) runs the game's own code.
//
// Round 4 (review: "replace the unreliable legless-walk test with deterministic, seeded or explicitly triggered scenarios; test natural
// trip frequency separately from the forced fall/recovery mechanics"). A trip used to be a coin tossed every frame (chance rate x dt)
// on the stream the wobble also draws from, so when one came hung on every frame's length and on every draw before it: no seed could
// repeat a walk's trips. Now a hazard process on a stream of its own: walking, the hazard climbs at the trip rate (trips a second) and
// a trip comes when it reaches the next of a row of exponential draws - the same rate as before, and for one seed the same trips at
// the same hazard at any frame rate (to the frame).
#pragma once
#include <stdint.h>
#include <math.h>

struct DrTripGen {
    uint32_t s;      // the stream
    double haz;      // the hazard walked up since the last trip (double: summed over thousands of frames)
    float next;      // the hazard the next trip comes at (an exponential draw, mean 1)
    int n;           // trips so far
    double total;    // (report) all the hazard walked up
    uint32_t seed;   // (report) what it was seeded with
};

static inline float DrTripU(uint32_t& s)   // [0, 1)
{
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFFFFu) / 16777216.0f;
}

static inline void DrTripSeed(DrTripGen& g, uint32_t seed)
{
    DrTripGen z = {};
    g = z;
    g.seed = seed;
    g.s = seed * 2654435761u + 0x9E3779B9u;
    g.next = -logf(1.0f - DrTripU(g.s));
}

// trips a second: walking (faster than 0.8 m/s), drunk past 0.25 - `rate` at level 1, with the level squared (to 1.3) and the speed
// (to 3 m/s)
static inline float DrTripRate(float rate, float level, float speed)
{
    if (speed <= 0.8f || level <= 0.25f) return 0.0f;
    float Lc = fminf(level, 1.3f);
    return rate * Lc * Lc * fminf(speed, 3.0f);
}

// one frame of walking: true when a trip comes (*side: -0.5 .. 0.5, how far off his way it throws him)
static inline bool DrTripStep(DrTripGen& g, float dt, float rate, float level, float speed, float* side)
{
    double h = (double)DrTripRate(rate, level, speed) * dt;
    g.haz += h, g.total += h;
    if (g.haz < g.next) return false;
    g.haz -= g.next;   // (what the frame walked past it carried on: thrown away, each trip came up to a frame late and the lateness added
                       // up - the same seed's trips drifted 30 frames apart between 18 and 144 fps - trip_rate.cpp)
    g.n++;
    if (side) *side = DrTripU(g.s) - 0.5f;
    g.next = -logf(1.0f - DrTripU(g.s));
    return true;
}
