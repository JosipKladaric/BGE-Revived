#!/usr/bin/env python3
"""End-to-end Blender parity check.

Runs the whole Blender -> BGE Revived pipeline and reports how close the engine
render is to Blender's own render:

    python tools/parity_check.py                 # build the scene, export, render
    python tools/parity_check.py --reference     # also render Blender (slow, Cycles)
    python tools/parity_check.py --mode env      # environment / normal / alpha scene

The script never fails the build on a small colour difference; it prints the mean
absolute difference so the numbers can be tracked over time.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
BLENDER = Path(r"C:\Program Files\Blender Foundation\Blender 4.4\blender.exe")


def run(cmd: list[str], cwd: Path = ROOT) -> subprocess.CompletedProcess:
    print("  $", " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)


def blender_available() -> bool:
    return BLENDER.is_file()


def make_scene(args) -> Path:
    blend = BUILD / f"blender_{args.mode}.blend"
    blend.parent.mkdir(parents=True, exist_ok=True)
    result = run([str(BLENDER), "--background", "--factory-startup",
                  "--python", str(ROOT / "tools" / "make_blender_test_scene.py"),
                  "--", "--out", str(blend), "--mode", args.mode])
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise SystemExit("failed to generate the Blender test scene")
    return blend


def export_scene(blend: Path, mode: str) -> Path:
    out = BUILD / f"blender_{mode}_scene"
    if out.exists():
        shutil.rmtree(out)
    result = run([str(BLENDER), "--background", str(blend),
                  "--python", str(ROOT / "tools" / "bge_export.py"), "--", "--out", str(out)])
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise SystemExit("failed to export the scene")
    return out / "scene.bgescene"


def build_engine() -> None:
    result = run([sys.executable, str(ROOT / "build.py")])
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise SystemExit("engine build failed")


def render_engine(scene: Path, width: int, height: int, output: Path) -> None:
    exe = BUILD / "bin" / "bge_sandbox.exe"
    if not exe.is_file():
        raise SystemExit("sandbox binary missing; run `python build.py` first")
    result = run([str(exe), "--scene", str(scene), "--headless",
                  "--width", str(width), "--height", str(height),
                  "--frames", "2", "--screenshot", str(output)])
    print(result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "")


def render_blender_reference(blend: Path, width: int, height: int, output: Path) -> None:
    expression = (
        "import bpy; s=bpy.context.scene; s.cycles.samples=24; s.cycles.use_denoising=True; "
        f"s.render.resolution_x={width}; s.render.resolution_y={height}; "
        "s.render.resolution_percentage=100; s.render.image_settings.file_format='PNG'"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    run([str(BLENDER), "--background", str(blend), "-E", "CYCLES",
         "--python-expr", expression, "-o", str(output.with_suffix("")), "-f", "1"])


def compare(reference: Path, candidate: Path) -> None:
    if not reference.is_file():
        print("  (no reference render to compare against)")
        return
    result = run([str(BLENDER), "--background", "--python",
                  str(ROOT / "tools" / "compare_renders.py"),
                  "--", str(reference), str(candidate)])
    for line in result.stdout.splitlines():
        if line.startswith("--- mean") or line.startswith("--- reference") or line.startswith("--- candidate"):
            print(" ", line)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", default="full", choices=["full", "env"])
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=512)
    parser.add_argument("--reference", action="store_true", help="also render the Blender reference")
    parser.add_argument("--no-build", action="store_true", help="skip rebuilding the engine")
    args = parser.parse_args()

    if not blender_available():
        raise SystemExit(f"Blender not found at {BLENDER}")

    print("1/4 generating the Blender test scene")
    blend = make_scene(args)
    print("2/4 exporting to the .bgescene format")
    scene = export_scene(blend, args.mode)
    print("3/4 rendering with the engine")
    if not args.no_build:
        build_engine()
    engine_png = BUILD / f"parity_{args.mode}_engine.png"
    render_engine(scene, args.width, args.height, engine_png)
    print("4/4 comparing")
    reference_png = BUILD / f"parity_{args.mode}_blender.png"
    if args.reference:
        render_blender_reference(blend, args.width, args.height, reference_png)
    compare(reference_png, engine_png)
    print(f"engine render: {engine_png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
