# Example Body: 7 Inputs, 5 Outputs

[← Back to project overview](index.html)

A complete, buildable wiring for one ESP32 DevKit: a two-wheeled stepper robot with
a panning "head", light eyes, ears, a bumper, a distance sensor, an RF sense of the
charging station, and a battery it can feel. The [simulator](../src/sim/README.md)
uses exactly this body, so what it predicts is what this build should do.

The organism is only ever told each channel's **kind, pin and range**. It is never
told that two channels are wheels, that the light sensors point left and right, or
that the microphone hears its own motors. The one wiring fact it gets is which input
is its battery, because that is what it lives on (`energy_sensor`).

## Parts

| Qty | Part | Role | Approx. |
|---|---|---|---|
| 1 | ESP32-WROOM-32 DevKit v1 (38-pin) | brain | €6 |
| 2 | NEMA17 stepper, ~1.5 A rated (e.g. 17HS4401) | wheels | €2 × 9 |
| 2 | Stepper driver: A4988 (cheap, loud) or TMC2209 (quiet) | wheel drivers | €2–5 each |
| 2 | 65 mm wheel with 5 mm hub + a ball caster | chassis | €6 |
| 1 | SG90 micro servo | head pan | €2 |
| 2 | GL5528 LDR + 10 kΩ | light eyes (on the head, angled ±35°) | €1 |
| 1 | MAX4466 electret mic amp module | ears | €3 |
| 1 | Sharp GP2Y0A21YK0F IR distance (10–80 cm) | distance ahead | €6 |
| 1 | Microswitch with lever + bumper plate | touch | €1 |
| 1 | 5 mm high-brightness LED + 220 Ω | light it can make | €0.2 |
| 1 | Passive piezo disc | sound it can make | €0.5 |
| 1 | 3S LiPo 1000–2200 mAh + XT60 + 3S BMS | body energy | €20 |
| 1 | 5 V 3 A buck converter (e.g. MP1584 / LM2596) | logic + servo power | €2 |
| 1 | 100 kΩ + 22 kΩ resistors, 100 nF | battery sense divider | — |
| 2 | Wide copper pads (robot) / pogo pins (station) | charge contacts | €3 |

The quieter TMC2209 drivers change what the mic hears of the robot's own motion.
That's part of the body, not a detail: with A4988s, "I moved" is loud in the mic
channel, and the organism will learn from that.

## Wiring

**Pins to keep free:** GPIO 6–11 (flash), 0/2/5/12/15 (boot strapping; GPIO 12
pulled high at boot makes the module fail to boot), 1/3 (USB serial). GPIO 34–39 are
input-only. **Analog inputs must be on ADC1 (GPIO 32–39)**: ADC2 is unusable while
Wi-Fi is on, and the dashboard and RF sensing keep Wi-Fi on.

### Inputs (7)

| Channel | Part | ESP32 pin | Kind | Range → 0..1 | Notes |
|---|---|---|---|---|---|
| `battery_voltage` | pack + → 100 k → pin → 22 k → GND | GPIO 34 | analog | 9.0–12.6 V pack (1.62–2.27 V at pin) | 100 nF at the pin. **This is the energy sensor.** |
| `mic` | MAX4466 OUT | GPIO 35 | analog envelope | RMS of the audio | Gain pot mid-way. Needs sampling at ~8 kHz, see below |
| `light_left` | 3.3 V → LDR → pin → 10 k → GND | GPIO 32 | analog | full ADC span | Mounted on the head, looking 35° left |
| `light_right` | same | GPIO 33 | analog | full ADC span | Looking 35° right |
| `ir_distance` | Sharp Vo | GPIO 36 (VP) | analog | ~0.4–3.1 V | Powered from 5 V; 10 µF at the sensor, it draws spiky current |
| `bump` | microswitch to GND, 10 k pull-up to 3.3 V | GPIO 4 | digital | 0/1 | Pressed reads 0. Polarity doesn't matter to the organism |
| `rf_rssi_station` | the ESP32's own radio | — | Wi-Fi RSSI | −90…−30 dBm | Tracks the station's SSID `emergent-station` |

### Outputs (5)

| Channel | Part | ESP32 pin(s) | Kind | Range | Cost |
|---|---|---|---|---|---|
| `stepper_left` | driver STEP / DIR | GPIO 25 / 26 | stepper velocity | −1…1 (full = 1 rev/s) | 1.0 |
| `stepper_right` | driver STEP / DIR | GPIO 27 / 14 | stepper velocity | −1…1 | 1.0 |
| `head_pan` | SG90 signal | GPIO 18 | servo | −1…1 (±90°) | 0.2 |
| `led` | LED + 220 Ω | GPIO 19 | PWM | 0…1 | 0.1 |
| `buzzer` | piezo | GPIO 23 | PWM (5 kHz carrier) | 0…1 | 0.1 |

Both stepper drivers share **EN on GPIO 13**. The firmware should disable the drivers
whenever both wheel commands are ~0, because a stepper at standstill with the driver
enabled draws holding current and would drain the battery for nothing.

### Power

```
3S LiPo ─┬─ BMS ─┬─ stepper drivers VMOT (9–12.6 V, 100 µF each)
         │       ├─ 5 V buck ── ESP32 5V pin, servo, Sharp IR, MAX4466
         │       └─ 100k/22k divider ── GPIO 34
charge pads ── BMS charge input (the station supplies CC/CV 12.6 V, 1 A)
```

Budget: ~0.25 A for the ESP32 with Wi-Fi, up to ~0.7 A per moving stepper at a sane
driver current limit, ~0.5 A servo peaks. The buck needs ≥ 3 A. **LiPo safety:**
charge only through a proper 3S BMS with a current-limited CC/CV supply. The station
firmware senses charge state; it does not make charging safe on its own.

## Config

This is how the body would be declared once the firmware reads its body from a file
(planned; today the same information lives in a board header under
`src/esp32/include/boards/`):

```json
{
  "energy_sensor": "battery_voltage",
  "inputs": [
    {"name": "battery_voltage", "type": "analog",   "pin": 34, "range_v": [1.62, 2.27]},
    {"name": "mic",             "type": "envelope", "pin": 35},
    {"name": "light_left",      "type": "analog",   "pin": 32},
    {"name": "light_right",     "type": "analog",   "pin": 33},
    {"name": "ir_distance",     "type": "analog",   "pin": 36, "range_v": [0.4, 3.1]},
    {"name": "bump",            "type": "digital",  "pin": 4},
    {"name": "rf_rssi_station", "type": "rssi",     "ssid": "emergent-station", "range_dbm": [-90, -30]}
  ],
  "outputs": [
    {"name": "stepper_left",  "type": "stepper", "step": 25, "dir": 26, "enable": 13, "steps_per_rev": 3200, "max_rps": 1.0, "cost": 1.0},
    {"name": "stepper_right", "type": "stepper", "step": 27, "dir": 14, "enable": 13, "steps_per_rev": 3200, "max_rps": 1.0, "cost": 1.0},
    {"name": "head_pan",      "type": "servo",   "pin": 18, "cost": 0.2},
    {"name": "led",           "type": "pwm",     "pin": 19, "cost": 0.1},
    {"name": "buzzer",        "type": "pwm",     "pin": 23, "cost": 0.1}
  ]
}
```

## What the firmware supports today, and what this build needs

| Needed by this build | Status |
|---|---|
| analog, digital, Wi-Fi RSSI inputs; PWM, servo outputs | **supported** |
| Per-input range (`range_v`, `range_dbm`) | **missing.** Today analog is always `raw / 4095`, so the battery reads 0.52–0.73 instead of 0–1 and hunger would never switch on. The simulator already normalizes this way. |
| `envelope` input (mic RMS from a background ~8 kHz sampler) | **missing.** A single `analogRead()` at 20 Hz sees one random point of an audio waveform, not loudness. |
| `stepper` output (velocity mode, shared enable, disabled at rest) | **missing.** Existing `kPwmBidirectional` is for DC motors via H-bridge. FastAccelStepper or the RMT peripheral can generate the steps. |
| Body from a JSON file, editable from the dashboard | **planned** |
| Faster station RSSI (ESP-NOW beacon at ~10 Hz instead of a 5 s scan) | **planned.** The simulator can show what the 5 s scan costs: `--rssi-period`. |

## Why steppers

Steppers don't slip under normal load, so commanded rotation is actual rotation: the
wheels' step counts are real odometry. The organism isn't told that. But a later
"proprioception" input fed from step counts would carry true self-motion, which
DC motors on PWM duty can't give without encoders. That's the grounded path to
heading-aware spatial memory.
