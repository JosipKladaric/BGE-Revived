"""Determine Blender's world equirectangular mapping using banded probe images.

Probe A: four quadrants (tells whether u and v are above/below 0.5).
Probe B: eight vertical bands (pins u).
Probe C: eight horizontal bands (pins v).
"""
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

ROOT = Path(r"C:\Users\Notebook\Desktop\Blender_Game_Engine_Revived")
sys.path.insert(0, str(ROOT / "tools"))
from make_blender_test_scene import write_hdr  # noqa: E402

WIDTH, HEIGHT = 512, 256
COLORS = {
    "R": (1.0, 0.0, 0.0), "G": (0.0, 1.0, 0.0), "B": (0.0, 0.0, 1.0),
    "Y": (1.0, 1.0, 0.0), "C": (0.0, 1.0, 1.0), "M": (1.0, 0.0, 1.0),
    "K": (1.0, 1.0, 1.0), "D": (0.15, 0.15, 0.15),
}


def build_quadrants():
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH]
    u = xs / (WIDTH - 1.0)
    v = ys / (HEIGHT - 1.0)
    rgb = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float32)
    rgb[(u < 0.5) & (v < 0.5)] = COLORS["R"]
    rgb[(u >= 0.5) & (v < 0.5)] = COLORS["G"]
    rgb[(u < 0.5) & (v >= 0.5)] = COLORS["B"]
    rgb[(u >= 0.5) & (v >= 0.5)] = COLORS["Y"]
    return rgb


def build_u_bands():
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH]
    u = xs / (WIDTH - 1.0)
    rgb = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float32)
    keys = ["R", "G", "B", "Y", "C", "M", "K", "D"]
    for i, key in enumerate(keys):
        rgb[(u >= i / 8.0) & (u < (i + 1) / 8.0)] = COLORS[key]
    return rgb


def build_v_bands():
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH]
    v = ys / (HEIGHT - 1.0)
    rgb = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float32)
    keys = ["R", "G", "B", "Y", "C", "M", "K", "D"]
    for i, key in enumerate(keys):
        rgb[(v >= i / 8.0) & (v < (i + 1) / 8.0)] = COLORS[key]
    return rgb


def classify(colour, keys):
    best = min(keys, key=lambda k: sum((a - b) ** 2 for a, b in zip(COLORS[k], colour)))
    return best if sum((a - b) ** 2 for a, b in zip(COLORS[best], colour)) < 0.25 else "?"


def render_directions(rgb, tag):
    path = ROOT / "build" / f"probe_{tag}.hdr"
    write_hdr(path, rgb)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    world = bpy.data.worlds.new("Probe")
    world.use_nodes = True
    scene.world = world
    tree = world.node_tree
    background = tree.nodes.get("Background")
    tex = tree.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(str(path))
    coords = tree.nodes.new("ShaderNodeTexCoord")
    tree.links.new(coords.outputs["Generated"], tex.inputs["Vector"])
    tree.links.new(tex.outputs["Color"], background.inputs["Color"])

    scene.render.engine = "CYCLES"
    scene.cycles.samples = 1
    scene.render.resolution_x = 8
    scene.render.resolution_y = 4
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "Raw"

    bpy.ops.object.camera_add()
    cam = bpy.context.object
    scene.camera = cam

    directions = {
        "+X": (1, 0, 0), "-X": (-1, 0, 0), "+Y": (0, 1, 0), "-Y": (0, -1, 0),
        "+Z": (0, 0, 1), "-Z": (0, 0, -1),
        "+XY": (0.707, 0.707, 0), "-XY": (-0.707, -0.707, 0),
        "+XZ": (0.707, 0, 0.707), "+ZY": (0, 0.707, 0.707),
    }
    results = {}
    for label, d in directions.items():
        direction = Vector(d)
        cam.location = (0, 0, 0)
        cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(ROOT / "build" / f"probe_{tag}_{label}_")
        bpy.ops.render.render(write_still=True)
        out = str(ROOT / "build" / f"probe_{tag}_{label}_")
        if not out.lower().endswith(".png"):
            out += ".png"
        image = bpy.data.images.load(out, check_existing=False)
        w, h = image.size
        pixels = np.empty(w * h * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        pixels = pixels.reshape(h, w, 4)
        centre = tuple(float(v) for v in pixels[h // 2, w // 2, :3])
        results[label] = centre
    return results


quad = render_directions(build_quadrants(), "quad")
ubands = render_directions(build_u_bands(), "uband")
vbands = render_directions(build_v_bands(), "vband")

print("MAP  direction   quadrant  u-band  v-band")
for label in quad:
    q = classify(quad[label], ["R", "G", "B", "Y"])
    u = classify(ubands[label], ["R", "G", "B", "Y", "C", "M", "K", "D"])
    v = classify(vbands[label], ["R", "G", "B", "Y", "C", "M", "K", "D"])
    u_index = ["R", "G", "B", "Y", "C", "M", "K", "D"].index(u) if u in "RGBYCMKD" else -1
    v_index = ["R", "G", "B", "Y", "C", "M", "K", "D"].index(v) if v in "RGBYCMKD" else -1
    print(f"MAP  {label:4s}  {q}  u={u}({u_index}) v={v}({v_index})")
