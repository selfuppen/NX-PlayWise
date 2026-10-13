"""Read-only reconstruction helpers. A derived timeline is a candidate, not truth."""
from __future__ import annotations

from collections import defaultdict


def unique_events(events: list[dict]) -> list[dict]:
    seen: set[tuple] = set()
    result = []
    for event in events:
        key = (event.get("index"), event.get("raw_hex"))
        if key not in seen:
            seen.add(key)
            result.append(event)
    return sorted(result, key=lambda e: int(e["index"]))


def focus_window(events: list[dict], start: int, end: int, *, time_field: str = "event_steady_s") -> dict:
    """Union of event-derived focus spans, using steady seconds, never CPU uptime.

    Power event values deliberately remain uninterpreted. Incomplete event
    retention, logging policy and stale open tails still need separate evidence.
    """
    if end < start:
        return {"available": False, "reason": "clock_reversed"}
    focused: set[str] = set()
    totals: defaultdict[str, int] = defaultdict(int)
    previous = start
    union = 0
    power = []
    reversals = 0
    previous_event = -1
    initialized = False
    for event in unique_events(events):
        t = int(event.get(time_field, "0"))
        if t <= 0:
            continue
        if t < previous_event:
            focused.clear()
            reversals += 1
        previous_event = t
        if t > end:
            break
        if t >= start:
            elapsed = max(0, t - previous)
            if focused:
                union += elapsed
                for program in focused:
                    totals[program] += elapsed
            previous = t
            if event.get("type") == 2:
                power.append({"steady_s": t, "raw_value": event.get("power_value")})
        if event.get("type") == 0:
            program = event.get("program_id")
            kind = event.get("event_type")
            if kind == 2 and program:
                focused.add(program)
                initialized = True
            elif kind in (1, 3, 4, 5, 6):
                focused.discard(program)
    # This tail is explicit: no final focus event is assumed to exist.
    tail = max(0, end - previous) if focused else 0
    if focused:
        union += tail
        for program in focused:
            totals[program] += tail
    return {"available": initialized, "candidate_focus_union_s": union,
            "by_program_s": dict(totals), "open_tail_s": tail,
            "power_events": power, "steady_reversals": reversals,
            "semantics_confirmed": False, "time_field": time_field}


def focus_for_samples(events: list[dict], before: dict, after: dict) -> dict:
    """Only compare User timestamps when wall/steady endpoints agree.

    PDM raw steady timestamps can have a different epoch from libnx shared
    memory. An unchecked open tail would otherwise cover every test window.
    """
    a, b = int(before["unix_s"]), int(after["unix_s"])
    sa, sb = int(before["steady_s"]), int(after["steady_s"])
    if before.get("steady_source") != after.get("steady_source") or abs((b-a)-(sb-sa)) > 2:
        return {"available": False, "reason": "clock_discontinuity", "semantics_confirmed": False}
    result = focus_window(events, a, b, time_field="event_user_s")
    offsets = sorted({(int(e["event_user_s"])-int(e["event_steady_s"]))-(int(e["unix_s"])-int(e["steady_s"]))
                      for e in events if a-30 <= int(e.get("event_user_s", "0")) <= b+30
                      and all(k in e for k in ("event_steady_s", "unix_s", "steady_s"))})
    result["observed_event_to_sample_steady_offsets_s"] = offsets
    current = {s.get("application_id") for s in (before, after)}
    # libnx AppletId_application is 0x01; 5 is not the application tag.
    applications = {e.get("program_id") for e in events if e.get("type") == 0 and e.get("applet_id") == 1}
    result["unclosed_other_applications"] = sorted(p for p in result.get("by_program_s", {}) if p in applications and p not in current)
    result["note"] = "按 User 时间轴映射当前稳定窗口；原始 steady 不直接跨域相减。历史改时、开放尾段和睡眠口径仍未验证。"
    return result


def stats_window(stats: list[dict], start: int, end: int, max_lag: int = 30) -> dict:
    groups: defaultdict[str, list[dict]] = defaultdict(list)
    for sample in stats:
        if sample.get("result") == 0 and sample.get("account_ordinal") == 0:
            groups[sample["program_id"]].append(sample)
    deltas = {}
    for program, values in groups.items():
        values.sort(key=lambda s: int(s["steady_s"]))
        a = [s for s in values if start-max_lag <= int(s["steady_s"]) <= start]
        b = [s for s in values if end <= int(s["steady_s"]) <= end+max_lag]
        if a and b:
            delta = int(b[0]["playtime_ns"]) - int(a[-1]["playtime_ns"])
            deltas[program] = {"delta_ns": str(delta), "before_steady_s": a[-1]["steady_s"], "after_steady_s": b[0]["steady_s"], "counter_reset": delta < 0}
    return {"available": bool(deltas), "programs": deltas,
            "note": "累计统计更新可能延迟；边界允许30秒查询窗口，不能当作逐秒真值"}
