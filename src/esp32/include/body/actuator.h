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

    float value() const { return current_; }
    float cost() const;
    const char* name() const { return spec_.name; }
    const ActuatorSpec& spec() const { return spec_; }

private:
    void drive_hardware();

    ActuatorSpec spec_{};
    float current_ = 0.0f;
    uint32_t last_write_us_ = 0;
    uint8_t channel_ = 0;  // ledc channel, for PWM/servo kinds
    bool initialized_ = false;
};
