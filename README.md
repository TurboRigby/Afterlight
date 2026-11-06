# Afterlight

A game engine written in C17, with Vulkan rendering and game code in C.
Built for the fun of building an engine.

Afterlight **0.13** adds primitive rigid-body physics, friction and bounce,
sleeping, fast-body sweeps, character pushing, grabbing/throwing and editor diagnostics.
Start with the drop room, then try the larger physics playground.

## Build and run

Requires a C17 compiler, CMake 3.24+, Ninja, SDL3 3.4+, Vulkan 1.1+ and
`glslangValidator`. Follow the [dependency and platform setup](docs/guide.md#build-and-run)
first, then run from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --target run_physics-drop_editor
```

Press **Play**. **WASD** walks, **Shift** runs, **Space** jumps, **E** toggles
root motion, **R** restarts and **Escape** releases the mouse.
In the physics demo, **F** grabs/releases, **V** throws, **Q** spawns a dynamic crate and **N** switches rooms.

Use `run_physics-playground_editor` for stacks, ramps and dominoes or
`run_physics-drop_player` for standalone play. See the [physics guide](docs/development-013.md)
for the other demos, debugging tools and current limits.

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
