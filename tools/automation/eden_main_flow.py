"""Release-time end-to-end journeys. Inputs use the real NRO UI, never queued fixtures."""
from __future__ import annotations

from pathlib import Path
import time

from tools.automation.eden_runner import read, require, save, digest
from tools.lib.ptc_token_v2 import encode_token, tier_for_minutes

MAIN_SCENARIOS = ("code_cancel_journey", "plan_journey", "priority_journey", "buffer_journey", "eye_journey",
                  "bedtime_journey", "dock_journey", "backup_journey", "preferences_journey", "business_reopen")


def verify_main(output: Path) -> None:
    """Recheck serialized business assertions without starting an emulator."""
    for name in MAIN_SCENARIOS:
        value = read(output / f"assertions/{name}.json")
        require(value.get("passed") is True and value.get("checks"), f"missing main-flow assertions: {name}")
    plan = read(output / "assertions/plan_journey.json")
    rules = read(output / "app-final/rules.json")
    require(rules["week"][plan["weekday"]] == {"mode": "limit", "minutes": 180}, "restored week differs")
    require(rules["daily_buffer_minutes"] == 5 and not rules["eye_care_enabled"] and
            not rules["bedtime_enabled"] and not rules["force_docked"] and
            not rules["undocked_limit_enabled"], "main-flow final policies differ")
    require(read(output / "app-final/state.json")["buffer_claimed"] is True, "claimed buffer was refunded")
    config = read(output / "app-final/config.json")
    require(config["theme"] == "dark" and config["ui_language"] == "en", "UI preferences did not survive reopen")
    for name, expected in {
        "plan_journey": {"today_minutes": 180},
        "priority_journey": {"today_minutes": 180},
        "buffer_journey": {"today_minutes": 200, "daily_buffer_claimed": True, "daily_buffer_available": False},
        "business_reopen": {"view": 0, "today_minutes": 180, "daily_buffer_claimed": True},
        "daily-and-dock": {"daily_restriction_active": True, "dock_restriction_active": True, "restricted_now": 1},
        "dock-on-unlimited": {"daily_restriction_active": False, "dock_restriction_active": True, "restricted_now": 1},
        "dock-remains-after-daily-clear": {"daily_restriction_active": False, "dock_restriction_active": True, "restricted_now": 1},
        "eye-natural-rest": {"eye_resting": True},
        "eye-natural-recovery": {"eye_resting": False, "restricted_now": 0},
    }.items():
        ui = read(output / f"checkpoints/{name}.json")["ui"]
        require(all(ui.get(k) == v for k, v in expected.items()), f"main-flow checkpoint differs: {name}")
    reopen = read(output / "assertions/business_reopen.json")
    require(digest(output / "app-final/ledger/used_nonces.jsonl") == reopen["ledger_sha256"],
            "business reopen lost consumed nonce")
    rows = [read(p) for p in (output / "receipts").glob("*.json")]
    for kind in ("set_weekly_template", "set_scheduled_override", "set_today_limit", "restore_today_policy",
                 "add_today_minutes", "set_autonomy_policy", "claim_daily_buffer", "set_eye_care_policy",
                 "set_bedtime_policy", "skip_bedtime", "set_dock_policy", "disable_today_limit",
                 "waive_dock_policy_today", "create_config_backup", "restore_config_backup", "set_holiday_policy"):
        require(any(r.get("type") == kind and r.get("status") == "ok" for r in rows), f"missing receipt: {kind}")
    require(sum(r.get("type") == "claim_daily_buffer" for r in rows) == 1, "buffer duplicated")


class MainFlow:
    def code_cancel_journey(self) -> None:
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        rules = (self.app / "rules.json").read_bytes()
        ledger = (self.app / "ledger/used_nonces.jsonl").read_bytes()
        code = encode_token(tier_for_minutes(30), 1, "eden-switch", "playwise-eden-test-secret-00000001", self.code_day)
        before = self.type_code(code)
        require(self.wait_receipt("preview_offline_code", before)["status"] == "ok", "unused code preview rejected")
        self.wait_ui(overlay=3)
        before = self.ids()
        self.action(button="B")
        self.wait_ui(view=0, overlay=0)
        require((self.app / "rules.json").read_bytes() == rules and
                (self.app / "ledger/used_nonces.jsonl").read_bytes() == ledger, "cancel consumed preview")
        require(not any(read(p).get("type") == "offline_code" for p in (self.app / "results").glob("*.json")
                        if p.stem not in before), "cancel submitted redemption")
        self.checked("code_cancel_journey", ["unused code preview succeeds", "cancel leaves allowance and nonce unchanged"])

    def checked(self, name: str, checks: list[str], **values: object) -> None:
        save(self.output / f"assertions/{name}.json", {"passed": True, "checks": checks, **values})

    def parent(self) -> None:
        self.wait_ui(overlay=0, waiting=False)
        if self.observe()["ui"]["view"] == 0:
            self.pin_open()
            self.enter_pin("110")
        self.wait_ui(view=1, overlay=0, waiting=False)

    def page(self, index: int) -> None:
        self.parent()
        self.action(target=f"tab_{index}")
        self.wait_ui(parent_page=index, overlay=0, waiting=False)

    def plan(self, card: int, page: int | None = None, overlay: int = 0) -> dict:
        self.page(1)
        self.action(target=f"card_{card}")
        expected = {"overlay": overlay, "waiting": False}
        if page is not None:
            expected["plan_page"] = page
        return self.wait_ui(**expected)

    def confirm(self) -> None:
        value = self.wait_ui(overlay=3, waiting=False)["ui"]
        self.action(button="A", hold_ms=1400 if value["confirm_hold_required"] else 200)

    def submitted(self, kind: str, before: set[str]) -> dict:
        # Requirements can precede a second, held confirmation. Never repeat
        # the business action; only advance an explicitly visible dialog.
        for _ in range(3):
            self.wait_ui(waiting=False)
            if self.observe()["ui"]["overlay"] != 3:
                break
            self.confirm()
        result = self.wait_receipt(kind, before)
        require(result["status"] == "ok", f"{kind} explicitly rejected")
        self.wait_ui(waiting=False)
        return result

    def refresh(self, **expected: object) -> dict:
        self.wait_ui(overlay=0, waiting=False)
        before = self.ids()
        self.action(button="Y")
        result = self.wait_receipt("status", before)
        require(result["status"] == "ok", "fresh status failed")
        self.wait_ui(overlay=0, waiting=False, **expected)
        return result

    def duration(self, minutes: int) -> None:
        self.wait_ui(overlay=20)
        for field, number in ((0, minutes // 60), (1, minutes % 60)):
            self.action(target=f"duration_{field}")
            for digit in str(number):
                self.action(target=f"code_{digit}")
        self.action(button="Plus")

    def today(self, minutes: int) -> None:
        self.page(0)
        self.action(target="card_0")
        self.wait_ui(overlay=20)
        self.action(button="L")  # Explicit limited mode, including after unlimited.
        before = self.ids()
        self.duration(minutes)
        self.submitted("set_today_limit", before)
        self.refresh(today_minutes=minutes)

    def today_action(self, card: int, kind: str) -> None:
        self.page(0)
        before = self.ids()
        self.action(target=f"card_{card}")
        self.submitted(kind, before)

    def weekly(self, minutes: int) -> int:
        value = self.plan(2, page=1)
        weekday = value["ui"]["editor_index"]
        self.action(target=f"week_{(weekday + 6) % 7}")
        self.duration(minutes)
        self.wait_ui(overlay=0, weekly_dirty=True)
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_weekly_template", before)
        self.wait_ui(overlay=0, weekly_dirty=False)
        require(read(self.app / "rules.json")["week"][weekday] == {"mode": "limit", "minutes": minutes},
                "saved week differs from UI edit")
        self.action(button="B")
        self.wait_ui(plan_page=0, overlay=0)
        return weekday

    def plan_journey(self) -> None:
        self.wait_ui(overlay=0)
        self.today_action(3, "restore_today_policy")
        before = (self.app / "rules.json").read_bytes()
        self.plan(2, page=1)
        self.action(button="X")
        self.wait_ui(weekly_dirty=True)
        self.action(button="ZL")
        self.wait_ui(weekly_dirty=False)
        require((self.app / "rules.json").read_bytes() == before, "discard persisted a draft")
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.weekday = self.weekly(180)
        self.page(0)
        self.refresh(today_minutes=180)
        self.checked("plan_journey", ["discard leaves persisted week unchanged", "save reaches backend",
                                     "today projects saved 180 minutes"], weekday=self.weekday)

    def priority_journey(self) -> None:
        self.holiday(True)
        self.page(0)
        result = self.refresh(today_minutes=90)
        require(result["state"]["rule_source"] == "statutory_holiday", "fixture holiday did not override week")
        self.plan(0, overlay=24)
        self.action(button="A")  # Enable today's default 60-minute date range.
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_scheduled_override", before)
        self.wait_ui(overlay=0)
        self.page(0)
        self.refresh(today_minutes=60)
        self.today(150)
        require(read(self.app / "rules.json")["scheduled_override_enabled"] is True, "today destroyed date plan")
        self.today_action(3, "restore_today_policy")
        self.refresh(today_minutes=60)
        self.plan(0, overlay=24)
        self.action(button="A")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_scheduled_override", before)
        self.wait_ui(overlay=0)
        self.page(0)
        self.refresh(today_minutes=90)
        self.holiday(False)
        self.page(0)
        self.refresh(today_minutes=180)
        self.checked("priority_journey", ["holiday overrides week", "date range overrides holiday", "today overrides date range",
                                         "clearing each layer restores underlying policy"])

    def holiday(self, enabled: bool) -> None:
        self.plan(1, page=2)
        self.action(button="A")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_holiday_policy", before)
        require(read(self.app / "rules.json")["holiday_enabled"] is enabled, "holiday toggle did not persist")
        self.action(button="B")
        self.wait_ui(plan_page=0)

    def buffer_journey(self) -> None:
        self.page(0)
        self.action(target="card_1")
        self.wait_ui(overlay=30)
        self.action(button="A")  # 15-minute preset.
        before = self.ids()
        self.confirm()
        self.submitted("add_today_minutes", before)
        self.refresh(today_minutes=195)
        self.plan(6, overlay=25)
        self.action(button="Right")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_autonomy_policy", before)
        self.wait_ui(overlay=0)
        self.action(button="B")
        self.wait_ui(view=0, overlay=0, daily_buffer_available=True)
        before = self.ids()
        self.action(target="child_buffer")
        self.submitted("claim_daily_buffer", before)
        self.wait_ui(view=0, overlay=0, daily_buffer_claimed=True, daily_buffer_available=False, today_minutes=200)
        rules = (self.app / "rules.json").read_bytes()
        before = self.ids()
        self.action(target="child_buffer")  # Explicit attempt through the disabled product entry.
        self.wait_ui(daily_buffer_available=False)
        require((self.app / "rules.json").read_bytes() == rules, "disabled second claim changed allowance")
        require(not any(read(p).get("type") == "claim_daily_buffer"
                        for p in (self.app / "results").glob("*.json") if p.stem not in before),
                "disabled second claim submitted a transaction")
        self.checked("buffer_journey", ["quick grant 180 to 195", "child claims five minutes once",
                                       "claimed entry is disabled without another request"])

    def wait_policy(self, seconds: int, **expected: object) -> None:
        end = min(self.deadline, time.monotonic() + seconds)
        while time.monotonic() < end:
            self.bounded()
            if all(self.observe()["ui"].get(k) == v for k, v in expected.items()):
                return
            time.sleep(2)
            self.refresh()
        require(False, f"natural simulated policy deadline exceeded: {expected}")

    def eye_journey(self) -> None:
        self.plan(4, page=4)
        self.capture("eye-policy-guide")
        self.action(target="eye_master")
        before = self.ids()
        self.action(button="Plus", hold_ms=600)
        self.submitted("set_eye_care_policy", before)
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.page(0)
        # Enable establishes its baseline on a scheduler minute. The one-minute
        # threshold may then be seen just below 60 s and need the next minute.
        self.wait_policy(205, eye_resting=True)
        self.capture("eye-natural-rest")
        self.wait_policy(85, eye_resting=False, restricted_now=0)
        self.capture("eye-natural-recovery")
        self.plan(4, page=4)
        self.action(target="eye_master")
        before = self.ids()
        self.action(button="Plus", hold_ms=600)
        self.submitted("set_eye_care_policy", before)
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.checked("eye_journey", ["saved one-minute cycle naturally enters rest",
                                    "natural deadline restores simulated availability", "disable persists"])

    def bedtime_journey(self) -> None:
        self.plan(3, page=3)
        self.action(target="bedtime_master")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_bedtime_policy", before)
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.page(0)
        # The in-process bridge does not rescan the SD queue. Bedtime entry is
        # enforced by the next natural scheduler minute, not the save receipt.
        self.wait_policy(85, bedtime_active=True, bedtime_skipped=False, restricted_now=1)
        self.refresh(bedtime_active=True, bedtime_skipped=False, restricted_now=1)
        before = self.ids()
        self.action(target="card_4")
        self.wait_ui(overlay=22)
        self.enter_pin("110")
        self.confirm()
        self.submitted("skip_bedtime", before)
        self.refresh(bedtime_skipped=True, restricted_now=0)
        self.plan(3, page=3)
        self.action(target="bedtime_master")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_bedtime_policy", before)
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.checked("bedtime_journey", ["normal UI activates fixture window", "PIN skip clears current simulated limit",
                                        "disable saves without recovery failure"])

    def dock_journey(self) -> None:
        self.today_action(2, "disable_today_limit")
        self.refresh(daily_restriction_active=False, unrestricted_today=1)
        self.plan(5, page=5)
        self.action(target="dock_1")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_dock_policy", before)
        self.wait_ui(overlay=0)
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.page(0)
        self.refresh(daily_restriction_active=False, dock_restriction_active=True, restricted_now=1)
        self.capture("dock-on-unlimited")
        self.today(1)
        self.refresh(daily_restriction_active=True, dock_restriction_active=True, restricted_now=1)
        self.capture("daily-and-dock")
        # The quick card is disabled; the editor can still reach the backend's
        # explicit dock_active rejection. It must not change persisted rules.
        rules = (self.app / "rules.json").read_bytes()
        self.page(0)
        self.action(target="card_0")
        self.wait_ui(overlay=20)
        self.action(button="R")
        before = self.ids()
        self.action(button="Plus")
        self.wait_ui(overlay=3, waiting=False)
        self.confirm()
        denied = self.wait_receipt("disable_today_limit", before)
        require(denied["status"] == "error" and denied.get("error", {}).get("code") == 326,
                "unlimited bypassed active dock policy")
        self.wait_ui(overlay=0, waiting=False)
        require((self.app / "rules.json").read_bytes() == rules, "rejected unlimited changed daily policy")
        self.capture("unlimited-rejected-while-dock")
        self.today(180)
        self.refresh(daily_restriction_active=False, dock_restriction_active=True, restricted_now=1)
        self.capture("dock-remains-after-daily-clear")
        self.plan(5, page=5)
        before = self.ids()
        self.action(target="dock_7")  # Existing Eden-only simulated TV button.
        self.submitted("status", before)
        self.refresh(dock_restriction_active=False, restricted_now=0)
        before = self.ids()
        self.action(target="dock_8")
        self.submitted("status", before)
        self.refresh(dock_restriction_active=True, restricted_now=1)
        before = self.ids()
        self.action(target="dock_4")
        self.wait_ui(overlay=22)
        self.enter_pin("110")
        self.confirm()
        self.submitted("waive_dock_policy_today", before)
        self.refresh(dock_restriction_active=False, restricted_now=0)
        self.action(target="dock_0")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_dock_policy", before)
        self.action(button="B")
        self.wait_ui(plan_page=0)
        self.today_action(3, "restore_today_policy")
        self.checked("dock_journey", ["dock restricts an unlimited daily policy", "daily and dock reasons coexist",
                                     "unlimited is explicitly rejected while dock blocks", "setting daily quota clears daily reason only",
                                     "simulated TV and handheld switch reasons", "PIN waiver and disable restore"])

    def backup_journey(self) -> None:
        self.page(3)
        self.action(target="card_7")
        self.wait_ui(overlay=44)
        before = self.ids()
        self.action(target="backup_13")
        self.wait_ui(overlay=22)
        self.enter_pin("110")
        self.confirm()
        self.submitted("create_config_backup", before)
        self.wait_ui(overlay=44, config_backup_ready=True)
        self.action(button="B")
        self.wait_ui(overlay=0)
        self.weekly(240)
        self.page(3)
        self.action(target="card_7")
        self.wait_ui(overlay=44, config_backup_ready=True)
        self.action(target="backup_17")
        self.action(target="backup_12")
        self.wait_ui(config_groups=0)
        self.action(target="backup_0")
        self.wait_ui(config_groups=1)
        ledger = (self.app / "ledger/used_nonces.jsonl").read_bytes()
        before = self.ids()
        self.action(target="backup_14")
        self.wait_ui(overlay=22)
        self.enter_pin("110")
        self.wait_ui(overlay=3, confirm_hold_required=True)
        self.confirm()
        self.submitted("restore_config_backup", before)
        self.wait_ui(overlay=44)
        rules = read(self.app / "rules.json")
        require(rules["week"][self.weekday]["minutes"] == 180 and rules["daily_buffer_minutes"] == 5,
                "selective restore did not merge only selected week")
        require((self.app / "ledger/used_nonces.jsonl").read_bytes() == ledger, "restore refunded consumed nonce")
        self.action(button="B")
        self.wait_ui(overlay=0)
        self.checked("backup_journey", ["UI creates archive", "week changed after backup", "PIN and hold restore only week",
                                       "buffer policy and consumed nonce survive selective restore"])

    def business_reopen(self) -> None:
        before_rules = read(self.app / "rules.json")
        ledger = digest(self.app / "ledger/used_nonces.jsonl")
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0, daily_buffer_claimed=True, daily_buffer_available=False, today_minutes=180)
        require(read(self.app / "rules.json") == before_rules, "reopen changed saved policies")
        require(digest(self.app / "ledger/used_nonces.jsonl") == ledger, "reopen lost consumed nonce")
        config = read(self.app / "config.json")
        require(config["theme"] == "dark" and config["ui_language"] == "en", "reopen lost preferences")
        self.checked("business_reopen", ["normal close/reopen preserves saved week and restored policies",
                                        "claimed buffer remains unavailable", "nonce ledger remains consumed"], ledger_sha256=ledger)

    def preferences_journey(self) -> None:
        config = self.app / "config.json"
        self.page(3)
        self.action(target="card_1")
        self.wait_ui(overlay=21, overlay_selection=0)
        # Both radio groups preview live but write nothing until A commits them together.
        self.action(button="Right")
        self.wait_ui(overlay_selection=1)
        self.action(button="Down")
        self.wait_ui(overlay_selection=4)
        self.action(button="Right")
        self.wait_ui(overlay_selection=5)
        require(read(config)["theme"] == "system" and read(config)["theme_style"] == "slate",
                "theme preview wrote config before confirm")
        self.action(button="A")
        self.wait_ui(overlay=0)
        saved = read(config)
        require(saved["theme"] == "light" and saved["theme_style"] == "teal",
                "theme mode and style were not saved together")
        self.capture("preference-light")
        self.action(target="card_1")
        self.wait_ui(overlay=21, overlay_selection=1)
        self.action(button="Right")
        self.wait_ui(overlay_selection=2)
        self.action(button="A")
        self.wait_ui(overlay=0)
        require(read(config)["theme"] == "dark" and read(config)["theme_style"] == "teal",
                "dark theme did not persist with the chosen style")
        self.capture("preference-dark")
        # B discards a pending draft and restores the saved appearance.
        self.action(target="card_1")
        self.wait_ui(overlay=21, overlay_selection=2)
        self.action(button="Right")
        self.wait_ui(overlay_selection=0)
        self.action(button="B")
        self.wait_ui(overlay=0)
        require(read(config)["theme"] == "dark", "discarded theme draft reached config")
        self.action(target="card_3")
        self.wait_ui(overlay=37, overlay_selection=0)
        self.action(button="Down")
        self.wait_ui(overlay_selection=2)
        self.action(button="Right")
        self.wait_ui(overlay_selection=3)
        self.action(button="A")
        self.wait_ui(overlay=0)
        require(read(self.app / "config.json")["ui_language"] == "en", "English did not persist")
        self.capture("preference-english")
        self.checked("preferences_journey", ["theme mode and style preview without writing config",
                                            "A saves both radio groups together and B discards the draft",
                                            "language switch keeps navigation usable"])
