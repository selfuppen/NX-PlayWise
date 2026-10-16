#!/usr/bin/env python3
"""Seal a Claude Code vision verdict into an auditable, hash-bound sidecar.

The sys-botbase vision pipeline (tools/automation/vision_capture_driver.py) drives a
Device Lab campaign slot and captures real-device screenshots into
device-reports/vision/<run_id>/, recording them in a pending.json. The AI
agent (Claude Code) reads those screenshots and decides the two observation
enums. This tool records that decision as <run_id>.vision.json next to the run
report, binding the verdict to each screenshot's SHA-256, the wire-log hash and
the sys-botbase commit.

This sidecar is the AUTHORITATIVE provenance for a vision-driven run: it names
the method `vision_ai` explicitly (never masquerading as a human observation),
and verify_device_qualification.py --observation-source vision_ai requires it,
re-hashing every referenced screenshot. The screenshots are the audit trail --
more reviewable than an ephemeral human glance.

Fail-closed: the tool records the AI's TRUE reading. If that reading does not
satisfy the qualification matrix (restriction_visible + paused/continued), it
WARNs that the slot will not pass -- it never nudges a verdict into a pass.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import sys

# The Device Lab on-device observation UI enums (device_lab/overlay/source/
# main.cpp submit_observation): the vision verdict must stay inside them.
OBSERVATIONS = ("restriction_visible", "no_visible_restriction", "unsure")
RUNTIME_EFFECTS = ("continued", "paused_or_suspended", "exited", "unsure")
# The qualification matrix only accepts a visible restriction plus a decisive
# runtime effect; everything else is an honest non-pass (plan risk 2).
PASSING_RUNTIME_EFFECTS = ("continued", "paused_or_suspended")


class VerdictError(RuntimeError):
    pass


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            try:
                reconfigure(errors="replace")
            except (ValueError, OSError):
                pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise VerdictError(f"无法读取 JSON：{path}: {exc}") from exc
    if not isinstance(value, dict):
        raise VerdictError(f"JSON 顶层必须是对象：{path}")
    return value


def now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def seal(reports_dir: Path, pending_path: Path, *, observation: str, runtime_effect: str,
         context_confirmed: bool, confidence: str, reasoning: str, now=now_iso) -> dict:
    """Build a sealed vision verdict, re-hashing every screenshot named by the
    driver's pending.json. Raises VerdictError on any missing/changed evidence
    so a tampered or stale bundle can never be sealed."""
    if observation not in OBSERVATIONS:
        raise VerdictError(f"observation 非法：{observation!r}；取值 {OBSERVATIONS}")
    if runtime_effect not in RUNTIME_EFFECTS:
        raise VerdictError(f"runtime_effect 非法：{runtime_effect!r}；取值 {RUNTIME_EFFECTS}")
    if not reasoning.strip():
        raise VerdictError("reasoning 不能为空：必须引用具体画面证据")
    pending = read_json(pending_path)
    run_id = pending.get("run_id")
    if not isinstance(run_id, str) or not run_id:
        raise VerdictError(f"pending.json 缺少 run_id：{pending_path}")
    screenshots = pending.get("screenshots")
    if not isinstance(screenshots, list) or len(screenshots) < 2:
        raise VerdictError("pending.json 需要至少 2 张截图（基线 + 窗口帧），避免单帧幻觉定案")
    sealed_shots: list[dict] = []
    for shot in screenshots:
        if not isinstance(shot, dict):
            raise VerdictError("pending.json 的 screenshots 元素必须是对象")
        rel = shot.get("path")
        recorded = shot.get("sha256")
        if not isinstance(rel, str) or not rel:
            raise VerdictError("截图缺少 path")
        abs_path = reports_dir / rel
        if not abs_path.is_file():
            raise VerdictError(f"封存前截图缺失：{abs_path}")
        actual = sha256_file(abs_path)
        if recorded is not None and recorded != actual:
            raise VerdictError(
                f"截图自抓取后已变化，拒绝封存：{rel} 记录 {recorded} != 实际 {actual}")
        sealed_shots.append({
            "checkpoint": shot.get("checkpoint"),
            "path": rel,
            "sha256": actual,
            "captured_at": shot.get("captured_at"),
        })
    wire_log_sha256 = pending.get("wire_log_sha256")
    if not isinstance(wire_log_sha256, str) or not wire_log_sha256:
        raise VerdictError("pending.json 缺少 wire_log_sha256")
    sysbotbase_commit = pending.get("sysbotbase_commit")
    if not isinstance(sysbotbase_commit, str) or not sysbotbase_commit:
        raise VerdictError("pending.json 缺少 sysbotbase_commit")
    return {
        "schema_version": 1,
        "method": "vision_ai",
        "run_id": run_id,
        "campaign_id": pending.get("campaign_id"),
        "slot": pending.get("slot"),
        "observation": observation,
        "runtime_effect": runtime_effect,
        "context_confirmed": bool(context_confirmed),
        "confidence": confidence,
        "reasoning": reasoning.strip(),
        "sysbotbase_commit": sysbotbase_commit,
        "wire_log_sha256": wire_log_sha256,
        "captured_at": now(),
        "screenshots": sealed_shots,
    }


def verdict_satisfies_matrix(verdict: dict) -> bool:
    return (verdict.get("observation") == "restriction_visible"
            and verdict.get("runtime_effect") in PASSING_RUNTIME_EFFECTS
            and verdict.get("context_confirmed") is True)


def write_verdict(reports_dir: Path, verdict: dict) -> Path:
    out = reports_dir / f"{verdict['run_id']}.vision.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(verdict, ensure_ascii=False, indent=2) + "\n",
                   encoding="utf-8", newline="\n")
    return out


def _parse_bool(text: str) -> bool:
    low = text.strip().lower()
    if low in ("y", "yes", "true", "1"):
        return True
    if low in ("n", "no", "false", "0"):
        return False
    raise argparse.ArgumentTypeError(f"expected y/n, got {text!r}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="封存 Claude Code 视觉判定为可审计旁证。")
    sub = parser.add_subparsers(dest="command", required=True)
    p_seal = sub.add_parser("seal", help="把一个槽位的 AI 判定封存为 <run_id>.vision.json")
    p_seal.add_argument("--reports", type=Path, default=Path("device-reports"))
    p_seal.add_argument("--pending", type=Path, default=None,
                        help="pending.json 路径；默认 <reports>/vision/<run-id>/pending.json")
    p_seal.add_argument("--run-id", default=None,
                        help="未显式给 --pending 时用来定位默认 pending.json")
    p_seal.add_argument("--observation", required=True, choices=OBSERVATIONS)
    p_seal.add_argument("--runtime-effect", required=True, choices=RUNTIME_EFFECTS)
    p_seal.add_argument("--context-confirmed", type=_parse_bool, required=True)
    p_seal.add_argument("--confidence", default="high")
    p_seal.add_argument("--reasoning", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    configure_console()
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command == "seal":
        pending_path = args.pending
        if pending_path is None:
            if not args.run_id:
                print("FAIL: 需要 --pending 或 --run-id 之一来定位 pending.json", file=sys.stderr)
                return 2
            pending_path = args.reports / "vision" / args.run_id / "pending.json"
        try:
            verdict = seal(args.reports.resolve(), pending_path.resolve(),
                           observation=args.observation, runtime_effect=args.runtime_effect,
                           context_confirmed=args.context_confirmed, confidence=args.confidence,
                           reasoning=args.reasoning)
            out = write_verdict(args.reports.resolve(), verdict)
        except VerdictError as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 1
        print(f"已封存 {out}")
        if not verdict_satisfies_matrix(verdict):
            print("WARN: 该判定不满足资格矩阵（需 restriction_visible + continued/paused_or_suspended "
                  "+ context_confirmed）。这是如实的非通过结果，请勿为放行而改判。")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

