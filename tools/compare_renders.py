#!/usr/bin/env python3
"""Compare two renders block by block (run with Blender's Python for numpy).

    blender --background --python tools/compare_renders.py -- reference.png candidate.png

Prints a coarse colour grid for both images plus the per-block average
difference, which makes lighting/exposure drift obvious at a glance.
"""
from __future__ import annotations

import sys

import bpy
import numpy as np


def load_rgb(path: str) -> np.ndarray:
    image = bpy.data.images.load(path)
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    pixels = pixels.reshape(height, width, 4)
    # Blender stores row 0 at the bottom; flip to top-left origin.
    return pixels[::-1, :, :3]


def blocks(image: np.ndarray, rows: int, cols: int) -> np.ndarray:
    height, width, _ = image.shape
    out = np.zeros((rows, cols, 3), dtype=np.float32)
    for r in range(rows):
        for c in range(cols):
            y0 = r * height // rows
            y1 = (r + 1) * height // rows
            x0 = c * width // cols
            x1 = (c + 1) * width // cols
            out[r, c] = image[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)
    return out


def describe(name: str, grid: np.ndarray) -> None:
    print(f"--- {name} ---")
    for row in grid:
        print("  " + "  ".join(f"({r:0.2f},{g:0.2f},{b:0.2f})" for r, g, b in row))


def main() -> None:
    argv = sys.argv
    argv = argv[argv.index("--") + 1 :] if "--" in argv else []
    if len(argv) < 2:
        raise SystemExit("usage: compare_renders.py reference.png candidate.png")
    reference = load_rgb(argv[0])
    candidate = load_rgb(argv[1])

    rows, cols = 4, 8
    ref_grid = blocks(reference, rows, cols)
    cand_grid = blocks(candidate, rows, cols)
    describe(f"reference {argv[0]}", ref_grid)
    describe(f"candidate {argv[1]}", cand_grid)
    diff = np.abs(ref_grid - cand_grid)
    print(f"--- mean absolute difference: {diff.mean():.4f} (max {diff.max():.4f}) ---")


if __name__ == "__main__":
    main()
