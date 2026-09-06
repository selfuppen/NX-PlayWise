#!/usr/bin/env python3
"""
PlayWise FTP Package Installer
Installs PlayWise packages directly to Nintendo Switch over FTP with full clean install support.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import ftplib
import json
import os
from pathlib import Path
import sys
from urllib.parse import urlparse


DEFAULT_FTP_URL = "ftp://192.168.8.178:5000/"

STD_APP_NAME = "playwise"
STD_SYSMODULE_ID = "4200000000BD2300"
STD_OVERLAY_NAME = "playwise.ovl"

LAB_APP_NAME = "playwise-device-lab"
LAB_SYSMODULE_ID = "4200000000BD23F0"
LAB_OVERLAY_NAME = "playwise-device-lab.ovl"

LEGACY_OVERLAY_NAME = "pctc.ovl"


@dataclass
class PackageConfig:
    app_name: str
    sysmodule_id: str
    overlay_name: str
    display_name: str
    is_lab: bool = False


@dataclass
class InstallTask:
    config: PackageConfig
    source_root: Path
    source_app: Path
    available_relative_paths: list[str]


@dataclass
class FtpEndpoint:
    host: str
    port: int
    user: str
    password: str
    base_path: str

    @classmethod
    def parse(cls, url: str) -> FtpEndpoint:
        raw_url = url.strip()
        if not raw_url:
            raw_url = DEFAULT_FTP_URL
        if "://" not in raw_url:
            raw_url = f"ftp://{raw_url}"
        parsed = urlparse(raw_url)
        if parsed.scheme.lower() != "ftp":
            raise ValueError(f"Unsupported scheme '{parsed.scheme}'. Only ftp:// is supported.")
        host = parsed.hostname
        if not host:
            raise ValueError(f"Invalid FTP URL: host is required in '{url}'")
        port = parsed.port or 5000
        user = parsed.username or "anonymous"
        password = parsed.password or ""
        base_path = parsed.path.rstrip("/")
        if not base_path:
            base_path = "/"
        return cls(host=host, port=port, user=user, password=password, base_path=base_path)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def assert_standard_package(package_root: Path) -> str:
    app_dir = package_root / "switch" / STD_APP_NAME
    if not app_dir.is_dir():
        raise ValueError(f"Invalid package: missing switch/{STD_APP_NAME} directory")

    required_defaults = [
        "config.json",
        "auth.json",
        "rules.json",
        "state.json",
        "compatibility.json",
        "setup.json",
    ]
    for default_file in required_defaults:
        default_path = app_dir / "defaults" / default_file
        if not default_path.is_file():
            raise ValueError(f"Invalid package: switch/{STD_APP_NAME}/defaults/{default_file} is missing.")
        if (app_dir / default_file).exists():
            raise ValueError(f"Invalid package: switch/{STD_APP_NAME}/{default_file} would overwrite runtime data.")

    for forbidden in ("credentials.json", "capabilities.json"):
        if (app_dir / forbidden).exists():
            raise ValueError(f"Invalid package: switch/{STD_APP_NAME}/{forbidden} must not be installed.")

    build_json = app_dir / "build.json"
    if not build_json.is_file():
        raise ValueError(f"Invalid package: switch/{STD_APP_NAME}/build.json is missing.")
    build_data = read_json(build_json)
    if build_data.get("profile") != "release" or not build_data.get("release_id"):
        raise ValueError("Invalid package: build.json must identify a release build.")

    sysmodule_dir = package_root / "atmosphere" / "contents" / STD_SYSMODULE_ID
    if not (sysmodule_dir / "exefs.nsp").is_file():
        raise ValueError(f"Invalid package: atmosphere/contents/{STD_SYSMODULE_ID}/exefs.nsp is missing.")

    overlay_path = package_root / "switch" / ".overlays" / STD_OVERLAY_NAME
    if not overlay_path.is_file():
        raise ValueError(f"Invalid package: switch/.overlays/{STD_OVERLAY_NAME} is missing.")

    boot_flag = sysmodule_dir / "flags" / "boot2.flag"
    if boot_flag.is_file() and boot_flag.stat().st_size != 0:
        raise ValueError("Invalid package: standard boot2.flag must be empty.")

    return str(build_data["release_id"])


def assert_lab_package(package_root: Path) -> str:
    app_dir = package_root / "switch" / LAB_APP_NAME
    if not app_dir.is_dir():
        raise ValueError(f"Invalid package: missing switch/{LAB_APP_NAME} directory")

    build_json = app_dir / "build.json"
    if not build_json.is_file():
        raise ValueError(f"Invalid package: switch/{LAB_APP_NAME}/build.json is missing.")
    build_data = read_json(build_json)
    if build_data.get("profile") != "device-lab":
        raise ValueError("Invalid package: build.json profile must be device-lab.")

    sysmodule_dir = package_root / "atmosphere" / "contents" / LAB_SYSMODULE_ID
    if not (sysmodule_dir / "exefs.nsp").is_file():
        raise ValueError(f"Invalid package: atmosphere/contents/{LAB_SYSMODULE_ID}/exefs.nsp is missing.")

    overlay_path = package_root / "switch" / ".overlays" / LAB_OVERLAY_NAME
    if not overlay_path.is_file():
        raise ValueError(f"Invalid package: switch/.overlays/{LAB_OVERLAY_NAME} is missing.")

    return str(build_data.get("release_id", "device-lab"))


def resolve_package_tasks(source_folder: Path | None, package_type: str) -> tuple[list[InstallTask], Path]:
    repo_root = Path(__file__).resolve().parents[1]
    default_packages_dir = repo_root / "build" / "packages"

    base_path: Path
    if source_folder is not None:
        base_path = source_folder.resolve()
        if not base_path.exists():
            raise FileNotFoundError(f"Specified source folder does not exist: {source_folder}")
    else:
        base_path = default_packages_dir.resolve()
        if not base_path.exists():
            raise FileNotFoundError(
                f"Default packages directory not found: {base_path}. Please build packages first or specify --source."
            )

    selected_configs: list[PackageConfig] = []
    if package_type in ("standard", "both"):
        selected_configs.append(
            PackageConfig(
                app_name=STD_APP_NAME,
                sysmodule_id=STD_SYSMODULE_ID,
                overlay_name=STD_OVERLAY_NAME,
                display_name="PlayWise Standard (Release)",
                is_lab=False,
            )
        )
    if package_type in ("lab", "both"):
        selected_configs.append(
            PackageConfig(
                app_name=LAB_APP_NAME,
                sysmodule_id=LAB_SYSMODULE_ID,
                overlay_name=LAB_OVERLAY_NAME,
                display_name="PlayWise Device Lab",
                is_lab=True,
            )
        )

    tasks: list[InstallTask] = []
    for cfg in selected_configs:
        pkg_root = base_path
        expected_app_dir = pkg_root / "switch" / cfg.app_name

        if not expected_app_dir.is_dir():
            nested = base_path / cfg.app_name
            if (nested / "switch" / cfg.app_name).is_dir():
                pkg_root = nested
                expected_app_dir = nested / "switch" / cfg.app_name

        if not expected_app_dir.is_dir():
            raise ValueError(
                f"Unable to find package for '{cfg.app_name}' under '{base_path}'. Expected '{expected_app_dir}'."
            )

        if cfg.is_lab:
            assert_lab_package(pkg_root)
        else:
            assert_standard_package(pkg_root)

        available_paths: list[str] = [
            f"switch/{cfg.app_name}",
            f"atmosphere/contents/{cfg.sysmodule_id}",
            f"switch/.overlays/{cfg.overlay_name}",
        ]

        tasks.append(
            InstallTask(
                config=cfg,
                source_root=pkg_root,
                source_app=expected_app_dir,
                available_relative_paths=available_paths,
            )
        )

    return tasks, base_path


def compute_paths_to_clean(package_type: str, clean_all: bool) -> list[str]:
    all_app_paths = [f"switch/{STD_APP_NAME}", f"switch/{LAB_APP_NAME}"]
    all_sysmodule_paths = [
        f"atmosphere/contents/{STD_SYSMODULE_ID}",
        f"atmosphere/contents/{LAB_SYSMODULE_ID}",
    ]
    all_overlay_paths = [
        f"switch/.overlays/{STD_OVERLAY_NAME}",
        f"switch/.overlays/{LAB_OVERLAY_NAME}",
        f"switch/.overlays/{LEGACY_OVERLAY_NAME}",
    ]

    if clean_all:
        paths = all_app_paths + all_sysmodule_paths + all_overlay_paths
    elif package_type == "both":
        paths = all_app_paths + all_sysmodule_paths + all_overlay_paths
    elif package_type == "lab":
        paths = [
            f"switch/{LAB_APP_NAME}",
            f"atmosphere/contents/{LAB_SYSMODULE_ID}",
            f"switch/.overlays/{LAB_OVERLAY_NAME}",
            f"switch/.overlays/{LEGACY_OVERLAY_NAME}",
        ]
    else:  # standard
        paths = [
            f"switch/{STD_APP_NAME}",
            f"atmosphere/contents/{STD_SYSMODULE_ID}",
            f"switch/.overlays/{STD_OVERLAY_NAME}",
            f"switch/.overlays/{LEGACY_OVERLAY_NAME}",
        ]

    # Return ordered unique list
    seen = set()
    unique = []
    for p in paths:
        if p not in seen:
            seen.add(p)
            unique.append(p)
    return unique


# ----------------------------------------------------------------------
# Robust FTP Operations tailored for Switch sys-ftpd / ftpd
# ----------------------------------------------------------------------

class SwitchFtpClient:
    def __init__(self, endpoint: FtpEndpoint, timeout: int = 30):
        self.endpoint = endpoint
        self.timeout = timeout
        self.ftp = ftplib.FTP()
        self._ensured_dirs: set[str] = set()

    def connect(self) -> None:
        self.ftp.connect(self.endpoint.host, self.endpoint.port, timeout=self.timeout)
        self.ftp.login(self.endpoint.user, self.endpoint.password)
        # Enable passive mode
        self.ftp.set_pasv(True)
        if self.endpoint.base_path and self.endpoint.base_path != "/":
            self.ftp.cwd(self.endpoint.base_path)

    def close(self) -> None:
        try:
            self.ftp.quit()
        except Exception:
            try:
                self.ftp.close()
            except Exception:
                pass

    def __enter__(self) -> SwitchFtpClient:
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def _to_remote_abs(self, relative_path: str) -> str:
        rel = relative_path.replace("\\", "/").strip("/")
        if not rel:
            return self.endpoint.base_path or "/"
        if self.endpoint.base_path == "/":
            return f"/{rel}"
        return f"{self.endpoint.base_path}/{rel}"

    def remove_path(self, relative_path: str) -> None:
        """Removes a remote path (file or directory) if it exists.
        
        Attempts directory removal first via CWD; if not a directory, attempts file deletion.
        Avoids expensive and fragile pre-probing commands on embedded FTP servers.
        """
        abs_path = self._to_remote_abs(relative_path)
        base = self.endpoint.base_path or "/"

        # First, test if it is a directory by attempting to CWD into it
        is_dir = False
        try:
            self.ftp.cwd(abs_path)
            is_dir = True
        except Exception:
            # Not a directory or doesn't exist
            pass

        if is_dir:
            try:
                self._clean_and_remove_current_dir(abs_path)
            finally:
                try:
                    self.ftp.cwd(base)
                except Exception:
                    pass
            return

        # If not a directory, try deleting as a file in its parent directory
        rel_norm = relative_path.replace("\\", "/").strip("/")
        parent_rel = os.path.dirname(rel_norm)
        filename = os.path.basename(rel_norm)
        parent_abs = self._to_remote_abs(parent_rel) if parent_rel else base

        try:
            self.ftp.cwd(parent_abs)
            self.ftp.delete(filename)
        except Exception:
            # File does not exist or cannot be deleted; safe to ignore in clean install
            pass
        finally:
            try:
                self.ftp.cwd(base)
            except Exception:
                pass

    def _clean_and_remove_current_dir(self, abs_dir_path: str) -> None:
        """Assumes cwd is already inside abs_dir_path."""
        items: list[tuple[str, bool]] = []
        try:
            for name, facts in self.ftp.mlsd():
                if name in (".", ".."):
                    continue
                is_sub = facts.get("type") == "dir"
                items.append((name, is_sub))
        except Exception:
            # Fallback if mlsd not supported
            lines: list[str] = []
            try:
                self.ftp.retrlines("LIST", lines.append)
                for line in lines:
                    parts = line.split(maxsplit=8)
                    if len(parts) >= 9:
                        name = parts[-1]
                        if name in (".", ".."):
                            continue
                        is_sub = parts[0].startswith("d")
                        items.append((name, is_sub))
            except Exception:
                pass

        for name, is_sub in items:
            if is_sub:
                try:
                    self.ftp.cwd(name)
                    self._clean_and_remove_current_dir(f"{abs_dir_path.rstrip('/')}/{name}")
                except Exception as e:
                    print(f" (warning: could not enter {name}: {e})", end="")
            else:
                try:
                    self.ftp.delete(name)
                except Exception as e:
                    print(f" (warning: could not delete {name}: {e})", end="")

        # Step up and remove this directory
        try:
            self.ftp.cwd("..")
            dir_name = os.path.basename(abs_dir_path.rstrip("/"))
            self.ftp.rmd(dir_name)
        except Exception as e:
            print(f" (warning: could not rmd {abs_dir_path}: {e})", end="")

    def ensure_dir(self, relative_dir_path: str) -> None:
        """Ensures that a directory exists, creating missing segments progressively."""
        rel_norm = relative_dir_path.replace("\\", "/").strip("/")
        if not rel_norm or rel_norm in self._ensured_dirs:
            return

        segments = [s for s in rel_norm.split("/") if s]
        accum = []
        base = self.endpoint.base_path or "/"
        self.ftp.cwd(base)

        for seg in segments:
            accum.append(seg)
            cur_path = "/".join(accum)
            if cur_path in self._ensured_dirs:
                try:
                    self.ftp.cwd(seg)
                    continue
                except Exception:
                    pass

            try:
                self.ftp.cwd(seg)
            except Exception:
                try:
                    self.ftp.mkd(seg)
                except Exception:
                    pass
                self.ftp.cwd(seg)
            self._ensured_dirs.add(cur_path)

        self.ftp.cwd(base)

    def upload_file(self, local_path: Path, relative_remote_path: str) -> None:
        """Uploads a local file to the destination remote path, with size verification."""
        rel_norm = relative_remote_path.replace("\\", "/").strip("/")
        remote_dir = os.path.dirname(rel_norm)
        filename = os.path.basename(rel_norm)
        base = self.endpoint.base_path or "/"

        if remote_dir:
            self.ensure_dir(remote_dir)
            remote_parent_abs = self._to_remote_abs(remote_dir)
        else:
            remote_parent_abs = base

        self.ftp.cwd(remote_parent_abs)

        local_size = local_path.stat().st_size
        with open(local_path, "rb") as f:
            self.ftp.storbinary(f"STOR {filename}", f)

        # Verification: size check
        remote_size = None
        try:
            remote_size = self.ftp.size(filename)
        except Exception:
            pass

        if remote_size is not None and remote_size != local_size:
            raise IOError(
                f"File verification failed for {relative_remote_path}: local size {local_size} != remote size {remote_size}"
            )

        self.ftp.cwd(base)


# ----------------------------------------------------------------------
# CLI & Execution
# ----------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Full clean installer for PlayWise Nintendo Switch packages via FTP."
    )
    parser.add_argument(
        "--url",
        "-u",
        default=DEFAULT_FTP_URL,
        help=f"Target Switch FTP URL (default: {DEFAULT_FTP_URL})",
    )
    parser.add_argument(
        "--source",
        "-s",
        type=Path,
        default=None,
        help="Path to extracted package root or packages folder (default: auto-detect from build/packages)",
    )
    parser.add_argument(
        "--package-type",
        "-t",
        choices=["standard", "lab", "both"],
        default="standard",
        help="Type of package to install: standard (default), lab, or both",
    )
    parser.add_argument(
        "--lab",
        action="store_true",
        help="Convenience switch to install PlayWise Device Lab package (equivalent to --package-type lab)",
    )
    parser.add_argument(
        "--both",
        action="store_true",
        help="Convenience switch to install both Standard and Device Lab packages (equivalent to --package-type both)",
    )
    parser.add_argument(
        "--clean-all",
        action="store_true",
        help="Clean all PlayWise packages (both Standard and Device Lab) on the device prior to install",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Actually apply changes to the Switch over FTP (default is dry-run preview)",
    )
    parser.add_argument(
        "--force",
        "-f",
        action="store_true",
        help="Bypass interactive user confirmation prompt",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=30,
        help="FTP socket timeout in seconds (default: 30)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    # Determine package type
    pkg_type = args.package_type
    if args.both:
        pkg_type = "both"
    elif args.lab:
        pkg_type = "lab"

    endpoint = FtpEndpoint.parse(args.url)
    tasks, source_base = resolve_package_tasks(args.source, pkg_type)
    paths_to_clean = compute_paths_to_clean(pkg_type, args.clean_all)

    # Collect all files to copy across all tasks
    files_to_upload: list[tuple[Path, str]] = []
    for task in tasks:
        for rel_prefix in task.available_relative_paths:
            src_item = task.source_root / Path(rel_prefix)
            if not src_item.exists():
                continue
            if src_item.is_file():
                files_to_upload.append((src_item, rel_prefix))
            elif src_item.is_dir():
                for sub in sorted(src_item.rglob("*")):
                    if sub.is_file():
                        rel = sub.relative_to(task.source_root).as_posix()
                        files_to_upload.append((sub, rel))

    print("================================================================")
    print(" PlayWise Switch FTP Full Clean Installer")
    print("================================================================")
    print(f"Target FTP:     ftp://{endpoint.host}:{endpoint.port}{endpoint.base_path}")
    print(f"Package source: {source_base}")
    print(f"Package type:   {pkg_type} ({', '.join(t.config.display_name for t in tasks)})")
    print(f"Install mode:   Full Clean Install ({'Clean All' if args.clean_all else 'Clean Selected'})")
    print(f"Files to write: {len(files_to_upload)} files")
    print("")
    print("Target paths to completely remove prior to copying:")
    for path in paths_to_clean:
        print(f"  [-] /{path}")
    print("")
    print("Package components to copy:")
    for task in tasks:
        print(f"  [+] {task.config.display_name}:")
        for rel in task.available_relative_paths:
            print(f"      {rel}")
    print("================================================================")

    if not args.apply:
        print("")
        print("Dry-run preview only. No files were changed on the Switch.")
        print("To execute full clean installation, re-run with --apply.")
        return 0

    # Confirmation prompt if not forced
    if not args.force:
        target_token = endpoint.host
        prompt_msg = f"Type '{target_token}' to confirm FULL CLEAN installation to {endpoint.host}:{endpoint.port}: "
        try:
            confirm = input(prompt_msg).strip()
        except (KeyboardInterrupt, EOFError):
            print("\nInstallation aborted.")
            return 1
        if confirm != target_token:
            print(f"Confirmation '{confirm}' did not match '{target_token}'. Aborted.")
            return 1

    print(f"\nConnecting to ftp://{endpoint.host}:{endpoint.port} ...")
    with SwitchFtpClient(endpoint, timeout=args.timeout) as client:
        print("Connected and logged in successfully.")

        # Step 1: Clean old paths
        print("\n--- Phase 1: Removing old installation files ---")
        for clean_path in paths_to_clean:
            print(f"  Cleaning /{clean_path} ...", end="", flush=True)
            client.remove_path(clean_path)
            print(" Done.")

        # Step 2: Upload new files
        print("\n--- Phase 2: Uploading package files ---")
        uploaded_count = 0
        for local_file, rel_remote in files_to_upload:
            file_size = local_file.stat().st_size
            print(f"  [{uploaded_count + 1}/{len(files_to_upload)}] Uploading {rel_remote} ({file_size} bytes) ...", end="", flush=True)
            client.upload_file(local_file, rel_remote)
            print(" OK")
            uploaded_count += 1

        print("\n================================================================")
        print(f"Installation completed successfully! ({uploaded_count} files verified)")
        print("Please reboot your Switch or restart the sysmodule to load the new build.")
        print("================================================================")

    return 0


if __name__ == "__main__":
    sys.exit(main())
