#!/usr/bin/env python3
"""Explicit, internal usage probe artifact; never part of standard distribution."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "usage-probe"
ZIP = BUILD / "playwise-usage-probe.zip"
TITLE_ID = "4200000000BD23F1"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def lz4_block(data: bytes, size: int) -> bytes:
    if not 0 <= size <= 8 * 1024 * 1024:
        raise ValueError("NSO segment size out of bounds")
    output = bytearray()
    cursor = 0
    def extended(n: int) -> int:
        nonlocal cursor
        if n == 15:
            while True:
                if cursor >= len(data):
                    raise ValueError("truncated LZ4 length")
                byte = data[cursor]; cursor += 1; n += byte
                if byte != 255:
                    break
        return n
    while cursor < len(data):
        token = data[cursor]; cursor += 1
        literal = extended(token >> 4)
        if cursor + literal > len(data) or len(output) + literal > size:
            raise ValueError("invalid LZ4 literal")
        output.extend(data[cursor:cursor+literal]); cursor += literal
        if cursor == len(data):
            break
        if cursor + 2 > len(data):
            raise ValueError("truncated LZ4 offset")
        offset = int.from_bytes(data[cursor:cursor+2], "little"); cursor += 2
        length = extended(token & 15) + 4
        if not offset or offset > len(output) or len(output) + length > size:
            raise ValueError("invalid LZ4 match")
        for _ in range(length):
            output.append(output[-offset])
    if len(output) != size:
        raise ValueError("NSO uncompressed size mismatch")
    return bytes(output)


def nso_segments_from_nsp(data: bytes) -> bytes:
    if data[:4] != b"PFS0" or len(data) < 16:
        raise ValueError("invalid probe NSP")
    count, string_size = struct.unpack_from("<II", data, 4)
    if count > 16 or 16 + count*24 + string_size > len(data):
        raise ValueError("invalid PFS0 table")
    strings = data[16+count*24:16+count*24+string_size]
    base = 16 + count*24 + string_size
    main = None
    for i in range(count):
        offset, size, name_index = struct.unpack_from("<QQI", data, 16+i*24)
        if name_index >= len(strings) or base+offset+size > len(data):
            raise ValueError("invalid PFS0 member")
        name = strings[name_index:].split(b"\0", 1)[0]
        if name == b"main":
            main = data[base+offset:base+offset+size]
    if main is None or len(main) < 0x100 or main[:4] != b"NSO0":
        raise ValueError("missing main NSO")
    flags = struct.unpack_from("<I", main, 12)[0]
    result = bytearray()
    for i in range(3):
        offset, _, size = struct.unpack_from("<III", main, 0x10+i*0x10)
        encoded_size = struct.unpack_from("<I", main, 0x60+i*4)[0] if flags & (1 << i) else size
        if offset+encoded_size > len(main) or size > 8*1024*1024:
            raise ValueError("invalid NSO segment")
        segment = main[offset:offset+encoded_size]
        if flags & (1 << i):
            segment = lz4_block(segment, size)
        if flags & (1 << (i+3)) and hashlib.sha256(segment).digest() != main[0xa0+i*32:0xc0+i*32]:
            raise ValueError("NSO segment hash mismatch")
        result.extend(segment)
    return bytes(result)


def prepare() -> dict:
    files = sorted((ROOT / "usage_probe").glob("*")) + [
        ROOT / "platform/switch/pctl_adapter.c", ROOT / "platform/switch/play_timer_settings_layout.c",
        ROOT / "common/time/ptc_time.c", ROOT / "tools/package_usage_probe.py",
    ]
    sources = {p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in files if p.is_file() and p.suffix in {".c", ".h", ".json", ".py", ""}}
    identity = {
        "schema_version": 1, "profile": "usage-probe", "advisory": True,
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "sources": sources, "source_hash": sha(json.dumps(sources, sort_keys=True).encode()),
        "title_id": TITLE_ID,
    }
    devkit = os.environ.get("DEVKITPRO", "/opt/devkitpro")
    for label, command in {
        "compiler": [devkit + "/devkitA64/bin/aarch64-none-elf-gcc", "--version"],
        "libnx": ["dkp-pacman", "-Q", "libnx"],
    }.items():
        try:
            identity[label] = subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).splitlines()[0]
        except (OSError, subprocess.CalledProcessError) as exc:
            raise RuntimeError(f"cannot bind probe build {label}") from exc
    generated = BUILD / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    (generated / "identity.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
    (generated / "probe_identity.h").write_text('#define PROBE_SOURCE_HASH "' + identity["source_hash"] + '"\n', encoding="utf-8")
    return identity


def package() -> None:
    identity = json.loads((BUILD / "generated/identity.json").read_text(encoding="utf-8"))
    members = {
        f"atmosphere/contents/{TITLE_ID}/exefs.nsp": (ROOT / "usage_probe/usage-probe.nsp").read_bytes(),
        "switch/playwise-usage-probe/launcher.nro": (ROOT / "usage_probe/launcher.nro").read_bytes(),
    }
    identity["artifacts"] = {k: {"size": len(v), "sha256": sha(v)} for k, v in members.items()}
    members["switch/playwise-usage-probe/build.json"] = (json.dumps(identity, indent=2) + "\n").encode()
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in members.items():
            z.writestr(name, data)
    verify(ZIP)


def verify(path: Path = ZIP) -> dict:
    with zipfile.ZipFile(path) as z:
        identity = json.loads(z.read("switch/playwise-usage-probe/build.json"))
        expected = {f"atmosphere/contents/{TITLE_ID}/exefs.nsp", "switch/playwise-usage-probe/launcher.nro"}
        if identity.get("profile") != "usage-probe" or identity.get("advisory") is not True or identity.get("title_id") != TITLE_ID:
            raise ValueError("wrong probe identity")
        if not re.fullmatch(r"[a-f0-9]{64}", identity.get("source_hash", "")):
            raise ValueError("invalid probe source hash")
        if len(z.namelist()) != 3 or set(z.namelist()) != expected | {"switch/playwise-usage-probe/build.json"}:
            raise ValueError("unexpected probe member or boot flag")
        if set(identity["artifacts"]) != expected:
            raise ValueError("incomplete probe artifact binding")
        for name in expected:
            data = z.read(name)
            if len(data) != identity["artifacts"][name]["size"] or sha(data) != identity["artifacts"][name]["sha256"]:
                raise ValueError("probe artifact hash mismatch")
            decoded = nso_segments_from_nsp(data) if name.endswith(".nsp") else data
            if identity["source_hash"].encode() not in decoded:
                raise ValueError("missing embedded probe identity: " + name)
        nro = z.read("switch/playwise-usage-probe/launcher.nro")
        if nro[0x10:0x14] != b"NRO0":
            raise ValueError("invalid launcher NRO")
        if z.read(f"atmosphere/contents/{TITLE_ID}/exefs.nsp")[:4] != b"PFS0":
            raise ValueError("invalid probe NSP")
    return identity


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["prepare", "package", "verify"])
    action = p.parse_args().action
    if action == "prepare":
        prepare()
    elif action == "package":
        package()
    else:
        verify()
    print(f"PASS usage probe {action}")
