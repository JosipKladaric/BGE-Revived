# BGE Revived - Current State

> Last updated: 2026-10-21
>
> This file is the human-readable source of truth for what is implemented, what is verified,
> and what is deliberately not done yet. `docs/current_state.html` mirrors this page for the
> project documentation site.

## Status

**Phase M1 - Renderer and Blender scene import, in progress.** The repository contains a
clean-room C++20 engine, a zero-install Windows build (Zig, optionally CMake), a Win32/OpenGL 4.6
platform layer, a sparse-set ECS, a job system, a fixed-timestep loop, a forward PBR renderer and
a Blender scene importer that renders real `.blend` scenes (all four light types, world
environment, camera settings, materials, textures, normal maps and transparency).

## Implemented

| Area | Status | Notes |
| --- | --- | --- |
| Repository structure | Done | `engine/`, `sandbox/`, `tests/`, `tools/`, `assets/`, `docs/`, `cmake/` |
| Build toolchain | Done | Zig 0.14.1 in `.toolchain/`; CMake 4.x + Ninja; MinGW-w64 GCC supported as an alternative |
| Build scripts | Done | `build.py` (incremental, hashed, `--compiler zig|mingw`) and `CMakeLists.txt` (engine/sandbox/tests) |
| Logging/assertions | Done | Thread-safe, ANSI console, `BGE_ASSERT` |
| Math | Done | Vec2/3/4, Quat, Mat4 (column major), AABB, Frustum, TRS decomposition |
| ECS | Done | Sparse-set storage, generational entity handles, hierarchy |
| Job system | Done | Fork-join thread pool + `parallel_for` |
| Fixed timestep | Done | Accumulator with spiral-of-death guard |
| Win32 window | Done | DPI-aware, GL 4.6 core context, resize, hidden `WS_POPUP` for exact captures |
| Input | Done | Keyboard/mouse state, edge detection, scroll |
| Renderer | Done (M1) | HDR RGBA16F, directional shadow map, PBR forward, instancing, frustum culling, FXAA |
| Post processing | Done | Exposure, Standard (linear to sRGB) and ACES/AgX-approximation view transforms |
| Multi-light shading | Done | Sun, point, spot and area lights in one forward pass, inverse-square falloff |
| Environment lighting | Partial | Equirectangular world maps (LDR + Radiance `.hdr`), ambient and specular IBL approximation |
| Materials | Partial | Base colour/metallic/roughness/emissive, base colour textures, normal maps, alpha blend and clip |
| Scene/transform system | Done | Hierarchy, world matrices, per-object visibility and shadow flags |
| JSON parsing | Done | Dependency-free parser used by the scene importer |
| Blender export | Done | `tools/bge_export.py`: meshes (incl. modifiers, curves, text), materials, lights, cameras, world, render settings |
| Blender import | Done | `engine/src/scene/scene_loader.cpp` reads the `.bgescene` format |
| Blender add-on | Done | `tools/blender_addon/bge_revival`: export, play, stop and package from the 3D View sidebar |
| Live link | Done | The engine reloads the scene in place when the authoring tool rewrites it (`--watch`) |
| Engine preview | Done | The engine renders into a watched folder (`--preview`) and the add-on mirrors the frames into Blender; measured round trip about 160 ms |
| Game template | Done | `game/src/main.cpp`: the runtime a packaged game ships with, loads the scene next to the executable |
| Packaging | Done | `tools/package_game.py` builds a self contained game folder (and zip) from an exported scene |
| Scene saving | Done | `engine/src/scene/scene_saver.cpp` writes the same format back out (`--save-scene`) |
| Sandbox | Done | Procedural demo, `--scene` loading, `--save-scene`, `--script` Python host, orbit/free camera, headless screenshots |
| Tests | Done | 63 assertions in `tests/src/test_main.cpp`, run by `python build.py test` |
| Python scripting | Done (M1) | Embedded CPython 3.12 with a `bge` module: `bge.types` (Vector/Euler/Quaternion), `bge.objects`, `bge.logic.scene`, `bge.logic.getCurrentController()`, `bge.app.handlers`, `bge.app.timers` |
| Scripting data | Done | Custom properties on objects, controller components with Python sources |
| Physics | In progress | Built-in rigid body solver (spheres, boxes, ground plane, impulses, sleeping); Jolt planned for the full feature set |
| Asset pipeline | Planned | glTF loading and cooked asset packs |
| Live Blender link | Planned | Streaming edits from a running Blender instance |

## Verified

- `python build.py test` reports `44 checks, 0 failures`.
- CMake build (`cmake -S . -B build/cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake`)
  compiles the engine, sandbox and tests with the same Zig toolchain and links CPython 3.12.
- A MinGW-w64 toolchain is picked up automatically (`python build.py --compiler mingw`, or
  `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-windows.cmake`); the GCC on this machine
  (6.3) is too old for C++20, so the vendored Zig toolchain stays the default.
- Blender 4.4.3 exports the generated test scene and the engine renders it headlessly:
  `build/blender_test_render2.png` (1600x900) matches the Cycles reference
  `build/blender_ref_0001.png` closely in layout, light direction, shadows and material response.
- The importer creates 8 objects, 8 materials, 4 lights (sun, area, point, spot) and 1 camera from
  the test scene, including an image-textured cube and a converted text object.
- Environment test scene (`build/blender_env_scene`) renders an equirectangular Radiance `.hdr`
  world with visible IBL on diffuse and metal spheres.
- The Blender add-on exports, plays and packages a scene from the 3D View sidebar; the live link
  was verified by re-exporting a scene while the engine was running: the engine reloaded it in
  place ("Live link: reloaded", 8 objects, 4 lights, 1 camera).
- A packaged game runs from its own folder: `build/packaged_game/BGE Test.exe` loads
  `game.bgescene`, its textures and the engine assets without Blender or Python present.
- The preview path works end to end: the engine writes `frame.png` from its own renderer and the
  add-on mirrors it into a Blender image (960x540). A scene change reaches the preview in ~160 ms.
- The embedded Python host runs `assets/scripts/demo.py`: it finds objects by name, reads and
  writes transforms through `bge.types.Vector`/`Euler`, and drives per-frame work from
  `bge.app.handlers.post_update`. Controllers attached to objects run their script at startup
  with `bge.logic.getCurrentController()` bound to the owner.
- A scene can be saved at runtime and loaded back: `--save-scene` writes the same `.bgescene`
  format the loader reads, keeping meshes, materials, texture paths, lights and cameras.
- The built-in physics solver handles gravity, impulses, sphere/box contacts and a static ground
  plane: `--physics` drops a grid of boxes and spheres that topple into a pile
  (`build/physics_demo.png`).
- Demo scene still renders with ~11 draw calls, ~154 instances, ~50k triangles and ~6 ms CPU.

## Out of scope

The classic BGE shipped a few subsystems that are not part of this engine's goal and will not
be implemented: Freestyle line rendering, Grease Pencil, movie clips / sequencer and
multi-view (stereo) rendering.

## Known limitations

- The Blender world image is sampled with a lat-long mapping whose vertical orientation does not
  match Blender's `Generated` texture coordinates exactly; the environment appears rotated
  relative to Blender (horizontal axis matches, vertical axis is offset).
- Only the first sun/directional light casts shadows; point, spot and area lights are unshadowed.
- Area lights use a roughness-widening approximation instead of a proper LTC/rectangle integral.
- Diffuse image-based lighting is an irradiance approximation, not a prefiltered irradiance map.
- AgX/Filmic colour management is approximated with an ACES curve; `Standard` is exact.
- No `EXR` environment support (Radiance `.hdr` and LDR images only).
- Alpha blending is single-pass and unsorted beyond a distance sort; no depth peeling.
- Normal maps use screen-space derivatives, not imported tangents.

## Next actions

1. Socket based live link with incremental mesh and material updates.
2. Replace the built-in solver with Jolt Physics (rigid body constraints, character control).
3. glTF 2.0 import and a cooked asset format.
4. Environment orientation fix, prefiltered IBL and per-light shadow maps.
5. Audio, gamepad input and a debug UI.
