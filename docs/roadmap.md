# Roadmap

Rolling versioning: `0.MINOR.PATCH`, incremented as work lands. A major version bump
(`1.0.0`, `2.0.0`, ...) is a deliberate decision made by a human — never automated.

We work **one release at a time**. Don't start tasks from a later release until the
current one is closed out, unless explicitly reprioritized.

## Status

**Current release: v0.8.0** (in progress)

---

## v0.1.0 — Scaffolding

- [x] Git repo initialized
- [x] `docs/` site skeleton (`index.html` frontpage, `synth-behavior.md`, this roadmap)
- [x] GitHub Actions workflow to rebuild `synth-behavior.pdf` from the markdown source
- [x] ESP32 PlatformIO project skeleton (`src/esp32`)
- [x] Board configuration abstraction (per-board pin/IO profiles)
- [x] Push to GitHub, enable Pages (serve from `docs/` on `main`)
- [x] CI: PlatformIO build check for at least one board target

## v0.2.0 — Body layer: sensors & actuators

- [x] Generic `Actuator` interface (range, rate limit, cost estimate, safety envelope)
- [x] Generic `Sensor` interface (normalized numeric channel, update rate)
- [x] IO profile format mapping named channels to physical pins/drivers
- [x] Drivers for an initial hardware set: motors (PWM/H-bridge), servo, RGB LED,
      ADC-based sensors (battery voltage/current), digital touch/bump
- [x] Board profiles for at least two boards (e.g. ESP32 DevKit, ESP32-S3)
- [x] Sensor/actuator self-test routine over serial

Verified: both board targets (`esp32dev`, `esp32-s3-devkitc-1`) build clean with
PlatformIO. Not yet verified against real hardware — pin choices per board are a
starting point, expect to override them per physical chassis.

## v0.3.0 — WiFi hotspot & dashboard

- [x] SoftAP mode, SSID/password configurable per board profile
- [x] Async web server serving the dashboard UI
- [x] Live telemetry push (WebSocket or SSE): raw sensor values, actuator outputs
- [x] Minimal dashboard front-end served from firmware (LittleFS)
- [x] Config page: change WiFi credentials without reflashing (persisted to NVS)

Verified: both board targets build the firmware and the LittleFS image clean
with PlatformIO. Not yet run on real hardware. "Drive targets" config is
deferred to v0.4.0, once drives exist to configure — for now the dashboard's
manual actuator sliders stand in for direct output control.

## v0.4.0 — Physiology & drives

- [x] Internal variable vector (`h_energy`, `h_fatigue`, `h_safety`, `h_arousal`,
      `h_curiosity`, `h_boredom`, `h_social`)
- [x] Update rules: decay/recovery, sensor coupling, actuator-cost coupling
- [x] Drive computation from deviation against target bands
- [x] Fuzz-scale / exploration modulation from drives
- [x] Dashboard: live physiology & drive readout

Verified: both board targets build clean with PlatformIO; a 20 Hz behavior
tick runs `Physiology::update()` and the dashboard's new Physiology section
shows live value/drive per variable plus `fuzz_scale`. Not yet run on real
hardware, so the tuning constants (decay/recovery/coupling rates) are
starting points, not calibrated values. `h_social` has no sensor source yet
(needs another robot/human present) and sits at rest until a later release.
`h_curiosity`'s novelty signal is a placeholder (proportional to raw sensor
change) — real prediction error arrives with contingency memory in v0.5.0.

## v0.5.0 — Contingency memory

- [x] Fixed-size action-delta / sensor-delta record structure
- [x] Quantization & hashing of deltas
- [x] Learn/decay/prune update algorithm bounded to `MAX_ENTRIES`
- [x] Contingency-biased action query
- [x] Dashboard: inspect top contingency entries

Verified: both board targets build clean with PlatformIO (RAM use +7KB for
the 256-entry table). Not yet run on real hardware. The article's pseudocode
doesn't specify how a sensor-delta pattern gets linked to "led to improving
drives" — that's left to whoever queries the memory. This implementation
makes it concrete: each entry tracks an EMA of drive-pressure change
(`mean_drive_delta`) alongside the action/sensor codes, so `query_bias()` can
rank matches by whether they historically helped. `query_bias()` is
implemented and exposed on the dashboard, but nothing calls it yet — no
action generator exists until v0.7.0, so today the table only learns from
whatever a human does via the dashboard sliders or the heartbeat blink.

## v0.6.0 — Spatial memory & navigation

- [x] Coarse grid / place-cell representation
- [x] Visit stats + drive-improvement profile per cell
- [x] Spatial bias contribution to action generation
- [x] Dashboard: simple 2D map view of visited cells

Verified: both board targets build clean with PlatformIO. Not yet run on
real hardware. A significant, deliberate departure from the article's
pseudocode: this hardware has no positioning sensor (no encoders/IMU/GPS),
and the article's `spatial_bias(position, drives, spatial_map)` assumes
`position` already exists without saying where it comes from. Rather than
fabricate open-loop odometry from commanded motor duty cycle — which would
drift into meaninglessness within seconds, since duty cycle isn't measured
displacement — this uses the article's own explicitly offered alternative
(§8.1): "a coarse 2-D grid OR a set of place cells". A place cell here is a
coarse signature of current sensor readings; same ambient conditions are
treated as the same place, which is the same gradient-following idea §10
already uses for individual channels (light, RF), just made the identity of
a location instead of a single bias term.

Consequence: `best_cell()` (the "spatial bias contribution" task) answers
*which* remembered place looks most promising for current drives, but not
*which direction* to move — there's no direction without real coordinates.
Turning that into actual motor output is deferred to v0.7.0's core loop,
which will need a concrete policy for it (most likely: bias exploration
persistence/exploitation balance rather than literal steering, given the
hardware). Currently, with only 2 sensor channels on both board profiles,
signature entropy is low (few distinguishable "places") — richer
differentiation needs richer sensor profiles, which is a board-config
change, not an engine change.

## v0.7.0 — Core loop integration

- [x] Full tick loop wired end-to-end (sense → physiology → drives → memory query →
      action → actuate → learn)
- [x] Task split: high-frequency (PWM/ADC/audio), behavior task (15–30 Hz),
      slow tasks (~1 Hz logging/checkpoints)
- [x] Fixed-point/integer arithmetic pass for the hot loop — evaluated, not done (see note)
- [x] On-device logging to flash for later analysis

`ActionGenerator` is the piece every prior release was missing: physiology,
contingency memory, and spatial memory now actually drive the actuators each
tick (`baseline + drive-scaled noise + contingency bias + spatial nudge`,
clamped) instead of only observing whatever the dashboard did. A channel
recently touched from the dashboard (`Actuator::manual_override_active`,
3s window) is left alone rather than fought over — the heartbeat blink uses
the same mechanism (`write_manual`) so it stays system-owned without a
hardcoded channel-name exception in the engine.

Task split: high-frequency work (PWM via hardware LEDC, ADC via on-demand
cached `Sensor::read()`) needs no separate task — the hardware and existing
on-demand reads already satisfy it. Behavior task is the existing 20 Hz
tick. Slow task is the new `StateLogger`, ~1 Hz, appending physiology +
total actuator cost to a 64KB-capped `/log.csv` on the same LittleFS
partition the dashboard serves from — downloadable at `/log.csv` with zero
extra server code, and exposed as a button on the dashboard's Config tab.

Fixed-point arithmetic: evaluated and deliberately not done. The article's
advice (§14.2) targets microcontrollers generically; both ESP32 and ESP32-S3
have a hardware single-precision FPU, so float ops in a tick this small
(a few hundred multiply-adds across four modules, worst case) cost
microseconds — replacing them with fixed-point would trade real readability
for no measurable benefit on this specific hardware. Revisit only if a
future board target lacks an FPU.

Honest limitation carried over from v0.6.0: `SpatialMemory::best_cell()`
still can't say *which direction* to move, only *which remembered place*
looks best. `ActionGenerator` resolves that the way flagged there — as an
explore/exploit nudge (wider noise if a better place than "here" is known,
narrower if "here" already looks best), not fabricated steering.

Verified: both esp32dev and esp32-s3-devkitc-1 build clean (firmware +
LittleFS image) with PlatformIO. Dashboard changes (manual-override badge,
log download link) verified against a mocked API in a real browser. Not yet
run on real hardware — the gains/constants above (noise, bias, and nudge
scaling) are starting points, not tuned values.

## v0.8.0 — Life-state persistence

- [x] Organism state serialization (physiology, drives, contingencies, spatial memory, stats)
- [x] Download/upload state via the dashboard
- [x] Channel-fingerprint + remapping layer for transferring state across bodies

The article's schema (§13) is JSON with an opaque `"contingencies": [/*
compressed entries */]` blob — it doesn't say how those entries stay
meaningful if you move them to a body with different channels. Resolved
that concretely: contingency/spatial entries are serialized by channel
*name* (`{"actuators": {"motor_left": 1}, "sensors": {"battery_voltage": 1},
...}`), not bit position. On restore, each name is looked up in the
*current* body; matches get re-packed into whatever index that channel
happens to be at now, and anything that doesn't exist on the new body is
simply absent from the map — its contribution to that entry is dropped,
which is what "gradually forgets mismatched contingencies" cashes out to
concretely here. An entry that loses every channel it referenced is
discarded outright rather than kept as a meaningless all-zero record.
`channel_fingerprint` (hash of the current channel name list) rides along
for information/debugging, but restore doesn't require it to match —
name-based lookup handles cross-body transfer either way.

One accepted ambiguity, worth knowing about: spatial memory's signature
quantizes each sensor into 4 levels (0-3), and 0 is both "lowest quartile"
and "this channel wasn't in the saved map" (e.g. restoring onto a body with
extra sensors the old one didn't have). No way to distinguish those from
the encoding alone; unmapped sensor channels default to level 0 on restore.

Dashboard's Config tab gained a Life State section: download button (`GET
/api/state`, same LittleFS-adjacent pattern as the log) and a file-upload
form that POSTs the JSON back for restore, reporting how many entries
carried over and whether the fingerprint matched.

Verified: both esp32dev and esp32-s3-devkitc-1 build clean (firmware +
LittleFS image) with PlatformIO. Upload/download UI flow verified against a
mocked API in a real browser (Puppeteer) — the actual encode/decode/remap
logic is reasoned through carefully but not yet exercised on real hardware
or with a genuine cross-board transfer.

## v0.9.0 — Charging station integration

- [ ] Reference charging station firmware/circuit notes (RF beacon, LED pattern,
      audio cue, power module states: IDLE/CHARGING/FULL)
- [ ] Robot-side: `battery_voltage`/`current` as ordinary sensor channels feeding `h_energy`
- [ ] End-to-end test: emergent approach-and-dock behavior over repeated sessions

## v1.0.0 — First stable "alive" release (human-gated)

- [ ] All of the above validated on real hardware
- [ ] Experimental program from the paper (§15) run at least once: homeostasis quality,
      exploration structure, history dependence, perceived aliveness
- [ ] Ablation switches implemented (disable stochasticity, freeze contingency memory,
      clear spatial memory, flatten drives, decouple battery from `h_energy`)
- [ ] Human sign-off to cut 1.0.0

## Backlog / later

- Multi-agent / multi-robot stigmergic experiments (scent, RF, light tags)
- Mechanical voice / prosody experiments
- Additional board support as hardware becomes available
- Formal comparison against HRL baselines
