#!/usr/bin/env python3

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ANDROID_ROOT_NAME = "lvk_xr"


def adb_output(*args: str) -> str:
    result = subprocess.run(["adb", *args], check=True, capture_output=True, text=True)
    return result.stdout.strip()


def adb(*args: str) -> None:
    subprocess.run(["adb", *args], check=True)


def push(path: Path, dst_root: str) -> None:
    relative = path.relative_to(ROOT).as_posix()
    dst = f"{dst_root}/{relative}"
    if path.is_dir():
        print(f"Copying {relative}/ -> {dst}/")
        adb("shell", "mkdir", "-p", dst)
        for child in path.iterdir():
            push_to(child, f"{dst}/{child.name}")
    else:
        print(f"Copying {relative} -> {dst}")
        adb("push", str(path), dst)


def push_to(path: Path, dst: str) -> None:
    if path.is_dir():
        adb("shell", "mkdir", "-p", dst)
        for child in path.iterdir():
            push_to(child, f"{dst}/{child.name}")
    else:
        adb("push", str(path), dst)


def push_if_exists(relative: str, dst_root: str) -> None:
    path = ROOT / relative
    if path.exists():
        push(path, dst_root)
    else:
        print(f"Skipping missing optional cache: {relative}")


def main() -> None:
    external_storage = adb_output("shell", "echo", "$EXTERNAL_STORAGE") or "/sdcard"
    dst_root = f"{external_storage}/{ANDROID_ROOT_NAME}"

    adb("shell", "mkdir", "-p", dst_root)
    adb("shell", "mkdir", "-p", f"{dst_root}/.cache/out_textures_11_astc")

    android_cache_paths = [
        ".cache/ch11_bistro_android.meshes",
        ".cache/ch11_bistro_android.materials",
        ".cache/ch11_bistro_android.scene",
        ".cache/out_textures_11_astc",
    ]

    for relative in [
        "data",
        "Renderer/src",
        "Renderer/shaders",
        #"deps/src/",
    ]:
        push(ROOT / relative, dst_root)

    # for relative in android_cache_paths:
    #     push_if_exists(relative, dst_root)

    print("Done. The Android app uses this as its working directory:")
    print(dst_root)


if __name__ == "__main__":
    main()
