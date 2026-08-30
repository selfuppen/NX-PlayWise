#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import sys

from playwise_version import read_playwise_version


ROOT = Path(__file__).resolve().parents[1]
VERSION = read_playwise_version(ROOT)
PACKAGE_NAMES = (
    f"playwise-{VERSION}.zip",
    f"playwise-complete-{VERSION}.zip",
    f"playwise-device-lab-{VERSION}.zip",
)


class PromotionError(RuntimeError):
    pass


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8", errors="replace")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def promote(packages: Path, verification_path: Path, output: Path) -> dict:
    try:
        verification = json.loads(verification_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PromotionError(f"无法读取验证记录：{exc}") from exc
    if verification.get("status") != "passed":
        raise PromotionError("验证记录未通过，禁止晋级")
    expected = verification.get("artifacts", {}).get("packages", {})
    actual: dict[str, str] = {}
    for name in PACKAGE_NAMES:
        path = packages / name
        if not path.is_file():
            raise PromotionError(f"缺少候选包：{path}")
        actual[name] = sha256_file(path)
        if actual[name] != expected.get(name):
            raise PromotionError(f"候选包已变化，哈希不匹配：{name}")
    if output.exists() and any(output.iterdir()):
        raise PromotionError(f"晋级目录不是空目录：{output}")
    output.mkdir(parents=True, exist_ok=True)
    for name in PACKAGE_NAMES:
        shutil.copy2(packages / name, output / name)
        if sha256_file(output / name) != actual[name]:
            raise PromotionError(f"复制后哈希校验失败：{name}")
    qualification = {
        "schema_version": 1,
        "status": "qualified",
        "qualified_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "artifact_binding": "detached-sha256",
        "subject": verification["subject"],
        "baseline": verification["baseline"],
        "reports": verification["reports"]["run_ids"],
        "packages": actual,
        "release_components": verification["artifacts"]["release_components"],
        "verification_sha256": sha256_file(verification_path),
    }
    qualification_path = output / "qualification.json"
    qualification_path.write_text(json.dumps(qualification, ensure_ascii=False, indent=2) + "\n",
                                  encoding="utf-8", newline="\n")
    return qualification


def main() -> int:
    configure_console()
    parser = argparse.ArgumentParser(description="按哈希原样晋级已完成真机资格验证的 PlayWise 包。")
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--verification", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = promote(args.packages.resolve(), args.verification.resolve(), args.out.resolve())
    except PromotionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: 已原样晋级 {len(result['packages'])} 个候选包。")
    print(f"资格记录：{(args.out.resolve() / 'qualification.json')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
