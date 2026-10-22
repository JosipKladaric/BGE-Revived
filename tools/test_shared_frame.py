"""End to end check of the shared memory frame transport.

Starts the game runtime headless, connects the preview client and verifies that

  * the engine advertises a shared frame buffer,
  * a frame request is answered from that buffer,
  * the preview can take new frames without asking (latest_frame),
  * a client that does not understand shared memory still gets the pixels
    over the socket.

    python tools/test_shared_frame.py
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from preview_client import PreviewClient  # noqa: E402

PORT = 8796
WIDTH = 320
HEIGHT = 180
EXE = ROOT / "build" / "bin" / "bge_game.exe"


def find_scene() -> Path | None:
    for candidate in ("blender_test_scene", "blender_env_scene", "preview_test"):
        path = ROOT / "build" / candidate / "scene.bgescene"
        if path.is_file():
            return path
    return None


def start_engine(scene: Path | None) -> subprocess.Popen:
    command = [str(EXE), "--preview-port", str(PORT), "--headless", "--frames", "0",
               "--preview-size", f"{WIDTH}x{HEIGHT}", "--width", str(WIDTH), "--height", str(HEIGHT)]
    if scene is not None:
        command += ["--scene", str(scene)]
    print("[TEST] starting:", " ".join(command))
    return subprocess.Popen(command, cwd=str(EXE.parent))


def connect_with_retry(timeout: float = 15.0) -> PreviewClient:
    deadline = time.time() + timeout
    while time.time() < deadline:
        client = PreviewClient(port=PORT)
        if client.connect():
            return client
        time.sleep(0.2)
    raise RuntimeError("the engine never accepted a connection")


def wait_for_frame(client: PreviewClient, timeout: float = 10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        frame = client.latest_frame()
        if frame is not None:
            return frame
        time.sleep(0.01)
    return None


def main() -> int:
    if not EXE.is_file():
        print(f"error: {EXE} is missing; run `python build.py` first", file=sys.stderr)
        return 1

    scene = find_scene()
    print("[TEST] scene:", scene or "(none, preview mode)")
    process = start_engine(scene)
    failures = 0

    def check(condition: bool, message: str) -> None:
        nonlocal failures
        print(("[TEST] ok   " if condition else "[TEST] FAIL ") + message)
        if not condition:
            failures += 1

    try:
        client = connect_with_retry()
        try:
            check(client.has_shared, "the engine advertises a shared frame buffer")
            print("[TEST] hello:", client.info)

            frame = wait_for_frame(client)
            check(frame is not None, "latest_frame() returned a frame without a request")
            if frame is not None:
                width, height, pixels = frame
                check((width, height) == (WIDTH, HEIGHT),
                      f"shared frame size is {width}x{height}")
                check(len(pixels) == WIDTH * HEIGHT * 4,
                      f"shared frame carries {len(pixels)} bytes")

            width, height, pixels = client.request_frame()
            check((width, height) == (WIDTH, HEIGHT), "request_frame() returned the right size")
            check(len(pixels) == WIDTH * HEIGHT * 4, "request_frame() returned full RGBA")
            check(any(pixels), "the frame is not blank")

            # Two reads in a row must not return the exact same serial.
            first = client.shared.serial()
            second = client.shared.serial()
            time.sleep(0.05)
            check(client.shared.serial() != first or second != first,
                  "the engine keeps publishing frames")
        finally:
            client.close()

        # The engine only serves one client at a time; give it a moment to see
        # the disconnect before the fallback connection arrives.
        time.sleep(0.3)
        legacy = PreviewClient(port=PORT, use_shared=False)
        try:
            check(legacy.connect(), "a second client connected")
            check(not legacy.has_shared, "the second client did not ask for shared memory")
            width, height, pixels = legacy.request_frame()
            check((width, height) == (WIDTH, HEIGHT),
                  "the socket fallback returned the right size")
            check(len(pixels) == WIDTH * HEIGHT * 4,
                  "the socket fallback carried the full frame")
        finally:
            legacy.close()
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()

    if failures == 0:
        print("[TEST] OK")
        return 0
    print(f"[TEST] {failures} check(s) failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
