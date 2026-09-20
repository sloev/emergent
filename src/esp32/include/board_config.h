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

    // Defaults for the onboard WiFi hotspot. Overridable at runtime from the
    // dashboard's config page (persisted to NVS) without reflashing.
    const char* ap_ssid;
    const char* ap_password;  // "" for an open network; WPA2 needs >= 8 chars

    const ActuatorSpec* actuators;
    size_t actuator_count;

    const SensorSpec* sensors;
    size_t sensor_count;

    // Sensor channel name numerically coupled into physiology's h_energy
    // (docs/synth-behavior.md §10.6) — the one privileged wiring the design
    // allows, expressed as a fact about the chassis, not the engine.
    // nullptr if this body has no such sensor.
    const char* energy_sensor;
};

// Returns the profile selected at build time via -DBOARD_PROFILE_*.
const BoardConfig& active_board();
