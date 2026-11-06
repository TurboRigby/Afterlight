# Afterlight scene format, version 10

Scene files are UTF-8 JSON, conventionally named `*.afterlight.json`. They store
CPU scene data and references to files. Loading a scene does not start Vulkan,
load asset bytes or execute game code. `af_engine_load_scene_assets` loads the
manifest into the renderer separately. Game library selection stays outside the
scene file.

```json
{
  "format": "afterlight.scene",
  "version": 10,
  "next_id": "3",
  "assets": [
    {
      "name": "model",
      "kind": "mesh",
      "path": "assets/model.obj"
    },
    {
      "name": "checker",
      "kind": "texture",
      "path": "assets/checker.png"
    }
  ],
  "light": {
    "direction": [
      -0.6,
      -1,
      -0.4
    ],
    "color": [
      1,
      0.95,
      0.86
    ],
    "intensity": 1.2,
    "ambient": 0.3
  },
  "shadows": {
    "enabled": true,
    "mode": "cascaded",
    "resolution": 2048,
    "distance": 40,
    "constant_bias": 1.25,
    "slope_bias": 1.75,
    "normal_bias": 0.015,
    "filter_radius": 1,
    "split_lambda": 0.65,
    "blend_fraction": 0.1,
    "debug_cascades": false
  },
  "objects": [
    {
      "id": "1",
      "parent": "0",
      "name": "Parent",
      "mesh": "cube",
      "visible": true,
      "position": [
        0,
        0,
        0
      ],
      "rotation": [
        0,
        0,
        0
      ],
      "scale": [
        1,
        1,
        1
      ],
      "material": {
        "base_color": [
          1,
          1,
          1,
          1
        ],
        "texture": "",
        "unlit": false,
        "cast_shadows": true,
        "receive_shadows": true,
        "physically_based": false,
        "roughness": 0.6,
        "metallic": 0,
        "emission": [
          0,
          0,
          0
        ],
        "emission_strength": 0,
        "normal_map": "",
        "normal_strength": 1,
        "metallic_roughness_map": ""
      },
      "collider": {
        "type": "none",
        "enabled": true,
        "trigger": false,
        "center": [
          0,
          0,
          0
        ],
        "half_extents": [
          0.5,
          0.5,
          0.5
        ],
        "radius": 0.5,
        "half_height": 0.5,
        "layer": 1
      },
      "gameplay": {
        "role": "none",
        "checkpoint_order": 0,
        "player": {
          "enabled": false,
          "spawn": "0",
          "radius": 0.35,
          "half_height": 0.55,
          "speed": 4.5,
          "jump_speed": 6.5,
          "gravity": 18,
          "slope_limit": 0.7853981633974483,
          "skin": 0.002,
          "ground_snap": 0.15,
          "mouse_sensitivity": 0.0025
        },
        "camera": {
          "enabled": false,
          "primary": false,
          "eye_height": 0.75,
          "vertical_fov": 1.0471975511965976,
          "near_clip": 0.08,
          "far_clip": 160
        }
      },
      "animation": {
        "asset": "",
        "clip": 0,
        "speed": 1,
        "autoplay": false,
        "loop": true
      },
      "model": {
        "root": "0",
        "node": 0,
        "skin": -1,
        "primitive": false
      }
    },
    {
      "id": "2",
      "parent": "1",
      "name": "Child",
      "mesh": "model",
      "visible": true,
      "position": [
        2,
        0,
        0
      ],
      "rotation": [
        0,
        0,
        0
      ],
      "scale": [
        1,
        1,
        1
      ],
      "material": {
        "base_color": [
          1,
          1,
          1,
          1
        ],
        "texture": "checker",
        "unlit": false,
        "cast_shadows": true,
        "receive_shadows": true,
        "physically_based": false,
        "roughness": 0.6,
        "metallic": 0,
        "emission": [
          0,
          0,
          0
        ],
        "emission_strength": 0,
        "normal_map": "",
        "normal_strength": 1,
        "metallic_roughness_map": ""
      },
      "collider": {
        "type": "none",
        "enabled": true,
        "trigger": false,
        "center": [
          0,
          0,
          0
        ],
        "half_extents": [
          0.5,
          0.5,
          0.5
        ],
        "radius": 0.5,
        "half_height": 0.5,
        "layer": 1
      },
      "gameplay": {
        "role": "none",
        "checkpoint_order": 0,
        "player": {
          "enabled": false,
          "spawn": "0",
          "radius": 0.35,
          "half_height": 0.55,
          "speed": 4.5,
          "jump_speed": 6.5,
          "gravity": 18,
          "slope_limit": 0.7853981633974483,
          "skin": 0.002,
          "ground_snap": 0.15,
          "mouse_sensitivity": 0.0025
        },
        "camera": {
          "enabled": false,
          "primary": false,
          "eye_height": 0.75,
          "vertical_fov": 1.0471975511965976,
          "near_clip": 0.08,
          "far_clip": 160
        }
      },
      "animation": {
        "asset": "",
        "clip": 0,
        "speed": 1,
        "autoplay": false,
        "loop": true
      },
      "model": {
        "root": "0",
        "node": 0,
        "skin": -1,
        "primitive": false
      }
    }
  ],
  "render": {
    "exposure": 0,
    "tone_mapping": false,
    "bloom": false,
    "bloom_strength": 0.12,
    "bloom_threshold": 1,
    "bloom_radius": 1.5,
    "environment_enabled": false,
    "environment_profile": 0,
    "environment_intensity": 1,
    "environment_rotation": 0,
    "local_shadow_resolution": 512,
    "environment_asset": "",
    "skybox": false,
    "skybox_intensity": 1
  },
  "local_lights": [
    {
      "name": "Lamp",
      "type": "spot",
      "enabled": true,
      "position": [
        0,
        3,
        0
      ],
      "color": [
        1,
        1,
        1
      ],
      "intensity": 10,
      "range": 10,
      "direction": [
        0,
        -1,
        0
      ],
      "inner_angle": 0.2617993877991494,
      "outer_angle": 0.5235987755982988,
      "cast_shadows": false,
      "shadow_bias": 0.001
    }
  ]
}
```

All shown fields are required in version 10. Names are case sensitive. Unknown or
duplicate fields, unsupported versions, trailing data, embedded NUL and malformed
UTF-8 are rejected. Version 1 remains readable with its original strict schema:
no `assets`, `light`, `material` or `parent` fields. It receives an empty manifest,
a default directional light, white lit materials and root-level objects. Version 2
retains its strict schema: it includes assets/light/material/parent, but omits
`shadows` and both material shadow flags. Versions 1 and 2 receive default
four-cascade settings and true cast/receive flags. Version 3 retains its strict shadow settings and cast/receive fields, but omits
new surface fields, `render` and `local_lights`. Versions 1–3 use Lambert
materials with roughness 0.6, metallic/emission 0, no normal map and strength 1;
exposure is 0, tone mapping is off, and local lights are empty. Saving always
writes version 10. Version 4 retains material/render/local-light fields but omits
colliders; versions 1–4 get an enabled collider of type `none`.

IDs, parent IDs and `next_id` are canonical unsigned decimal **strings** so all
64 bits survive JSON tools. Object IDs are nonzero and unique. `parent: "0"`
means scene root; all other parents must exist. Missing parents, cycles, and
chains of 128 or more parent edges are rejected. Object order is preserved.
Display names may repeat.

A nonzero next ID must exceed every object ID and preserves deleted-ID history.
`next_id: "0"` means permanent ID exhaustion. IDs are not reused after deletion.

Names and mesh references are nonempty UTF-8 strings of at most 63 bytes. Texture
references allow the empty string, meaning white. Asset names share the 63-byte
limit and must be unique across kinds. Paths are nonempty, up to 4095 bytes.
Kinds are `mesh` (OBJ/glTF/GLB), `texture` (sRGB PNG/JPEG/BMP/TGA), `audio` (WAV) and `normal_map`
(linear PNG/JPEG/BMP/TGA; versions 4–7). Relative paths resolve
against the scene file's directory, independently of the process working directory.
The loader resolves them to absolute paths in memory. Saving uses a relative path
when the asset path begins with the destination directory; other references stay
absolute. Save As preserves resolved references. Files are referenced, not copied
or packed; move external assets deliberately when moving a project.

Transforms use parent-local position, Euler rotation and scale. Each vector has
three finite 32-bit floats. Coordinates are right handed, Y up; rotations are
radians and compose as `T * Rz * Ry * Rx * S`. World matrices multiply ancestor
transforms before the child's local matrix. Negative and zero scales are allowed;
singular transforms cannot be picked. Visibility is a boolean and inherits from
ancestors. Deleting an object deletes its subtree. Reparenting keeps local TRS,
so world position may change.

Version 5 objects additionally require `collider`, with exactly eight fields:
`type`, `enabled`, `trigger`, `center`, `half_extents`, `radius`, `half_height`
and `layer`. Type is `none`, `box`, `sphere` or `capsule`. Enabled/trigger are
booleans; center is a finite local 3-vector. Half extents/radius/half-height are
finite, nonnegative and at most 10000. Boxes require strictly positive half
extents; spheres/capsules require positive radius. Capsule half-height is the
straight segment half-length. Layer is an integer [1,4294967295], stored as a
JSON number (all uint32 values are exact). Hidden meshes may still collide.
See [collision transforms, queries and limitations](gameplay.md).

Material base color is four finite linear RGBA components in [0,1]. The surface
is opaque even when alpha is less than one. Texture color is decoded from sRGB,
multiplied by vertex color and base color, and lit unless `unlit` is true. Missing
textures fall back to white; missing meshes are skipped and counted in stats.
`cast_shadows` and `receive_shadows` are booleans. Hidden objects, including
children of hidden parents, do not cast shadows. Unlit materials bypass lighting
and shadow reception but may still cast. No imported MTL or embedded mesh/image
data is stored in the scene.

The directional light travels along its nonzero direction vector. Color is
nonnegative finite linear RGB, intensity is [0,100], and ambient is [0,10].
The legacy path uses ambient plus Lambert diffuse. The optional PBR path uses
metallic/roughness microfacet shading. Shadows attenuate only the directional
component; point/spot lights are unshadowed.

Versions 4 and 5 materials have exactly the twelve fields shown. `physically_based` is
a boolean; roughness/metallic are finite [0,1], emission is linear RGB [0,1],
emission strength [0,1000] and normal strength [0,4]. `normal_map` is an optional
asset-name reference with the same capacity as `texture`; empty means flat.

`render` has exactly `exposure` (finite stops [-10,10]) and `tone_mapping`
(boolean). Exposure multiplies scene geometry by 2^exposure; optional Reinhard
mapping applies before swapchain output. UI and background are unchanged.

`local_lights` contains at most 16 entries with exactly the ten fields shown.
Name is nonempty UTF-8 up to 63 bytes; type is `point` or `spot`, enabled is boolean.
Position/direction are finite 3-vectors in world space, color is linear RGB
[0,100], intensity [0,10000] and range [0.01,10000]. Spot direction must be
nonzero. Inner/outer half-cone angles are radians, 0 <= inner <= outer <= 89°.
Point entries still serialize direction/cone fields. Array order is preserved;
there are no separate local-light IDs or transform parents. Removing an entry
changes later indices. See [rendering](rendering.md).

`shadows` has exactly the fields shown above. `mode` is `single` (one map) or
`cascaded` (four maps). Resolution is 512, 1024, 2048 or 4096 per map. Distance
is finite in [1,10000]; constant/slope bias are [0,16]; normal bias is [0,1].
Filter radius is integer 0, 1 or 2 (hard, 3x3 PCF or 5x5 PCF). Split lambda is
[0,1], blend fraction is [0,0.3]. Enabled/debug flags are booleans. Defaults
match the example. See [shadow behavior and controls](shadows.md).

Files are limited to 16 MiB, 65,536 objects, 1,024 assets and 32 levels of JSON
nesting. Floats round-trip through the engine's float representation. Formatting
may change when saving.

## Ownership and file replacement

`af_scene_load(path, &error)` returns a new owned scene or NULL; failure never
changes an existing scene. `af_scene_clone` copies objects, IDs/counters, assets,
directional/local lights, render/shadow settings and hierarchy without GPU resources. Detach an engine's borrowed scene
before destroying it, or replace it with another live scene first.

`af_scene_save` builds the JSON before opening an exclusive sibling temporary
file. It replaces the destination only after the complete write/close succeeds.
Failure preserves the previous destination and cleans up owned temporary files.
The parent directory must exist. This avoids partial replacements without
promising power-loss durability. Windows file replacement is implemented but
only macOS has been exercised. The parser is vendored MIT-licensed cJSON 1.7.19.

## Gameplay components (version 6)

Every object stores a `gameplay` object with `role`, `checkpoint_order`, `player`
and `camera`. The example above shows disabled defaults. Roles are `none`,
`spawn`, `checkpoint`, `goal`, `restart` and `platform`. Checkpoint order is an
integer in 0–1024; the course requires unique consecutive orders starting at 1.

Player fields include `enabled`, `spawn` (an ID string, zero uses the player's
position), capsule `radius`/`half_height`, `speed`, `jump_speed`, `gravity`,
`slope_limit` (radians), `skin`, `ground_snap` and `mouse_sensitivity` (radians per
mouse unit). Camera fields include `enabled`, `primary`, `eye_height`,
`vertical_fov` (radians), `near_clip` and `far_clip`. See `gameplay.h` for types and
validation. The editor's Jump height converts to/from jump speed using gravity.

The playground requires one enabled player at the root with unit scale, a
matching enabled solid capsule centered at zero, and a primary camera. Spawn
references select a Spawn component. Display names have no runtime meaning.
The role trigger's world-space center is the checkpoint respawn capsule center.

Version 5 remains readable with collider data. Versions 1–5 receive disabled
component defaults, with a one-time compatibility import for the old course's
Player/Spawn/Moving platform/Goal trigger/Restart trigger names. Saving upgrades
to explicit components; renaming objects then has no gameplay effect. Old versions
do not accept audio assets. Cloning, subtree duplication and undo preserve
components; duplication remaps spawn IDs within the copied subtree.

## Packed surface maps and model references (version 7)

Materials require `metallic_roughness_map`, an asset-name string (empty means
constant material factors). Manifest kind `metallic_roughness` uses a linear
image with roughness in G and metallic in B. Versions 1–6 remain readable with an
empty reference. Existing material fields and gameplay components are unchanged.

Model imports preserve hierarchy as regular objects. `mesh: "__node"` is reserved
for transform-only nodes and does not draw. Primitive objects reference named
mesh assets with a source selector, e.g. `model.glb#mesh=0,primitive=1`; images use
`model.glb#image=2`. All text before the selector resolves relative to the scene.
The exporter copies/rewrites glTF dependencies and keeps selectors. GPU asset
watching does not overwrite saved transform/material edits. See [shipping](shipping.md).


## HDR, environment, bloom and local shadows (version 8)

`render` additionally requires `bloom` and `environment_enabled` booleans,
`bloom_strength` [0,5], `bloom_threshold` [0,100], `bloom_radius` [0,4],
`environment_profile` (integer 0 studio / 1 sunset / 2 overcast),
`environment_intensity` [0,100], `environment_rotation` in radians [-1000,1000],
and `local_shadow_resolution` (256 / 512 / 1024). Each local light additionally
requires `cast_shadows` and `shadow_bias` [0,0.1]. All numbers must be finite.

Versions 1–7 retain their strict schemas; their missing new fields receive
safe defaults, with environment/bloom/local shadows disabled. Saving always
writes version 10. These settings survive clone, undo/redo, Play and project
export. See [lighting controls and limits](lighting-08.md).


## Imported environments and skyboxes (version 9)

The asset kind `environment` references a linear Radiance RGBE `.hdr` panorama.
`render` additionally requires `environment_asset` (empty or a named environment),
`skybox` (boolean) and `skybox_intensity` (finite [0,100]). Intensity for lighting
remains `environment_intensity`; rotation is shared. Environment names are UTF-8
and fit `AF_MESH_NAME_CAPACITY` including NUL. Relative HDR paths use the same
asset resolution/export rules as other source files. Versions 1–8 default to a
hidden skybox and a procedural environment; saving upgrades to v10. See
[HDR limits, resource ownership and editor workflow](assets-09.md).


## Animation instances (version 10)

Every object additionally has `animation` with `asset` (empty or named animation
resource), `clip` [0,127], finite `speed` [-100,100], `autoplay` and `loop` booleans.
`model` has `root` as an ID string, `node` [0,16383], `skin` [-1,127] and `primitive`
boolean. Unbound defaults are root `"0"`, node 0, skin -1 and primitive false.
A bound object must descend from its root; non-primitive source-node bindings
must be unique per root. Root configuration and node bindings survive undo,
clone, complete-model duplication and export. Asset kind `animation` references
its glTF/GLB source and shares the usual relative-path/dependency packaging rules.
Versions 1–9 receive empty configurations and unbound defaults. Current saves
write v10. Playback time, fade buffers and posed matrices are transient runtime
state; collision remains based on authored transforms.

## Versions 11 and 12: rigs and alpha

Version 11 adds a `rig` object to every scene object, with `enabled`,
`root_motion`, source `root_node`, five `clips` indices in Idle/Walk/Run/Jump/Fall
order, model-space `walk_speed`/`run_speed`, `camera_distance`, `camera_height`
and `turn_speed`. Version 12 adds material `alpha_mode` (opaque/mask/blend) and
`alpha_cutoff` in [0,1]. Earlier versions read with a disabled rig and opaque
material defaults. Version 12 introduced these fields. See [0.12](development-012.md).

## Version 13: rigid bodies

Each object adds `body`, with `enabled`, `type` (static/kinematic/dynamic),
`mass` [.001,10000], `friction` [0,2], `restitution` [0,1], `linear_damping` and
`angular_damping` [0,20], `gravity_scale` [-10,10], `allow_sleep`, `continuous`,
and three-component `velocity`/`angular_velocity`. Magnitudes are limited to
200/100. Dynamic bodies require a scene-root sphere/box collider, centered origin,
positive scale and a nontrigger enabled collider. Versions 1–12 load disabled
body defaults. Saving writes version 16. Physics runtime limits and diagnostics
are documented in [0.13](development-013.md).

## Version 14: mesh colliders and scene joints

`collider.type` additionally accepts `mesh`, using the object's mesh asset for
static triangle collision. Its center must be zero and any enabled body must be
static. Imported OBJ/glTF triangles are cooked into an immutable BVH cache; the
file stores references, not cooked data. Invertible, nonsheared transforms
including mirrored/nonuniform scale are supported. Dynamic bodies remain boxes
or spheres.

Version 14 requires two additional root fields: `next_joint` (a decimal string
ID counter, zero when exhausted) and `joints` (up to 128 entries). Each joint has
exactly these fields:

- `id`, `a`, `b`: decimal string IDs. `a` is required; `b: "0"` is the world.
- `name`, `type` (`distance` or `hinge`), `enabled`, `collide_connected`.
- `anchor_a`, `anchor_b`: three-element vectors in unscaled body axes about the
  collider center. A world endpoint uses world coordinates.
- `distance`: nonnegative rest length (zero constrains a point).
- `axis_a`, `axis_b`, `reference_a`, `reference_b`: unit vectors, with each
  reference perpendicular to its axis.
- `limits_enabled`, `lower_limit`, `upper_limit`: hinge twist bounds in radians
  within [-pi, pi], measured from A's reference toward B's reference.

IDs are unique and below the nonzero next counter. At least one endpoint must
be dynamic; endpoints need enabled, nontrigger sphere/box/static-mesh colliders.
Body deletion removes attached joints. Versions 1–13 load with no joints.
Prefabs remap internal joint endpoints and reject external/world constraints.

## Version 15: powered hinges

All version-14 scene fields remain. Every joint additionally requires
`motor_enabled` (boolean), `motor_speed` (finite relative B-A angular velocity,
-100..100 rad/s) and `max_motor_torque` (finite 0..1000000 Nm). Enabled motors
require hinge joints. The accumulated impulse budget is torque multiplied by
substep duration. A zero speed target is a brake. Versions 1–14 remain readable;
version-14 joints default to motors disabled, speed zero and torque 10 Nm.
Runtime islands, cached impulses and diagnostics are reconstructed from saved
components. Game ABI 14 modules must be rebuilt with the matching headers.

## Version 16: compound collider parts

`collider.type` additionally accepts `compound`. Two root fields are required:
`collider_parts` (up to 1024 records, 16 per owner) and `next_part` (a decimal
string ID counter; zero means exhausted). Each part has exactly `id`, `owner`,
`name`, `position`, `rotation`, `scale`, `type`, `enabled`, `half_extents`,
`radius` and `density`. IDs/owners are decimal strings. Part type is `box` or
`sphere`; local scale is positive and local Euler angles are radians. Densities
are kg/m3. Root layer/trigger settings apply to all its parts.

Dynamic compounds require root objects, uniform positive world scale and enabled
nontrigger shapes. Volumes, density-weighted COM and full inertia derive from
parts; explicit `body.mass` rescales the mass distribution. Overlaps contribute
additively. Parts do not need separate visual objects. Query hits carry owner
and part IDs; body forces use owners. Prefabs/duplication remap both identities.
Versions 1–15 remain readable with an empty part table. Game ABI is 15.
