#include "engine.h"

#include "settings.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kUnitsPerNotch = 120.0;

// Never inject more than three notches in a single tick; keeps a stalled
// worker from firing a huge wheel jump at the target application.
constexpr long long kMaxStepUnits = 360;

// Progressive acceleration (when enabled): continuous fast scrolling ramps
// the distance multiplier in three stages:
//   - after a short 120 ms warm-up it quickly reaches +50 %,
//   - +100 % at around 500 ms of fast scrolling,
//   - +400 % at around 1.1 s, then stays there.
// Any clearly slower notch brakes straight back to 1:1 and discards
// not-yet-delivered momentum.
constexpr double kWarmupEndMs = 120.0;
constexpr double kEarlyEndMs = 220.0;   // +50 %
constexpr double kMidEndMs = 500.0;     // +100 %
constexpr double kLateEndMs = 1100.0;   // +400 %
constexpr double kBoostEarly = 1.5;
constexpr double kBoostMid = 2.0;
constexpr double kBoostLate = 5.0;

double SmoothStep01(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

double BoostForFastRun(double fastRunMs) {
    if (fastRunMs <= kWarmupEndMs) return 1.0;
    if (fastRunMs <= kEarlyEndMs) {
        const double t = (fastRunMs - kWarmupEndMs) / (kEarlyEndMs - kWarmupEndMs);
        return 1.0 + (kBoostEarly - 1.0) * t;
    }
    if (fastRunMs <= kMidEndMs) {
        const double t = (fastRunMs - kEarlyEndMs) / (kMidEndMs - kEarlyEndMs);
        return kBoostEarly + (kBoostMid - kBoostEarly) * t;
    }
    if (fastRunMs <= kLateEndMs) {
        const double t = (fastRunMs - kMidEndMs) / (kLateEndMs - kMidEndMs);
        return kBoostMid + (kBoostLate - kBoostMid) * t;
    }
    return kBoostLate;
}

}  // namespace

void ScrollEngine::addNotch(double deltaUnits, double nowMs, bool precise) {
    if (deltaUnits == 0.0) return;
    precise_ = precise;

    const int smoothness = app::g_smoothness.load(std::memory_order_relaxed);
    const int inertia = app::g_inertia.load(std::memory_order_relaxed);
    const int sensitivity = app::g_sensitivity.load(std::memory_order_relaxed);
    const bool acceleration = app::g_acceleration.load(std::memory_order_relaxed);

    const double rawGap = nowMs - lastNotch_;
    double dt = rawGap;
    if (!(dt > 0.0) || dt > 600.0) dt = 600.0;

    // A long pause starts a new gesture: do not carry acceleration over.
    if (rawGap < 0.0 || rawGap > 150.0) {
        fastRunMs_ = 0.0;
        boost_ = 1.0;
    }
    lastNotch_ = nowMs;

    // Speed is measured from the spacing of wheel notches: a deliberate notch
    // every 200 ms is "slow", a flick every ~33 ms is "fast". Only fast
    // gestures stretch the glide and add momentum.
    const double notches = std::abs(deltaUnits) / kUnitsPerNotch;
    const double speed = notches / dt;  // notches per millisecond
    double f = (speed - 0.005) / 0.025;  // 5 .. 30 notches / second
    f = SmoothStep01(f);

    bool brake = false;
    if (acceleration) {
        const bool fastNotch = f >= 0.60 && rawGap <= 90.0;
        const bool slowNotch = f <= 0.35;

        if (fastNotch) {
            fastRunMs_ = std::min(3000.0, fastRunMs_ + dt);
            boost_ = BoostForFastRun(fastRunMs_);
        } else if (slowNotch) {
            // The user is physically slowing down: brake immediately, drop
            // any accumulated boost and stay precise from here on. If extra
            // momentum from a previous fast gesture is still in flight, the
            // current slow notch cancels it outright.
            if (fastRunMs_ > 0.0 || boost_ > 1.0 || momentum_) brake = true;
            fastRunMs_ = 0.0;
            boost_ = 1.0;
            momentum_ = false;
        }
        // Medium-speed notches pause the ramp but keep the current boost.
    } else {
        fastRunMs_ = 0.0;
        boost_ = 1.0;
    }

    const double baseTau = 18.0 + 1.1 * static_cast<double>(smoothness);
    tau_ = baseTau * (0.5 + 1.1 * f);
    if (brake) tau_ = baseTau * 0.35;  // tight, immediate delivery after a brake

    // Slow input travels exactly as far as the physical wheel did (gain 1),
    // fast input adds up to twice the distance as glide-out momentum. The
    // acceleration multiplier only grows during sustained fast scrolling.
    const double gain = 1.0 + (static_cast<double>(inertia) / 100.0) * 2.0 * f;
    const double distance =
        (deltaUnits / kUnitsPerNotch) * (static_cast<double>(sensitivity) / 100.0) * gain * boost_;

    if (gain > 1.001 || boost_ > 1.001) momentum_ = true;

    if (brake) {
        // Discard not-yet-delivered momentum: keep only the current slow notch.
        target_ = pos_ + distance;
    } else {
        target_ += distance;
    }

    if (!running_) {
        lastTick_ = nowMs;
        running_ = true;
    }
}

int ScrollEngine::tick(double nowMs) {
    if (!running_) return 0;

    double dt = nowMs - lastTick_;
    lastTick_ = nowMs;
    if (dt <= 0.0) return 0;
    if (dt > 50.0) dt = 50.0;

    const double alpha = 1.0 - std::exp(-dt / tau_);
    pos_ += (target_ - pos_) * alpha;

    const double quantum = precise_ ? 1.0 : kUnitsPerNotch;

    // Emit whenever the smoothed position crosses half a quantum. Emitting at
    // the half point (instead of waiting for a whole notch) means a single
    // notch is delivered promptly and the stream is evenly spread out.
    const long long desiredUnits =
        std::llround(pos_ * (kUnitsPerNotch / quantum)) * static_cast<long long>(quantum);

    long long diff = desiredUnits - sentUnits_;
    if (diff > kMaxStepUnits) diff = kMaxStepUnits;
    if (diff < -kMaxStepUnits) diff = -kMaxStepUnits;
    sentUnits_ += diff;

    // No further quantum boundary can be crossed once the rounding of the
    // final target has been reached, so stop and fold the invisible residue
    // into the position.
    const long long finalUnits =
        std::llround(target_ * (kUnitsPerNotch / quantum)) * static_cast<long long>(quantum);
    if (sentUnits_ == finalUnits) {
        running_ = false;
        pos_ = target_;
        momentum_ = false;
        return static_cast<int>(diff);
    }

    return static_cast<int>(diff);
}

void ScrollEngine::reset() {
    target_ = 0.0;
    pos_ = 0.0;
    sentUnits_ = 0;
    running_ = false;
    lastNotch_ = -1e9;
    tau_ = 60.0;
    fastRunMs_ = 0.0;
    boost_ = 1.0;
    precise_ = false;
    momentum_ = false;
}
