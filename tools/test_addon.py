"""Exercise the BGE Revived Blender add-on from a background Blender session.

    blender --background --factory-startup --python tools/test_addon.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import bpy

ROOT = Path(r"C:\Users\Notebook\Desktop\Blender_Game_Engine_Revived")
ADDON = ROOT / "tools" / "blender_addon"
OUTPUT = ROOT / "build" / "addon_test"

sys.path.insert(0, str(ADDON))
bpy.ops.preferences.addon_enable(module="bge_revival")
print("[TEST] addon enabled")

prefs = bpy.context.preferences.addons["bge_revival"].preferences
prefs.engine_root = str(ROOT)
prefs.export_folder = str(OUTPUT)
prefs.headless = True

# A small scene so the export has something to write.
bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 1.0))
bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=(3.0, 0.0, 1.0))
bpy.ops.object.light_add(type="SUN", location=(4.0, -4.0, 8.0))
bpy.ops.object.camera_add(location=(8.0, -8.0, 6.0))
bpy.context.scene.camera = bpy.context.scene.objects[-1]

result = bpy.ops.bge.export_scene()
print("[TEST] export result:", result)
manifest = OUTPUT / "scene.bgescene"
assert manifest.is_file(), "the manifest was not written"
print("[TEST] manifest:", manifest, manifest.stat().st_size, "bytes")

result = bpy.ops.bge.package()
print("[TEST] package result:", result)
packaged = OUTPUT.parent / "Scene"
print("[TEST] packaged folder:", packaged)
assert packaged.is_dir(), "the package folder was not created"
for item in sorted(packaged.iterdir()):
    print("   ", item.name)

# The live link: play, then check that the engine is running and stops again.
result = bpy.ops.bge.play()
print("[TEST] play result:", result)
import time

time.sleep(3.0)
import bge_revival

print("[TEST] engine running:", bool(getattr(bge_revival, "_state", {}).get("process")))
bpy.ops.bge.stop()
print("[TEST] stop result:", bpy.ops.bge.stop())

# The preview: the engine renders and the add-on mirrors the frame.
result = bpy.ops.bge.preview_start()
print("[TEST] preview start:", result)
time.sleep(4.0)
frame = OUTPUT / "preview" / "frame.png"
print("[TEST] preview frame:", frame, frame.is_file(), frame.stat().st_size if frame.is_file() else 0)
# In background mode Blender does not run app timers, so drive the mirror by hand.
bge_revival._update_preview_image()
image = getattr(bge_revival, "_state", {}).get("preview_image")
print("[TEST] mirrored image:", image.name if image else None,
      tuple(image.size) if image else None)
bpy.ops.bge.preview_stop()
print("[TEST] preview stopped:", bpy.ops.bge.preview_stop())
print("[TEST] OK")
