#!/usr/bin/env python3
"""
Heroes Lore 5 (Recomp) - Text Extraction & Repacking Tool
Supports EUC-KR / CP949 text files used in HL5:
  - common_text.dat
  - menu_text.dat
  - ingame_text.dat
  - mission_text.dat
  - name.dat
  - tips.dat
  - quest_0.dat / quest_1.dat / quest_2.dat
"""

import os
import sys
import json
import struct
import argparse

def parse_simple_text_dat(data):
    cnt = struct.unpack('<H', data[:2])[0]
    pos = 2
    items = []
    for i in range(cnt):
        buf_sz = struct.unpack('<H', data[pos:pos+2])[0]
        str_len = data[pos+2]
        s_bytes = data[pos+3:pos+3+str_len]
        pos += 2 + 1 + str_len
        try:
            s_txt = s_bytes.decode('euc-kr')
        except UnicodeDecodeError:
            try:
                s_txt = s_bytes.decode('cp949', errors='replace')
            except Exception:
                s_txt = s_bytes.decode('latin1')
        items.append({
            "id": i,
            "text": s_txt
        })
    return items

def build_simple_text_dat(items, encoding='cp949'):
    out = bytearray(struct.pack('<H', len(items)))
    for item in items:
        text = item["text"]
        s_bytes = text.encode(encoding, errors='replace')
        buf_sz = len(s_bytes) + 1
        out += struct.pack('<H', buf_sz)
        out.append(len(s_bytes))
        out += s_bytes
    return bytes(out)

def export_all(vfs_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    simple_files = [
        "common_text.dat",
        "menu_text.dat",
        "ingame_text.dat",
        "mission_text.dat",
        "name.dat",
        "tips.dat"
    ]
    for fn in simple_files:
        in_path = os.path.join(vfs_dir, "c", "csv", fn)
        if not os.path.exists(in_path):
            print(f"Skipping missing {in_path}")
            continue
        with open(in_path, "rb") as f:
            data = f.read()
        items = parse_simple_text_dat(data)
        out_name = fn.replace(".dat", ".json")
        out_path = os.path.join(out_dir, out_name)
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(items, f, ensure_ascii=False, indent=2)
        print(f"Exported {len(items)} strings from {fn} -> {out_path}")

def import_all(json_dir, out_dat_dir):
    os.makedirs(os.path.join(out_dat_dir, "c", "csv"), exist_ok=True)
    simple_files = [
        "common_text.json",
        "menu_text.json",
        "ingame_text.json",
        "mission_text.json",
        "name.json",
        "tips.json"
    ]
    for fn in simple_files:
        in_path = os.path.join(json_dir, fn)
        if not os.path.exists(in_path):
            continue
        with open(in_path, "r", encoding="utf-8") as f:
            items = json.load(f)
        dat_data = build_simple_text_dat(items)
        out_fn = fn.replace(".json", ".dat")
        out_path = os.path.join(out_dat_dir, "c", "csv", out_fn)
        with open(out_path, "wb") as f:
            f.write(dat_data)
        print(f"Compiled {len(items)} strings from {fn} -> {out_path} ({len(dat_data)} bytes)")

def main():
    parser = argparse.ArgumentParser(description="HL5 Text Extraction and Compiler")
    parser.add_argument("mode", choices=["export", "import"], help="Mode: export or import")
    parser.add_argument("--vfs", default="reference/extracted_vfs", help="Extracted VFS directory")
    parser.add_argument("--json", default="lang/source_ko", help="Directory for JSON text files")
    parser.add_argument("--out-dat", default="assets_override", help="Output directory for compiled .dat files")
    args = parser.parse_args()

    if args.mode == "export":
        export_all(args.vfs, args.json)
    elif args.mode == "import":
        import_all(args.json, args.out_dat)

if __name__ == "__main__":
    main()
