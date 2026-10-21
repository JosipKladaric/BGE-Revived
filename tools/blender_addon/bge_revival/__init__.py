"""BGE Revived - author in Blender, play and ship with the BGE Revived runtime.

Install this folder through Blender's Preferences > Add-ons > Install from Disk
(pick the ``bge_revival`` folder or a zip of it), then enable "Game: BGE Revived".

What it gives you:

* **BGE Revived as a render engine.** Pick it next to Cycles and EEVEE in the
  render properties and press F12: the scene is rendered by the game engine
  itself, so what you see is what the game shows.
* **A live viewport preview.** The engine renders in the background and the
  frames are mirrored into Blender, as a picture in picture in the 3D View.
* **Play and ship.** "Play" runs the game on the scene you are editing, and
  "Package Game" builds a distributable game folder.

The panel lives in the 3D View sidebar (N).
"""
from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path

import bpy
from bpy.props import BoolProperty, IntProperty, StringProperty
from bpy.types import AddonPreferences, Operator, Panel

bl_info = {
    "name": "Game: BGE Revived",
    "author": "BGE Revived",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > BGE Revived",
    "description": "Render, play and ship with the BGE Revived engine",
    "category": "Game",
}

DEFAULT_PORT = 8765

# Module level state, kept between operator calls.
_state = {
    "process": None,            # the running game (Play)
    "engine_process": None,     # the background engine used for previews
    "client": None,             # socket connection to the background engine
    "dirty": False,             # the scene changed and needs a re-export
    "timer": None,              # the live update timer
    "engine_root": "",
    "preview_dir": "",
    "preview_image": None,      # the mirrored frame inside Blender
    "preview_stamp": 0.0,
    "preview_texture": None,
    "preview_texture_stamp": None,
    "preview_enabled": False,
    "overlay_handle": None,
    "live_target": "",
}


# --------------------------------------------------------------- helpers ----

def find_engine_root(preferred: str = "") -> Path:
    """Locate the BGE Revived repository (the folder holding tools/bge_export.py)."""
    if preferred:
        candidate = Path(preferred).expanduser()
        if (candidate / "tools" / "bge_export.py").is_file():
            return candidate.resolve()

    here = Path(__file__).resolve()
    for parent in list(here.parents)[:6]:
        if (parent / "tools" / "bge_export.py").is_file():
            return parent

    if bpy.data.filepath:
        blend_dir = Path(bpy.data.filepath).resolve().parent
        for parent in list(blend_dir.parents)[:4]:
            if (parent / "tools" / "bge_export.py").is_file():
                return parent
    return Path("").resolve()


def _tools_dir(root: Path) -> str:
    path = str(root / "tools")
    if path not in sys.path:
        sys.path.insert(0, path)
    return path


def load_exporter(root: Path):
    """Import tools/bge_export.py from the engine repository."""
    path = root / "tools" / "bge_export.py"
    if not path.is_file():
        raise RuntimeError(f"bge_export.py not found in {root}")
    module_name = "bge_revival_exporter"
    module = sys.modules.get(module_name)
    if module is None:
        spec = importlib.util.spec_from_file_location(module_name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
    return module


def load_client(root: Path):
    """Import tools/preview_client.py, the client for the engine's preview server."""
    _tools_dir(root)
    try:
        import preview_client  # type: ignore
    except ImportError as exc:
        raise RuntimeError(f"the preview client is not available: {exc}")
    return preview_client


class ExportOptions:
    """The small options object the exporter expects."""

    def __init__(self, selected: bool = False, max_texture: int = 2048, no_textures: bool = False):
        self.selected = selected
        self.max_texture = max_texture
        self.no_textures = no_textures


def export_current_scene(root: Path, output: Path, selected_only: bool = False) -> Path:
    exporter = load_exporter(root)
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / "scene.bgescene"
    exporter.export_scene(bpy.context.scene, manifest, ExportOptions(selected=selected_only))
    return manifest


def engine_executable(root: Path) -> Path:
    name = "bge_game.exe" if os.name == "nt" else "bge_game"
    for candidate in (root / "build" / "bin" / name, root / "build" / "cmake" / "bin" / name):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"the game runtime was not found; build it with `python build.py`")


def is_running() -> bool:
    process = _state.get("process")
    return process is not None and process.poll() is None


def live_export_folder(prefs) -> Path:
    """Where live updates are written: the render folder when the engine is up."""
    if _state.get("client") is not None and _state.get("preview_dir"):
        return Path(_state["preview_dir"])
    return Path(prefs.export_folder).expanduser()


# ------------------------------------------------------------ engine link ----

def _engine_client():
    """The shared connection to the background engine, started on demand."""
    client = _state.get("client")
    if client is not None:
        return client

    prefs = bpy.context.preferences.addons["bge_revival"].preferences
    root = find_engine_root(prefs.engine_root)
    if not (root / "tools" / "bge_export.py").is_file():
        return None
    try:
        preview_client = load_client(root)
    except Exception as exc:
        print(f"[BGE] {exc}")
        return None

    folder = Path(prefs.export_folder).expanduser() / "render"
    folder.mkdir(parents=True, exist_ok=True)

    process = _state.get("engine_process")
    if process is None or process.poll() is not None:
        try:
            executable = engine_executable(root)
        except Exception as exc:
            print(f"[BGE] {exc}")
            return None
        command = [str(executable), "--preview-port", str(prefs.preview_port),
                   "--preview", str(folder)]
        try:
            process = subprocess.Popen(command, cwd=str(executable.parent))
        except Exception as exc:
            print(f"[BGE] could not start the render engine: {exc}")
            return None
        _state["engine_process"] = process

    client = preview_client.PreviewClient(port=prefs.preview_port)
    for _ in range(60):
        if client.connect():
            _state["client"] = client
            _state["preview_dir"] = str(folder)
            _state["engine_root"] = str(root)
            return client
        time.sleep(0.1)
    return None


def _stop_engine() -> None:
    client = _state.get("client")
    if client is not None:
        client.close()
        _state["client"] = None
    process = _state.get("engine_process")
    if process is not None and process.poll() is None:
        process.terminate()
    _state["engine_process"] = None
    _state["preview_enabled"] = False
    _remove_overlay()


# ---------------------------------------------------------------- timers ----

def _mark_dirty(scene, depsgraph=None):
    if is_running() or _state.get("client") is not None:
        _state["dirty"] = True


def _srgb_to_linear(data: bytes, width: int, height: int):
    """Converts the engine's sRGB frame into linear floats for Blender."""
    try:
        import numpy as np
        array = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)
        colour = array[:, :, :3].astype(np.float32) / 255.0
        linear = np.where(colour <= 0.04045, colour / 12.92,
                          ((colour + 0.055) / 1.055) ** 2.4)
        out = np.ones((height, width, 4), dtype=np.float32)
        out[:, :, :3] = linear
        return out.ravel()
    except Exception:
        values = []
        for i in range(0, len(data), 4):
            for channel in data[i:i + 3]:
                c = channel / 255.0
                values.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
            values.append(data[i + 3] / 255.0)
        return values


def _update_preview_image():
    """Pull the newest frame from the engine and mirror it into Blender."""
    client = _state.get("client")
    if client is None:
        return
    try:
        client.send({"cmd": "frame"})
        reply, payload = client.receive()
        if reply.get("cmd") != "frame" or not reply.get("ok", False):
            return
        width = int(reply["width"])
        height = int(reply["height"])
        if not payload:
            return

        image = _state.get("preview_image")
        if image is None or tuple(image.size) != (width, height):
            if image is not None:
                bpy.data.images.remove(image)
            image = bpy.data.images.new("BGE Preview", width=width, height=height,
                                        alpha=False, float_buffer=True)
            image.colorspace_settings.name = "Non-Color"  # the pixels are already linear
            _state["preview_image"] = image
        image.pixels.foreach_set(_srgb_to_linear(payload, width, height))
        image.update()
        _state["preview_stamp"] = time.time()

        # Show it in any open image editor.
        screen = bpy.context.screen
        if screen:
            for area in screen.areas:
                if area.type == "IMAGE_EDITOR":
                    area.spaces.active.image = image
    except Exception as exc:
        print(f"[BGE] preview update failed: {exc}")


def _watch_timer():
    """Re-export the scene while the game runs and mirror the engine's frames."""
    if not is_running() and _state.get("client") is None:
        _state["timer"] = None
        return None
    try:
        if _state["dirty"]:
            _state["dirty"] = False
            _state["last_export"] = time.time()
            root = Path(_state["engine_root"] or str(find_engine_root()))
            prefs = bpy.context.preferences.addons["bge_revival"].preferences
            export_current_scene(root, live_export_folder(prefs))
        _update_preview_image()
    except Exception as exc:  # never let a timer error kill Blender
        print(f"[BGE] live export failed: {exc}")
    return 0.05


# --------------------------------------------------- viewport overlay ----
# The rendered frame is drawn in the 3D View so the author can see the game
# while they work. Everything is guarded, because the GPU image API differs
# between Blender versions.

def _draw_preview_overlay():
    if not _state.get("preview_enabled"):
        return
    image = _state.get("preview_image")
    if image is None or not image.has_data:
        return
    region = bpy.context.region
    if region is None:
        return

    try:
        import gpu
        from gpu_extras.batch import batch_for_shader
    except Exception:
        return
    try:
        shader = gpu.shader.from_builtin("IMAGE_COLOR")
    except Exception:
        return

    texture = _state.get("preview_texture")
    if texture is None:
        try:
            texture = image.gl_load()
        except Exception:
            texture = None
        if texture is None:
            try:
                texture = gpu.types.GPUTexture(image.size[:], format="RGBA16F", data=image.pixels[:])
            except Exception:
                return
        _state["preview_texture"] = texture

    prefs = bpy.context.preferences.addons["bge_revival"].preferences
    full = prefs.preview_fullscreen
    if full:
        width = region.width * 0.9
        height = width * (image.size[1] / max(image.size[0], 1))
        if height > region.height * 0.9:
            height = region.height * 0.9
            width = height * (image.size[0] / max(image.size[1], 1))
        x = (region.width - width) * 0.5
        y = (region.height - height) * 0.5
    else:
        width = region.width * 0.28
        height = width * (image.size[1] / max(image.size[0], 1))
        x = region.width - width - 18.0
        y = 18.0

    corners = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    try:
        gpu.state.blend_set("ALPHA")
        batch = batch_for_shader(shader, "TRI_FAN", {"pos": corners, "texCoord": uvs})
        shader.uniform_sampler("image", texture)
        batch.draw(shader)
        gpu.state.blend_set("NONE")
    except Exception:
        return


def _add_overlay() -> None:
    if _state.get("overlay_handle") is not None:
        return
    try:
        _state["overlay_handle"] = bpy.types.SpaceView3D.draw_handler_add(
            _draw_preview_overlay, (), "WINDOW", "POST_PIXEL"
        )
    except Exception:
        _state["overlay_handle"] = None


def _remove_overlay() -> None:
    handle = _state.get("overlay_handle")
    if handle is not None:
        try:
            bpy.types.SpaceView3D.draw_handler_remove(handle, "WINDOW")
        except Exception:
            pass
        _state["overlay_handle"] = None


# ------------------------------------------------------------- rendering ----

def _render_scene_with_engine(width: int, height: int):
    """Exports the scene, asks the engine for a frame and returns (w, h, rgba)."""
    prefs = bpy.context.preferences.addons["bge_revival"].preferences
    root = find_engine_root(prefs.engine_root)
    folder = Path(prefs.export_folder).expanduser() / "render"
    manifest_path = export_current_scene(root, folder)
    manifest = manifest_path.read_text(encoding="utf-8")
    binary_path = folder / "scene.bin"
    binary = binary_path.read_bytes() if binary_path.is_file() else b""

    client = _engine_client()
    if client is None:
        raise RuntimeError("the preview engine is not running")
    client.resize(width, height)
    client.push_scene(manifest, binary)
    frame_width, frame_height, pixels = client.request_frame(width, height)
    return frame_width, frame_height, pixels


class BGE_RenderEngine(bpy.types.RenderEngine):
    """Renders the scene with the BGE Revived engine, inside Blender."""

    bl_idname = "BGE_REVIVED"
    bl_label = "BGE Revived"
    bl_use_preview = True
    bl_use_eevee_viewport = False
    bl_use_postprocess = True
    bl_use_compositing = True
    bl_use_simplify = False

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def render(self, depsgraph):
        scene = bpy.context.scene
        scale = scene.render.resolution_percentage / 100.0
        width = max(1, int(scene.render.resolution_x * scale))
        height = max(1, int(scene.render.resolution_y * scale))

        try:
            frame_width, frame_height, pixels = _render_scene_with_engine(width, height)
        except Exception as exc:
            print(f"[BGE] the render failed: {exc}")
            return

        # Blender expects the result in linear space; the engine writes sRGB.
        linear = _srgb_to_linear(pixels, frame_width, frame_height)
        image = bpy.data.images.get("BGE Render")
        if image is None or tuple(image.size) != (frame_width, frame_height):
            if image is not None:
                bpy.data.images.remove(image)
            image = bpy.data.images.new("BGE Render", width=frame_width, height=frame_height,
                                        alpha=False, float_buffer=True)
        image.pixels.foreach_set(linear)
        image.update()

        result = bpy.data.images.get("Render Result")
        if result is not None:
            try:
                result.pixels.foreach_set(linear)
            except Exception as exc:
                print(f"[BGE] could not write the render result: {exc}")


# ------------------------------------------------------------- operators ----

class BGE_OT_export(Operator):
    bl_idname = "bge.export_scene"
    bl_label = "Export Scene"
    bl_description = "Write the current scene to the BGE Revived .bgescene format"
    bl_options = {"REGISTER"}

    selected_only: BoolProperty(name="Selected only", default=False)

    def execute(self, context):
        prefs = context.preferences.addons["bge_revival"].preferences
        root = find_engine_root(prefs.engine_root)
        if not (root / "tools" / "bge_export.py").is_file():
            self.report({"ERROR"}, "BGE Revived engine folder not found; set it in the add-on preferences")
            return {"CANCELLED"}
        output = Path(prefs.export_folder).expanduser()
        try:
            manifest = export_current_scene(root, output, self.selected_only)
        except Exception as exc:
            self.report({"ERROR"}, f"Export failed: {exc}")
            return {"CANCELLED"}
        self.report({"INFO"}, f"Exported {manifest.name} to {output}")
        return {"FINISHED"}


class BGE_OT_play(Operator):
    bl_idname = "bge.play"
    bl_label = "Play"
    bl_description = "Export the scene and start the engine, keeping it in sync with your edits"
    bl_options = {"REGISTER"}

    def execute(self, context):
        prefs = context.preferences.addons["bge_revival"].preferences
        root = find_engine_root(prefs.engine_root)
        if not (root / "tools" / "bge_export.py").is_file():
            self.report({"ERROR"}, "BGE Revived engine folder not found; set it in the add-on preferences")
            return {"CANCELLED"}

        if is_running():
            self.report({"INFO"}, "The engine is already running")
            return {"FINISHED"}

        output = Path(prefs.export_folder).expanduser()
        try:
            manifest = export_current_scene(root, output)
            executable = engine_executable(root)
        except Exception as exc:
            self.report({"ERROR"}, str(exc))
            return {"CANCELLED"}

        command = [str(executable), "--scene", str(manifest), "--watch"]
        if prefs.headless:
            command.append("--headless")
        _state["engine_root"] = str(root)
        _state["dirty"] = True
        try:
            _state["process"] = subprocess.Popen(command, cwd=str(executable.parent))
        except Exception as exc:
            self.report({"ERROR"}, f"Could not start the engine: {exc}")
            return {"CANCELLED"}

        if _state["timer"] is None:
            _state["timer"] = bpy.app.timers.register(_watch_timer, persistent=True)
        self.report({"INFO"}, "Engine started; edits are pushed live")
        return {"FINISHED"}


class BGE_OT_stop(Operator):
    bl_idname = "bge.stop"
    bl_label = "Stop"
    bl_description = "Close the running engine"
    bl_options = {"REGISTER"}

    def execute(self, context):
        process = _state.get("process")
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except Exception:
                process.kill()
        _state["process"] = None
        if _state.get("timer") is not None:
            try:
                bpy.app.timers.unregister(_state["timer"])
            except Exception:
                pass
            _state["timer"] = None
        self.report({"INFO"}, "Engine stopped")
        return {"FINISHED"}


class BGE_OT_package(Operator):
    bl_idname = "bge.package"
    bl_label = "Package Game"
    bl_description = "Build a distributable game folder from the current scene"
    bl_options = {"REGISTER"}

    make_zip: BoolProperty(name="Also create a zip", default=False)

    def execute(self, context):
        prefs = context.preferences.addons["bge_revival"].preferences
        root = find_engine_root(prefs.engine_root)
        if not (root / "tools" / "bge_export.py").is_file():
            self.report({"ERROR"}, "BGE Revived engine folder not found; set it in the add-on preferences")
            return {"CANCELLED"}

        output = Path(prefs.export_folder).expanduser()
        try:
            manifest = export_current_scene(root, output)
        except Exception as exc:
            self.report({"ERROR"}, f"Export failed: {exc}")
            return {"CANCELLED"}

        target = output.parent / f"{bpy.context.scene.name or 'Game'}"
        command = [sys.executable, str(root / "tools" / "package_game.py"),
                   "--scene", str(manifest), "--out", str(target), "--name", bpy.context.scene.name or "Game"]
        if self.make_zip:
            command.append("--zip")
        result = subprocess.run(command, cwd=str(root), capture_output=True, text=True)
        if result.returncode != 0:
            self.report({"ERROR"}, f"Packaging failed: {result.stderr.strip()[:200]}")
            return {"CANCELLED"}
        self.report({"INFO"}, f"Game packaged in {target}")
        print(result.stdout)
        return {"FINISHED"}


class BGE_OT_preview_start(Operator):
    bl_idname = "bge.preview_start"
    bl_label = "Start Preview"
    bl_description = "Render the scene with the engine and show the result in Blender"
    bl_options = {"REGISTER"}

    def execute(self, context):
        prefs = context.preferences.addons["bge_revival"].preferences
        if _engine_client() is None:
            self.report({"ERROR"}, "The preview engine could not be started")
            return {"CANCELLED"}
        _state["dirty"] = True
        if _state["timer"] is None:
            _state["timer"] = bpy.app.timers.register(_watch_timer, persistent=True)
        _state["preview_enabled"] = bool(prefs.preview_overlay)
        if _state["preview_enabled"]:
            _add_overlay()
        self.report({"INFO"}, "Preview started")
        return {"FINISHED"}


class BGE_OT_preview_stop(Operator):
    bl_idname = "bge.preview_stop"
    bl_label = "Stop Preview"
    bl_description = "Stop the preview renderer"
    bl_options = {"REGISTER"}

    def execute(self, context):
        _stop_engine()
        self.report({"INFO"}, "Preview stopped")
        return {"FINISHED"}


class BGE_OT_preview_window(Operator):
    bl_idname = "bge.preview_window"
    bl_label = "Open Preview"
    bl_description = "Open the preview frame in an image editor window"
    bl_options = {"REGISTER"}

    def execute(self, context):
        image = _state.get("preview_image")
        if image is None:
            self.report({"ERROR"}, "The preview is not running")
            return {"CANCELLED"}
        try:
            bpy.ops.image.open(filepath=image.filepath_from_user() if image.filepath else "", check_existing=True)
        except Exception:
            # The frame lives in memory, so show the image editor instead.
            screen = bpy.context.screen
            if screen:
                for area in screen.areas:
                    if area.type == "IMAGE_EDITOR":
                        area.spaces.active.image = image
        return {"FINISHED"}


# ----------------------------------------------------------- preferences ----

class BGE_AddonPreferences(AddonPreferences):
    bl_idname = "bge_revival"

    engine_root: StringProperty(
        name="Engine folder",
        description="The BGE Revived repository (the folder that contains tools/bge_export.py)",
        subtype="DIR_PATH",
        default="",
    )
    export_folder: StringProperty(
        name="Export folder",
        description="Where the exported scene is written",
        subtype="DIR_PATH",
        default=str(Path.home() / "BGE_Exports"),
    )
    headless: BoolProperty(
        name="Run headless",
        description="Start the engine without a window (useful for automated builds)",
        default=False,
    )
    preview_port: IntProperty(
        name="Preview port",
        description="The local port the background engine listens on",
        default=DEFAULT_PORT,
        min=1024,
        max=65535,
    )
    preview_overlay: BoolProperty(
        name="Overlay in 3D View",
        description="Draw the rendered frame in the 3D View while you edit",
        default=True,
    )
    preview_fullscreen: BoolProperty(
        name="Large overlay",
        description="Draw the preview large instead of as a corner overlay",
        default=False,
    )

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "engine_root")
        layout.prop(self, "export_folder")
        layout.prop(self, "headless")
        box = layout.box()
        box.label(text="Preview")
        box.prop(self, "preview_port")
        box.prop(self, "preview_overlay")
        box.prop(self, "preview_fullscreen")
        root = find_engine_root(self.engine_root)
        layout.label(text=f"Detected engine: {root}" if str(root) else "Engine not found", icon="INFO")


# ----------------------------------------------------------------- panel ----

class BGE_PT_panel(Panel):
    bl_label = "BGE Revived"
    bl_idname = "BGE_PT_panel"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "BGE Revived"

    def draw(self, context):
        layout = self.layout
        prefs = context.preferences.addons["bge_revival"].preferences

        column = layout.column(align=True)
        column.operator("bge.export_scene", icon="EXPORT")
        column.operator("bge.play", icon="PLAY")
        column.operator("bge.stop", icon="PAUSE")

        box = layout.box()
        box.label(text="Preview", icon="RENDER_STILL")
        row = box.row(align=True)
        row.operator("bge.preview_start", icon="PLAY")
        row.operator("bge.preview_stop", icon="PAUSE")
        box.operator("bge.preview_window", icon="IMAGE")
        box.prop(prefs, "preview_overlay")
        box.prop(prefs, "preview_fullscreen")

        layout.separator()
        layout.operator("bge.package", icon="PACKAGE")

        layout.separator()
        layout.label(text="Render engine: BGE Revived (in the render properties)")
        layout.label(text="Engine running" if is_running() else "Engine stopped",
                     icon="PLAY" if is_running() else "PAUSE")


# ------------------------------------------------------------- register ----

_classes = (BGE_AddonPreferences, BGE_RenderEngine, BGE_OT_export, BGE_OT_play, BGE_OT_stop,
           BGE_OT_package, BGE_OT_preview_start, BGE_OT_preview_stop, BGE_OT_preview_window,
           BGE_PT_panel)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    if bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_mark_dirty)


def unregister():
    process = _state.get("process")
    if process and process.poll() is None:
        process.terminate()
    _state["process"] = None
    _stop_engine()
    if _state.get("timer") is not None:
        try:
            bpy.app.timers.unregister(_state["timer"])
        except Exception:
            pass
        _state["timer"] = None
    if bpy.app.handlers.depsgraph_update_post and _mark_dirty in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_mark_dirty)
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
