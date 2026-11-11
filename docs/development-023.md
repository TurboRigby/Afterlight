# 0.23.0: Animation authoring, streamed geometry and story tools

Afterlight 0.23.0 adds native skeletal animation authoring, clustered static
geometry with LOD and asynchronous streaming, and story authoring in the editor.
Low Water provides a standalone chapter, and Signal Annex exercises authored
scenes, dialogue, timelines, sound and saved story flags.

## Skeletal animation authoring

Select an imported rigged model and choose **Author skeletal clips**. The model
can contain a skin without any imported animation clips. Select bones in the
hierarchy or viewport, pose them with gizmos or local transform fields, and use
**Record pose** at the playhead. The workspace supports new clips, key deletion,
linear or step interpolation, scrubbing, playback and undo/redo.

**Save clips** writes a native `.afanim` asset and binds it to the selected model.
Save the scene to persist that binding. The native asset contains the rig,
inverse bind matrices, clips, morph channels and events; gameplay playback and
export use the same asset. Pose previews belong to the selected instance.

The editor authors bone transform keys on an existing rig. Creating skeletons,
skinning meshes, retargeting and IK are outside this workspace. Imported morph
channels and events are preserved; the pose tools edit translation, rotation and
scale. See [the animation workflow](editor.md#skeletal-animation-authoring).

## Clustered geometry and streaming

The offline cooker divides static meshes into clusters of up to 128 triangles.
The renderer culls clusters with GPU-generated indexed indirect draws when
supported, with CPU culling available as a fallback. Region LOD preserves boundary
vertices and chooses detail from projected geometric error.

```sh
cmake --preset debug
cmake --build --preset debug --target afterlight_cluster_cook afterlight_cluster_demo
build/debug/afterlight_cluster_cook --stream input.glb output.afpages
build/debug/afterlight_cluster_demo output.afpages
```

Streaming keeps a resident coarse mesh and loads fine pages through a bounded,
prioritized request queue. All streamed assets share an 8 MiB default budget for
fine-page vertex and index allocations. `af_engine_set_cluster_cache_budget`
changes the byte limit; zero selects coarse geometry. Eviction waits for the
relevant frame fences, and unavailable detail falls back to the coarse mesh.

Directional and local shadow maps, plus hardware ray queries, use resident fine
pages for the visible regions selected by the opaque pass. Off-camera regions
retain coarse geometry for shadows and reflections. The current limits are eight
streamed assets and 128 resident fine pages per asset. This path targets static
geometry; it does not stream skinned meshes or provide a general texture streamer.
See [clustered geometry](rendering.md#clustered-geometry) for the demo controls,
allocation policy and ray-instance limits.

## Story tools and Low Water

Interactable Blueprints support prompts, dialogue choices, persistent story flags
and scene transitions. Timeline nodes animate object transforms and directional
light intensity and cue WAV sounds. Looping spatial emitters support fades. The
Blueprint debugger shows execution activity, recent entries and scene-change
failures. See [story authoring](editor.md#story-interactions-and-dialogue).

```sh
cmake --build --preset debug --target run_low_water
cmake --build --preset debug --target run_low_water_annex_editor
```

The original Low Water chapter defines its level in C. Signal Annex uses the
generated project at `build/debug/games/low-water/signal-annex.afterlight.project`
and the scenes in `games/low-water/annex`. The editor launch target builds its
gameplay module as well as the editor. See [Low Water](../games/low-water/README.md).

## Compatibility and version reporting

The engine release is **0.23.0**, the scene format is **30**, the C game module ABI
is **30**, and the engine extension ABI is **1**. These numbers serve different
purposes. Scene versions 1–29 remain readable; current saves use version 30.
Rebuild game modules against the current public headers and restart older
editor/player processes before loading them.

`engine/include/afterlight/version.h` supplies the release components and
`AF_VERSION_STRING`. CMake reads those components for package/bundle metadata;
the Vulkan application/engine metadata, sandbox banner and NGX adapter use the
same header. Changing a release version does not change scene or ABI versions.

The source tree now groups engine, editor and test code by subsystem. Projects
that directly reference private source paths need to use the new locations;
public includes remain under `afterlight/`.

## Verification

The release preparation passed the local full build and all 108 non-graphics
tests. The legacy wind/render-data checks also passed in Debug, Release and
ASan/UBSan configurations on macOS.

The Arch Linux installation passed seven focused checks: wind, render data,
native animation authoring, animation tools, the skeletal authoring UI, editor
smoke and editor project playback. Its installed launcher rendered 90 frames on
an NVIDIA RTX 5060 with DLSS active and zero Vulkan validation errors. Those
checks do not establish coverage for every optional ray-traced effect or for
Windows. See [testing](testing.md) for the wider test matrix.
