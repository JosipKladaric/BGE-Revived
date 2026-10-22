# BGE Revived - Current State

> Last updated: 2026-10-22
>
> This file is the human-readable source of truth for what is implemented, what is verified,
> and what is deliberately not done yet. `docs/current_state.html` mirrors this page for the
> project documentation site.

## Status

**Phase M1 - Renderer and Blender scene import, in progress.** The repository contains a
clean-room C++20 engine, a zero-install Windows build (Zig, optionally CMake), a Win32/OpenGL 4.6
platform layer, a sparse-set ECS, a job system, a fixed-timestep loop, a forward PBR renderer and
a Blender scene importer that renders real `.blend` scenes (all four light types, world
environment, camera settings, materials, textures, normal maps and transparency). Jolt Physics
5.3.0 is vendored as the rigid body backend, with the small built-in solver as a fallback when the
optional source is not present.

## Implemented

| Area | Status | Notes |
| --- | --- | --- |
| Repository structure | Done | `engine/`, `sandbox/`, `tests/`, `tools/`, `assets/`, `docs/`, `cmake/` |
| Build toolchain | Done | Zig 0.14.1 in `.toolchain/`; CMake 4.x + Ninja; MinGW-w64 GCC supported as an alternative |
| Build scripts | Done | `build.py` (incremental, hashed, `--compiler zig|mingw`, `--no-jolt`) and `CMakeLists.txt` (engine/sandbox/tests) |
| Optional dependencies | Done | `tools/fetch_jolt.py` downloads Jolt Physics 5.3.0 into `engine/third_party/jolt`; both build paths pick it up automatically and fall back to the built-in solver when it is absent |
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
| Animation export | Done | Keyframed object transforms are sampled per frame and played back by the runtime |
| Blender import | Done | `engine/src/scene/scene_loader.cpp` reads the `.bgescene` format |
| Blender add-on | Done | `tools/blender_addon/bge_revival`: export, play, stop and package from the 3D View sidebar |
| Blender render engine | Done | The add-on registers "BGE Revived" as a render engine: pick it instead of Cycles and F12 renders with the game engine |
| Preview transport | Done | The engine runs a local TCP server for scenes, commands and small replies; frame pixels travel through a named shared memory region |
| Shared frame transport | Done | `SharedFrame` (engine) and `SharedFrameReader` (add-on) publish/read frames with a seqlock, so the socket never carries several megabytes per frame |
| Incremental live link | Done | Only the changed objects are transferred: transforms are a few bytes, edited geometry is re-uploaded on its own |
| Viewport preview | Done | The rendered frame is mirrored into Blender and drawn over the camera frame in the 3D View |
| Live link | Done | The engine reloads the scene in place when the authoring tool rewrites it (`--watch`) |
| Engine preview | Done | The engine renders into a watched folder (`--preview`) and the add-on mirrors the frames into Blender; the live preview now reads the newest frame from shared memory instead of asking for one |
| Game template | Done | `game/src/main.cpp`: the runtime a packaged game ships with, loads the scene next to the executable |
| Packaging | Done | `tools/package_game.py` builds a self contained game folder (and zip) from an exported scene |
| Scene saving | Done | `engine/src/scene/scene_saver.cpp` writes the same format back out (`--save-scene`) |
| Sandbox | Done | Procedural demo, `--scene` loading, `--save-scene`, `--script` Python host, orbit/free camera, headless screenshots |
| Tests | Done | 86 assertions in `tests/src/test_main.cpp`, run by `python build.py test` |
| Python scripting | Done (M1) | Embedded CPython 3.12 with a `bge` module: `bge.types` (Vector/Euler/Quaternion), `bge.objects`, `bge.logic.scene`, `bge.logic.getCurrentController()`, `bge.app.handlers`, `bge.app.timers` |
| Scripting data | Done | Custom properties on objects, controller components with Python sources |
| Physics | Done (M1) | Jolt Physics 5.3.0 backend (boxes, spheres, static bodies, impulses, sleeping, multithreaded solver); the built-in solver is the fallback when Jolt was not fetched |
| Physics authoring | Done | Blender rigid body objects are exported as `physics` blocks and become bodies in the engine; the exporter writes shape, mass, friction and restitution |
| Asset pipeline | Planned | glTF loading and cooked asset packs |
| Live Blender link | Planned | Streaming edits from a running Blender instance |

## Verified

- `python build.py test` reports `86 checks, 0 failures`, including the Jolt backend test
  (a box falls onto a static floor, rests on it and responds to an impulse).
- `python build.py test --no-jolt` still compiles and passes with the built-in solver, so Jolt
  stays an optional dependency.
- The CMake build (`cmake -S . -B build/cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake`)
  compiles the engine, sandbox, game and tests with the same Zig toolchain, builds the vendored
  Jolt sources and links CPython 3.12; `build/cmake/bin/bge_tests.exe` reports the same
  `86 checks, 0 failures`.
- A MinGW-w64 toolchain is picked up automatically (`python build.py --compiler mingw`, or
  `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-windows.cmake`); the GCC on this machine
  (6.3) is too old for C++20, so the vendored Zig toolchain stays the default.
- Blender 4.4.3 exports the generated test scene and the engine renders it headlessly:
  `build/blender_test_render2.png` (1600x900) matches the Cycles reference
  `build/blender_ref_0001.png` closely in layout, light direction, shadows and material response.
- The importer creates 13 objects (including the passive floor and the three crates), 8 materials,
  4 lights (sun, area, point, spot) and 1 camera from the test scene, including an image-textured
  cube and a converted text object.
- Keyframed objects animate in the game: the exporter samples the transform animation per frame
  and the runtime interpolates the samples on the fixed step (verified: the object moves between
  frames of the same render).
- Environment test scene (`build/blender_env_scene`) renders an equirectangular Radiance `.hdr`
  world with visible IBL on diffuse and metal spheres.
- The Blender add-on exports, plays and packages a scene from the 3D View sidebar; the live link
  was verified by re-exporting a scene while the engine was running: the engine reloaded it in
  place ("Live link: reloaded", 8 objects, 4 lights, 1 camera).
- A packaged game runs from its own folder: `build/packaged_game/BGE Test.exe` loads
  `game.bgescene`, its textures and the engine assets without Blender or Python present.
- The preview path works end to end: the engine writes `frame.png` from its own renderer and the
  add-on mirrors it into a Blender image (960x540). The live preview now reads the newest frame from
  shared memory instead of waiting for an answer.
- Frames travel through shared memory: `python tools/test_shared_frame.py` verifies the handshake, a
  frame of the right size, `latest_frame()` without a request and the socket fallback for a client
  that does not ask for shared memory; `tools/test_shared_frame_blender.py` does the same from inside
  Blender. Reading a 1600x900 frame takes about 7 ms, against about 34 ms of socket transfer for the
  same bytes, and `latest_frame()` costs microseconds when there is nothing new.
- The add-on registers a **BGE Revived render engine** for Blender: with it selected, F12 renders
  the scene with the game engine, at Blender's render resolution, and the result is written to the
  output path (verified in `build/render_engine_test/bge_render.png`).
- The socket transport pushes a scene into the running engine and pulls the rendered frame back as
  raw RGBA; the engine resizes to the resolution the authoring tool asks for.
- The live link is incremental: moving an object sends a 16 float matrix, editing its geometry
  re-uploads only that mesh, and the engine rebinds it in place. Both were verified against a
  running engine (`tools/test_incremental.py`).
- The add-on mirrors the frames into a Blender image and draws them over the camera frame in the
  3D View, so the game is visible while the author works.
- The embedded Python host runs `assets/scripts/demo.py`: it finds objects by name, reads and
  writes transforms through `bge.types.Vector`/`Euler`, and drives per-frame work from
  `bge.app.handlers.post_update`. Controllers attached to objects run their script at startup
  with `bge.logic.getCurrentController()` bound to the owner.
- A scene can be saved at runtime and loaded back: `--save-scene` writes the same `.bgescene`
  format the loader reads, keeping meshes, materials, texture paths, lights and cameras.
- The `--physics` sandbox demo runs on Jolt: a static floor body plus 13 dynamic boxes and
  spheres topple into a pile on it (`build/physics_jolt_demo.png`). The same demo also runs on the
  built-in solver with `python build.py --no-jolt`, which uses its own ground plane.
- Physics is authored in Blender and simulated by Jolt in the game: the generated test scene now
  contains a passive floor and three falling crates (`bpy` rigid bodies), the exporter writes them
  as `physics` blocks, and `bge_game --scene build/physics_scene/scene.bgescene` logs
  `Physics: jolt backend` and `Physics: Jolt 5.3.0 ready`. The crates are still in the air on the
  first frame (`build/physics_f1.png`) and rest on the floor after 150 frames
  (`build/physics_f150.png`).
- Demo scene still renders with ~11 draw calls, ~154 instances, ~50k triangles and ~6 ms CPU.

## Out of scope

The classic BGE shipped a few subsystems that are not part of this engine's goal and will not
be implemented: Freestyle line rendering, Grease Pencil, movie clips / sequencer and
multi-view (stereo) rendering.

## Known limitations

- The Jolt backend currently creates boxes and spheres only: capsule, cylinder, convex hull and
  mesh shapes from Blender fall back to a box that encloses the object. Constraints, the character
  controller and ray casts are not exposed yet.
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

1. glTF 2.0 import and a cooked asset format.
2. Audio subsystem (3D positional playback) and XInput gamepad support.
3. Physics follow-up: capsule/convex shapes, constraints, character controller and ray casts.
4. Environment orientation fix, prefiltered IBL and per-light shadow maps.
5. Socket based live link with incremental light/camera updates and a debug UI.
