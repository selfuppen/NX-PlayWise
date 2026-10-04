#!/usr/bin/env python3
"""Convert the host renderer's deterministic PPM snapshots to PNG (stdlib only).

Supports incremental conversion based on file modification times and parallel
conversion across CPU cores via ProcessPoolExecutor.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor
import os
from pathlib import Path
import struct
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def convert_one(source_path: str) -> None:
    source = Path(source_path)
    magic, dimensions, maximum, pixels = source.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError(f"Invalid preview: {source}")
    stride = width * 3
    rows = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    target = source.with_suffix(".png")
    target.write_bytes(png)


def parse_args(args: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert PPM preview snapshots to PNG.")
    parser.add_argument(
        "--dir",
        type=Path,
        default=ROOT / "build" / "ui-previews",
        help="Directory containing PPM previews (default: build/ui-previews)",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=None,
        help="Number of worker processes for parallel conversion (default: auto)",
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Force re-conversion of all PPM previews regardless of timestamp",
    )
    return parser.parse_args(args)


def convert_all(
    preview_dir: Path,
    *,
    jobs: int | None = None,
    force: bool = False,
) -> tuple[int, int, int]:
    """Convert all PPM files in preview_dir to PNG.

    Returns (converted_count, skipped_count, total_count).
    """
    if not preview_dir.is_dir():
        return 0, 0, 0

    sources = sorted(preview_dir.rglob("*.ppm"))
    tasks: list[Path] = []
    skipped = 0

    for source in sources:
        target = source.with_suffix(".png")
        if (
            not force
            and target.is_file()
            and target.stat().st_size > 0
            and target.stat().st_mtime >= source.stat().st_mtime
        ):
            skipped += 1
        else:
            tasks.append(source)

    total = len(sources)
    converted = len(tasks)

    if not tasks:
        return 0, skipped, total

    if (jobs is not None and jobs <= 1) or len(tasks) == 1:
        for task in tasks:
            convert_one(str(task))
    else:
        max_workers = jobs or min(os.cpu_count() or 4, 16, len(tasks))
        with ProcessPoolExecutor(max_workers=max_workers) as executor:
            chunksize = max(1, len(tasks) // (max_workers * 4))
            list(executor.map(convert_one, [str(p) for p in tasks], chunksize=chunksize))

    return converted, skipped, total


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    t0 = time.perf_counter()
    converted, skipped, total = convert_all(args.dir, jobs=args.jobs, force=args.force)
    elapsed = time.perf_counter() - t0
    print(
        f"PASS: convert-previews: {converted} converted, {skipped} up-to-date "
        f"({total} total) in {elapsed:.2f}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
