"""Non-TV allowance editor and legacy-state migration through the real Eden UI."""
from __future__ import annotations

from pathlib import Path
import json
import time

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import read, read_observation_file, require, save

DOCK_SCENARIOS = ("setup", "dock_legacy", "dock_editor", "dock_reopen", "dock_limits")


def legacy_fixture(state: dict, day: int) -> dict:
    """Historical input only, written with the owned emulator stopped."""
    return {**state, "dock_tracking_started": True, "dock_day_index": day - 12,
            "dock_baseline_known": False, "dock_usage_known": False,
            "dock_interval_unknown": True, "dock_usage_incomplete": True,
            "dock_counter_source": 0, "dock_counter_known": False,
            "dock_counter_day_index": 0, "dock_counter_total_ns": 0,
            "dock_counter_captured_at": 0}


def install_fixture(path: Path, fixture: dict, pid: int | None) -> None:
    require(pid is None and path.name == "state.json" and path.parent.is_dir(),
            "legacy fixture requires a stopped owned instance")
    temporary = path.with_name("state.eden-legacy-fixture.tmp")
    with temporary.open("x", encoding="utf-8") as output:
        json.dump(fixture, output, ensure_ascii=False, separators=(",", ":"))
    temporary.replace(path)


def state_snapshot(path: Path) -> dict:
    # Read-only retries cover the guest's atomic remove/rename publication;
    # malformed JSON still fails immediately and no business action is replayed.
    deadline = time.monotonic() + 1
    while True:
        try:
            return read_observation_file(path)[0]
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.02)


def verify_preview(ui: dict, minutes: int, state: int) -> None:
    require(all(type(ui.get(key)) is int for key in
                ("dock_editor_minutes", "dock_after_preview", "dock_after_remaining")),
            "non-TV preview numeric types differ")
    require(ui.get("overlay") == 20 and ui.get("numpad_purpose") == 11 and
            ui.get("dock_editor_minutes") == minutes and ui.get("dock_after_preview") == state,
            "non-TV editor projection differs")
    if state in (3, 4):
        require(ui.get("dock_usage_known") is True and type(ui.get("dock_used")) is int and
                ui["dock_used"] >= 0 and ui.get("dock_after_remaining") == max(0, minutes - ui["dock_used"]),
                "non-TV preview arithmetic differs")
    elif state == 0:
        require(ui.get("dock_after_remaining") == -1, "stale editor guessed remaining use")


def verify_dock(output: Path) -> None:
    for name in DOCK_SCENARIOS[1:]:
        row = read(output / f"assertions/{name}.json")
        require(row.get("passed") is True and row.get("checks"), f"missing non-TV journey: {name}")
    before = read(output / "dock-evidence/pre-legacy.json")
    fixture = read(output / "dock-evidence/legacy-fixture.json")
    day = read(output / "dock-evidence/legacy-status.json")["state"]["day_index"]
    require(fixture == legacy_fixture(before, day), "legacy input differs from declared fixture")
    migrated = read(output / "dock-evidence/migrated.json")
    require(migrated["dock_day_index"] == day and migrated["dock_usage_known"] is True and
            migrated["dock_interval_unknown"] is False, "legacy migration did not recover trusted usage")
    for name, minutes, state in (("dock-preview-94", 94, 3), ("dock-preview-exhausted", 5, 4),
                                  ("dock-preview-stale", 94, 0)):
        verify_preview(read(output / f"checkpoints/{name}.json")["ui"], minutes, state)
    require(read(output / "dock-evidence/cancel-before.json") == read(output / "dock-evidence/cancel-after.json"),
            "cancel wrote non-TV rules")
    for name, dock, daily in (("dock-94-active", False, False), ("dock-persisted", False, False),
                              ("dock-daily-independent", False, True), ("dock-exhausted", True, False)):
        ui = read(output / f"checkpoints/{name}.json")["ui"]
        require(ui["dock_restriction_active"] is dock and ui["daily_restriction_active"] is daily and
                ui["restricted_now"] == int(dock or daily), f"restriction mismatch: {name}")
    persisted = read(output / "dock-evidence/reopened-rules.json")
    require(persisted["undocked_daily_minutes"] == 94 and persisted["undocked_limit_enabled"] is True and
            persisted["force_docked"] is False, "saved allowance lost on reopen")
    reopen_before = read(output / "dock-evidence/reopen-before-state.json")
    reopen_after = read(output / "dock-evidence/reopen-after-state.json")
    require(reopen_after["dock_usage_known"] is True and
            reopen_after["undocked_used_ns"] >= reopen_before["undocked_used_ns"],
            "non-TV cumulative use went backwards across reopen")
    rules = read(output / "app-final/rules.json")
    require(not rules["force_docked"] and not rules["undocked_limit_enabled"], "non-TV suite did not close its policy")
    rows = [read(p) for p in (output / "receipts").glob("*.json")]
    require(sum(r.get("type") == "set_dock_policy" and r.get("status") == "ok" for r in rows) == 4,
            "non-TV policy save count differs")


class DockFlow(MainFlow):
    def dock_legacy(self) -> None:
        self.plan(5, page=5)
        self.action(target="dock_1")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_dock_policy", before)
        status = self.refresh(dock_restriction_active=True, restricted_now=1)
        self.capture("dock-before-migration")
        state = state_snapshot(self.app / "state.json")
        save(self.output / "dock-evidence/pre-legacy.json", state)
        self.stop()
        # This is a declared historical input fixture, never a synthetic
        # successful request/result. No live process or production SD is edited.
        for name in ("state.json", "state.json.write"):
            path = self.app / name
            if path.is_file():
                save(self.output / f"dock-evidence/stopped-{name}", read(path))
        fixture = legacy_fixture(state, status["state"]["day_index"])
        save(self.output / "dock-evidence/legacy-fixture.json", fixture)
        install_fixture(self.app / "state.json", fixture, self.pid)
        save(self.output / "dock-evidence/legacy-status.json", status)
        self.start()
        self.wait_ui(view=0, overlay=0)
        self.parent()
        self.refresh(dock_restriction_active=True, restricted_now=1)
        migrated = state_snapshot(self.app / "state.json")
        require(migrated["dock_usage_known"] and not migrated["dock_interval_unknown"], "old unknown usage stayed unknown")
        save(self.output / "dock-evidence/migrated.json", migrated)
        self.checked("dock_legacy", ["stopped isolated legacy fixture", "real startup migrates twelve-day-old unknown use",
                                     "TV-only restriction remains until normal policy change"])

    def edit_minutes(self, minutes: int) -> None:
        self.wait_ui(overlay=20, numpad_purpose=11)
        for field, number in ((0, minutes // 60), (1, minutes % 60)):
            self.action(target=f"duration_{field}")
            self.action(button="Y")
            for digit in str(number):
                self.action(target=f"code_{digit}")
        self.wait_ui(dock_editor_minutes=minutes)

    def save_policy(self, minutes: int) -> None:
        ui = self.wait_ui(overlay=0, waiting=False)["ui"]
        if ui["view"] != 1 or ui["parent_page"] != 1 or ui["plan_page"] != 5:
            self.plan(5, page=5)
        self.action(target="dock_2")
        self.edit_minutes(minutes)
        self.action(button="Plus")
        self.wait_ui(overlay=0, dock_dirty=True)
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_dock_policy", before)

    def dock_editor(self) -> None:
        self.plan(5, page=5)
        self.refresh()
        baseline = read(self.app / "rules.json")
        save(self.output / "dock-evidence/cancel-before.json", baseline)
        requests = self.ids()
        self.action(target="dock_2")
        self.edit_minutes(94)
        verify_preview(self.observe()["ui"], 94, 3)
        self.capture("dock-preview-94")
        self.edit_minutes(5)
        verify_preview(self.observe()["ui"], 5, 4)
        self.capture("dock-preview-exhausted")
        self.edit_minutes(94)
        # Keep the same editor and let the real snapshot become stale; no
        # clock acceleration, fake refresh failure or background state write.
        deadline = min(self.deadline, time.monotonic() + 145)
        while time.monotonic() < deadline:
            if self.wait_ui(overlay=20)["ui"]["dock_after_preview"] == 0:
                break
            time.sleep(0.5)
        verify_preview(self.wait_ui(overlay=20)["ui"], 94, 0)
        self.capture("dock-preview-stale")
        self.action(button="B")
        self.wait_ui(overlay=0)
        require(read(self.app / "rules.json") == baseline, "editor cancel wrote rules")
        require(not any(read(p).get("type") == "set_dock_policy" for p in (self.app / "results").glob("*.json")
                        if p.stem not in requests), "editor cancel submitted a policy")
        save(self.output / "dock-evidence/cancel-after.json", read(self.app / "rules.json"))
        self.refresh()
        self.save_policy(94)
        self.refresh(dock_restriction_active=False, daily_restriction_active=False, restricted_now=0)
        self.capture("dock-94-active")
        self.checked("dock_editor", ["preview 94 minus observed use", "exhausted draft preview",
                                     "same editor naturally becomes stale without guessing", "cancel does not save",
                                     "unique normal save of 94 releases legacy TV requirement"])

    def dock_reopen(self) -> None:
        before = state_snapshot(self.app / "state.json")
        save(self.output / "dock-evidence/reopen-before-state.json", before)
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0)
        self.parent()
        self.refresh(dock_restriction_active=False, daily_restriction_active=False, restricted_now=0)
        after = state_snapshot(self.app / "state.json")
        require(after["dock_usage_known"] and after["undocked_used_ns"] >= before["undocked_used_ns"],
                "non-TV use was lost across re-open")
        save(self.output / "dock-evidence/reopen-after-state.json", after)
        save(self.output / "dock-evidence/reopened-rules.json", read(self.app / "rules.json"))
        self.capture("dock-persisted")
        self.checked("dock_reopen", ["normal re-open retains 94-minute policy and migrated use"])

    def dock_limits(self) -> None:
        self.today(1)
        self.refresh(dock_restriction_active=False, daily_restriction_active=True, restricted_now=1)
        self.capture("dock-daily-independent")
        self.today(120)
        self.save_policy(5)
        self.refresh(dock_restriction_active=True, daily_restriction_active=False, restricted_now=1)
        self.capture("dock-exhausted")
        self.action(target="dock_0")
        before = self.ids()
        self.action(button="Plus")
        self.submitted("set_dock_policy", before)
        self.refresh(dock_restriction_active=False, restricted_now=0)
        self.checked("dock_limits", ["non-TV allowance does not override exhausted daily total",
                                     "allowance below real simulated use restricts again", "normal disable restores use"])
