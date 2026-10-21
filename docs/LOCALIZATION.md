# Heroes Lore 5 - Localization & Text Pipeline

## 1. Overview

Heroes Lore 5 stores textual assets in binary string table files (`.dat`) located in `/c/csv/`, and scenario cutscenes in `/c/map/*.scn` and `/c/ep/*/*.scn`.

The recompilation introduces:
1. `tools/text_tool.py`: An automated Python pipeline to extract all game strings to editable JSON, and recompile them back into exact binary formats.
2. `StaticUtil::LoadRes` Live Asset Override: Transparently loads translated `.dat` files directly from `assets_override/c/csv/` without needing to unpack or rebuild `data.vfs.mp3`.

---

## 2. Binary Format Specification (`.dat`)

All `.dat` text files follow a standardized binary structure:

```
[Header]
  uint16_t  string_count

[Repeated for string_count entries]
  uint16_t  buffer_capacity   (typically string_length + 1)
  uint8_t   string_length     (number of bytes in the string)
  uint8_t   string_bytes[...] (encoded in CP949 / EUC-KR / ASCII)
```

### Note on Encodings & Fonts
- The original game loads two bitmap fonts at boot:
  - `/c/font/kor.fnt`: Korean Syllables + ASCII / digits.
  - `/c/font/eng.fnt`: Western Roman alphabet + punctuation.
- Strings encoded in standard ASCII or CP949 render automatically.
- Multi-line text utilizes semicolons (`;`) or line-breaks as delimiters (e.g. `Central;17 TH` or `First Line;Second Line`).

---

## 3. Localization Catalog

| File | Strings | Current Status | Description |
|---|---|---|---|
| `common_text.dat` | 351 | **100% English** | Universal UI, stats, items, menus, confirmations. (348 originally EN, 3 translated) |
| `name.dat` | 63 | **100% English** | All protagonists, antagonists, NPCs, merchants, and soldiers. |
| `tips.dat` | 26 | **100% English** | Gameplay hints, battle mechanics, multple-ending hint. |
| `ingame_text.dat` | 357 | **Partial EN (93/357)** | Area/Zone names (already in EN), status messages and quest banners. |
| `mission_text.dat` | 105 | Source KO Extracted | Title achievements, boss rush criteria, monster hunt milestones. |
| `menu_text.dat` | 210 | Source KO Extracted | Save management, sound settings, cloud save text. |
| `quest_0/1/2.dat` | 151 | Source KO Extracted | Story & side quest descriptions and objectives. |
| `c/map/*.scn` | 258 files | Source KO Extracted | In-game dialogues, cutscenes, scripted events. |

---

## 4. Pipeline Workflow

### Step 1: Export Source Strings
```bash
python tools/text_tool.py export --vfs reference/extracted_vfs --json lang/source_ko
```

### Step 2: Edit Translations
Create or edit JSON files in `lang/en/` (or `lang/pt_br/`):
```json
[
  { "id": 0, "text": "None" },
  { "id": 1, "text": "Schulz" },
  { "id": 2, "text": "Ren" },
  { "id": 3, "text": "Tiana" }
]
```

### Step 3: Compile Overrides
```bash
python tools/text_tool.py import --json lang/en --out-dat assets_override
```

When `hl5.exe` is run, the engine loads the translated `.dat` files immediately and displays the English text.


## 3. Live English / PT-BR — version 1.1.1

The compiled ASCII English overrides and Korean→English dict.bin remain canonical.
src/localization.* loads assets/locales/pt-BR.json once. PT-BR is the default;
language.ini in the platform data root stores English/PT-BR preference. This file
is per-device and excluded from cloud archives/install packages.

Trans::translatePtr resolves Korean→English→selected language at every draw,
retaining separately keyed stable guest pointers for each language. Do not free
those buffers on a toggle; native callers may retain a pointer temporarily.
DrawTextLine (0x141940) and DrawTextField (0x141b00) translate the complete message
before native wrapping/typewriter truncation. Numbers embedded in printf messages
are matched against anchored templates, with stable format parameters preserved.

UTF-8 Latin-1 accents occupy two bytes but use the original ASCII cell width.
Hooks at 0x140ee8, 0x140f6c, 0x141510, 0x14178c and 0x14170c keep width/wrapping
consistent. 0x13cf00 renders accented glyphs from a cached Inter bitmap into the
current RGB565 framebuffer, respecting clipping/color/alpha. ASCII keeps the
native pixel font. 0x141c64 prevents partial UTF-8 during typewriter memcpy.
TextLine bounds are enforced at glyph submission and restored at 0x141a94;
entry stack +4 is line count, +12 is first line (not the other way around).

The original title instruction is a bitmap. DrawTitle (0x21db18) marks title
visibility; native state changes/main menu clear it. Game::frame composes the
localized instruction and actual services::Version after guest drawing, including
cached title frames. Do not persist this overlay into gameplay.

Corpus: lang/pt_br/phrases.json (editable cache) and assets/locales/pt-BR.json
(runtime resource). Translation uses Google Translate with user authorization;
proper names/classes and formatting were reviewed, but full campaign prose has
not had a complete human playthrough. Preserve printf tokens and ; ^ } { commands.
The renderer supports Latin-1; normalize smart punctuation and remove zero-width
characters. Check screenshots for text length, not only dictionary coverage.

tools/prepare_locale.py rebuilds from the cache. --translate explicitly permits
sending missing game text to Google Translate; obtain authorization before using
that flag on new private texts. It never reads saves/tokens. Both Android and
Switch packagers explicitly include locales; Windows/Linux copy assets recursively.

Validation: python tools/verify_localization.py --exe build/hl5.exe. It compares
fresh English/PT-BR with an EN→PT-BR→EN switch during an active dialogue, checks
UTF-8/format tokens and native return to title. Reports/captures use disposable
saves under build/localization-verification. CLI --language en|pt-BR and
--language-at frame:code do not persist the test override. --return-title-at frame
exercises the native state factory. Runtime/aspect verifiers explicitly use English.


## 1.1.2 localized widget budgets

Locale::uiText provides Full/Grid/Tab/Stat/Hud contexts. Trans guest-cache keys
include context so HABILIDADES in a header cannot leak into a 33-pixel tab (HAB.)
or 48-pixel tile (HABIL.). Canonical resource pointers and English stay unchanged.
Stat terms were manually revised: ATQ, DEF fís., DEF mág., Vel. atq., Resist.
These are UI labels, not replacements inside narrative sentences.

Verified BFont wrapper return addresses select widget context:
- 0x1fa390 / 0x21d6e4: in-game/title grids (48-pixel tiles).
- 0x1f1450 / 0x1f1a04: skill filter tabs (33-pixel spacing).
- 0x1f5864 / 0x1f5948: equipment stat label columns, separate numeric draws.
- 0x1dc818 / 0x1dcc98 / 0x1dcff4: touch HUD labels (30-pixel buttons).

HUD text is Mapa / Comprar / Salvar, including lowercase source aliases. If the
native bitmap advance exceeds 28 pixels, fitPortLabel rasterizes the whole label
with the existing cached Latin TTF, fits only its horizontal text width, respects
font color/alpha and updates the pen. It changes neither canvas nor scenery scale.
Short fitting labels retain the native pixel font. Do not globally truncate or
abbreviate SKILL: headers and dialogue have different space requirements.

tools/verify_ui.py captures real grid/equipment/skill menus using disposable
campaigns and checks English/PT HUD pixel differences outside button rectangles.
It also opens and exits original Options with the host renderer active.
--game-menu-at frame:0 pushes native menu state 3 for diagnostics; -1 previews the
touch HUD in headless mode, temporarily enabling visibility then restoring flags.
The preview never advances campaign state. HL5_TRACE_UI logs unique label call
sites/geometry and fitted widths; never enable it in normal release environments.

## 1.1.3 — dictionary independent of external extraction

The authoritative full PT corpus is now compiled into src/locales_pt_br.inc.
tools/prepare_locale.py emits BOTH assets/locales/pt-BR.json and that generated
include, offline from the existing cache. Never edit the include independently;
verify_localization.py checks parsed equality of both corpora. Locale::init resets
language/cache/dictionary state and loads the compiled corpus; live EN/PT/EN uses
the same draw-time resolution and language-specific guest buffers as before.

This prevents the mixed state reported after Android OTA: compact HUD/grid labels
were compiled in C++, whereas full native labels and host Settings depended on a
JSON file that extraction could truncate. Fixing extraction repairs all assets;
embedding the dictionary additionally makes translation independent of extraction,
the working directory, and older external copies. Keep English canonical assets,
control codes, printf tokens and Latin glyph handling unchanged. The native
Game speed label is now Velocidade, fitting before the value field.

Tests include the complete dictionary with an absent resource directory, both
directions of live switching, native Options rendering and the custom panel.
