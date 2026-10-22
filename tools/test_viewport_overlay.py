"""Interactive check of the viewport overlay: start the preview, switch the 3D View
to the camera and take a screenshot of the Blender window.

    blender --factory-startup --python tools/test_viewport_overlay.py
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import bpy

ROOT = Path(r"C:\Users\Notebook\Desktop\Blender_Game_Engine_Revived")
ADDON = ROOT / "tools" / "blender_addon"
OUTPUT = ROOT / "build" / "viewport_test"

sys.path.insert(0, str(ADDON))
bpy.ops.preferences.addon_enable(module="bge_revival")

prefs = bpy.context.preferences.addons["bge_revival"].preferences
prefs.engine_root = str(ROOT)
prefs.export_folder = str(OUTPUT)
prefs.preview_port = 8794
prefs.preview_mode = "CORNER"

bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 1.0))
bpy.ops.object.light_add(type="SUN", location=(4.0, -4.0, 8.0))
bpy.ops.object.camera_add(location=(6.0, -6.0, 4.0))
camera = bpy.context.scene.objects[-1]
from mathutils import Vector

camera.rotation_euler = (Vector((0.0, 0.0, 1.0)) - camera.location).to_track_quat("-Z", "Y").to_euler()
bpy.context.scene.camera = camera

bpy.ops.bge.preview_start()
print("[TEST] preview started")

start = time.time()
state = {"switched": False}


def step():
    """Lets the preview run, then switches to the camera view and captures."""
    if not state["switched"] and time.time() - start > 8.0:
        state["switched"] = True
        for area in bpy.context.window.screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.region_3d.view_perspective = "CAMERA"
        print("[TEST] switched to the camera view")
        return 2.0

    if state["switched"] and time.time() - start > 14.0:
        OUTPUT.mkdir(parents=True, exist_ok=True)
        bpy.ops.screen.screenshot(filepath=str(OUTPUT / "viewport_overlay.png"))
        print("[TEST] screenshot written")
        return None
    return 0.5


bpy.app.timers.register(step, first_interval=1.0)
