#!/usr/bin/env python3
"""
Prepara a pasta romfs/ para ser embutida diretamente no heroes_lore_5.nro do Nintendo Switch.
"""
import os
import shutil

def prepare_romfs():
    romfs_dir = "romfs"
    if os.path.exists(romfs_dir):
        shutil.rmtree(romfs_dir)
    os.makedirs(romfs_dir, exist_ok=True)

    # 1. Copia arquivos essenciais da raiz / dist
    candidates = [
        ("dist/libHeroesLore5.so", "libHeroesLore5.so"),
        ("reference/extracted_apk/lib/armeabi/libHeroesLore5.so", "libHeroesLore5.so"),
        ("dist/data.vfs.mp3", "data.vfs.mp3"),
        ("reference/extracted_apk/assets/data.vfs.mp3", "data.vfs.mp3"),
        ("data/dict.bin", "dict.bin"),
        ("dist/dict.bin", "dict.bin"),
    ]

    for src, dst in candidates:
        if os.path.exists(src) and not os.path.exists(os.path.join(romfs_dir, dst)):
            print(f"[RomFS] Copiando {src} -> {dst}")
            shutil.copy2(src, os.path.join(romfs_dir, dst))

    # 2. Copia pasta data/
    if os.path.exists("data"):
        print("[RomFS] Copiando pasta data/...")
        shutil.copytree("data", os.path.join(romfs_dir, "data"), dirs_exist_ok=True)

    # 3. Copia assets_override se existir
    if os.path.exists("assets_override"):
        print("[RomFS] Copiando pasta assets_override/...")
        shutil.copytree("assets_override", os.path.join(romfs_dir, "assets_override"), dirs_exist_ok=True)

    # 4. Copia pasta dist/c se existir
    if os.path.exists("dist/c"):
        print("[RomFS] Copiando pasta c/...")
        shutil.copytree("dist/c", os.path.join(romfs_dir, "c"), dirs_exist_ok=True)

    print("[RomFS] Pasta romfs/ gerada com sucesso e pronta para o elf2nro!")

if __name__ == "__main__":
    prepare_romfs()
