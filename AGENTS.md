# Heroes Lore 5 (Recomp) - Agent & Developer Guide

This document is the authoritative engineering guide for **Heroes Lore 5: Covenant of Darkness (영웅서기5: 칠흑의 이경)** native C++ recompilation project. It documents the architecture, boot flow, reverse engineering discoveries, network bypass, asset pipeline, translation system, gamepad support, widescreen viewport, and roadmap so that any AI agent or human engineer can seamlessly continue development.

---

## Current integration — 2026-10-20

The user has manually accepted the Windows, Android and Switch runtime as fluid,
fast, and audible. Its runtime changes are now integrated into `src/`; the main project is
the authoritative source. `codex_lab/` remains a local ignored experiment and
backup, not a CI/release source. Read `docs/HANDOFF.md` and `docs/RUNTIME.md` before
changing the CPU, clocks, audio, build rules, or VFS hooks.

- CPU: seven opcode-guarded statically compiled ARM regions in
  `src/core/hot_blocks.inc`. Regenerate with `python tools/generate_hot_blocks.py`;
  do not hand-edit the generated decoder or alter its ARM semantics independently.
- Audio: `src/audio/audio.cpp` provides SDL2 mixing and Ogg/MP3 decoding. Do not
  restore the silent JNI stubs or require SDL2_mixer. `stb_vorbis.c` is included by
  `audio.cpp`, so it must not be compiled as a separate C translation unit.
- Switch `SOURCES` must include `src/audio`; `CFILES` excludes `stb_vorbis.c`.
  Desktop and Android CMake recursively collect C++ sources, not the decoder C file.
- Timing: retain fractional real milliseconds and avoid the old >50 ms ->16 ms
  rule. The manual limiter is skipped when renderer vsync is active.
- Diagnostics: `HL5_NO_AOT`, `HL5_NO_VFS_CACHE`, `HL5_VERIFY_AOT`,
  `HL5_VERIFY_VFS`, `HL5_FIXED_STEP_MS`, `HL5_AUDIO_TEST`, `HL5_CAPTURE_AUDIO`,
  `HL5_PROF`, and `HL5_BENCH` are documented in `docs/RUNTIME.md`.
- Required runtime validation after relevant changes:
  `python tools/verify_runtime.py --exe build/hl5.exe` (Linux: `build/hl5`).
  It uses disposable saves and dummy audio, compares 11 full frames, checks 14
  ARM states and VFS results, and confirms nonzero audio output. CI runs it on
  Windows/Linux. Do not claim Android/Switch gameplay validation from a PC test.
- Local preexisting CPU blitter/input changes were included in the accepted
  snapshot and are intentionally preserved in this integration.

## 1. Project Overview & Target Platforms

- **Source Binary:** `libHeroesLore5.so` (ARMv5TE/v7a 32-bit ELF extracted from `Heroes Lore 5 [01.01.00].apk`).
- **Primary Goal:** High-performance, portable, native C++17 port running on:
  1. **Windows** (SDL2 + OpenGL / Software Compositor) - *Active, Playable & Fully Localized*
  2. **Nintendo Switch** (libnx + SDL2)
  3. **Android ARM64** (Native C++ NDK without legacy 32-bit container requirements)
- **Status:** **Milestone 1 (Standalone Playable PC Release with Full English Translation & Gamepad Support) Achieved**.
  - All engine subsystems fully initialize.
  - Legacy Korean telecom carrier network verification completely bypassed (Boot, Title Screen, Main Menu).
  - Title Screen fully renders in 1129 unique RGB565 colors.
  - Title to Main Menu transition verified (`KeyTitle` bypass).
  - Main Menu to Character / Slot Selection verified (`KeyMainMenu` -> `DrawStart` / `KeyStart`).
  - Character Creation, Class Selection (Warrior, Knight, Rogue, Gunslinger), and confirmation verified.
  - In-Game cutscene and scenario engine (`00000.scn`, Map 36 tiles, objects, animations) fully loaded and rendering in 4,685 unique colors.
  - **Full English Translation:** 100% of Menus (`menu_text.dat`), In-game strings (`ingame_text.dat`), Quests/Achievements (`mission_text.dat`), Help Manuals (`help_android_*.dat`), and All Story Dialogue Scenarios (`c/map/*.scn` and `c/ep/ep_*/*.scn`, 258 files total) translated and compiled into `assets_override/`.
  - **Gamepad Support:** Plug-and-play SDL2 GameController auto-detection (Xbox, PlayStation, Nintendo Switch) with analog stick and button mappings.
  - **Aspect Ratio & Window Controls:** Real Native / Wide / Auto viewport expansion (F9, Select+Start, Settings) and Fullscreen toggle (F11 / Alt+Enter).
  - Complete standalone distribution packaged in `dist/` with zero external dependencies (pure Windows native).

---

## 2. Directory Structure

```
C:\Users\david\Desktop\Projetos_Flutter\heroes_lore_5_recomp\
├── build\                 # Ninja / CMake build directory (hl5.exe)
├── dist\                  # Standalone portable distribution (double-click to play)
│   ├── hl5.exe            # Standalone Windows executable
│   ├── SDL2.dll           # SDL2 runtime
│   ├── libHeroesLore5.so  # Game core ELF library
│   ├── data.vfs.mp3       # Original game VFS archive
│   ├── assets_override\   # Live English translations (.dat and .scn overrides)
│   ├── data\              # Persistent save games (internal/external)
│   ├── README.txt         # English user manual & controls
│   └── LEIAME.txt         # Portuguese user manual & controls
├── src\
│   ├── main.cpp           # Windows host entry point (SDL2 window, input, gamepad, frame loop)
│   ├── core\
│   │   ├── cpu.h / .cpp   # Interpreted 32-bit ARM CPU with hooks & trap system
│   │   ├── guest.h / .cpp # Guest virtual memory allocator (gm::malloc_, gm::ptr)
│   │   └── elf_loader.h/.cpp # ELF loader, symbol resolver, and PLT importer
│   └── hle\
│       ├── hle.h / .cpp   # High-Level Emulation: libc, libm, JNI, GLES1 compositor
├── assets_override\       # Live host asset overrides (c/csv/*.dat, c/csv2/*.dat, c/map/*.scn)
├── lang\
│   ├── source_ko\         # Extracted original Korean text in JSON format
│   ├── en\                # English translated text in JSON format
│   └── dict.json          # Cached translation dictionary (7,000+ phrases)
├── tools\
│   ├── translate_all.py   # Full automated translation and .dat/.scn compiler pipeline
│   ├── text_tool.py       # Bi-directional text extractor and .dat compiler
│   └── extract_vfs.py     # VFS archive unpacker
├── reference\
│   ├── extracted_apk\     # Raw extracted APK contents (classes.dex, assets, libs)
│   ├── extracted_vfs\     # Extracted assets from data.vfs.mp3
│   ├── libHeroesLore5.asm # 16 MB disassembly with function and symbol names
│   └── vfs_manifest.json  # File manifest of the internal VFS archive
```

---

## 3. Scenario & Dialogue System (.scn)

All story cutscenes, in-game NPC dialogues, and quest triggers are stored in binary `.scn` files (218 in `c/map/` and 40 in `c/ep/ep_*/`).

### Binary Format Specification:
1. **Opcode Bytecode:** Located from byte 0 up to `count_pos`. Contains script tokens and VM opcodes.
2. **String Table Header:** Located at `count_pos`:
   - `count` (1 byte, $N$ strings).
3. **String Lengths Array:** $N$ bytes, where each byte specifies `len(string_bytes) + 1` (including the null terminator).
4. **String Data Pool:** $N$ null-terminated CP949 strings corresponding sequentially to the lengths array.
5. **Runtime Lookup:**
   - Bytecode opcodes call `Interpreter::Strings::getString(int index, int* outLen)` at `0x0009a528`.
   - Strings are resolved **strictly by integer index** (`0, 1, 2, ...`), **NOT** by hardcoded file offsets!
   - This architectural feature allows English strings of arbitrary lengths without invalidating bytecode addresses or pointers.

### Exact Roundtrip Verification:
- All 258 `.scn` files across the VFS archive were tested and verified with 100% roundtrip byte accuracy using `tools/translate_all.py`.

---

## 4. Help Menu System (help_*.dat)

The Help menu (`c/csv2/help_android_skt.dat`, `help_ktf.dat`, etc.) contains 21 game guide topics:
1. **Header:** `uint16 count` (21 entries).
2. **Entries:**
   - `uint16 entry_size` (total byte length of the entry).
   - `uint16 title_len` + title bytes (CP949).
   - `uint16 body_len` + body bytes (CP949).
3. Semicolons (`;`) act as line breaks within the body text.

---

## 5. Controls & Gamepad Support

`src/main.cpp` incorporates plug-and-play controller detection via SDL2 `SDL_GameController`:
- **Auto-detection:** Automatically hooks `SDL_CONTROLLERDEVICEADDED` and `SDL_CONTROLLERDEVICEREMOVED`.
- **Directional Controls:** Supports both the physical D-Pad and the Left Analog Stick (with a deadzone threshold of 12,000 for activation and 8,000 for release).
- **Default Button Mapping:**
  - `A` (Xbox / Switch): Action / Attack / Interact (`WK_SELECT = -5`)
  - `B` (Xbox / Switch): Cancel / Open Menu / Status (`WK_CLEAR = -16`)
  - `X` (Xbox / Switch): Quick Skill 1 (`'1'`)
  - `Y` (Xbox / Switch): Quick Skill 2 (`'3'`)
  - `LB`: Quick Skill 3 (`'7'`)
  - `RB`: Quick Item / Potion (`'9'`)
  - `Start`: Menu / Pause (`WK_CLEAR = -16`)
  - `Back / Select / −`: Skill row switch (`'#'`), on release.
  - `L3`: Minimap (`'0'`); `R3`: host Settings.
  - Keyboard `C`, Tab or `#`: skill row switch.
  - Nintendo A/B and X/Y use their printed labels; see `src/input_mapping.cpp`.

---

## 6. Aspect Ratio & Widescreen Architecture

### Current implementation
- `src/display_layout.*` owns Native (240x400), Wide (712x400, approximately
  16:9), and Auto (actual window/orientation) policy, saved in `data/display.ini`.
- F9, Back/Select+Start (Switch −/+), or Settings → Aspect ratio cycles
  modes. Back and Start alone dispatch on release so the chord sends no game action.
- SDL presentation always uses uniform scaling. There is no stretch mode.
- `Game::resizeViewport` calls the original `nativeInitDisplay`, replaces both
  framebuffer pixel allocations while preserving handles, and refreshes Graphic
  context, map culling/cache/camera, and live dialog geometry. Read
  `docs/DISPLAY.md` before changing the guest structure fields or CPU paint hooks.
- F11 / Alt+Enter toggles desktop fullscreen. Auto debounces window resizes.
- Run CTest, `tools/verify_aspect.py`, and the runtime verifier after changes.
  The aspect test verifies actual scenery expansion, identical tile pixels,
  live dialog/orientation changes, 60 repeated resizes, and nonzero audio.
- The user confirmed audio, performance and the aspect modes on the integrated
  build. Version 1.1.0 services/input still require their own device validation.
- Auto is now the missing/invalid-preference default. Preserve explicit saved choices.

---

## 7. Gameplay Databases & Tables Specification

All gameplay data is stored in specialized binary `.dat` files inside `c/csv/`:

### 1. Quests (`quest_0.dat`, `quest_1.dat`, `quest_2.dat`):
- **Header:** `uint16 count` (151 quests per file).
- **Entry Structure:**
  - `uint16 entry_size`
  - 3-byte prefix (flags / ID)
  - `uint8 title_len` + title bytes (CP949)
  - `uint8 body_len` + body bytes (CP949, `;`-separated)
  - `uint8 subtitle_len` + subtitle bytes (e.g. `Main Quest`)
  - 38-byte fixed footer (rewards, exp, gold, item IDs, progress state)

### 2. Items & Equipment (`item_00.dat` .. `item_18.dat`):
- **Header:** `uint16 count` (Weapons, Armors, Consumables, Accessories).
- **Entry Structure:**
  - `uint16 entry_size`
  - `uint16 item_id`
  - `uint8 name_len` + name bytes
  - `uint32 price`
  - `uint8 desc_len` + description bytes (`;`-separated)
  - Trailing attribute block (stats, level requirement, class masks)

### 3. Skills & Traits (`skill_00.dat` .. `skill_05.dat`):
- **Header:** `uint16 count` (Class skills, weapon skills, passive traits).
- **Entry Structure:**
  - `uint16 entry_size`
  - `uint8 name_len` + name bytes
  - 47-byte fixed attribute block (cooldown, SP cost, animation IDs, scaling)
  - `uint8 desc_len` + description bytes (`;`-separated)

### 4. Enemies (`enemy_0.dat` .. `enemy_2.dat`, `enemy_expert_*.dat`):
- **Header:** `uint16 count` (Monsters, bosses, turrets).
- **Entry Structure:**
  - `uint16 entry_size`
  - `uint8 name_len` + name bytes
  - 128-byte fixed stat block (HP, ATK, DEF, EXP, drop rates, AI scripts)

### 5. Other Gameplay Tables:
- `mission_list.dat`: Achievement titles (`uint16 count`, `uint8 name_len`, name, 34-byte data).
- `smith_*.dat`: Blacksmith craft recipes (`uint16 id`, name, material list, 23-byte data).
- `buildup.dat`: Socket gems & upgrades (`uint8 name_len`, name, gem stat block).
- `set_option.dat`: Equipment set bonuses (`uint8 name_len`, name, bonus stat block).

All compiled tables are handled by `tools/translate_gameplay_tables.py` and output directly to `assets_override/c/csv/` and `dist/assets_override/c/csv/`.

---

## 8. Translation & Localization Pipeline

The project includes an end-to-end automated pipeline:
- `tools/translate_all.py`: Extracts and parses all UI text files (`menu_text`, `ingame_text`, `common_text`), help databases, and all 258 scenario dialogue files (`.scn`).
- `tools/translate_gameplay_tables.py`: Translates and compiles all quests, items, skills, enemies, smith recipes, achievements, and socket options.
- Uses an incremental dictionary cache at `lang/dict.json` (7,700+ phrases).
- Preserves engine control formatting:
  - `;`: Line breaks in dialog boxes and UI
  - `^`: Color formatting tags
  - `}word{`: Highlight keyword brackets
  - `*`, `#`, `&`, `|`: UI item indicators
- Engine Terminology:
  - `정령` / `GIRL` -> `SPIRIT` (Spirit / Elemental)
  - `SHARGE` -> `CHARGE`
- Title Screen Rating Badge:
  - Hooked `MX_cmnDrawGameRating` (`0x00100000 + 0x40d78`) and `gamerating_draw` (`0x00100000 + 0x3fff4`) in `src/core/cpu.cpp` to cleanly suppress the legacy Korean rating badge.
- Encodes output in CP949 (with standard ASCII English support).
- Simultaneously outputs compiled `.dat` and `.scn` files to `assets_override/` (development) and `dist/assets_override/` (standalone package).

---

## 9. Roadmap

### Milestone 1: Windows Standalone (Completed)
- [x] ARM32 ELF execution on Windows host via interpreted CPU & memory manager.
- [x] Bypass telecom carrier verification and network dependencies.
- [x] SDL2 window, software compositor, and input handling.
- [x] Plug-and-play GameController auto-detection (Xbox, PlayStation, Nintendo Switch).
- [x] Full English UI, Scenarios, Quests, Items, Skills, and Enemies.
- [x] Standalone portable distribution (`dist/`).

### Milestone 2: Modernization & Quality of Life (In Progress)
- [x] **True Widescreen & Auto Aspect Ratio:** Modify viewport culling in `nativeInitDisplay` / `Camera` / `Graphic` to render additional map tiles dynamically instead of letterboxing.
- [ ] **Gamepad Polish:** On-screen button prompt glyphs matching connected controller (Xbox/PlayStation/Switch).
- [x] **Multilanguage (PT-BR):** Live English/PT-BR selection, default PT-BR; see docs/LOCALIZATION.md.
- [x] **Cloud Saves:** Google device login and manual backup/restore implemented for HL5; real device/account backup and restore confirmed by user. Separate archives from other games.

### Milestone 3: Cross-Platform Ports
- [x] **Nintendo Switch:** libnx + SDL2 build pipeline available; the user accepted shared runtime/aspect gameplay; validate new services on hardware.
- [x] **Android ARM64:** Android NDK build pipeline and touch controls available; the user accepted shared runtime/aspect gameplay; validate new services on hardware.

---

## 10. Building and Running

### Build from Source:
```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
Set-Location "C:\Users\david\Desktop\Projetos_Flutter\heroes_lore_5_recomp"
ninja -C build
```

### Run Standalone:
Double-click `dist\hl5.exe`.

## 11. Settings, cloud saves and updates — 1.1.0

Read `docs/SERVICES.md` before changing networking, save paths, installers or
version metadata. The shared services do not run in headless game tests.
Settings pauses guest frames. Restore workers only stage validated bytes. The main
thread MUST destroy the old guest state before swapping save files, then recreate
the title to reload slot metadata. Never resume the old campaign after restoration.
Use Game::returnToTitle() / services::applyPendingRestore(); do not quit the app.
Archive only internal/external files, never OAuth tokens or device preferences.
Keep transactional restore safety copies and installer rollback. Keep TLS peer
verification enabled, including Switch's RomFS CA bundle.

Required checks now include CTest and `python tools/verify_services.py`, in
addition to runtime/aspect scripts. Services tests use mocked transport, never a
real Google account. Package only public data/dict.bin plus runtime assets;
never restore the old whole-data-directory copy rules. Existing OAuth client is
reused from Zero; game IDs, Drive folder and save filenames stay HL5-specific.
Version changes must update C++ services::Version, Android name/code and Switch
metadata. Actions artifacts are not releases and do not trigger the updater.


## 12. Version 1.1.1 — live language and services polish

Read docs/LOCALIZATION.md and the 1.1.1 additions to docs/SERVICES.md before edits.
PT-BR is the default; English resources remain canonical. Never overwrite guest
resource strings with translated bytes: language changes must affect already-open
dialogues. Locale::text resolves at draw time; Trans retains separate stable guest
buffers per language. Width/wrapping hooks treat UTF-8 Latin-1 accents as one ASCII
cell; preserve original CPU instructions for other encodings and English.

Settings uses a gear at upper right; no separate aspect overlay. Android layout
scales to available pixels; RomFS resource joins must preserve romfs:/ (one slash).
Use SDL_getenv with SDL_setenv for UI diagnostic toggles on Windows.

Required checks: CTest, runtime/aspect/services scripts, and
python tools/verify_localization.py --exe build/hl5.exe (Linux: build/hl5).
Use tools/bump_version.py, then --check before a release. Android versionCode must
increase; preserve signing configuration. Keep language.ini out of cloud backups
and packages, alongside all other per-device preferences and credentials.
User confirmed prior cloud roundtrip; v1.1.1 device UI/update validation remains
separate from host tests. Do not claim hardware execution from cross-compilation.


## 13. Version 1.1.2 — native UI bounds and independent settings

User confirmed Android OTA, live language, and restoring without app exit. Switch
OTA still needs the next-release device test. Native DrawOption 0x21a1ec MUST NOT
auto-open host Settings: that hook and guest-frame bookkeeping were removed.
Only gear/F10/R3 open host Settings. Preserve bounded gear geometry/hit testing.

Locale::uiText contexts and Trans cache keys preserve full titles while compact
grid/tab labels fit their original rectangles. Do not apply a global SKILL→HAB.
replacement or shrink the whole game. Read docs/LOCALIZATION.md for verified draw
sites and HUD-only 28-pixel fitting. Keep translated attribute labels separate
from right-aligned values. Use tools/verify_ui.py in addition to existing checks.
Diagnostics operate on isolated test campaigns, never the player's saves.

## 14. Version 1.1.3 — native settings entry and Android OTA regression

This supersedes the gear instructions in sections 12/13. Read the 1.1.3 additions
to SERVICES/LOCALIZATION/DISPLAY. Native paint hooks may record/render the explicit
footer row, but MUST NEVER auto-open the host menu. Open only on new-row confirm/
touch or F10/R3. src/native_settings.cpp guards six original ARM entry opcodes;
title Options=0x21a1ec, game System=0x1e7d38/substate1. Native option arrays/counts
are never extended. Guest state +12=menu,+13=substate; title +14/+15 and in-game
+15/+16 are visible/absolute cursor bytes. Temporary paint-only suppression must
restore BOTH bytes; retain their difference so scrolling stays intact. Original
menus may reuse cached frames: do not erase footer selection on a skipped paint.
Validate current top state at Common+0x244 before keeping a cached footer visible.

Android extraction MUST open APK sources through AAssetManager, never
SDL_RWFromFile (which prefers extracted internal-storage files). Stage and validate
size/SHA before replacing; don't truncate the live destination. Locale's full
dictionary is generated into src/locales_pt_br.inc by prepare_locale.py along with
the public JSON. verify_localization.py must check their equality. Keep the
existing live Trans pointer caches and UTF-8 layout guards.

Required checks: CTest plus verify_runtime/aspect/services/localization/ui.py.
The UI verifier now checks explicit native title/game access and touch. Test paths
must remain disposable. Version bump uses bump_version.py; code 5 for 1.1.3.

## 15. Version 1.1.4 — updater regression and Switch copy primitives

Read the 1.1.4 SERVICES/HANDOFF sections. Do not use filesystem::copy_file for
Switch NRO installation: libnx returned Function not implemented on user hardware.
installPortableUpdate uses stdio streaming + C rename, verifies before unmounting
RomFS, retains a backup and rolls back on swap failure. No guest execution may
resume after unmount; envSetNextLoad targets the exact launched NRO on success.

Startup check result must remain visible without a gear: one-shot notice and
checking banner, bounded retries and explicit autoCheck=false preference. Progress
is worker-owned byte counts published under mutex; render only on main thread.
Preserve TLS verification/CA; earlier ports' TLS settings are not an example to copy.
Required checks remain CTest, runtime/aspect/services/localization/ui verifiers.
The services verifier now also requires build/hl5_download_tests and uses a local
HTTP server to validate real redirect/stream progress with no external accounts.
