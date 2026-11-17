#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

// Turns discrete wheel notches into a smooth, speed-aware stream of wheel
// deltas. Distances are tracked in wheel units (120 units == one notch).
class ScrollEngine {
public:
    // deltaUnits is the raw wheel delta reported by the hook (usually +/-120).
    // precise selects sub-notch output (1 unit quantum) instead of whole
    // notches (120 unit quantum).
    void addNotch(double deltaUnits, double nowMs, bool precise);

    // Returns the signed wheel units that should be injected now (0 if none).
    int tick(double nowMs);

    bool active() const { return running_; }
    void reset();

private:
    double target_ = 0.0;   // notches, desired position
    double pos_ = 0.0;      // notches, smoothed position
    double lastNotch_ = -1e9;
    double lastTick_ = 0.0;
    double tau_ = 60.0;     // exponential smoothing time constant, ms
    double fastRunMs_ = 0.0;  // continuous fast-scroll duration
    double boost_ = 1.0;      // progressive acceleration multiplier
    long long sentUnits_ = 0;
    bool running_ = false;
    bool precise_ = false;
    bool momentum_ = false;   // extra (inertia/boost) distance is still in flight
};