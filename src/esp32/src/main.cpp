// Emergent firmware — v0.7.0: core loop integration.
//
// The full loop from docs/synth-behavior.md §9 is wired end-to-end for the
// first time: sense -> physiology/drives -> memory query -> action ->
// actuate -> learn. ActionGenerator is the piece every earlier release was
// missing — until now, physiology/contingency/spatial memory only observed
// whatever moved the actuators (dashboard sliders, the heartbeat blink).
// They now drive the body themselves, unless a human is actively overriding
// a channel from the dashboard (Actuator::manual_override_active).
//
// Three-tier task split per §14.1:
//   - high-frequency: PWM (hardware LEDC, runs continuously once
//     configured) and ADC (on-demand via Sensor::read(), cached per
//     update_rate_hz) — no separate task needed, hardware + existing
//     on-demand reads already satisfy this.
//   - behavior task: this file's 20 Hz tick.
//   - slow task: StateLogger below, ~1 Hz.
//
// See docs/roadmap.md for the fixed-point-arithmetic note and what's next.

#include <Arduino.h>

#include "body/body.h"
#include "board_config.h"
#include "core/action_generator.h"
#include "core/contingency_memory.h"
#include "core/physiology.h"
#include "core/safety_monitor.h"
#include "core/spatial_memory.h"
#include "core/state_logger.h"
#include "net/dashboard_server.h"
#include "net/wifi_ap.h"

namespace {
constexpr uint32_t kBehaviorTickMs = 50;  // 20 Hz, within the 15-30 Hz the article targets
constexpr uint32_t kSlowTickMs = 1000;    // ~1 Hz
}

static Body body(active_board());
static Physiology phys;
static ContingencyMemory contingency;
static SpatialMemory spatial;
static ActionGenerator action_gen;
static SafetyMonitor safety;
static StateLogger logger;

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
    // WiFi mode has to be set before anything reads a sensor — self_test()
    // below reads every sensor including a possible SensorKind::kWifiRssi,
    // which needs AP_STA (not the plain AP default) to scan at all.
    wifi_ap::begin(active_board());
    self_test();
    phys.begin(body);
    contingency.begin(body);
    spatial.begin(body);
    action_gen.begin(body);
    safety.begin(body);

    dashboard::begin(body, phys, contingency, spatial, safety);  // mounts LittleFS
    logger.begin();
}

void loop() {
    static uint32_t last_toggle_ms = 0;
    static uint32_t last_tick_ms = 0;
    static uint32_t last_slow_ms = 0;
    static bool led_on = false;

    uint32_t now = millis();

    // Heartbeat: proves a flashed board is alive without a serial monitor.
    // Uses write_manual() so the action generator treats led_status as
    // permanently human/system-owned rather than fighting over it — the
    // same generic mechanism dashboard sliders use, not a hardcoded
    // exception for this one channel.
    Actuator* led = body.actuator("led_status");
    if (now - last_toggle_ms >= (led_on ? 150u : 850u)) {
        led_on = !led_on;
        if (led) led->write_manual(led_on ? 1.0f : 0.0f);
        last_toggle_ms = now;
    }

    // Behavior tick: sense -> physiology/drives -> memory update -> act.
    // Physiology's curiosity term consumes contingency's last_surprise()
    // from the *previous* tick, since this tick's contingency.update()
    // (which recomputes it) hasn't run yet — a harmless one-tick lag at
    // 20 Hz, standard for this kind of feedback loop.
    if (now - last_tick_ms >= kBehaviorTickMs) {
        float dt_s = (now - last_tick_ms) / 1000.0f;
        phys.update(body, dt_s, contingency.last_surprise());
        contingency.update(body, phys, dt_s);
        spatial.update(body, phys, dt_s);
        action_gen.tick(body, phys, contingency, spatial);
        // Independent hard floor, enforced last so nothing upstream — drives,
        // memory bias, even a manual dashboard override — gets a vote once a
        // limit is crossed (docs/roadmap.md, GH issue #1).
        safety.enforce(body, dt_s);
        last_tick_ms = now;
    }

    // Slow task: bounded flash log for later offline analysis.
    if (now - last_slow_ms >= kSlowTickMs) {
        logger.tick(body, phys, now);
        last_slow_ms = now;
    }

    dashboard::loop_tick(body, phys, safety);
}
