#!/usr/bin/env python3
"""Run deterministic production-resource gates and retain machine-readable metrics."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main() -> int:
    directory = Path(sys.argv[1])
    font = Path(sys.argv[2])
    report: dict = {"schema_version": 1, "executables": {}, "metrics": {}}
    failed = False
    for name in ("backend", "nro", "overlay"):
        binary = directory / f"resource_{name}"
        command = [str(binary)] + ([str(font)] if name != "backend" else [])
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        if result.returncode:
            print(result.stdout, end="")
            failed = True
            continue
        metrics = json.loads(result.stdout)
        if type(metrics.get("schema_version")) is not int or metrics["schema_version"] != 1 or metrics.get("failures") != 0:
            raise ValueError(f"invalid {name} metrics")
        report["metrics"][name] = metrics
        report["executables"][name] = hashlib.sha256(binary.read_bytes()).hexdigest()
    if failed:
        return 1
    report["font_sha256"] = hashlib.sha256(font.read_bytes()).hexdigest()
    report["compilers"] = {
        name: subprocess.check_output([name, "--version"], text=True).splitlines()[0]
        for name in ("gcc", "g++")
    }
    path = directory / "runtime-resources.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if json.loads(path.read_text(encoding="utf-8")) != report:
        raise ValueError("resource metrics failed JSON roundtrip")
    print("PASS: runtime resources: parsed cache, one-pass calendars, identical shadow pixels and linear text wrapping")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
