// Emergent firmware — v0.4.0: physiology & drives.
//
// A behavior tick now runs alongside the heartbeat: Physiology folds sensor
// change and actuator cost into a small vector of internal variables
// (docs/synth-behavior.md §6), decays/recovers them, and derives drives plus
// an exploration scale. Nothing acts on the drives yet — there is no action
// generator until contingency memory (v0.5.0) and spatial memory (v0.6.0)
// exist to bias it — but physiology/drives are live and visible on the
// dashboard. See docs/roadmap.md for what's next.

#include <Arduino.h>

#include "body/body.h"
#include "board_config.h"
#include "core/physiology.h"
#include "net/dashboard_server.h"
#include "net/wifi_ap.h"

namespace {
constexpr uint32_t kBehaviorTickMs = 50;  // 20 Hz, within the 15-30 Hz the article targets
}

static Body body(active_board());
static Physiology phys;

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
    phys.begin(body);

    wifi_ap::begin(active_board());
    dashboard::begin(body, phys);
}

void loop() {
    static uint32_t last_toggle_ms = 0;
    static uint32_t last_tick_ms = 0;
    static bool led_on = false;

    uint32_t now = millis();

    // Heartbeat: proves a flashed board is alive without a serial monitor.
    Actuator* led = body.actuator("led_status");
    if (now - last_toggle_ms >= (led_on ? 150u : 850u)) {
        led_on = !led_on;
        if (led) led->write(led_on ? 1.0f : 0.0f);
        last_toggle_ms = now;
    }

    // Behavior tick: physiology/drives update at a fixed rate.
    if (now - last_tick_ms >= kBehaviorTickMs) {
        float dt_s = (now - last_tick_ms) / 1000.0f;
        phys.update(body, dt_s);
        last_tick_ms = now;
    }

    dashboard::loop_tick(body, phys);
}
