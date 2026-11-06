# Afterlight 0.16: compound physics props

Eight milestones cover bounded collider parts, individual-part queries, center of
mass/inertia, shared-body contacts, compound CCD, editor authoring, prefab/runtime
ownership and simple-to-complex prop tests. Water follows this foundation in order:
pools/lakes, rivers, then pourable fluids.

Scene 16 adds stable-ID collider_parts and next_part tables. Each owner can have
16 box/sphere parts; a scene holds up to 1024 parts. Local poses have positive
scale. Part densities define physical mass weights; explicit body mass remains
the total mass used by the solver. Game modules use ABI 15.

Queries flatten compound parts into transformed primitives, returning both owner
and part IDs. Empty space between parts stays empty. Owner filters ignore all
parts together, and the existing capsule controller consumes these queries.

Compound mass properties use scaled box/sphere volumes, density-weighted centers,
rotated inertia tensors and the parallel-axis theorem. They are expressed in
normalized owner axes and physical world units; inertia is about the computed
center. Overlapping parts contribute additively. Tests verify unequal-density
centers, fifth-power inertia scaling and off-diagonal rotated-box terms.

Compound rigid bodies share one linear/angular state across parts. The full
inverse inertia tensor operates in COM axes, while visual transforms retain the
authored pivot. Dynamic compound roots require positive uniform scale; each
physical part dimension is at least .01 units. Contacts retain part-pair identity
for warm starting. Character pushing aggregates contacts and applies its impulse
budget once per owner. Primitive, mesh, hinge, sleep and landing regressions pass.

Compound CCD advances each part from the body's shared pose and velocity. Swept
root bounds include angular reach, then primitive/mesh distances test individual
parts. Impact suppression is per part pair, so another protrusion can contact the
same body during the substep. Work retains the 16-event / 80-sample limits. Tests
cover offset tips, thin walls, empty-gap passages and rotating protrusions.

The editor adds box/sphere parts, fits bounds over rigid mesh descendants, edits
local poses/density, and exposes computed material mass. Move/rotate/scale tools
reuse the snapped viewport gizmo and grouped undo/cancel. Collider overlays draw
individual parts. Dynamic owners stay rooted with uniform positive scale.

Prefab saving keeps only owned parts inside the saved hierarchy. Instantiation
and editor duplication assign new part IDs and remap owners before validating
bodies. Removing an owner removes its parts; removing the last active shape from
a dynamic compound rolls back. C host calls expose part views and mutations.

## Prop demos

`run_prop-room_editor` starts with one imported two-part L prop. Then use
`run_prop-workshop_editor` for six compounds: imported L props, three-part steps
and a hollow five-part cup with falling payloads, alongside powered machinery.
The standalone `_player` targets use the same C module. Q spawns an L prefab;
F/V grab/throw; Z/X brake/reverse mechanisms; R resets; N switches rooms.

Parts are an approximation of imported geometry, without automatic convex
decomposition. Mass uses additive volumes and supports explicit gameplay mass
overrides. Collider shapes and material mass can therefore be tuned separately.
The next feature stages are pools/lakes, rivers and finally pourable fluids,
with buoyancy, quality settings and light-dependent water optics.

Validation: all 124 macOS Debug tests pass after fixing the toolbox export fixture;
all 124 address/undefined-behavior sanitizer tests pass. Both Release compound
exports run after source removal and relocation, with Vulkan validation enabled.
Static analysis of the compound physics and demo sources reports no findings.
Linux sanitizer coverage is scheduled with the following water milestone.
