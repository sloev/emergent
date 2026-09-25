#pragma once
//
// A hard safety floor, deliberately independent of physiology/drives/the
// action generator. Everything else in core/ is allowed to be wrong,
// stuck, or slow to react — a bad contingency-memory bias, a runaway
// exploration streak, a manual override left engaged — without that ever
// being allowed to cook a motor or run the battery past empty. This module
// knows nothing about "wanting" anything; it only enforces two hard limits:
//
//   - battery critical: if the board's energy_sensor reads below a raw
//     threshold, every actuator is force-zeroed, full stop, regardless of
//     drives or manual overrides.
//   - thermal budget: each actuator has a leaky-bucket accumulator of its
//     own cost() over time (a crude proxy for "how hot has this been
//     running"); sustained high cost trips it and forces that channel to 0
//     until it's cooled down.
//
// This runs *after* the action generator each tick and can override even a
// manual dashboard write — the whole point is that nothing upstream gets a
// vote once a limit is crossed.

#include "body/body.h"

class SafetyMonitor {
public:
    // Cost-seconds an actuator can accumulate before being force-cut. A
    // cost of 1.0 (a channel pinned at its full range with cost_per_unit=1)
    // trips this after kThermalLimit seconds of continuous full drive.
    static constexpr float kThermalLimit = 20.0f;
    static constexpr float kThermalCooldownRate = 2.0f;  // cost-seconds recovered per second

    // Raw (unfiltered) energy_sensor reading below which every actuator is
    // force-zeroed. Deliberately a raw sensor threshold, not h_energy —
    // h_energy is a decayed/coupled internal variable the rest of the
    // system can influence; this reads the sensor directly so nothing
    // upstream can mask a genuinely empty battery.
    static constexpr float kCriticalBatteryLevel = 0.05f;

    void begin(Body& body);

    // Call every behavior tick, after the action generator has written its
    // proposed values.
    void enforce(Body& body, float dt_s);

    bool battery_critical() const { return battery_critical_; }
    bool actuator_tripped(size_t i) const { return i < Body::kMaxActuators && thermal_tripped_[i]; }

private:
    int energy_sensor_index_ = -1;
    float thermal_accum_[Body::kMaxActuators] = {};
    bool thermal_tripped_[Body::kMaxActuators] = {};
    bool battery_critical_ = false;
};
