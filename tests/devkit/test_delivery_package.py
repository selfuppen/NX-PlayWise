#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import package_delivery  # noqa: E402
import package_remote  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def expect_failure(action, fragment: str) -> None:
    try:
        action()
    except (package_delivery.DeliveryPackageError, package_remote.PackageError) as exc:
        require(fragment in str(exc), f"failure must mention {fragment!r}: {exc}")
    else:
        raise AssertionError(f"operation must fail with {fragment!r}")


def test_build_and_verify() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-delivery-") as tmp_dir:
        root = Path(tmp_dir)
        standard = root / package_remote.STANDARD_PACKAGE
        html = root / "source.html"
        output = root / package_remote.COMPLETE_PACKAGE
        standard.write_bytes(b"standard zip bytes")
        html.write_bytes(b"<!doctype html><title>offline</title>")
        package_delivery.build_delivery_package(standard, html, output)
        package_remote.verify_complete_package(output, standard, html)
        first = output.read_bytes()
        package_delivery.build_delivery_package(standard, html, output)
        require(output.read_bytes() == first, "complete package generation must be deterministic")


def test_failures() -> None:
    with tempfile.TemporaryDirectory(prefix="playwise-delivery-failure-") as tmp_dir:
        root = Path(tmp_dir)
        standard = root / package_remote.STANDARD_PACKAGE
        html = root / "source.html"
        output = root / package_remote.COMPLETE_PACKAGE
        html.write_text("offline", encoding="utf-8")
        expect_failure(lambda: package_delivery.build_delivery_package(standard, html, output), "missing delivery input")
        standard.write_bytes(b"standard")
        package_delivery.build_delivery_package(standard, html, output)
        standard.write_bytes(b"changed")
        expect_failure(lambda: package_remote.verify_complete_package(output, standard, html), "embedded standard package differs")
        standard.write_bytes(b"standard")
        html.write_text("changed", encoding="utf-8")
        expect_failure(lambda: package_remote.verify_complete_package(output, standard, html), "embedded playwise-offline.html differs")
        with zipfile.ZipFile(output, "w") as package:
            package.writestr("../unsafe", b"bad")
        expect_failure(lambda: package_remote.verify_complete_package(output, standard, html), "unsafe zip entry")


def main() -> int:
    test_build_and_verify()
    test_failures()
    print("Complete delivery package tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
