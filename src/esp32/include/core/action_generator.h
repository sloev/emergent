#pragma once
//
// Action generator: each tick, for every actuator channel that hasn't been
// manually driven recently (Actuator::manual_override_active), it proposes:
//
//   next = clamp((reflex + explore) * (1 - rest) + contingency_bias)
//
// - reflex: what the learned sensorimotor map (SensorimotorPolicy) proposes
//   for this channel given current sensors and drives. Starts at 0 for
//   everything; only drive reduction shapes it.
// - explore: Ornstein-Uhlenbeck noise per channel (correlation time
//   tuning.action.noise_tau_s), scaled by Physiology::fuzz_scale() and the
//   channel's span. Correlated noise gives smooth runs and turns instead of
//   per-tick jitter that averages to standing still. Widened or narrowed by
//   the spatial nudge.
// - rest: shrinks everything toward 0, the zero-cost state for every
//   actuator kind (Actuator::cost() is |value| * cost_per_unit), in
//   proportion to fatigue and low-energy pressure.
// - contingency_bias: decoded from ContingencyMemory::query_bias() against
//   the sensor-delta pattern just observed, scaled by its confidence.
// - spatial nudge: NOT a direction. There's no positioning sensor, so
//   SpatialMemory::best_cell() can only say *which* remembered place looks
//   promising, not *which way*. Exploration widens when a better place than
//   "here" is known to exist and narrows when "here" already looks best.
//
// After writing, each channel's exploration sample is handed back to the
// policy as the perturbation its next reward will be credited against.
//
// tick() is templated on the body type (duck-typed) so it can run against a
// plain, hardware-free test double under the native test environment and
// the simulator as well as the real Body.

#include <cmath>
#include <cstdint>

#include "body/body.h"
#include "core/contingency_memory.h"
#include "core/math_util.h"
#include "core/physiology.h"
#include "core/sensorimotor_policy.h"
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
            float magnitude = clampf(best_score * g_tuning.action.spatial_gain, 0.0f, g_tuning.action.spatial_nudge_max);
            spatial_nudge = (best_sig == current_sig) ? -magnitude : magnitude;
        }

        float fuzz = phys.fuzz_scale();

        // Independent of any single channel: how much pressure there is to
        // stop spending and settle toward the zero-cost state.
        float rest_pressure = clampf(phys.drive(PhysVar::kFatigue) + phys.drive(PhysVar::kEnergy), 0.0f, 1.0f);
        float keep = 1.0f - rest_pressure * g_tuning.action.rest_pull_gain;

        constexpr float kDt = 0.05f;  // behavior tick, see main.cpp
        float reflex[SensorimotorPolicy::kMaxOut] = {};
        if (learning_) {
            if (plastic_) policy_.learn(phys, kDt);
            policy_.propose(body, phys, kDt, reflex);
        }

        float ou_decay = expf(-kDt / g_tuning.action.noise_tau_s);
        float ou_kick = sqrtf(1.0f - ou_decay * ou_decay);

        float taken[SensorimotorPolicy::kMaxOut] = {};
        size_t n = body.actuator_count() < SensorimotorPolicy::kMaxOut ? body.actuator_count()
                                                                        : SensorimotorPolicy::kMaxOut;
        for (size_t i = 0; i < n; i++) {
            auto& a = body.actuator_at(i);
            if (a.manual_override_active(now_ms)) continue;

            float span = a.spec().range_max - a.spec().range_min;

            // Unit-variance OU process: persistent, smooth, zero-mean.
            ou_[i] = ou_decay * ou_[i] + ou_kick * gaussian();
            float explore = ou_[i] * fuzz * span * g_tuning.action.noise_gain * (1.0f + spatial_nudge);

            float bias = 0.0f;
            if (have_bias) {
                int8_t dir = ContingencyMemory::decode_channel(action_code, i);
                bias = static_cast<float>(dir) * confidence * span * g_tuning.action.contingency_gain;
            }

            float target = clampf((reflex[i] + explore) * keep + bias, a.spec().range_min, a.spec().range_max);
            a.write(target);
            // Credit the exploration sample itself (zero-mean by
            // construction), not "executed minus proposed": when a proposal
            // hits a range or rate limit that difference becomes a constant
            // offset, which biases every weight the same way and runs away.
            taken[i] = explore * keep;
        }

        if (learning_) policy_.record(taken, kDt);
    }

    // Learned reflexes start empty for this body's channel layout. Call
    // once after the body is up (and again if its channels change).
    template <typename BodyT>
    void begin_learning(BodyT& body) {
        policy_.begin(body);
    }

    // Ablation switch: with learning off, reflexes contribute nothing and
    // nothing is learned; the rest of the generator is unchanged.
    void set_learning(bool on) { learning_ = on; }
    // Reflexes still act but stop changing (for experiments that fix them).
    void set_plasticity(bool on) { plastic_ = on; }
    SensorimotorPolicy& policy() { return policy_; }
    const SensorimotorPolicy& policy() const { return policy_; }

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

    // Approximately standard normal (Irwin-Hall: sum of four uniforms,
    // rescaled to unit variance). Good enough for exploration, no libm.
    float gaussian() { return (noise() + noise() + noise() + noise()) * 0.8660254f; }

    uint32_t rng_state_ = 1;
    float ou_[SensorimotorPolicy::kMaxOut] = {};
    SensorimotorPolicy policy_;
    bool learning_ = true;
    bool plastic_ = true;
};
