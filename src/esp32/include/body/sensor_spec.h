#pragma once
//
// Sensor channel description. As with actuators, this only says how to read
// a channel and normalize it — never what it represents.

#include <cstdint>

enum class SensorKind : uint8_t {
    kAdcNormalized,  // analogRead(pin) / 4095.0 -> 0..1
    kDigitalIn,      // digitalRead(pin) -> 0.0 or 1.0
};

struct SensorSpec {
    const char* name;
    SensorKind kind;
    uint8_t pin;
    float update_rate_hz;  // 0 = read fresh every call, no caching
};
