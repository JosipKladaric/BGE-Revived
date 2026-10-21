#!/usr/bin/env python3
"""Client for the BGE Revived preview server.

The engine runs a tiny local TCP server; this module talks to it. It is used by
the Blender add-on and by the self test below:

    python tools/preview_client.py --scene build/my_scene/scene.bgescene --out frame.png
"""
from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time
from pathlib import Path

DEFAULT_PORT = 8765


class PreviewClient:
    """A small blocking client for the engine's preview server."""

    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_PORT, timeout: float = 30.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.socket: socket.socket | None = None

    # ------------------------------------------------------------- setup ----
    def connect(self) -> bool:
        self.close()
        try:
            self.socket = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError:
            return False
        self.socket.settimeout(self.timeout)
        return True

    def close(self) -> None:
        if self.socket is not None:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()

    # ---------------------------------------------------------- protocol ----
    def send(self, message: dict, payload: bytes = b"") -> None:
        if self.socket is None:
            raise RuntimeError("the client is not connected")
        header = json.dumps(message).encode("utf-8")
        self.socket.sendall(struct.pack("<II", len(header), len(payload)) + header + payload)

    def receive(self) -> tuple[dict, bytes]:
        if self.socket is None:
            raise RuntimeError("the client is not connected")
        header = self._read_exactly(8)
        json_length, payload_length = struct.unpack("<II", header)
        raw = self._read_exactly(json_length)
        payload = self._read_exactly(payload_length) if payload_length else b""
        return json.loads(raw.decode("utf-8")), payload

    def _read_exactly(self, count: int) -> bytes:
        assert self.socket is not None
        data = bytearray()
        while len(data) < count:
            chunk = self.socket.recv(count - len(data))
            if not chunk:
                raise ConnectionError("the engine closed the connection")
            data.extend(chunk)
        return bytes(data)

    # ------------------------------------------------------------ helpers ----
    def hello(self) -> dict:
        self.send({"cmd": "hello"})
        reply, _ = self.receive()
        return reply

    def push_scene(self, manifest: str, binary: bytes) -> dict:
        self.send({"cmd": "scene", "json": manifest, "bin": len(binary)}, binary)
        reply, _ = self.receive()
        return reply

    def resize(self, width: int, height: int) -> dict:
        self.send({"cmd": "resize", "width": width, "height": height})
        reply, _ = self.receive()
        return reply

    def request_frame(self, expected_width: int = 0, expected_height: int = 0) -> tuple[int, int, bytes]:
        """Asks the engine to render and returns the frame (width, height, RGBA).

        When an expected size is given the client keeps asking until the engine
        has applied the resize, so a caller always gets the resolution it wants.
        """
        self.send({"cmd": "render"})
        attempts = 0
        while True:
            reply, payload = self.receive()
            if reply.get("cmd") != "frame":
                continue
            if not reply.get("ok", False):
                raise RuntimeError("the engine has no frame yet")
            width = int(reply["width"])
            height = int(reply["height"])
            if expected_width and expected_height:
                if (width, height) == (expected_width, expected_height):
                    return width, height, payload
                attempts += 1
                if attempts > 60:
                    raise RuntimeError(
                        f"the engine rendered {width}x{height} instead of "
                        f"{expected_width}x{expected_height}")
                time.sleep(0.05)
                self.send({"cmd": "render"})
                continue
            return width, height, payload


def write_png(path: str, width: int, height: int, rgba: bytes) -> None:
    """Writes raw RGBA bytes as a PNG without any third party dependency."""
    import struct
    import zlib

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter type: none
        raw.extend(rgba[y * stride:(y + 1) * stride])

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def main() -> int:
    parser = argparse.ArgumentParser(description="Talk to the BGE Revived preview server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--scene", default=None, help="scene manifest to push")
    parser.add_argument("--out", default=None, help="write the received frame to this PNG")
    parser.add_argument("--width", type=int, default=0, help="ask the engine to resize")
    parser.add_argument("--height", type=int, default=0)
    args = parser.parse_args()

    client = PreviewClient(args.host, args.port)
    if not client.connect():
        print(f"could not connect to {args.host}:{args.port}", file=sys.stderr)
        return 1
    try:
        print("hello:", client.hello())
        if args.scene:
            manifest_path = Path(args.scene).resolve()
            manifest = manifest_path.read_text(encoding="utf-8")
            binary_path = manifest_path.parent / "scene.bin"
            binary = binary_path.read_bytes() if binary_path.is_file() else b""
            print("push:", client.push_scene(manifest, binary))
        if args.width and args.height:
            print("resize:", client.resize(args.width, args.height))
        width, height, pixels = client.request_frame()
        print(f"frame: {width}x{height}, {len(pixels)} bytes")
        if args.out:
            write_png(args.out, width, height, pixels)
            print(f"wrote {args.out}")
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
