#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import shutil

import verify_devkitpro_build as devkit


def copy_local_package_zips(destination_dir: Path) -> None:
    packages = devkit.ROOT / "build" / "packages"
    devkit.prepare_package_download_dir(destination_dir)
    for prefix in devkit.WRITE_MODE_PACKAGE_PREFIXES:
        source = devkit.latest_timestamped_zip(packages, prefix)
        local_zip = destination_dir / source.name
        shutil.copy2(source, local_zip)
        devkit.verify_package_zip_by_prefix(local_zip, prefix)
        extract_root = devkit.extract_zip_to_named_dir(local_zip)
        devkit.verify_package(extract_root, **devkit.PACKAGE_ROOT_EXPECTATIONS[prefix])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build, verify, download, and extract grant/enforce boot2 packages."
    )
    parser.add_argument("--ssh", dest="remote", action="store_true", help="Run through SSH. This is the default.")
    parser.add_argument("--remote", dest="remote", action="store_true", help="Alias for --ssh.")
    parser.add_argument("--local", action="store_true", help="Run locally when devkitPro and make are available.")
    parser.add_argument("--no-pull", action="store_true", help="Skip git pull in remote mode.")
    parser.add_argument(
        "--package-download-dir",
        type=Path,
        default=devkit.DEFAULT_PACKAGE_DOWNLOAD_DIR,
        help=f"Package zip download/extract directory. Default: {devkit.DEFAULT_PACKAGE_DOWNLOAD_DIR}",
    )
    parser.add_argument("--ssh-alias", default=devkit.REMOTE_ALIAS)
    parser.add_argument("--remote-path", default=devkit.REMOTE_PATH)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.remote and args.local:
        print("--remote and --local cannot be used together")
        return 2

    if args.local:
        steps = [
            ("local grant/enforce package build", lambda: devkit.run_local_build(devkit.WRITE_MODE_BUILD_TARGETS)),
            (
                "local grant/enforce package artifacts",
                lambda: devkit.verify_artifacts(
                    devkit.ROOT,
                    package_names=devkit.WRITE_MODE_PACKAGE_PREFIXES,
                    zip_prefixes=devkit.WRITE_MODE_PACKAGE_PREFIXES,
                ),
            ),
            ("copy and extract local grant/enforce zips", lambda: copy_local_package_zips(args.package_download_dir)),
        ]
    else:
        steps = [
            (
                "remote grant/enforce package build",
                lambda: devkit.run_remote_build(
                    args.ssh_alias,
                    args.remote_path,
                    pull=not args.no_pull,
                    targets=devkit.WRITE_MODE_BUILD_TARGETS,
                ),
            ),
            (
                "download and extract remote grant/enforce zips",
                lambda: devkit.download_remote_package_zips(
                    args.ssh_alias,
                    args.remote_path,
                    args.package_download_dir,
                    prefixes=devkit.WRITE_MODE_PACKAGE_PREFIXES,
                ),
            ),
        ]

    passed = 0
    for index, (name, fn) in enumerate(steps):
        ok = devkit.run_step(name, fn)
        if ok:
            passed += 1
            continue
        for skipped_name, _ in steps[index + 1 :]:
            print(f"[SKIP] {skipped_name}")
        break
    total = len(steps)
    print(f"summary: {passed}/{total} grant/enforce package verification steps passed")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
