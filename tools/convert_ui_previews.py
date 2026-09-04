#!/usr/bin/env python3
"""Convert the host renderer's deterministic PPM snapshots to PNG (stdlib only)."""
from __future__ import annotations

from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def main() -> int:
    for source in sorted((ROOT / "build" / "ui-previews").glob("*.ppm")):
        magic, dimensions, maximum, pixels = source.read_bytes().split(b"\n", 3)
        width, height = map(int, dimensions.split())
        if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
            raise ValueError(f"Invalid preview: {source}")
        stride = width * 3
        rows = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
        source.with_suffix(".png").write_bytes(png)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
