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
| Implemented | 20 Hz loop: sense → physiology → drives → memory → action → actuate → learn. Named-channel body. SoftAP dashboard. Life-state JSON. Station firmware as a separate state machine. |
| Tested | Host-native tests for bit packing, age saturation, rate-limit clamp, LEDC cap, and organism behavior (energy dynamics, contingency bias) against a fake body. Not real hardware. |
| Hardware tested | Nothing. No board has run this. |
| Designed / planned | Approach-and-dock, ablations, extra boards. |
| Hypothesis | That drives + decaying memory + noise will look alive, including charging as an attractor. Unverified. |

---

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
