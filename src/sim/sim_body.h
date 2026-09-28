#pragma once
//
// The organism's side of the simulator: a duck-typed body (same accessors
// the behavior engine uses on the real Body) whose channels are the example
// body from docs/example-body.md — 7 inputs, 5 outputs.
//
// Each input is normalized to 0..1 from its physical range, the way the
// planned per-channel `range` in body.json would do it. The engine sees only
// these numbers and the channel kinds; it is never told which one is a
// wheel, a light sensor, or the battery — except that the battery channel is
// named as the board's energy_sensor, the one wiring fact the design allows.

#include <cmath>
#include <cstddef>

#include "board_config.h"
#include "body/rate_limit.h"
#include "core/math_util.h"
#include "world.h"

namespace sim {

class SimActuator {
public:
    SimActuator() = default;
    explicit SimActuator(const ActuatorSpec& spec) : spec_(spec) {}

    // Same clamp + rate limit the real Actuator::write() applies, at the
    // behavior tick's dt.
    float write(float target) {
        float t = clampf(target, spec_.range_min, spec_.range_max);
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
    void set(float v) { value_ = clampf(v, 0.0f, 1.0f); }

private:
    SensorSpec spec_{};
    float value_ = 0.0f;
};

// Channel order matters only for bit packing inside the memories; it
// carries no meaning.
enum SensorIndex { kBattery, kMic, kLightL, kLightR, kBump, kIr, kRssi, kSensorCount };
enum ActuatorIndex { kWheelL, kWheelR, kHead, kLed, kBuzzer, kActuatorCount };

inline const ActuatorSpec kSimActuators[] = {
    // name           kind                            pin aux  min    max  rate/s cost
    {"stepper_left",  ActuatorKind::kPwmBidirectional, 25, 26, -1.0f, 1.0f, 2.0f, 1.0f},
    {"stepper_right", ActuatorKind::kPwmBidirectional, 27, 14, -1.0f, 1.0f, 2.0f, 1.0f},
    {"head_pan",      ActuatorKind::kServo,            18,  0, -1.0f, 1.0f, 2.0f, 0.2f},
    {"led",           ActuatorKind::kPwmUnipolar,      19,  0,  0.0f, 1.0f, 0.0f, 0.1f},
    {"buzzer",        ActuatorKind::kPwmUnipolar,      23,  0,  0.0f, 1.0f, 0.0f, 0.1f},
};

inline const SensorSpec kSimSensors[] = {
    {"battery_voltage", SensorKind::kAdcNormalized, 34, 0.0f},
    {"mic",             SensorKind::kAdcNormalized, 35, 0.0f},
    {"light_left",      SensorKind::kAdcNormalized, 32, 0.0f},
    {"light_right",     SensorKind::kAdcNormalized, 33, 0.0f},
    {"bump",            SensorKind::kDigitalIn,      4, 0.0f},
    {"ir_distance",     SensorKind::kAdcNormalized, 36, 0.0f},
    {"rf_rssi_station", SensorKind::kWifiRssi,       0, 0.2f, "emergent-station"},
};

class SimBody {
public:
    static constexpr size_t kMaxActuators = 16;
    static constexpr size_t kMaxSensors = 16;

    explicit SimBody(bool battery_linked) {
        board_ = BoardConfig{
            "sim", "sim", "sim", kSimActuators, kActuatorCount, kSimSensors, kSensorCount,
            battery_linked ? "battery_voltage" : nullptr,
        };
        for (size_t i = 0; i < kActuatorCount; i++) actuators_[i] = SimActuator(kSimActuators[i]);
        for (size_t i = 0; i < kSensorCount; i++) sensors_[i] = SimSensor(kSimSensors[i]);
    }

    const BoardConfig& board() const { return board_; }
    size_t actuator_count() const { return kActuatorCount; }
    size_t sensor_count() const { return kSensorCount; }
    SimActuator& actuator_at(size_t i) { return actuators_[i]; }
    SimSensor& sensor_at(size_t i) { return sensors_[i]; }

    // Physical readings -> normalized channel values.
    void sense(const Readings& r) {
        sensors_[kBattery].set((r.battery_v - 9.0f) / (12.6f - 9.0f));
        sensors_[kMic].set(r.mic_level);
        // LDR in a divider: compressive, roughly L / (L + L_half).
        sensors_[kLightL].set(r.light_left / (r.light_left + 0.5f));
        sensors_[kLightR].set(r.light_right / (r.light_right + 0.5f));
        sensors_[kBump].set(r.bump ? 1.0f : 0.0f);
        // Sharp GP2Y0A21: usable 10..80 cm, output roughly proportional to 1/d.
        float d = r.ir_distance_m < 0.1f ? 0.1f : (r.ir_distance_m > 0.8f ? 0.8f : r.ir_distance_m);
        sensors_[kIr].set((1.0f / d - 1.25f) / (10.0f - 1.25f));
        sensors_[kRssi].set((r.rssi_dbm + 90.0f) / 60.0f);
    }

    Commands commands() const {
        Commands c;
        c.wheel_left = actuators_[kWheelL].value();
        c.wheel_right = actuators_[kWheelR].value();
        c.head_pan = actuators_[kHead].value();
        c.led = actuators_[kLed].value();
        c.buzzer = actuators_[kBuzzer].value();
        return c;
    }

private:
    BoardConfig board_{};
    SimActuator actuators_[kMaxActuators];
    SimSensor sensors_[kMaxSensors];
};

}  // namespace sim
