"""Check that the Blender preview reads frames through shared memory.

    blender --background --factory-startup --python tools/test_shared_frame_blender.py
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parent.parent
ADDON = ROOT / "tools" / "blender_addon"
OUTPUT = ROOT / "build" / "shm_blender_test"

bpy.ops.wm.read_factory_settings(use_empty=True)
sys.path.insert(0, str(ADDON))
bpy.ops.preferences.addon_enable(module="bge_revival")

prefs = bpy.context.preferences.addons["bge_revival"].preferences
prefs.engine_root = str(ROOT)
prefs.export_folder = str(OUTPUT)
prefs.preview_port = 8801

bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 1.0))
bpy.ops.object.light_add(type="SUN", location=(4.0, -4.0, 8.0))
bpy.ops.object.camera_add(location=(6.0, -6.0, 4.0))
camera = bpy.context.scene.objects[-1]
from mathutils import Vector  # noqa: E402

camera.rotation_euler = (Vector((0.0, 0.0, 1.0)) - camera.location).to_track_quat("-Z", "Y").to_euler()
bpy.context.scene.camera = camera

import bge_revival  # noqa: E402

client = bge_revival._engine_client()
print("[TEST] engine client:", client is not None)
assert client is not None
print("[TEST] shared memory transport:", client.has_shared)
assert client.has_shared, "the engine did not offer a shared frame buffer"

# Let the engine render a few frames, then pull them the way the live preview
# does: no request, just the newest frame from shared memory.
frames = 0
start = time.perf_counter()
updated = 0
while time.perf_counter() - start < 5.0 and updated < 10:
    before = bge_revival._state.get("preview_stamp")
    bge_revival._update_preview_image()
    if bge_revival._state.get("preview_stamp") != before:
        updated += 1
    frames += 1
    time.sleep(0.01)

image = bge_revival._state.get("preview_image")
print(f"[TEST] updates: {updated}, ticks: {frames}")
print("[TEST] preview image:", None if image is None else tuple(image.size))
assert image is not None and image.has_data, "the preview image never received a frame"
assert updated > 0, "the preview never updated"

# A second call without a new engine frame must not touch the image again.
stamp = bge_revival._state.get("preview_stamp")
bge_revival._update_preview_image()
print("[TEST] idle call kept the image:", bge_revival._state.get("preview_stamp") == stamp)

bge_revival._stop_engine()
print("[TEST] OK")
