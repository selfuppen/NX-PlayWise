#!/usr/bin/env python3
"""Host regression for tools/lab_wire_client.py.

Drives the wire client against an in-memory fake FTP (no real device) and pins:
the stage-then-rename submit (a request appears only as a complete <id>.json),
result parsing/normalization, timeout behavior under an injected clock, the
three-file session reset, and that expected_phase mirrors the device's phase
order. No real device, no real time.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import lab_wire_client as lwc  # noqa: E402

FAILURES = 0


def check(condition: bool, label: str) -> None:
    global FAILURES
    if not condition:
        FAILURES += 1
        print(f"FAIL: {label}", file=sys.stderr)


class FakeFtp:
    """In-memory stand-in for SwitchFtpClient: paths are stored stripped of any
    leading/trailing slash so remote spellings compare cleanly."""

    def __init__(self) -> None:
        self.files: dict[str, bytes] = {}
        self.renames: list[tuple[str, str]] = []
        self.removed: list[str] = []

    def upload_bytes(self, data: bytes, remote: str) -> None:
        self.files[remote.strip("/")] = bytes(data)

    def rename_path(self, src: str, dst: str) -> None:
        s, d = src.strip("/"), dst.strip("/")
        if s not in self.files:
            raise IOError(f"missing source {s}")
        self.files[d] = self.files.pop(s)
        self.renames.append((s, d))

    def remove_path(self, remote: str) -> None:
        r = remote.strip("/")
        self.removed.append(r)
        self.files.pop(r, None)

    def download_file(self, remote: str, local) -> int:
        r = remote.strip("/")
        if r not in self.files:
            raise IOError(f"not found {r}")
        local = Path(local)
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_bytes(self.files[r])
        return len(self.files[r])

    def put_result(self, request_id: str, payload: dict) -> None:
        remote = f"{lwc.RESULTS_DIR}/{request_id}.json".strip("/")
        self.files[remote] = (json.dumps(payload) + "\n").encode("utf-8")


# <!-- APPEND-MARKER -->

def test_new_request_id_valid() -> None:
    rid = lwc.new_request_id(1700000000000, 0x1a2b)
    check(rid == "1700000000000-1a2b", "request_id is '<ms>-<hex4>'")
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
    check(all(c in allowed for c in rid) and 0 < len(rid) < 80,
          "request_id passes ptc_request_id_is_valid charset/length")


def test_expected_phase() -> None:
    check(lwc.expected_phase({"mode": "restriction_quick", "next_phase": 0}) == "restriction_effect",
          "restriction_quick phase 0 is restriction_effect")
    check(lwc.expected_phase({"mode": "restriction_quick", "next_phase": 1}) is None,
          "restriction_quick is done after phase 0")
    seq = [lwc.expected_phase({"mode": "timer_activation_ab", "next_phase": n}) for n in range(7)]
    check(tuple(seq) == lwc.LAB_ACTIVATION_AB_PHASES, "timer_activation_ab yields the 7 AB phases in order")
    check(lwc.expected_phase({"mode": "timer_activation_ab", "next_phase": 7}) is None,
          "timer_activation_ab is done after the 7th phase")
    check(lwc.expected_phase({"mode": "full", "next_phase": 5}) == "restriction_effect",
          "full mode phase 5 is restriction_effect")
    check(lwc.expected_phase({"mode": "timer_activation_ab", "next_phase": None}) is None,
          "missing next_phase yields no expected phase")


def test_submit_stages_then_renames() -> None:
    ftp = FakeFtp()
    client = lwc.LabWireClient(ftp, now_ms=lambda: 1700000000000)
    rid = client.submit("lab_phase_start", {"phase": "restriction_effect"}, request_id="req-1")
    check(rid == "req-1", "submit returns the request id")
    final = f"{lwc.PENDING_DIR}/req-1.json".strip("/")
    part = f"{lwc.PENDING_DIR}/req-1.json.part".strip("/")
    check(final in ftp.files, "final <id>.json exists after submit")
    check(part not in ftp.files, "staged <id>.json.part is gone after rename")
    check(ftp.renames == [(part, final)], "submit staged .part then renamed to .json")
    body = json.loads(ftp.files[final].decode("utf-8"))
    check(body["version"] == 1 and body["type"] == "lab_phase_start", "request carries version + type")
    check(body["payload"] == {"phase": "restriction_effect"}, "payload carries the phase")
    check("payload" in body, "payload key present (ptc_request_parse requires it)")


def test_wait_result_reads_and_normalizes() -> None:
    ftp = FakeFtp()
    client = lwc.LabWireClient(ftp)
    ftp.put_result("req-ok", {
        "version": 1, "request_id": "req-ok", "type": "lab_session_start", "status": "ok",
        "lab_session": {"run_id": "r1", "state": "ready", "next_phase": 0, "restored": False},
        "lab_campaign": None,
    })
    with tempfile.TemporaryDirectory() as tmp:
        res = client.wait_result("req-ok", Path(tmp), timeout_s=5.0)
    check(res["status"] == "ok", "ok result normalized to status ok")
    check(res["error_code"] is None, "ok result has no error code")
    check(res["lab_session"]["state"] == "ready", "lab_session state surfaced")


def test_wait_result_surfaces_error_code() -> None:
    ftp = FakeFtp()
    client = lwc.LabWireClient(ftp)
    ftp.put_result("req-err", {
        "version": 1, "request_id": "req-err", "type": "lab_session_start", "status": "error",
        "error": {"code": 405, "reason": "pctl_effect_not_observed", "message": "x"},
        "lab_session": {"state": "ready", "next_phase": 0},
    })
    with tempfile.TemporaryDirectory() as tmp:
        res = client.wait_result("req-err", Path(tmp), timeout_s=5.0)
    check(res["status"] == "error", "error result normalized to status error")
    check(res["error_code"] == 405, "error code surfaced")
    check(res["error_reason"] == "pctl_effect_not_observed", "error reason surfaced")


def test_wait_result_timeout() -> None:
    ftp = FakeFtp()
    ticks = iter([0.0, 10.0, 20.0, 30.0, 40.0])
    client = lwc.LabWireClient(ftp, clock=lambda: next(ticks), sleep=lambda _s: None)
    raised = False
    with tempfile.TemporaryDirectory() as tmp:
        try:
            client.wait_result("never", Path(tmp), timeout_s=1.0)
        except lwc.WireTimeout:
            raised = True
    check(raised, "missing result raises WireTimeout once the injected clock passes the deadline")


def test_reset_session_removes_three() -> None:
    ftp = FakeFtp()
    ftp.files[lwc.SESSION_REMOTE.strip("/")] = b"{}"
    client = lwc.LabWireClient(ftp)
    client.reset_session()
    for remote in (lwc.SESSION_REMOTE, lwc.CAMPAIGN_REMOTE, lwc.DISABLE_FLAG_REMOTE):
        check(remote.strip("/") in ftp.removed, f"reset removes {remote}")
    check(lwc.SESSION_REMOTE.strip("/") not in ftp.files, "session.json is gone after reset")


class FakeRawFtp:
    """Minimal ftplib.FTP stand-in to exercise SwitchFtpClient.upload_bytes /
    rename_path (the two methods lab_wire_client depends on) without a device."""

    def __init__(self, size_override: int | None = None) -> None:
        self.stored: dict[str, bytes] = {}
        self.renames: list[tuple[str, str]] = []
        self._size_override = size_override

    def cwd(self, path):  # noqa: D401 - fake
        pass

    def mkd(self, name):
        pass

    def storbinary(self, cmd, fileobj):
        name = cmd.split(" ", 1)[1]
        self.stored[name] = fileobj.read()

    def size(self, name):
        if self._size_override is not None:
            return self._size_override
        return len(self.stored.get(name, b""))

    def rename(self, src, dst):
        self.renames.append((src, dst))

    def delete(self, name):
        self.stored.pop(name, None)


def test_switchftp_upload_bytes_and_rename() -> None:
    import install_package_via_ftp as ftp_installer
    endpoint = ftp_installer.FtpEndpoint(host="h", port=21, user="anonymous", password="", base_path="/")
    client = ftp_installer.SwitchFtpClient(endpoint)
    client.ftp = FakeRawFtp()
    client.upload_bytes(b"hello-wire", "switch/x/inbox/pending/req.json.part")
    check(client.ftp.stored.get("req.json.part") == b"hello-wire", "upload_bytes stores the exact bytes")
    client.rename_path("switch/x/inbox/pending/req.json.part", "switch/x/inbox/pending/req.json")
    check(client.ftp.renames and client.ftp.renames[-1][1].endswith("req.json"),
          "rename_path issues RNFR/RNTO to the final .json name")
    # size mismatch (truncated transfer) must raise so a bad request isn't claimed
    client.ftp = FakeRawFtp(size_override=999)
    raised = False
    try:
        client.upload_bytes(b"hi", "switch/x/inbox/pending/y.json.part")
    except IOError:
        raised = True
    check(raised, "upload_bytes raises on a SIZE mismatch")


def main() -> int:
    test_new_request_id_valid()
    test_expected_phase()
    test_submit_stages_then_renames()
    test_wait_result_reads_and_normalizes()
    test_wait_result_surfaces_error_code()
    test_wait_result_timeout()
    test_reset_session_removes_three()
    test_switchftp_upload_bytes_and_rename()
    if FAILURES:
        print(f"{FAILURES} lab_wire_client test(s) failed", file=sys.stderr)
        return 1
    print("PASS: lab_wire_client wire format, result parsing, reset and phase order")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

