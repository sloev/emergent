#pragma once
//
// Internal physiology and drives: a small vector of slow internal variables
// that drift over time, integrate sensor activity and actuator cost, and
// recover toward a rest value. Drives are scalar pressures derived from how
// far each variable sits outside its preferred band. Tuning constants live
// in tuning.h.
//
// Nothing here knows what a sensor "means". The one numeric coupling the
// design allows — battery into h_energy — is wired by *channel name* in the
// board profile (BoardConfig::energy_sensor), so it stays a fact about how a
// chassis is built rather than a semantic baked into the engine.
//
// begin()/update() are templated on the body type (duck-typed: anything
// exposing Body's actuator/sensor accessors) so they can run against a
// plain, hardware-free test double under the native test environment as
// well as the real Body — see test/test_behavior_scenarios/fake_body.h.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "body/body.h"
#include "core/math_util.h"
#include "core/tuning.h"

enum class PhysVar : uint8_t {
    kEnergy,
    kFatigue,
    kSafety,
    kArousal,
    kCuriosity,
    kBoredom,
    kCount,
};

constexpr size_t kPhysVarCount = static_cast<size_t>(PhysVar::kCount);

namespace physiology_detail {
struct Band {
    float lo, hi;
};

// Comfortable range per variable; drives fire on deviation outside this
// band. h_curiosity is a pressure rather than a regulated quantity, so it's
// exempted from the band formula and its drive is just its own value —
// "comfortable range" for it is the whole [0,1], shown here for the
// dashboard, not used in the drive calc.
inline constexpr Band kBands[kPhysVarCount] = {
    // Hunger starts around half charge on a LiPo read across its full
    // 9.0-12.6 V span; any lower and it arrives minutes before empty.
    /* kEnergy    */ {0.65f, 1.0f},
    /* kFatigue   */ {0.0f, 0.6f},
    /* kSafety    */ {0.7f, 1.0f},
    /* kArousal   */ {0.0f, 0.7f},  // only over-arousal is aversive
    /* kCuriosity */ {0.0f, 1.0f},
    /* kBoredom   */ {0.0f, 0.4f},
};

inline constexpr const char* kNames[kPhysVarCount] = {
    "h_energy", "h_fatigue", "h_safety", "h_arousal", "h_curiosity", "h_boredom",
};
}  // namespace physiology_detail

class Physiology {
public:
    template <typename BodyT>
    void begin(BodyT& body) {
        value_[static_cast<size_t>(PhysVar::kEnergy)] = 0.7f;
        value_[static_cast<size_t>(PhysVar::kFatigue)] = 0.1f;
        value_[static_cast<size_t>(PhysVar::kSafety)] = 0.9f;
        value_[static_cast<size_t>(PhysVar::kArousal)] = 0.3f;
        value_[static_cast<size_t>(PhysVar::kCuriosity)] = 0.3f;
        value_[static_cast<size_t>(PhysVar::kBoredom)] = 0.0f;

        const char* energy_sensor = body.board().energy_sensor;
        energy_sensor_index_ = -1;
        if (energy_sensor != nullptr) {
            for (size_t i = 0; i < body.sensor_count(); i++) {
                if (strcmp(body.sensor_at(i).name(), energy_sensor) == 0) {
                    energy_sensor_index_ = static_cast<int>(i);
                    break;
                }
            }
        }

        for (size_t i = 0; i < body.sensor_count() && i < BodyT::kMaxSensors; i++) {
            prev_sensor_[i] = body.sensor_at(i).last_value();
        }
        has_prev_ = false;
    }

    // Advance one behavior tick. Reads every sensor channel, folds change
    // magnitude and actuator cost into the internal variables, then
    // recomputes drives and the exploration scale. `surprise` (in [0,1],
    // typically ContingencyMemory::last_surprise() from the *previous*
    // tick — see main.cpp) drives curiosity: real prediction error against
    // memory, not a proxy like raw sensor activity.
    template <typename BodyT>
    void update(BodyT& body, float dt_s, float surprise) {
        if (dt_s <= 0.0f) return;

        // --- sense: aggregate *salient* change across every channel ---------
        // Salience is prediction error, not raw change: each channel is
        // predicted to keep changing at the rate it just did (constant
        // velocity), and the miss is compared to that channel's own running
        // jitter (sensory adaptation). Only a miss beyond a few times the
        // jitter counts. So ADC noise isn't an event, a steady turn or a
        // straight run habituates away, and a bump, a new light or a sound
        // stands out.
        float sum_abs_delta = 0.0f;
        float max_abs_delta = 0.0f;
        size_t n = body.sensor_count() < BodyT::kMaxSensors ? body.sensor_count() : BodyT::kMaxSensors;
        float adapt = dt_s / kTuning.senses.adaptation_tau_s;
        for (size_t i = 0; i < n; i++) {
            float v = body.sensor_at(i).read();
            if (has_prev_) {
                float predicted = prev_sensor_[i] + prev_velocity_[i];
                float d = fabsf(v - predicted);
                prev_velocity_[i] = v - prev_sensor_[i];
                float floor = kTuning.senses.salience_k * jitter_[i] + kTuning.senses.min_salient_change;
                float salient = d > floor ? d - floor : 0.0f;
                sum_abs_delta += salient;
                // Startle habituates: a channel that keeps producing the same
                // kind of jolt (a bumper resting against the dock) startles
                // less each time, recovering over habituation_tau_s.
                float startle = salient * (1.0f - habituation_[i]);
                if (startle > max_abs_delta) max_abs_delta = startle;
                if (salient > 0.0f) habituation_[i] += kTuning.senses.habituation_step * (1.0f - habituation_[i]);
                habituation_[i] -= habituation_[i] * (dt_s / kTuning.senses.habituation_tau_s);
                // Outlier-resistant: a real event shouldn't teach the
                // channel that it's noisy.
                float capped = d < 2.0f * floor ? d : 2.0f * floor;
                jitter_[i] += adapt * (capped - jitter_[i]);
            }
            prev_sensor_[i] = v;
        }
        has_prev_ = true;
        activity_ = n > 0 ? sum_abs_delta / n : 0.0f;
        spike_ = max_abs_delta;

        float total_cost = 0.0f;
        for (size_t i = 0; i < body.actuator_count(); i++) {
            total_cost += body.actuator_at(i).cost();
        }

        size_t iE = static_cast<size_t>(PhysVar::kEnergy);
        size_t iF = static_cast<size_t>(PhysVar::kFatigue);
        size_t iS = static_cast<size_t>(PhysVar::kSafety);
        size_t iAr = static_cast<size_t>(PhysVar::kArousal);
        size_t iC = static_cast<size_t>(PhysVar::kCuriosity);
        size_t iB = static_cast<size_t>(PhysVar::kBoredom);

        // --- update: decay / recover / couple ------------------------------
        // Energy: with a real energy sensor, that sensor *is* the
        // metabolism: exertion already shows up as the battery draining, so
        // subtracting a modelled drain as well would count it twice. Only a
        // body with no energy sensor gets the synthetic drain/regen model.
        if (energy_sensor_index_ >= 0) {
            float battery = body.sensor_at(static_cast<size_t>(energy_sensor_index_)).last_value();
            value_[iE] += (battery - value_[iE]) * kTuning.energy.battery_gain * dt_s;
        } else {
            value_[iE] += (kTuning.energy.idle_regen_rate - kTuning.energy.drain_rate * total_cost) * dt_s;
        }
        value_[iE] = clampf(value_[iE], 0.0f, 1.0f);

        // Fatigue: saturating accumulation, dF = g*cost*(1-F) - r*F. It
        // settles at g*c / (g*c + r): comfortable at moderate effort, into
        // the "tired" band only under sustained high effort.
        value_[iF] += (kTuning.fatigue.gain * total_cost * (1.0f - value_[iF]) -
                       kTuning.fatigue.recovery_rate * value_[iF]) *
                      dt_s;
        value_[iF] = clampf(value_[iF], 0.0f, 1.0f);

        value_[iS] += (kTuning.safety.recovery_rate * (1.0f - value_[iS]) - kTuning.safety.drop_gain * spike_) * dt_s;
        value_[iS] = clampf(value_[iS], 0.0f, 1.0f);

        value_[iAr] += (kTuning.arousal.gain * activity_ - kTuning.arousal.decay * value_[iAr]) * dt_s;
        value_[iAr] = clampf(value_[iAr], 0.0f, 1.0f);

        // Real prediction error: `surprise` is how much the last tick's
        // outcome differed from what contingency memory already expected,
        // not a proxy like raw sensor activity.
        value_[iC] += (kTuning.curiosity.gain * surprise - kTuning.curiosity.decay * value_[iC]) * dt_s;
        value_[iC] = clampf(value_[iC], 0.0f, 1.0f);

        // Boredom is habituation: it rises whenever things change less than
        // they usually do *for this body in this place*, and is reset by
        // more-than-usual change. Relative to a slow running average of
        // activity, so a body with sluggish sensors isn't bored forever and
        // one with lively sensors isn't never bored.
        activity_avg_ += (activity_ - activity_avg_) * (dt_s / kTuning.boredom.habituation_tau_s);
        float relative = clampf(activity_ / (activity_avg_ + 1e-4f), 0.0f, 2.0f);
        value_[iB] += (kTuning.boredom.gain * (1.0f - value_[iB]) - kTuning.boredom.reset_gain * relative * value_[iB]) *
                      dt_s;
        value_[iB] = clampf(value_[iB], 0.0f, 1.0f);

        // --- drives ----------------------------------------------------------
        for (size_t i = 0; i < kPhysVarCount; i++) {
            float lo, hi;
            target_band(static_cast<PhysVar>(i), lo, hi);
            float v = value_[i];
            if (v < lo) {
                drive_[i] = (lo - v) / (hi - lo + 1e-6f);
            } else if (v > hi) {
                drive_[i] = (v - hi) / (hi - lo + 1e-6f);
            } else {
                drive_[i] = 0.0f;
            }
        }
        // Curiosity is a pressure, not a regulated quantity — no "comfortable
        // deviation" formula applies; the drive is just how curious it is.
        drive_[iC] = value_[iC];

        // --- exploration scale: how much noise the action generator should
        // inject, in [0.05, 0.8]. Rises with curiosity, boredom, and hunger
        // (food deprivation raises locomotor activity in animals: foraging),
        // falls under threat (freezing). Exploiting what's been learned when
        // hungry comes from the energy-gated reflexes, not from going still.
        float fuzz = 0.3f * drive_[iC] + 0.4f * drive_[iB] + 0.3f * drive_[iE] - 0.3f * drive_[iS];
        fuzz_ = clampf(fuzz, 0.05f, 0.8f);
    }

    float value(PhysVar v) const { return value_[static_cast<size_t>(v)]; }
    float drive(PhysVar v) const { return drive_[static_cast<size_t>(v)]; }

    // For life-state restore — sets a variable's raw value directly, clamped
    // to [0,1] like everything else here. Drives are left to the next
    // update() to recompute from it.
    void set_value(PhysVar v, float val) { value_[static_cast<size_t>(v)] = clampf(val, 0.0f, 1.0f); }

    // Exploration scale in [0.05, 0.8] — how much noise the action generator
    // should inject. Rises with curiosity, boredom and hunger, falls under
    // safety pressure.
    float fuzz_scale() const { return fuzz_; }

    // Mean salient |Δsensor| (beyond each channel's own noise floor) across
    // channels on the last tick; the "something changed" signal that
    // boredom and arousal are built from.
    float sensor_activity() const { return activity_; }

    // Homeostatic distance: the sum of *squared* drives — a single scalar
    // "how much pressure is the organism under right now". Falling
    // total_drive() is "things got better": it's the reward both memories
    // and the learned reflexes use (drive reduction, Keramati & Gutkin 2014,
    // with their n = 2). Squaring makes a large deficit dominate many small
    // fluctuations, so relieving real hunger outweighs sensor jitter.
    // Curiosity is left out on purpose: it rises with surprise, so counting
    // it would make every novel outcome feel like a setback and teach the
    // organism to avoid novelty. Curiosity acts through exploration width
    // (fuzz_scale()) instead.
    float total_drive() const {
        float sum = 0.0f;
        for (size_t i = 0; i < kPhysVarCount; i++) {
            if (i == static_cast<size_t>(PhysVar::kCuriosity)) continue;
            sum += drive_[i] * drive_[i];
        }
        return sum;
    }

    static const char* var_name(PhysVar v) { return physiology_detail::kNames[static_cast<size_t>(v)]; }
    static void target_band(PhysVar v, float& lo, float& hi) {
        lo = physiology_detail::kBands[static_cast<size_t>(v)].lo;
        hi = physiology_detail::kBands[static_cast<size_t>(v)].hi;
    }

private:
    float value_[kPhysVarCount] = {};
    float drive_[kPhysVarCount] = {};
    float prev_sensor_[Body::kMaxSensors] = {};
    float jitter_[Body::kMaxSensors] = {};
    float prev_velocity_[Body::kMaxSensors] = {};
    float habituation_[Body::kMaxSensors] = {};
    bool has_prev_ = false;
    float activity_ = 0.0f;
    float spike_ = 0.0f;
    float activity_avg_ = 0.0f;
    float fuzz_ = 0.03f;
    int energy_sensor_index_ = -1;
};
