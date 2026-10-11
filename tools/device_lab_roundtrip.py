#!/usr/bin/env python3
"""PlayWise Device Lab evidence round-trip.

Pushes candidate packages to a Switch over FTP (or MTP via the DBI helper) and,
after the operator has run the on-device Device Lab campaign, pulls the forensic
evidence back and chains the existing qualification gate and promotion tools --
all without pulling the SD card.

The hands-on-device steps (long-press ZL+ZR+A, long-press A for two seconds, the
~15s restriction observation, physical dock replug, official-PIN clock changes)
can never be remoted. This tool only moves bytes, verifies them byte-for-byte,
and runs the machine gates; it never fabricates a human observation.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

import install_package_via_ftp as ftp_installer  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable

STD_APP = ftp_installer.STD_APP_NAME
LAB_APP = ftp_installer.LAB_APP_NAME

DEFAULT_REPORTS = ROOT / "device-reports"
DEFAULT_PACKAGES = ROOT / "build" / "packages"
DEFAULT_VERIFICATION = ROOT / "build" / "qualification" / "verification.json"
DEFAULT_QUALIFIED = ROOT / "build" / "qualified"

MANUAL_CORE_CHECKLIST = (
    "启用实验后台：实体手柄长按 ZL+ZR+A",
    "危险阶段：实体手柄长按 A 两秒（触摸无效）",
    "关 Overlay 肉眼观察约 15 秒：限制提示是否出现 + 游戏继续/暂停/挂起/退出",
    "Timer A/B：HOME 亮屏 ≥90 秒、待机 ≥90 秒、唤醒后立即重开 Overlay",
    "护眼：对比原不限时 ↔ 临时 1440 代理，确认睡眠不扣时；观察休息起止/浮窗",
    "Bedtime：用 Nintendo 官方 PIN 把系统时钟调到窗口前/内/后/跨日",
    "物理拔插底座，确认 2 秒内进入/恢复限制",
)


class RoundtripError(RuntimeError):
    pass


def configure_console() -> None:
    # Only soften the error handler; do not force an encoding, so output stays
    # in the locale codec that a capturing parent (tools/test.py) decodes with.
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            try:
                reconfigure(errors="replace")
            except (ValueError, OSError):
                pass


@dataclass
class PullMapping:
    remote_dir: str
    local_dir: Path
    flatten: bool = False


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def default_mappings(reports_dir: Path) -> list[PullMapping]:
    """Device Lab reports land flat in reports_dir (the layout
    verify_device_qualification.py --reports expects); the rest is archived
    under reports_dir with its remote structure preserved."""
    standard = reports_dir / "standard"
    lab_archive = reports_dir / "device-lab"
    return [
        PullMapping(f"switch/{LAB_APP}/reports", reports_dir, flatten=True),
        PullMapping(f"switch/{LAB_APP}/lab", lab_archive / "lab"),
        PullMapping(f"switch/{LAB_APP}/logs", lab_archive / "logs"),
        PullMapping(f"switch/{LAB_APP}/results", lab_archive / "results"),
        PullMapping(f"switch/{STD_APP}/logs", standard / "logs"),
        PullMapping(f"switch/{STD_APP}/results", standard / "results"),
        PullMapping(f"switch/{STD_APP}/ledger", standard / "ledger"),
        PullMapping(f"switch/{STD_APP}/support", standard / "support"),
    ]

def _strip_prefix(remote_rel: str, remote_dir: str) -> str:
    rd = remote_dir.replace("\\", "/").strip("/")
    rr = remote_rel.replace("\\", "/").strip("/")
    prefix = f"{rd}/"
    if rr.startswith(prefix):
        return rr[len(prefix):]
    return Path(rr).name


def load_manifest(path: Path) -> dict:
    if path.is_file():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return {}
        if isinstance(data, dict):
            files = data.get("files", data)
            if isinstance(files, dict):
                return files
    return {}


def save_manifest(path: Path, files: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {"schema_version": 1, "files": files}
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

def pull_tree(client, mappings: list[PullMapping], manifest_path: Path, *,
              apply: bool = True, allow_overwrite_changed: bool = False) -> dict:
    """Downloads evidence idempotently. Immutable (run-id-named) reports that
    reappear with different bytes raise unless allow_overwrite_changed is set.
    Returns a summary with downloaded/skipped/planned path lists."""
    manifest = load_manifest(manifest_path)
    summary: dict[str, list[str]] = {"downloaded": [], "skipped": [], "planned": []}
    for mapping in mappings:
        for remote_rel, declared_size in client.walk_remote_dir(mapping.remote_dir):
            sub = _strip_prefix(remote_rel, mapping.remote_dir)
            name = Path(sub).name if mapping.flatten else sub
            local_path = mapping.local_dir / name
            if not apply:
                summary["planned"].append(remote_rel)
                continue
            if local_path.is_file() and remote_rel in manifest:
                if sha256_file(local_path) == manifest[remote_rel].get("sha256"):
                    summary["skipped"].append(remote_rel)
                    continue
            written = client.download_file(remote_rel, local_path)
            if declared_size is not None and written != declared_size:
                raise RoundtripError(
                    f"size mismatch for {remote_rel}: declared {declared_size} != written {written}")
            new_sha = sha256_file(local_path)
            prior = manifest.get(remote_rel)
            if prior and prior.get("sha256") not in (None, new_sha) and not allow_overwrite_changed:
                raise RoundtripError(
                    f"immutable evidence changed for {remote_rel}: "
                    f"{prior.get('sha256')} != {new_sha}; pass --allow-changed only if you "
                    f"deliberately want to overwrite archived evidence")
            manifest[remote_rel] = {"size": written, "sha256": new_sha, "mtime": time.time()}
            summary["downloaded"].append(remote_rel)
    if apply:
        save_manifest(manifest_path, manifest)
    return summary

def collect_upload_files(tasks) -> list[tuple[Path, str]]:
    files: list[tuple[Path, str]] = []
    for task in tasks:
        for rel_prefix in task.available_relative_paths:
            src_item = task.source_root / Path(rel_prefix)
            if not src_item.exists():
                continue
            if src_item.is_file():
                files.append((src_item, rel_prefix))
            elif src_item.is_dir():
                for sub in sorted(src_item.rglob("*")):
                    if sub.is_file():
                        files.append((sub, sub.relative_to(task.source_root).as_posix()))
    return files


def run_push(args) -> int:
    pkg_type = args.package
    if args.transport == "mtp":
        if args.mode != "clean":
            print("MTP 传输仅支持 --mode clean；preserve 请用 --transport ftp。", file=sys.stderr)
            return 2
        return _push_mtp(args, pkg_type)
    if args.mode == "clean":
        argv = ["--url", args.url, "--package-type", pkg_type, "--timeout", str(args.timeout)]
        if args.source is not None:
            argv += ["--source", str(args.source)]
        if args.clean_all:
            argv.append("--clean-all")
        if args.apply:
            argv.append("--apply")
        if args.force:
            argv.append("--force")
        return ftp_installer.main(argv)
    return _push_preserve(args, pkg_type)

def _push_preserve(args, pkg_type: str) -> int:
    endpoint = ftp_installer.FtpEndpoint.parse(args.url)
    tasks, source_base = ftp_installer.resolve_package_tasks(args.source, pkg_type)
    files = collect_upload_files(tasks)
    print("================================================================")
    print(" PlayWise round-trip PUSH (preserve 模式：不清理运行时数据)")
    print("================================================================")
    print(f"Target FTP:     ftp://{endpoint.host}:{endpoint.port}{endpoint.base_path}")
    print(f"Package source: {source_base}")
    print(f"Package type:   {pkg_type}")
    print(f"Files to write: {len(files)}（覆盖写，不删除运行时文件）")
    if not args.apply:
        print("\nDry-run。追加 --apply 才会经 FTP 覆盖写入。")
        return 0
    with ftp_installer.SwitchFtpClient(endpoint, timeout=args.timeout) as client:
        for index, (local_file, rel_remote) in enumerate(files, start=1):
            print(f"  [{index}/{len(files)}] {rel_remote} ...", end="", flush=True)
            client.upload_file(local_file, rel_remote)
            print(" OK")
    print("preserve 覆盖完成；credentials/PIN/rules/state/ledger 等逐字节未动。")
    return 0


def _push_mtp(args, pkg_type: str) -> int:
    script = ROOT / "tools" / "install_package_via_dbi_mtp.ps1"
    cmd = ["pwsh", "-File", str(script), "-PackageType", pkg_type]
    if args.apply:
        cmd.append("-Apply")
    print(f"委托 MTP 推送给 {script.name}: {' '.join(cmd)}")
    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError:
        print("未找到 pwsh；请在 PowerShell 手动运行 DBI MTP 脚本。", file=sys.stderr)
        return 2

def run_pull(args) -> int:
    endpoint = ftp_installer.FtpEndpoint.parse(args.url)
    reports = args.reports
    manifest_path = reports / ".manifest.json"
    mappings = default_mappings(reports)
    with ftp_installer.SwitchFtpClient(endpoint, timeout=args.timeout) as client:
        summary = pull_tree(client, mappings, manifest_path,
                            apply=args.apply, allow_overwrite_changed=args.allow_changed)
    if not args.apply:
        print(f"Dry-run：发现 {len(summary['planned'])} 个远端证据文件；追加 --apply 才下载。")
        for rel in summary["planned"]:
            print(f"  [>] {rel}")
        return 0
    print(f"已拉取 {len(summary['downloaded'])} 个，跳过 {len(summary['skipped'])} 个未变化。")
    print(f"证据目录：{reports}")
    return 0


def run_verify(args) -> int:
    cmd = [PYTHON, str(ROOT / "tools" / "verify_device_qualification.py"),
           "--packages", str(args.packages), "--reports", str(args.reports),
           "--expected-model", args.expected_model, "--expected-hos", args.expected_hos,
           "--expected-atmosphere", args.expected_atmosphere]
    if args.campaign:
        cmd += ["--campaign", args.campaign]
    return subprocess.run(cmd).returncode


def run_promote(args) -> int:
    cmd = [PYTHON, str(ROOT / "tools" / "promote_qualified_build.py"),
           "--packages", str(args.packages),
           "--verification", str(args.verification),
           "--out", str(args.out)]
    return subprocess.run(cmd).returncode

def run_all(args) -> int:
    rc = run_push(args)
    if rc != 0:
        return rc
    print("\n" + "=" * 64)
    print(" 真机人工核心（本工具无法代劳，请在设备上完成后继续）")
    for item in MANUAL_CORE_CHECKLIST:
        print(f"  - {item}")
    print("=" * 64)
    try:
        input("完成设备侧取证后按 Enter 继续拉取证据...")
    except (KeyboardInterrupt, EOFError):
        print("\n已中止；稍后可单独运行 `pull`。")
        return 1
    rc = run_pull(args)
    if rc != 0:
        return rc
    rc = run_verify(args)
    if rc != 0:
        return rc
    return run_promote(args)

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PlayWise Device Lab evidence round-trip.")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_ftp(p: argparse.ArgumentParser) -> None:
        p.add_argument("--url", "-u", default=ftp_installer.DEFAULT_FTP_URL,
                       help=f"Switch FTP URL (default {ftp_installer.DEFAULT_FTP_URL})")
        p.add_argument("--timeout", type=int, default=30, help="FTP timeout seconds (default 30)")

    def add_reports(p: argparse.ArgumentParser) -> None:
        p.add_argument("--reports", type=Path, default=DEFAULT_REPORTS,
                       help=f"Local evidence dir (default {DEFAULT_REPORTS})")

    def add_env(p: argparse.ArgumentParser) -> None:
        p.add_argument("--packages", type=Path, default=DEFAULT_PACKAGES)
        p.add_argument("--expected-model", default="oled")
        p.add_argument("--expected-hos", default="22.5.0")
        p.add_argument("--expected-atmosphere", default="1.11.2")
        p.add_argument("--campaign", default=None)

    def add_push_args(p: argparse.ArgumentParser) -> None:
        p.add_argument("--package", choices=["standard", "lab", "both"], default="lab")
        p.add_argument("--mode", choices=["clean", "preserve"], default="clean")
        p.add_argument("--transport", choices=["ftp", "mtp"], default="ftp")
        p.add_argument("--source", type=Path, default=None)
        p.add_argument("--clean-all", action="store_true")
        p.add_argument("--apply", action="store_true")
        p.add_argument("--force", action="store_true")

    p_push = sub.add_parser("push", help="推候选包到设备")
    add_ftp(p_push)
    add_push_args(p_push)
    p_push.set_defaults(func=run_push)

    p_pull = sub.add_parser("pull", help="从设备拉回取证证据")
    add_ftp(p_pull)
    add_reports(p_pull)
    p_pull.add_argument("--apply", action="store_true")
    p_pull.add_argument("--allow-changed", action="store_true",
                        help="允许覆盖已归档但字节变化的证据")
    p_pull.set_defaults(func=run_pull)

    p_verify = sub.add_parser("verify", help="对拉回证据跑资格门禁")
    add_reports(p_verify)
    add_env(p_verify)
    p_verify.set_defaults(func=run_verify)

    p_promote = sub.add_parser("promote", help="晋级已验证构建（仅按哈希复制）")
    p_promote.add_argument("--packages", type=Path, default=DEFAULT_PACKAGES)
    p_promote.add_argument("--verification", type=Path, default=DEFAULT_VERIFICATION)
    p_promote.add_argument("--out", type=Path, default=DEFAULT_QUALIFIED)
    p_promote.set_defaults(func=run_promote)

    p_all = sub.add_parser("all", help="push -> (操作者) -> pull -> verify -> promote")
    add_ftp(p_all)
    add_reports(p_all)
    add_env(p_all)
    add_push_args(p_all)
    p_all.add_argument("--allow-changed", action="store_true")
    p_all.add_argument("--verification", type=Path, default=DEFAULT_VERIFICATION)
    p_all.add_argument("--out", type=Path, default=DEFAULT_QUALIFIED)
    p_all.set_defaults(func=run_all)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    configure_console()
    try:
        return args.func(args)
    except RoundtripError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
