#!/usr/bin/env python3
"""
Prepara a pasta AppDir/ para o empacotamento em AppImage no Linux.
"""
import os
import sys
import shutil
import subprocess

def prepare_appdir(binary_path=None, appdir_path="AppDir"):
    if binary_path is None:
        if os.path.exists("build/hl5"):
            binary_path = "build/hl5"
        elif os.path.exists("build/hl5.exe"):
            binary_path = "build/hl5.exe"
        else:
            binary_path = "build/hl5"

    if not os.path.exists(binary_path):
        print(f"[ERRO] Binario compilado nao encontrado em: {binary_path}")
        sys.exit(1)

    if os.path.exists(appdir_path):
        shutil.rmtree(appdir_path)

    os.makedirs(f"{appdir_path}/usr/bin", exist_ok=True)
    os.makedirs(f"{appdir_path}/usr/lib", exist_ok=True)

    print("[AppImage] Copiando binario executavel...")
    dest_bin = f"{appdir_path}/usr/bin/heroes_lore_5"
    shutil.copy2(binary_path, dest_bin)
    os.chmod(dest_bin, 0o755)

    print("[AppImage] Copiando metadados de desktop e icones...")
    if os.path.exists("dist_linux/heroes_lore_5.desktop"):
        shutil.copy2("dist_linux/heroes_lore_5.desktop", f"{appdir_path}/heroes_lore_5.desktop")
    if os.path.exists("dist_linux/heroes_lore_5.png"):
        shutil.copy2("dist_linux/heroes_lore_5.png", f"{appdir_path}/heroes_lore_5.png")
        shutil.copy2("dist_linux/heroes_lore_5.png", f"{appdir_path}/.DirIcon")

    if os.path.exists("dist_linux/AppRun"):
        shutil.copy2("dist_linux/AppRun", f"{appdir_path}/AppRun")
        os.chmod(f"{appdir_path}/AppRun", 0o755)

    print("[AppImage] Copiando arquivos do jogo...")
    shutil.copytree("assets", os.path.join(appdir_path, "assets"), dirs_exist_ok=True)

    candidates = [
        ("dist/libHeroesLore5.so", "libHeroesLore5.so"),
        ("reference/extracted_apk/lib/armeabi/libHeroesLore5.so", "libHeroesLore5.so"),
        ("dist/data.vfs.mp3", "data.vfs.mp3"),
        ("reference/extracted_apk/assets/data.vfs.mp3", "data.vfs.mp3"),
        ("data/dict.bin", "dict.bin"),
        ("dist/dict.bin", "dict.bin"),
    ]
    for src, dst in candidates:
        if os.path.exists(src) and not os.path.exists(os.path.join(appdir_path, dst)):
            shutil.copy2(src, os.path.join(appdir_path, dst))

    if os.path.exists("data"):
        os.makedirs(os.path.join(appdir_path, "data"), exist_ok=True)
        if os.path.isfile("data/dict.bin"):
            shutil.copy2("data/dict.bin", os.path.join(appdir_path, "data", "dict.bin"))
    if os.path.exists("assets_override"):
        shutil.copytree("assets_override", os.path.join(appdir_path, "assets_override"), dirs_exist_ok=True)
    if os.path.exists("dist/c"):
        shutil.copytree("dist/c", os.path.join(appdir_path, "c"), dirs_exist_ok=True)

    if sys.platform.startswith("linux"):
        print("[AppImage] Analisando dependencias com ldd...")
        try:
            out = subprocess.check_output(["ldd", binary_path], text=True)
            core_system_libs = {
                "libc.so", "libm.so", "libdl.so", "libpthread.so", "librt.so",
                "ld-linux", "libresolv.so", "libutil.so"
            }
            copied = set()
            for line in out.splitlines():
                if "=>" in line:
                    parts = line.strip().split("=>")
                    lib_name = parts[0].strip()
                    lib_target = parts[1].strip().split(" ")[0].strip()
                    if lib_target and os.path.exists(lib_target):
                        if any(core in lib_name for core in core_system_libs):
                            continue
                        if lib_target not in copied:
                            real_path = os.path.realpath(lib_target)
                            target_dest = f"{appdir_path}/usr/lib/{os.path.basename(lib_target)}"
                            shutil.copy2(real_path, target_dest)
                            copied.add(lib_target)
        except Exception as e:
            print(f"[Aviso] Falha ldd: {e}")

    print(f"[OK] AppDir preparado com sucesso em: {appdir_path}")

if __name__ == "__main__":
    prepare_appdir()
