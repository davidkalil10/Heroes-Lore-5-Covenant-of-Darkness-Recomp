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
