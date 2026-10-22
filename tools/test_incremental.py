"""Exercise the incremental live link: move an object and watch the engine follow.

    blender --background --factory-startup --python tools/test_incremental.py
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import bpy

ROOT = Path(r"C:\Users\Notebook\Desktop\Blender_Game_Engine_Revived")
ADDON = ROOT / "tools" / "blender_addon"
OUTPUT = ROOT / "build" / "incremental_test"

bpy.ops.wm.read_factory_settings(use_empty=True)
sys.path.insert(0, str(ADDON))
bpy.ops.preferences.addon_enable(module="bge_revival")
print("[TEST] addon enabled")

prefs = bpy.context.preferences.addons["bge_revival"].preferences
prefs.engine_root = str(ROOT)
prefs.export_folder = str(OUTPUT)
prefs.preview_port = 8793

bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 1.0))
cube = bpy.context.object
cube.name = "Cube"
bpy.ops.object.light_add(type="SUN", location=(4.0, -4.0, 8.0))
bpy.ops.object.camera_add(location=(6.0, -6.0, 4.0))
camera = bpy.context.scene.objects[-1]
from mathutils import Vector

camera.rotation_euler = (Vector((0.0, 0.0, 1.0)) - camera.location).to_track_quat("-Z", "Y").to_euler()
bpy.context.scene.camera = camera

import bge_revival

client = bge_revival._engine_client()
print("[TEST] engine client:", client is not None)
assert client is not None
time.sleep(0.5)

# The engine knows the scene; now move the cube and push only the transform.
before = client.request_frame()
cube.location = (3.0, 0.0, 1.0)
bpy.context.view_layer.update()
pushed = bge_revival._push_incremental(["Cube"])
print("[TEST] incremental push:", pushed)
print("[TEST] pushed state:", bge_revival._state.get("pushed"))
after = client.request_frame()
print("[TEST] the frame changed:", before[2] != after[2])

# A second move must be sent as a transform only (no new geometry).
bge_revival._state["pushed"]["Cube"]["matrix"] = None
cube.location = (-3.0, 0.0, 1.0)
bpy.context.view_layer.update()
second = bge_revival._push_incremental(["Cube"])
print("[TEST] second push:", second)
final = client.request_frame()
print("[TEST] the frame changed again:", after[2] != final[2])
print("[TEST] OK")
