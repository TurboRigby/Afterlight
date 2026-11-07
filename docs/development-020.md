# 0.20 anti-aliasing

The Environment panel saves anti-aliasing per scene. Off preserves the original
single-sample rendering path. FXAA filters the tone-mapped scene in perceptual
space; sharpening is clamped to the local color range. The editor UI is drawn
after filtering, and samples stay inside the scene viewport.

Scene format 23 stores `anti_aliasing`, `aa_sharpness` and `taa_history_weight`.
Older scenes load with AA Off. C game modules must rebuild for ABI 22.

The native `antialiasing` regression reads rendered image regions, verifies
coverage along a diagonal, preserves flat colors, checks an inset viewport and
resize, and round-trips the scene settings. FXAA also passes the existing native
water, refraction, alpha, environment, render feature and editor smoke checks.
