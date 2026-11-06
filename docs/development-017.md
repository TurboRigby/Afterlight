# 0.17 water development

The requested sequence is pools/lakes, rivers/streams, then pourable fluid.
The current scene format is 19 and game ABI is 18; earlier stages below describe
the compatible format/ABI additions made along the way.

The first stage adds eight authored water surfaces, scene format 17 and C module
ABI 16. Old scenes 1–16 still load. Add water in the object panel, then edit its
size, depth, density, drag, wave speed/damping/wind and optical settings in the
inspector. Owners are root nodes with unit scale and yaw rotation. Definitions
survive saving, duplication, prefabs, undo and Play cloning; wave state is runtime
only and resets on Stop or scene reload. Disabled surfaces are hidden and inactive.

Low/medium/high use 16/32/64 squared wave grids and 2/3/4 cubed box integration
samples. Fixed-step damped waves use CFL-limited substeps. Spheres use clipped
spherical-cap volume and centroid; boxes approximate submerged volume in oriented
cells. Buoyancy acts at submerged points, producing torque, with implicit point
drag relative to the water. Compound parts share their owner's rigid body.
Mass is in kg and density in kg/m³: matching object and water density matters.
Wet bodies stay awake while fluid forces are integrated.

Water attenuates the background separately by RGB channel, then adds shadowed
sun/point/spot lighting, environment reflections and scattering. Fresnel reflection
uses IOR; absorption depends on optical depth and view angle. This is a bounded
real-time approximation: depth is authored, the level stays fixed, and transmitted
scene geometry is not refracted. No underwater camera treatment or swimming yet.

Run `run_water-pool_editor` before `run_water-lake_editor`; `_player` variants also
exist. The lake adds twelve mixed-density bodies and a two-pontoon compound raft.
Right-click captures the fly camera, WASD/QE moves, Space makes ripples, R reloads,
N changes rooms, F6 pauses. CPU tests check all qualities, floating/sinking,
settling, wave bounds, ownership/history and both authored scenes. Vulkan pixel
tests verify light color/intensity and wavelength-dependent absorption.

The second stage adds rivers with two to eight centerline points, slope, width
and current speed. Ripples advect along the strip, and open ends damp outgoing
waves. Buoyancy queries use the nearest segment's height and tangent, including
vertical flow on slopes. The current is configured, without a global riverbed
pressure/erosion solve. Author gently bending paths with distinct points; the
validator rejects short segments, slopes over 1:2 and turns over 60 degrees.
Scene format 18 stores the path/current; format 17 pools still load. C ABI is 17.

Run `run_water-stream_editor` before `run_water-river-bend_editor`. The N key
switches within the pool or river demo pair. River tests check slope, banks and
ends, downstream drift at every quality, bends, persistence, and both full scenes.

The third stage adds bounded CPU position-based fluid with Poly6 density, Spiky
gradients, non-negative pressure, anti-clustering and XSPH viscosity. Low/medium/
high cap each source at 256/1024/4096 particles with 3/5/7 density iterations per
substep. Spatial hashing retains exact cell coordinates. Initial volume and an
optional emitter create equal-volume particles; source capacity stops emission.
Particles beyond the 50 m horizontal / −30 m vertical simulation bounds are
removed and counted as escaped volume. Emitted, active and escaped volume remain
accounted for. Source changes to spacing/initial volume/quality reset the runtime;
changing optical settings, viscosity or emission rate preserves it.

Particles sweep and project against the actual primitive, mesh and compound
shapes. Their bounded contact impulses affect dynamic containers. This coupling
is experimental; it approximates fluid loading without a fully coupled solid/
fluid pressure solve. It does not apply pool Archimedes forces a second time.
Wet bodies stay awake, so dense fluid scenes still cost CPU time when settled.
Low renders faceted drops; medium/high reconstruct a smooth bounded isosurface
using marching tetrahedra. Widely dispersed or over-budget surfaces fall back to
drops. Surface reconstruction is capped at 48/64 nodes per axis and 180k vertices.
This implementation rebuilds/uploads the generated mesh and waits for GPU safety;
it is a functional prototype, not a large-volume optimized fluid renderer.

Use `run_water-pour_editor` before `run_water-pour-lab_editor`. The second has a
kinematic tipping cup, receiving cup, ramp and continuing emitter. F tips/returns
the source cup; R resets its contents and N changes the pouring pair. Initial
source water is shown in Edit; Play owns the moving particles. Scene format 19
and C ABI 18 include particle settings and copied particle/stat queries. The
renderer shares pool lighting/absorption and uses particle scale for optical depth.

Tests retain 100 particles in an upright compound cup at every quality, then
spill them after tilting, account for escaped fluid, check source/capacity limits,
surface mesh bounds and cleanup, and verify particle-surface lighting. Six native
C Play demos exercise simple-first ordering, pause/single-step, tipping, generated
mesh cleanup and Edit isolation. Export tests remove sources and relocate all labs.

The surface-wave/lighting work is informed by [GPU Gems' water chapter](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models).
The particle solver follows the density-projection approach in
[Macklin and Müller, Position Based Fluids](https://matthias-research.github.io/pages/publications/pbf_sig_preprint.pdf),
with bounded CPU budgets and simplified boundary/coupling treatment.

Validation covers 135 macOS Debug/sanitizer checks, including targeted reruns
after the final source-bound and emitter-stop fixes, and 91 Linux CPU checks with
address/undefined-behavior/leak sanitizers. All six Release labs export, rebuild
their fluid surfaces and run after source removal and relocation. Static analysis
of the new fluid implementation and C lab reports no findings. Distribution
source archives and MPL/third-party notice checks are refreshed with final sources.
The emitter-stop regression is also verified to fail against the old behavior.
