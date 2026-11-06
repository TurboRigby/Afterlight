# Afterlight

A game engine written in C17, with Vulkan rendering and game code in C.
Built for the fun of building an engine.

Afterlight **0.15** adds sleeping constraint islands, joint warm starting,
torque-limited hinge motors, physics diagnostics and viewport joint tools.
Start with the powered-door room, then try the machinery playground.

## Build and run

Requires a C17 compiler, CMake 3.24+, Ninja, SDL3 3.4+, Vulkan 1.1+ and
`glslangValidator`. Follow the [dependency and platform setup](docs/guide.md#build-and-run)
first, then run from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --target run_motor-room_editor
```

Press **Play**. **WASD** walks, **Shift** runs, **Space** jumps, **E** toggles
root motion, **R** restarts and **Escape** releases the mouse.
In the physics demo, **F** grabs/releases, **V** throws, **Q** spawns a dynamic crate and **N** switches rooms. In machinery demos, **Z** brakes/resumes motors and **X** reverses them.

Use `run_machinery-playground_editor` for powered hinges, chains and a rotating platform or
`run_motor-room_player` for standalone play. See the [physics and machinery guide](docs/development-015.md)
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
