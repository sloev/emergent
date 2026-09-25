#pragma once
//
// Sensor channel description. As with actuators, this only says how to read
// a channel and normalize it — never what it represents.

#include <cstdint>

enum class SensorKind : uint8_t {
    kAdcNormalized,  // analogRead(pin) / 4095.0 -> 0..1
    kDigitalIn,      // digitalRead(pin) -> 0.0 or 1.0
    kWifiRssi,       // signal strength of a named SSID -> 0..1 (see sensor.cpp)
};

struct SensorSpec {
    const char* name;
    SensorKind kind;
    uint8_t pin;            // unused for kWifiRssi
    float update_rate_hz;   // 0 = read fresh every call, no caching. For
                             // kWifiRssi this instead means "don't start a
                             // new scan more often than this" — a real scan
                             // cycle takes a few seconds regardless.
    const char* target_ssid = nullptr;  // kWifiRssi only: SSID to track (e.g. a
                                         // charging station's beacon, docs/
                                         // synth-behavior.md §11.3's "RF Lure")
};
