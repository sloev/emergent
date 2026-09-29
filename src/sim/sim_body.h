#pragma once
//
// The organism's side of the simulator: numbered channels built from a
// BodyPlan. Inputs arrive normalized to 0..1; outputs are clamped and
// rate-limited exactly as the firmware's Actuator::write() does.

#include <algorithm>
#include <cstddef>
#include <vector>

#include "body/rate_limit.h"
#include "body_plan.h"

namespace sim {

struct SimActuator {
    float min = -1.0f, max = 1.0f, rate = 0.0f, v = 0.0f;
    void write(float target) { v = apply_rate_limit(v, std::clamp(target, min, max), rate, 0.05f); }
    void force(float x) { v = x; }
    float value() const { return v; }
};

struct SimSensor {
    float v = 0.0f;
    void set(float x) { v = std::clamp(x, 0.0f, 1.0f); }
    float last_value() const { return v; }
};

class SimBody {
public:
    static constexpr size_t kMax = 16;  // the organism's channel limit

    explicit SimBody(const BodyPlan& plan) : plan_(plan), sensors_(std::min(plan.sensors.size(), kMax)) {
        for (size_t j = 0; j < plan.actuators.size() && j < kMax; j++)
            actuators_.push_back({plan.actuators[j].min, plan.actuators[j].max, plan.actuators[j].rate, 0.0f});
        values_.assign(actuators_.size(), 0.0f);
    }

    size_t actuator_count() const { return actuators_.size(); }
    size_t sensor_count() const { return sensors_.size(); }
    SimActuator& actuator_at(size_t i) { return actuators_[i]; }
    SimSensor& sensor_at(size_t i) { return sensors_[i]; }

    int energy_index() const {
        for (size_t i = 0; i < plan_.sensors.size(); i++)
            if (plan_.sensors[i].type == Sense::kBattery) return static_cast<int>(i);
        return -1;
    }

    const std::vector<float>& values() {
        for (size_t i = 0; i < actuators_.size(); i++) values_[i] = actuators_[i].v;
        return values_;
    }

private:
    const BodyPlan& plan_;
    std::vector<SimActuator> actuators_;
    std::vector<SimSensor> sensors_;
    std::vector<float> values_;
};

}  // namespace sim
