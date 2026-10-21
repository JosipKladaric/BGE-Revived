#!/usr/bin/env python3
"""Create a deterministic Blender scene that exercises BGE's Blender importer.

Run:
    blender --background --factory-startup --python tools/make_blender_test_scene.py -- --out build/blender_test.blend
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1 :] if "--" in argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="build/blender_test.blend")
    parser.add_argument("--mode", default="full", choices=["full", "env"],
                        help="full: general scene; env: environment/normal/alpha test")
    return parser.parse_args(argv)


def look_at(obj, target):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def principled_material(name, base, metallic=0.0, roughness=0.5, emission=None, emission_strength=1.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*base, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        bsdf.inputs["Emission Strength"].default_value = emission_strength
    return mat


def make_checker_image(texture_dir: Path):
    import numpy as np

    size = 256
    ys, xs = np.mgrid[0:size, 0:size]
    cell = (((xs // 32) + (ys // 32)) % 2).astype(np.float32)
    r = np.where(cell > 0, 0.85, 0.08)
    g = np.where(cell > 0, 0.25, 0.35)
    b = np.where(cell > 0, 0.12, 0.80)
    rgba = np.stack([r, g, b, np.ones_like(r)], axis=-1).astype(np.float32)
    return write_image("BGE_Checker", rgba, texture_dir)


def write_hdr(path: Path, rgb):
    """Write a Radiance (.hdr) equirectangular map with run-length encoding."""
    import numpy as np

    height, width, _ = rgb.shape
    maxc = rgb.max(axis=2)
    # RGBE encoding: exponent e with maxc in [2^e, 2^(e+1)), mantissa scaled to
    # 8 bits. (np.frexp is avoided: it crashes on some CPUs with this numpy build.)
    exponent = np.floor(np.log2(np.maximum(maxc, 1e-30)))
    scale = 2.0 ** (7.0 - exponent)
    mant = np.clip(rgb * scale[..., None], 0, 255).astype(np.uint8)
    e_byte = np.clip(exponent.astype(np.int32) + 129, 0, 255).astype(np.uint8)

    pixels = [tuple(int(v) for v in (mant[y, x, 0], mant[y, x, 1], mant[y, x, 2], e_byte[y, x]))
              for y in range(height) for x in range(width)]

    def encode_channel(values):
        """Radiance RLE for one channel of a scanline (planar, not interleaved)."""
        out = bytearray()
        i = 0
        n = len(values)
        while i < n:
            run = 1
            while i + run < n and values[i + run] == values[i] and run < 127:
                run += 1
            if run >= 4:
                out.append(128 + run)
                out.append(values[i])
                i += run
                continue
            start_index = i
            count = 0
            while i < n and count < 127:
                probe = 1
                while i + probe < n and values[i + probe] == values[i] and probe < 4:
                    probe += 1
                if probe >= 4:
                    break
                i += 1
                count += 1
            out.append(count)
            out.extend(values[start_index:start_index + count])
        return out

    data = bytearray()
    data += b"#?RADIANCE\n"
    data += b"FORMAT=32-bit_rle_rgbe\n\n"
    data += f"-Y {height} +X {width}\n".encode("ascii")
    for y in range(height):
        data += bytes((2, 2, (width >> 8) & 0xFF, width & 0xFF))
        for channel in range(4):
            column = [row[channel] for row in pixels[y * width:(y + 1) * width]]
            data += encode_channel(column)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(data))
    return path


def make_equirect_environment(texture_dir: Path):
    """Lat-long HDR environment map with a colour-coded hemisphere and a sun blob."""
    import numpy as np

    width, height = 1024, 512
    ys, xs = np.mgrid[0:height, 0:width]
    u = xs / (width - 1.0)
    v = ys / (height - 1.0)  # row 0 is the bottom of the image

    top = np.array([0.30, 0.50, 0.95])
    bottom = np.array([0.10, 0.08, 0.07])
    base = bottom[None, None, :] * (1.0 - v)[..., None] + top[None, None, :] * v[..., None]
    left = base * np.array([0.55, 1.00, 0.60])
    right = base * np.array([1.00, 0.45, 0.40])
    pixels = np.where((u < 0.5)[..., None], left, right)

    # A bright sun blob at a known longitude makes the orientation easy to verify.
    sun_u, sun_v = 0.25, 0.60
    du = np.minimum(np.abs(u - sun_u), 1.0 - np.abs(u - sun_u))
    distance = np.sqrt((du * 2.0) ** 2 + ((v - sun_v) * 2.0) ** 2)
    blob = np.clip(1.0 - distance * 7.0, 0.0, 1.0) ** 2
    pixels = pixels + blob[..., None] * np.array([8.0, 7.5, 6.5])

    path = write_hdr(texture_dir / "BGE_Env.hdr", pixels.astype(np.float32))
    image = bpy.data.images.load(str(path))
    image.name = "BGE_Env"
    return image


def make_normal_map_image(texture_dir: Path):
    """Tangent-space normal map with a regular bump pattern."""
    import numpy as np

    size = 256
    ys, xs = np.mgrid[0:size, 0:size]
    u = xs / (size - 1.0)
    v = ys / (size - 1.0)
    nx = 0.5 + 0.45 * np.sin(u * math.pi * 8.0) * np.cos(v * math.pi * 8.0)
    ny = 0.5 + 0.45 * np.sin(v * math.pi * 8.0)
    nz = np.ones_like(nx)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgba = np.stack([nx / length, ny / length, nz / length, np.ones_like(nx)], axis=-1)
    return write_image("BGE_Normal", rgba.astype(np.float32), texture_dir)


def write_image(name: str, rgba, texture_dir: Path):
    """Write pixels to a PNG and load it back so the image has a real file source.

    Blender's renderer handles file-backed images (including packed ones) far more
    predictably than generated buffers, especially for world lighting.
    """
    texture_dir.mkdir(parents=True, exist_ok=True)
    path = texture_dir / f"{name}.png"
    image = bpy.data.images.new(name, width=rgba.shape[1], height=rgba.shape[0], alpha=True)
    image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw = str(path)
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)

    loaded = bpy.data.images.load(str(path))
    loaded.name = name
    loaded.pack()
    return loaded


def build_env_scene(scene, texture_dir: Path):
    """Scene that exercises environment lighting, normal maps and alpha blending."""
    world = bpy.data.worlds.new("BGE Env World")
    world.use_nodes = True
    tree = world.node_tree
    background = tree.nodes.get("Background")
    background.inputs["Strength"].default_value = 1.0
    tex = tree.nodes.new("ShaderNodeTexImage")
    tex.image = make_equirect_environment(texture_dir)
    tex.location = (-320, 120)
    # Explicit Generated coordinates: Blender maps world directions to the
    # lat-long image with these, and the engine mirrors the same convention.
    coords = tree.nodes.new("ShaderNodeTexCoord")
    coords.location = (-560, 120)
    tree.links.new(coords.outputs["Generated"], tex.inputs["Vector"])
    tree.links.new(tex.outputs["Color"], background.inputs["Color"])
    scene.world = world

    ground_mat = principled_material("Ground", (0.55, 0.55, 0.55), roughness=0.55)
    bpy.ops.mesh.primitive_plane_add(size=30.0, location=(0, 0, 0))
    ground = bpy.context.object
    ground.name = "Ground"
    ground.data.materials.append(ground_mat)

    # Diffuse and metal spheres show the environment reflection.
    diffuse_mat = principled_material("Diffuse", (0.80, 0.80, 0.80), roughness=0.9)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.2, segments=64, ring_count=32, location=(-1.6, 0, 1.2))
    diffuse = bpy.context.object
    diffuse.name = "Diffuse Sphere"
    diffuse.data.materials.append(diffuse_mat)
    bpy.ops.object.shade_smooth()

    metal_mat = principled_material("Metal", (0.95, 0.95, 0.95), metallic=1.0, roughness=0.15)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.2, segments=64, ring_count=32, location=(1.6, 0, 1.2))
    metal = bpy.context.object
    metal.name = "Metal Sphere"
    metal.data.materials.append(metal_mat)
    bpy.ops.object.shade_smooth()

    # Normal mapped cube.
    normal_image = make_normal_map_image(texture_dir)
    normal_mat = bpy.data.materials.new("Normal Mapped")
    normal_mat.use_nodes = True
    tree = normal_mat.node_tree
    bsdf = tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.25, 0.45, 0.85, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.45
    normal_tex = tree.nodes.new("ShaderNodeTexImage")
    normal_tex.image = normal_image
    normal_tex.location = (-560, -180)
    normal_map = tree.nodes.new("ShaderNodeNormalMap")
    normal_map.location = (-300, -180)
    tree.links.new(normal_tex.outputs["Color"], normal_map.inputs["Color"])
    tree.links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
    bpy.ops.mesh.primitive_cube_add(size=1.8, location=(0, 2.6, 0.9))
    normal_cube = bpy.context.object
    normal_cube.name = "Normal Cube"
    normal_cube.data.materials.append(normal_mat)

    # Alpha blended sphere.
    glass = bpy.data.materials.new("Glass")
    glass.use_nodes = True
    glass_bsdf = glass.node_tree.nodes.get("Principled BSDF")
    glass_bsdf.inputs["Base Color"].default_value = (0.6, 0.8, 1.0, 1.0)
    glass_bsdf.inputs["Roughness"].default_value = 0.08
    glass_bsdf.inputs["Alpha"].default_value = 0.35
    try:
        glass.surface_render_method = "BLENDED"
    except Exception:
        pass
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.1, segments=48, ring_count=24, location=(0, -2.4, 1.1))
    glass_sphere = bpy.context.object
    glass_sphere.name = "Glass Sphere"
    glass_sphere.data.materials.append(glass)

    bpy.ops.object.camera_add(location=(0.0, -9.0, 3.4))
    camera = bpy.context.object
    camera.name = "Env Camera"
    camera.data.lens = 50.0
    camera.data.sensor_fit = "HORIZONTAL"
    look_at(camera, (0.0, 0.0, 1.4))
    scene.camera = camera

    scene.render.resolution_x = 1024
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.exposure = 0.0


def main():
    args = parse_args()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    out = Path(args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    texture_dir = out.parent / "textures"

    if args.mode == "env":
        build_env_scene(scene, texture_dir)
        bpy.ops.wm.save_as_mainfile(filepath=str(out))
        print(f"[BGE] wrote test scene {out}")
        return

    world = bpy.data.worlds.new("BGE World")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.018, 0.028, 0.052, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = world

    ground_mat = principled_material("Ground", (0.055, 0.065, 0.080), roughness=0.78)
    red_mat = principled_material("Crimson", (0.62, 0.035, 0.025), roughness=0.28, metallic=0.05)
    blue_mat = principled_material("Cobalt", (0.025, 0.16, 0.72), roughness=0.22, metallic=0.15)
    gold_mat = principled_material("Gold", (0.95, 0.52, 0.08), metallic=1.0, roughness=0.18)
    green_mat = principled_material("Sage", (0.08, 0.45, 0.18), roughness=0.38, metallic=0.05)
    white_mat = principled_material("Ceramic", (0.72, 0.70, 0.64), roughness=0.20)
    emit_mat = principled_material("Emissive", (0.02, 0.01, 0.01), emission=(1.0, 0.22, 0.04), emission_strength=4.0)

    # Ground.
    bpy.ops.mesh.primitive_plane_add(size=40.0, location=(0, 0, 0))
    ground = bpy.context.object
    ground.name = "Ground"
    ground.data.materials.append(ground_mat)

    # PBR primitives.
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(-2.2, 1.2, 1.0))
    cube = bpy.context.object
    cube.name = "Blue Cube"
    cube.data.materials.append(blue_mat)

    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.25, segments=64, ring_count=32, location=(2.0, 1.2, 1.25))
    sphere = bpy.context.object
    sphere.name = "Gold Sphere"
    sphere.data.materials.append(gold_mat)
    bpy.ops.object.shade_smooth()

    bpy.ops.mesh.primitive_monkey_add(size=2.0, location=(-0.5, 2.8, 2.0))
    monkey = bpy.context.object
    monkey.name = "Suzanne"
    monkey.data.materials.append(red_mat)
    bpy.ops.object.shade_smooth()

    bpy.ops.mesh.primitive_cylinder_add(radius=0.65, depth=3.0, location=(-4.6, -1.0, 1.5))
    cylinder = bpy.context.object
    cylinder.name = "Green Pillar"
    cylinder.data.materials.append(green_mat)
    bpy.ops.object.shade_smooth()

    bpy.ops.mesh.primitive_torus_add(
        major_radius=1.0, minor_radius=0.32, major_segments=64, minor_segments=20,
        location=(4.2, -1.2, 1.3), rotation=(math.radians(70), 0, math.radians(20))
    )
    torus = bpy.context.object
    torus.name = "Emissive Ring"
    torus.data.materials.append(emit_mat)
    bpy.ops.object.shade_smooth()

    # Texture test.
    checker = make_checker_image(texture_dir)
    tex_mat = bpy.data.materials.new("Checker Texture")
    tex_mat.use_nodes = True
    bsdf = tex_mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Roughness"].default_value = 0.32
    bsdf.inputs["Metallic"].default_value = 0.08
    tex = tex_mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = checker
    tex.interpolation = "Closest"
    tex.location = (-340, 200)
    tex_mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bpy.ops.mesh.primitive_cube_add(size=1.6, location=(0.4, -2.2, 0.8))
    textured = bpy.context.object
    textured.name = "Textured Cube"
    textured.data.materials.append(tex_mat)

    # Text object: exercises Blender's evaluated mesh conversion.
    bpy.ops.object.text_add(location=(-4.0, 3.4, 3.1), rotation=(math.radians(62), 0, math.radians(18)))
    text = bpy.context.object
    text.name = "BGE Text"
    text.data.body = "BGE"
    text.data.extrude = 0.08
    text.data.bevel_depth = 0.01
    text.data.materials.append(white_mat)

    # Lights: one of each Blender type.
    bpy.ops.object.light_add(type="SUN", location=(5.0, -5.0, 9.0))
    sun = bpy.context.object
    sun.name = "Key Sun"
    sun.data.energy = 3.2
    sun.data.color = (1.0, 0.86, 0.68)
    sun.data.angle = math.radians(4.0)
    look_at(sun, (0, 0, 0))

    bpy.ops.object.light_add(type="AREA", location=(-6.0, -2.0, 6.0))
    area = bpy.context.object
    area.name = "Soft Area"
    area.data.energy = 900.0
    area.data.shape = "RECTANGLE"
    area.data.size = 5.0
    area.data.size_y = 3.0
    area.data.color = (0.55, 0.70, 1.0)
    look_at(area, (0, 0, 1.0))

    bpy.ops.object.light_add(type="POINT", location=(3.4, 2.1, 3.2))
    point = bpy.context.object
    point.name = "Warm Point"
    point.data.energy = 220.0
    point.data.color = (1.0, 0.52, 0.18)
    point.data.shadow_soft_size = 0.35

    bpy.ops.object.light_add(type="SPOT", location=(-1.0, -6.0, 5.0))
    spot = bpy.context.object
    spot.name = "Blue Spot"
    spot.data.energy = 700.0
    spot.data.color = (0.25, 0.45, 1.0)
    spot.data.spot_size = math.radians(52)
    spot.data.spot_blend = 0.35
    spot.data.shadow_soft_size = 0.22
    look_at(spot, (0.0, 1.0, 1.0))

    # Camera.
    bpy.ops.object.camera_add(location=(10.5, -12.5, 8.0))
    camera = bpy.context.object
    camera.name = "Hero Camera"
    camera.data.lens = 52.0
    camera.data.sensor_width = 36.0
    camera.data.sensor_fit = "HORIZONTAL"
    camera.data.clip_start = 0.05
    camera.data.clip_end = 500.0
    look_at(camera, (0.0, 0.0, 1.4))
    scene.camera = camera

    scene.render.resolution_x = 1600
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.fps = 60
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.exposure = 0.0
    scene.render.film_transparent = False

    out = Path(args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(out))
    print(f"[BGE] wrote test scene {out}")


if __name__ == "__main__":
    main()
