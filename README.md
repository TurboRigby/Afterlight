# Afterlight

A game engine written in C17, with Vulkan rendering and game code in C.
Built for the fun of building an engine.

Afterlight **0.22** features a complete landscape terrain sculpting and instanced
foliage system, comprehensive post-processing pipeline (ACES/AgX tone mapping,
bloom, color grading, stochastic bokeh DoF, motion blur, vignette, chromatic
aberration, film grain), single-binary game bundling, native extensions (AMD FSR,
NVIDIA DLSS/NGX), and optional hardware ray-traced sun shadows and water reflections.
See the [0.22 release guide](docs/development-022.md) and [documentation index](docs/README.md).

## Build and run

Requires a C17 compiler, CMake 3.24+, Ninja, SDL3 3.4+, Vulkan 1.1+ and
`glslangValidator`. Follow the [dependency and platform setup](docs/guide.md#build-and-run)
first, then run from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --target run_water-course_editor
```

Press **Play**. **WASD** moves, **Space** jumps/swims up, **Ctrl** dives, and
**Shift** runs on land. **F/V** grab/throw, **R** restarts, and **N** changes courses.

Start with `run_water-course_editor`, then `run_water-expedition_editor`. The
expedition adds a river, cargo and a receiving-cup objective; **E** near the source
cup starts pouring. Both have `_player` targets. See the [water and wind guide](docs/development-019.md)
for controls, settings, benchmarks and limits, or the [water laboratory guide](docs/development-017.md)
for the six focused demos.

## Low Water

[Low Water](games/low-water/README.md) is a standalone first-person story chapter
set in a flood archive. Build and launch it with the `run_low_water` target.

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
