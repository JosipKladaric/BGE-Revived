#!/usr/bin/env python3
"""Build an installable zip of the BGE Revived Blender add-on.

    python tools/make_addon_zip.py

The result is written to build/bge_revival.zip and can be installed in Blender
through Preferences > Add-ons > Install from Disk.
"""
from __future__ import annotations

import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDON = ROOT / "tools" / "blender_addon" / "bge_revival"
OUT = ROOT / "build" / "bge_revival.zip"


def main() -> int:
    if not (ADDON / "__init__.py").is_file():
        raise SystemExit(f"the add-on was not found in {ADDON}")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    if OUT.exists():
        OUT.unlink()
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as zf:
        for item in sorted(ADDON.rglob("*")):
            if item.is_file() and item.suffix != ".pyc":
                zf.write(item, Path(ADDON.name) / item.relative_to(ADDON))
    print(f"Wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
