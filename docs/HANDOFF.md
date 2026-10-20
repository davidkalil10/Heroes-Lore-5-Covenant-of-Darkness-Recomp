# Runtime integration handoff — 2026-10-20

## Current state

The user accepted `codex_lab/pc_test` as fluid, fast, and audible and explicitly
authorized integration and push to GitHub `main`. Those sources now live in the
main project, retaining the preexisting local blitter and input changes from the
tested snapshot. `src/` is authoritative; do not reapply the old lab patch.

Changes include seven statically compiled ARM hot regions, SDL Ogg/MP3 audio,
indexed VFS lookup, real elapsed clock accumulation, and removal of the extra
manual limiter when renderer vsync is active. Runtime metrics are opt-in during
graphical gameplay. Save formats and existing player save files were preserved.

`Makefile.switch` now includes `src/audio` and excludes `stb_vorbis.c` from CFILES;
the decoder is included exactly once by audio.cpp. Android/desktop recursive
CMake already include audio.cpp. The checked-in generated include means CI does
not require Python code generation or new external audio packages to compile.

## Verification and continuation

Read `docs/RUNTIME.md` for diagnostics and limitations, and run
`python tools/verify_runtime.py --exe build/hl5.exe` after runtime changes.
The integrated PC build passes 14 full ARM state comparisons, VFS oracle checks,
11 identical rendered images, and nonzero output from the SDL audio mixer.
The hot-block generator reproduces the checked-in include without changing other
sources. Windows/Linux Actions run the same runtime validation before packaging. The
Windows MSYS2 environment explicitly installs UCRT64 Python for this check.

The prior isolated PC benchmark measured a 3.13x mean improvement. The user has
confirmed PC gameplay and sound. APK/NRO compilation and installation are the next
steps; do not treat successful cross-compilation as device gameplay validation.

For Android/Switch reports collect:

- Commit/artifact identity and device model.
- Time to title/menu, first map, and map transitions.
- Music/effect playback, volume, loops, and pause/resume behavior.
- Gameplay speed, input response, loading spikes, and memory-related failures.
- Boot logs: Android internal storage `boot.log`; Switch
  `sdmc:/switch/heroes_lore_5/boot.log` (fallback `sdmc:/heroes_lore_boot.log`).

Known remaining concerns: full music decoding memory/load cost on Switch, other
interpreted effects, refresh rates above 60 Hz, and the Switch window appearing
after Game::boot (a possible contributor to the prior white startup screen).
Measure before replacing the renderer or changing CPU clocks/physics timing.

The ignored lab retains the accepted executable, original snapshot, reports,
and integration backup. No personal saves, generated packages, extracted caches,
or lab files belong in the source commit. CI artifacts come from the main tree.
