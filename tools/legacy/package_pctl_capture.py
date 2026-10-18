#!/usr/bin/env python3
"""Internal same-process read-only PCTL tap; zip is inert staging, never release."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import struct
import tarfile
import tempfile
import zipfile

from tools.legacy.package_usage_probe import nso_segments_from_nsp
from tools.build.generate_release_manifest import write_header

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/pctl-capture"
ZIP = BUILD / "playwise-pctl-capture.zip"
STAGE = "switch/playwise-pctl-capture"
MARKER = b"PCTL1952_CAPTURE_ONLY"


def prepare() -> dict:
    installed = BUILD / "input/installed-manifest.json"
    baseline_manifest = json.loads(installed.read_text(encoding="utf-8")) if installed.exists() else json.loads((ROOT / "build/generated/release-manifest.json").read_text(encoding="utf-8"))
    candidate_file = BUILD / "input/candidate-commit.txt"
    commit = candidate_file.read_text(encoding="utf-8").strip() if candidate_file.exists() else baseline_manifest["commit"]
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or baseline_manifest.get("profile") != "release":
        raise ValueError("capture baseline requires a pinned local release commit")
    base = BUILD / ("base-" + commit[:12])
    base.mkdir(parents=True, exist_ok=True)
    archive = subprocess.check_output(["git", "archive", "--format=tar", commit, "sysmodule", "common", "platform", "third_party/cjson"], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as t:
        members = t.getmembers()
        for member in members:
            if not (member.isdir() or member.isfile()):
                raise ValueError("baseline archive must contain only regular files/directories")
            (base / member.name).resolve().relative_to(base.resolve())
        t.extractall(base, members=members)
    baseline_header = base / "build/generated/release_manifest.h"
    baseline_header.parent.mkdir(parents=True, exist_ok=True)
    write_header(baseline_header, baseline_manifest)
    # Add only the four compile-time tap hooks. Refuse any other main-loop diff.
    current = (ROOT / "sysmodule/source/main.c").read_text(encoding="utf-8")
    plain = re.sub(r"#ifdef PLAYWISE_PCTL_CAPTURE\n.*?#endif\n", "", current, flags=re.S)
    original = (base / "sysmodule/source/main.c").read_text(encoding="utf-8")
    if plain != original:
        raise ValueError("capture main loop differs from pinned baseline beyond diagnostic hooks")
    (base / "sysmodule/source/main.c").write_text(current, encoding="utf-8")
    for name in ("pctl_usage_tap.c", "pctl_usage_tap.h"):
        (base / "sysmodule" / name).write_bytes((ROOT / "sysmodule" / name).read_bytes())
    makefile = base / "sysmodule/Makefile"
    text = makefile.read_text(encoding="utf-8")
    text = text.replace(" $(SYSMODULE_CFILES)\n", " $(SYSMODULE_CFILES) $(EXTRA_CFILES)\n")
    makefile.write_text(text, encoding="utf-8")
    sources = {}
    for directory in ("sysmodule", "common", "platform", "third_party/cjson"):
        for path in sorted((base / directory).rglob("*")):
            if path.suffix in (".c", ".h") and "build" not in path.parts:
                sources[path.relative_to(base).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ("sysmodule/Makefile", "tools/legacy/package_pctl_capture.py"):
        sources[name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
    identity = {"version": 1, "profile": "pctl-capture-internal", "advisory": True,
                "title_id": "4200000000BD2300", "process_model": "existing standard process; no extra sysmodule",
                "source_hash": hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest(),
                "sources": sources, "source_commit": commit, "base_directory": str(base.relative_to(ROOT)),
                "baseline_manifest": baseline_manifest,
                "changes": "compile-time diagnostic tap only; no product display algorithm changes",
                "permissions": "unchanged standard NPDM; no PDM, PM, input or power service",
                "capture_interval_s": 5, "qualification": "pending_hardware"}
    generated = BUILD / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    (generated / "identity.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
    (generated / "capture_identity.h").write_text('#define PCTL_CAPTURE_SOURCE_HASH "' + identity["source_hash"] + '"\n', encoding="utf-8")
    target = base / "build/pctl-capture/generated/capture_identity.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes((generated / "capture_identity.h").read_bytes())
    return identity


def build() -> None:
    j = json.loads((BUILD / "generated/identity.json").read_text(encoding="utf-8"))
    directory = ROOT / j["base_directory"] / "sysmodule"
    subprocess.run(["make", "-C", str(directory), "TARGET=pctc-sysmodule", "BUILD=build-baseline"], check=True)
    data = (directory / "pctc-sysmodule.nsp").read_bytes()
    (directory / "pctl-baseline.nsp").write_bytes(data)
    installed = BUILD / "input/installed-exefs.nsp"
    # Without an installed preimage this target remains a build-only prototype.
    j["baseline_loaded_segments_match"] = installed.exists() and normalized_segments(data) == normalized_segments(installed.read_bytes())
    j["baseline_npdm_match"] = installed.exists() and nsp_member(data, "main.npdm") == nsp_member(installed.read_bytes(), "main.npdm")
    j["control_nsp_sha256"] = hashlib.sha256(data).hexdigest()
    j["comparison_normalization"] = "GNU build-id note only, validated against each NSO header; code/data and module name must match"
    if installed.exists():
        j["installed_nsp_sha256"] = hashlib.sha256(installed.read_bytes()).hexdigest()
    (BUILD / "generated/identity.json").write_text(json.dumps(j, indent=2) + "\n", encoding="utf-8")
    if installed.exists() and not (j["baseline_loaded_segments_match"] and j["baseline_npdm_match"]):
        raise ValueError("installed baseline cannot be reproduced; do not replace the running backend")
    subprocess.run(["make", "-C", str(directory), "TARGET=pctc-sysmodule", "BUILD=build-pctl-capture",
                    "EXTRA_CFILES=pctl_usage_tap.c", "DEFINES=-DPLAYWISE_PCTL_CAPTURE"], check=True)
    (directory / "pctl-capture.nsp").write_bytes((directory / "pctc-sysmodule.nsp").read_bytes())
    if nsp_member((directory / "pctl-capture.nsp").read_bytes(), "main.npdm") != nsp_member(data, "main.npdm"):
        raise ValueError("capture changed baseline process permissions")


def nsp_member(data: bytes, wanted: str) -> bytes:
    if data[:4] != b"PFS0":
        raise ValueError("invalid capture PFS0")
    count, string_size = struct.unpack_from("<II", data, 4)
    strings = data[16+count*24:16+count*24+string_size]
    base = 16+count*24+string_size
    for i in range(count):
        offset, size, index = struct.unpack_from("<QQI", data, 16+i*24)
        if strings[index:].split(b"\0", 1)[0].decode() == wanted:
            return data[base+offset:base+offset+size]
    raise ValueError("missing capture member: " + wanted)


def normalized_segments(data: bytes) -> bytes:
    decoded = bytearray(nso_segments_from_nsp(data))
    main = nsp_member(data, "main")
    build_id = main[0x40:0x54]
    note = struct.pack("<III", 4, 20, 3) + b"GNU\0" + build_id
    position = decoded.find(note)
    if position < 0 or decoded.find(note, position + 1) >= 0 or main[0x54:0x60] != bytes(12):
        raise ValueError("cannot uniquely bind the GNU note to the NSO build-id; no normalization allowed")
    decoded[position+16:position+36] = bytes(20)
    # Preserve segment memory offsets, sizes and BSS extent as part of equality.
    layout = b"".join(main[0x14+i*0x10:0x1c+i*0x10] for i in range(3)) + main[0x3c:0x40]
    return layout + decoded


def package() -> None:
    identity = json.loads((BUILD / "generated/identity.json").read_text(encoding="utf-8"))
    data = (ROOT / identity["base_directory"] / "sysmodule/pctl-capture.nsp").read_bytes()
    identity["artifact"] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(STAGE + "/staged-exefs.nsp", data)
        z.writestr(STAGE + "/capture-build.json", json.dumps(identity, indent=2).encode())
    verify()


def host_test() -> None:
    binary = BUILD / "test-pctl-capture"
    subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                    "-DPLAYWISE_PCTL_CAPTURE", "-Itests/pctl_capture", "tests/c/test_pctl_capture.c",
                    "sysmodule/pctl_usage_tap.c", "third_party/cjson/cJSON.c", "-o", str(binary)], cwd=ROOT, check=True)
    for failure in range(7):
        with tempfile.TemporaryDirectory(prefix="pctl-tap-test-") as work:
            subprocess.run([str(binary), str(failure)], cwd=work, check=True)


def verify(path: Path = ZIP) -> dict:
    with zipfile.ZipFile(path) as z:
        if set(z.namelist()) != {STAGE + "/staged-exefs.nsp", STAGE + "/capture-build.json"}:
            raise ValueError("capture archive must be inert staging only, without boot flag or activation config")
        j = json.loads(z.read(STAGE + "/capture-build.json"))
        data = z.read(STAGE + "/staged-exefs.nsp")
        if j.get("profile") != "pctl-capture-internal" or j.get("title_id") != "4200000000BD2300":
            raise ValueError("invalid internal capture identity")
        if len(data) != j["artifact"]["size"] or hashlib.sha256(data).hexdigest() != j["artifact"]["sha256"]:
            raise ValueError("capture artifact hash mismatch")
        decoded = nso_segments_from_nsp(data)
        if MARKER not in decoded or j["source_hash"].encode() not in decoded:
            raise ValueError("capture payload is not source-bound")
    standard = ROOT / "build/switch/exefs.nsp"
    if standard.exists() and MARKER in nso_segments_from_nsp(standard.read_bytes()):
        raise ValueError("internal tap leaked into standard package")
    return j


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("prepare", "build", "package", "verify", "host-test"))
    action = p.parse_args().action
    prepare() if action == "prepare" else build() if action == "build" else package() if action == "package" else host_test() if action == "host-test" else verify()
    print("PASS internal PCTL capture " + action)
