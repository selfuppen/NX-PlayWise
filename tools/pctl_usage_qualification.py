from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

from playwise_version import read_playwise_version


CONTRACT = "pctl1952_v1_proxy1440"
DIRECT_CONTRACT = "pctl1952_v2_direct"
CURRENT_CONTRACT = DIRECT_CONTRACT
SUPPORTED_CONTRACTS = (CONTRACT, DIRECT_CONTRACT)
REPORT_NAME = "pctl-usage-qualification.json"
CHECKS = ("pctl1952_candidate_identity", "pctl1952_usage_grant_continuity",
          "pctl1952_proxy_handover", "pctl1952_restore")
CASES = {
    "usage_live": ("before", "after"),
    "grant_recovery": ("before", "after", "active_end"),
    "proxy_cycle": ("unlimited", "proxy", "blocked", "proxy_restored", "unlimited_restored"),
    "reason_handover": ("both_blocked", "eye_removed", "released"),
}
OBSERVATIONS = ("same_candidate_loaded", "raw_pairing_checked", "console_scope_confirmed",
                "grant_access_restored", "proxy_cycle_observed", "remaining_reason_still_blocks")
DIRECT_CASES = dict(CASES)
DIRECT_CASES.pop("proxy_cycle")
DIRECT_CASES["direct_cycle"] = ("unlimited", "tracking", "blocked", "restored", "unlimited_restored")
DIRECT_OBSERVATIONS = tuple("direct_cycle_observed" if name == "proxy_cycle_observed" else name for name in OBSERVATIONS)


def checks_for(contract: str) -> tuple[str, ...]:
    require(contract in SUPPORTED_CONTRACTS, "unsupported PCTL usage contract")
    return tuple("pctl1952_direct_handover" if name == "pctl1952_proxy_handover" else name
                 for name in CHECKS) if contract == DIRECT_CONTRACT else CHECKS


def contract_cases(contract: str) -> dict[str, tuple[str, ...]]:
    require(contract in SUPPORTED_CONTRACTS, "unsupported PCTL usage contract")
    return DIRECT_CASES if contract == DIRECT_CONTRACT else CASES
COMPONENTS = ("atmosphere/contents/4200000000BD2300/exefs.nsp",
              "switch/playwise/pctc.nro", "switch/.overlays/playwise.ovl")
MINUTE_NS = 60_000_000_000
DAY_NS = 1440 * MINUTE_NS
PAIRING_NS = 2_000_000_000


class UsageQualificationError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise UsageQualificationError(message)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict:
    def unique_pairs(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            require(key not in result, f"{path.name}: duplicate JSON key {key}")
            result[key] = value
        return result

    try:
        result = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_pairs)
    except (OSError, UnicodeError, ValueError) as exc:
        raise UsageQualificationError(f"{path.name}: cannot read JSON: {exc}") from exc
    require(isinstance(result, dict), f"{path.name}: object required")
    return result


def integer(value: object, minimum: int, maximum: int, label: str) -> int:
    require(type(value) is int and minimum <= value <= maximum, f"{label}: invalid integer")
    return value


def nanoseconds(value: object, signed: bool, label: str) -> int:
    pattern = r"(?:0|-[1-9][0-9]*|[1-9][0-9]*)" if signed else r"(?:0|[1-9][0-9]*)"
    require(isinstance(value, str) and len(value) <= 20 and re.fullmatch(pattern, value) is not None,
            f"{label}: canonical decimal string required")
    parsed = int(value)
    require(-(1 << 63) <= parsed <= (1 << 63) - 1, f"{label}: out of range")
    return parsed


def sample(value: object, label: str) -> dict:
    require(isinstance(value, dict), f"{label}: sample object required")
    require(value.get("ipc_result") == 0 and type(value.get("ipc_result")) is int,
            f"{label}: successful 1952 read required")
    require(value.get("source") == "pctl_1952" and value.get("scope") == "console_usable_time",
            f"{label}: wrong source/scope")
    total = nanoseconds(value.get("total_ns"), False, label + ".total_ns")
    require(total <= DAY_NS, f"{label}: day total out of range")
    mono = nanoseconds(value.get("monotonic_ns"), False, label + ".monotonic_ns")
    require(mono > 0 and isinstance(value.get("boot_id"), str) and bool(value["boot_id"]),
            f"{label}: capture epoch missing")
    integer(value.get("day_index"), 0, 65535, label + ".day_index")
    integer(value.get("captured_at"), 1, (1 << 63) - 1, label + ".captured_at")
    integer(value.get("pairing_span_ns"), 0, PAIRING_NS, label + ".pairing_span_ns")
    integer(value.get("start_timer_calls"), 0, 65535, label + ".start_timer_calls")
    require(type(value.get("timer_enabled")) is bool and value.get("temporarily_unlocked") is False,
            f"{label}: timer or temporary PIN state unconfirmed")
    integer(value.get("business_minutes"), 0, 1440, label + ".business_minutes")
    configured = integer(value.get("configured_minutes"), 0, 1440, label + ".configured_minutes")
    mode = value.get("pctl_mode")
    require(mode in ("limit", "blocked", "unlimited") and value.get("business_mode") in ("limit", "unlimited"),
            f"{label}: unknown target")
    require((mode == "limit" and configured > 0) or (mode != "limit" and configured == 0),
            f"{label}: target minutes mismatch")
    reasons = value.get("restriction_reasons")
    require(isinstance(reasons, list) and all(isinstance(r, str) for r in reasons) and
            set(reasons) <= {"daily_allowance", "bedtime", "eye_care", "dock"} and len(set(reasons)) == len(reasons),
            f"{label}: invalid restriction reasons")
    if mode == "limit":
        remaining = nanoseconds(value.get("signed_remaining_ns"), True, label + ".signed_remaining_ns")
        require(abs(remaining - (configured * MINUTE_NS - total)) <= PAIRING_NS,
                f"{label}: signed remaining does not match independent consumption")
    return value


def bound_file(reports: Path, binding: object, label: str) -> Path:
    require(isinstance(binding, dict) and isinstance(binding.get("path"), str) and bool(binding["path"]),
            f"{label}: evidence path missing")
    relative = Path(binding["path"])
    resolved = (reports / relative).resolve()
    require(not relative.is_absolute() and resolved.is_relative_to(reports.resolve()),
            f"{label}: evidence path outside report bundle")
    require(resolved.is_file() and resolved.stat().st_size > 0 and binding.get("sha256") == sha256_file(resolved),
            f"{label}: evidence hash mismatch")
    return resolved


def direct_raw_pair(reports: Path, value: dict, bindings: list[dict], manifest: dict) -> dict:
    """New contracts pair every endpoint to an actual stopped production stream."""
    import pctl_standard_evidence as production
    from pctl_capture import normalized_sample
    binding = value.get("raw_source")
    path = bound_file(reports, binding, "direct sample raw")
    require(any(Path(item["path"]) == path and item["sha256"] == binding["sha256"] for item in bindings),
            "direct sample raw is outside its retained sources")
    data = path.read_bytes()
    try:
        first = json.loads(data.splitlines()[0])
    except (IndexError, ValueError, UnicodeError) as exc:
        raise UsageQualificationError("malformed direct production raw") from exc
    require(isinstance(first, dict), "production start object required")
    require(first.get("commit") == manifest["commit"] and first.get("release_id") == manifest["release_id"],
            "direct raw belongs to another production candidate")
    require(isinstance(first.get("run_id"), str) and re.fullmatch(r"[0-9a-f]{32}", first["run_id"]) is not None,
            "direct raw run identity missing")
    duration = nanoseconds(first.get("duration_ns"), False, "raw duration")
    require(duration % 1_000_000_000 == 0, "non-integral raw lease")
    config = {"commit": manifest["commit"], "release_id": manifest["release_id"],
              "run_id": first.get("run_id"), "duration_s": duration // 1_000_000_000}
    integer(config["duration_s"], 300, 7200, "raw lease")
    audit = production.audit(data, config)
    require(audit["stop_record_present"] and not audit["partial_tail_lines"] and
            not audit["invalid_sample_lines"] and not audit["gaps"], "direct raw is incomplete or invalid")
    stop = json.loads([line for line in data.splitlines() if line.strip()][-1])
    require(stop["consecutive_failures"] == 0 and stop["reason"] in
            ("config_removed_or_changed", "lease_expired", "process_handoff"), "direct raw ended in failure")
    line = integer(binding.get("line"), 2, len(data.splitlines()), "raw source line")
    found = [item["raw"] for item in audit["samples"] if item["line"] == line]
    require(len(found) == 1, "direct endpoint is not a production sample")
    raw = found[0]
    decoded = normalized_sample(dict(raw, day_index=raw["day_before"]))
    expected_mode = "limit" if decoded["settings_mode"] == "limited" else decoded["settings_mode"]
    require(value["pctl_mode"] == expected_mode and value["configured_minutes"] == decoded["configured_minutes"],
            "direct target differs from complete raw 0x44")
    require(all(value[name] == raw[field] for name, field in (
        ("boot_id", "boot_id"), ("day_index", "day_before"), ("captured_at", "unix_after"),
        ("monotonic_ns", "begin_ns"), ("total_ns", "total_ns"), ("timer_enabled", "timer_enabled"),
        ("temporarily_unlocked", "temporarily_unlocked"))), "direct endpoint differs from retained raw")
    require(value["pairing_span_ns"] == int(raw["end_ns"]) - int(raw["begin_ns"]) and
            value["start_timer_calls"] == int(raw["start_timer_calls"]), "direct pairing or 1451 count differs from raw")
    if expected_mode == "limit":
        require(value["signed_remaining_ns"] == raw["signed_remaining_ns"], "direct signed remaining differs from raw")
    return raw


def verify_direct_cycle(values: list[dict], data: dict) -> None:
    expected = ("unlimited", "unlimited", "blocked", "unlimited", "unlimited")
    require(len(values) == 5 and all(v["pctl_mode"] == mode and v["configured_minutes"] == 0 and
            v["business_mode"] == "unlimited" and v["business_minutes"] == 0
            for v, mode in zip(values, expected)), "direct_cycle: synthetic quota or incorrect restoration")
    require(all(type(v.get("eye_care_enabled")) is bool for v in values) and
            [v["eye_care_enabled"] for v in values] == [False, True, True, True, False] and
            [set(v["restriction_reasons"]) for v in values] == [set(), set(), {"eye_care"}, set(), set()] and
            data.get("automatic_recovery") is True, "direct_cycle: health lifecycle or automatic recovery missing")
    elapsed = int(values[1]["monotonic_ns"]) - int(values[0]["monotonic_ns"])
    require(elapsed >= MINUTE_NS and abs(int(values[1]["total_ns"]) - int(values[0]["total_ns"]) - elapsed) <= PAIRING_NS,
            "direct_cycle: true-unlimited consumption progression missing")
    deadline = integer(data.get("rest_deadline_unix"), 1, (1 << 63) - 1, "rest deadline")
    # A five-second sample after expiry legitimately includes resumed use. Bound
    # only that suffix, never treat the whole blocked interval as usable time.
    suffix = values[3]["captured_at"] - deadline
    require(values[2]["captured_at"] < deadline and 0 <= suffix <= 15,
            "direct_cycle: natural expiry endpoint missing or too late")
    delta = int(values[3]["total_ns"]) - int(values[2]["total_ns"])
    require(len({v["start_timer_calls"] for v in values}) == 1 and
            0 <= delta <= suffix * 1_000_000_000 + PAIRING_NS,
            "direct_cycle: automatic 1451 or consumption during restriction")


def verify_report(reports: Path, manifest: dict, standard_sha256: str, components: dict[str, str],
                  model: str, hos: str, atmosphere: str) -> dict:
    path = reports / REPORT_NAME
    report = read_json(path)
    require(report.get("schema_version") == 1 and type(report.get("schema_version")) is int and
            report.get("report_status") == "final", f"{REPORT_NAME}: final schema v1 required")
    contract = manifest.get("usage_contract")
    require(contract in SUPPORTED_CONTRACTS and report.get("usage_contract") == contract,
            "unsupported or mismatched PCTL usage contract")
    direct = contract == DIRECT_CONTRACT
    cases_spec = contract_cases(contract)
    require(manifest.get("build", {}).get("source_dirty") is False, "PCTL candidate contains tracked modifications")
    subject = {key: manifest[key] for key in ("commit", "release_id")}
    require(report.get("subject") == subject and report.get("standard_zip_sha256") == standard_sha256 and
            report.get("release_components") == components, "PCTL report candidate identity mismatch")
    expected_model = "mariko-oled" if model.lower() in ("oled", "nintendo switch oled") else model.lower()
    require(report.get("environment") == {"model": expected_model, "hos": hos, "atmosphere": atmosphere},
            "PCTL report environment mismatch")
    observations = report.get("observations", {})
    require(isinstance(observations, dict) and all(observations.get(key) is True for key in
            (DIRECT_OBSERVATIONS if direct else OBSERVATIONS)),
            "PCTL report manual observations incomplete")
    attestation = report.get("attestation", {})
    require(isinstance(attestation, dict) and isinstance(attestation.get("operator"), str) and
            bool(attestation["operator"].strip()), "PCTL report operator attestation missing")
    integer(attestation.get("signed_at"), 1, (1 << 63) - 1, "attestation.signed_at")
    restoration = report.get("restoration", {})
    require(isinstance(restoration, dict), "PCTL restoration object required")
    before = restoration.get("settings_before_hex")
    restored = restoration.get("settings_restored_hex")
    require(isinstance(before, str) and re.fullmatch(r"[0-9a-fA-F]{136}", before) is not None and
            isinstance(restored, str) and restored.lower() == before.lower() and
            type(restoration.get("timer_before")) is bool and
            restoration.get("timer_after") is restoration["timer_before"] and
            restoration.get("unexpected_raw_offsets") == [], "PCTL exact restoration not proved")
    cases = report.get("cases")
    require(isinstance(cases, dict) and set(cases) == set(cases_spec), "PCTL case set incomplete")
    evidence = []
    paired_raw = {}
    used_paths = set()
    for case, phases in cases_spec.items():
        binding = cases[case]
        resolved = bound_file(reports, binding, case)
        require(resolved not in used_paths and resolved != path.resolve(), f"{case}: evidence file reused")
        used_paths.add(resolved)
        data = read_json(resolved)
        require(type(data.get("schema_version")) is int and data["schema_version"] == 1 and
                data.get("kind") == "pctl_usage_samples" and data.get("subject") == subject and
                data.get("release_components") == components,
                f"{case}: evidence belongs to another candidate")
        raw_sources = data.get("raw_sources")
        require(isinstance(raw_sources, list) and bool(raw_sources), f"{case}: retained raw sources missing")
        raw_bindings = []
        for raw_binding in raw_sources:
            raw_path = bound_file(reports, raw_binding, case + ".raw")
            require(raw_path not in {resolved, path.resolve()} and
                    str(raw_path) not in {r["path"] for r in raw_bindings}, f"{case}: raw source reused as normalized sample")
            raw_bindings.append({"path": str(raw_path), "sha256": raw_binding["sha256"]})
        values = data.get("samples")
        require(isinstance(values, list) and len(values) == len(phases), f"{case}: sample sequence incomplete")
        for index, (value, phase) in enumerate(zip(values, phases)):
            sample(value, f"{case}.{phase}")
            require(value.get("phase") == phase, f"{case}: phase order mismatch")
            if direct:
                paired_raw[json.dumps(value["raw_source"], sort_keys=True)] = direct_raw_pair(reports, value, raw_bindings, manifest)
            if index:
                previous = values[index - 1]
                elapsed = int(value["monotonic_ns"]) - int(previous["monotonic_ns"])
                delta = int(value["total_ns"]) - int(previous["total_ns"])
                require(value["boot_id"] == previous["boot_id"] and value["day_index"] == previous["day_index"] and
                        value["captured_at"] >= previous["captured_at"] and 0 < elapsed <= 300_000_000_000 and
                        value["start_timer_calls"] >= previous["start_timer_calls"] and
                        abs((value["captured_at"] - previous["captured_at"]) * 1_000_000_000 - elapsed) <= PAIRING_NS and
                        0 <= delta <= elapsed + PAIRING_NS, f"{case}: counter/clock discontinuity or invented consumption")
        if case == "usage_live":
            elapsed = int(values[1]["monotonic_ns"]) - int(values[0]["monotonic_ns"])
            delta = int(values[1]["total_ns"]) - int(values[0]["total_ns"])
            require(elapsed >= MINUTE_NS and abs(delta - elapsed) <= PAIRING_NS and
                    all(not v["restriction_reasons"] and v["timer_enabled"] for v in values),
                    "usage_live: usable 60-second progression missing")
        elif case == "grant_recovery":
            before, after, end = values
            face = integer(data.get("grant_minutes"), 1, 120, "grant_minutes")
            used = int(before["total_ns"])
            base = min(1440, max(before["business_minutes"], (used + MINUTE_NS - 1) // MINUTE_NS))
            target = min(1440, base + face)
            require(before["business_mode"] == "limit" and used > before["business_minutes"] * MINUTE_NS and
                    all(v["pctl_mode"] == "limit" for v in values) and
                    before["configured_minutes"] == before["business_minutes"] and
                    set(before["restriction_reasons"]) == {"daily_allowance"} and
                    not after["restriction_reasons"] and not end["restriction_reasons"] and
                    after["business_minutes"] == after["configured_minutes"] == target and
                    end["configured_minutes"] == end["business_minutes"] == target and
                    type(data.get("effective_minutes")) is int and data["effective_minutes"] == target - base > 0 and
                    int(after["signed_remaining_ns"]) > 0 and
                    after["timer_enabled"] and end["timer_enabled"] and
                    int(after["monotonic_ns"]) - int(before["monotonic_ns"]) <= 10_000_000_000 and
                    int(end["monotonic_ns"]) - int(after["monotonic_ns"]) >= MINUTE_NS and
                    abs(int(end["total_ns"]) - int(after["total_ns"]) -
                        (int(end["monotonic_ns"]) - int(after["monotonic_ns"]))) <= PAIRING_NS,
                    "grant_recovery: excess usage, actual award or usable recovery not proved")
        elif case == "direct_cycle":
            verify_direct_cycle(values, data)
        elif case == "proxy_cycle":
            expected = ("unlimited", "limit", "blocked", "limit", "unlimited")
            require(all(v["pctl_mode"] == mode and v["configured_minutes"] == (1440 if mode == "limit" else 0) and
                        v["business_mode"] == "unlimited" and v["business_minutes"] == 0
                        for v, mode in zip(values, expected)), "proxy_cycle: unlimited/proxy/BLOCKED/restoration mismatch")
            require(values[1]["timer_enabled"] and values[3]["timer_enabled"], "proxy_cycle: proxy timer unconfirmed")
            require(values[2]["start_timer_calls"] == values[3]["start_timer_calls"],
                    "proxy_cycle: automatic rest recovery introduced 1451")
        else:
            require([set(v["restriction_reasons"]) for v in values] == [{"eye_care", "dock"}, {"dock"}, set()] and
                    [v["pctl_mode"] for v in values] == ["blocked", "blocked", "unlimited" if direct else "limit"] and
                    values[-1]["configured_minutes"] == (0 if direct else 1440) and
                    (direct or values[-1]["timer_enabled"]) and
                    all(v["business_mode"] == "unlimited" and v["business_minutes"] == 0 for v in values) and
                    len({v["start_timer_calls"] for v in values}) == 1,
                    "reason_handover: remaining reason released or automatic 1451 introduced")
        evidence.append({"case": case, "path": str(resolved), "sha256": binding["sha256"], "raw_sources": raw_bindings})
    if direct:
        restored_rows = []
        for name in ("before_raw_source", "after_raw_source"):
            key = json.dumps(restoration.get(name), sort_keys=True)
            require(key in paired_raw, "direct restoration is not paired to an audited endpoint")
            restored_rows.append(paired_raw[key])
        require(restored_rows[0]["settings_hex"].lower() == restoration["settings_before_hex"].lower() and
                restored_rows[1]["settings_hex"].lower() == restoration["settings_restored_hex"].lower() and
                restored_rows[0]["timer_enabled"] is restoration["timer_before"] and
                restored_rows[1]["timer_enabled"] is restoration["timer_after"], "direct raw does not prove exact restoration")
    return {"contract": contract, "path": str(path.resolve()), "sha256": sha256_file(path), "evidence": evidence}


def template(manifest: dict, standard_sha256: str, components: dict[str, str],
             model: str, hos: str, atmosphere: str) -> dict:
    contract = manifest.get("usage_contract")
    cases = contract_cases(contract)
    result = {"schema_version": 1, "report_status": "draft", "usage_contract": contract,
            "subject": {key: manifest[key] for key in ("commit", "release_id")},
            "standard_zip_sha256": standard_sha256, "release_components": components,
            "environment": {"model": model, "hos": hos, "atmosphere": atmosphere},
            "observations": {key: None for key in (DIRECT_OBSERVATIONS if contract == DIRECT_CONTRACT else OBSERVATIONS)},
            "attestation": {"operator": None, "signed_at": None},
            "restoration": {"settings_before_hex": None, "settings_restored_hex": None,
                            "timer_before": None, "timer_after": None, "unexpected_raw_offsets": None},
            "cases": {case: {"path": f"{case}.json", "sha256": None} for case in cases}}
    if contract == DIRECT_CONTRACT:
        result["restoration"].update(before_raw_source=None, after_raw_source=None)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a hash-bound PCTL1952 qualification draft; never certify or operate a device.")
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--model", default="mariko-oled")
    parser.add_argument("--hos", default="22.5.0")
    parser.add_argument("--atmosphere", default="1.11.2")
    args = parser.parse_args()
    require(not args.output.exists(), "output already exists; preserve existing evidence")
    version = read_playwise_version(Path(__file__).resolve().parents[1])
    archive_path = args.packages / f"playwise-{version}.zip"
    with zipfile.ZipFile(archive_path) as archive:
        manifest = json.loads(archive.read("switch/playwise/build.json"))
        require(manifest.get("usage_contract") in SUPPORTED_CONTRACTS, "candidate does not declare a supported usage contract")
        components = {name: hashlib.sha256(archive.read(name)).hexdigest() for name in COMPONENTS}
    draft = template(manifest, sha256_file(archive_path), components, args.model, args.hos, args.atmosphere)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(draft, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"DRAFT (not qualified): {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (UsageQualificationError, OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        raise SystemExit(f"FAIL: {exc}") from exc
