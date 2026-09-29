# Roadmap

Rolling versioning: `0.MINOR.PATCH`, incremented as work lands. A major version bump
(`1.0.0`, `2.0.0`, ...) is a deliberate decision made by a human — never automated.

We work **one release at a time**. Don't start tasks from a later release until the
current one is closed out, unless explicitly reprioritized.

Closed releases are one line each in [`CHANGELOG.md`](../CHANGELOG.md). This file is
open work only.

## Status

| Layer | Fact |
|---|---|
| Implemented | 20 Hz loop: sense → physiology → drives → memory → action → actuate → learn. Learned sensor→actuator reflexes trained by drive reduction. Named-channel body. SoftAP dashboard. Life-state JSON. Station firmware as a separate state machine. |
| Tested | Host-native tests for bit packing, age saturation, rate-limit clamp, LEDC cap, and organism behavior (energy dynamics, contingency bias, reflex learning rule) against a fake body. Not real hardware. |
| Simulated | The unmodified engine in a modelled room with a station (`src/sim`): learned reflexes lift mean lifespan from 1.26 h to 1.86 h (hand-wired bound 2.46 h). Learns to reach and stay on the dock; not yet to leave when full, and nothing accumulates across lives. |
| Hardware tested | Nothing. No board has run this. |
| Designed / planned | Body declared in a JSON file with per-input ranges, stepper and mic-envelope channel kinds ([example build](example-body.md)); approach-and-dock on hardware; extra boards. |
| Hypothesis | That drives + decaying memory + learned reflexes + noise will look alive on real hardware, including charging as an attractor. Unverified. |

---

## v0.10.0 — Simulator and learned reflexes (current)

- [x] Native simulator driving the unmodified engine in a modelled room with a station,
      ablation conditions, a no-engine baseline and a hand-wired upper bound
      ([src/sim](../src/sim/README.md))
- [x] Learned sensor→actuator reflexes trained by drive reduction, with an eligibility trace
- [x] Physiology fixes the simulator exposed: hunger band, hunger→activity, energy double
      count, sensory adaptation and startle habituation, curiosity out of the reward,
      convex drive, correlated exploration noise
- [x] [Example build](example-body.md): 7 inputs, 5 outputs on one ESP32 DevKit
- [ ] Save reflex weights in life-state (today they're lost on reboot)
- [ ] Leave when full: the learned organism stays docked at 87% charge
- [ ] Learning that accumulates across lives instead of peaking in the first one
- [ ] Reflex weights on the dashboard

## v0.10.1 — Traits: satiety, curiosity as learning progress, places

Design and rationale: [traits.md](traits.md).

- [x] Satiety: energy above a set-point is a need (no reason to charge forever)
- [x] Boredom relieved by learning progress instead of raw surprise (noise goes stale)
- [x] Place memory: remembered situations as features, arrival novelty
- [x] `--traits` ablation switch in the simulator; docked-while-full and places metrics
- [x] Re-evolved organism-0 genome for the two-station world
- [ ] Firmware loop on `life/organism.h` (today only the simulator runs it)
- [ ] Save learned weights and places across reboots
- [ ] Fatigue need; two organisms in one room (signaling); a pushable object (engagement)

## v0.11.0 — Body from a file

- [ ] `body.json` on LittleFS, editable from the dashboard, applied live without reflash
- [ ] Per-input range (battery 9.0–12.6 V → 0..1)
- [ ] `envelope` input for an analog mic (background sampling, RMS)
- [ ] `stepper` output (velocity mode, shared enable, disabled at rest)
- [ ] ESP-NOW station beacon for ~10 Hz RSSI (the simulator can measure what the 5 s scan costs)

## v0.9.1 — Hardware validation (blocked, no hardware in this environment)

- [ ] End-to-end test: emergent approach-and-dock behavior over repeated sessions

Split out of v0.9.0 rather than left blocking it, since everything else in
that release was done and independently verifiable, and this one task
isn't a firmware task at all — it's "build the physical robot and station,
run them together, and watch what happens over repeated sessions."

**Blocked, honestly:** needs a real robot, a real station, and repeated
physical trials — there is no way to verify this from firmware code review
or PlatformIO builds, and this environment has no hardware to run it on.
Every prior release has carried a "not yet run on real hardware" caveat;
this is the first task that can't even be partially exercised without
hardware, since it's fundamentally about physical behavior over time, not
firmware correctness. Left unchecked rather than claimed done. This is
also, not coincidentally, exactly the kind of validation v1.0.0 is gated
on below — this milestone stays open until that hardware exists.

## v1.0.0 — First stable "alive" release (human-gated)

- [ ] All of the above validated on real hardware
- [ ] Experimental program from the paper (`synth-behavior.md` §15) run at least
      once: homeostasis quality, exploration structure, history dependence,
      perceived aliveness
- [ ] Ablation switches implemented (disable stochasticity, freeze contingency memory,
      clear spatial memory, flatten drives, decouple battery from `h_energy`)
- [ ] Human sign-off to cut 1.0.0

## Backlog / later

- Multi-agent / multi-robot stigmergic experiments (scent, RF, light tags)
- Mechanical voice / prosody experiments
- Additional board support as hardware becomes available
- Formal comparison against HRL baselines
