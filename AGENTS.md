# Heroes Lore 5 (Recomp) - Agent & Developer Guide

This document is the authoritative engineering guide for **Heroes Lore 5: Knights of the Blue (영웅서기5: 칠흑의 이경)** native C++ recompilation project. It documents the architecture, boot flow, reverse engineering discoveries, network bypass, asset pipeline, translation system, gamepad support, widescreen viewport, and roadmap so that any AI agent or human engineer can seamlessly continue development.

---

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
  - **Aspect Ratio & Window Controls:** Resizable window with Letterbox preservation (F9) and Fullscreen toggle (F11 / Alt+Enter).
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
  - `A` (Xbox) / `B` (Switch): Action / Attack / Interact (`WK_SELECT = -5`)
  - `B` (Xbox) / `A` (Switch): Cancel / Open Menu / Status (`WK_CLEAR = -16`)
  - `X` (Xbox) / `Y` (Switch): Quick Skill 1 (`'1'`)
  - `Y` (Xbox) / `X` (Switch): Quick Skill 2 (`'3'`)
  - `LB`: Quick Skill 3 (`'7'`)
  - `RB`: Quick Item / Potion (`'9'`)
  - `Start`: Menu / Pause (`WK_CLEAR = -16`)
  - `Back / Select / L3`: Toggle Minimap (`'0'`)
  - `R3`: Potion Switch (`'#'`)

---

## 6. Aspect Ratio & Widescreen Architecture

### Current Implementation:
- **Logical Canvas:** 240x400 (3:5 portrait).
- **Aspect Ratio Toggle (F9):**
  - **Mode 1 (Letterbox):** Uses `SDL_RenderSetLogicalSize(ren, W, H)` to preserve the crisp 3:5 aspect ratio with clean borders on widescreen displays.
  - **Mode 2 (Stretched / Fill):** Disables logical letterboxing to stretch the viewport to fill the window.
- **Fullscreen (F11 / Alt+Enter):** Toggles borderless desktop fullscreen.

### True Widescreen Roadmap (Milestone 2):
In the original mobile engine, viewport clipping and tile rendering are governed by:
- `nativeInitDisplay(w, h)` at `0x0009987c`
- `glOrthof(0, width, 0, height)` in the compositor
- `Camera::Camera` and `Graphic::Graphic` map bounds
- **Next Step:** Expose dynamic viewport width (e.g. 480x270 or 320x240) to `nativeInitDisplay`, expanding horizontal tile culling and revealing more of the environment rather than letterboxing or stretching.

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
- [ ] **True Widescreen & Auto Aspect Ratio:** Modify viewport culling in `nativeInitDisplay` / `Camera` / `Graphic` to render additional map tiles dynamically instead of letterboxing.
- [ ] **Gamepad Polish:** On-screen button prompt glyphs matching connected controller (Xbox/PlayStation/Switch).
- [ ] **Multilanguage (PT-BR):** Implement Portuguese translation using the established translation pipeline.
- [ ] **Cloud Saves & Cross-Platform Save Compatibility:** Save slot sync and cloud backup format compatible with other Heroes Lore ports.

### Milestone 3: Cross-Platform Ports
- [ ] **Nintendo Switch:** Port engine using libnx + SDL2.
- [ ] **Android ARM64:** Native Android NDK port with modern touch controls.

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
