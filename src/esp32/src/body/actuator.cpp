#include "body/actuator.h"

#include <Arduino.h>
#include <cmath>

#include "body/ledc_allocator.h"
#include "body/rate_limit.h"

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

constexpr float kMaxElapsedS = 1.0f;  // see rate_limit.h's clamp_elapsed_seconds()

// All PWM-capable actuator kinds manage their own LEDC channel rather than
// going through analogWrite(), so allocation never collides with it — see
// body/ledc_allocator.h for why this is bounded instead of a raw counter.
LedcChannelAllocator g_ledc_channels;

uint8_t allocate_ledc_channel() {
    uint8_t ch = g_ledc_channels.allocate();
    if (ch == LedcChannelAllocator::kNone) {
        Serial.println("[actuator] ERROR: all 16 LEDC channels are in use — this actuator will not drive hardware");
    }
    return ch;
}
}  // namespace

void Actuator::begin() {
    if (spec_.name == nullptr) return;  // unused slot

    switch (spec_.kind) {
        case ActuatorKind::kPwmUnipolar:
            channel_ = allocate_ledc_channel();
            hardware_failed_ = (channel_ == LedcChannelAllocator::kNone);
            if (!hardware_failed_) {
                ledcSetup(channel_, kPwmFreqHz, kPwmResolutionBits);
                ledcAttachPin(spec_.pin, channel_);
            }
            break;
        case ActuatorKind::kPwmBidirectional:
            pinMode(spec_.aux_pin, OUTPUT);
            channel_ = allocate_ledc_channel();
            hardware_failed_ = (channel_ == LedcChannelAllocator::kNone);
            if (!hardware_failed_) {
                ledcSetup(channel_, kPwmFreqHz, kPwmResolutionBits);
                ledcAttachPin(spec_.pin, channel_);
            }
            break;
        case ActuatorKind::kDigitalOut:
            pinMode(spec_.pin, OUTPUT);
            break;
        case ActuatorKind::kServo:
            channel_ = allocate_ledc_channel();
            hardware_failed_ = (channel_ == LedcChannelAllocator::kNone);
            if (!hardware_failed_) {
                ledcSetup(channel_, kServoFreqHz, kServoResolutionBits);
                ledcAttachPin(spec_.pin, channel_);
            }
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
        // Unsigned subtraction wraps modulo 2^32 automatically and is exact
        // for any true elapsed time under ~71 minutes (2^32 us) — the
        // standard safe idiom for micros()-based deltas, not a bug by
        // itself. The real edge case is a channel going unwritten for
        // *longer* than that: elapsed_s would under-report the true gap
        // (wrapping back to a small number), which could freeze the
        // actuator for a tick instead of letting it jump. clamp_elapsed_
        // seconds() bounds it to a sane ceiling regardless of cause — see
        // body/rate_limit.h and test/native/test_rate_limit.cpp.
        float elapsed_s = clamp_elapsed_seconds((now - last_write_us_) / 1'000'000.0f, kMaxElapsedS);
        target = apply_rate_limit(current_, target, spec_.max_rate_per_s, elapsed_s);
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
    if (hardware_failed_) return;  // current_/cost() still track state; no pin to touch

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
