#!/usr/bin/env python3
"""
Extracts all 2,205 files from Heroes Lore 5's data.vfs.mp3 container.
Entry #2187 (hash 0xbc909d54) inside data.vfs.mp3 is the internal VFS directory
table containing all 2,204 null-terminated ASCII paths!
"""
import json
import os
import struct

VFS_PATH = "reference/extracted_apk/assets/data.vfs.mp3"
OUT_DIR = "reference/extracted_vfs"
MANIFEST_PATH = "reference/vfs_manifest.json"

def djb2(s: bytes) -> int:
    h = 5381
    for b in s:
        h = ((h << 5) + h + b) & 0xFFFFFFFF
    return h

def main():
    with open(VFS_PATH, "rb") as f:
        vfs = f.read()

    pos = 0
    entries = []
    while pos + 8 <= len(vfs):
        h, sz = struct.unpack("<II", vfs[pos:pos + 8])
        entries.append((pos, h, sz))
        pos += 8 + sz

    assert pos == len(vfs), f"VFS size mismatch: {pos} != {len(vfs)}"

    # Entry 2187 (0xbc909d54) is the directory table
    dir_entry = next((off, h, sz) for off, h, sz in entries if h == 0xBC909D54)
    dir_blob = vfs[dir_entry[0] + 8 : dir_entry[0] + 8 + dir_entry[2]]
    file_count, = struct.unpack("<I", dir_blob[:4])
    names = [s.decode("ascii") for s in dir_blob[4:].split(b"\x00") if s]
    assert len(names) == file_count, f"Expected {file_count} names, got {len(names)}"

    hash_to_name = {djb2(n.encode("ascii")): n for n in names}
    hash_to_name[0xBC909D54] = "__vfs_filelist.bin"

    os.makedirs(OUT_DIR, exist_ok=True)
    manifest = []

    for idx, (off, h, sz) in enumerate(entries):
        rel_path = hash_to_name.get(h, f"__unknown_{h:08x}.bin")
        full_path = os.path.join(OUT_DIR, rel_path)
        os.makedirs(os.path.dirname(full_path), exist_ok=True)
        data = vfs[off + 8 : off + 8 + sz]
        with open(full_path, "wb") as out_f:
            out_f.write(data)
        manifest.append({
            "index": idx,
            "offset": off,
            "data_offset": off + 8,
            "size": sz,
            "hash": f"0x{h:08x}",
            "path": rel_path,
        })

    with open(MANIFEST_PATH, "w", encoding="utf-8") as mf:
        json.dump(manifest, mf, indent=2)

    print(f"Successfully extracted {len(entries)} / {len(entries)} files to {OUT_DIR}")
    print(f"Manifest saved to {MANIFEST_PATH}")

if __name__ == "__main__":
    main()
