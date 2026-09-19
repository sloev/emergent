#pragma once
// Pin profile for a generic ESP32 DevKit board.
// Treat these as a starting point — override per physical chassis as needed.

#include "board_config.h"

static const BoardConfig kBoardEsp32Dev = {
    .name = "esp32dev",
    .status_led_pin = 2,
    .battery_voltage_adc_pin = 34,
    .motor_left_pwm_pin = 25,
    .motor_right_pwm_pin = 26,
};
