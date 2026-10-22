# BGE Revived — Roadmap

> Last updated: 2026-10-22
>
> The goal is a standalone, modern, clean-room recreation of the Blender Game Engine (BGE),
> later connected to modern Blender as the authoring front-end. We are not porting the old
> source. We are rebuilding the engine around modern architecture and fixing the problems
> that made the original hard to use and maintain.

## Product principles

1. **Standalone runtime first.** The engine runs games without Blender installed.
2. **Blender is the editor.** Authoring stays in modern Blender; the engine is the runtime.
   A live-link addon will provide the old "press P and play" workflow.
3. **Fixed-step simulation, decoupled rendering.** Gameplay never depends on frame rate.
4. **Data-oriented core, scriptable surface.** ECS and jobs internally; Python for gameplay.
5. **Cooked assets, no `.blend` runtime dependency.** glTF/PNG in, compact packs out.
6. **Clean-room and permissively licensed.** No copied BGE/GPL source.
7. **Measure everything.** Built-in profiling, validation, and reproducible performance tests.
8. **Progressive rendering.** OpenGL 4.6 first for reach; Vulkan RHI next for Blender parity.

## Legacy BGE problems → solutions

| Legacy problem | Replacement |
| --- | --- |
| GPL-only codebase | Clean-room MIT/Apache-2.0 engine |
| Single-threaded runtime | Job system, parallel culling/transform/animation, async asset IO |
| Logic tied to frame rate | Fixed 120 Hz simulation + render interpolation |
| Fixed-function/old GL renderer | HDR forward PBR, shadows, instancing, post-processing; RHI abstraction |
| Bullet 2.x physics limits | Jolt Physics, deterministic stepping, proper character controller |
| Python performance cliffs | Batched bindings, native components, one ABI boundary, hot reload |
| No scene/prefab workflow | ECS hierarchy, prefabs, cooked scene format |
| Blender-embedded editor instability | Standalone runtime + Blender live-link addon |
| No asset pipeline | glTF importer, texture compression, dependency graph, pack files |
| 32-bit/Windows-only assumptions | 64-bit, platform abstraction, Linux target |
| No debugging/profiling | In-engine profiler, remote console, GPU/CPU timings |
| No modern input/audio | Unified input, gamepad support, miniaudio backend |
| No UI | Immediate-mode debug UI, then RmlUi for game UI |
| Python version lock | Current CPython, versioned host, optional Lua/WASM later |

## Phases

### M0 - Foundation (done)

**Goal:** a buildable engine skeleton and a verified, good-looking demo.

- [x] Clean-room repository layout and design docs
- [x] Zero-install Windows toolchain (Zig) plus an optional CMake/Ninja path
- [x] Logging, assertions, math, ECS, job system, fixed timestep
- [x] Win32 window, OpenGL 4.6 context, keyboard/mouse input
- [x] Build script + unit tests (32 checks)
- [x] HDR PBR forward renderer with directional shadows
- [x] Instanced draw batching, frustum culling, post-processing
- [x] Sandbox demo + headless screenshot verification
- [x] HTML documentation site

**Exit criteria (met):** `python build.py run` opens a stable demo; `python build.py test` passes;
headless screenshots match the documented reference.

### M1 - Playable runtime (current)

- [x] Blender scene export/import pipeline (`.bgescene` JSON + binary buffers)
- [x] All four Blender light types, world environment, camera and material settings
- [x] CMake build path alongside the Zig driver, MinGW-w64 supported as an alternative
- [x] Embedded CPython host with a `bge` module (`bge.types`, `bge.objects`, `bge.logic.scene`,
      `bge.app.handlers`) and a scripted demo
- [x] Runtime scene serialization: the `.bgescene` format is written and read by the engine
- [x] Built-in rigid body solver (spheres, boxes, ground plane, impulses) as a Jolt stand-in
- [x] Jolt Physics 5.3.0 vendored with `tools/fetch_jolt.py`; the runtime picks it over the
      built-in solver and both build paths link it
- [x] Blender rigid body objects exported as `physics` blocks and simulated by the engine
- [ ] Prefabs and a runtime asset pack
- [ ] Wider scripting API: logic bricks, timers, custom properties, scene switching
- [ ] Gamepad input, audio (miniaudio), simple UI
- [ ] Jolt follow-up: capsule/convex shapes, character controller, collision events
- [ ] Runtime profiler overlay and remote console
- [ ] Asset import: glTF meshes/materials, PNG/DDS textures, hot reload

**Exit criteria:** a small walkable level with physics, Python gameplay, audio, and saved scenes,
imported from a real Blender scene.

### Not in scope

Some classic BGE subsystems are deliberately excluded: Freestyle line rendering, Grease Pencil,
movie clips / sequencer and multi-view (stereo) rendering are not part of this engine.

### M2 — Rendering and content pipeline

- Cooked asset packs with dependency tracking
- PBR material graph subset, normal maps, parallax, decals
- Cascaded shadow maps, point/spot lights, clustered forward lighting
- SSAO, bloom, color grading, TAA/FXAA
- Animation: skeletal, blend trees, events
- Particles and GPU instancing improvements

### M3 - Blender live link (partially done)

The socket based bridge is working and the frame pixels now bypass the socket through shared
memory. What is left is authoring feedback: lights and cameras in the incremental path, selection
round trips and baked lighting.

- [x] Blender add-on: export, play, stop and package from the 3D View sidebar
- [x] Blender render engine: "BGE Revived" next to Cycles and EEVEE, rendering with the game engine
- [x] Socket transport: the engine serves scenes and frames over a local TCP port
- [x] Shared memory frames: the pixels travel through a named region, so the socket only carries
      small messages and the preview never waits for a round trip
- [x] Incremental live link: only the changed objects, transforms and geometry are transferred
- [x] Viewport preview: the rendered frame is drawn over the camera frame in the 3D View
- [x] Live link: the engine reloads the scene in place when the scene file changes
- [x] Engine preview: the engine renders the scene and the frames are mirrored into Blender
- [x] Game template and packaging: a self contained game folder (and zip)
- [ ] Incremental updates for lights and cameras too
- [ ] Selection and entity inspection in Blender
- [ ] Bake lighting/occlusion back into the engine

### M4 — Editor-grade tooling

- Standalone debug inspector and scene outliner
- Profiler timeline, GPU captures, stats
- Package/export pipeline (single executable + pack)
- Crash reporting and deterministic replay

### M5 — Scale and platform

- Vulkan RHI, Linux target, Steam Deck verification
- Render graph, bindless resources, GPU-driven culling
- Networking (client/server, rollback-friendly snapshot API)
- Streaming worlds and LOD

## Working agreement

- `current_state.md` and `roadmap.md` are updated with every meaningful change.
- `docs/*.html` is kept in sync with those two files.
- Every milestone ends with a runnable artifact and a measured result, not just code.
