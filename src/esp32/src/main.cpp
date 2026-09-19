// Emergent firmware — v0.3.0: WiFi hotspot & dashboard.
//
// The board now brings up its own AP and serves a live dashboard (LittleFS
// front-end, JSON + WebSocket telemetry, manual actuator control, and a
// config page to change the AP's WiFi credentials without reflashing). The
// behavior engine (physiology, drives, memory) is not here yet — see
// docs/roadmap.md.

#include <Arduino.h>

#include "body/body.h"
#include "board_config.h"
#include "net/dashboard_server.h"
#include "net/wifi_ap.h"

static Body body(active_board());

static void self_test() {
    const BoardConfig& board = active_board();

    Serial.println();
    Serial.print("[emergent] board profile: ");
    Serial.println(board.name);

    Serial.print("[emergent] actuators (");
    Serial.print(body.actuator_count());
    Serial.println("):");
    for (size_t i = 0; i < body.actuator_count(); i++) {
        Actuator& a = body.actuator_at(i);
        Serial.print("  - ");
        Serial.print(a.name());
        Serial.print("  range [");
        Serial.print(a.spec().range_min);
        Serial.print(", ");
        Serial.print(a.spec().range_max);
        Serial.println("]");
    }

    Serial.print("[emergent] sensors (");
    Serial.print(body.sensor_count());
    Serial.println("):");
    for (size_t i = 0; i < body.sensor_count(); i++) {
        Sensor& s = body.sensor_at(i);
        Serial.print("  - ");
        Serial.print(s.name());
        Serial.print(" = ");
        Serial.println(s.read());
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    body.begin();
    self_test();

    wifi_ap::begin(active_board());
    dashboard::begin(body);
}

void loop() {
    static uint32_t last_toggle_ms = 0;
    static bool led_on = false;

    Actuator* led = body.actuator("led_status");
    uint32_t now = millis();

    // Heartbeat: proves a flashed board is alive without a serial monitor.
    if (now - last_toggle_ms >= (led_on ? 150u : 850u)) {
        led_on = !led_on;
        if (led) led->write(led_on ? 1.0f : 0.0f);
        last_toggle_ms = now;
    }

    dashboard::loop_tick(body);
}
