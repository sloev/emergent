# Emergent simulator

Runs the **unmodified firmware behavior engine** (`src/esp32/include/core/`, header-only)
in a 2D room with a charging station, and measures what it does. The point is to find
out, before any hardware exists, what the engine actually does with realistic inputs,
and to have a number to compare every change against.

```sh
cd src/sim
make
./emergent-sim --runs 8 --lives 6 --hours 3              # every condition, ~1 min each
./emergent-sim --condition full --runs 8 --lives 6        # one condition
./emergent-sim --trace out.csv --seed 5 --lives 1 --hours 3   # one organism, 1 Hz trace + learned wiring
```

`docs/sim-viewer.html` plays back a trace (the site ships one: seed 5, first life).

## The world

The body is the build in [`docs/example-body.md`](../../docs/example-body.md): two
stepper wheels, a panning head with two light sensors, mic, bumper, IR distance,
station RSSI, battery; LED and buzzer as outputs. Every model is simple but grounded:

| Part | Model |
|---|---|
| Room | 3 × 2.5 m rectangle, window along the top wall |
| Drive | differential, 0.2 m/s max, 15 cm track; steppers don't slip; walls clamp position and press the bumper |
| Battery | 3S LiPo 1 Ah, LiPo-shaped discharge curve, 0.15 Ω sag under load, +0.3 V while charging; 0.25 A base draw, 0.7 A per stepper at full speed |
| Station | contacts on the right wall behind a 45° V funnel (30 cm deep); 1 A CC/CV charge, tapering above 80%; LED lure; clicks while charging |
| Light | inverse-square from the station LED and the window, cosine acceptance per sensor, LDR-like compression |
| RF | log-distance path loss (n = 2.5), spatially correlated shadowing (σ 4 dB), 2 dB fast fading, **scan every 5 s** as in firmware (`--rssi-period` to change) |
| Mic | own steppers (A4988-loud), own buzzer, station clicks, room noise |
| IR | Sharp GP2Y0A21: 10–80 cm, output ~ 1/d |
| Safety floor | firmware's: below 5% of the energy channel, every actuator is forced to 0 |

A **life** ends when the battery is flat away from the dock. The next life is what an
experimenter would do with the real robot: recharge it, set it down somewhere random,
power it on. Memories and learned reflexes persist (the firmware saves life-state);
physiology restarts.

## Conditions

| Condition | What changes |
|---|---|
| `full` | nothing: the engine as it is |
| `hand_wired` | reflexes set by hand to Braitenberg phototaxis in the hunger context, then frozen. **An upper bound for what the representation can do, not emergent.** |
| `hand_wired_plastic` | starts hand-wired, keeps learning (does learning keep a good solution?) |
| `no_reflexes` | learned sensorimotor reflexes off |
| `no_contingency` / `no_spatial` | that memory's influence on action removed |
| `no_battery_link` | battery not wired to `h_energy` |
| `noise_only` | no memory, no reflexes: drives + exploration noise |
| `random_walk` | null model: run-and-tumble at fixed speed, no engine at all |

## Results (8 organisms × 6 lives × max 3 h, seeds 1–8)

| Condition | Lifespan (h) | Lives that died | Time docked | Lifespan by life number 1 → 6 |
|---|---|---|---|---|
| `hand_wired` | 2.46 ± 0.73 | 40% | 34% | 2.49 2.49 2.21 3.00 2.21 2.33 |
| **`full`** | **1.86 ± 0.95** | **67%** | **38%** | 2.21 1.94 1.13 2.29 1.82 1.79 |
| `hand_wired_plastic` | 1.79 ± 1.02 | 62% | 37% | 2.08 1.69 1.42 1.06 2.07 2.44 |
| `no_contingency` | 1.61 ± 0.96 | 71% | 36% | 2.11 1.59 1.78 1.68 1.38 1.12 |
| `no_spatial` | 1.61 ± 0.97 | 71% | 30% | 1.65 1.90 1.66 1.40 1.31 1.72 |
| `random_walk` | 1.28 ± 0.18 | 100% | 15% | 1.26 1.40 1.25 1.34 1.20 1.22 |
| `no_reflexes` | 1.26 ± 0.06 | 100% | 2% | 1.28 1.26 1.24 1.28 1.23 1.26 |
| `noise_only` | 1.26 ± 0.05 | 100% | 2% | 1.25 1.24 1.28 1.25 1.26 1.25 |
| `no_battery_link` | 0.79 ± 0.11 | 100% | 3% | 0.84 0.78 0.81 0.76 0.75 0.78 |

What this supports:

- **Learned reflexes keep it alive longer.** From all-zero weights, mean lifespan goes
  from 1.26 h (no reflexes) to 1.86 h, and a third of lives survive the 3 h cap
  (none do without reflexes). That's about 60% of the way to the hand-wired bound.
- **The battery link is what makes it matter.** Unhook the battery from `h_energy` and
  lifespan drops to 0.79 h, worse than doing nothing: without the link docking isn't
  rewarded, and the reflexes learn whatever else relieves drives.
- **Both memories contribute a little** (1.61 h without either one, vs 1.86 h), but the
  spread overlaps; 8 organisms is not enough to call it.

What it does not support yet:

- **No accumulation across lives.** Lifespan by life number is flat or noisy, not
  rising. Most of the learning happens inside the first life that stumbles onto the
  dock, and a later life can undo it.
- **It learns to stay, not to come and go.** In the seed-5 trace, time docked rises
  from 6% to over 90%, and it stays docked even at 87% charge. The hand-wired organism
  does forage-and-return (it's much nearer the station when hungry: 0.75 m vs 1.69 m);
  the learned one isn't (1.31 vs 1.33 m).
- **Learning can erase a working solution** (`hand_wired_plastic` does worse than
  `hand_wired`).

## What the simulator found in the engine (fixed on this branch)

Each of these would have shown up on hardware as "it just sits there" or "it just
spins", with no way to tell why:

1. **Hunger arrived minutes before death.** The energy band's lower edge (0.4) is ~9%
   state of charge on a LiPo read across 9.0–12.6 V. Now 0.65 (~50%).
2. **Hunger froze the organism.** Exploration width *fell* with energy pressure, so a
   hungry organism with nothing learned stood still and died. Now hunger widens
   exploration (food deprivation raises activity in animals) and threat narrows it.
3. **Energy was counted twice**: pulled toward the real battery *and* drained by a
   modelled cost. With an energy sensor, the sensor is the metabolism now.
4. **Sensor noise read as danger.** Any tick-to-tick jump dropped `h_safety`, and ADC
   noise alone kept it pinned. Now each channel learns its own noise floor, salience
   is prediction error against a constant-velocity guess, and startle habituates.
5. **Curiosity punished novelty.** `total_drive()` included curiosity, which rises
   with surprise. Now excluded from the reward; it acts through exploration width.
6. **Drive reduction is now convex** (sum of squared drives, Keramati & Gutkin 2014),
   so relieving real hunger outweighs small fluctuations.
7. **Jitter instead of movement.** Per-tick independent noise averaged to standing
   still. Exploration is now an Ornstein–Uhlenbeck process (τ = 1 s).
8. **Contingency memory looks one tick (50 ms) back**, too short to connect an approach
   with docking seconds later. The new learned reflexes carry a 3 s eligibility trace.

## Known limitations of the model

- Charging is 1 A regardless of how the robot arrives; real pogo pins need alignment.
- Shadowing is a fixed random field, not walls or furniture; there are no obstacles.
- The station LED is always on; the real station firmware changes it with charge state.
- One organism alone. Signaling between organisms needs a second one in the room.
