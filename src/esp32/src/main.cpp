// Emergent firmware — v0.1.0 scaffolding.
//
// At this stage the goal is just: prove the board-profile abstraction works
// and the project builds/flashes across board targets. The body layer
// (sensors/actuators), WiFi hotspot + dashboard, and the behavior engine
// (physiology, drives, contingency memory, spatial memory) land in later
// releases — see docs/roadmap.md.

#include <Arduino.h>
#include "board_config.h"

static const BoardConfig* board = nullptr;

void setup() {
    Serial.begin(115200);
    delay(200);

    board = &active_board();

    pinMode(board->status_led_pin, OUTPUT);

    Serial.println();
    Serial.print("[emergent] board profile: ");
    Serial.println(board->name);
}

void loop() {
    // Heartbeat blink so a flashed board visibly proves it's alive without a
    // serial monitor attached.
    digitalWrite(board->status_led_pin, HIGH);
    delay(150);
    digitalWrite(board->status_led_pin, LOW);
    delay(850);
}
