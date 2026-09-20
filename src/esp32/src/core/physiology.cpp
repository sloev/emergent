#include "core/physiology.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

namespace {
float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Tuning constants. All rates are "per second" so behavior is independent of
// tick rate. None of this is claimed to be biologically accurate — it is the
// smallest set of decay/recovery/coupling rules that makes each variable in
// docs/synth-behavior.md §6.1 drift the way that section describes.
constexpr float kEnergyIdleRegenRate = 0.01f;   // slow trickle, "idle metabolism"
constexpr float kEnergyDrainRate = 0.08f;       // scaled by summed actuator cost
constexpr float kEnergyBatteryGain = 0.4f;      // pull rate toward energy_sensor reading

constexpr float kFatigueGain = 0.12f;           // scaled by summed actuator cost
constexpr float kFatigueRecoveryRate = 0.03f;

constexpr float kSafetyDropGain = 1.2f;         // scaled by single-tick sensor spike
constexpr float kSafetyRecoveryRate = 0.05f;

constexpr float kArousalGain = 1.5f;
constexpr float kArousalDecay = 0.3f;

constexpr float kCuriosityGain = 1.0f;
constexpr float kCuriosityDecay = 0.25f;

constexpr float kBoredomGain = 0.15f;
constexpr float kBoredomResetGain = 2.0f;

struct Band {
    float lo, hi;
};

// Comfortable range per variable; drives fire on deviation outside this band
// (docs/synth-behavior.md §6.2 compute_drives). h_curiosity is a pressure
// rather than a regulated quantity, so it's exempted from the band formula
// below and its drive is just its own value — "comfortable range" for it is
// the whole [0,1], shown here for the dashboard, not used in the drive calc.
constexpr Band kBands[kPhysVarCount] = {
    /* kEnergy    */ {0.4f, 1.0f},
    /* kFatigue   */ {0.0f, 0.6f},
    /* kSafety    */ {0.7f, 1.0f},
    /* kArousal   */ {0.2f, 0.7f},
    /* kCuriosity */ {0.0f, 1.0f},
    /* kBoredom   */ {0.0f, 0.4f},
    /* kSocial    */ {0.0f, 1.0f},
};

constexpr const char* kNames[kPhysVarCount] = {
    "h_energy", "h_fatigue", "h_safety", "h_arousal", "h_curiosity", "h_boredom", "h_social",
};
}  // namespace

const char* Physiology::var_name(PhysVar v) { return kNames[static_cast<size_t>(v)]; }

void Physiology::target_band(PhysVar v, float& lo, float& hi) {
    lo = kBands[static_cast<size_t>(v)].lo;
    hi = kBands[static_cast<size_t>(v)].hi;
}

void Physiology::begin(Body& body) {
    value_[static_cast<size_t>(PhysVar::kEnergy)] = 0.7f;
    value_[static_cast<size_t>(PhysVar::kFatigue)] = 0.1f;
    value_[static_cast<size_t>(PhysVar::kSafety)] = 0.9f;
    value_[static_cast<size_t>(PhysVar::kArousal)] = 0.3f;
    value_[static_cast<size_t>(PhysVar::kCuriosity)] = 0.3f;
    value_[static_cast<size_t>(PhysVar::kBoredom)] = 0.0f;
    value_[static_cast<size_t>(PhysVar::kSocial)] = 0.0f;

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

    for (size_t i = 0; i < body.sensor_count() && i < Body::kMaxSensors; i++) {
        prev_sensor_[i] = body.sensor_at(i).last_value();
    }
    has_prev_ = false;
}

void Physiology::update(Body& body, float dt_s) {
    if (dt_s <= 0.0f) return;

    // --- sense: aggregate change across every channel -----------------
    float sum_abs_delta = 0.0f;
    float max_abs_delta = 0.0f;
    size_t n = body.sensor_count() < Body::kMaxSensors ? body.sensor_count() : Body::kMaxSensors;
    for (size_t i = 0; i < n; i++) {
        float v = body.sensor_at(i).read();
        if (has_prev_) {
            float d = fabsf(v - prev_sensor_[i]);
            sum_abs_delta += d;
            if (d > max_abs_delta) max_abs_delta = d;
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
    value_[iE] += (kEnergyIdleRegenRate - kEnergyDrainRate * total_cost) * dt_s;
    if (energy_sensor_index_ >= 0) {
        float battery = body.sensor_at(static_cast<size_t>(energy_sensor_index_)).last_value();
        value_[iE] += (battery - value_[iE]) * kEnergyBatteryGain * dt_s;
    }
    value_[iE] = clampf(value_[iE], 0.0f, 1.0f);

    value_[iF] += (kFatigueGain * total_cost - kFatigueRecoveryRate) * dt_s;
    value_[iF] = clampf(value_[iF], 0.0f, 1.0f);

    value_[iS] += (kSafetyRecoveryRate * (1.0f - value_[iS]) - kSafetyDropGain * spike_) * dt_s;
    value_[iS] = clampf(value_[iS], 0.0f, 1.0f);

    value_[iAr] += (kArousalGain * activity_ - kArousalDecay * value_[iAr]) * dt_s;
    value_[iAr] = clampf(value_[iAr], 0.0f, 1.0f);

    // Placeholder novelty signal: proportional to raw sensor change. Once
    // contingency memory (v0.5.0) exists, this should become real prediction
    // error instead of a stand-in for it.
    value_[iC] += (kCuriosityGain * activity_ - kCuriosityDecay * value_[iC]) * dt_s;
    value_[iC] = clampf(value_[iC], 0.0f, 1.0f);

    value_[iB] += (kBoredomGain * (1.0f - activity_) - kBoredomResetGain * activity_ * value_[iB]) * dt_s;
    value_[iB] = clampf(value_[iB], 0.0f, 1.0f);

    // h_social has no sensor source yet (needs another robot/human present
    // to correlate against); left at rest until a later release wires one.

    // --- drives ----------------------------------------------------------
    for (size_t i = 0; i < kPhysVarCount; i++) {
        float lo = kBands[i].lo, hi = kBands[i].hi;
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

    // --- exploration scale (docs/synth-behavior.md §6.2) -----------------
    float fuzz = 0.6f * drive_[iC] + 0.4f * drive_[iB] - 0.4f * drive_[iE] - 0.3f * drive_[iS];
    fuzz_ = clampf(fuzz, 0.03f, 0.8f);
}
