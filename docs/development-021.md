# 0.21 engine foundations

This development pass follows the foundation roadmap in order. The water course
and expedition are the representative game. Native Linux verification targets
GNOME Wayland and the NVIDIA GPU supplied for development; Windows verification
is deferred until hardware is available.

Acceptance checklist:

- [x] Player start/pause/settings/finish flow and persisted preferences/progress.
- [ ] Repeated scene/reload/save/load/failure checks and rendering debug views.
- [ ] Resource diagnostics, frame/memory budgets and disabled-effect allocations.
- [ ] Measured large-scene visibility/batching improvements.
- [ ] Raster/material and asset dependency diagnostics exercised in the demo.
- [ ] Native Linux GPU and relocated exported-player verification, plus macOS regressions.

Each completed item records its implementation, checks and measured limits here.

The player now starts at a menu; Escape/F6 pauses and opens graphics/sensitivity
settings, Save/Continue, Restart and Quit. Batch `--frames` runs still autostart.
Preferences and the most recently completed course's best time live in the SDL
user data directory, keyed by project name. Progress saves contain a bounded,
checksummed module snapshot and a validated local scene key; loading initializes
the authored world again and restores module progress, rather than preserving
fluid particles or rigid-body momentum. ABI/state mismatches and corrupt files
retain the running game. These are local project/module saves, not a portable
world-simulation format.

The expedition adds a skinned idle guide and checkpoint/finish audio. Its original
water/river/raft/pour objectives remain the complete course loop. The authored
water-course scene with local user edits is preserved. `player_menu`,
`player_preferences`, `save_game`, `state_reload` and `water_course` pass locally.
Native Linux baseline AA/optics/editor checks (14) passed on GNOME Wayland / RTX
5060 with Vulkan synchronization validation. Broader native checks are ongoing.
