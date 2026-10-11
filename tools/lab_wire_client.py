#!/usr/bin/env python3
"""Over-FTP wire client for the Device Lab session (lab-only).

The Device Lab sysmodule polls switch/playwise-device-lab/inbox/pending and
claims any <id>.json file (ptc_sysmodule_process_all, sysmodule/sysmodule_core.c:406),
runs ptc_request_parse + ptc_lab_process_request, and writes results/<id>.json.
Writing a request file over FTP is therefore equivalent to submitting it from
the Device Lab overlay -- same inbox, same dispatch -- so the host can drive the
whole lab session with no on-device navigation and no physical long-press
(begin_phase has no hold check; lab_session.c:972). sys-botbase is only needed
for screenshots.

Lab-only: these requests drive the very safety gates the product requires, so
this must only ever run against a dedicated test console, never a managed
child's device.

Request JSON matches the companion builders (companion/request_client.c): a flat
{"version":1,"request_id":...,"type":...,"created_at":...,"payload":{...}}. The
sysmodule parser (common/protocol/request_schema.c) finds payload fields by a
flat key scan, so mode/phase inside payload read the same as the overlay's.
"""
from __future__ import annotations

import json
import random
import time
from pathlib import Path

SCHEMA_VERSION = 1
LAB_APP = "playwise-device-lab"
PENDING_DIR = f"switch/{LAB_APP}/inbox/pending"
RESULTS_DIR = f"switch/{LAB_APP}/results"
SESSION_REMOTE = f"switch/{LAB_APP}/lab/session.json"
CAMPAIGN_REMOTE = f"switch/{LAB_APP}/lab/campaign.json"
DISABLE_FLAG_REMOTE = f"switch/{LAB_APP}/flags/disable.flag"

# Mirrors sysmodule/lab_session.c LAB_ACTIVATION_AB_PHASES / LAB_PHASES and
# expected_phase(): the host must send phases in the exact order the device
# enforces (begin_phase rejects a phase != expected_phase).
LAB_ACTIVATION_AB_PHASES = (
    "ab_home_awake", "ab_sleep_wake", "ab_limited_settings_only",
    "ab_restriction_settings_only", "ab_grant_settings_only",
    "ab_restriction_before_unlimited", "ab_unlimited_settings_only",
)
LAB_PHASES = (
    "home_stopped", "home_started", "game_foreground",
    "game_suspended", "sleep_wake", "restriction_effect",
)
# Device phase windows (sysmodule/lab_session_internal.h): the two activation
# legs sample for 90s, every other phase for 15s.
PHASE_WINDOW_SECONDS = {"ab_home_awake": 90, "ab_sleep_wake": 90}
DEFAULT_PHASE_WINDOW_SECONDS = 15


class WireError(RuntimeError):
    pass


class WireTimeout(WireError):
    pass


def now_ms() -> int:
    return int(time.time() * 1000)


def new_request_id(ms: int | None = None, rand: int | None = None) -> str:
    """'<ms>-<hex4>', matching companion/file_protocol.c ptc_companion_make_request_id
    and passing ptc_request_id_is_valid ([A-Za-z0-9_-], length 1..79)."""
    stamp = now_ms() if ms is None else ms
    token = random.randint(0, 0xFFFF) if rand is None else (rand & 0xFFFF)
    return f"{stamp}-{token:04x}"


def expected_phase(session: dict) -> str | None:
    """Pure mirror of sysmodule/lab_session.c expected_phase(): the next phase
    name for a session's mode + next_phase, or None when the sequence is done."""
    mode = session.get("mode") or "full"
    n = session.get("next_phase")
    if not isinstance(n, int) or n < 0:
        return None
    if mode == "restriction_quick":
        return "restriction_effect" if n == 0 else None
    if mode == "timer_activation_ab":
        return LAB_ACTIVATION_AB_PHASES[n] if n < len(LAB_ACTIVATION_AB_PHASES) else None
    return LAB_PHASES[n] if n < len(LAB_PHASES) else None


def phase_window_seconds(phase: str) -> int:
    return PHASE_WINDOW_SECONDS.get(phase, DEFAULT_PHASE_WINDOW_SECONDS)


def build_request_json(request_id: str, type_: str, created_at: int,
                       payload: dict | None = None) -> str:
    """Serialize a request like the companion builders: a flat object with
    version/request_id/type/created_at/payload. payload is always emitted
    because ptc_request_parse requires the "payload" key to be present."""
    body = {
        "version": SCHEMA_VERSION,
        "request_id": request_id,
        "type": type_,
        "created_at": int(created_at),
        "payload": payload or {},
    }
    return json.dumps(body, ensure_ascii=False, separators=(",", ":")) + "\n"


class LabWireClient:
    """Submits lab_* requests to the device inbox over FTP and reads results.

    ``ftp`` is any object exposing upload_bytes/rename_path/remove_path/
    download_file with the SwitchFtpClient signatures, so a fake can be injected
    in tests and a real run passes a tools.install_package_via_ftp.SwitchFtpClient.
    """

    def __init__(self, ftp, *, now_ms=now_ms, clock=time.monotonic, sleep=time.sleep) -> None:
        self.ftp = ftp
        self._now_ms = now_ms
        self._clock = clock
        self._sleep = sleep

    def submit(self, type_: str, payload: dict | None = None, *,
               request_id: str | None = None, created_at: int | None = None) -> str:
        rid = request_id or new_request_id(self._now_ms())
        stamp = self._now_ms() if created_at is None else created_at
        data = build_request_json(rid, type_, stamp, payload).encode("utf-8")
        part = f"{PENDING_DIR}/{rid}.json.part"
        final = f"{PENDING_DIR}/{rid}.json"
        # Stage under a non-.json name the inbox scanner ignores, then rename so
        # the request appears atomically and is never claimed half-written.
        self.ftp.upload_bytes(data, part)
        self.ftp.rename_path(part, final)
        return rid

    def read_result(self, request_id: str, scratch: Path) -> dict | None:
        """Download results/<id>.json if present and parse it; None if absent."""
        scratch = Path(scratch)
        scratch.mkdir(parents=True, exist_ok=True)
        dest = scratch / f"result-{request_id}.json"
        try:
            self.ftp.download_file(f"{RESULTS_DIR}/{request_id}.json", dest)
        except Exception:
            return None
        try:
            value = json.loads(dest.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None
        return value if isinstance(value, dict) else None

    def wait_result(self, request_id: str, scratch: Path, *, timeout_s: float = 20.0,
                    poll_s: float = 1.0) -> dict:
        """Poll for results/<id>.json until it parses, then return a normalized
        {status, error_code, error_reason, lab_session, raw}. Raise WireTimeout
        if the device never claims/answers the request within timeout_s."""
        deadline = self._clock() + timeout_s
        while True:
            value = self.read_result(request_id, scratch)
            if value is not None:
                return self._normalize(value)
            if self._clock() >= deadline:
                raise WireTimeout(f"等待设备结果超时：{request_id}")
            self._sleep(poll_s)

    @staticmethod
    def _normalize(value: dict) -> dict:
        error = value.get("error") if isinstance(value.get("error"), dict) else {}
        session = value.get("lab_session") if isinstance(value.get("lab_session"), dict) else {}
        return {
            "status": value.get("status"),
            "error_code": error.get("code"),
            "error_reason": error.get("reason"),
            "lab_session": session,
            "raw": value,
        }

    def reset_session(self) -> None:
        """Delete session/campaign/disable-flag so the next lab_session_start
        starts clean. start_session short-circuits its 'previous session not
        complete' guard when session.json is absent (lab_session.c:1105
        ``else if (loaded && ...)``), so a plain delete is the entire reset -- no
        on-device observation or restore is required. Missing files are ignored."""
        for remote in (SESSION_REMOTE, CAMPAIGN_REMOTE, DISABLE_FLAG_REMOTE):
            try:
                self.ftp.remove_path(remote)
            except Exception:
                pass

