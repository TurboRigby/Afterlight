# Afterlight

A game engine written in C17, with Vulkan rendering and game code in C.
Built for the fun of building an engine.

Afterlight **0.17** adds buoyant pools and lakes, flowing rivers, and bounded
pourable fluid, with quality settings and light-dependent water rendering.
Start with the pool, then try the lake, stream and pouring labs.

## Build and run

Requires a C17 compiler, CMake 3.24+, Ninja, SDL3 3.4+, Vulkan 1.1+ and
`glslangValidator`. Follow the [dependency and platform setup](docs/guide.md#build-and-run)
first, then run from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --target run_water-pool_editor
```

Press **Play**, right-click to capture the camera, and use **WASD/QE** to fly.
**Space** makes ripples, **R** resets, **N** switches the demo pair, and **F6** pauses.

Try `run_water-lake_editor`, `run_water-stream_editor`, `run_water-river-bend_editor`,
`run_water-pour_editor`, then `run_water-pour-lab_editor`. **F** tips the cup in the
pouring lab. Each also has a `_player` target. See the [water guide](docs/development-017.md)
for settings, controls and simulation limits, or the [compound prop guide](docs/development-016.md)
for the earlier physics demos.

## Documentation

The [documentation index](docs/README.md) links to all guides. Start with:

- [Full guide](docs/guide.md): setup, demos, verification and repository layout.
- [Editor workflow](docs/editor.md) and [C gameplay](docs/game-code.md).
- [Architecture](docs/architecture.md) and [scene format](docs/scene-format.md).
- [Testing](docs/testing.md), [projects](docs/projects.md) and [exporting](docs/shipping.md).

## Contributing and licensing

Every contribution commit requires a **DCO sign-off** (`git commit -s`).
Read [CONTRIBUTING.md](CONTRIBUTING.md) and the [DCO 1.1](DCO) before contributing.

Original Afterlight files are licensed under the [Mozilla Public License 2.0](LICENSE).
Bundled dependencies retain their [own licenses](THIRD_PARTY_NOTICES.md).
Builds and exports include covered source and notices; see
[licensing and distribution](docs/licensing.md).
