#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile

from playwise_version import read_playwise_version


ROOT = Path(__file__).resolve().parents[1]
VERSION = read_playwise_version(ROOT)
STANDARD_ZIP = f"playwise-{VERSION}.zip"
COMPLETE_ZIP = f"playwise-complete-{VERSION}.zip"
LAB_ZIP = f"playwise-device-lab-{VERSION}.zip"
RELEASE_COMPONENTS = (
    "atmosphere/contents/4200000000BD2300/exefs.nsp",
    "switch/playwise/pctc.nro",
    "switch/.overlays/playwise.ovl",
)
MODEL_ALIASES = {"oled": "mariko-oled", "nintendo switch oled": "mariko-oled"}


class QualificationError(RuntimeError):
    pass


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8", errors="replace")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise QualificationError(f"无法读取 JSON：{path}: {exc}") from exc
    if not isinstance(value, dict):
        raise QualificationError(f"JSON 顶层必须是对象：{path}")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise QualificationError(message)


def package_identity(packages: Path) -> tuple[dict, dict, dict[str, str], dict[str, str]]:
    standard_manifest = read_json(packages / "playwise" / "switch" / "playwise" / "build.json")
    lab_manifest = read_json(packages / "playwise-device-lab" / "switch" / "playwise-device-lab" / "build.json")
    require(standard_manifest.get("profile") == "release", "标准包 manifest profile 不是 release")
    require(lab_manifest.get("profile") == "device-lab", "Device Lab manifest profile 不正确")
    for key in ("commit", "release_id", "playwise_version"):
        require(standard_manifest.get(key) == lab_manifest.get(key), f"双包 manifest 的 {key} 不一致")
    require(standard_manifest.get("playwise_version") == VERSION, "候选包版本与源码版本不一致")
    require(standard_manifest.get("qualification", {}).get("status") == "pending", "候选包必须以 pending 状态验机")
    require(standard_manifest.get("build", {}).get("source_dirty") is False, "资格候选包含未提交的 tracked 修改")
    require(standard_manifest.get("build", {}).get("libnx") not in (None, "", "unknown"), "候选包缺少 libnx 身份")
    require(standard_manifest.get("build", {}).get("container_image") not in (None, "", "unknown"),
            "候选包缺少 Docker 镜像标签")

    zip_hashes: dict[str, str] = {}
    for name in (STANDARD_ZIP, COMPLETE_ZIP, LAB_ZIP):
        path = packages / name
        require(path.is_file(), f"缺少候选产物：{path}")
        zip_hashes[name] = sha256_file(path)
    component_hashes: dict[str, str] = {}
    with zipfile.ZipFile(packages / STANDARD_ZIP) as archive:
        for member in RELEASE_COMPONENTS:
            try:
                component_hashes[member] = sha256_bytes(archive.read(member))
            except KeyError as exc:
                raise QualificationError(f"标准包缺少发布二进制：{member}") from exc
    return standard_manifest, lab_manifest, zip_hashes, component_hashes


def verify_public_parity(report: dict, label: str) -> None:
    parity = report.get("public_parity")
    require(isinstance(parity, dict), f"{label}: 缺少 public_parity")
    commands = parity.get("commands", {})
    for command in ("1006", "1031", "1035", "1457", "1458"):
        item = commands.get(command, {})
        require(item.get("comparable") is True, f"{label}: {command} raw/libnx 不可比")
        if command == "1035":
            require(item.get("structure_equal") is True, f"{label}: 1035 结构不一致")
        elif command != "1457":
            require(item.get("value_equal") is True, f"{label}: {command} 值不一致")
    settings = parity.get("settings_0x44", {})
    require(settings.get("same_value_write_succeeded") is True, f"{label}: 0x44 同值写失败")
    require(settings.get("exactly_restored") is True, f"{label}: 0x44 同值写未精确恢复")


def verify_report_common(report: dict, label: str, manifest: dict,
                         expected_model: str, expected_hos: str, expected_atmosphere: str) -> None:
    require(report.get("schema_version") == 2 and report.get("version") == 2, f"{label}: 只接受 Device Lab schema v2")
    require(report.get("report_status") == "final", f"{label}: 报告不是 final")
    require(report.get("summary", {}).get("complete") is True, f"{label}: summary.complete 不是 true")
    require(report.get("restoration", {}).get("proved") is True and
            report.get("restoration", {}).get("verdict") == "exact_restore_proved",
            f"{label}: 缺少精确恢复证明")
    environment = report.get("environment")
    require(isinstance(environment, dict), f"{label}: 缺少报告环境身份")
    build = environment.get("build")
    require(isinstance(build, dict), f"{label}: 缺少候选构建身份")
    require(build.get("commit") == manifest.get("commit") and
            build.get("release_id") == manifest.get("release_id"), f"{label}: 报告与候选 commit/release_id 不一致")
    runtime = environment.get("runtime")
    require(isinstance(runtime, dict), f"{label}: 缺少运行时环境证据")
    expected_model_value = MODEL_ALIASES.get(expected_model.lower(), expected_model.lower())
    require(str(runtime.get("model", "")).lower() == expected_model_value, f"{label}: 主机型号不匹配")
    require(runtime.get("hos") == expected_hos, f"{label}: HOS 版本不匹配")
    require(runtime.get("atmosphere") is True, f"{label}: Atmosphère 未得到运行时证明")
    detection = runtime.get("atmosphere_detection", {})
    require(detection.get("source") == "spl:ExosphereApiVersion" and
            detection.get("version") == expected_atmosphere, f"{label}: Exosphere 版本证据不匹配")
    verify_public_parity(report, label)
    expected_write_phases = {
        "timer_activation_ab": {
            "ab_limited_settings_only", "ab_restriction_settings_only",
            "ab_grant_settings_only", "ab_restriction_before_unlimited",
            "ab_unlimited_settings_only",
        },
        "restriction_quick": {"restriction_effect"},
    }.get(report.get("mode"), set())
    proved_write_phases: set[str] = set()
    for index, phase in enumerate(report.get("phases", [])):
        if not isinstance(phase, dict):
            continue
        scope = phase.get("settings_write_scope", {})
        phase_name = phase.get("phase")
        if phase_name in expected_write_phases:
            proved_write_phases.add(phase_name)
            require(scope.get("comparison") == "phase_prewrite_to_after",
                    f"{label}: phase {index} 未保存写入前像")
            prewrite_hex = scope.get("prewrite_settings_hex")
            require(isinstance(prewrite_hex, str) and len(prewrite_hex) == 136 and
                    prewrite_hex == phase.get("before", {}).get("settings_hex"),
                    f"{label}: phase {index} 的完整 0x44 前像不一致")
            require(scope.get("unexpected_bytes_unchanged") is True,
                    f"{label}: phase {index} 存在意外 0x44 offset")
    require(proved_write_phases == expected_write_phases,
            f"{label}: 危险写入阶段的前像或 offset 证据不完整")


def verify_reports(reports_dir: Path, manifest: dict, expected_model: str,
                   expected_hos: str, expected_atmosphere: str) -> tuple[list[str], list[str]]:
    paths = sorted(path for path in reports_dir.glob("*.json") if path.is_file())
    require(paths, f"报告目录为空：{reports_dir}")
    run_ids: set[str] = set()
    ab_runs: list[str] = []
    paused_runs: list[str] = []
    continued_runs: list[str] = []
    for path in paths:
        report = read_json(path)
        label = path.name
        verify_report_common(report, label, manifest, expected_model, expected_hos, expected_atmosphere)
        run_id = report.get("run_id")
        require(isinstance(run_id, str) and run_id and run_id not in run_ids, f"{label}: run_id 缺失或重复")
        run_ids.add(run_id)
        mode = report.get("mode")
        if mode == "timer_activation_ab":
            baseline = report.get("baseline", {})
            require(baseline.get("activation_preconditions_met") is True and
                    int(baseline.get("remaining_ns", 0)) >= int(baseline.get("minimum_remaining_ns", 1)),
                    f"{label}: Timer A/B 前置条件不足")
            evidence = report.get("timer_activation_ab", {})
            require(evidence.get("home_awake_counted") is True, f"{label}: 未证明 HOME 亮屏计时")
            require(evidence.get("sleep_excluded") is True, f"{label}: 未证明待机不计时")
            cases = evidence.get("fallback_cases")
            require(isinstance(cases, list) and {item.get("target") for item in cases if isinstance(item, dict)} ==
                    {"limited", "grant", "unlimited"}, f"{label}: fallback 目标集合不完整")
            for item in cases:
                ready = item.get("settings_only_runtime_ready")
                called = item.get("fallback_called")
                succeeded = item.get("fallback_succeeded")
                require((ready is True and called is False) or
                        (ready is False and called is True and succeeded is True),
                        f"{label}: {item.get('target')} fallback 与 settings-only 结果未绑定")
            ab_runs.append(run_id)
        elif mode == "restriction_quick":
            require(report.get("manual_observation") == "restriction_visible", f"{label}: 未记录限制提示可见")
            effect = report.get("manual_runtime_effect")
            if effect == "paused_or_suspended":
                paused_runs.append(run_id)
            elif effect == "continued":
                continued_runs.append(run_id)
            else:
                raise QualificationError(f"{label}: 游戏实际行为不是资格矩阵要求的结果")
        else:
            raise QualificationError(f"{label}: 资格报告集只接受 timer_activation_ab 和 restriction_quick")
    require(len(ab_runs) >= 1, "缺少 Timer 激活 A/B 报告")
    require(len(paused_runs) >= 2, "至少需要两份“提示可见 + 暂停/挂起”报告")
    require(len(continued_runs) >= 1, "至少需要一份“提示可见 + 软件继续”报告")
    return sorted(run_ids), paths_as_strings(paths)


def paths_as_strings(paths: list[Path]) -> list[str]:
    return [str(path.resolve()) for path in paths]


def verify(packages: Path, reports: Path, expected_model: str,
           expected_hos: str, expected_atmosphere: str) -> dict:
    manifest, _lab_manifest, zip_hashes, component_hashes = package_identity(packages)
    run_ids, report_paths = verify_reports(reports, manifest, expected_model, expected_hos, expected_atmosphere)
    return {
        "schema_version": 1,
        "status": "passed",
        "subject": {"commit": manifest["commit"], "release_id": manifest["release_id"]},
        "baseline": {"model": expected_model, "hos": expected_hos, "atmosphere": expected_atmosphere},
        "reports": {"run_ids": run_ids, "paths": report_paths},
        "artifacts": {"packages": zip_hashes, "release_components": component_hashes},
        "checks": [
            "device_lab_schema_v2", "environment_exosphere", "public_raw_libnx_parity",
            "target_bound_activation_fallback", "restriction_matrix", "exact_restore",
            "artifact_identity",
        ],
    }


def main() -> int:
    configure_console()
    parser = argparse.ArgumentParser(description="校验 PlayWise 当前基线 Device Lab 资格报告。")
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--reports", type=Path, required=True)
    parser.add_argument("--expected-model", required=True)
    parser.add_argument("--expected-hos", required=True)
    parser.add_argument("--expected-atmosphere", required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "qualification" / "verification.json")
    args = parser.parse_args()
    try:
        result = verify(args.packages.resolve(), args.reports.resolve(), args.expected_model,
                        args.expected_hos, args.expected_atmosphere)
    except QualificationError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    labels = {
        "device_lab_schema_v2": "报告均为 Device Lab schema v2",
        "environment_exosphere": "机型、HOS 与 Exosphere Atmosphère 证据匹配",
        "public_raw_libnx_parity": "公开命令 raw/libnx 对照一致",
        "target_bound_activation_fallback": "Timer fallback 与目标及前置读数绑定",
        "restriction_matrix": "两游戏与官方暂停开关观察矩阵完整",
        "exact_restore": "全部危险实验均逐字节精确恢复",
        "artifact_identity": "报告、候选 Zip 与 Switch 二进制身份绑定",
    }
    for check_name in result["checks"]:
        print(f"PASS: {labels[check_name]}")
    print(f"PASS: 当前基线资格报告通过机器校验，共 {len(result['reports']['run_ids'])} 个 run。")
    print(f"验证记录：{args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
