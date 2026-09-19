#pragma once
//
// Actuator channel description — the data half of the body layer. A board
// profile is just an array of these; nothing here knows what a channel is
// "for" (motor, LED, pump, ...), only how to drive it safely.

#include <cstdint>

enum class ActuatorKind : uint8_t {
    // Single pin, duty cycle mapped from [range_min, range_max] (expected
    // 0..1) onto 0..255. E.g. LED brightness, fan speed, vibration motor.
    kPwmUnipolar,

    // Two pins: a PWM magnitude pin and a direction pin. Value in
    // [range_min, range_max] (expected -1..1); sign selects direction,
    // magnitude maps to duty cycle. E.g. a brushed DC motor via H-bridge.
    kPwmBidirectional,

    // Single pin, simple on/off. Value > 0.5 is "on".
    kDigitalOut,

    // Single pin, 50 Hz servo pulse. Value in [range_min, range_max] maps
    // linearly onto a 1000-2000us pulse width.
    kServo,
};

struct ActuatorSpec {
    const char* name;
    ActuatorKind kind;
    uint8_t pin;
    uint8_t aux_pin;        // direction pin for kPwmBidirectional; unused otherwise
    float range_min;
    float range_max;
    float max_rate_per_s;   // max |change| per second the channel will accept; 0 = unlimited
    float cost_per_unit;    // scales |value| into an abstract exertion cost (physiology, later)
};
