// Emergent firmware: the body's channels in, the organism, the body's
// channels out, at 20 Hz. Everything the robot does comes from
// ../../organism.h; this file only wires it to pins.

#include <Arduino.h>
#include <WiFi.h>

#include <cstring>

#include "../../organism.h"
#include "body/body.h"
#include "board_config.h"

namespace {
constexpr uint32_t kTickMs = 50;
constexpr float kBatteryFloor = 0.05f;  // below this energy reading every output is held at 0

Body body(active_board());
life::Organism org;
float in[life::Organism::kMaxIn];
float out[life::Organism::kMaxOut];
int energy = -1;
}  // namespace

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);  // RSSI sensors scan for the station's beacon
    body.begin();

    const BoardConfig& b = active_board();
    uint32_t pain = 0;
    for (size_t i = 0; i < body.sensor_count(); i++) {
        const SensorSpec& s = body.sensor_at(i).spec();
        if (s.hurts) pain |= 1u << i;
        if (b.energy_sensor && strcmp(s.name, b.energy_sensor) == 0) energy = static_cast<int>(i);
    }
    bool bipolar[life::Organism::kMaxOut] = {};
    for (size_t j = 0; j < body.actuator_count(); j++) bipolar[j] = body.actuator_at(j).spec().range_min < 0.0f;

    life::Genome g;
    g.seed = esp_random() | 1u;  // a new individual each boot until genomes are flashed
    org.begin(g, body.sensor_count(), body.actuator_count(), energy, pain, bipolar);
    Serial.printf("[emergent] %s: %u inputs, %u outputs\n", b.name, static_cast<unsigned>(body.sensor_count()),
                  static_cast<unsigned>(body.actuator_count()));
}

void loop() {
    static uint32_t last_ms = millis(), last_log_ms = 0;
    uint32_t now = millis();
    if (now - last_ms < kTickMs) return;
    float dt = (now - last_ms) / 1000.0f;
    last_ms = now;

    for (size_t i = 0; i < body.sensor_count(); i++) in[i] = body.sensor_at(i).read();
    org.tick(in, out, dt);
    bool flat = energy >= 0 && in[energy] < kBatteryFloor;
    for (size_t j = 0; j < body.actuator_count(); j++) body.actuator_at(j).write(flat ? 0.0f : out[j]);

    if (now - last_log_ms >= 1000) {
        last_log_ms = now;
        Serial.printf("hunger %.2f full %.2f pain %.2f bored %.2f dopa %+.2f place %d/%u\n", org.hunger(), org.fullness(),
                      org.pain(), org.boredom(), org.dopamine(), org.place(), static_cast<unsigned>(org.place_count()));
    }
}
