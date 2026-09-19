#pragma once
//
// Board configuration abstraction.
//
// The behavior engine (physiology, drives, memory, action generation) never
// references a pin number directly. It talks to named channels; a BoardConfig
// is what maps those channels onto a specific chassis's wiring, as a flat
// list of actuator/sensor specs. Adding a new board means adding a profile
// under boards/ and a case in board_config.cpp — nothing else in the
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
};

// Returns the profile selected at build time via -DBOARD_PROFILE_*.
const BoardConfig& active_board();
