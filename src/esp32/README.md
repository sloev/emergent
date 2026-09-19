# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

v0.2.0 body layer: generic `Sensor`/`Actuator` classes driven by a per-board,
data-driven IO profile (`ActuatorSpec[]` / `SensorSpec[]`), plus a serial
self-test and a heartbeat/telemetry loop. No WiFi, dashboard, or behavior
engine yet — see [`docs/roadmap.md`](../../docs/roadmap.md) for what's next.

## Layout

```
platformio.ini              Board environments (one per supported ESP32 variant)
include/board_config.h      BoardConfig: a board's actuator/sensor channel lists
include/boards/             One header per supported board, each defining its
                             channel arrays and a BoardConfig instance
include/body/actuator_spec.h  Actuator channel description (kind, pins, range,
                               rate limit, cost) — the data half of the body layer
include/body/sensor_spec.h    Sensor channel description (kind, pin, sample rate)
include/body/actuator.h/.cpp  Actuator: clamps, rate-limits, drives hardware
include/body/sensor.h/.cpp    Sensor: reads and normalizes, caches per sample rate
include/body/body.h/.cpp      Body: owns the live Actuator/Sensor instances for
                               a board, looked up by name
src/board_config.cpp        Picks the active BoardConfig from the -DBOARD_PROFILE_*
                             macro set by the PlatformIO environment
src/main.cpp                Entry point: self-test + heartbeat/telemetry loop
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

## Adding a board

1. Define `ActuatorSpec[]` / `SensorSpec[]` arrays and a `BoardConfig` in
   `include/boards/board_<name>.h`.
2. Add a case for it in `src/board_config.cpp` (`#if defined(BOARD_PROFILE_<NAME>)`).
3. Add a `[env:<name>]` section in `platformio.ini` with
   `-DBOARD_PROFILE_<NAME>` in `build_flags`.

## Adding a channel to an existing board

Add one line to that board's `ActuatorSpec[]`/`SensorSpec[]` array in
`include/boards/board_<name>.h`. No other file changes.

## Build

```sh
pio run -e esp32dev                 # or: -e esp32-s3-devkitc-1
pio run -e esp32dev -t upload
pio device monitor
```
