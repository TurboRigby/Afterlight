# 0.19 water boundaries, ripple texture and wind

Pools and rivers now have closed render boundaries: the animated top, vertical
sides and a bottom at the authored depth. Outside views draw entry faces; inside
views draw exit faces. This prevents double attenuation while making the water
visible from its sides and below. River cross-sections use continuous miter joins
instead of folding at abrupt changes of segment direction. Physics still uses
the existing bounded water regions; the river's miter corners are a visual
approximation of those regions.

Both optical passes apply the same positive depth bias so a solid wall/floor
wins at coincident boundaries. This prevents alternating water/solid triangles
(z-fighting) when a container touches the liquid sides or bottom.

Water detail is an original procedural normal texture in
`engine/shaders/water_ripples.glsl`. Three warped travelling-wave layers change
reflection, scattering and refraction rather than painting an opaque pattern on
the surface. Frequencies fade with pixel footprint to reduce distant shimmer.
Water controls expose **Ripple size (m)** and **Ripple detail**; zero detail
disables it. Particle fluids retain their reconstructed surface normals. The
pattern follows simulation time, freezes on pause and resets on a new scene.

The Inspector's global **Wind** section exposes enable, direction in degrees,
speed in m/s, gust strength, frequency in Hz and air density. Direction points
downwind in world XZ: 0 degrees is +X, 90 is +Z. Gusts are deterministic; disabling
wind preserves older scene behavior. The existing water scalar is labelled
**Local wave forcing**, independent of global wind. World wind drives physical
height-field waves and the ripple normal texture. Both playable water courses
start with a gentle gusting breeze and procedural environment lighting.

Exposed dynamic collider parts receive projected-area drag relative to air
velocity. Mass and angular effective mass determine response; partial submersion
reduces exposed area and moves the force toward the exposed portion. Fully
submerged parts are excluded. An upstream center ray checks shelter out to
20 metres, and significant wind wakes sleeping islands. This is a bounded
aerodynamic approximation, without turbulent flow, pressure fields, foliage,
cloth or wind-driven particle spray. Compound areas can overlap and shelter is
coarse, so the result is intended for game props rather than engineering CFD.

Scene format **22** saves a root `wind` object and water `ripple_scale` /
`ripple_strength`. Versions 1–21 still load, with world wind disabled and default
ripple controls. The C module ABI is **21**: rebuild modules and restart older
editors/players. The host provides `wind_default`, `get_wind`, `set_wind` and
`wind_velocity`; the latter samples settings at a simulation time in seconds.

`water_volume` checks oriented, watertight boundaries at all three qualities and
sloped river bends. `water_render` checks actual pixels from the side, below and
inside, disabling water, ripple detail, pause and wind animation. `wind` covers
validation, history, persistence, legacy defaults, deterministic gusts, mass,
30/60/144 Hz stepping, reversal, shelter, submersion, partial-exposure torque,
sleep/wake and physical wave generation. Existing water/course, underwater,
refraction and export checks remain applicable.

`water_contact_render` compares 64 wall/floor pixels with water on/off, with
refraction both enabled and disabled. It reproduces the contact artifact before
the depth fix and verifies that opaque contact surfaces remain stable afterward.

Release benchmarks remain available as `afterlight_water_benchmark` and
`afterlight_water_render_benchmark`; see the [profiling guide](development-018.md).
The 0.19 18-case render run reports zero mesh-wide upload waits. Raw measurements
are in build/release/019-render.csv and 019-water-quiet.csv; presentation pacing
and machine load still prevent interpreting these as uncapped FPS claims.

The **Environment** toolbar panel groups sun direction/color/intensity, ambient
light, sky source/brightness/rotation, environment lighting, exposure and tone
mapping. Day/Night presets are atomic history edits. In Play, changes affect
only the runtime scene; in Edit they persist on Save. No object selection is
needed. Native tests compare sky/object brightness and verify undo and persistence.
