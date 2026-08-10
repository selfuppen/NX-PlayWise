#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import zipfile


ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


class DeliveryPackageError(RuntimeError):
    pass


def add_file(package: zipfile.ZipFile, source: Path, archive_name: str) -> None:
    if not source.is_file():
        raise DeliveryPackageError(f"missing delivery input: {source}")
    info = zipfile.ZipInfo(archive_name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    package.writestr(info, source.read_bytes())


def build_delivery_package(standard_package: Path, offline_html: Path, output: Path) -> None:
    if output.resolve() in {standard_package.resolve(), offline_html.resolve()}:
        raise DeliveryPackageError("delivery output must not overwrite an input")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.tmp")
    try:
        with zipfile.ZipFile(temporary, "w") as package:
            add_file(package, standard_package, standard_package.name)
            add_file(package, offline_html, "playwise-offline.html")
        temporary.replace(output)
    finally:
        if temporary.exists():
            temporary.unlink()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the PlayWise complete delivery package.")
    parser.add_argument("--standard-package", type=Path, required=True)
    parser.add_argument("--offline-html", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        build_delivery_package(args.standard_package, args.offline_html, args.output)
    except (OSError, DeliveryPackageError, zipfile.BadZipFile) as exc:
        print(f"FAIL: delivery package: {exc}")
        return 1
    print(f"PASS: delivery package -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
