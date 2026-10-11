#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import device_lab_roundtrip as rt  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class FakeClient:
    """In-memory stand-in for SwitchFtpClient backed by a fake device SD tree."""

    def __init__(self, store: dict[str, bytes], truncate: set[str] | None = None) -> None:
        self.store = store
        self.truncate = truncate or set()

    def walk_remote_dir(self, remote_dir: str) -> list[tuple[str, int | None]]:
        rd = remote_dir.strip("/")
        prefix = f"{rd}/"
        out: list[tuple[str, int | None]] = []
        for rel, data in sorted(self.store.items()):
            if rel == rd or rel.startswith(prefix):
                out.append((rel, len(data)))
        return out

    def download_file(self, remote_rel: str, local_path: Path) -> int:
        data = self.store[remote_rel]
        if remote_rel in self.truncate:
            data = data[:-1]
        local_path.parent.mkdir(parents=True, exist_ok=True)
        local_path.write_bytes(data)
        return len(data)


def _mappings(reports: Path) -> list[rt.PullMapping]:
    return [
        rt.PullMapping("switch/playwise-device-lab/reports", reports, flatten=True),
        rt.PullMapping("switch/playwise/logs", reports / "standard" / "logs"),
    ]

def test_pull_is_idempotent() -> None:
    with tempfile.TemporaryDirectory(prefix="rt-pull-") as tmp:
        reports = Path(tmp) / "device-reports"
        manifest = reports / ".manifest.json"
        store = {
            "switch/playwise-device-lab/reports/run-1.json": b'{"run":1}',
            "switch/playwise-device-lab/reports/c1.campaign.json": b'{"campaign":1}',
            "switch/playwise/logs/2026-10-11/ipc-client.log": b"client=overlay stage=connected\n",
        }
        client = FakeClient(store)
        first = rt.pull_tree(client, _mappings(reports), manifest, apply=True)
        require(len(first["downloaded"]) == 3, f"first pull must fetch 3, got {first['downloaded']}")
        require(first["skipped"] == [], "first pull must skip nothing")
        require((reports / "run-1.json").read_bytes() == b'{"run":1}', "flat report must land by basename")
        require((reports / "standard" / "logs" / "2026-10-11" / "ipc-client.log").is_file(),
                "nested standard log must preserve its remote structure")

        second = rt.pull_tree(client, _mappings(reports), manifest, apply=True)
        require(second["downloaded"] == [], "second pull must not re-download unchanged evidence")
        require(len(second["skipped"]) == 3, f"second pull must skip 3, got {second['skipped']}")


def test_size_mismatch_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="rt-size-") as tmp:
        reports = Path(tmp) / "device-reports"
        manifest = reports / ".manifest.json"
        rel = "switch/playwise-device-lab/reports/run-1.json"
        client = FakeClient({rel: b'{"run":1}'}, truncate={rel})
        mapping = [rt.PullMapping("switch/playwise-device-lab/reports", reports, flatten=True)]
        try:
            rt.pull_tree(client, mapping, manifest, apply=True)
        except rt.RoundtripError as exc:
            require("size mismatch" in str(exc), f"unexpected error: {exc}")
            return
        raise AssertionError("a truncated download must raise a size-mismatch RoundtripError")


def test_immutable_evidence_change_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="rt-immutable-") as tmp:
        reports = Path(tmp) / "device-reports"
        manifest = reports / ".manifest.json"
        rel = "switch/playwise-device-lab/reports/run-1.json"
        data = b'{"run":1}'
        rt.save_manifest(manifest, {rel: {"size": len(data), "sha256": "0" * 64}})
        client = FakeClient({rel: data})
        mapping = [rt.PullMapping("switch/playwise-device-lab/reports", reports, flatten=True)]
        try:
            rt.pull_tree(client, mapping, manifest, apply=True)
        except rt.RoundtripError as exc:
            require("immutable evidence changed" in str(exc), f"unexpected error: {exc}")
            return
        raise AssertionError("changed immutable evidence must raise a RoundtripError")


def main() -> int:
    test_pull_is_idempotent()
    test_size_mismatch_fails()
    test_immutable_evidence_change_fails()
    print("Device roundtrip tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
