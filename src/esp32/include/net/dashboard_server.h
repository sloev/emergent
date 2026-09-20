#pragma once

#include "body/body.h"
#include "core/physiology.h"

// Serves the dashboard UI (from LittleFS) plus a JSON/WebSocket API for
// telemetry and manual actuator control. See src/esp32/data/ for the
// front-end and docs/synth-behavior.md §4 for the layer this sits in.
namespace dashboard {

void begin(Body& body, Physiology& phys);

// Call every loop() iteration; internally throttles telemetry broadcast.
void loop_tick(Body& body, Physiology& phys);

}  // namespace dashboard
