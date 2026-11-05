# Assets and materials

Use **Import mesh (OBJ/glTF)** to load a model and create a selected object. The
editor stores a named asset reference in the scene. The inspector's Mesh asset
field can assign that asset to other objects; GPU mesh memory is shared.

The OBJ loader accepts positions, UVs, normals, triangles and convex polygon
faces. Positive and negative indices and `v`, `v/vt`, `v//vn`, `v/vt/vn` face
corners work. Convex polygons are fan-triangulated. Missing normals are generated
per triangle; explicit normals are normalized. UV V is flipped for image origin
conventions. Imported vertex colors default to white. Object/group/material/MTL
statements do not create submeshes or materials. Concave polygons should be
triangulated before export. FBX and skeletal animation are not supported.

Scroll down in the inspector to **Material**. Edit linear base RGB, choose Unlit,
or use **Import texture** to load and assign a PNG, JPEG, BMP or TGA image. The
Texture asset field can reuse an existing texture name; clear it for white.
Materials multiply texture, vertex color and base color. The renderer is opaque;
transparent materials are future work. Color images decode to RGBA and upload
as sRGB. Scene textures have a full sRGB-aware mip chain, trilinear repeat sampling
and up to 8x anisotropy, limited by device support. The UI atlas stays single-level.
Normal-map imports upload linear RGB, use their own material reference/strength
and perturb normals using the UV derivative tangent frame. PBR materials expose
roughness/metallic values; emission adds light-independent color.
See [rendering](rendering.md) for model details and limits.
The sample includes an OBJ pyramid and a checker image to exercise both paths.

**Directional light** controls appear below the object properties (and remain
available without a selection). Edit direction, ambient and intensity. The
scene stores linear light color too, accessible through the C API. Lighting is
ambient plus Lambert diffuse. Up to 16 point/spot lights add range/cone-attenuated illumination.
Normals use the inverse-transpose model transform
for nonuniform scale. Vertices without normals use a fragment geometric normal.
The editor's selection tint is applied separately from persisted material data.

Material **Cast shadows** and **Receive shadows** default to enabled. Unlit
materials ignore lighting/shadow reception and can still cast. Hidden objects
and descendants of hidden parents do not cast. The **Shadows** section below
the directional light controls one map or four stabilized cascades, filtered
edges, distance and bias. See [shadows](shadows.md).

## Static glTF and GLB

The editor imports glTF 2.0/GLB hierarchy, per-primitive materials, base color,
normal and packed metallic/roughness textures, including embedded images. CPU
`af_model_file_load` and transactional `af_scene_import_model` provide this path.
`af_mesh_file_load_gltf` remains a flattened geometry API for older callers.
[Rich import, source selectors, limits and reloading](shipping.md) describes both.
The decoder is vendored [cgltf](https://github.com/jkuhlmann/cgltf), compiled in C.

Base-color images use sRGB; normal and metallic/roughness maps are linear. The
packed surface texture's G/B channels multiply the material roughness/metallic
factors, following [the glTF material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material).
The Inspector accepts `metallic_roughness_map`; the Assets panel imports/assigns
these maps. Existing files get an empty reference, meaning constant factors.

The Assets panel's **Reload changed assets** follows source model files and their
external images/buffers. A rejected rebuild keeps previous GPU resources; a
later valid edit recovers. Changes to hierarchy/material definitions require
reimport so document edits are preserved. See [asset reload](shipping.md).

## WAV audio

The Assets browser imports WAV files and previews selected audio. Scene manifest
kind `audio` names a clip; game modules call `host->play_sound(engine,name,gain)`.
The course uses `jump`, `land`, `checkpoint` and `finish`. Original synthesized
WAVs are bundled, so gameplay has no external sound dependency. Loading decodes
without requiring an output device. Playback opens the default SDL device lazily;
a missing device does not prevent the game from running.

Different clips mix. Replaying a clip restarts its existing stream. Gain is 0–1.
Stop, pause and focus loss stop sounds. This is non-positional one-shot audio,
without streaming music, looping, buses or spatial attenuation. Limits are 32
cached clips, 64 MiB decoded cache, 8 MiB per input WAV and 30 seconds per clip,
1–8 channels at 8–192 kHz. `af_audio_*` provides the independent main-thread audio
API; engine wrappers expose loading, playback, stop and statistics.

## C API

- `af_mesh_file_load` (dispatch), `af_mesh_file_load_obj`, `af_mesh_file_load_gltf` and `af_image_file_load` are CPU-only. Free successful
  outputs with `af_mesh_file_free` / `af_image_file_free` after uploading.
- `af_engine_load_mesh` / `af_engine_load_texture` / `af_engine_load_normal_map` load a file and upload it under
  a name. Failed uploads retain existing assets with that name.
- `af_engine_upload_mesh` / `af_engine_upload_texture` copy caller data into GPU
  ownership. Replacing or removing assets waits for outstanding GPU work.
- `af_scene_set_asset` records a mesh/texture/normal-map/audio path. `af_engine_load_scene_assets`
  loads the manifest, including relative paths already resolved by scene load.
- `af_scene_set_object_material`, `af_scene_set_light`, `af_scene_set_shadows` and hierarchy/transform
  setters edit CPU data without re-uploading geometry or textures.
- Stats expose loaded meshes/textures, missing meshes/textures, object draws and shadow draws/map count/resolution.

Asset files are capped at 64 MiB. OBJ arrays/output are capped at 1,048,576
vertices; each face may have at most 1,024 corners. Decoded images are at most
8192 by 8192, also subject to device allocation limits. Import errors leave the
scene document intact and appear in the status bar. The current registry is a
small named cache, not a project asset database. Paths are references to source
files, not copied packages; see the scene format's relocation rules.

The image decoder is vendored stb_image 2.30, compiled in C. Source provenance,
checksums and MIT license are in `third_party/stb/`.

The toolbar Assets browser filters manifest entries by kind/name/path, assigns
references and shows texture diagnostics via `af_engine_texture_info`. It includes
the built-in cube and can create objects from mesh assets. Texture GPU allocation
stats exclude shadow arrays, fallback images and the UI atlas.
