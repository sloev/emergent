#pragma once
//
// Action generator: each tick, for every actuator channel that hasn't been
// manually driven recently (Actuator::manual_override_active), it proposes:
//
//   next = clamp(baseline + rest_pull + noise + contingency_bias + spatial_nudge)
//
// - baseline: the channel's current value (persistence — "keep doing what
//   you were doing" is the simplest stand-in for a baseline action).
// - rest_pull: pulls toward 0 — the zero-cost state for every actuator kind
//   here (Actuator::cost() is |value| * cost_per_unit) — scaled by fatigue
//   and low-energy pressure. Without this, a channel that drifted to a
//   high-cost value under low drive pressure would just sit there
//   indefinitely; persistence alone has no reason to move it back.
// - noise: uniform random, scaled by Physiology::fuzz_scale() and the
//   channel's own range.
// - contingency_bias: decoded from ContingencyMemory::query_bias() against
//   the sensor-delta pattern just observed, scaled by its confidence.
// - spatial_nudge: NOT a direction — this hardware has no positioning
//   sensor, so SpatialMemory::best_cell() can only say *which* remembered
//   place looks promising, not *which way*. This uses it to modulate
//   explore-vs-exploit instead of steering — noise widens when a better
//   place than "here" is known to exist, narrows when the current place
//   already looks like the best one remembered.
//
// tick() is templated on the body type (duck-typed) so it can run against a
// plain, hardware-free test double under the native test environment as
// well as the real Body — see test/test_behavior_scenarios/fake_body.h.

#include <cstdint>

#include "body/body.h"
#include "core/contingency_memory.h"
#include "core/math_util.h"
#include "core/physiology.h"
#include "core/spatial_memory.h"
#include "core/tuning.h"

class ActionGenerator {
public:
    void begin(Body& body);

    // `now_ms` is the caller's current tick timestamp (millis() in
    // firmware), passed in rather than read internally so this can run
    // against a fake clock in tests.
    template <typename BodyT>
    void tick(BodyT& body, Physiology& phys, ContingencyMemory& contingency, SpatialMemory& spatial,
               uint32_t now_ms) {
        uint32_t action_code = 0;
        float confidence = 0.0f;
        bool have_bias = contingency.query_bias(contingency.last_sensor_code(), action_code, confidence);

        uint32_t current_sig = SpatialMemory::compute_signature(body);
        uint32_t best_sig = 0;
        float best_score = 0.0f;
        bool have_place = spatial.best_cell(phys, best_sig, best_score);

        // Explore/exploit nudge, not a direction (see header comment): widen
        // noise if a better-scoring place than "here" is remembered, narrow
        // it if "here" already looks like the best one known.
        float spatial_nudge = 0.0f;
        if (have_place && best_score > 0.0f) {
            float magnitude = clampf(best_score * kTuning.action.spatial_gain, 0.0f, kTuning.action.spatial_nudge_max);
            spatial_nudge = (best_sig == current_sig) ? -magnitude : magnitude;
        }

        float fuzz = phys.fuzz_scale();

        // Independent of any single channel: how much pressure there is to
        // stop spending and settle toward the zero-cost state. Every
        // ActuatorKind's cost() is |value| * cost_per_unit, so 0 is the
        // rest state for all of them regardless of what they physically are.
        float rest_pressure = clampf(phys.drive(PhysVar::kFatigue) + phys.drive(PhysVar::kEnergy), 0.0f, 1.0f);

        for (size_t i = 0; i < body.actuator_count(); i++) {
            auto& a = body.actuator_at(i);
            if (a.manual_override_active(now_ms)) continue;

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

    // Exploration noise comes from a small xorshift32 PRNG owned here, not
    // Arduino's random() — on ESP32, random() draws from the hardware TRNG
    // unconditionally and can't be seeded, which is fine for normal
    // operation but makes any specific run impossible to reproduce for a
    // controlled experiment. begin() seeds it from the hardware RNG by
    // default (so behavior is still varied every boot) and logs the seed
    // via Serial; set_seed() pins it to a known value instead.
    void set_seed(uint32_t seed) { rng_state_ = seed == 0 ? 1 : seed; }
    uint32_t seed() const { return rng_state_; }

private:
    // xorshift32 — small, fast, and (unlike ESP32's random()) fully
    // determined by its state, so a captured seed reproduces the exact same
    // exploration sequence.
    float noise() {
        rng_state_ ^= rng_state_ << 13;
        rng_state_ ^= rng_state_ >> 17;
        rng_state_ ^= rng_state_ << 5;
        return (static_cast<float>(rng_state_) / 4294967295.0f) * 2.0f - 1.0f;
    }

    uint32_t rng_state_ = 1;
};
