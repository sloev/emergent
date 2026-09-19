#pragma once
// Channel profile for a generic ESP32 DevKit board.
// Treat these as a starting point — override per physical chassis as needed.

#include "board_config.h"

static const ActuatorSpec kActuatorsEsp32Dev[] = {
    // name          kind                          pin  aux  min    max   rate/s  cost/unit
    {"motor_left",  ActuatorKind::kPwmBidirectional, 25, 27, -1.0f, 1.0f, 4.0f,   1.0f},
    {"motor_right", ActuatorKind::kPwmBidirectional, 26, 14, -1.0f, 1.0f, 4.0f,   1.0f},
    {"led_status",  ActuatorKind::kPwmUnipolar,       2,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"led_r",       ActuatorKind::kPwmUnipolar,      16,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"led_g",       ActuatorKind::kPwmUnipolar,      17,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"led_b",       ActuatorKind::kPwmUnipolar,      18,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"head_pan",    ActuatorKind::kServo,            19,  0, -1.0f, 1.0f, 2.0f,   0.2f},
};

static const SensorSpec kSensorsEsp32Dev[] = {
    // name              kind                        pin  update_rate_hz
    {"battery_voltage", SensorKind::kAdcNormalized,   34, 1.0f},
    {"touch_bump",       SensorKind::kDigitalIn,       4, 0.0f},
};

static const BoardConfig kBoardEsp32Dev = {
    .name = "esp32dev",
    .actuators = kActuatorsEsp32Dev,
    .actuator_count = sizeof(kActuatorsEsp32Dev) / sizeof(kActuatorsEsp32Dev[0]),
    .sensors = kSensorsEsp32Dev,
    .sensor_count = sizeof(kSensorsEsp32Dev) / sizeof(kSensorsEsp32Dev[0]),
};
