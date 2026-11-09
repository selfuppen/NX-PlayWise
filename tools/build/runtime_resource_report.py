#!/usr/bin/env python3
"""Seal Host resource metrics and lossless preview equivalence; verify offline."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value


def write(path: Path, value: dict) -> None:
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
    if load(path) != value:
        raise ValueError(f"JSON roundtrip mismatch: {path}")


def summarize(baseline: dict, current: dict, metrics: dict) -> dict:
    for group in metrics["metrics"].values():
        if type(group.get("schema_version")) is not int or group["schema_version"] != 1:
            raise ValueError("invalid metrics schema")
        for key, value in group.items():
            if key == "failures" and (type(value) is not int or value != 0):
                raise ValueError("resource tests failed")
            if key.endswith("_samples"):
                if not isinstance(value, list) or len(value) != 5 or any(
                    type(sample) not in (int, float) or sample < 0 or not math.isfinite(sample) for sample in value
                ):
                    raise ValueError(f"invalid timing samples {key}")
                continue
            if type(value) not in (int, float) or value < 0 or not math.isfinite(value):
                raise ValueError(f"invalid metric {key}")
    b, n, o = (metrics["metrics"][name] for name in ("backend", "nro", "overlay"))
    savings = b["cache_before_bytes"] - b["cache_after_bytes"]
    if savings < 8192 or any(b[key] for key in ("warm_rules_parses", "warm_state_parses", "warm_text_reads")):
        raise ValueError("parsed-cache resource gate failed")
    if b["calendar_reads_after"] >= b["calendar_reads_before"]:
        raise ValueError("calendar read gate failed")
    if n["warm_font_size_calls"] or n["warm_font_loads"] or n["shadow_distance_calls_after"] >= n["shadow_distance_calls_before"]:
        raise ValueError("NRO resource gate failed")
    if o["allocations_after"] or o["measurement_bytes_after"] > 2 * o["input_bytes"] * o["iterations"]:
        raise ValueError("Overlay resource gate failed")
    before, after = baseline["ppm_sha256"], current["ppm_sha256"]
    if not before or before.keys() != after.keys():
        raise ValueError("preview matrix membership changed")
    different = [name for name in before if before[name] != after[name]]
    if different:
        raise ValueError(f"preview pixels changed: {different[:12]}")
    return {"schema_version": 1, "cache_saved_bytes": savings,
            "identical_ppm_count": len(before), "metrics": metrics["metrics"],
            "measurement_scope": "deterministic Host; not Switch CPU, frame rate or hardware qualification",
            "baseline_kind": {"cache": "original 6144+4096 byte source buffers",
                              "calendar": "original validate-then-load composition",
                              "nro_shadow": "frozen scalar production implementation",
                              "overlay_wrap": "frozen prefix-measurement production implementation"}}


def verify(directory: Path) -> dict:
    attachments = load(directory / "attachments.json")
    if attachments.get("schema_version") != 1:
        raise ValueError("invalid attachment schema")
    for name, expected in attachments["sha256"].items():
        path = directory / name
        if path.parent.resolve() != directory.resolve() or digest(path) != expected:
            raise ValueError(f"attachment mismatch: {name}")
    result = summarize(load(directory / "baseline.json"), load(directory / "current.json"),
                       load(directory / "metrics.json"))
    if result != load(directory / "summary.json"):
        raise ValueError("derived summary differs on repeat verification")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--capture-baseline", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--metrics", type=Path, default=ROOT / "build/host/runtime-resources.json")
    parser.add_argument("--previews", type=Path, default=ROOT / "build/ui-previews")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--build-command", default="python -X utf8 tools/package_remote.py --with-previews --preview-scope all")
    args = parser.parse_args()
    if args.capture_baseline:
        metadata = {kind: load(args.previews / f".{kind}-render.json") for kind in ("nro", "overlay")}
        names = sorted(set().union(*(set(item["outputs"]) for item in metadata.values())))
        write(args.capture_baseline, {"schema_version": 1,
            "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "source_status": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
            "render_metadata": metadata, "ppm_sha256": {name: digest(args.previews / name) for name in names}})
        print(f"Captured {len(names)} baseline previews")
        return 0
    if args.verify:
        result = verify(args.verify)
    else:
        if not args.baseline or not args.out:
            parser.error("--baseline and --out are required when sealing")
        baseline = load(args.baseline)
        current = {"schema_version": 1, "ppm_sha256": {
            name: digest(args.previews / name) for name in sorted(baseline["ppm_sha256"])
        }}
        # Check the fresh render inventories, not an unscoped directory of old images.
        rendered: set[str] = set()
        for kind in ("nro", "overlay"):
            rendered.update(load(args.previews / f".{kind}-render.json")["outputs"])
        if rendered != set(current["ppm_sha256"]):
            raise ValueError("fresh render inventories differ from the baseline matrix")
        metrics = load(args.metrics)
        result = summarize(baseline, current, metrics)
        args.out.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(args.baseline, args.out / "baseline.json")
        shutil.copyfile(args.metrics, args.out / "metrics.json")
        write(args.out / "current.json", current)
        write(args.out / "summary.json", result)
        names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT).decode().split("\0")
        source = {name: digest(ROOT / name) for name in sorted(set(names)) if name and
                  ((ROOT / name).suffix in {".c", ".h", ".cpp", ".hpp", ".inc", ".mk", ".py"} or
                   (ROOT / name).name == "Makefile")}
        artifacts = ("companion/nro/pctc.nro", "companion/overlay/playwise.ovl",
                     "sysmodule/pctc-sysmodule.nsp", "build/eden-test/pctc-eden.nro")
        write(args.out / "source.json", {
            "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "status": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
            "sha256": source, "compilers": metrics["compilers"],
            "artifacts_sha256": {name: digest(ROOT / name) for name in artifacts},
            "commands": [args.build_command,
                         "make test-runtime-resources (included in unified test-host)"]})
        shutil.copyfile(Path(__file__), args.out / "verifier.py")
        write(args.out / "attachments.json", {"schema_version": 1, "sha256": {
            path.name: digest(path) for path in sorted(args.out.iterdir()) if path.is_file()
        }})
        if verify(args.out) != result or verify(args.out) != result:
            raise ValueError("repeat offline verification differs")
    print(f"PASS: {result['identical_ppm_count']} identical previews; {result['cache_saved_bytes']} bytes less persistent cache")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
