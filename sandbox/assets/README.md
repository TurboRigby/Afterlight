# Sample assets

`beacon.glb` and the jump, land, checkpoint and finish WAV cues are original
procedural Afterlight assets. Regenerate them with:

```sh
python3 scripts/generate_sample_assets.py
```

The same script writes the analytic glTF/GLB fixtures under `tests/assets`.
The beacon is a static colored 16-sided stepped tower. Audio is mono 16-bit PCM
at 22050 Hz. No external model or sound download is needed to run the course.
