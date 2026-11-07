# 0.21 engine foundations

This pass uses the water course and expedition as the representative game.
Engine/editor/game code remains C17. Game module ABI is **23**; rebuild modules
against the matching headers and restart old processes. Scene format remains 23.
Windows verification and ray tracing remain deferred.

## Implemented baseline

- [x] Player start/pause/settings/finish flow and persisted preferences/progress.
- [x] Scene/reload/save/load/failure checks and rendering debug views.
- [x] Resource diagnostics, frame/memory targets and disabled-effect allocations.
- [x] Measured visibility culling and indexed object lookup.
- [x] Material/animation/audio integration and contextual asset diagnostics.
- [x] Native Linux GPU, relocated Release exports and macOS regressions.

This completes the scoped baseline, not every future scalability item: rendering
still submits one draw per visible object. Instancing, LOD, asynchronous geometry
streaming and broader hardware coverage remain future work guided by profiling.

## Play and persistence

```sh
cmake --preset debug
cmake --build --preset debug --target run_water-expedition_player
```

Interactive players start at a menu. Escape/F6 opens pause/settings, Save/Continue,
Restart and Quit. A completed water course opens the finish menu with restart and
next-course actions. `--frames N` batch runs still autostart. Preferences include
AA, sharpness/history weight, exposure and mouse sensitivity. They and the most
recently completed course's best time live in SDL's project-specific user data
directory. The expedition adds a skinned idle guide and checkpoint/finish audio
to its swimming, river, raft, cargo and receiving-cup objectives.

Progress files contain a bounded (1 MiB) checksummed module snapshot, local scene
key, authored-scene byte fingerprint, module name and ABI/state versions. Loading
initializes the authored world and restores module progress. Changed authored
scene files, corrupt/truncated saves and incompatible modules are rejected while
the active game is retained. Saving after an authored scene edit requires a
restart. The fingerprint is a revision guard, not a security signature.

These are local module saves: water particles and rigid-body momentum do not
persist. The sample module snapshot uses its local C representation and is not a
portable cross-platform world format. Writes use a temporary file and rename;
this does not promise crash-durable storage. Hot reload restores live module state
after movement while preserving the session's authored revision.

## Rendering diagnostics and allocations

Open **Performance** for depth (log scale), motion, previous-history and history
rejection views. Debug views temporarily allocate TAA resources without changing
the saved scene AA mode. Invalid motion is magenta; rejected history is red and
accepted history is green. Unsupported temporal blending rejects debug selection.

The default diagnostic targets are 16.67 ms GPU time and 512 MiB tracked retained
GPU memory; C callers can change them with `af_engine_set_render_budget`. These
produce warnings, not hard allocation limits. Counts include Vulkan allocation
requirements for tracked meshes, textures, frame targets/buffers, environment,
shadows and post effects. UI buffers/fonts and driver/presentation allocations
are excluded. GPU samples arrive asynchronously and exclude presentation.

Post images follow the selected effects: Off keeps HDR; FXAA adds its display
image; SMAA adds scratch images and official lookup textures; TAA adds history and
motion. Bloom scratch is conditional; unused water refraction snapshots shrink
to 1x1 placeholders. Configuration changes wait for the device once and can
briefly pause. The resource-switch test returns exactly to its original Off
allocation after cycling all AA modes and bloom. On the macOS test window, post
allocations were 12,238,848 bytes Off, 16,318,464 FXAA, 24,932,480 SMAA,
36,716,544 TAA and 43,646,976 TAA+bloom. These sizes depend on resolution,
swapchain image count and device alignment.

## Visibility baseline

Scene IDs now use an eagerly maintained hash index while retaining authored object
order. A 10,000-object test covers growth, lookup, hierarchy removal, clone
ownership and unordered imported IDs. Conservative main-pass frustum culling
uses unjittered camera bounds expanded two framebuffer pixels, including current
skinned/water bounds. Shadow visibility keeps its existing path.

[Recorded CSV](benchmarks/021-linux-visibility.csv), native Arch Linux x86_64,
GNOME Wayland, NVIDIA RTX 5060 / driver 615.78.08, GCC 16.2.1, Release build,
**validation disabled**:

| Mode | Objects | Draws | Culled | GPU scene ms | CPU render less fence/acquire ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Unculled | 5,001 | 5,001 | 0 | 0.149111 | 1.943414 |
| Culled | 5,001 | 1 | 5,000 | 0.010770 | 1.462372 |

Reproduce on a native desktop after building Release:

```sh
build/release/afterlight_renderer_benchmark 5000 --no-validation
```

The window/framebuffer is 400x300 on the tested scale-1 desktop. One cube is
visible and 5,000 instances of the shared cube mesh lie off-screen; shadows are
disabled and physics/animation are paused. Each phase averages 31 samples after
warm-up. A central 64x64 pixel region must match byte-for-byte. Both modes retain
18,227,056 tracked GPU bytes. This isolates culling, not dense-scene throughput,
instancing or a representative game's frame budget. FIFO pacing caps observed
FPS. The CPU column still includes presentation/driver work after subtracting
fence/acquire waits; it is not pure CPU submission cost. Validation adds substantial
per-draw overhead and is enabled separately for correctness tests.

## Asset and platform checks

Player `--check` validates object asset references and semantic kinds as well as
scene/asset decoding and the module ABI. Import/reload errors include the source
asset name/path. Failed grouped reloads retain last-good resources. The existing
PBR, environment, shadow, animation, water optics and editor tests remain part of
the broad regression suite; no new material model is claimed.

Linux tests used an isolated build on a real GNOME Wayland desktop with NVIDIA
Vulkan, plus Debug/Release and ASan/UBSan builds. Relocated exported courses run
after source removal. Arch absolute `ldd` entries are normalized before packaging.
Synthetic SDL events explicitly clear all union storage. GNOME may refuse
programmatic unminimize; the smoke test reports that restriction after completing
its rendering assertions. Actual unminimize requires desktop activation.

The 21-test focused suite passed on macOS and native Linux, including ASan/UBSan.
Full regression coverage is 170 checks on macOS and 169 on Linux (the extra
macOS check covers module unload). Source archives were refreshed after final
edits and affected packaging/menu checks rerun. The macOS ASan/UBSan CPU suite
also covers 103 checks. GPU tests use
synchronization validation; only the performance run above disables it. Sanitizer
GPU runs disable leak detection. Audio uses SDL's dummy backend, so audible
speaker output was not assessed. See [native test commands](testing.md#native-linux-021).
The locally edited water-course scene is preserved.
