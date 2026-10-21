#!/usr/bin/env python3
"""Package an exported Blender scene as a playable game.

    python tools/package_game.py --scene build/my_game/scene.bgescene --out dist/MyGame

The result is a self contained folder:

    dist/MyGame/MyGame.exe     the game runtime
    dist/MyGame/game.bgescene  the scene
    dist/MyGame/textures/      the textures the scene references
    dist/MyGame/assets/        shaders and other engine assets
    dist/MyGame/startup.py     optional Python entry point

Add --zip to also produce a distributable archive.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "bin"


def read_scene(scene_path: Path) -> dict:
    try:
        return json.loads(scene_path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise SystemExit(f"could not read the scene manifest: {exc}")


def collect_assets(scene: dict, scene_dir: Path) -> list[Path]:
    """Find every file the scene references, so the package is self contained."""
    files: list[Path] = []
    for material in scene.get("materials", []):
        for key in ("albedo", "normal"):
            value = material.get(key)
            if not value:
                continue
            path = Path(value)
            if not path.is_absolute():
                path = scene_dir / path
            if path.is_file():
                files.append(path)
    world = scene.get("world", {})
    value = world.get("environment_texture")
    if value:
        path = Path(value)
        if not path.is_absolute():
            path = scene_dir / path
        if path.is_file():
            files.append(path)
    return files


def copy_engine_assets(destination: Path) -> None:
    source = ROOT / "assets"
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)


def copy_runtime(destination: Path, name: str) -> Path:
    exe = BIN / ("bge_game.exe" if os.name == "nt" else "bge_game")
    if not exe.is_file():
        raise SystemExit(f"game runtime not found at {exe}; run `python build.py` first")
    target = destination / f"{name}{exe.suffix}"
    shutil.copy2(exe, target)

    # The embedded Python host needs the interpreter next to the executable.
    for dll in ("python312.dll", "python3.dll"):
        source = BIN / dll
        if source.is_file():
            shutil.copy2(source, destination / dll)
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a BGE Revived game")
    parser.add_argument("--scene", required=True, help="the exported .bgescene file")
    parser.add_argument("--out", required=True, help="output folder")
    parser.add_argument("--name", default=None, help="game name (default: the scene name)")
    parser.add_argument("--script", default=None, help="optional startup Python script")
    parser.add_argument("--zip", action="store_true", help="also create a zip archive")
    parser.add_argument("--clean", action="store_true", help="empty the output folder first")
    args = parser.parse_args()

    scene_path = Path(args.scene).resolve()
    if not scene_path.is_file():
        raise SystemExit(f"scene not found: {scene_path}")

    scene = read_scene(scene_path)
    name = args.name or scene.get("name") or scene_path.parent.name or "Game"
    safe_name = "".join(c if c.isalnum() or c in "-_ " else "_" for c in name).strip() or "Game"

    out = Path(args.out).resolve()
    if args.clean and out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    # The scene and everything it needs.
    scene_dir = scene_path.parent
    shutil.copy2(scene_path, out / "game.bgescene")
    binary = scene_dir / "scene.bin"
    if binary.is_file():
        shutil.copy2(binary, out / "scene.bin")

    textures = out / "textures"
    for asset in collect_assets(scene, scene_dir):
        relative = asset.relative_to(scene_dir) if asset.is_relative_to(scene_dir) else Path(asset.name)
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(asset, target)

    copy_engine_assets(out / "assets")
    executable = copy_runtime(out, safe_name)

    if args.script:
        script = Path(args.script).resolve()
        if script.is_file():
            shutil.copy2(script, out / "startup.py")

    readme = out / "README.txt"
    readme.write_text(
        f"{safe_name}\n"
        f"{'=' * len(safe_name)}\n\n"
        f"Run {executable.name} to play. The game loads game.bgescene from this folder.\n"
        f"Press Escape to quit.\n",
        encoding="utf-8",
    )

    print(f"Packaged '{safe_name}' -> {out}")
    for item in sorted(out.iterdir()):
        print(f"  {item.name}")

    if args.zip:
        archive = out.with_suffix(".zip")
        if archive.exists():
            archive.unlink()
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
            for item in sorted(out.rglob("*")):
                if item.is_file():
                    zf.write(item, item.relative_to(out.parent))
        print(f"  archive: {archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
