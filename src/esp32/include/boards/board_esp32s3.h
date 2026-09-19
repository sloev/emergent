#pragma once
// Pin profile for an ESP32-S3 DevKitC-1 board.
// Treat these as a starting point — override per physical chassis as needed.

#include "board_config.h"

static const BoardConfig kBoardEsp32S3 = {
    .name = "esp32-s3-devkitc-1",
    .status_led_pin = 48,
    .battery_voltage_adc_pin = 4,
    .motor_left_pwm_pin = 9,
    .motor_right_pwm_pin = 10,
};
