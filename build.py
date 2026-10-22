#!/usr/bin/env python3
"""BGE Revived build driver.

The project deliberately has no hard dependency on CMake or Visual Studio. It
builds with the vendored Zig toolchain (`zig cc` / `zig c++`) or with a MinGW-w64
GCC toolchain, and it can also use a system Zig or MinGW. The script is
incremental: object files are cached by a content hash of sources, headers and
flags.

Usage:
    python build.py [build|run|test|clean] [options] [-- extra args]

Options:
    --debug            Build with debug symbols and no optimization.
    --release          Build optimized (default).
    --no-python        Do not link the embedded Python host.
    --compiler NAME    auto (default), zig or mingw.
    --jobs N           Parallel compile jobs (default: CPU count).
    --windows          Link the sandbox as a Windows GUI app (no console).
    --verbose          Echo every compiler command.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
TOOLCHAIN = ROOT / ".toolchain" / "zig" / "zig.exe"
OBJ = BUILD / "obj"
BIN = BUILD / "bin"
JOLT_ROOT = ROOT / "engine" / "third_party" / "jolt"
JOLT_SOURCE = JOLT_ROOT / "Jolt"

# Jolt is optional: the engine ships a small built-in solver and uses Jolt when
# the source has been fetched with tools/fetch_jolt.py.
JOLT_DEFINES = [
    "-DJPH_OBJECT_LAYER_BITS=16",
    "-DJPH_CROSS_PLATFORM_DETERMINISTIC",
    "-DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED=0",
]
JOLT_SKIP = ("\\Renderer\\", "\\UnitTests\\", "\\PerformanceTest\\", "\\HelloWorld\\", "\\Samples\\")

# Set from --no-jolt; keeps the built-in solver even when Jolt is checked out.
JOLT_ENABLED = True

EXE_SUFFIX = ".exe" if os.name == "nt" else ""

CXX_FLAGS = [
    "-std=c++20",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-Wno-unused-function",
    "-Wno-cast-function-type-mismatch",
    "-fno-strict-aliasing",
]

INCLUDE_DIRS = [
    ROOT / "engine" / "include",
    ROOT / "engine" / "third_party" / "glad" / "include",
    ROOT / "engine" / "third_party" / "stb",
    ROOT / "engine" / "third_party",
]

LINK_LIBS = ["-lopengl32", "-lgdi32", "-luser32", "-lole32", "-lshell32", "-lwinmm", "-lws2_32"]

SRC_DIRS = [ROOT / "engine" / "src"]


def zig_path() -> Path | None:
    """Locates a usable Zig: the ZIG override, the vendored copy, or PATH."""
    env = os.environ.get("ZIG")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    if TOOLCHAIN.is_file():
        return TOOLCHAIN
    found = shutil.which("zig")
    return Path(found) if found else None


def find_zig() -> Path:
    path = zig_path()
    if path:
        return path
    sys.exit(
        "error: Zig not found.\n"
        "  Place Zig at .toolchain/zig/zig.exe or set the ZIG environment variable."
    )


def find_mingw() -> Path | None:
    """Locate a MinGW-w64 GCC toolchain (g++.exe)."""
    candidates: list[Path] = []
    for var in ("BGE_MINGW_ROOT", "MINGW_ROOT", "MINGW64"):
        value = os.environ.get(var)
        if value:
            candidates.append(Path(value))
    candidates.append(ROOT / ".toolchain" / "mingw64")
    candidates.append(ROOT / "mingw64")
    candidates.append(ROOT / "mingw-w64" / "bin")
    candidates.append(ROOT / "mingw" / "bin")
    candidates.append(ROOT / "toolchains" / "mingw64" / "bin")
    candidates.append(ROOT / "mingw-w64-v11.0.0" / "bin")
    candidates.append(ROOT / "x86_64-w64-mingw32")
    candidates.append(ROOT / "ucrt64")
    candidates.append(Path("C:/mingw64"))
    candidates.append(Path("C:/MinGW"))
    candidates.append(Path("C:/msys64/mingw64"))
    candidates.append(Path("C:/msys64/ucrt64"))
    for root in candidates:
        exe = root / "bin" / ("g++.exe" if os.name == "nt" else "g++")
        if exe.is_file():
            return exe
    found = shutil.which("g++")
    if found:
        return Path(found)
    return None


class Toolchain:
    """A C/C++ driver: either the vendored Zig toolchain or MinGW-w64 GCC."""

    def __init__(self, kind: str, exe: Path):
        self.kind = kind
        self.exe = exe
        self.name = f"{kind} ({exe})"

    def compile_command(self, src: Path, out: Path, config: str, py: dict | None,
                       flags: list[str], verbose: bool) -> list[str]:
        if self.kind == "zig":
            cmd = [str(self.exe)]
            if src.suffix == ".c":
                cmd += ["cc", "-std=c11", "-Wall", "-Wno-unused-function"]
            else:
                cmd += ["c++"] + flags
            cmd += ["-target", "x86_64-windows-gnu"]
        else:
            cmd = [str(self.exe)]
            if src.suffix == ".c":
                cmd += ["-x", "c", "-std=c11", "-Wall", "-Wno-unused-function"]
            else:
                cmd += ["-x", "c++"] + flags
        if config == "debug":
            cmd += ["-O0", "-g", "-DBGE_DEBUG=1"]
        else:
            cmd += ["-O2", "-DNDEBUG=1"]
        for inc in INCLUDE_DIRS:
            cmd += ["-I", str(inc)]
        if py and src.suffix != ".c":
            cmd += ["-I", str(py["include"]), "-DBGE_WITH_PYTHON=1"]
        if JOLT_ENABLED and JOLT_SOURCE.is_dir() and str(src).startswith(str(JOLT_SOURCE)):
            cmd += ["-I", str(JOLT_ROOT)]
            cmd += list(JOLT_DEFINES)
            if config == "debug":
                cmd += ["-DJPH_ENABLE_ASSERTS"]
        elif str(src).startswith(str(ROOT / "engine" / "src")):
            # The engine knows whether Jolt is available.
            if JOLT_ENABLED and JOLT_SOURCE.is_dir():
                cmd += ["-DBGE_WITH_JOLT=1"]
                # jolt_physics.cpp includes the Jolt headers directly.
                cmd += ["-I", str(JOLT_ROOT)]
                cmd += list(JOLT_DEFINES)
        cmd += ["-c", str(src), "-o", str(out)]
        return cmd

    def link_command(self, objects: list[Path], out: Path, config: str, py: dict | None,
                     windows: bool, verbose: bool) -> list[str]:
        if self.kind == "zig":
            cmd = [str(self.exe), "c++"]
            if config == "debug":
                cmd += ["-g"]
            cmd += [str(o) for o in objects]
            cmd += ["-o", str(out)]
            cmd += ["-target", "x86_64-windows-gnu"]
        else:
            cmd = [str(self.exe)]
            cmd += [str(o) for o in objects]
            cmd += ["-o", str(out)]
            if config == "debug":
                cmd += ["-g"]
        cmd += LINK_LIBS
        if py:
            if self.kind == "zig":
                cmd += [str(py["lib"])]
            else:
                cmd += ["-L", str(py["libs"]), "-lpython312"]
        if windows:
            cmd += ["-Wl,--subsystem,windows", "-Wl,--entry,mainCRTStartup"]
        return cmd


def select_toolchain(name: str) -> Toolchain:
    if name == "zig":
        return Toolchain("zig", find_zig())
    if name == "mingw":
        exe = find_mingw()
        if not exe:
            sys.exit(
                "error: MinGW-w64 not found.\n"
                "  Install it under .toolchain/mingw64 or set BGE_MINGW_ROOT."
            )
        return Toolchain("mingw", exe)
    # auto: prefer Zig (override, vendored copy or PATH), fall back to MinGW.
    zig = zig_path()
    if zig:
        return Toolchain("zig", zig)
    exe = find_mingw()
    if exe:
        return Toolchain("mingw", exe)
    sys.exit("error: no C++ toolchain found (install Zig or MinGW-w64).")


def detect_python() -> dict | None:
    """Locate a CPython installation usable for embedding."""
    candidates = []
    if os.environ.get("BGE_PYTHON_ROOT"):
        candidates.append(Path(os.environ["BGE_PYTHON_ROOT"]))
    candidates.append(Path(sys.prefix))
    if os.name == "nt":
        local = Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Python"
        if local.is_dir():
            for child in sorted(local.iterdir(), reverse=True):
                candidates.append(child)
    for root in candidates:
        inc = root / "include"
        libs = root / "libs"
        lib = libs / "python312.lib"
        dll = root / "python312.dll"
        if (inc / "Python.h").is_file() and lib.is_file() and dll.is_file():
            return {"root": root, "include": inc, "lib": lib, "libs": libs, "dll": dll}
    return None


def collect_jolt_sources() -> list[Path]:
    """The Jolt Physics sources, when the optional dependency is present."""
    if not JOLT_ENABLED or not JOLT_SOURCE.is_dir():
        return []
    sources: list[Path] = []
    for path in sorted(JOLT_SOURCE.rglob("*.cpp")):
        text = str(path)
        if any(skip in text for skip in JOLT_SKIP):
            continue
        sources.append(path)
    return sources


def collect_sources() -> tuple[list[Path], list[Path], list[Path], list[Path], list[Path]]:
    engine_cpp: list[Path] = []
    c: list[Path] = []
    for d in SRC_DIRS:
        engine_cpp.extend(sorted(d.rglob("*.cpp")))
    app_cpp = sorted((ROOT / "sandbox" / "src").rglob("*.cpp"))
    game_cpp = sorted((ROOT / "game" / "src").rglob("*.cpp"))
    jolt_cpp = collect_jolt_sources()
    c.append(ROOT / "engine" / "third_party" / "glad" / "src" / "gl.c")
    return engine_cpp, app_cpp, game_cpp, jolt_cpp, c


def collect_headers() -> list[Path]:
    headers = []
    for d in INCLUDE_DIRS:
        if d.is_dir():
            headers.extend(sorted(d.rglob("*.h")))
            headers.extend(sorted(d.rglob("*.hpp")))
    return headers


def fingerprint(files: list[Path], flags: list[str]) -> str:
    h = hashlib.sha256()
    for f in files:
        st = f.stat()
        h.update(str(f.relative_to(ROOT)).encode())
        h.update(str(st.st_mtime_ns).encode())
        h.update(str(st.st_size).encode())
    for f in flags:
        h.update(f.encode())
    return h.hexdigest()


def object_path(config: str, src: Path) -> Path:
    rel = src.relative_to(ROOT)
    return OBJ / config / rel.with_suffix(".o")


def run(cmd: list[str], verbose: bool, cwd: Path = ROOT) -> None:
    if verbose:
        print("  " + " ".join(str(c) for c in cmd))
    proc = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
    if proc.returncode != 0:
        print(proc.stdout)
        print(proc.stderr, file=sys.stderr)
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(map(str, cmd))}")


def compile_one(toolchain: Toolchain, src: Path, out: Path, config: str, py: dict | None,
                flags: list[str], verbose: bool) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    run(toolchain.compile_command(src, out, config, py, flags, verbose), verbose)


def link(toolchain: Toolchain, objects: list[Path], out: Path, config: str, py: dict | None,
         windows: bool, verbose: bool) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    run(toolchain.link_command(objects, out, config, py, windows, verbose), verbose)


def copy_assets() -> None:
    src = ROOT / "assets"
    dst = BIN / "assets"
    if not src.is_dir():
        return
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.rglob("*"):
        if item.is_dir():
            continue
        target = dst / item.relative_to(src)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or item.stat().st_mtime_ns > target.stat().st_mtime_ns:
            shutil.copy2(item, target)


def copy_python(py: dict) -> None:
    BIN.mkdir(parents=True, exist_ok=True)
    for name in ("python312.dll", "python3.dll"):
        src = py["root"] / name
        if src.is_file():
            shutil.copy2(src, BIN / name)


def build(args: argparse.Namespace) -> int:
    toolchain = select_toolchain(args.compiler)
    py = None if args.no_python else detect_python()
    config = "debug" if args.debug else "release"

    engine_cpp, app_cpp, game_cpp, jolt_cpp, c = collect_sources()
    headers = collect_headers()
    sources = engine_cpp + app_cpp + game_cpp + jolt_cpp + c

    base_flags = CXX_FLAGS[:]
    if args.verbose:
        base_flags.append("-v")
    if py:
        base_flags.append("-DBGE_WITH_PYTHON=1")

    print(f"BGE Revived build · {config} · {len(sources)} sources · {toolchain.name}")
    if py:
        print(f"  Python: {py['root']}")
    else:
        print("  Python: disabled")

    stale: list[tuple[Path, Path]] = []
    config_dir = OBJ / config
    config_dir.mkdir(parents=True, exist_ok=True)
    fp_file = config_dir / "fingerprint.txt"
    previous_fp = fp_file.read_text() if fp_file.exists() else ""
    current_fp = fingerprint(sources + headers, base_flags + [toolchain.kind])
    force = previous_fp != current_fp

    for src in sources:
        out = object_path(config, src)
        if force or not out.exists() or out.stat().st_mtime_ns < src.stat().st_mtime_ns:
            stale.append((src, out))

    if stale:
        print(f"  Compiling {len(stale)} file(s)...")
        start = time.time()
        jobs = args.jobs or max(1, (os.cpu_count() or 4))
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            futures = [ex.submit(compile_one, toolchain, s, o, config, py, base_flags, args.verbose)
                       for s, o in stale]
            for f in concurrent.futures.as_completed(futures):
                f.result()
        print(f"  Compiled in {time.time() - start:.1f}s")
    else:
        print("  Objects up to date")

    fp_file.write_text(current_fp)

    # Engine + sandbox
    sandbox_objs = [object_path(config, s) for s in engine_cpp + app_cpp + jolt_cpp + c]
    sandbox_exe = BIN / f"bge_sandbox{EXE_SUFFIX}"
    link(toolchain, sandbox_objs, sandbox_exe, config, py, args.windows, args.verbose)
    copy_assets()
    if py:
        copy_python(py)
    print(f"  Built {sandbox_exe.relative_to(ROOT)}")

    # Game template: the runtime a packaged game ships with.
    game_objs = [object_path(config, s) for s in engine_cpp + game_cpp + jolt_cpp + c]
    game_exe = BIN / f"bge_game{EXE_SUFFIX}"
    link(toolchain, game_objs, game_exe, config, py, args.windows, args.verbose)
    print(f"  Built {game_exe.relative_to(ROOT)}")

    # Tests (engine only; no sandbox main).
    if args.command in ("test", "all"):
        test_src = ROOT / "tests" / "src" / "test_main.cpp"
        tests_exe = BIN / f"bge_tests{EXE_SUFFIX}"
        test_obj = object_path(config, test_src)
        compile_one(toolchain, test_src, test_obj, config, py, base_flags, args.verbose)
        test_link = [object_path(config, s) for s in engine_cpp + jolt_cpp + c] + [test_obj]
        link(toolchain, test_link, tests_exe, config, py, False, args.verbose)
        print(f"  Built {tests_exe.relative_to(ROOT)}")
        env = os.environ.copy()
        if py:
            env["PYTHONHOME"] = str(py["root"])
            env["PYTHONPATH"] = str(py["root"] / "Lib")
        proc = subprocess.run([str(tests_exe)], cwd=ROOT, env=env)
        return proc.returncode

    return 0


def run_sandbox(args: argparse.Namespace) -> int:
    py = None if args.no_python else detect_python()
    exe = BIN / f"bge_sandbox{EXE_SUFFIX}"
    if not exe.exists():
        print("error: sandbox not built", file=sys.stderr)
        return 1
    env = os.environ.copy()
    if py:
        env["PYTHONHOME"] = str(py["root"])
        env["PYTHONPATH"] = str(py["root"] / "Lib")
    cmd = [str(exe)] + list(args.extra)
    print("  Running:", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT, env=env).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="BGE Revived build driver")
    parser.add_argument("command", nargs="?", default="build", choices=["build", "run", "test", "all", "clean"])
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--no-python", action="store_true")
    parser.add_argument("--no-jolt", action="store_true",
                        help="use the built-in solver even when Jolt is checked out")
    parser.add_argument("--compiler", choices=["auto", "zig", "mingw"], default="auto",
                        help="toolchain to use (default: auto, Zig first)")
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--windows", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args, unknown = parser.parse_known_args()
    extra = list(unknown)
    if extra and extra[0] == "--":
        extra = extra[1:]
    args.extra = extra

    global JOLT_ENABLED
    JOLT_ENABLED = JOLT_SOURCE.is_dir() and not args.no_jolt

    if args.command == "clean":
        shutil.rmtree(BUILD, ignore_errors=True)
        print("  Cleaned build/")
        return 0

    code = build(args)
    if code != 0:
        return code
    if args.command in ("run", "all"):
        return run_sandbox(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
