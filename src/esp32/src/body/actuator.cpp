#include "body/actuator.h"

#include <Arduino.h>
#include <cmath>

namespace {
float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

constexpr int kPwmFreqHz = 5000;
constexpr int kPwmResolutionBits = 8;

constexpr int kServoFreqHz = 50;
constexpr int kServoResolutionBits = 14;
constexpr float kServoMinPulseUs = 1000.0f;
constexpr float kServoMaxPulseUs = 2000.0f;

// All PWM-capable actuator kinds manage their own ledc channel rather than
// going through analogWrite(), so channel allocation never collides with it.
// ESP32 has 16 ledc channels; each PWM/servo actuator claims one for life.
uint8_t allocate_ledc_channel() {
    static uint8_t next_channel = 0;
    return next_channel++;
}
}  // namespace

void Actuator::begin() {
    if (spec_.name == nullptr) return;  // unused slot

    switch (spec_.kind) {
        case ActuatorKind::kPwmUnipolar:
            channel_ = allocate_ledc_channel();
            ledcSetup(channel_, kPwmFreqHz, kPwmResolutionBits);
            ledcAttachPin(spec_.pin, channel_);
            break;
        case ActuatorKind::kPwmBidirectional:
            pinMode(spec_.aux_pin, OUTPUT);
            channel_ = allocate_ledc_channel();
            ledcSetup(channel_, kPwmFreqHz, kPwmResolutionBits);
            ledcAttachPin(spec_.pin, channel_);
            break;
        case ActuatorKind::kDigitalOut:
            pinMode(spec_.pin, OUTPUT);
            break;
        case ActuatorKind::kServo:
            channel_ = allocate_ledc_channel();
            ledcSetup(channel_, kServoFreqHz, kServoResolutionBits);
            ledcAttachPin(spec_.pin, channel_);
            break;
    }

    current_ = 0.0f;
    last_write_us_ = micros();
    initialized_ = true;
    drive_hardware();
}

float Actuator::write(float value) {
    if (spec_.name == nullptr) return 0.0f;

    float target = clampf(value, spec_.range_min, spec_.range_max);

    if (initialized_ && spec_.max_rate_per_s > 0.0f) {
        uint32_t now = micros();
        float elapsed_s = (now - last_write_us_) / 1'000'000.0f;
        float max_delta = spec_.max_rate_per_s * elapsed_s;
        float delta = target - current_;
        if (delta > max_delta) delta = max_delta;
        if (delta < -max_delta) delta = -max_delta;
        target = current_ + delta;
        last_write_us_ = now;
    } else {
        last_write_us_ = micros();
    }

    current_ = target;
    drive_hardware();
    return current_;
}

float Actuator::write_manual(float value) {
    float applied = write(value);
    last_manual_ms_ = millis();
    return applied;
}

bool Actuator::manual_override_active(uint32_t now_ms, uint32_t window_ms) const {
    return last_manual_ms_ != 0 && (now_ms - last_manual_ms_) < window_ms;
}

float Actuator::cost() const {
    return fabsf(current_) * spec_.cost_per_unit;
}

void Actuator::drive_hardware() {
    switch (spec_.kind) {
        case ActuatorKind::kPwmUnipolar: {
            float span = spec_.range_max - spec_.range_min;
            float frac = span > 0.0f ? (current_ - spec_.range_min) / span : 0.0f;
            ledcWrite(channel_, static_cast<uint32_t>(clampf(frac, 0.0f, 1.0f) * 255.0f));
            break;
        }
        case ActuatorKind::kPwmBidirectional: {
            float span = fmaxf(fabsf(spec_.range_min), fabsf(spec_.range_max));
            float frac = span > 0.0f ? fabsf(current_) / span : 0.0f;
            digitalWrite(spec_.aux_pin, current_ >= 0.0f ? HIGH : LOW);
            ledcWrite(channel_, static_cast<uint32_t>(clampf(frac, 0.0f, 1.0f) * 255.0f));
            break;
        }
        case ActuatorKind::kDigitalOut:
            digitalWrite(spec_.pin, current_ > 0.5f ? HIGH : LOW);
            break;
        case ActuatorKind::kServo: {
            float span = spec_.range_max - spec_.range_min;
            float frac = span > 0.0f ? (current_ - spec_.range_min) / span : 0.5f;
            float pulse_us = kServoMinPulseUs + clampf(frac, 0.0f, 1.0f) * (kServoMaxPulseUs - kServoMinPulseUs);
            float period_us = 1'000'000.0f / kServoFreqHz;
            uint32_t max_duty = (1u << kServoResolutionBits) - 1;
            uint32_t duty = static_cast<uint32_t>((pulse_us / period_us) * max_duty);
            ledcWrite(channel_, duty);
            break;
        }
    }
}
