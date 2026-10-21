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

Save restoration uses validated raw guest files with safety backup and a main-thread
native return-to-title handoff; 1.1.1 removes the old mandatory app reopen. Do not copy Java RMS or live VM-reload code from HL1/HL2/Zero. No
tests authorize/upload real player saves: Drive and download routes are mocked.
The user confirmed real backup/restore. New return-to-title behavior and
OS installers/NRO replacement need v1.1.1 device validation. README was rewritten as the user guide.


## 1.1.1 continuation — 2026-10-20

User requested and authorized PT-BR preparation with Google Translate. English
source assets/dict.bin remain untouched. Locale runtime adds 16k+ cached phrases,
live English/PT-BR switching, Latin-1 accents, dynamic formatted strings and
language.ini persistence. See docs/LOCALIZATION.md for hooks and diagnostics.
Title replaces the original version/prompt presentation with installed version
and localized instruction. Gear replaces the separate aspect button; Android
panel scales to screen pixels; RomFS font joins and texture creation are fixed.

Cloud worker only stages validated saves. The main loop destroys old state,
transactionally replaces files, reconstructs native title/slots, and continues
without process exit. Details and safety ordering are in docs/SERVICES.md.

Version is 1.1.1 / Android code 3. tools/bump_version.py updates all metadata;
--check detects drift. Added verify_localization.py checks live dialogue roundtrip,
format preservation, valid UTF-8 and return-to-title with isolated test saves.
Keep hardware validation distinct: user must test installed 1.1.0 detecting the
new release, signature-preserving Android upgrade, NRO replacement, Settings text,
mobile sizing, live language and restored campaign slot selection.


## 1.1.2 continuation — user feedback and UI fixes

User confirmed Android automatic update, live language and restore returning to
title without closing. Switch automatic update is explicitly left for this release.
Screenshots exposed fixed native widget widths: long PT labels overlapped grid
tiles, skill tabs, HUD buttons and equipment values. Added context-aware variants
and revised stat terminology; full headings remain full. HUD Comprar/Salvar fit
inside 28 pixels of the original 30-pixel buttons. See LOCALIZATION.md for sites.

Removed native DrawOption→host auto-open completely, including guest-frame state.
Original Options and gear Settings are independent. Gear growth is capped and
its footprint stays above the native menu/back button. Removed redundant host
Game settings row; preserve updated selection indices in service_tests.cpp.

Added verify_ui.py to Windows/Linux CI: native grid/equipment/tabs, English/PT
HUD differences constrained to button rectangles, and original Options entry/exit
without host modal. Required prior runtime/aspect/localization/services checks
remain. Metadata is 1.1.2 / Android code 4; release packages keep existing names.

## 1.1.3 continuation — 2026-10-21

User reported the 1.1.2 gear too small and asked to integrate the port settings
into original Options, keeping the gamepad shortcut. User also reported most PT
text reverting to English, including the host panel. Removed gear draw/hit code;
added an explicit footer action in both native Options screens, preserving F10/R3.
The six opcode-guarded hooks record paint and intercept only explicit footer input.
Do not restore the prior auto-open-on-paint behavior. Native values/counts stay
untouched; selection survives cached frames and closing the host modal.

Inspected the released 1.1.2 ARM64 APK: full locale JSON and manifest are correct.
Confirmed SDL 2.30.12 opens existing internal-storage files before APK assets.
The old extractor could read and truncate the same destination during an update.
Switched all extraction source reads to AAssetManager and staged size/SHA-checked
replacement. Existing damaged copies are retried. Full PT dictionary is also
compiled in, generated with the external JSON; no partial compact-only PT state
should depend on extraction anymore. Details/source reference in SERVICES.md.

Added same-size upgrade/failure-preservation tests and embedded dictionary tests;
expanded native UI navigation, close/reopen and touch regressions. See the latest
verification summaries under ignored build/ for local evidence. These checks do
not constitute Android/Switch hardware execution. User will validate OTA and UI
on devices; don't claim those tests happened here. Metadata: 1.1.3 / Android code 5.
