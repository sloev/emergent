# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

v0.3.0 WiFi hotspot & dashboard: the board brings up its own AP and serves a
live dashboard — telemetry over WebSocket, manual actuator control, and a
config page to change WiFi credentials without reflashing. The behavior
engine (physiology, drives, memory) is not here yet — see
[`docs/roadmap.md`](../../docs/roadmap.md) for what's next.

## Layout

```
platformio.ini                Board environments (one per supported ESP32 variant)
include/board_config.h        BoardConfig: a board's WiFi defaults + channel lists
include/boards/                One header per supported board, each defining its
                                channel arrays and a BoardConfig instance
include/body/actuator_spec.h  Actuator channel description (kind, pins, range,
                               rate limit, cost) — the data half of the body layer
include/body/sensor_spec.h    Sensor channel description (kind, pin, sample rate)
include/body/actuator.h/.cpp  Actuator: clamps, rate-limits, drives hardware
include/body/sensor.h/.cpp    Sensor: reads and normalizes, caches per sample rate
include/body/body.h/.cpp      Body: owns the live Actuator/Sensor instances for
                               a board, looked up by name
include/net/wifi_ap.h/.cpp    Brings up the SoftAP; credentials default to the
                               board profile, overridable at runtime (NVS)
include/net/dashboard_server.h/.cpp
                               ESPAsyncWebServer + WebSocket: serves data/,
                               /api/channels, /api/config/wifi
data/                          Dashboard front-end (served from LittleFS)
src/board_config.cpp          Picks the active BoardConfig from the -DBOARD_PROFILE_*
                               macro set by the PlatformIO environment
src/main.cpp                  Entry point: self-test, WiFi/dashboard bring-up, loop
```

Nothing outside `board_config.*` and `boards/` ever sees a pin number or
mentions what a channel is "for" — the behavior engine that lands in later
releases will only ever ask the `Body` for a named channel.

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

- **Dashboard tab** — every sensor's live value, and a slider per actuator
  for manual control. Updated over WebSocket (`/ws`) at 5 Hz.
- **Config tab** — change the AP's SSID/password. Saved to NVS and applied
  on restart, no reflash needed.

API, for anything that wants to talk to the board directly instead of using
the bundled UI:
- `GET /api/channels` — JSON snapshot of all actuators/sensors.
- WebSocket `/ws` — server pushes the same JSON shape at 5 Hz; send
  `{"actuator": "<name>", "value": <float>}` to drive a channel.
- `POST /api/config/wifi` — JSON `{"ssid": "...", "password": "..."}`,
  persists and restarts the board.

## Adding a board

1. Define `ActuatorSpec[]` / `SensorSpec[]` arrays and a `BoardConfig`
   (including `ap_ssid`/`ap_password` defaults) in `include/boards/board_<name>.h`.
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
