# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

v0.9.0 charging station integration: a `SensorKind::kWifiRssi` channel now
lets the robot perceive a station's RF beacon as an ordinary long-range
gradient (`docs/synth-behavior.md` §11.3's "RF Lure"), the same way it
already perceives `battery_voltage` up close — no station-specific code
anywhere in the engine, just another named sensor. See
[`src/station`](../station) for reference charging-station firmware (its
own, much simpler PlatformIO project — a state machine, not an organism)
and [`docs/roadmap.md`](../../docs/roadmap.md) for why the actual
approach-and-dock behavior can't be verified without physical hardware and
repeated real trials, which this environment can't run.

v0.8.1 hardening: a response to [GH issue #1](https://github.com/sloev/emergent/issues/1)'s
review of everything through v0.8.0 — LEDC channel exhaustion now fails
loudly instead of silently colliding, contingency-memory age saturates
instead of wrapping every ~55 minutes, decayed entries below a strength
floor are reclaimed instead of occupying a slot forever, contingency
queries tolerate near-misses instead of requiring a bit-exact signature,
curiosity is driven by real prediction error against memory instead of raw
sensor activity, the action generator pulls toward the zero-cost rest state
under fatigue/energy pressure, and a `SafetyMonitor` enforces a battery/
thermal cutoff independent of drives or manual overrides. Pure logic
(channel encode/decode, decay/aging, rate-limit math, LEDC allocation
bounds) is factored into small header-only functions with zero hardware
dependency and covered by host-native tests — see Testing below. Full
details and what was deliberately *not* changed (a couple of the issue's
claims didn't hold up under unsigned-arithmetic scrutiny) are in
[`docs/roadmap.md`](../../docs/roadmap.md).

v0.8.0 life-state persistence: the organism's physiology, contingency
memory, and spatial memory can be downloaded as one JSON file and restored
later — including onto a *different* board (`docs/synth-behavior.md` §13).
Contingency/spatial entries are serialized by channel *name*, not bit
position, specifically so restore works when the new body's channels don't
match the old one's: matching names get re-packed into whatever index they
sit at now, names that don't exist on the new body are simply dropped from
that entry, and an entry that loses every channel it referenced is
discarded rather than kept as noise. See
[`docs/roadmap.md`](../../docs/roadmap.md) for the one accepted ambiguity
in that scheme (spatial memory's level-0 quantization bin isn't
distinguishable from "this channel wasn't in the saved map") and what's
next.

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

Runs the host-native unit tests under `test/` against the pure logic in
`include/{core,body}/*.h` (channel encode/decode/remapping, decay/age
saturation, rate-limit math, LEDC allocation bounds) — no board, no
hardware simulation, just plain functions on plain numbers. This is
deliberately a small, honest subset: the rest of the engine (`Physiology`,
`ContingencyMemory::update()`, `ActionGenerator`, ...) takes a live `Body&`
and calls real `Sensor`/`Actuator` methods that touch hardware
(`analogRead`, `ledcWrite`, ...), so exercising it end-to-end needs either a
real board or a proper hardware mock layer — neither exists yet. When you
add a new piece of genuinely hardware-independent logic to this codebase,
consider whether it belongs in a small header like the ones above instead
of inline in a hardware-coupled class, specifically so it stays testable
this way.
