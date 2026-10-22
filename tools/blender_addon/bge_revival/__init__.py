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
from bpy.props import BoolProperty, EnumProperty, IntProperty, StringProperty
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

    def __init__(self, selected: bool = False, max_texture: int = 2048, no_textures: bool = False,
                 local_space: bool = False):
        self.selected = selected
        self.max_texture = max_texture
        self.no_textures = no_textures
        # Incremental updates keep the geometry in the object's own space.
        self.local_space = local_space


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

    # The engine needs a scene to start with; it is replaced over the socket as
    # soon as the author changes anything.
    try:
        export_current_scene(root, folder)
    except Exception as exc:
        print(f"[BGE] could not export the preview scene: {exc}")

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
    _state["preview_texture"] = None
    _state["preview_texture_stamp"] = None
    _remove_overlay()


# ---------------------------------------------------------------- timers ----

def _mark_dirty(scene, depsgraph=None):
    if is_running() or _state.get("client") is not None:
        _state["dirty"] = True
        if depsgraph is not None:
            # The update list is only valid inside the handler, so the names are
            # collected right away.
            names = []
            for update in depsgraph.updates:
                if isinstance(update.id, bpy.types.Object):
                    names.append(update.id.name)
            if names:
                _state["updated_names"] = names


def _mesh_signature(obj, depsgraph):
    """A cheap fingerprint of an object's geometry, used to detect mesh edits."""
    try:
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        if mesh is None:
            return None
        count = len(mesh.vertices)
        checksum = 0
        step = max(1, count // 64)
        for index in range(0, count, step):
            vertex = mesh.vertices[index].co
            checksum = (checksum * 31 + int(vertex.x * 1000.0) + int(vertex.y * 1000.0) * 7 +
                        int(vertex.z * 1000.0) * 13) & 0xFFFFFFFF
        evaluated.to_mesh_clear()
        return (count, checksum)
    except Exception:
        return None


def _push_incremental(names=None, depsgraph=None):
    """Pushes only what changed: transforms, new geometry and new objects."""
    client = _state.get("client")
    if client is None:
        return False

    root = Path(_state.get("engine_root") or str(find_engine_root()))
    try:
        exporter = load_exporter(root)
    except Exception:
        return False

    if names is None:
        names = _state.get("updated_names") or []
    if depsgraph is None:
        depsgraph = bpy.context.evaluated_depsgraph_get()

    scene = bpy.context.scene
    pushed = _state.setdefault("pushed", {})
    mesh_counter = _state.setdefault("mesh_counter", 0)

    transforms = []
    objects = []
    removals = []
    meshes = []

    for name in names:
        obj = bpy.data.objects.get(name)
        if obj is None or obj.name not in scene.objects or obj.hide_render:
            if name in pushed:
                removals.append(name)
                pushed.pop(name, None)
            continue
        if obj.type in {"LIGHT", "CAMERA"}:
            # Lights and cameras are only handled by a full export.
            return False
        if obj.type not in {"MESH", "CURVE", "SURFACE", "META", "FONT"}:
            continue

        matrix = [float(obj.matrix_world[row][column]) for column in range(4) for row in range(4)]
        record = pushed.get(name)

        if record is None:
            record = {"mesh_id": 0, "signature": None, "matrix": None}
            pushed[name] = record

        signature = _mesh_signature(obj, depsgraph)
        if signature != record["signature"]:
            # The geometry changed: upload it and rebind the object.
            try:
                options = ExportOptions(local_space=True)
                material_index = {}
                material_records = []
                image_cache = {}
                primitives = exporter.mesh_primitives(
                    obj, depsgraph, options, material_index, material_records,
                    Path(_state.get("preview_dir", ".")), image_cache, local_space=True)
            except Exception as exc:
                print(f"[BGE] could not export '{name}': {exc}")
                continue
            if not primitives:
                continue
            primitive = primitives[0]
            mesh_counter += 1
            mesh_id = mesh_counter
            meshes.append((mesh_id, primitive["vertices"], primitive["indices"]))
            record["mesh_id"] = mesh_id
            record["signature"] = signature
            objects.append((name, mesh_id, 0, True, matrix))
            record["matrix"] = matrix
        elif record["matrix"] != matrix:
            transforms.append((name, matrix))
            record["matrix"] = matrix

    try:
        if removals:
            client.push_remove(removals)
        for mesh_id, vertices, indices in meshes:
            client.push_mesh(mesh_id, vertices, indices)
        if objects:
            client.push_objects(objects)
        if transforms:
            client.push_transforms(transforms)
    except Exception as exc:
        print(f"[BGE] incremental update failed: {exc}")
        return False

    _state["updated_names"] = []
    return bool(removals or objects or transforms or meshes)


def _bytes_to_floats(data: bytes, width: int, height: int):
    """Converts the engine's sRGB frame into float pixels for Blender.

    The engine writes display referred sRGB values, so they are stored in a
    "Non-Color" image: both the file that Blender writes and the image editor
    then show exactly what the engine produced.
    """
    try:
        import numpy as np
        array = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)
        return (array.astype(np.float32) / 255.0).ravel()
    except Exception:
        return [value / 255.0 for value in data]


def _debug_log(message: str) -> None:
    """Writes a diagnostic line next to the export folder (GUI mode has no console)."""
    try:
        folder = Path(_state.get("preview_dir", ".")).parent
        with open(folder / "preview_debug.log", "a", encoding="utf-8") as handle:
            handle.write(message + "\n")
    except Exception:
        pass


def _update_preview_image():
    """Pull the newest frame from the engine and mirror it into Blender."""
    client = _state.get("client")
    if client is None:
        return
    try:
        if getattr(client, "has_shared", False):
            # The frame bytes live in shared memory, so the preview can take the
            # newest one without asking the engine. No round trip, no waiting.
            frame = client.latest_frame()
            if frame is None:
                return  # nothing new since the last tick
        else:
            # Socket fallback: the request is synchronous, so the preview always
            # shows the current scene.
            frame = client.request_frame()
        width, height, payload = frame
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
        image.pixels.foreach_set(_bytes_to_floats(payload, width, height))
        image.update()
        # A counter, not a timestamp: it is the change token for the viewport
        # texture cache, and timestamps can collide inside one clock tick.
        _state["preview_stamp"] = (_state.get("preview_stamp") or 0.0) + 1.0

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
            depsgraph = _state.pop("updated", None)
            depsgraph = _state.pop("updated", None)
            root = Path(_state["engine_root"] or str(find_engine_root()))
            prefs = bpy.context.preferences.addons["bge_revival"].preferences
            if not _push_incremental():
                # Fall back to a full export when the change is not incremental.
                export_current_scene(root, live_export_folder(prefs))
        _update_preview_image()
    except Exception as exc:  # never let a timer error kill Blender
        print(f"[BGE] live export failed: {exc}")
    return 0.05


# --------------------------------------------------- viewport overlay ----
# The rendered frame is drawn in the 3D View so the author can see the game
# while they work. Everything is guarded, because the GPU image API differs
# between Blender versions.

def _camera_frame_corners(context, region):
    """The camera frame corners inside a 3D View region.

    Blender 4.x has no hook for a custom engine to draw the viewport, so the
    game view is drawn over the camera frame instead: the author keeps editing
    while the game shows exactly what the camera sees.

    Returns the four corners (top left, top right, bottom right, bottom left) in
    region pixels, or None when the view is not a camera view.
    """
    view = region.data
    if view.view_perspective != "CAMERA":
        return None
    camera = context.scene.camera
    if camera is None:
        return None
    try:
        from bpy_extras import view3d_utils

        corners = []
        for corner in camera.data.view_frame(scene=context.scene):
            world = camera.matrix_world @ corner
            position = view3d_utils.location_3d_to_region_2d(region, view, world)
            if position is None:
                return None
            corners.append((position.x, position.y))
    except Exception:
        return None
    if len(corners) != 4:
        return None
    # view_frame returns top right, bottom right, bottom left, top left.
    top_right, bottom_right, bottom_left, top_left = corners
    return [top_left, top_right, bottom_right, bottom_left]


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

    # The GPU texture is a copy of the image, so it is rebuilt whenever the
    # frame changed. It must *not* be rebuilt on every redraw: the 3D View
    # redraws while the user moves the mouse, and a 5 MB texture upload per
    # redraw is what made the preview feel sluggish.
    stamp = _state.get("preview_stamp")
    texture = _state.get("preview_texture")
    if texture is None or _state.get("preview_texture_stamp") != stamp:
        try:
            import numpy as np

            pixels = np.empty(len(image.pixels), dtype=np.float32)
            image.pixels.foreach_get(pixels)
            buffer = gpu.types.Buffer("FLOAT", pixels.size, pixels)
            texture = gpu.types.GPUTexture(image.size[:], format="RGBA16F", data=buffer)
        except Exception as exc:
            _debug_log(f"overlay texture failed: {exc}")
            return
        _state["preview_texture"] = texture
        _state["preview_texture_stamp"] = stamp

    prefs = bpy.context.preferences.addons["bge_revival"].preferences
    mode = prefs.preview_mode
    corners = None

    if mode == "CAMERA":
        corners = _camera_frame_corners(bpy.context, region)

    if corners is None:
        # No camera frame (not a camera view): fall back to the corner overlay.
        width = region.width * 0.28
        height = width * (image.size[1] / max(image.size[0], 1))
        x = region.width - width - 18.0
        y = 18.0
        corners = [(x, y + height), (x + width, y + height), (x + width, y), (x, y)]

    # The image is stored bottom up, so the top of the frame uses v = 1.
    uvs = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    try:
        gpu.state.blend_set("ALPHA")
        # The viewport draws with depth testing, which would hide the overlay.
        gpu.state.depth_test_set("NONE")
        batch = batch_for_shader(shader, "TRI_FAN", {"pos": corners, "texCoord": uvs})
        shader.uniform_sampler("image", texture)
        batch.draw(shader)
        gpu.state.depth_test_set("LESS_EQUAL")
        gpu.state.blend_set("NONE")
    except Exception as exc:
        _debug_log(f"overlay draw failed: {exc}")
        return


def _add_overlay() -> None:
    if _state.get("overlay_handle") is not None:
        return
    try:
        _state["overlay_handle"] = bpy.types.SpaceView3D.draw_handler_add(
            _draw_preview_overlay, (), "WINDOW", "POST_PIXEL"
        )
        print("[BGE] viewport overlay registered")
    except Exception as exc:
        _state["overlay_handle"] = None
        print(f"[BGE] the viewport overlay could not be registered: {exc}")


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
        linear = _bytes_to_floats(pixels, frame_width, frame_height)
        image = bpy.data.images.get("BGE Render")
        if image is None or tuple(image.size) != (frame_width, frame_height):
            if image is not None:
                bpy.data.images.remove(image)
            image = bpy.data.images.new("BGE Render", width=frame_width, height=frame_height,
                                        alpha=False, float_buffer=True)
            image.colorspace_settings.name = "Non-Color"  # the pixels are already linear
        image.pixels.foreach_set(linear)
        image.update()

        # Blender does not expose the render result buffer to Python engines, so
        # the image is written to the output path itself. Blender writes its own
        # (empty) result afterwards, so the final write happens in the
        # render_write handler below.
        _state["render_image"] = image
        _state["render_paths"] = _render_output_paths(scene)

        result = bpy.data.images.get("Render Result")
        if result is not None and tuple(result.size) == (frame_width, frame_height):
            try:
                result.pixels.foreach_set(linear)
            except Exception:
                pass

        screen = bpy.context.screen
        if screen:
            for area in screen.areas:
                if area.type == "IMAGE_EDITOR":
                    area.spaces.active.image = image


def _render_output_paths(scene):
    """The files Blender writes for this render: the still path and the frame path."""
    paths = []
    if scene.render.filepath:
        paths.append(bpy.path.abspath(scene.render.filepath))
    try:
        paths.append(scene.render.frame_path(frame=scene.frame_current))
    except Exception:
        pass
    unique = []
    for path in paths:
        if path and path not in unique:
            unique.append(path)
    return unique


def _write_render_output(scene, *args):
    """Writes the engine's frame over Blender's placeholder output.

    Blender writes its own (empty) result after the render, so the image is
    written in the render_complete handler, which runs last.
    """
    image = _state.get("render_image")
    if image is None:
        return
    for path in _state.get("render_paths", []):
        try:
            image.filepath_raw = path
            image.file_format = scene.render.image_settings.file_format
            image.save()
        except Exception as exc:
            print(f"[BGE] could not write {path}: {exc}")


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
    preview_mode: EnumProperty(
        name="Overlay",
        description="Where the rendered frame is drawn in the 3D View",
        items=[
            ("CAMERA", "Camera frame", "Draw the game over the camera frame"),
            ("CORNER", "Corner", "Draw the game in the corner of the 3D View"),
            ("OFF", "Off", "Do not draw the game in the 3D View"),
        ],
        default="CAMERA",
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
        box.prop(prefs, "preview_mode")

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
    if hasattr(bpy.app.handlers, "render_complete"):
        bpy.app.handlers.render_complete.append(_write_render_output)


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
    if hasattr(bpy.app.handlers, "render_complete") and _write_render_output in bpy.app.handlers.render_complete:
        bpy.app.handlers.render_complete.remove(_write_render_output)
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
