# Emergent

A synthetic ethology platform for ESP32-class hardware: creature-like behavior from
homeostatic drives, a decaying contingency memory, and a coarse spatial map — no cloud
inference, no hand-authored behavior tree, no large neural network.

- **Design doc:** [`docs/synth-behavior.md`](docs/synth-behavior.md) ([PDF](docs/synth-behavior.pdf))
- **Roadmap:** [`docs/roadmap.md`](docs/roadmap.md)
- **Firmware:** [`src/esp32`](src/esp32) — PlatformIO project, configurable across ESP32
  boards and IO (sensors/actuators), with an onboard WiFi hotspot serving a live dashboard.
- **Site:** `docs/index.html`, published via GitHub Pages.

## Repo layout

```
docs/               GitHub Pages site: paper, PDF, roadmap
src/esp32/          ESP32 firmware (PlatformIO)
.github/workflows/  CI: rebuilds docs/synth-behavior.pdf from the markdown source
```

## Status

Early scaffolding stage. See [`docs/roadmap.md`](docs/roadmap.md) for the current release
and task list — we work one release at a time, versioned as `0.x.y` with rolling minor/patch
bumps; a `1.0.0` (or any major bump) is a deliberate, human-approved decision.

## License

[MIT](LICENSE)
