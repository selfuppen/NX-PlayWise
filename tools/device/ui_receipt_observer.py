"""Bounded, read-only UI receipt observation; never sends input or requests.

Several clients can respond to one observation window. Count matching original
IDs separately from proving which visible page received input.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import time
from typing import Callable


def summarize_receipts(pairs: list[tuple[dict, dict]], *,
                       expected_type: str = "status",
                       target_request_id: str | None = None,
                       pending_ids: list[str] | None = None) -> dict:
    """An extra acknowledgement is diagnostic context, not a missing reply."""
    records = []
    seen = set()
    for request, result in pairs:
        request_id = request["request_id"]
        if request_id in seen or (request_id, request["type"]) != (
                result.get("request_id"), result.get("type")):
            raise ValueError("duplicate or mismatched original receipt")
        seen.add(request_id)
        records.append({"request_id": request_id, "type": request["type"],
                        "status": result.get("status")})
    matching = [r for r in records if r["type"] == expected_type and r["status"] == "ok"]
    rejected = [r for r in records if r["type"] == expected_type and r["status"] != "ok"]
    unexpected = [r for r in records if r["type"] != expected_type]
    pending = sorted(pending_ids or [])
    return {
        "schema_version": 1,
        "response_evidence": "matching_ok_observed" if matching else "no_matching_ok_observed",
        "matching_ok_count": len(matching),
        "rejected_count": len(rejected),
        "unexpected_type_count": len(unexpected),
        "pending_request_ids": pending,
        "records": records,
        "target_request_id": target_request_id,
        "target_acknowledged": (any(r["request_id"] == target_request_id for r in matching)
                                if target_request_id is not None else None),
        "additional_matching_responses": max(0, len(matching) - 1),
        "requires_producer_review": len(matching) > 1 or bool(unexpected),
        "requires_failure_review": bool(rejected or pending),
        "visible_page_input_confirmed": None,
        "qualified": False,
    }


def _write_json(path: Path, value: object) -> None:
    with path.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    if json.loads(path.read_bytes()) != value:
        raise ValueError("JSON readback differs")


def observe_ui_receipts(out: Path, *, before_names: set[str],
                        list_done: Callable[[], list[str]],
                        read_request: Callable[[str], bytes],
                        read_result: Callable[[str], bytes | None],
                        health: Callable[[], None], duration_s: float = 12,
                        poll_s: float = 1, expected_type: str = "status",
                        target_request_id: str | None = None,
                        clock: Callable[[], float] = time.monotonic,
                        sleep: Callable[[float], None] = time.sleep) -> dict:
    """Collect the full bounded window, including delayed additional clients.

The caller saves the baseline before one authorized input. All callbacks here
are read-only. A positive response does not certify input isolation or producer
identity; exact target attribution needs an independently known request ID.
    """
    if not 0 < poll_s <= duration_s <= 60:
        raise ValueError("observation bounds outside supported range")
    out.mkdir(parents=True, exist_ok=False)
    _write_json(out / "baseline.json", {"done_names": sorted(before_names),
                                      "duration_s": duration_s, "poll_s": poll_s})
    deadline = clock() + duration_s
    requests: dict[str, dict] = {}
    pairs: dict[str, tuple[dict, dict]] = {}
    try:
        while True:
            health()
            for name in sorted(set(list_done()) - before_names):
                if not re.fullmatch(r"[A-Za-z0-9_-]+\.json", name):
                    raise ValueError("unsafe request filename")
                if name not in requests:
                    raw = read_request(name)
                    (out / ("accepted-" + name)).write_bytes(raw)
                    request = json.loads(raw)
                    if request.get("request_id") != name[:-5] or not isinstance(request.get("type"), str):
                        raise ValueError("request filename or type differs")
                    requests[name] = request
                if name in pairs:
                    continue
                raw = read_result(requests[name]["request_id"])
                if raw is None:
                    continue
                (out / ("result-" + name)).write_bytes(raw)
                pair = (requests[name], json.loads(raw))
                # Validate the exact original ID/type before recording success.
                summarize_receipts([pair], expected_type=expected_type)
                pairs[name] = pair
            remaining = deadline - clock()
            if remaining <= 0:
                break
            sleep(min(poll_s, remaining))
        value = summarize_receipts([pairs[n] for n in sorted(pairs)],
                                   expected_type=expected_type,
                                   target_request_id=target_request_id,
                                   pending_ids=[requests[n]["request_id"]
                                                for n in sorted(requests) if n not in pairs])
        _write_json(out / "summary.json", value)
        return value
    except Exception as error:
        _write_json(out / "stopped.json", {"error_type": type(error).__name__,
                                           "qualified": False})
        raise
