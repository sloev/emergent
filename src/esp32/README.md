# Emergent firmware (ESP32)

PlatformIO project for the synthetic-ethology organism described in
[`docs/synth-behavior.md`](../../docs/synth-behavior.md).

## Status

v0.1.0 scaffolding: board-profile abstraction + a build that boots and blinks
the status LED. No sensors/actuators, WiFi, or behavior engine yet — see
[`docs/roadmap.md`](../../docs/roadmap.md) for what's next and in what order.

## Layout

```
platformio.ini          Board environments (one per supported ESP32 variant)
include/board_config.h  BoardConfig struct — the only thing the rest of the
                         firmware knows about a specific chassis's wiring
include/boards/         One header per supported board, each defining a
                         concrete BoardConfig instance
src/board_config.cpp    Picks the active BoardConfig from the -DBOARD_PROFILE_*
                         macro set by the PlatformIO environment
src/main.cpp            Entry point
```

## Adding a board

1. Add a `BoardConfig` instance in `include/boards/board_<name>.h`.
2. Add a case for it in `src/board_config.cpp` (`#if defined(BOARD_PROFILE_<NAME>)`).
3. Add a `[env:<name>]` section in `platformio.ini` with
   `-DBOARD_PROFILE_<NAME>` in `build_flags`.

No other file should need to change — code outside `board_config.*` and
`boards/` talks to named channels (`status_led_pin`, `motor_left_pwm_pin`, ...),
never to a specific board's pin numbers.

## Build

```sh
pio run -e esp32dev                 # or: -e esp32-s3-devkitc-1
pio run -e esp32dev -t upload
pio device monitor
```
