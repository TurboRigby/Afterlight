# 0.17 water development

The requested sequence is pools/lakes, rivers/streams, then pourable fluid.

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
