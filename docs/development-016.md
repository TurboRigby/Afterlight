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
