#pragma once
//
// Board configuration abstraction.
//
// The behavior engine (physiology, drives, memory, action generation) never
// references a pin number directly. It talks to named channels; a BoardConfig
// is what maps those channels onto a specific chassis's wiring. Adding a new
// board means adding a profile under boards/ and a case in board_config.cpp —
// nothing else in the codebase should need to change.

#include <cstdint>

struct BoardConfig {
    const char* name;

    // Status / debug
    uint8_t status_led_pin;

    // Power sensing
    uint8_t battery_voltage_adc_pin;

    // Locomotion (differential drive assumed for now; extend as new bodies
    // are supported)
    uint8_t motor_left_pwm_pin;
    uint8_t motor_right_pwm_pin;
};

// Returns the profile selected at build time via -DBOARD_PROFILE_*.
const BoardConfig& active_board();
