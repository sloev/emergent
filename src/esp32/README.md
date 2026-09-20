# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

v0.7.0 core loop integration: the full loop from `docs/synth-behavior.md`
§9 is wired end-to-end for the first time — sense → physiology/drives →
memory query → action → actuate → learn. `ActionGenerator` is the piece
every earlier release was missing: physiology, contingency memory, and
spatial memory now actually drive the actuators each tick
(`baseline + drive-scaled noise + contingency bias + spatial nudge`,
clamped), instead of only observing whatever the dashboard did. A channel
recently touched from the dashboard is left alone for a few seconds rather
than fought over (`Actuator::manual_override_active`) — the heartbeat blink
uses the same mechanism, so it stays system-owned without a hardcoded
exception in the engine. A new `StateLogger` slow task (~1 Hz) appends
physiology + actuator cost to a bounded `/log.csv` on the dashboard's
LittleFS partition for later analysis. See
[`docs/roadmap.md`](../../docs/roadmap.md) for what's next, and for why a
fixed-point arithmetic pass was evaluated and deliberately skipped on this
FPU-equipped hardware.

## Layout

```
platformio.ini                Board environments (one per supported ESP32 variant)
include/board_config.h        BoardConfig: WiFi defaults, channel lists, energy_sensor
include/boards/                One header per supported board, each defining its
                                channel arrays and a BoardConfig instance
include/body/actuator_spec.h  Actuator channel description (kind, pins, range,
                               rate limit, cost) — the data half of the body layer
include/body/sensor_spec.h    Sensor channel description (kind, pin, sample rate)
include/body/actuator.h/.cpp  Actuator: clamps, rate-limits, drives hardware
include/body/sensor.h/.cpp    Sensor: reads and normalizes, caches per sample rate
include/body/body.h/.cpp      Body: owns the live Actuator/Sensor instances for
                               a board, looked up by name
include/core/physiology.h/.cpp
                              Internal variables + drives + exploration scale
                              (docs/synth-behavior.md §6)
include/core/contingency_memory.h/.cpp
                              Bounded, decaying action/sensor-delta memory
                              (docs/synth-behavior.md §7)
include/core/spatial_memory.h/.cpp
                              Sensor-signature place cells + best_cell() query
                              (docs/synth-behavior.md §8)
include/core/action_generator.h/.cpp
                              Proposes and writes each tick's actuator values
                              from physiology + both memories (docs/synth-behavior.md §9)
include/core/state_logger.h/.cpp
                              ~1 Hz slow task: bounded state log on LittleFS
include/net/wifi_ap.h/.cpp    Brings up the SoftAP; credentials default to the
                               board profile, overridable at runtime (NVS)
include/net/dashboard_server.h/.cpp
                               ESPAsyncWebServer + WebSocket: serves data/,
                               /api/channels, /api/contingency, /api/spatial,
                               /api/config/wifi
data/                          Dashboard front-end (served from LittleFS)
src/board_config.cpp          Picks the active BoardConfig from the -DBOARD_PROFILE_*
                               macro set by the PlatformIO environment
src/main.cpp                  Entry point: self-test, WiFi/dashboard bring-up,
                               heartbeat, 20 Hz behavior tick, ~1 Hz slow tick
```

Nothing outside `board_config.*` and `boards/` ever sees a pin number or
mentions what a channel is "for" — the behavior engine only ever asks the
`Body` for a named channel. The one privileged wiring the article's design
allows (battery → `h_energy`) is expressed as `BoardConfig::energy_sensor` —
a channel *name*, so it stays a fact about how the chassis is built rather
than a semantic baked into the engine.

## Channel kinds

Actuators (`ActuatorKind`):
- `kPwmUnipolar` — one pin, duty cycle from `[range_min, range_max]` (0..1) → 0..255. LEDs, fans, vibration motors.
- `kPwmBidirectional` — PWM pin + direction pin, value in `[-1, 1]`. Brushed DC motors via H-bridge.
- `kDigitalOut` — one pin, on/off.
- `kServo` — one pin, 50 Hz pulse; `[range_min, range_max]` maps onto a 1000-2000us pulse width.

Sensors (`SensorKind`):
- `kAdcNormalized` — `analogRead(pin) / 4095.0`.
- `kDigitalIn` — `digitalRead(pin)` as 0.0/1.0.

## Dashboard

On boot the board starts a SoftAP (SSID/password from the board profile,
overridable — see below) and serves a dashboard at `http://192.168.4.1/`:

- **Dashboard tab** — physiology (value + drive per internal variable,
  overall `fuzz_scale`), every sensor's live value, and a slider per
  actuator for manual control (tagged `M` while your override is active).
  Updated over WebSocket (`/ws`) at 5 Hz.
- **Memory tab** — top contingency-memory entries by strength, decoded into
  which channels moved which way and whether it coincided with falling
  drive pressure, plus a fixed 8x4 grid of remembered "places" (sensor
  signatures) shaded by how promising each looks right now. Polled every 2s.
- **Config tab** — change the AP's SSID/password (saved to NVS, applied on
  restart, no reflash needed), and download the state log.

API, for anything that wants to talk to the board directly instead of using
the bundled UI:
- `GET /api/channels` — JSON snapshot of all actuators (value + whether a
  manual override is active)/sensors/physiology.
- `GET /api/contingency` — top contingency-memory entries (strength, age,
  mean drive delta, decoded channel deltas).
- `GET /api/spatial` — all 32 place-cell slots (occupied or not), each with
  visit count, age, and current drive-weighted score.
- `GET /log.csv` — the state log (physiology + total actuator cost, ~1
  row/sec, capped at 64KB).
- WebSocket `/ws` — server pushes the same `/api/channels` shape at 5 Hz;
  send `{"actuator": "<name>", "value": <float>}` to manually drive a
  channel (autonomous control backs off for ~3s).
- `POST /api/config/wifi` — JSON `{"ssid": "...", "password": "..."}`,
  persists and restarts the board.

## Adding a board

1. Define `ActuatorSpec[]` / `SensorSpec[]` arrays and a `BoardConfig`
   (WiFi defaults + optional `energy_sensor` channel name) in
   `include/boards/board_<name>.h`.
2. Add a case for it in `src/board_config.cpp` (`#if defined(BOARD_PROFILE_<NAME>)`).
3. Add a `[env:<name>]` section in `platformio.ini` with
   `-DBOARD_PROFILE_<NAME>` in `build_flags`.

## Adding a channel to an existing board

Add one line to that board's `ActuatorSpec[]`/`SensorSpec[]` array in
`include/boards/board_<name>.h`. No other file changes — it shows up in the
dashboard automatically.

## Build

```sh
pio run -e esp32dev                 # or: -e esp32-s3-devkitc-1
pio run -e esp32dev -t buildfs      # build the dashboard filesystem image
pio run -e esp32dev -t uploadfs     # flash it
pio run -e esp32dev -t upload       # flash the firmware
pio device monitor
```
