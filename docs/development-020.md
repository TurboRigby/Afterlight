# 0.20 anti-aliasing

Choose **Environment → Anti-aliasing**. The selection belongs to the scene,
participates in undo/redo and saves with it. Changes during Play remain temporary.
Off is the default, preserving older scene appearance. UI and HUD drawing follows
post processing, so text and controls remain crisp.

| Mode | Behavior | Tradeoff |
| --- | --- | --- |
| Off | One raster sample, no AA filter | Sharpest raw output; jagged edges/shimmer remain |
| FXAA | One perceptual-space edge filter | Simplest; can soften fine detail |
| SMAA | Color edges, directional weights, neighborhood blend | More passes; preserves detail better; no temporal stabilization |
| TAA | Jittered samples and reprojected HDR history | Stabilizes edges over time; can soften detail and needs history rejection |

**AA sharpening** (0–1, default 0.1) is bounded by nearby colors. TAA additionally
exposes **History weight** (0–0.98, default 0.9); higher values retain more history.
Fast motion lowers the effective weight. Begin with SMAA for crisp editing or
TAA when motion shimmer is the larger problem. These are quality preferences;
measure GPU post time on the target scene rather than assuming a fixed cost.

FXAA runs after bloom, exposure and tone mapping, using square-root perceptual
color and returning linear output for presentation. Its taps clamp to the scene
viewport. SMAA uses the upstream MIT-licensed shader and area/search tables,
HIGH preset, color edge detection and SMAA 1x. Its three passes include diagonal
and corner handling. A two-pixel guard keeps filtering away from viewport edges.
SMAA attribution and source are retained in source archives and runnable bundles.

TAA resolves linear HDR before bloom and exposure. A 16-phase Halton(2,3) jitter
moves raster samples; picking and shadow views remain unjittered. Per-object
previous rendered matrices and previous bone palettes produce full-window motion
vectors, including inset editor viewports. Sky motion ignores camera translation.
The history alpha stores linear eye depth. Depth mismatch, invalid projection and
out-of-viewport reprojection reject history; a YCoCg neighborhood and variance
bounds clip history colors before blending.

Water and alpha-blended surfaces are reactive: they use current-frame color and
an FXAA finish instead of accumulating refraction or changing surface topology.
This avoids water trails but does not provide temporal stabilization on those
surfaces. Transparent material layers are not independently reprojected. Like
other neighborhood-clipped TAA implementations, fine moving detail can soften;
use SMAA/FXAA if that tradeoff is undesirable. This is TAA at native resolution,
not a temporal upscaler or a denoiser for ray-traced lighting.

Scene switches, interpolation resets, resize, viewport/settings/lighting/asset
changes and large camera cuts invalidate history. New, previously hidden or
material-changed objects invalidate their own motion. Ordinary object movement
and bone animation retain history. Two scratch targets serve either SMAA or
TAA; graphics-queue dependencies protect their reuse. Descriptors for both TAA
history parities are fixed, and CPU transform buffers retain capacity after
warm-up. The main HDR pass has an additional motion attachment even when AA is
Off. Post/motion allocations are currently tied to swapchain size; reducing
unused-mode allocation is a future memory-budget optimization.

TAA needs Vulkan independent attachment blending. Devices without it retain the
saved preference and use FXAA, with a startup log explaining the fallback. Native
verification so far is Apple M4 through MoltenVK; other GPUs still need testing.

Scene format **23** stores `anti_aliasing` (`off`, `fxaa`, `smaa`, `taa`),
`aa_sharpness` and `taa_history_weight`. Older scenes receive AA Off. C modules
must rebuild for **ABI 22**, and existing editor/player processes must restart.

`antialiasing` reads rendered regions to check diagonal coverage, flat colors,
inset and outside viewports, resize and byte-identical overlay pixels across
all modes. `temporal_antialiasing` checks reduced static edge variation, moving
camera/props/bones, exposed background colors, cut/reset/resize/mode switching,
bloom and persistence. Water optics, refraction and underwater regressions run
under each mode with Vulkan synchronization validation. Render-data tests cover
all saved modes, history/undo, invalid settings and legacy defaults.
`motion_vectors` directly checks GPU velocities and linear depth for camera,
object, bone and combined motion; a TAA contact test also covers coincident
water/solid walls and floors. See [verification](testing.md) for the completed run.

Algorithm references: [FXAA whitepaper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf),
[SMAA upstream](https://github.com/iryoku/smaa), and
[temporal AA history and rejection](https://research.nvidia.com/sites/default/files/pubs/2018-08_Adaptive-Temporal-Antialiasing/adaptive-temporal-antialiasing-preprint.pdf).
