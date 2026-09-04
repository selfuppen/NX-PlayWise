#!/usr/bin/env python3
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unicodedata

if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
if hasattr(sys.stderr, "reconfigure"):
    try:
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = Path(__file__).resolve().parents[1]
TIMING_FILE = ROOT / "build" / "build_timing.json"
LOCK_FILE = ROOT / "build" / "build_timing.lock"

def display_width(text: str) -> int:
    width = 0
    for ch in text:
        status = unicodedata.east_asian_width(ch)
        if status in ("F", "W"):
            width += 2
        else:
            width += 1
    return width


def pad_display(text: str, width: int, align: str = "left") -> str:
    dw = display_width(text)
    if dw >= width:
        return text
    pad = " " * (width - dw)
    if align == "right":
        return pad + text
    elif align == "center":
        left_pad = " " * ((width - dw) // 2)
        right_pad = " " * (width - dw - len(left_pad))
        return left_pad + text + right_pad
    return text + pad


PACKAGE_TITLES: dict[str, str] = {
    "global": "全局前置任务",
    "playwise": "标准分发包 (playwise)",
    "playwise-complete": "完整交付包 (playwise-complete)",
    "device-lab": "设备实验室包 (playwise-device-lab)",
    "eden-test": "Eden 模拟器测试包 (eden-test)",
}

STAGE_DESCRIPTIONS: dict[str, dict[str, str]] = {
    "global": {
        "clean": "清理旧构建产物",
        "test-host": "Host C 核心与 UI 状态测试",
        "test-python": "Python 协议回归测试",
    },
    "playwise": {
        "manifest": "生成发布元数据清单",
        "sysmodule": "编译生产版后台 Sysmodule",
        "nro": "编译设置主程序 NRO",
        "overlay": "编译快捷悬浮窗 Overlay",
        "package-zip": "SD 目录组织与 ZIP 压缩",
        "verify": "宿主机安全扫描与完整性校验",
    },
    "playwise-complete": {
        "offline-html": "离线单文件独立前端构建/校验",
        "package-zip": "组装全套交付 ZIP",
        "verify": "宿主机交付包双重封装校验",
    },
    "device-lab": {
        "manifest": "生成实验专用元数据清单",
        "sysmodule": "编译实验后台 Sysmodule",
        "nro": "编译实验引导程序 NRO",
        "overlay": "编译实验快捷悬浮窗 Overlay",
        "package-zip": "实验 SD 目录组织与 ZIP 压缩",
        "verify": "宿主机实验安全隔离性校验",
    },
    "eden-test": {
        "manifest": "生成 Eden 专用元数据清单",
        "nro": "编译 Eden 模拟器专用前端 NRO",
        "verify": "宿主机 Eden NACP 与 Marker 隔离校验",
    },
}


def read_timing_records(path: Path = TIMING_FILE) -> list[dict]:
    if not path.is_file():
        return []
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return []


def write_timing_record(
    package: str,
    stage: str,
    duration: float,
    path: Path = TIMING_FILE,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # Simple file lock for concurrent make -j safety
    lock_path = path.with_suffix(".lock")
    acquired = False
    for _ in range(100):
        try:
            fd = os.open(str(lock_path), os.O_CREAT | os.O_EXCL | os.O_RDWR)
            os.close(fd)
            acquired = True
            break
        except FileExistsError:
            time.sleep(0.02)
        except OSError:
            break

    try:
        records = read_timing_records(path)
        records.append({
            "package": package,
            "stage": stage,
            "duration": max(0.0, float(duration)),
        })
        tmp_path = path.with_suffix(".tmp")
        tmp_path.write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding="utf-8")
        tmp_path.replace(path)
    finally:
        if acquired:
            try:
                lock_path.unlink()
            except OSError:
                pass


def clear_timing_records(path: Path = TIMING_FILE) -> None:
    if path.is_file():
        try:
            path.unlink()
        except OSError:
            pass


def stage_label(package: str, stage: str) -> str:
    desc = STAGE_DESCRIPTIONS.get(package, {}).get(stage)
    if desc:
        return f"{stage} ({desc})"
    return stage


def format_timing_report(records: list[dict], metadata: dict | None = None) -> str:
    if not records:
        return "没有耗时统计记录。"

    # Group by package preserving order of occurrence
    grouped: dict[str, list[dict]] = {}
    for r in records:
        pkg = r["package"]
        if pkg not in grouped:
            grouped[pkg] = []
        grouped[pkg].append(r)

    grand_total = sum(r["duration"] for r in records)

    lines: list[str] = []
    bar_width = 94
    lines.append("=" * bar_width)
    lines.append(pad_display("PlayWise 打包耗时统计报告", bar_width, align="center"))
    if metadata:
        meta_items = [f"[{k}: {v}]" for k, v in metadata.items()]
        lines.append("  " + "  ".join(meta_items))
    lines.append("=" * bar_width)
    header = (
        pad_display("包 / 阶段任务", 52)
        + " "
        + pad_display("耗时 (s)", 12, "right")
        + " "
        + pad_display("包内占比", 14, "right")
        + " "
        + pad_display("全局占比", 14, "right")
    )
    lines.append(header)
    lines.append("-" * bar_width)

    # Order: global first, then specific packages in standard order, then any other
    desired_order = ["global", "playwise", "playwise-complete", "device-lab", "eden-test"]
    package_keys = [k for k in desired_order if k in grouped]
    for k in grouped:
        if k not in package_keys:
            package_keys.append(k)

    for pkg in package_keys:
        items = grouped[pkg]
        pkg_title = PACKAGE_TITLES.get(pkg, f"包 {pkg}")
        pkg_total = sum(item["duration"] for item in items)
        pkg_overall_pct = (pkg_total / grand_total * 100.0) if grand_total > 0 else 0.0

        lines.append(f"[{pkg_title}]")
        for item in items:
            stg = item["stage"]
            dur = item["duration"]
            lbl = f"  {stage_label(pkg, stg)}"
            dur_str = f"{dur:.2f}s" if dur >= 0.01 else "<0.01s"

            if pkg == "global":
                pkg_pct_str = "-"
            else:
                pct = (dur / pkg_total * 100.0) if pkg_total > 0 else 0.0
                pkg_pct_str = f"{pct:5.1f}%"

            ov_pct = (dur / grand_total * 100.0) if grand_total > 0 else 0.0
            ov_pct_str = f"{ov_pct:5.1f}%"

            row = (
                pad_display(lbl, 52)
                + " "
                + pad_display(dur_str, 12, "right")
                + " "
                + pad_display(pkg_pct_str, 14, "right")
                + " "
                + pad_display(ov_pct_str, 14, "right")
            )
            lines.append(row)

        # Subtotal
        sub_dur_str = f"{pkg_total:.2f}s" if pkg_total >= 0.01 else "<0.01s"
        sub_pkg_pct = "100.0%" if pkg != "global" else "-"
        sub_ov_pct = f"{pkg_overall_pct:5.1f}%"
        sub_row = (
            pad_display("  小计", 52)
            + " "
            + pad_display(sub_dur_str, 12, "right")
            + " "
            + pad_display(sub_pkg_pct, 14, "right")
            + " "
            + pad_display(sub_ov_pct, 14, "right")
        )
        lines.append(sub_row)
        lines.append("")

    lines.append("-" * bar_width)
    total_str = f"{grand_total:.2f}s"
    total_row = (
        pad_display("全流程总计耗时", 52)
        + " "
        + pad_display(total_str, 12, "right")
        + " "
        + pad_display("100.0%", 14, "right")
        + " "
        + pad_display("100.0%", 14, "right")
    )
    lines.append(total_row)
    lines.append("=" * bar_width)

    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) < 4 or sys.argv[3] != "--":
        print("Usage: stage_timer.py <package> <stage> -- <command...>", file=sys.stderr)
        return 1

    package = sys.argv[1]
    stage = sys.argv[2]
    cmd = sys.argv[4:]

    start_time = time.perf_counter()
    result = subprocess.run(cmd)
    duration = time.perf_counter() - start_time

    if result.returncode == 0:
        write_timing_record(package, stage, duration)

    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
