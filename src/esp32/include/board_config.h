#pragma once
//
// Board configuration abstraction.
//
// The organism never sees a pin: it gets numbered channels. A BoardConfig
// is what maps those channels onto a specific chassis's wiring, as a flat
// list of actuator/sensor specs. Adding a new board means adding a profile
// under boards/ and a matching PlatformIO environment — nothing else in the
// codebase should need to change.

#include <cstddef>

#include "body/actuator_spec.h"
#include "body/sensor_spec.h"

struct BoardConfig {
    const char* name;

    const ActuatorSpec* actuators;
    size_t actuator_count;

    const SensorSpec* sensors;
    size_t sensor_count;

    // The battery: the one input the organism is told is its energy.
    // nullptr if this body has no such sensor.
    const char* energy_sensor;
};

// Returns the profile selected at build time via -DBOARD_PROFILE_*.
const BoardConfig& active_board();
