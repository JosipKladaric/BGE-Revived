#!/usr/bin/env python3
"""Download the Jolt Physics source used by the engine.

    python tools/fetch_jolt.py                 # fetch the default version
    python tools/fetch_jolt.py --version v5.3.0
    python tools/fetch_jolt.py --force         # re-download even when present

The source is placed in engine/third_party/jolt and is not committed: the build
picks it up when it is there, and the engine falls back to the built-in solver
when it is not.
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESTINATION = ROOT / "engine" / "third_party" / "jolt"
MARKER = DESTINATION / ".jolt_version"

DEFAULT_VERSION = "v5.3.0"

MIRRORS = [
    "https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/refs/tags/{version}",
    "https://codeload.github.com/jrouwe/JoltPhysics/zip/refs/tags/{version}",
    "https://github.com/jrouwe/JoltPhysics/archive/refs/tags/{version}.zip",
]


def human(size: float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if size < 1024.0:
            return f"{size:.1f} {unit}"
        size /= 1024.0
    return f"{size:.1f} TiB"


def ssl_context(insecure: bool):
    """A TLS context that also works behind a TLS inspecting proxy.

    Some networks re-sign the traffic with a certificate that is not in the
    Python trust store. We prefer the bundled CA bundle, then the system store,
    and only then fall back to an unverified connection with a loud warning.
    """
    import ssl

    if insecure:
        print("  warning: certificate verification is disabled")
        return ssl._create_unverified_context()

    try:
        import certifi  # type: ignore

        return ssl.create_default_context(cafile=certifi.where())
    except Exception:
        pass
    return ssl.create_default_context()


def download(url: str, destination: Path, context, timeout: float = 120.0) -> bool:
    request = urllib.request.Request(url, headers={"User-Agent": "bge-revived-build"})
    try:
        with urllib.request.urlopen(request, timeout=timeout, context=context) as response:
            total = response.headers.get("Content-Length")
            total_size = int(total) if total else 0
            done = 0
            with open(destination, "wb") as handle:
                while True:
                    chunk = response.read(1 << 16)
                    if not chunk:
                        break
                    handle.write(chunk)
                    done += len(chunk)
                    if total_size:
                        print(f"\r  {human(done)} / {human(total_size)}", end="", flush=True)
            print()
            return done > 0
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        print(f"  failed: {exc}")
        return False


def looks_like_archive(path: Path) -> bool:
    """Checks the magic bytes so an error page is never treated as a download."""
    try:
        with open(path, "rb") as handle:
            head = handle.read(4)
    except OSError:
        return False
    if head[:2] == b"\x1f\x8b":
        return True  # gzip
    if head[:2] == b"PK":
        return True  # zip
    return False


def extract(archive: Path, target: Path) -> Path:
    """Extracts the archive and returns the folder that holds the source root."""
    with open(archive, "rb") as handle:
        magic = handle.read(2)
    with tempfile.TemporaryDirectory() as temporary:
        staging = Path(temporary)
        if magic == b"PK":
            with zipfile.ZipFile(archive) as zf:
                zf.extractall(staging)
        else:
            with tarfile.open(archive) as tf:
                tf.extractall(staging)
        entries = [entry for entry in staging.iterdir() if entry.is_dir()]
        if not entries:
            raise SystemExit("the archive did not contain a source folder")
        source = entries[0]
        if target.exists():
            shutil.rmtree(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(source), str(target))
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description="Download the Jolt Physics source")
    parser.add_argument("--version", default=DEFAULT_VERSION, help="the tag to download")
    parser.add_argument("--force", action="store_true", help="download even when the source is present")
    parser.add_argument("--insecure", action="store_true",
                        help="skip TLS verification (only for a proxy that re-signs the traffic)")
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()

    if DESTINATION.exists() and not args.force:
        version = MARKER.read_text(encoding="utf-8").strip() if MARKER.exists() else "unknown"
        print(f"Jolt is already present in {DESTINATION} (version {version})")
        print("use --force to download it again")
        return 0

    with tempfile.TemporaryDirectory() as temporary:
        context = ssl_context(args.insecure)
        for template in MIRRORS:
            url = template.format(version=args.version)
            archive = Path(temporary) / "jolt-download.bin"
            print(f"downloading {url}")
            if not download(url, archive, context, args.timeout):
                continue
            if archive.stat().st_size < 100_000:
                print("  the download is too small, ignoring it")
                continue
            if not looks_like_archive(archive):
                print("  the download is not an archive, ignoring it")
                continue
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()[:16]
            print(f"  {human(archive.stat().st_size)} (sha256 {digest})")
            extract(archive, DESTINATION)
            MARKER.write_text(args.version + "\n", encoding="utf-8")
            print(f"Jolt {args.version} is in {DESTINATION}")
            return 0

    print("error: could not download Jolt Physics", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
