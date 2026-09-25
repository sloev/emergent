# Emergent charging station (reference firmware)

Companion firmware for [`docs/synth-behavior.md`](../../docs/synth-behavior.md)
§11 — "Charging Station as an Emergent Attractor". A separate, much simpler
PlatformIO project from [`src/esp32`](../esp32): the station is a state
machine, not an organism. It has no physiology, no drives, no memory, and
no dashboard. Its entire job is:

1. Watch contact voltage/current.
2. Derive `IDLE` / `CHARGING` / `FULL`.
3. Emit a distinct sensory signature per state (RF beacon presence, LED
   waveform, audio click) — and nothing else. No protocol-level "I am a
   charger" message is ever sent; the robot only ever sees ordinary sensor
   values shift, the same as any other channel in this project.

## What this firmware does *not* do

It does not deliver charge current. That's a real battery charger IC or
module — size one for your pack chemistry (a TP4056-based module is a
common, cheap choice for single-cell LiPo) and wire it in parallel with the
sense points this firmware reads. Mixing "deliver current safely" and
"tell a robot how a coarse gradient feels" into one piece of firmware would
make the safety-critical part harder to get right for no benefit; keeping
them separate means the charging module can be swapped for whatever's right
for your battery without touching this code at all.

## Mechanical & circuit build notes

The article's own §11.2 (mechanical/contact geometry) and §11.3 (sensory
lures) are the actual design reference — this section is just how this
firmware's assumptions map onto that.

- **Contact geometry:** self-aligning is doing the real work here, not
  precise control — a funnel entrance and a shallow ramp with a soft stop
  should bring a robot approaching at a shallow, low-speed angle into valid
  contact without the robot needing to hit an exact pose. Spring-loaded pogo
  pins (station side) onto wide flat pads (robot side) tolerate misalignment
  far better than a rigid connector.
- **Voltage sense (`kVoltageSensePin`, default GPIO 34):** a simple resistor
  divider from the contact/pack voltage down into the ESP32 ADC's 0-3.3V
  range. Size it for your pack's full-charge voltage.
- **Current sense (`kCurrentSensePin`, default GPIO 35):** the analog output
  of a current-sense IC (e.g. ACS712, INA169) in series with the charge
  path, scaled into 0-3.3V. If you don't have current sensing, you can
  approximate "charging active" from voltage alone (rising vs. flat), but
  the `IDLE`/`CHARGING`/`FULL` split in this firmware is written assuming
  real current sensing exists — expect to adjust `update_led`/`loop()` if
  you skip it.
- **LED (`kLedPin`, default GPIO 2):** any LED bright enough to see across a
  room; the waveform (slow pulse / fast pulse / solid) carries the meaning,
  not the color.
- **Buzzer (`kBuzzerPin`, default GPIO 25):** a passive piezo disc driven by
  a plain digital pulse (see `click()`) — deliberately not `tone()`, which
  isn't reliably available across ESP32 Arduino core versions.
- **RF beacon:** the station's own SoftAP (`kApSsid`, default
  `"emergent-station"`). This is a long-range gradient, not a state signal —
  state lives entirely in the LED/audio pattern per §11.5, so the SSID
  never changes. See below for wiring this into the robot.

## Tuning thresholds

`kCurrentActiveThreshold`, `kVoltageFullThreshold`, and
`kCurrentTaperThreshold` in `src/main.cpp` are normalized-ADC placeholders,
not calibrated values — tune them against your actual pack's real
voltage/current curve. Get a multimeter reading at "definitely charging"
and "definitely full" for your specific battery before trusting the
defaults.

## Connecting a robot to this station

The robot doesn't need any station-specific code — it just needs a sensor
channel that perceives the beacon:

1. On the robot, add a `SensorKind::kWifiRssi` channel to a board profile
   (see [`src/esp32/include/board_config.h`](../esp32/include/board_config.h))
   with `target_ssid` set to this firmware's `kApSsid`.
2. That's it. Everything past that point — learning that the beacon's
   signal strength correlates with rising `battery_voltage`, and eventually
   biasing exploration toward it — is contingency memory and spatial memory
   doing their ordinary job on an ordinary-looking sensor channel, exactly
   as docs/synth-behavior.md §11.7 describes.

## Build

```sh
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor
```
