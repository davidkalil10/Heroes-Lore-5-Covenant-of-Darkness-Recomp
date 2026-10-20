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
confirmed gameplay and sound on PC, Android and Switch. This is explicit user
device feedback for the runtime integration. The user later accepted the aspect
feature as working too; version 1.1.0 services/input need separate device tests.

For Android/Switch reports collect:

- Commit/artifact identity and device model.
- Time to title/menu, first map, and map transitions.
- Music/effect playback, volume, loops, and pause/resume behavior.
- Gameplay speed, input response, loading spikes, and memory-related failures.
- Boot logs: Android internal storage `boot.log`; Switch
  `sdmc:/switch/heroes_lore_5/boot.log` (fallback `sdmc:/heroes_lore_boot.log`).

Known remaining concerns: full music decoding memory/load cost on Switch, other
interpreted effects, refresh rates above 60 Hz, and startup presentation. The SDL window is now created before Game::boot
so the initial viewport can use its actual size; the renderer starts afterward.
Measure before replacing the renderer or changing CPU clocks/physics timing.

The ignored lab retains the accepted executable, original snapshot, reports,
and integration backup. No personal saves, generated packages, extracted caches,
or lab files belong in the source commit. CI artifacts come from the main tree.

## Real aspect expansion

Native / Wide / Auto are implemented in the main sources. Read `docs/DISPLAY.md`
for controls, policy, guest structure offsets and tests. This changes the actual
game canvas and map cache; never replace it with stretched SDL output. Desktop
CMake and Android collect the new root-level source automatically; Switch already
includes `src`. Player save formats are unchanged. Display preference is a separate
ignored `data/display.ini` (relative to each platform's writable game data root).

Run `ctest --test-dir build --output-on-failure`, the runtime verifier, and
`python tools/verify_aspect.py --exe build/hl5.exe` (Linux: `build/hl5`). CI now runs
all three on Windows and Linux and uploads the aspect captures/logs. Exercise map
transitions, menus, existing saves, dialogue, touch controls, orientation and
docked/handheld modes on devices before declaring full feature coverage.

## 1.1.0 Settings, platform services and input

The user confirmed the complete preceding aspect/performance/audio integration
working. Current additions: Auto default, native Nintendo A/B and X/Y labels,
C/Tab/# and Select/− for skill-row switch, L3 map, R3/F10 Settings, and Google
Drive backup plus GitHub release update services. See `docs/SERVICES.md` for
architecture, required tests, recovery, packaging and exact release asset names.
The existing native options remain reachable when the host panel is closed.

Save restoration uses validated raw guest files with safety backup and mandatory
app reopen. Do not copy Java RMS or live VM-reload code from HL1/HL2/Zero. No
tests authorize/upload real player saves: Drive and download routes are mocked.
Google login and real cross-device save migration still need user validation,
as do OS installers/NRO replacement. README was rewritten as the user guide.
