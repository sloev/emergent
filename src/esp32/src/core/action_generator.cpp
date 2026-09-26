#include "core/action_generator.h"

#include <Arduino.h>
#include <esp_system.h>

#include <cmath>

#include "core/tuning.h"

namespace {
float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
}  // namespace

void ActionGenerator::begin(Body& body) {
    (void)body;
    rng_state_ = esp_random();
    if (rng_state_ == 0) rng_state_ = 1;  // xorshift is fixed at 0 forever; never let it land there
    Serial.print("[action_gen] rng seed: ");
    Serial.println(rng_state_);
}

void ActionGenerator::set_seed(uint32_t seed) { rng_state_ = seed == 0 ? 1 : seed; }

float ActionGenerator::noise() {
    // xorshift32 — small, fast, and (unlike ESP32's random()) fully
    // determined by its state, so a captured seed reproduces the exact same
    // exploration sequence.
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return (static_cast<float>(rng_state_) / 4294967295.0f) * 2.0f - 1.0f;
}

void ActionGenerator::tick(Body& body, Physiology& phys, ContingencyMemory& contingency,
                            SpatialMemory& spatial) {
    uint32_t now = millis();

    uint32_t action_code = 0;
    float confidence = 0.0f;
    bool have_bias = contingency.query_bias(contingency.last_sensor_code(), action_code, confidence);

    uint32_t current_sig = SpatialMemory::compute_signature(body);
    uint32_t best_sig = 0;
    float best_score = 0.0f;
    bool have_place = spatial.best_cell(phys, best_sig, best_score);

    // Explore/exploit nudge, not a direction (see header comment): widen
    // noise if a better-scoring place than "here" is remembered, narrow it
    // if "here" already looks like the best one known.
    float spatial_nudge = 0.0f;
    if (have_place && best_score > 0.0f) {
        float magnitude = clampf(best_score * kTuning.action.spatial_gain, 0.0f, kTuning.action.spatial_nudge_max);
        spatial_nudge = (best_sig == current_sig) ? -magnitude : magnitude;
    }

    float fuzz = phys.fuzz_scale();

    // Independent of any single channel: how much pressure there is to stop
    // spending and settle toward the zero-cost state. Every ActuatorKind's
    // cost() is |value| * cost_per_unit, so 0 is the rest state for all of
    // them regardless of what they physically are.
    float rest_pressure = clampf(phys.drive(PhysVar::kFatigue) + phys.drive(PhysVar::kEnergy), 0.0f, 1.0f);

    for (size_t i = 0; i < body.actuator_count(); i++) {
        Actuator& a = body.actuator_at(i);
        if (a.manual_override_active(now)) continue;

        float span = a.spec().range_max - a.spec().range_min;

        float baseline = a.value();
        float rest_pull = -baseline * rest_pressure * kTuning.action.rest_pull_gain;
        float noise_term = noise() * fuzz * span * kTuning.action.noise_gain * (1.0f + spatial_nudge);

        float bias = 0.0f;
        if (have_bias) {
            int8_t dir = ContingencyMemory::decode_channel(action_code, i);
            bias = static_cast<float>(dir) * confidence * span * kTuning.action.contingency_gain;
        }

        float target = clampf(baseline + rest_pull + noise_term + bias, a.spec().range_min, a.spec().range_max);
        a.write(target);
    }
}
