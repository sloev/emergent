# Changelog

One line per closed release. See [`docs/roadmap.md`](docs/roadmap.md) for open work
and [GitHub Releases](https://github.com/sloev/emergent/releases) for tagged versions.

- **v0.9.0** — Charging station reference firmware (`src/station`); `SensorKind::kWifiRssi` lets the robot perceive a station's beacon as a long-range RF gradient.
- **v0.8.1** — Hardening pass responding to [GH issue #1](https://github.com/sloev/emergent/issues/1): LEDC exhaustion, contingency/spatial age wraparound, exact-match-only recall, safety envelope (`SafetyMonitor`), real prediction-error curiosity, rest-pull baseline, plus 27 native unit tests for the pure logic behind these fixes.
- **v0.8.0** — Life-state persistence: physiology/contingency/spatial memory serialize to JSON by channel name, downloadable/restorable across boards.
- **v0.7.0** — Core loop integration: `ActionGenerator` wires physiology, contingency memory, and spatial memory into actual actuator output for the first time.
- **v0.6.0** — Spatial memory: sensor-signature place cells and `best_cell()`, since this hardware has no positioning sensor.
- **v0.5.0** — Contingency memory: bounded, decaying action/sensor-delta records with drive-outcome tracking.
- **v0.4.0** — Physiology and drives: internal variable vector, decay/recovery/coupling rules, exploration-scale modulation.
- **v0.3.0** — WiFi hotspot and live dashboard (SoftAP, WebSocket telemetry, manual actuator control).
- **v0.2.0** — Configurable body layer: `Actuator`/`Sensor` abstractions, board profiles for two boards.
- **v0.1.0** — Project scaffolding: PlatformIO skeleton, docs site, CI.
