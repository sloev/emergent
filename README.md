# Emergent

> **Disclaimer:** this project is built with the help of several different AI systems —
> code, docs, and design included.

A synthetic ethology platform for ESP32-class hardware: creature-like behavior from
homeostatic drives, a decaying contingency memory, and a coarse spatial map — no cloud
inference, no hand-authored behavior tree, no large neural network.

## Build

```sh
cd src/esp32
pio run -e esp32dev              # or -e esp32-s3-devkitc-1
pio run -e esp32dev -t uploadfs   # flash the dashboard filesystem
pio run -e esp32dev -t upload     # flash the firmware
pio test -e native                # host-native unit + behavior-scenario tests
```

See [`src/esp32/README.md`](src/esp32/README.md) for adding a board or channel, the
dashboard API, and what the native tests cover. [`src/station`](src/station) is a
separate, much simpler PlatformIO project for the companion charging dock.

## Architecture

Named-channel body (sensors/actuators) → physiology/drives → contingency memory
(action→sensor correlation) → spatial memory (sensor-signature "places", since there's
no positioning sensor) → action generator, on a 20 Hz loop. Full spec:
[`docs/synth-behavior.md`](docs/synth-behavior.md) ([PDF](docs/synth-behavior.pdf)).
Theory, prior art, and speculative extensions live separately in
[`docs/research.md`](docs/research.md) — not the spec.

## Status

| Layer | Fact |
|---|---|
| Implemented | 20 Hz loop: sense → physiology → drives → memory → action → actuate → learn. Named-channel body. SoftAP dashboard. Life-state JSON. Station firmware as a separate state machine. |
| Tested | Host-native tests for bit packing, age saturation, rate-limit clamp, LEDC cap, and organism behavior (energy dynamics, contingency bias) against a fake body. Not real hardware. |
| Hardware tested | Nothing. No board has run this. |
| Designed / planned | Approach-and-dock, ablations, extra boards. |
| Hypothesis | That drives + decaying memory + noise will look alive, including charging as an attractor. Unverified. |

See [`CHANGELOG.md`](CHANGELOG.md) for what shipped in each release and
[`docs/roadmap.md`](docs/roadmap.md) for open work — one release at a time, versioned
`0.x.y` with rolling minor/patch bumps; a `1.0.0` (or any major bump) is a deliberate,
human-approved decision, gated on the validation this table calls out as missing.

## Links

- **Site:** [sloev.github.io/emergent](https://sloev.github.io/emergent/), published via
  GitHub Pages from `docs/`.
- **Releases:** [GitHub Releases](https://github.com/sloev/emergent/releases) — tagged
  per version bump.

## License

[MIT](LICENSE)
