// Emergent firmware — v0.5.0: contingency memory.
//
// The behavior tick now also runs ContingencyMemory: a bounded, decaying
// table of "actuators moved like X, sensors moved like Y, and drive pressure
// changed by Z" records (docs/synth-behavior.md §7). It learns from whatever
// moves the actuators today — dashboard sliders, the heartbeat blink — since
// there is still no autonomous action generator; that's v0.7.0, once spatial
// memory (v0.6.0) also exists to help bias it. The memory's query API is
// exposed and testable via the dashboard, just not consumed by anything yet.
// See docs/roadmap.md for what's next.

#include <Arduino.h>

#include "body/body.h"
#include "board_config.h"
#include "core/contingency_memory.h"
#include "core/physiology.h"
#include "net/dashboard_server.h"
#include "net/wifi_ap.h"

namespace {
constexpr uint32_t kBehaviorTickMs = 50;  // 20 Hz, within the 15-30 Hz the article targets
}

static Body body(active_board());
static Physiology phys;
static ContingencyMemory contingency;

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
    contingency.begin(body);

    wifi_ap::begin(active_board());
    dashboard::begin(body, phys, contingency);
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

    // Behavior tick: physiology/drives and contingency memory update at a
    // fixed rate. Contingency memory reads actuator/sensor state *after*
    // physiology this tick, so both see the same snapshot.
    if (now - last_tick_ms >= kBehaviorTickMs) {
        float dt_s = (now - last_tick_ms) / 1000.0f;
        phys.update(body, dt_s);
        contingency.update(body, phys, dt_s);
        last_tick_ms = now;
    }

    dashboard::loop_tick(body, phys);
}
