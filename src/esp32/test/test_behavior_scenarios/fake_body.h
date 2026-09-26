#pragma once
//
// A plain, hardware-free stand-in for Body/Actuator/Sensor, used only under
// -e native. The real Body/Actuator/Sensor .cpp files #include <Arduino.h>
// and call analogRead/digitalRead/ledcWrite/millis/WiFi.* directly, so they
// can't compile here at all — this duck-types just the methods Physiology,
// ContingencyMemory, and ActionGenerator actually call on a body (see their
// template parameters), backed by plain floats instead of hardware.

#include <cstddef>

#include "board_config.h"
#include "core/math_util.h"

class FakeActuator {
public:
    FakeActuator() = default;
    explicit FakeActuator(const ActuatorSpec& spec) : spec_(spec) {}

    float write(float value) {
        current_ = clampf(value, spec_.range_min, spec_.range_max);
        return current_;
    }

    // No manual-override/rate-limit modeling here — that math is already
    // covered by test_rate_limit's pure tests; behavior scenarios only need
    // an actuator that holds a value and reports a cost.
    bool manual_override_active(uint32_t, uint32_t = 3000) const { return false; }

    float value() const { return current_; }
    float cost() const { return (current_ < 0 ? -current_ : current_) * spec_.cost_per_unit; }
    const char* name() const { return spec_.name; }
    const ActuatorSpec& spec() const { return spec_; }

    // Test seam: set the channel directly, bypassing write()'s clamp, to
    // set up a specific "before" state.
    void set_value(float v) { current_ = v; }

private:
    ActuatorSpec spec_{};
    float current_ = 0.0f;
};

class FakeSensor {
public:
    FakeSensor() = default;
    explicit FakeSensor(const SensorSpec& spec) : spec_(spec) {}

    float read() const { return value_; }
    float last_value() const { return value_; }
    const char* name() const { return spec_.name; }
    const SensorSpec& spec() const { return spec_; }

    // Test seam: set the reading directly — no caching/hardware to model.
    void set_value(float v) { value_ = v; }

private:
    SensorSpec spec_{};
    float value_ = 0.0f;
};

class FakeBody {
public:
    static constexpr size_t kMaxActuators = 16;
    static constexpr size_t kMaxSensors = 16;

    explicit FakeBody(const BoardConfig& board) : board_(board) {}

    void begin() {
        actuator_count_ = board_.actuator_count < kMaxActuators ? board_.actuator_count : kMaxActuators;
        for (size_t i = 0; i < actuator_count_; i++) actuators_[i] = FakeActuator(board_.actuators[i]);

        sensor_count_ = board_.sensor_count < kMaxSensors ? board_.sensor_count : kMaxSensors;
        for (size_t i = 0; i < sensor_count_; i++) sensors_[i] = FakeSensor(board_.sensors[i]);
    }

    const BoardConfig& board() const { return board_; }
    size_t actuator_count() const { return actuator_count_; }
    size_t sensor_count() const { return sensor_count_; }
    FakeActuator& actuator_at(size_t i) { return actuators_[i]; }
    FakeSensor& sensor_at(size_t i) { return sensors_[i]; }

private:
    const BoardConfig& board_;
    FakeActuator actuators_[kMaxActuators];
    FakeSensor sensors_[kMaxSensors];
    size_t actuator_count_ = 0;
    size_t sensor_count_ = 0;
};
