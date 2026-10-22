#!/usr/bin/env python3
"""Export the active Blender scene to the BGE Revived .bgescene format.

Run:
    blender --background scene.blend --python tools/bge_export.py -- --out build/scene

The exporter bakes evaluated mesh objects (modifiers, curves, text, surfaces) into
world space. Lights and cameras keep their Blender transform and photographic
properties. World background colour/strength, exposure, mist and the active camera
are carried into the engine scene.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import struct
import sys
from collections import defaultdict
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

# Blender is Z-up, right-handed. BGE uses Y-up, right-handed.
# C maps (x, y, z) -> (x, z, -y); its determinant is +1, so winding is preserved.
C = Matrix(
    (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, -1.0, 0.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )
)
C_INV = C.transposed()


def conv_vec(v: Vector) -> Vector:
    return Vector((v.x, v.z, -v.y))


def conv_matrix(m: Matrix) -> Matrix:
    return C @ m @ C_INV


def mat_to_list(m: Matrix) -> list[float]:
    # Column-major for the engine's Mat4.
    return [float(m[row][col]) for col in range(4) for row in range(4)]


def safe_name(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value)
    return value.strip("._") or "unnamed"


def parse_args() -> argparse.Namespace:
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    parser = argparse.ArgumentParser(description="Export a Blender scene to BGE Revived")
    parser.add_argument("--out", required=True, help="Output .bgescene path or directory")
    parser.add_argument("--scene", default=None, help="Scene name (default: active scene)")
    parser.add_argument("--selected", action="store_true", help="Export selected objects only")
    parser.add_argument("--no-textures", action="store_true", help="Do not write image files")
    parser.add_argument("--max-texture", type=int, default=2048, help="Resize exported textures")
    return parser.parse_args(argv)


def linear_rgba(value, fallback=(0.05, 0.05, 0.05, 1.0)):
    if value is None:
        return list(fallback)
    if hasattr(value, "__len__"):
        if len(value) == 3:
            return [float(value[0]), float(value[1]), float(value[2]), 1.0]
        if len(value) >= 4:
            return [float(value[0]), float(value[1]), float(value[2]), float(value[3])]
    f = float(value)
    return [f, f, f, 1.0]


def socket_default(node, name: str, fallback):
    socket = node.inputs.get(name)
    if socket is None or socket.is_linked:
        return fallback
    return socket.default_value


def find_principled(material):
    if not material or not material.use_nodes or not material.node_tree:
        return None
    for node in material.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            return node
    return None


def find_image_texture(node, depth=0):
    """Walk upstream from a Principled input to the first image texture."""
    if node is None or depth > 10:
        return None
    if node.type == "TEX_IMAGE" and node.image:
        return node
    for socket in node.inputs:
        for link in socket.links:
            found = find_image_texture(link.from_node, depth + 1)
            if found:
                return found
    return None


def linked_image(material, input_name: str):
    node = find_principled(material)
    if not node:
        return None
    socket = node.inputs.get(input_name)
    if socket is None or not socket.is_linked:
        return None
    return find_image_texture(socket.links[0].from_node)


def save_image(image, out_dir: Path, use_textures: bool, max_size: int, cache: dict):
    if not use_textures or image is None:
        return None
    key = image.name
    if key in cache:
        return cache[key]

    out_dir.mkdir(parents=True, exist_ok=True)
    source = Path(bpy.path.abspath(image.filepath_raw)) if image.filepath_raw else Path()

    # High dynamic range images are copied verbatim: re-encoding them as PNG
    # would clamp the radiance that the environment map depends on.
    if image.source == "FILE" and source.suffix.lower() in {".hdr", ".exr"} and source.is_file():
        filename = safe_name(image.name) + source.suffix.lower()
        try:
            shutil.copyfile(source, out_dir / filename)
            cache[key] = f"textures/{filename}"
            return cache[key]
        except Exception as exc:
            print(f"[BGE] HDR copy failed for {image.name}: {exc}", file=sys.stderr)

    filename = safe_name(image.name) + ".png"
    path = out_dir / filename
    try:
        copy = image.copy()
        copy.filepath_raw = str(path)
        copy.file_format = "PNG"
        if copy.size[0] > max_size or copy.size[1] > max_size:
            scale = max_size / max(copy.size[0], copy.size[1])
            copy.scale(max(1, int(copy.size[0] * scale)), max(1, int(copy.size[1] * scale)))
        copy.save()
        bpy.data.images.remove(copy)
        cache[key] = f"textures/{filename}"
        return cache[key]
    except Exception as exc:
        print(f"[BGE] texture export failed for {image.name}: {exc}", file=sys.stderr)
        cache[key] = None
        return None


def material_record(material, out_dir: Path, args, image_cache: dict, material_index: dict,
                    material_records: list):
    if material is None:
        material = bpy.data.materials.new("BGE Default")
        material.use_nodes = True
    if material.name in material_index:
        return material_index[material.name]

    prism = find_principled(material)
    base = [0.8, 0.8, 0.8, 1.0]
    metallic = 0.0
    roughness = 0.5
    emission = [0.0, 0.0, 0.0]
    alpha = 1.0
    alpha_mode = "OPAQUE"

    if prism:
        base = linear_rgba(socket_default(prism, "Base Color", (0.8, 0.8, 0.8, 1.0)))
        metallic = float(socket_default(prism, "Metallic", 0.0))
        roughness = float(socket_default(prism, "Roughness", 0.5))
        emission_color = linear_rgba(
            socket_default(prism, "Emission Color", socket_default(prism, "Emission", (0.0, 0.0, 0.0, 1.0)))
        )
        strength = float(socket_default(prism, "Emission Strength", 1.0))
        emission = [emission_color[0] * strength, emission_color[1] * strength, emission_color[2] * strength]
        alpha = float(socket_default(prism, "Alpha", 1.0))

    blend_method = getattr(material, "blend_method", None)
    surface_method = getattr(material, "surface_render_method", None)
    if alpha < 0.999:
        alpha_mode = "BLEND"
    elif surface_method == "BLENDED" or blend_method in {"BLEND", "HASHED"}:
        alpha_mode = "BLEND"
    elif blend_method == "CLIP":
        alpha_mode = "CLIP"

    albedo = None
    image_node = linked_image(material, "Base Color")
    if image_node and image_node.image:
        albedo = save_image(image_node.image, out_dir, not args.no_textures, args.max_texture, image_cache)

    normal = None
    normal_strength = 1.0
    normal_node = linked_image(material, "Normal")
    if normal_node and normal_node.image:
        normal = save_image(normal_node.image, out_dir, not args.no_textures, args.max_texture, image_cache)
        if prism:
            socket = prism.inputs.get("Normal")
            if socket and socket.is_linked:
                normal_map = socket.links[0].from_node
                if normal_map.type == "NORMAL_MAP" and normal_map.inputs.get("Strength"):
                    normal_strength = float(normal_map.inputs["Strength"].default_value)

    record = {
        "name": material.name,
        "base_color": base,
        "metallic": max(0.0, min(1.0, metallic)),
        "roughness": max(0.045, min(1.0, roughness)),
        "emissive": emission,
        "alpha": max(0.0, min(1.0, alpha)),
        "alpha_mode": alpha_mode,
    }
    if albedo:
        record["albedo"] = albedo
    if normal:
        record["normal"] = normal
        record["normal_strength"] = normal_strength
    index = len(material_records)
    material_index[material.name] = index
    material_records.append(record)
    return index


def mesh_primitives(obj, depsgraph, args, material_index, material_records, out_dir, image_cache,
                    local_space=False):
    """Return (primitives, matrix_identity). Primitives are already in world space.

    With `local_space` the geometry keeps the object's own transform, so the
    engine can move the object later with a cheap transform update.
    """
    if obj.type not in {"MESH", "CURVE", "SURFACE", "META", "FONT"}:
        return []

    evaluated = obj.evaluated_get(depsgraph)
    try:
        mesh = evaluated.to_mesh()
    except Exception as exc:
        print(f"[BGE] cannot evaluate {obj.name}: {exc}", file=sys.stderr)
        return []
    if mesh is None:
        return []

    try:
        mesh.calc_loop_triangles()
        if not mesh.loop_triangles:
            return []

        # In local space the geometry keeps the raw mesh vertices; the engine
        # applies the world matrix it receives with the object.
        world = Matrix.Identity(4) if local_space else obj.matrix_world.copy()
        normal_mat = world.to_3x3().inverted_safe().transposed()
        mirrored = world.to_3x3().determinant() < 0.0

        uv_layer = mesh.uv_layers.active.data if mesh.uv_layers.active else None
        corner_normals = getattr(mesh, "corner_normals", None)

        groups = defaultdict(list)
        for tri in mesh.loop_triangles:
            groups[int(tri.material_index)].append(tri)

        primitives = []
        for material_slot, triangles in groups.items():
            material = None
            if 0 <= material_slot < len(obj.material_slots):
                material = obj.material_slots[material_slot].material
            mat_index = material_record(material, out_dir, args, image_cache, material_index,
                                        material_records)

            vertices = []
            indices = []
            for tri in triangles:
                base = len(vertices)
                for loop_index in tri.loops:
                    loop = mesh.loops[loop_index]
                    co = world @ mesh.vertices[loop.vertex_index].co
                    if corner_normals is not None and loop_index < len(corner_normals):
                        normal = corner_normals[loop_index].vector.copy()
                    else:
                        normal = tri.normal.copy()
                    normal = normal_mat @ normal
                    uv = uv_layer[loop_index].uv if uv_layer else (0.0, 0.0)
                    p = conv_vec(co)
                    n = conv_vec(normal)
                    if n.length_squared > 1e-12:
                        n.normalize()
                    vertices.append((p.x, p.y, p.z, n.x, n.y, n.z, uv[0], uv[1]))
                if mirrored:
                    indices.extend((base, base + 2, base + 1))
                else:
                    indices.extend((base, base + 1, base + 2))

            name = obj.name
            if len(groups) > 1 and material:
                name = f"{obj.name} / {material.name}"
            primitives.append(
                {
                    "name": name,
                    "material": mat_index,
                    "vertices": vertices,
                    "indices": indices,
                }
            )
        return primitives
    finally:
        evaluated.to_mesh_clear()


def rigid_body_record(obj) -> dict | None:
    """Describes a Blender rigid body for the runtime physics world.

    Blender keeps the rigid body settings on the object, so an object with
    ``obj.rigid_body`` becomes a body in the engine as well. The bounds come
    from the object dimensions, which include its scale.
    """
    body = getattr(obj, "rigid_body", None)
    if body is None:
        return None
    dimensions = [max(float(d), 0.002) for d in obj.dimensions]
    half_extents = [d * 0.5 for d in dimensions]
    shape = "sphere" if body.collision_shape == "SPHERE" else "box"
    return {
        "shape": shape,
        "static": body.type == "PASSIVE",
        "mass": max(float(body.mass), 0.001),
        "friction": float(body.friction),
        "restitution": float(body.restitution),
        "half_extents": half_extents,
        "radius": max(half_extents),
    }


def light_record(obj) -> dict | None:
    light = obj.data
    type_map = {"SUN": 0, "POINT": 1, "SPOT": 2, "AREA": 3}
    if light.type not in type_map:
        return None
    world = obj.matrix_world.copy()
    direction = world.to_quaternion() @ Vector((0.0, 0.0, -1.0))
    direction = conv_vec(direction)
    if direction.length_squared > 1e-12:
        direction.normalize()

    radius = float(getattr(light, "shadow_soft_size", 0.1))
    size_x = float(getattr(light, "size", 0.25))
    size_y = float(getattr(light, "size_y", size_x))
    if light.type == "AREA":
        radius = 0.5 * max(size_x, size_y)
    elif light.type == "SUN":
        radius = float(getattr(light, "angle", 0.526) * 0.5)

    record = {
        "name": obj.name,
        "type": type_map[light.type],
        "matrix": mat_to_list(conv_matrix(world)),
        "direction": [float(direction.x), float(direction.y), float(direction.z)],
        "color": [float(light.color[0]), float(light.color[1]), float(light.color[2])],
        "energy": float(light.energy),
        "radius": max(radius, 0.001),
        "size": [max(size_x, 0.001), max(size_y, 0.001)],
        "spot_size": float(getattr(light, "spot_size", math.radians(45.0))),
        "spot_blend": float(getattr(light, "spot_blend", 0.15)),
        "cast_shadow": bool(getattr(light, "use_shadow", True)),
        "range": 0.0,
    }
    if getattr(light, "use_custom_distance", False):
        record["range"] = float(getattr(light, "cutoff_distance", 0.0))
    return record


def camera_record(obj, scene, is_active: bool) -> dict:
    cam = obj.data
    world = obj.matrix_world.copy()
    aspect = max(scene.render.resolution_x / max(scene.render.resolution_y, 1), 1e-4)
    orthographic = cam.type == "ORTHO"

    if orthographic:
        fit = cam.sensor_fit
        if fit == "VERTICAL" or (fit == "AUTO" and aspect < 1.0):
            ortho_height = float(cam.ortho_scale)
            ortho_width = ortho_height * aspect
        else:
            ortho_width = float(cam.ortho_scale)
            ortho_height = ortho_width / aspect
    else:
        ortho_width = float(cam.ortho_scale)
        ortho_height = ortho_width / aspect

    fov_y = float(getattr(cam, "angle_y", math.radians(50.0)))
    position_b = world.translation
    forward_b = world.to_quaternion() @ Vector((0.0, 0.0, -1.0))
    up_b = world.to_quaternion() @ Vector((0.0, 1.0, 0.0))
    position_e = conv_vec(position_b)
    forward_e = conv_vec(forward_b).normalized()
    up_e = conv_vec(up_b).normalized()
    record = {
        "name": obj.name,
        "matrix": mat_to_list(conv_matrix(world)),
        "position": [position_e.x, position_e.y, position_e.z],
        "forward": [forward_e.x, forward_e.y, forward_e.z],
        "up": [up_e.x, up_e.y, up_e.z],
        "fov_y": fov_y,
        "near": float(cam.clip_start),
        "far": float(cam.clip_end),
        "orthographic": orthographic,
        "ortho_width": ortho_width,
        "ortho_height": ortho_height,
        "shift_x": float(cam.shift_x),
        "shift_y": float(cam.shift_y),
        "primary": bool(is_active),
        "sensor_fit": cam.sensor_fit,
        "lens": float(cam.lens),
        "dof": {
            "enabled": bool(getattr(cam.dof, "use_dof", False)),
            "focus_distance": float(getattr(cam.dof, "focus_distance", 0.0)),
            "aperture_fstop": float(getattr(cam.dof, "aperture_fstop", 0.0)),
        },
    }
    return record


def world_record(scene, out_dir, args, image_cache):
    world = scene.world
    background_color = [0.05, 0.05, 0.05]
    strength = 1.0
    env_tex = None
    flat_sky = True

    if world:
        if world.use_nodes and world.node_tree:
            bg = None
            for node in world.node_tree.nodes:
                if node.type == "BACKGROUND":
                    bg = node
                    break
            if bg:
                color_socket = bg.inputs.get("Color")
                if color_socket is not None:
                    if color_socket.is_linked:
                        tex = find_image_texture(color_socket.links[0].from_node)
                        if tex and tex.image:
                            env_tex = save_image(tex.image, out_dir, not args.no_textures, args.max_texture, image_cache)
                            flat_sky = False
                    if not color_socket.is_linked:
                        background_color = linear_rgba(color_socket.default_value)[:3]
                strength = float(bg.inputs["Strength"].default_value) if bg.inputs.get("Strength") else 1.0
        else:
            background_color = [world.color[0], world.color[1], world.color[2]]

    mist = getattr(world, "mist_settings", None) if world else None
    fog_density = 0.0
    if mist and getattr(mist, "use_mist", False):
        depth = max(float(mist.depth), 0.001)
        fog_density = 1.0 / depth

    record = {
        "background_color": background_color,
        "strength": strength,
        "flat_sky": flat_sky,
        "fog_density": fog_density,
    }
    if env_tex:
        record["environment_texture"] = env_tex
    return record


def export_scene(scene, out, args) -> Path:
    """Export a Blender scene to the .bgescene format and return the manifest path.

    `args` only needs the `selected`, `max_texture` and `no_textures` attributes, so
    the Blender addon can call this with a simple namespace of its own.
    """
    out = Path(out)
    if out.suffix.lower() != ".bgescene":
        out = out / "scene.bgescene"
    out = out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    texture_dir = out.parent / "textures"

    depsgraph = bpy.context.evaluated_depsgraph_get()
    objects = [obj for obj in scene.objects if not obj.hide_render]
    if args.selected:
        objects = [obj for obj in objects if obj.select_get()]

    objects.sort(key=lambda o: o.name)
    material_index: dict[str, int] = {}
    material_records: list[dict] = []
    image_cache: dict[str, str | None] = {}
    mesh_records = []
    object_records = []
    buffer = bytearray()

    # Preserve a deterministic material order by collecting as objects are exported.
    for obj in objects:
        primitives = mesh_primitives(obj, depsgraph, args, material_index, material_records,
                                     texture_dir, image_cache)
        physics = rigid_body_record(obj)
        for prim_index, prim in enumerate(primitives):
            vertex_offset = len(buffer)
            for v in prim["vertices"]:
                buffer.extend(struct.pack("<8f", *v))
            index_offset = len(buffer)
            for index in prim["indices"]:
                buffer.extend(struct.pack("<I", index))
            mesh_records.append(
                {
                    "name": prim["name"],
                    "vertex_byte_offset": vertex_offset,
                    "vertex_count": len(prim["vertices"]),
                    "index_byte_offset": index_offset,
                    "index_count": len(prim["indices"]),
                }
            )
            record = {
                "name": prim["name"],
                "mesh": len(mesh_records) - 1,
                "material": prim["material"],
                "matrix": mat_to_list(Matrix.Identity(4)),
                "visible": True,
                "cast_shadow": bool(getattr(obj, "visible_shadow", True)),
            }
            # One body per object, not per material primitive.
            if physics is not None and prim_index == 0:
                record["physics"] = physics
            object_records.append(record)

    lights = [r for obj in objects if obj.type == "LIGHT" and (r := light_record(obj)) is not None]
    active_camera = scene.camera.name if scene.camera else None
    cameras = [
        camera_record(obj, scene, obj == scene.camera)
        for obj in objects
        if obj.type == "CAMERA"
    ]

    world = world_record(scene, texture_dir, args, image_cache)
    animations = animation_tracks(scene, objects)
    render = {
        "resolution_x": int(scene.render.resolution_x * scene.render.resolution_percentage / 100),
        "resolution_y": int(scene.render.resolution_y * scene.render.resolution_percentage / 100),
        "fps": float(scene.render.fps / max(scene.render.fps_base, 1e-6)),
        "frame_start": int(scene.frame_start),
        "frame_end": int(scene.frame_end),
        "film_transparent": bool(scene.render.film_transparent),
        "exposure": float(scene.view_settings.exposure),
        "view_transform": str(scene.view_settings.view_transform),
        "look": str(scene.view_settings.look),
    }

    doc = {
        "format": "bge.scene/1",
        "generator": "BGE Revived Blender exporter",
        "blender_version": bpy.app.version_string,
        "name": scene.name,
        "buffer": "scene.bin",
        "coordinate_system": "y_up_right_handed",
        "active_camera": active_camera,
        "world": world,
        "render": render,
        "animations": animations,
        "materials": material_records,
        "meshes": mesh_records,
        "objects": object_records,
        "lights": lights,
        "cameras": cameras,
    }

    (out.parent / "scene.bin").write_bytes(bytes(buffer))
    out.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(
        f"[BGE] wrote {out} ({len(object_records)} objects, {len(lights)} lights, "
        f"{len(cameras)} cameras, {len(buffer) / 1024 / 1024:.2f} MiB)"
    )
    return out


def animation_tracks(scene, objects):
    """Samples the transform animation of every animated object.

    Blender's action system is evaluated here, so the runtime receives plain
    world matrices sampled at a fixed rate and interpolates them itself.
    """
    tracks = []
    start = scene.frame_start
    end = scene.frame_end
    if end < start:
        return tracks
    fps = scene.render.fps / max(scene.render.fps_base, 1e-6)
    current = scene.frame_current

    for obj in objects:
        animation_data = getattr(obj, "animation_data", None)
        if animation_data is None or animation_data.action is None:
            continue
        matrices = []
        for frame in range(start, end + 1):
            scene.frame_set(frame)
            matrices.append(mat_to_list(conv_matrix(obj.matrix_world)))
        scene.frame_set(current)
        if not matrices or matrices[0] == matrices[-1]:
            # The object never moved: no track needed.
            continue
        times = [(frame - start) / fps for frame in range(start, end + 1)]
        tracks.append({
            "name": obj.name,
            "fps": fps,
            "start_time": 0.0,
            "times": times,
            "matrices": [value for matrix in matrices for value in matrix],
        })
    return tracks


def main() -> int:
    args = parse_args()
    scene = bpy.context.scene
    if args.scene:
        scene = bpy.data.scenes.get(args.scene) or scene

    out = Path(args.out)
    if out.suffix.lower() != ".bgescene":
        out = out / "scene.bgescene"
    export_scene(scene, out.resolve(), args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
