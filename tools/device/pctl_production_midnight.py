"""Offline checks for one retained production stream across a real local midnight."""
from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
from datetime import date, datetime, timedelta, timezone
import hashlib
import json

import tools.device.pctl_standard_evidence as evidence
import tools.qualification.pctl_usage_qualification as usage
from tools.legacy.pctl_capture import normalized_sample

SECOND = 1_000_000_000
ZONE = timezone(timedelta(hours=8))
EPOCH = date(2020, 1, 1)


def local_day(unix_s: int) -> tuple[int, int]:
    clock = datetime.fromtimestamp(unix_s, ZONE)
    return (clock.date() - EPOCH).days, clock.hour * 3600 + clock.minute * 60 + clock.second


def growth_window(rows: list[dict], *, early: bool) -> dict:
    if early:
        before = rows[0]
        choices = [row for row in rows if int(row["begin_ns"]) - int(before["begin_ns"]) >= 60 * SECOND]
        usage.require(bool(choices), "new-day usable window shorter than 60 device seconds")
        after = choices[0]
    else:
        after = rows[-1]
        choices = [row for row in rows if int(after["begin_ns"]) - int(row["begin_ns"]) >= 60 * SECOND]
        usage.require(bool(choices), "old-day usable window shorter than 60 device seconds")
        before = choices[-1]
    elapsed = int(after["begin_ns"]) - int(before["begin_ns"])
    delta = int(after["total_ns"]) - int(before["total_ns"])
    usage.require(abs(delta - elapsed) <= usage.PAIRING_NS, "usable window consumption does not follow device elapsed time")
    return {"before_ns": before["begin_ns"], "after_ns": after["begin_ns"],
            "elapsed_ns": str(elapsed), "consumption_ns": str(delta)}


def verify(data: bytes, config: dict, before_day: int) -> dict:
    usage.integer(before_day, 0, 65534, "before_day")
    audit = evidence.audit(data, config)
    usage.require(audit["start"].get("append_diagnostics_version") == 1,
                  "production midnight requires sequence and append diagnostics")
    usage.require(audit["stop_record_present"] and not audit["partial_tail_lines"] and not audit["gaps"],
                  "midnight stream is not complete and stopped")
    stop = json.loads([line for line in data.splitlines() if line.strip()][-1])
    usage.require(stop["consecutive_failures"] == 0 and stop["reason"] in
                  ("config_removed_or_changed", "lease_expired", "process_handoff"), "midnight stream stopped in failure")
    days = {before_day: [], before_day + 1: []}
    boundary, transients = [], []
    last_day = before_day
    calls = None
    previous_wire = None
    settings_base = None
    # Only tomorrow's three native day-setting words may change naturally.
    settings_offset = (7 + ((before_day + 1 + 3) % 7) * 4) * 2
    allowed_offsets = set(range(settings_offset, settings_offset + 6))
    for item in audit["samples"]:
        row = item["raw"]
        usage.require(not row["restricted"] and not row["temporarily_unlocked"], "midnight observation was restricted or PIN-unlocked")
        if calls is None:
            calls = row["start_timer_calls"]
        usage.require(row["start_timer_calls"] == calls, "automatic midnight introduced 1451")
        if previous_wire is not None:
            wall_delta = (row["unix_before"] - previous_wire["unix_before"]) * SECOND
            tick_delta = int(row["begin_ns"]) - int(previous_wire["begin_ns"])
            usage.require(tick_delta <= 15 * SECOND, "observation gap exceeds three production intervals")
            usage.require(abs(wall_delta - tick_delta) <= usage.PAIRING_NS, "wall clock differs from the retained device timeline")
        previous_wire = row
        old, old_second = local_day(row["unix_before"])
        new, _ = local_day(row["unix_after"])
        usage.require((old, new) == (row["day_before"], row["day_after"]), "raw local date differs from UTC+08 calendar")
        settings = bytes.fromhex(row["settings_hex"])
        if settings_base is None:
            usage.require(old == before_day, "stream must start before midnight")
            settings_base = settings
        differences = {index for index, (a, b) in enumerate(zip(settings_base, settings)) if a != b}
        usage.require(not differences if new == before_day else differences <= allowed_offsets,
                      "unrelated 0x44 bytes changed within midnight stream")
        if not row["valid"]:
            expected_boundary = (row["clock_before_valid"] and row["clock_after_valid"] and
                old == before_day and new == before_day + 1 and
                all(row[key] == 0 for key in evidence.RESULT_FIELDS) and
                0 <= row["unix_after"] - row["unix_before"] <= 2 and int(row["total_ns"]) >= 0)
            usage.require(expected_boundary and not boundary, "non-boundary failed sample or repeated date crossing")
            boundary_modes = {normalized_sample(dict(row, day_index=day))["settings_mode"] for day in (old, new)}
            usage.require(boundary_modes <= {"unlimited", "unconfigured"}, "boundary sample contains a limited target")
            boundary.append(item["line"])
            continue
        usage.require(old in days and old >= last_day, "unexpected date, rewind or multi-day stream")
        last_day = old
        decoded = normalized_sample(dict(row, day_index=old))
        if decoded["settings_mode"] == "unconfigured" and old == before_day + 1 and old_second <= 10:
            transients.append(item["line"])
        else:
            usage.require(decoded["settings_mode"] == "unlimited", "midnight target is not true unlimited")
        previous = days[old][-1] if days[old] else None
        if previous is not None:
            elapsed = int(row["begin_ns"]) - int(previous["begin_ns"])
            delta = int(row["total_ns"]) - int(previous["total_ns"])
            usage.require(0 <= delta <= elapsed + usage.PAIRING_NS, "same-day counter regression or jump")
        days[old].append(row)
    usage.require(boundary == audit["invalid_sample_lines"], "failed raw sample was hidden")
    usage.require(bool(days[before_day]) and bool(days[before_day + 1]), "both local dates required")
    old_last, new_first = days[before_day][-1], days[before_day + 1][0]
    _, second = local_day(new_first["unix_after"])
    usage.require(second <= 10 and int(new_first["total_ns"]) <= second * SECOND + usage.PAIRING_NS and
                  int(new_first["total_ns"]) < int(old_last["total_ns"]), "natural new-day cumulative reset missing")
    usage.require(new_first["unix_before"] - old_last["unix_after"] <= 15,
                  "missing retained endpoints around midnight")
    stable_new = [row for row in days[before_day + 1] if local_day(row["unix_before"])[1] >= 10 and
                  normalized_sample(dict(row, day_index=before_day + 1))["settings_mode"] == "unlimited"]
    usage.require(bool(stable_new), "new-day policy never became unlimited")
    left, right = bytes.fromhex(old_last["settings_hex"]), bytes.fromhex(stable_new[-1]["settings_hex"])
    changed = [index for index, (a, b) in enumerate(zip(left, right)) if a != b]
    usage.require(set(changed) <= allowed_offsets, "unrelated 0x44 bytes changed at midnight")
    return {"schema_version": 1, "kind": "production_midnight_raw_check", "qualified": False,
            "raw_sha256": hashlib.sha256(data).hexdigest(), "boot_id": audit["start"]["boot_id"],
            "before_day": before_day, "after_day": before_day + 1, "samples": len(audit["samples"]),
            "boundary_invalid_lines_retained": boundary, "initial_unconfigured_lines_retained": transients,
            "old_day_growth": growth_window(days[before_day], early=False),
            "new_day_growth": growth_window(stable_new, early=True), "automatic_1451_increment": 0,
            "old_last": old_last, "new_first": new_first, "new_last": stable_new[-1],
            "changed_settings_offsets": changed, "timer_before": old_last["timer_enabled"],
            "timer_after": stable_new[-1]["timer_enabled"],
            "note": "One-boot UTC+08 boundary consistency only; real chronology, history and operator observations remain separate."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--before-day", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.raw.read_bytes(), usage.read_json(args.config), args.before_day)
    result["raw_path"] = str(args.raw.resolve())
    evidence.save_new(args.output, result)
    print("PASS midnight raw consistency; qualification and history remain separate")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (usage.UsageQualificationError, OSError, ValueError, KeyError, TypeError) as exc:
        raise SystemExit(f"FAIL: {exc}") from exc
