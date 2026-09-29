# Traits: Alive by a Few Simple Rules

[← Back to project overview](index.html)

The question this page answers: **what's the smallest set of rules that makes a
robot seem alive (moving, curious, responsive, feeding itself, never stuck on one
thing) without writing any of those behaviors?**

The answer this project bets on has three parts:

1. **A body described as data.** A list of input and output channels, each with a
   kind and a range. The program never learns what a channel is for.
2. **One small loop with a handful of traits.** Each trait is a few lines, says
   nothing about the world, and is borrowed from how animals work.
3. **Two timescales of learning.** Evolution, run in the simulator, supplies the
   instincts a newborn needs to survive its first hour. Learning, on the robot,
   adjusts them for the rest of its life.

The program is [`organism.h`](../src/esp32/include/life/organism.h), and
[`life.md`](life.md) describes it tick by tick. This page is the design view:
which trait is for which goal, and what's measured so far.

## 1. A body from a catalog

A robot is a list of channels from a fixed catalog. Adding a part means adding a
catalog entry once. After that, any robot can use it in its configuration.

| Inputs (read as 0..1) | Outputs (−1..1 bipolar, else 0..1) |
|---|---|
| battery voltage (ADC, per-pack range) | DC motor (H-bridge, 2 PWM pins) |
| light (LDR on ADC) | stepper (STEP/DIR, velocity) |
| sound envelope (electret mic, RMS) | continuous servo (wheel) |
| touch (switch or ESP32 touch pad) | positional servo (joint, head) |
| distance (Sharp IR on ADC, or ToF on I2C) | vibration motor (PWM) |
| shake and turn (MPU6050 accel/gyro, I2C) | light (LED PWM or WS2812) |
| warmth (NTC on ADC) | sound (piezo tone) |
| radio strength (ESP-NOW RSSI) | knock (solenoid pulse) |
| radio message (last ESP-NOW byte heard) | radio (one ESP-NOW byte) |
| charge current (INA219) | switch (relay, magnet, pump) |

Each channel carries just three facts the program may use:

- **`energy`** on exactly one input: its food. Hunger and satiety read it.
- **`hurts`** on any inputs: jolts on them are pain.
- **`bipolar`** on outputs: −1..1 instead of 0..1.

Everything else, like pins, ranges, rate limits and cost, belongs to the body layer,
not the organism. A configuration looks like this (planned format for `body.json`,
roadmap v0.11):

```json
{
  "inputs": [
    {"name": "battery", "kind": "adc", "pin": 34, "range": [1.62, 2.27], "energy": true},
    {"name": "eye_l",   "kind": "adc", "pin": 32},
    {"name": "eye_r",   "kind": "adc", "pin": 33},
    {"name": "ears",    "kind": "mic_envelope", "pin": 35},
    {"name": "bumper",  "kind": "digital", "pin": 4, "hurts": true},
    {"name": "beacon",  "kind": "espnow_rssi"},
    {"name": "heard",   "kind": "espnow_byte"}
  ],
  "outputs": [
    {"name": "wheel_l", "kind": "stepper", "pins": [25, 26], "bipolar": true},
    {"name": "wheel_r", "kind": "stepper", "pins": [27, 14], "bipolar": true},
    {"name": "head",    "kind": "servo", "pin": 18, "bipolar": true},
    {"name": "glow",    "kind": "pwm", "pin": 19},
    {"name": "voice",   "kind": "tone", "pin": 23},
    {"name": "call",    "kind": "espnow_byte"}
  ]
}
```

The simulator already works this way: [`body_plan.h`](../src/sim/body_plan.h)
builds random bodies from real parts (wheels, a tricycle, vibration bristlebots,
two-servo crawlers) and the same organism has to live in each.

## 2. The loop

```text
every 50 ms:  sense → locate → feel → learn → think → act
```

The traits are what happens inside those steps. Anything in the "status" column
without a checkmark isn't built yet.

| Trait | Rule (all of it) | What it's for | Status |
|---|---|---|---|
| **Hunger** | energy below a set-point is a need | feeding, finding the charger | ✓ |
| **Satiety** | energy above a set-point is a (milder) need | not charging forever | ✓ new |
| **Pain** | a jolt on a `hurts` input is a need that fades | not bumping into things | ✓ |
| **Boredom** | rises while it learns nothing | never idle for long | ✓ |
| **Curiosity** | boredom is relieved by *learning progress*, meaning its prediction error is falling, and by arriving somewhere unfamiliar | exploring, poking things, not falling for noise | ✓ new |
| **Places** | a situation that looks unlike any it remembers becomes a new place; familiarity is time spent there | a map without coordinates, going back to good places | ✓ new |
| **Reward** | reward = total need going down (squared needs, so the worst need dominates) | the only thing learning optimizes | ✓ |
| **Hormones** | dopamine = reward vs expectation; adrenaline = recent surprise and pain; cortisol = need left unmet; serotonin = contentment | how much to explore and how fast to learn, never what to do | ✓ |
| **Brain** | fixed random recurrent network with 0.1–10 s timescales, fed its own outputs | rhythms, gaits, short-term memory | ✓ |
| **One learning rule** | readout weight += dopamine × trace(exploration × feature) | turning lucky accidents into habits | ✓ |
| **Instincts** | innate weights from evolution; forgetting relaxes toward them | surviving the first hour; not unlearning the basics | ✓ |
| **Fatigue** | each output's recent effort is a need that rest relieves | rest/activity cycles, no stereotyped loops | planned |
| **Sleep** | while charging and calm, replay recent traces into the readout | consolidation, a reason docking looks like sleep | planned |

### How each goal is supposed to come out

- **Move.** Exploration noise is smooth (Ornstein–Uhlenbeck), so outputs wander
  coherently instead of jittering. The brain gets a copy of its own outputs, so
  rhythmic gaits can emerge for legged or crawling bodies. Whatever movement
  relieves needs gets reinforced. Nothing says which outputs are legs.
- **Explore and be curious.** Boredom grows whenever the forward model stops
  improving. It widens exploration and, once relieved, pays out as reward. The
  relief comes from *learning progress*, so a flickering lamp or its own motor hum
  goes stale, while something new that it *can* learn stays interesting until it
  is learned. That's the "noisy TV" fix from the curiosity literature (Schmidhuber;
  Oudeyer & Kaplan). Unfamiliar places add a small bonus on arrival.
- **Engage: push it and see what it does.** The forward model is fed a copy of the
  robot's own outputs (through the brain), so the consequences of its own actions
  are the most learnable thing in its world. Pushing a movable object, stopping,
  and watching what changes is learning progress. A wall that never moves is learned
  once and goes stale. Developmental psychology calls this effectance, the pleasure
  of being a cause (White 1959).
- **Communicate.** Light, sound and radio are outputs like any other, and mic, light
  and radio are inputs like any other. Its own beep in its own mic is learned in
  minutes and goes stale. Another creature that *answers*, though, is learnable but
  never trivially so, which is the most interesting thing a learning-progress agent
  can find. Signaling only settles into a convention when both sides gain from it
  (Lewis signaling games), so the plan is to measure it with two organisms in the
  simulator. A null result would count as a real result.
- **Feed itself.** Hunger rises as the battery drains. The value estimate learns
  which places and senses precede hunger relief, and dopamine turns the moves that
  led there into habits. Evolution supplies a rough instinct (in the simulator, it
  finds "follow the charger's light/beacon") so a newborn doesn't starve before
  its first meal.
- **Not charging forever.** Three things push it off the dock. Satiety makes a full
  battery mildly unpleasant. Boredom rises on the dock, because nothing new is
  learned there. Cortisol (need left unmet for minutes) widens exploration. None of
  these says "leave".
- **Not stuck on any single action.** Every source of reward habituates. Hunger
  relief stops at satiety. Curiosity relief stops once something is learned. Pain
  fades. Needs are squared, so whichever is worst takes over. That's the
  animal version of task switching, with no scheduler.
- **A simple spatial model.** There's no positioning sensor, so a place is a
  *situation*: what the senses (minus energy) looked like there, smoothed over a
  second. Up to 12 are remembered, and the least familiar is forgotten first. Place
  activations feed the value estimate, the readout and the forward model. So "this
  is where hunger gets relieved" and "turn left here" are learnable. Coordinates,
  paths and a planner are deliberately left out.

### Why these and not more

Each trait here is a single scalar rule with a known counterpart in animals, and
each has an ablation switch (`--traits` in the simulator). A trait stays only if
switching it off makes the simulator measurably worse, or if it's the smallest
fix for a failure the simulator actually showed. Hand-written behaviors ("if
battery low, follow the beacon") are out by construction. They'd make it look
alive for a demo and then never do anything else.

## 3. What the simulator says

RESULTS_PLACEHOLDER

## 4. What's next, in order

1. **Firmware on the new engine.** `main.cpp` still runs the older
   physiology/contingency/spatial engine. The organism above runs only in the
   simulator and the native tests. The next step is a firmware loop that reads
   the catalog channels into 0..1, calls `Organism::tick`, and writes the outputs,
   plus saving the learned weights and places across reboots.
2. **The catalog's missing drivers:** ESP-NOW beacon RSSI and byte (instead of the
   5 s Wi-Fi scan), mic envelope, stepper velocity, piezo tone, MPU6050, and
   `body.json` on LittleFS (roadmap v0.11).
3. **Fatigue** as a fourth homeostatic need, if the simulator shows stereotyped
   loops.
4. **Two organisms in one room** to test signaling and social curiosity, and a
   pushable object to test engagement.
5. **Hardware.** None of this has run on a real robot. The v1.0 gate in the
   [roadmap](roadmap.md) stands.
