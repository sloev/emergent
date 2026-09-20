#pragma once

#include "body/actuator_spec.h"

// Runtime handle for one actuator channel: clamps, rate-limits, and drives
// the hardware described by an ActuatorSpec.
class Actuator {
public:
    Actuator() = default;
    explicit Actuator(const ActuatorSpec& spec) : spec_(spec) {}

    void begin();

    // Clamps to [range_min, range_max], applies the rate limit, then drives
    // the pin(s). Returns the value actually applied (may differ from
    // `value` if rate-limited).
    float write(float value);

    // Same as write(), but marks this channel as human-controlled for the
    // next `window_ms` (see manual_override_active()) — used by the
    // dashboard's manual sliders so the action generator backs off a
    // channel a person is actively driving instead of fighting it.
    float write_manual(float value);

    // True if write_manual() was called within the last `window_ms`.
    bool manual_override_active(uint32_t now_ms, uint32_t window_ms = 3000) const;

    float value() const { return current_; }
    float cost() const;
    const char* name() const { return spec_.name; }
    const ActuatorSpec& spec() const { return spec_; }

private:
    void drive_hardware();

    ActuatorSpec spec_{};
    float current_ = 0.0f;
    uint32_t last_write_us_ = 0;
    uint32_t last_manual_ms_ = 0;
    uint8_t channel_ = 0;  // ledc channel, for PWM/servo kinds
    bool initialized_ = false;
};
