# Roadmap

Rolling versioning: `0.MINOR.PATCH`, incremented as work lands. A major version bump
(`1.0.0`, `2.0.0`, ...) is a deliberate decision made by a human — never automated.

We work **one release at a time**. Don't start tasks from a later release until the
current one is closed out, unless explicitly reprioritized.

## Status

**Current release: v0.1.0** (complete — next up: v0.2.0)

---

## v0.1.0 — Scaffolding

- [x] Git repo initialized
- [x] `docs/` site skeleton (`index.html` frontpage, `synth-behavior.md`, this roadmap)
- [x] GitHub Actions workflow to rebuild `synth-behavior.pdf` from the markdown source
- [x] ESP32 PlatformIO project skeleton (`src/esp32`)
- [x] Board configuration abstraction (per-board pin/IO profiles)
- [x] Push to GitHub, enable Pages (serve from `docs/` on `main`)
- [x] CI: PlatformIO build check for at least one board target

## v0.2.0 — Body layer: sensors & actuators

- [ ] Generic `Actuator` interface (range, rate limit, cost estimate, safety envelope)
- [ ] Generic `Sensor` interface (normalized numeric channel, update rate)
- [ ] IO profile format mapping named channels to physical pins/drivers
- [ ] Drivers for an initial hardware set: motors (PWM/H-bridge), servo, RGB LED,
      ADC-based sensors (battery voltage/current), digital touch/bump
- [ ] Board profiles for at least two boards (e.g. ESP32 DevKit, ESP32-S3)
- [ ] Sensor/actuator self-test routine over serial

## v0.3.0 — WiFi hotspot & dashboard

- [ ] SoftAP mode, SSID/password configurable per board profile
- [ ] Async web server serving the dashboard UI
- [ ] Live telemetry push (WebSocket or SSE): raw sensor values, actuator outputs
- [ ] Minimal dashboard front-end served from firmware (LittleFS)
- [ ] Config page: adjust IO mapping / drive targets without reflashing

## v0.4.0 — Physiology & drives

- [ ] Internal variable vector (`h_energy`, `h_fatigue`, `h_safety`, `h_arousal`,
      `h_curiosity`, `h_boredom`, `h_social`)
- [ ] Update rules: decay/recovery, sensor coupling, actuator-cost coupling
- [ ] Drive computation from deviation against target bands
- [ ] Fuzz-scale / exploration modulation from drives
- [ ] Dashboard: live physiology & drive readout

## v0.5.0 — Contingency memory

- [ ] Fixed-size action-delta / sensor-delta record structure
- [ ] Quantization & hashing of deltas
- [ ] Learn/decay/prune update algorithm bounded to `MAX_ENTRIES`
- [ ] Contingency-biased action query
- [ ] Dashboard: inspect top contingency entries

## v0.6.0 — Spatial memory & navigation

- [ ] Coarse grid / place-cell representation
- [ ] Visit stats + drive-improvement profile per cell
- [ ] Spatial bias contribution to action generation
- [ ] Dashboard: simple 2D map view of visited cells

## v0.7.0 — Core loop integration

- [ ] Full tick loop wired end-to-end (sense → physiology → drives → memory query →
      action → actuate → learn)
- [ ] Task split: high-frequency (PWM/ADC/audio), behavior task (15–30 Hz),
      slow tasks (~1 Hz logging/checkpoints)
- [ ] Fixed-point/integer arithmetic pass for the hot loop
- [ ] On-device logging to flash for later analysis

## v0.8.0 — Life-state persistence

- [ ] Organism state serialization (physiology, drives, contingencies, spatial memory, stats)
- [ ] Download/upload state via the dashboard
- [ ] Channel-fingerprint + remapping layer for transferring state across bodies

## v0.9.0 — Charging station integration

- [ ] Reference charging station firmware/circuit notes (RF beacon, LED pattern,
      audio cue, power module states: IDLE/CHARGING/FULL)
- [ ] Robot-side: `battery_voltage`/`current` as ordinary sensor channels feeding `h_energy`
- [ ] End-to-end test: emergent approach-and-dock behavior over repeated sessions

## v1.0.0 — First stable "alive" release (human-gated)

- [ ] All of the above validated on real hardware
- [ ] Experimental program from the paper (§15) run at least once: homeostasis quality,
      exploration structure, history dependence, perceived aliveness
- [ ] Ablation switches implemented (disable stochasticity, freeze contingency memory,
      clear spatial memory, flatten drives, decouple battery from `h_energy`)
- [ ] Human sign-off to cut 1.0.0

## Backlog / later

- Multi-agent / multi-robot stigmergic experiments (scent, RF, light tags)
- Mechanical voice / prosody experiments
- Additional board support as hardware becomes available
- Formal comparison against HRL baselines
