#include "core/action_generator.h"

#include <Arduino.h>
#include <cmath>

namespace {
float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Uniform noise in [-1, 1]. Arduino's random() is good enough here — this
// is exploration noise, not anything security- or fairness-sensitive.
float uniform_noise() { return (random(0, 10001) / 10000.0f) * 2.0f - 1.0f; }

constexpr float kNoiseGain = 0.5f;         // fraction of channel span, at fuzz=1
constexpr float kContingencyGain = 0.3f;   // fraction of span, at confidence=1
constexpr float kSpatialGain = 0.5f;       // scales best_cell() score into an explore/exploit nudge
constexpr float kSpatialNudgeMax = 0.3f;   // clamp so it can widen/narrow noise, never dominate it
}  // namespace

void ActionGenerator::begin(Body& body) { (void)body; }

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
        float magnitude = clampf(best_score * kSpatialGain, 0.0f, kSpatialNudgeMax);
        spatial_nudge = (best_sig == current_sig) ? -magnitude : magnitude;
    }

    float fuzz = phys.fuzz_scale();

    for (size_t i = 0; i < body.actuator_count(); i++) {
        Actuator& a = body.actuator_at(i);
        if (a.manual_override_active(now)) continue;

        float span = a.spec().range_max - a.spec().range_min;

        float baseline = a.value();
        float noise = uniform_noise() * fuzz * span * kNoiseGain * (1.0f + spatial_nudge);

        float bias = 0.0f;
        if (have_bias) {
            int8_t dir = ContingencyMemory::decode_channel(action_code, i);
            bias = static_cast<float>(dir) * confidence * span * kContingencyGain;
        }

        float target = clampf(baseline + noise + bias, a.spec().range_min, a.spec().range_max);
        a.write(target);
    }
}
