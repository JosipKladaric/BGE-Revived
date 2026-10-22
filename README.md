# BGE Revived

A clean-room, standalone revival of the Blender Game Engine.

Modern C++20, a Win32 + OpenGL 4.6 renderer, a sparse-set ECS with a job system, Jolt Physics,
an embedded CPython host and a Blender 4.x authoring add-on. This is **not** a port of the
original BGE/GPL source: the behaviour is re-implemented from scratch and the legacy problems
(frame-rate-coupled logic, single-threaded runtime, Bullet 2.x limits, no asset pipeline) are
replaced rather than reproduced.

## Status

M1 "playable runtime" is in progress. Working today:

- Blender `.blend` → `.bgescene` export with all four light types, world environment, cameras,
  materials and textures, and sampled object animation
- HDR PBR forward renderer with shadows, post processing and a game camera
- Jolt Physics 5.3.0 rigid bodies fed by Blender rigid body objects, with a built-in solver as
  the fallback when Jolt is not fetched
- Embedded Python 3.12 with a `bge` module (`bge.types`, `bge.objects`, `bge.app.handlers`, …)
- Live link: play from Blender, edit, and watch the engine's own frames inside the 3D View
- "BGE Revived" registered as a render engine next to Cycles and EEVEE
- `bge_game.exe` runtime plus `tools/package_game.py` for distributable game folders

See [`current_state.md`](current_state.md) for the verified inventory and known gaps, and
[`roadmap.md`](roadmap.md) for the plan.

## Requirements

- Windows 10/11, x86-64
- Python 3.12
- A C++20 compiler: the vendored Zig 0.14.1, a system Zig (`ZIG` environment variable), or
  MinGW-w64 GCC (`--compiler mingw`)
- Blender 4.4 only for authoring, not for running a game

The Zig toolchain is *not* committed to this repository because of its size. Unpack Zig 0.14.1 so
that `.toolchain/zig/zig.exe` exists, or point the `ZIG` environment variable at `zig.exe`.

## Build, run, test

```bat
python build.py                     :: engine + sandbox
python build.py run                 :: build and launch the procedural demo
python build.py test                :: foundation tests
python build.py --compiler mingw    :: build with MinGW-w64 instead of Zig
python build.py --no-jolt           :: build without the Jolt backend
```

The same project builds through CMake + Ninja:

```bat
cmake -S . -B build/cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake
cmake --build build/cmake
build\cmake\bin\bge_tests.exe
```

### Optional: Jolt Physics

Jolt is an optional dependency and its source is not committed. Fetch it once:

```bat
python tools/fetch_jolt.py
```

It is unpacked into `engine/third_party/jolt` and both build paths pick it up automatically;
without it the engine falls back to its small built-in rigid body solver.

## Blender authoring

```bat
python tools/make_addon_zip.py                 :: writes build/bge_revival.zip
```

Install that zip in Blender 4.4 (*Edit ▸ Preferences ▸ Add-ons ▸ Install…*), then open the
**BGE Revived** tab in the 3D View sidebar (N):

- **Export Scene** writes `<export folder>/scene.bgescene`
- **Play** launches the engine on the scene and keeps reloading it as you edit
- **Start Preview** mirrors the engine's rendered frames into the 3D View
- **Package Game** builds a self-contained game folder (and a zip)

Objects with a Blender rigid body (active or passive) become physics bodies in the runtime.

Render a scene headlessly:

```bat
blender --background scene.blend --python tools/bge_export.py -- --out build/scene
build\bin\bge_game.exe --scene build\scene\scene.bgescene --headless --frames 120 --screenshot out.png
```

## Repository layout

| Path | Contents |
| --- | --- |
| `engine/` | Engine library: platform, renderer, ECS, scene, physics, scripting, networking |
| `sandbox/` | Development application: procedural demo, scene viewer, headless captures |
| `game/` | The runtime a packaged game ships with (`bge_game.exe`) |
| `tests/` | Dependency-free foundation test runner |
| `tools/` | Blender exporter, add-on, preview client, packaging and fetch scripts |
| `docs/` | The HTML documentation site (start at `docs/index.html`) |
| `cmake/` | CMake toolchain files for Zig and MinGW-w64 |

## Documentation

Open `docs/index.html` in a browser. The two Markdown files are the human-readable source of
truth and are kept in sync with the HTML pages.

## License

No license file is present yet. The project is intended to be permissively licensed
(MIT/Apache-2.0) and contains no original BGE/GPL code; third-party components under
`engine/third_party` keep their own licenses.
