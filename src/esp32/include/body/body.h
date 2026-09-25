#pragma once

#include <cstddef>

#include "body/actuator.h"
#include "body/sensor.h"
#include "board_config.h"

// Owns the live Actuator/Sensor instances for a board. This is the only
// place that knows how many channels exist; everything above it (self-test,
// dashboard, later the behavior engine) talks to channels by name.
class Body {
public:
    static constexpr size_t kMaxActuators = 16;
    static constexpr size_t kMaxSensors = 16;

    explicit Body(const BoardConfig& board) : board_(board) {}

    void begin();

    const BoardConfig& board() const { return board_; }

    size_t actuator_count() const { return actuator_count_; }
    size_t sensor_count() const { return sensor_count_; }

    // Bounds-checked against the physical array size (kMaxActuators/
    // kMaxSensors), not just actuator_count()/sensor_count() — callers are
    // expected to respect the count, but an out-of-range index here would
    // otherwise be undefined behavior rather than a loud, recoverable error.
    Actuator& actuator_at(size_t i);
    Sensor& sensor_at(size_t i);

    // Linear search by name; fine at these sizes. Returns nullptr if absent.
    Actuator* actuator(const char* name);
    Sensor* sensor(const char* name);

private:
    const BoardConfig& board_;
    Actuator actuators_[kMaxActuators];
    Sensor sensors_[kMaxSensors];
    size_t actuator_count_ = 0;
    size_t sensor_count_ = 0;
};
