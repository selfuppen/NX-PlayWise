#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import stage_timer  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_write_and_read_records() -> None:
    with tempfile.TemporaryDirectory(prefix="stage-timer-test-") as tmp_dir:
        timing_file = Path(tmp_dir) / "build_timing.json"
        require(stage_timer.read_timing_records(timing_file) == [], "empty path must return empty list")

        stage_timer.write_timing_record("playwise", "sysmodule", 2.5, path=timing_file)
        stage_timer.write_timing_record("playwise", "nro", 1.5, path=timing_file)
        stage_timer.write_timing_record("global", "clean", 0.5, path=timing_file)

        records = stage_timer.read_timing_records(timing_file)
        require(len(records) == 3, f"expected 3 records, got {len(records)}")
        require(records[0]["package"] == "playwise" and records[0]["stage"] == "sysmodule", "first record mismatch")
        require(records[1]["package"] == "playwise" and records[1]["duration"] == 1.5, "second record mismatch")
        require(records[2]["package"] == "global" and records[2]["stage"] == "clean", "third record mismatch")

        stage_timer.clear_timing_records(timing_file)
        require(not timing_file.is_file(), "clear must remove the file")


def test_format_timing_report() -> None:
    records = [
        {"package": "global", "stage": "clean", "duration": 1.0},
        {"package": "playwise", "stage": "manifest", "duration": 0.5},
        {"package": "playwise", "stage": "sysmodule", "duration": 4.5},
        {"package": "device-lab", "stage": "nro", "duration": 4.0},
    ]
    report = stage_timer.format_timing_report(records, metadata={"目标": "all", "模式": "clean"})
    require("PlayWise 打包耗时统计报告" in report, "report must include header")
    require("[目标: all]" in report, "report must include metadata")
    require("标准分发包 (playwise)" in report, "report must include package title")
    require("设备实验室包 (playwise-device-lab)" in report, "report must include device-lab title")
    require("10.00s" in report, "total must be 10.00s")
    require("45.0%" in report or "45%" in report, "sysmodule must be 45% of total or 90% of package")

    empty_report = stage_timer.format_timing_report([])
    require("没有耗时统计记录" in empty_report, "empty records must be handled safely")

    zero_records = [
        {"package": "playwise", "stage": "sysmodule", "duration": 0.0},
    ]
    zero_report = stage_timer.format_timing_report(zero_records)
    require("<0.01s" in zero_report, "zero duration must format safely without ZeroDivisionError")


def test_cli_execution() -> None:
    with tempfile.TemporaryDirectory(prefix="stage-timer-cli-") as tmp_dir:
        timing_file = Path(tmp_dir) / "build_timing.json"
        cmd = [
            sys.executable,
            str(TOOLS / "stage_timer.py"),
            "playwise",
            "test-stage",
            "--",
            sys.executable,
            "-c",
            "import sys; sys.exit(0)",
        ]
        env = dict(os.environ)
        env["PYTHONPATH"] = str(TOOLS)
        res = subprocess.run(cmd, env=env, cwd=tmp_dir)
        require(res.returncode == 0, "CLI execution must succeed")

        fail_cmd = [
            sys.executable,
            str(TOOLS / "stage_timer.py"),
            "playwise",
            "fail-stage",
            "--",
            sys.executable,
            "-c",
            "import sys; sys.exit(42)",
        ]
        fail_res = subprocess.run(fail_cmd, cwd=tmp_dir)
        require(fail_res.returncode == 42, f"CLI must pass-through return code, got {fail_res.returncode}")


def main() -> int:
    test_write_and_read_records()
    test_format_timing_report()
    test_cli_execution()
    print("Stage timer tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
