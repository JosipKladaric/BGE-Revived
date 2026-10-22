#!/usr/bin/env python3
"""Client for the BGE Revived preview server.

The engine runs a tiny local TCP server for scenes, commands and short replies,
and publishes the rendered frames into a named shared memory region that this
module maps. It is used by the Blender add-on and by the self test below:

    python tools/preview_client.py --scene build/my_scene/scene.bgescene --out frame.png
"""
from __future__ import annotations

import argparse
import json
import mmap
import os
import socket
import struct
import sys
import time
from pathlib import Path

DEFAULT_PORT = 8765

# Shared frame buffer layout, mirrored from engine/include/bge/net/shared_frame.h.
SHARED_MAGIC = 0x31454742  # "BGE1"
SHARED_VERSION = 1
SHARED_HEADER_SIZE = 4096
SHARED_SERIAL_OFFSET = 32


class SharedFrameReader:
    """Reads frames from the engine's shared memory region.

    The engine publishes each frame with a seqlock: the serial is odd while the
    pixels are being written and even once they are complete. A reader that sees
    the serial change under it simply tries again, so a frame is never torn.
    """

    def __init__(self, name: str, capacity: int):
        self.name = name
        self.capacity = int(capacity)
        self._map = None
        self._view = None
        self._last_serial = 0

    @property
    def is_open(self) -> bool:
        return self._view is not None

    def open(self) -> bool:
        self.close()
        if not self.name or self.capacity <= 0:
            return False
        try:
            total = SHARED_HEADER_SIZE + self.capacity
            if os.name == "nt":
                # tagname with fileno -1 opens the mapping the engine created.
                self._map = mmap.mmap(-1, total, tagname=self.name)
                self._view = self._map
            else:
                from multiprocessing import shared_memory

                self._map = shared_memory.SharedMemory(name=self.name)
                self._view = self._map.buf
        except Exception as exc:  # a missing region is not fatal, TCP still works
            print(f"[BGE] shared frame buffer unavailable ({exc}); using the socket")
            self._map = None
            self._view = None
            return False

        magic, version, _header_size, capacity = struct.unpack_from("<IIII", self._view, 0)
        if magic != SHARED_MAGIC or version != SHARED_VERSION:
            print("[BGE] shared frame buffer has an unexpected layout; using the socket")
            self.close()
            return False
        # Trust the header: it is the size the writer actually reserved.
        self.capacity = int(capacity)
        return True

    def close(self) -> None:
        self._view = None
        if self._map is not None:
            try:
                self._map.close()
            except Exception:
                pass
            self._map = None
        self._last_serial = 0

    def serial(self) -> int:
        if self._view is None:
            return 0
        return struct.unpack_from("<Q", self._view, SHARED_SERIAL_OFFSET)[0]

    def read(self, force: bool = False):
        """Returns (width, height, rgba) for a new frame, or None.

        With force the current frame is returned even when it has not changed
        since the previous read.
        """
        view = self._view
        if view is None:
            return None

        for _ in range(50):
            serial = struct.unpack_from("<Q", view, SHARED_SERIAL_OFFSET)[0]
            if serial & 1:  # a write is in progress
                # A tight loop would burn every attempt while the engine copies
                # the pixels, so give it a moment.
                time.sleep(0.0002)
                continue
            if serial == 0:
                return None
            if not force and serial == self._last_serial:
                return None

            width, height, stride = struct.unpack_from("<III", view, 16)
            if width <= 0 or height <= 0 or stride < width * 4:
                return None
            needed = stride * height
            if needed > self.capacity or SHARED_HEADER_SIZE + needed > len(view):
                return None

            data = bytes(view[SHARED_HEADER_SIZE:SHARED_HEADER_SIZE + needed])
            if struct.unpack_from("<Q", view, SHARED_SERIAL_OFFSET)[0] != serial:
                continue  # the writer overlapped the copy

            self._last_serial = serial
            return width, height, data
        return None



class PreviewClient:
    """A small blocking client for the engine's preview server."""

    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_PORT, timeout: float = 30.0,
                 use_shared: bool = True):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.use_shared = use_shared
        self.socket: socket.socket | None = None
        self.shared: SharedFrameReader | None = None
        self.info: dict = {}

    # ------------------------------------------------------------- setup ----
    def connect(self) -> bool:
        """Connects, says hello and attaches to the shared frame buffer."""
        self.close()
        try:
            self.socket = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError:
            return False
        self.socket.settimeout(self.timeout)
        try:
            # The handshake tells us where the pixels live; a slow engine should
            # not eat the whole timeout here.
            self.socket.settimeout(min(self.timeout, 5.0))
            self.info = self.hello()
            self.socket.settimeout(self.timeout)
        except (OSError, ValueError):
            self.close()
            return False
        self._open_shared()
        return True

    def _open_shared(self) -> None:
        if not self.use_shared:
            self.shared = None
            return
        name = self.info.get("shm")
        capacity = int(self.info.get("shm_capacity") or 0)
        if not name or capacity <= 0:
            self.shared = None
            return
        reader = SharedFrameReader(str(name), capacity)
        self.shared = reader if reader.open() else None

    @property
    def has_shared(self) -> bool:
        """True when frames travel through shared memory instead of the socket."""
        return self.shared is not None and self.shared.is_open

    def close(self) -> None:
        if self.shared is not None:
            self.shared.close()
            self.shared = None
        if self.socket is not None:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None
        self.info = {}

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
        self.send({"cmd": "hello", "shm": self.use_shared})
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

        The pixels normally come from the shared frame buffer; `payload` is only
        filled in by the socket fallback.
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
                    return width, height, self._frame_pixels(reply, payload)
                attempts += 1
                if attempts > 60:
                    raise RuntimeError(
                        f"the engine rendered {width}x{height} instead of "
                        f"{expected_width}x{expected_height}")
                time.sleep(0.05)
                self.send({"cmd": "render"})
                continue
            return width, height, self._frame_pixels(reply, payload)

    def _frame_pixels(self, reply: dict, payload: bytes) -> bytes:
        """The pixels of a `frame` reply: shared memory first, socket second."""
        if reply.get("shm") and self.has_shared:
            frame = self.shared.read(force=True)
            if frame is not None:
                width, height, data = frame
                if (width, height) == (int(reply["width"]), int(reply["height"])):
                    return data
            raise RuntimeError("the shared frame buffer did not contain the frame")
        return payload

    def latest_frame(self):
        """The newest frame without asking the engine for one.

        Returns (width, height, rgba), or None when no new frame was published
        since the last call. This is what a live preview polls: no round trip,
        no waiting for the engine to answer.
        """
        if not self.has_shared:
            return None
        return self.shared.read()

    # ------------------------------------------------- incremental updates ----

    def _read_ack(self) -> dict:
        """Consumes the acknowledgement of a scene update."""
        reply, _ = self.receive()
        return reply

    def push_transforms(self, entries) -> None:
        """entries: [(name, matrix)] where matrix has 16 floats."""
        payload = bytearray()
        for name, matrix in entries:
            encoded = name.encode("utf-8")
            payload += struct.pack("<H", len(encoded)) + encoded
            payload += struct.pack("<16f", *matrix)
        self.send({"cmd": "transform", "count": len(entries)}, bytes(payload))
        self._read_ack()

    def push_objects(self, entries) -> None:
        """entries: [(name, mesh_id, material_id, visible, matrix)]."""
        payload = bytearray()
        for name, mesh_id, material_id, visible, matrix in entries:
            encoded = name.encode("utf-8")
            payload += struct.pack("<H", len(encoded)) + encoded
            payload += struct.pack("<iiB", int(mesh_id), int(material_id), 1 if visible else 0)
            payload += struct.pack("<16f", *matrix)
        self.send({"cmd": "object", "count": len(entries)}, bytes(payload))
        self._read_ack()

    def push_mesh(self, mesh_id: int, vertices, indices) -> None:
        """Uploads new geometry: vertices are (position, normal, uv) tuples."""
        payload = bytearray()
        for vertex in vertices:
            payload += struct.pack("<8f", *vertex)
        for index in indices:
            payload += struct.pack("<I", int(index))
        self.send({"cmd": "mesh", "id": int(mesh_id),
                   "vertex_count": len(vertices), "index_count": len(indices)}, bytes(payload))
        self._read_ack()

    def push_remove(self, names) -> None:
        """Removes objects by name."""
        payload = bytearray()
        for name in names:
            encoded = name.encode("utf-8")
            payload += struct.pack("<H", len(encoded)) + encoded
        self.send({"cmd": "remove", "count": len(names)}, bytes(payload))
        self._read_ack()


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
        print("hello:", client.info)
        print("frame transport:", "shared memory" if client.has_shared else "socket")
        if not client.has_shared:
            print("warning: the engine did not provide a shared frame buffer", file=sys.stderr)
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
