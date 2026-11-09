#!/usr/bin/env python3
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.build import runtime_resource_report as report


def rejected(call) -> None:
    try:
        call()
    except ValueError:
        return
    raise AssertionError("invalid report accepted")


def main() -> None:
    image = {"ppm_sha256": {"child/sample.ppm": "a" * 64}}
    metrics = {"metrics": {
        "backend": {"schema_version": 1, "failures": 0, "cache_before_bytes": 10240,
                    "cache_after_bytes": 520, "warm_rules_parses": 0, "warm_state_parses": 0,
                    "warm_text_reads": 0, "calendar_reads_after": 4, "calendar_reads_before": 7},
        "nro": {"schema_version": 1, "failures": 0, "warm_font_size_calls": 0, "warm_font_loads": 0,
                "shadow_distance_calls_after": 5, "shadow_distance_calls_before": 50,
                "shadow_seconds_after_samples": [0.1] * 5},
        "overlay": {"schema_version": 1, "failures": 0, "allocations_after": 0,
                    "measurement_bytes_after": 10, "input_bytes": 10, "iterations": 1},
    }}
    expected = report.summarize(image, image, metrics)
    for key, value in (("warm_rules_parses", 1), ("cache_after_bytes", 4096), ("warm_text_reads", False)):
        invalid = deepcopy(metrics)
        invalid["metrics"]["backend"][key] = value
        rejected(lambda: report.summarize(image, image, invalid))
    changed = {"ppm_sha256": {"child/sample.ppm": "b" * 64}}
    rejected(lambda: report.summarize(image, changed, metrics))
    rejected(lambda: report.summarize(image, {"ppm_sha256": {}}, metrics))
    with tempfile.TemporaryDirectory(prefix="playwise-resource-report-") as directory:
        root = Path(directory)
        valid = root / "valid"
        valid.mkdir()
        for name, value in (("baseline", image), ("current", image), ("metrics", metrics), ("summary", expected)):
            report.write(valid / f"{name}.json", value)
        report.write(valid / "attachments.json", {"schema_version": 1, "sha256": {
            path.name: report.digest(path) for path in valid.iterdir()
        }})
        assert report.verify(valid) == expected
        assert report.verify(valid) == expected
        bad = root / "tampered"
        shutil.copytree(valid, bad)
        (bad / "summary.json").write_text(json.dumps({"schema_version": 1}), encoding="utf-8")
        rejected(lambda: report.verify(bad))
    print("PASS: resource report budgets, scalar types, pixel identity, JSON roundtrip and immutable attachment checks")


if __name__ == "__main__":
    main()
