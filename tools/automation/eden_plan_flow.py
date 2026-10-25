"""X01/X04 through normal UI, with independently repeatable evidence checks."""
from __future__ import annotations

from datetime import datetime
from pathlib import Path
import shutil

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import digest, read, require, save

PLAN_SCENARIOS = ("setup", "calendar_import", "calendar_apply", "bedtime_week",
                  "bedtime_calendar", "bedtime_scheduled", "plans_reopen")
SOURCES = ("weekly", "today_override", "scheduled_override", "statutory_holiday", "makeup_workday")


def prepare_plans(app: Path, output: Path, rules: dict) -> None:
    """Only pre-launch fixtures; no active calendar, authentication or results."""
    rules.update(version=2, holiday_enabled=True, holiday_mode="limit", holiday_minutes=90,
                 scheduled_override_enabled=False, bedtime_enabled=False, bedtime_calendar_enabled=False,
                 bedtime_week=[{"enabled": True, "start_minute": 1200, "end_minute": 360} for _ in range(7)],
                 bedtime_holiday_mode="inherit", bedtime_makeup_mode="inherit", bedtime_scheduled_present=False)
    year = datetime.now().year
    calendar = {"format_version": 1, "region_id": "EDENX", "region_name": "Eden X01 test calendar", "year": year,
                "holidays": [{"name": "X01 test holiday", "from": f"{year}-01-01", "to": f"{year}-12-31"}], "workdays": []}
    save(app / f"calendar-import/EDENX-{year}.json", calendar)
    save(output / "fixtures/calendar-import.json", calendar)


def policy(rules: dict) -> dict:
    return {key: value for key, value in rules.items() if key.startswith("bedtime_")}


def verify_partition(before: dict, after: dict, section: int) -> None:
    allowed = ({"bedtime_week"}, {"bedtime_calendar_enabled"} | {k for k in after if k.startswith(("bedtime_holiday_", "bedtime_makeup_"))},
               {k for k in after if k.startswith("bedtime_scheduled_")})[section]
    require(any(before.get(k) != after.get(k) for k in allowed), "saved section did not change")
    require(all(after.get(k) == v for k, v in before.items() if k not in allowed), "save changed another partition")


def verify_forecast(status: dict, ui: dict, imported: bool) -> None:
    state = status["state"]
    require(state["calendar_option_id"] == ("EDENX" if imported else "builtin-cn"), "wrong active calendar source")
    require(state["calendar_source"] == ("user_import" if imported else "builtin"), "wrong calendar origin")
    forecast = state["forecast"]
    require(len(forecast) == 7 and len(ui.get("forecast", [])) == 7, "seven-day preview missing")
    for i, row in enumerate(forecast):
        require(row["day_index"] == state["day_index"] + i, "forecast date is not consecutive")
        require(ui["forecast"][i] == [row["day_index"], row["mode"], row["minutes"], SOURCES.index(row["rule_source"]), row["calendar_covered"]],
                "UI forecast differs from backend")
        if imported:
            require(row["rule_source"] == "statutory_holiday" and row["minutes"] == 90 and row["mode"] == 1 and
                    row["calendar_covered"] is True, "imported calendar did not supply actual allowance")
    if imported:
        require(state["rule_source"] == "statutory_holiday", "today/date override masks calendar")


def verify_windows(ui: dict, rules: dict) -> None:
    expected = [[int(w["enabled"]), w["start_minute"], w["end_minute"]] for w in rules["bedtime_week"]]
    for name in ("holiday", "makeup", "scheduled"):
        expected.append([("inherit", "disabled", "custom").index(rules[f"bedtime_{name}_mode"]),
                         rules[f"bedtime_{name}_start_minute"], rules[f"bedtime_{name}_end_minute"]])
    require(ui.get("bedtime_windows") == expected and ui.get("bedtime_dirty") is False,
            "UI saved windows differ from persisted policy")
    for suffix, key in (("enabled", "bedtime_enabled"), ("calendar", "bedtime_calendar_enabled"),
                        ("present", "bedtime_scheduled_present"), ("start_day", "bedtime_scheduled_start_day_index"),
                        ("end_day", "bedtime_scheduled_end_day_index")):
        require(ui.get(f"bedtime_draft_{suffix}") == rules[key], "UI saved date/switch differs")


def verify_plans(output: Path) -> None:
    language = read(output / 'identity.json').get('plans_language', 'system')
    require(language in ('system', 'en'), 'unknown plan language selection')
    if language == 'en':
        require(read(output / 'app-final/config.json')['ui_language'] == 'en', 'explicit English UI did not persist')
    for name in PLAN_SCENARIOS[1:]:
        value = read(output / f"assertions/{name}.json")
        require(value.get("passed") is True and value.get("checks"), f"missing plan assertions: {name}")
    imported = read(output / "plan-evidence/import.json")
    require(imported["active_before"] == imported["active_after"] and imported["rules_before"] == imported["rules_after"],
            "import changed active selection or rules")
    for name, active in (("before", False), ("imported", False), ("applied", True), ("reopened", True)):
        verify_forecast(read(output / f"plan-status/{name}.json"), read(output / f"checkpoints/calendar-{name}.json")["ui"], active)
    require(read(output / "plan-status/before.json")["state"]["forecast"] ==
            read(output / "plan-status/imported.json")["state"]["forecast"], "import changed forecast")
    calendar_hash = digest(output / "fixtures/calendar-import.json")
    active = read(output / "app-final/calendars/active.json")
    require(active["option_id"] == "EDENX" and len(active["entries"]) == 1 and active["entries"][0]["sha256"] == calendar_hash,
            "applied calendar/hash not persisted")
    entry = active["entries"][0]
    require(digest(output / f"app-final/calendars/library/EDENX-{entry['year']}-{calendar_hash}.json") == calendar_hash,
            "library content hash differs")
    rows = [read(p) for p in (output / "receipts").glob("*.json")]
    for kind, count in (("import_holiday_calendar", 1), ("activate_holiday_calendar", 1), ("set_bedtime_policy", 3)):
        matched = [r for r in rows if r.get("type") == kind]
        require(len(matched) == count and all(r["status"] == "ok" for r in matched), f"unique business receipts differ: {kind}")
    requests = [read(p) for p in (output / "app-final/support").glob("automation-request-*.json")]
    plan_receipts = {r["request_id"]: r for r in rows if r["type"] in
                     ("import_holiday_calendar", "activate_holiday_calendar", "set_bedtime_policy")}
    require(len(requests) == 5 and {r["request_id"] for r in requests} == set(plan_receipts), "raw request evidence differs")
    for request in requests:
        require(request["type"] == plan_receipts[request["request_id"]]["type"], "request/receipt type differs")
        if request["type"] == "import_holiday_calendar":
            require(request["payload"]["sha256"] == calendar_hash, "import request hash differs")
        elif request["type"] == "activate_holiday_calendar":
            require(request["payload"]["option_id"] == "EDENX", "applied request selected another region")
    for section, name in enumerate(("week", "calendar", "scheduled")):
        before = read(output / f"plan-evidence/{name}-before.json")
        after = read(output / f"plan-evidence/{name}-saved.json")
        verify_partition(before, after, section)
        verify_windows(read(output / f"checkpoints/bedtime_{name}.json")["ui"], after)
        window_ui = read(output / f"checkpoints/bedtime-{section}-window-save.json")["ui"]
        expected = ((1290, 420), (1350, 450), (1380, 480))[section]
        actual = after["bedtime_week"][window_ui["bedtime_editor_day"]] if section == 0 else {
            "start_minute": after[f"bedtime_{'holiday' if section == 1 else 'scheduled'}_start_minute"],
            "end_minute": after[f"bedtime_{'holiday' if section == 1 else 'scheduled'}_end_minute"]}
        require((actual["start_minute"], actual["end_minute"]) == expected, "saved preset differs from edited UI")
        matching = [r for r in requests if r["type"] == "set_bedtime_policy" and
                    r["payload"]["bedtime_days"] == after["bedtime_week"] and
                    all(r["payload"].get(k) == v for k, v in after.items() if k.startswith("bedtime_") and
                        k not in ("bedtime_week", "bedtime_confirmation_version", "bedtime_official_setting_confirmed_at",
                                  "bedtime_confirmed_environment", "bedtime_overlay_risk_accepted"))]
        require(len(matching) == 1 and matching[0]["payload"]["activation"] == "immediate", "saved policy/request mismatch")
        cancelled = read(output / f"plan-evidence/{name}-navigation.json")
        require(cancelled["rules_before"] == cancelled["rules_after_cancel"] == cancelled["rules_after_discard"] and
                cancelled["write_ids_before"] == cancelled["write_ids_after"], "draft navigation persisted a write")
    final = read(output / "app-final/rules.json")
    require(policy(final) == policy(read(output / "plan-evidence/scheduled-saved.json")), "reopen lost saved bedtime policy")
    for name in ("week", "calendar", "scheduled"):
        verify_windows(read(output / f"checkpoints/bedtime-{name}-reopened.json")["ui"], final)
    require(not final["today_override_present"] and not final["scheduled_override_enabled"], "quota override masks source")
    require(final["bedtime_scheduled_present"] is True and final["bedtime_scheduled_mode"] == "custom" and
            final["bedtime_scheduled_start_day_index"] == read(output / "plan-status/reopened.json")["state"]["day_index"] ==
            final["bedtime_scheduled_end_day_index"], "specified date does not match today's date")


class PlanFlow(MainFlow):
    def setup(self) -> None:
        super().setup()
        if self.identity.get('plans_language', 'system') != 'en':
            return
        self.page(3)
        self.action(target='card_3')
        self.wait_ui(overlay=37, overlay_selection=0)
        self.action(button='Down')
        self.wait_ui(overlay_selection=2)
        self.action(button='Right')
        self.wait_ui(overlay_selection=3)
        self.action(button='A')
        self.wait_ui(overlay=0)
        require(read(self.app / 'config.json')['ui_language'] == 'en', 'explicit English UI selection did not persist')

    def archive_plan_status(self, name: str, imported: bool) -> dict:
        self.page(0)
        result = self.refresh()
        save(self.output / f"plan-status/{name}.json", result)
        self.capture(f"calendar-{name}")
        verify_forecast(result, read(self.output / f"checkpoints/calendar-{name}.json")["ui"], imported)
        if imported:
            self.action(button="B")
            self.wait_ui(view=0, overlay=0)
            self.capture(f"calendar-{name}-seven-days")
            verify_forecast(result, read(self.output / f"checkpoints/calendar-{name}-seven-days.json")["ui"], True)
            self.parent()
        return result

    def active_bytes(self) -> str | None:
        path = self.app / "calendars/active.json"
        return path.read_text(encoding="utf-8") if path.is_file() else None

    def calendar_import(self) -> None:
        # Setup deliberately creates today's quota; remove it through the UI.
        self.today_action(3, "restore_today_policy")
        before = self.archive_plan_status("before", False)
        require(before["state"]["forecast"][0]["minutes"] != 90, "calendar fixture would not change today's preview")
        rules = read(self.app / "rules.json")
        require(not rules["scheduled_override_enabled"], "date override masks calendar")
        active = self.active_bytes()
        self.plan(1, page=2)
        self.action(target="holiday_manage")
        self.wait_ui(overlay=40, calendar_tab=0)
        self.action(button="R")
        self.wait_ui(overlay=40, calendar_tab=1, calendar_count=1, calendar_selected=0)
        before_ids = self.ids()
        self.action(button="A")
        self.submitted("import_holiday_calendar", before_ids)
        self.wait_ui(overlay=40)
        require(self.active_bytes() == active and read(self.app / "rules.json") == rules, "import applied calendar")
        save(self.output / "plan-evidence/import.json", {"active_before": active, "active_after": self.active_bytes(),
             "rules_before": rules, "rules_after": read(self.app / "rules.json")})
        self.action(button="B")
        self.action(button="B")
        self.wait_ui(plan_page=0, overlay=0)
        after = self.archive_plan_status("imported", False)
        require(before["state"]["forecast"] == after["state"]["forecast"], "import changed forecast")
        self.checked("calendar_import", ["normal UI import has one receipt", "active and quota rules unchanged", "seven-day forecast unchanged"])

    def calendar_apply(self) -> None:
        self.plan(1, page=2)
        self.action(target="holiday_manage")
        self.wait_ui(overlay=40, calendar_tab=0, calendar_count=2)
        self.action(button="Down")
        self.wait_ui(calendar_selected=1)
        before = self.ids()
        self.action(button="A")
        self.wait_ui(overlay=3, confirm_hold_required=True)
        self.submitted("activate_holiday_calendar", before)
        self.wait_ui(overlay=40)
        self.action(button="B")
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.archive_plan_status("applied", True)
        self.checked("calendar_apply", ["selected imported region and held normal confirmation", "active hash and seven-day actual source match imported library"])

    def write_ids(self) -> list[str]:
        return sorted(p.stem for p in (self.app / "results").glob("*.json") if read(p).get("type") != "status")

    def edit_window(self, section: int, phase: str) -> None:
        field = (0, 1, 3)[section]
        self.action(target=f"bedtime_field_{field}")
        self.wait_ui(overlay=31 if section == 0 else 32)
        if section:
            self.action(button="A")
            self.action(button="A")
        self.action(target=f"bedtime_preset_{(1, 3, 4)[section]}")
        self.wait_ui(bedtime_dirty=True)
        self.capture(f"bedtime-{section}-window-{phase}")
        self.action(button="Plus")
        self.wait_ui(overlay=0, bedtime_dirty=True)

    def navigation_cancel_discard(self, section: int, name: str) -> None:
        before = read(self.app / "rules.json")
        ids = self.write_ids()
        self.edit_window(section, "discard")
        self.action(button="R")
        self.wait_ui(overlay=33, bedtime_section=section, bedtime_dirty=True)
        self.action(button="B")
        self.wait_ui(overlay=0, bedtime_section=section, bedtime_dirty=True)
        cancelled = read(self.app / "rules.json")
        self.capture(f"bedtime-{name}-cancel")
        self.action(button="R")
        self.wait_ui(overlay=33)
        self.action(button="X")
        self.wait_ui(overlay=0, bedtime_section=(section + 1) % 3, bedtime_dirty=False)
        save(self.output / f"plan-evidence/{name}-navigation.json", {"rules_before": before, "rules_after_cancel": cancelled,
             "rules_after_discard": read(self.app / "rules.json"), "write_ids_before": ids, "write_ids_after": self.write_ids()})
        require(read(self.app / "rules.json") == cancelled == before and self.write_ids() == ids, "navigation saved a draft")
        self.action(button="L")
        self.wait_ui(bedtime_section=section, overlay=0)

    def save_partition(self, section: int, name: str) -> None:
        self.navigation_cancel_discard(section, name)
        before = read(self.app / "rules.json")
        save(self.output / f"plan-evidence/{name}-before.json", before)
        if section == 2:
            self.action(button="A")  # Current row 0; normal pad path initializes today's date.
            self.wait_ui(bedtime_draft_present=True)
        self.edit_window(section, "save")
        before_ids = self.ids()
        if section == 2:
            self.action(button="R")
            self.wait_ui(overlay=33)
            self.action(button="A")  # Save only current partition and complete pending navigation.
        else:
            self.action(button="Plus")
        self.submitted("set_bedtime_policy", before_ids)
        self.wait_ui(overlay=0, bedtime_dirty=False, bedtime_section=0 if section == 2 else section)
        after = read(self.app / "rules.json")
        verify_partition(before, after, section)
        save(self.output / f"plan-evidence/{name}-saved.json", after)
        self.checked(f"bedtime_{name}", ["cross-page cancel retains draft without saving", "discard switches without write", "save changes only current partition"])

    def bedtime_week(self) -> None:
        self.plan(3, page=3)
        self.wait_ui(bedtime_section=0, bedtime_dirty=False)
        self.save_partition(0, "week")

    def bedtime_calendar(self) -> None:
        self.action(button="R")
        self.wait_ui(bedtime_section=1)
        self.save_partition(1, "calendar")

    def bedtime_scheduled(self) -> None:
        self.action(button="R")
        self.wait_ui(bedtime_section=2)
        self.save_partition(2, "scheduled")
        self.action(button="B")
        self.wait_ui(plan_page=0, overlay=0)

    def plans_reopen(self) -> None:
        before = read(self.app / "rules.json")
        active = self.active_bytes()
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0)
        require(read(self.app / "rules.json") == before and self.active_bytes() == active, "reopen changed saved plans")
        self.archive_plan_status("reopened", True)
        self.plan(3, page=3)
        self.wait_ui(bedtime_section=0, bedtime_dirty=False)
        self.capture("bedtime-week-reopened")
        self.action(button="R")
        self.wait_ui(bedtime_section=1, bedtime_dirty=False)
        self.capture("bedtime-calendar-reopened")
        self.action(button="R")
        self.wait_ui(bedtime_section=2, bedtime_dirty=False, bedtime_draft_present=True)
        self.capture("bedtime-scheduled-reopened")
        for name in ("calendars",):
            shutil.copytree(self.app / name, self.output / "plan-evidence" / name)
        self.checked("plans_reopen", ["new instance retains active calendar and exact saved rules", "all three saved sections visible after normal parent PIN"])
