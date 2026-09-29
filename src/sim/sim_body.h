#pragma once
//
// The organism's side of the simulator: a duck-typed body (the same
// accessors the behavior engine uses on the real Body) built from a
// BodyPlan. Inputs arrive already normalized to 0..1 from each part's
// physical range, the way the planned per-channel `range` in body.json
// would do it. The engine sees only these numbers and the channel kinds;
// the battery channel is named as the board's energy_sensor, the one
// wiring fact the design allows.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "board_config.h"
#include "body/rate_limit.h"
#include "body_plan.h"

namespace sim {

class SimActuator {
public:
    SimActuator() = default;
    explicit SimActuator(const ActuatorSpec& spec) : spec_(spec) {}

    // Same clamp + rate limit the real Actuator::write() applies, at the
    // behavior tick's dt.
    float write(float target) {
        float t = std::clamp(target, spec_.range_min, spec_.range_max);
        current_ = apply_rate_limit(current_, t, spec_.max_rate_per_s, kTickS);
        return current_;
    }
    bool manual_override_active(uint32_t, uint32_t = 3000) const { return false; }
    float value() const { return current_; }
    float cost() const { return fabsf(current_) * spec_.cost_per_unit; }
    const char* name() const { return spec_.name; }
    const ActuatorSpec& spec() const { return spec_; }
    void force(float v) { current_ = v; }

    static constexpr float kTickS = 0.05f;

private:
    ActuatorSpec spec_{};
    float current_ = 0.0f;
};

class SimSensor {
public:
    SimSensor() = default;
    explicit SimSensor(const SensorSpec& spec) : spec_(spec) {}
    float read() const { return value_; }
    float last_value() const { return value_; }
    const char* name() const { return spec_.name; }
    const SensorSpec& spec() const { return spec_; }
    void set(float v) { value_ = std::clamp(v, 0.0f, 1.0f); }

private:
    SensorSpec spec_{};
    float value_ = 0.0f;
};

class SimBody {
public:
    static constexpr size_t kMaxActuators = 16;
    static constexpr size_t kMaxSensors = 16;

    explicit SimBody(const BodyPlan& plan) : plan_(plan) {
        n_act_ = plan.actuators.size() < kMaxActuators ? plan.actuators.size() : kMaxActuators;
        n_sen_ = plan.sensors.size() < kMaxSensors ? plan.sensors.size() : kMaxSensors;
        for (size_t i = 0; i < n_act_; i++) {
            const ActuatorDef& d = plan.actuators[i];
            act_specs_[i] = ActuatorSpec{d.name.c_str(), d.kind, 0, 0, d.min, d.max, d.rate, d.cost};
            actuators_[i] = SimActuator(act_specs_[i]);
        }
        for (size_t i = 0; i < n_sen_; i++) {
            const SensorDef& d = plan.sensors[i];
            SensorKind k = d.type == Sense::kBump ? SensorKind::kDigitalIn
                           : d.type == Sense::kRssi ? SensorKind::kWifiRssi
                                                    : SensorKind::kAdcNormalized;
            sen_specs_[i] = SensorSpec{d.name.c_str(), k, 0, 0.0f};
            sensors_[i] = SimSensor(sen_specs_[i]);
        }
        const char* energy = nullptr;
        for (size_t i = 0; i < n_sen_; i++)
            if (plan.sensors[i].type == Sense::kBattery && plan.battery_linked) energy = plan.sensors[i].name.c_str();
        board_ = BoardConfig{"sim", "sim", "sim", act_specs_, n_act_, sen_specs_, n_sen_, energy};
        values_.assign(n_act_, 0.0f);
    }

    const BoardConfig& board() const { return board_; }
    size_t actuator_count() const { return n_act_; }
    size_t sensor_count() const { return n_sen_; }
    SimActuator& actuator_at(size_t i) { return actuators_[i]; }
    SimSensor& sensor_at(size_t i) { return sensors_[i]; }
    const BodyPlan& plan() const { return plan_; }

    int energy_index() const {
        for (size_t i = 0; i < n_sen_; i++)
            if (plan_.sensors[i].type == Sense::kBattery) return static_cast<int>(i);
        return -1;
    }

    const std::vector<float>& values() {
        for (size_t i = 0; i < n_act_; i++) values_[i] = actuators_[i].value();
        return values_;
    }

private:
    const BodyPlan& plan_;
    BoardConfig board_{};
    ActuatorSpec act_specs_[kMaxActuators] = {};
    SensorSpec sen_specs_[kMaxSensors] = {};
    SimActuator actuators_[kMaxActuators];
    SimSensor sensors_[kMaxSensors];
    size_t n_act_ = 0, n_sen_ = 0;
    std::vector<float> values_;
};

}  // namespace sim
