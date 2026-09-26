#pragma once
// Channel profile for an ESP32-S3 DevKitC-1 board.
// Treat these as a starting point — override per physical chassis as needed.

#include "board_config.h"

static const ActuatorSpec kActuatorsEsp32S3[] = {
    // name          kind                          pin  aux  min    max   rate/s  cost/unit
    {"motor_left",  ActuatorKind::kPwmBidirectional,  9, 11, -1.0f, 1.0f, 4.0f,   1.0f},
    {"motor_right", ActuatorKind::kPwmBidirectional, 10, 12, -1.0f, 1.0f, 4.0f,   1.0f},
    {"led_status",  ActuatorKind::kPwmUnipolar,      48,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"led_r",       ActuatorKind::kPwmUnipolar,      15,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"led_g",       ActuatorKind::kPwmUnipolar,      16,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"led_b",       ActuatorKind::kPwmUnipolar,      17,  0,  0.0f, 1.0f, 0.0f,   0.1f},
    {"head_pan",    ActuatorKind::kServo,            18,  0, -1.0f, 1.0f, 2.0f,   0.2f},
};

static const SensorSpec kSensorsEsp32S3[] = {
    // name              kind                        pin  update_rate_hz
    {"battery_voltage", SensorKind::kAdcNormalized,    4, 1.0f},
    {"touch_bump",       SensorKind::kDigitalIn,        5, 0.0f},
    // rf_rssi_station: signal strength of a charging station's beacon
    // (docs/synth-behavior.md §11.3, src/station/). update_rate_hz here
    // means "no more than one scan per 5s" — see body/sensor.cpp.
    {"rf_rssi_station", SensorKind::kWifiRssi, 0, 0.2f, "emergent-station"},
};

static const BoardConfig kActiveBoard = {
    .name = "esp32-s3-devkitc-1",
    .ap_ssid = "emergent-esp32s3",
    .ap_password = "emergent1",
    .actuators = kActuatorsEsp32S3,
    .actuator_count = sizeof(kActuatorsEsp32S3) / sizeof(kActuatorsEsp32S3[0]),
    .sensors = kSensorsEsp32S3,
    .sensor_count = sizeof(kSensorsEsp32S3) / sizeof(kSensorsEsp32S3[0]),
    .energy_sensor = "battery_voltage",
};
