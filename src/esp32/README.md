# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

| Layer | Fact |
|---|---|
| Implemented | 20 Hz loop: sense → physiology → drives → memory → action → actuate → learn. Named-channel body. SoftAP dashboard. Life-state JSON. Station firmware as a separate state machine. |
| Tested | Host-native tests for bit packing, age saturation, rate-limit clamp, LEDC cap, and organism behavior (energy dynamics, contingency bias) against a fake body. Not real hardware. |
| Hardware tested | Nothing. No board has run this. |
| Designed / planned | Approach-and-dock, ablations, extra boards. |
| Hypothesis | That drives + decaying memory + noise will look alive, including charging as an attractor. Unverified. |

See [`CHANGELOG.md`](../../CHANGELOG.md) for what shipped in each release and
[`docs/roadmap.md`](../../docs/roadmap.md) for open work.

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
include/core/physiology.h     Internal variables + drives + exploration scale.
                               Header-only: no hardware dependency, so it can
                               run against a fake body under native tests.
include/core/contingency_memory.h
                               Bounded, decaying action/sensor-delta memory.
                               Header-only, same reason as physiology.h.
include/core/spatial_memory.h Sensor-signature place cells + best_cell() query.
                               Header-only, same reason as physiology.h.
include/core/action_generator.h/.cpp
                              Proposes and writes each tick's actuator values
                              from physiology + both memories. tick() itself
                              is header-only (same reason as physiology.h);
                              the .cpp only holds begin()'s hardware RNG seed
include/core/state_logger.h/.cpp
                              ~1 Hz slow task: bounded state log on LittleFS
include/core/safety_monitor.h/.cpp
                              Battery/thermal hard cutoff, independent of
                              drives/action generator/manual overrides
include/core/tuning.h         Every tunable rate/gain/threshold (physiology,
                               action generator, memory, safety), one struct
include/core/channel_code.h   Pure channel encode/decode/distance bit-packing,
                               shared by contingency + spatial memory
include/core/decay_math.h     Pure strength-decay/age-saturation/prune math
include/body/rate_limit.h     Pure rate-limit math behind Actuator::write()
include/body/ledc_allocator.h Pure bounded-counter behind LEDC channel allocation
include/net/wifi_ap.h/.cpp    Brings up the SoftAP; credentials default to the
                               board profile, overridable at runtime (NVS)
include/net/dashboard_server.h/.cpp
                               ESPAsyncWebServer + WebSocket: serves data/,
                               /api/channels, /api/contingency, /api/spatial,
                               /api/state, /api/config/wifi
data/                          Dashboard front-end (served from LittleFS)
src/board_config.cpp          Includes whichever board header ACTIVE_BOARD_HEADER
                               names, set by the PlatformIO environment
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
- `kWifiRssi` — signal strength of `target_ssid`, normalized from -90..-30 dBm
  to 0..1 (not found = 0). Needs `WIFI_AP_STA` mode, which `wifi_ap.cpp`
  enables automatically whenever a board profile has one of these. Backed by
  an async `WiFi.scanNetworks()` cycle (a synchronous scan blocks for
  seconds — long enough to stall the 20 Hz tick and the dashboard's web
  server), so this channel's value only actually changes every few seconds
  regardless of how often it's read — a disclosed limitation, not a bug.

## Dashboard

On boot the board starts a SoftAP (SSID/password from the board profile,
overridable — see below) and serves a dashboard at `http://192.168.4.1/`:

- **Dashboard tab** — physiology (value + drive per internal variable,
  overall `fuzz_scale`), every sensor's live value, and a slider per
  actuator for manual control (tagged `M` while your override is active,
  `SAFE` while `SafetyMonitor` has force-cut it — see below). A banner
  appears if the battery is critical. Updated over WebSocket (`/ws`) at 5 Hz.
- **Memory tab** — top contingency-memory entries by strength, decoded into
  which channels moved which way and whether it coincided with falling
  drive pressure, plus a fixed 8x4 grid of remembered "places" (sensor
  signatures) shaded by how promising each looks right now. Polled every 2s.
- **Config tab** — change the AP's SSID/password (saved to NVS, applied on
  restart, no reflash needed), download the state log, and download/
  upload the full life state.

API, for anything that wants to talk to the board directly instead of using
the bundled UI:
- `GET /api/channels` — JSON snapshot of all actuators (value, whether a
  manual override is active, whether `SafetyMonitor` has force-cut it)/
  sensors/physiology, plus a top-level `battery_critical` flag.
- `GET /api/contingency` — top contingency-memory entries (strength, age,
  mean drive delta, decoded channel deltas).
- `GET /api/spatial` — all 32 place-cell slots (occupied or not), each with
  visit count, age, and current drive-weighted score.
- `GET /log.csv` — the state log (physiology + total actuator cost, ~1
  row/sec, capped at 64KB).
- `GET /api/state` — the full life-state envelope (physiology, every
  contingency/spatial entry keyed by channel name, age, a channel-layout
  fingerprint) as a downloadable JSON file.
- `POST /api/state` — restores from a previously downloaded life-state
  file (works across boards — see Status above); responds with how many
  entries carried over and whether the fingerprint matched.
- WebSocket `/ws` — server pushes the same `/api/channels` shape at 5 Hz;
  send `{"actuator": "<name>", "value": <float>}` to manually drive a
  channel (autonomous control backs off for ~3s).
- `POST /api/config/wifi` — JSON `{"ssid": "...", "password": "..."}`,
  persists and restarts the board.

## Adding a board

1. Define `ActuatorSpec[]` / `SensorSpec[]` arrays and a `BoardConfig` named
   `kActiveBoard` (WiFi defaults + optional `energy_sensor` channel name) in
   `include/boards/board_<name>.h`.
2. Add a `[env:<name>]` section in `platformio.ini` with
   `-DACTIVE_BOARD_HEADER='"boards/board_<name>.h"'` in `build_flags`.

`src/board_config.cpp` just includes whatever `ACTIVE_BOARD_HEADER` names —
no per-board case to add there.

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

## Testing

```sh
pio test -e native
```

Runs the host-native unit tests under `test/`: pure bit-packing/decay/
rate-limit/allocation math, plus three organism-behavior scenarios
(`test_behavior_scenarios`) run against `FakeBody`/`FakeActuator`/
`FakeSensor` (`test/test_behavior_scenarios/fake_body.h`) — plain
float-backed stand-ins for `Body`/`Actuator`/`Sensor`, no hardware, no
board. `Physiology::update()`, `ContingencyMemory::update()`, and
`ActionGenerator::tick()` are templated on the body type specifically so
they run unmodified against either the fake or the real thing. The three
scenarios: energy falls under sustained actuator cost, energy rises toward
a high energy-sensor reading, and a repeated (action, sensor-delta,
drive-drop) pattern measurably biases `ActionGenerator`'s output for that
actuator. `Sensor`/`Actuator`'s own hardware paths (`analogRead`,
`ledcWrite`, ...) and the dashboard/WiFi layer still need a real board —
those aren't covered here. When you add a new piece of hardware-independent
logic, consider whether it belongs in a header like the ones above (or is
expressible as a `BodyT`-templated method) instead of inline in a
hardware-coupled class, specifically so it stays testable this way.
