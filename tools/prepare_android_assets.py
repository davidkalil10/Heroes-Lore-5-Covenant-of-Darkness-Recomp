#!/usr/bin/env python3
"""
Prepara a pasta android/app/src/main/assets/ para ser embutida diretamente no APK do Android.
"""
import os
import shutil

def prepare_android_assets():
    assets_dir = os.path.join("android", "app", "src", "main", "assets")
    if os.path.exists(assets_dir):
        shutil.rmtree(assets_dir)
    os.makedirs(assets_dir, exist_ok=True)

    candidates = [
        ("dist/libHeroesLore5.so", "libHeroesLore5.so"),
        ("reference/extracted_apk/lib/armeabi/libHeroesLore5.so", "libHeroesLore5.so"),
        ("dist/data.vfs.mp3", "data.vfs.mp3"),
        ("reference/extracted_apk/assets/data.vfs.mp3", "data.vfs.mp3"),
        ("data/dict.bin", "dict.bin"),
        ("dist/dict.bin", "dict.bin"),
    ]

    for src, dst in candidates:
        if os.path.exists(src) and not os.path.exists(os.path.join(assets_dir, dst)):
            shutil.copy2(src, os.path.join(assets_dir, dst))

    if os.path.exists("data"):
        shutil.copytree("data", os.path.join(assets_dir, "data"), dirs_exist_ok=True)
    if os.path.exists("assets_override"):
        shutil.copytree("assets_override", os.path.join(assets_dir, "assets_override"), dirs_exist_ok=True)
    if os.path.exists("dist/c"):
        shutil.copytree("dist/c", os.path.join(assets_dir, "c"), dirs_exist_ok=True)

    manifest = []
    for root, dirs, files in os.walk(assets_dir):
        for f in files:
            full = os.path.join(root, f)
            rel = os.path.relpath(full, assets_dir).replace("\\", "/")
            if rel != "assets_list.txt":
                manifest.append(f"{rel}:{os.path.getsize(full)}")
    with open(os.path.join(assets_dir, "assets_list.txt"), "w", encoding="utf-8") as mf:
        mf.write("\n".join(manifest) + "\n")

    print(f"[OK] Android assets preparados com sucesso em: {assets_dir} ({len(manifest)} arquivos)")

if __name__ == "__main__":
    prepare_android_assets()
