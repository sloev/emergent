# Emergent

> **Disclaimer:** this project is built with the help of several different AI systems —
> code, docs, and design included.

A synthetic ethology platform for ESP32-class hardware: creature-like behavior from
homeostatic drives, a decaying contingency memory, and a coarse spatial map — no cloud
inference, no hand-authored behavior tree, no large neural network.

- **Design doc:** [`docs/synth-behavior.md`](docs/synth-behavior.md) ([PDF](docs/synth-behavior.pdf))
- **Roadmap:** [`docs/roadmap.md`](docs/roadmap.md)
- **Robot firmware:** [`src/esp32`](src/esp32) — PlatformIO project, configurable across
  ESP32 boards and IO (sensors/actuators), with an onboard WiFi hotspot serving a live
  dashboard.
- **Charging station firmware:** [`src/station`](src/station) — a separate, much simpler
  reference project for the companion charging dock.
- **Site:** [sloev.github.io/emergent](https://sloev.github.io/emergent/), published via
  GitHub Pages from `docs/`.
- **Releases:** [GitHub Releases](https://github.com/sloev/emergent/releases) — tagged
  per version bump.

## Repo layout

```
docs/               GitHub Pages site: paper, PDF, roadmap
src/esp32/          Robot firmware (PlatformIO)
src/station/        Charging station reference firmware (PlatformIO)
.github/workflows/  CI: firmware build + native unit tests, docs rebuild
```

## Status

v0.9.0 released; v0.9.1 (hardware validation) is open and blocked pending real hardware.
See [`docs/roadmap.md`](docs/roadmap.md) for the current release and full task list per
version — we work one release at a time, versioned as `0.x.y` with rolling minor/patch
bumps; a `1.0.0` (or any major bump) is a deliberate, human-approved decision. Nothing in
this repo has been run on real hardware yet; every release's verification is PlatformIO
builds, native unit tests, and browser-based UI checks against a mocked API.

## License

[MIT](LICENSE)
