#pragma once
//
// Internal physiology and drives (docs/synth-behavior.md §6).
//
// A small vector of slow internal variables that drift over time, integrate
// sensor activity and actuator cost, and recover toward a rest value. Drives
// are scalar pressures derived from how far each variable sits outside its
// preferred band.
//
// Nothing here knows what a sensor "means". The one numeric coupling the
// design allows — battery into h_energy — is wired by *channel name* in the
// board profile (BoardConfig::energy_sensor), so it stays a fact about how a
// chassis is built rather than a semantic baked into the engine.

#include <cstddef>
#include <cstdint>

#include "body/body.h"

enum class PhysVar : uint8_t {
    kEnergy,
    kFatigue,
    kSafety,
    kArousal,
    kCuriosity,
    kBoredom,
    kSocial,
    kCount,
};

constexpr size_t kPhysVarCount = static_cast<size_t>(PhysVar::kCount);

class Physiology {
public:
    void begin(Body& body);

    // Advance one behavior tick. Reads every sensor channel, folds change
    // magnitude and actuator cost into the internal variables, then
    // recomputes drives and the exploration scale.
    void update(Body& body, float dt_s);

    float value(PhysVar v) const { return value_[static_cast<size_t>(v)]; }
    float drive(PhysVar v) const { return drive_[static_cast<size_t>(v)]; }

    // Exploration scale in [0.03, 0.8] — how much noise the action generator
    // should inject. Rises with curiosity/boredom, falls under energy and
    // safety pressure.
    float fuzz_scale() const { return fuzz_; }

    // Mean |Δsensor| across channels on the last tick; the raw "something
    // changed" signal that curiosity, boredom and arousal are built from.
    float sensor_activity() const { return activity_; }

    // Sum of all drives — a single scalar "how much pressure is the organism
    // under right now". Falling total_drive() is "things got better"; used
    // by contingency memory to judge whether an action/sensor pattern
    // historically helped.
    float total_drive() const {
        float sum = 0.0f;
        for (size_t i = 0; i < kPhysVarCount; i++) sum += drive_[i];
        return sum;
    }

    static const char* var_name(PhysVar v);
    static void target_band(PhysVar v, float& lo, float& hi);

private:
    float value_[kPhysVarCount] = {};
    float drive_[kPhysVarCount] = {};
    float prev_sensor_[Body::kMaxSensors] = {};
    bool has_prev_ = false;
    float activity_ = 0.0f;
    float spike_ = 0.0f;
    float fuzz_ = 0.03f;
    int energy_sensor_index_ = -1;
};
