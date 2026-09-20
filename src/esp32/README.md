# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

v0.4.0 physiology & drives: a behavior tick runs at 20 Hz, folding sensor
change and actuator cost into a small vector of internal variables
(`h_energy`, `h_fatigue`, `h_safety`, `h_arousal`, `h_curiosity`,
`h_boredom`, `h_social`), decaying/recovering them, and deriving drives plus
an exploration scale (`fuzz_scale`) from deviation against per-variable
target bands. Nothing acts on the drives yet — the action generator is not
here until contingency memory and spatial memory exist. Physiology is live
on the dashboard's new Physiology section. See
[`docs/roadmap.md`](../../docs/roadmap.md) for what's next.

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
include/net/wifi_ap.h/.cpp    Brings up the SoftAP; credentials default to the
                               board profile, overridable at runtime (NVS)
include/net/dashboard_server.h/.cpp
                               ESPAsyncWebServer + WebSocket: serves data/,
                               /api/channels, /api/config/wifi
data/                          Dashboard front-end (served from LittleFS)
src/board_config.cpp          Picks the active BoardConfig from the -DBOARD_PROFILE_*
                               macro set by the PlatformIO environment
src/main.cpp                  Entry point: self-test, WiFi/dashboard bring-up,
                               heartbeat + 20 Hz behavior tick
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
  actuator for manual control. Updated over WebSocket (`/ws`) at 5 Hz.
- **Config tab** — change the AP's SSID/password. Saved to NVS and applied
  on restart, no reflash needed.

API, for anything that wants to talk to the board directly instead of using
the bundled UI:
- `GET /api/channels` — JSON snapshot of all actuators/sensors/physiology.
- WebSocket `/ws` — server pushes the same JSON shape at 5 Hz; send
  `{"actuator": "<name>", "value": <float>}` to drive a channel.
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
