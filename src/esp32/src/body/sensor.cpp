#include "body/sensor.h"

#include <Arduino.h>

void Sensor::begin() {
    if (spec_.name == nullptr) return;  // unused slot

    if (spec_.kind == SensorKind::kDigitalIn) {
        pinMode(spec_.pin, INPUT);
    }
    // ADC pins need no pinMode() on ESP32.

    has_reading_ = false;
}

float Sensor::read() {
    if (spec_.name == nullptr) return 0.0f;

    uint32_t now = micros();
    if (has_reading_ && spec_.update_rate_hz > 0.0f) {
        float period_us = 1'000'000.0f / spec_.update_rate_hz;
        if ((now - last_read_us_) < static_cast<uint32_t>(period_us)) {
            return last_value_;
        }
    }

    last_value_ = read_hardware();
    last_read_us_ = now;
    has_reading_ = true;
    return last_value_;
}

float Sensor::read_hardware() {
    switch (spec_.kind) {
        case SensorKind::kAdcNormalized:
            return analogRead(spec_.pin) / 4095.0f;
        case SensorKind::kDigitalIn:
            return digitalRead(spec_.pin) ? 1.0f : 0.0f;
    }
    return 0.0f;
}
