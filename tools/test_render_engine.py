"""Render a scene with the BGE Revived render engine from a background Blender.

    blender --background --factory-startup --python tools/test_render_engine.py

This exercises the whole path: the add-on registers a render engine, the engine
is selected, the scene is exported, the background engine renders it and the
result is written to disk.
"""
from __future__ import annotations

import sys
from pathlib import Path

import bpy

ROOT = Path(r"C:\Users\Notebook\Desktop\Blender_Game_Engine_Revived")
ADDON = ROOT / "tools" / "blender_addon"
OUTPUT = ROOT / "build" / "render_engine_test"

sys.path.insert(0, str(ADDON))
bpy.ops.preferences.addon_enable(module="bge_revival")
print("[TEST] addon enabled")

prefs = bpy.context.preferences.addons["bge_revival"].preferences
prefs.engine_root = str(ROOT)
prefs.export_folder = str(OUTPUT)
prefs.preview_port = 8791

# A small scene with a couple of objects and a light.
bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 1.0))
bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=(3.0, 0.0, 1.0))
bpy.ops.object.light_add(type="SUN", location=(4.0, -4.0, 8.0))
bpy.ops.object.camera_add(location=(7.0, -7.0, 5.0))
bpy.context.scene.camera = bpy.context.scene.objects[-1]

scene = bpy.context.scene
scene.render.engine = "BGE_REVIVED"
scene.render.resolution_x = 640
scene.render.resolution_y = 360
scene.render.resolution_percentage = 100
scene.render.filepath = str(OUTPUT / "bge_render.png")
scene.render.image_settings.file_format = "PNG"

print("[TEST] engine:", scene.render.engine)
result = bpy.ops.render.render(write_still=True)
print("[TEST] render result:", result)

image = OUTPUT / "bge_render.png"
print("[TEST] output:", image, image.is_file(), image.stat().st_size if image.is_file() else 0)
assert image.is_file(), "the render engine did not produce an image"
print("[TEST] OK")
