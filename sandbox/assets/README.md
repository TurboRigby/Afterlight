# Sample assets

`beacon.glb` and the jump, land, checkpoint and finish WAV cues are original
procedural Afterlight assets. Regenerate them with:

```sh
python3 scripts/generate_sample_assets.py
```

The same script writes the analytic glTF/GLB fixtures under `tests/assets`.
The beacon is a static colored 16-sided stepped tower. Audio is mono 16-bit PCM
at 22050 Hz. No external model or sound download is needed to run the course.

`sphere.obj` is an original smooth sphere for the Lighting Lab. Regenerate it
with `python3 scripts/generate_lighting_assets.py`. It is covered by MPL-2.0
and this directory's NOTICE.

`studio.hdr` is an original Radiance RGBE panorama with values above 1 for
lighting and skybox tests. Regenerate it and its matching test fixture with
`python3 scripts/generate_environment_assets.py`. No external HDRI is bundled.
