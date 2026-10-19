#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.device.ui_receipt_observer import observe_ui_receipts, summarize_receipts


def pair(request_id: str, status: str = "ok", kind: str = "status") -> tuple[dict, dict]:
    return ({"request_id": request_id, "type": kind},
            {"request_id": request_id, "type": kind, "status": status})


def main() -> None:
    assert summarize_receipts([])["response_evidence"] == "no_matching_ok_observed"
    one = summarize_receipts([pair("nro")])
    assert one["matching_ok_count"] == 1 and one["target_acknowledged"] is None
    assert one["visible_page_input_confirmed"] is None
    two = summarize_receipts([pair("nro"), pair("overlay")], target_request_id="overlay")
    assert two["response_evidence"] == "matching_ok_observed"
    assert two["matching_ok_count"] == 2 and two["additional_matching_responses"] == 1
    assert two["target_acknowledged"] and two["requires_producer_review"]
    assert not summarize_receipts([pair("other")], target_request_id="overlay")["target_acknowledged"]
    rejected = summarize_receipts([pair("ok"), pair("rejected", "error")], pending_ids=["pending"])
    assert rejected["matching_ok_count"] == 1 and rejected["requires_failure_review"]
    assert rejected["rejected_count"] == 1 and rejected["pending_request_ids"] == ["pending"]
    other = summarize_receipts([pair("ready", kind="overlay_ready")])
    assert other["matching_ok_count"] == 0 and other["requires_producer_review"]
    for pairs in ([pair("a"), pair("a")], [(pair("a")[0], pair("b")[1])]):
        try:
            summarize_receipts(pairs)
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate/mismatched receipt accepted")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        tick = [0.0]
        reads = []
        def sleep(seconds: float) -> None:
            tick[0] += seconds
        def done() -> list[str]:
            # Second producer arrives late; do not return at the first ok.
            return ["old.json", "nro.json"] + (["overlay.json"] if tick[0] >= 2 else [])
        def request(name: str) -> bytes:
            reads.append(name)
            return json.dumps(pair(name[:-5])[0]).encode()
        def result(request_id: str) -> bytes | None:
            if request_id == "overlay" and tick[0] < 3:
                return None
            return json.dumps(pair(request_id)[1]).encode()
        out = root / "two"
        value = observe_ui_receipts(out, before_names={"old.json"}, list_done=done,
                                    read_request=request, read_result=result,
                                    health=lambda: None, duration_s=4, poll_s=1,
                                    clock=lambda: tick[0], sleep=sleep)
        assert tick[0] == 4 and value["matching_ok_count"] == 2
        assert reads == ["nro.json", "overlay.json"]
        assert json.loads((out / "summary.json").read_bytes()) == value
        assert summarize_receipts([pair("nro"), pair("overlay")]) == value
        try:
            observe_ui_receipts(out, before_names=set(), list_done=done, read_request=request,
                                read_result=result, health=lambda: None)
        except FileExistsError:
            pass
        else:
            raise AssertionError("existing output overwritten")
        bad = root / "bad"
        try:
            observe_ui_receipts(bad, before_names=set(), list_done=lambda: ["a.json"],
                                read_request=lambda _: json.dumps(pair("a")[0]).encode(),
                                read_result=lambda _: json.dumps(pair("b")[1]).encode(),
                                health=lambda: None)
        except ValueError:
            pass
        else:
            raise AssertionError("wrong original result accepted")
        assert (bad / "accepted-a.json").exists() and (bad / "result-a.json").exists()
        assert (bad / "stopped.json").exists() and not (bad / "summary.json").exists()
    print("UI receipt observer regressions passed")


if __name__ == "__main__":
    main()
