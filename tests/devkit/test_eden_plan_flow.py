from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.automation.eden_plan_flow import PLAN_SCENARIOS, PlanFlow, prepare_plans, verify_partition, verify_forecast, verify_windows
from tools.automation.eden_runner import GateError, read, save, scenario_names


def rejected(function) -> None:
    try:
        function()
    except GateError:
        return
    raise AssertionError("bad plan evidence accepted")


def test_language_navigation() -> None:
    class SetupBase:
        def setup(self) -> None:
            self.calls.append(("setup",))

    class Flow(PlanFlow, SetupBase):
        def page(self, index: int) -> None:
            self.calls.append(("page", index))

        def action(self, **kwargs: object) -> None:
            self.calls.append(("action", kwargs))

        def wait_ui(self, **expected: object) -> dict:
            self.calls.append(("wait", expected))
            return {"ui": expected}

    with tempfile.TemporaryDirectory() as folder:
        flow = Flow()
        flow.app = Path(folder)
        save(flow.app / "config.json", {"ui_language": "en"})
        flow.calls, flow.identity = [], {"plans_language": "system"}
        flow.setup()
        assert flow.calls == [("setup",)]
        flow.calls, flow.identity = [], {"plans_language": "en"}
        flow.setup()
        assert flow.calls == [
            ("setup",), ("page", 0), ("action", {"target": "header_language"}),
            ("wait", {"overlay": 37, "overlay_selection": 0}),
            ("action", {"button": "Down"}), ("wait", {"overlay_selection": 2}),
            ("action", {"button": "Right"}), ("wait", {"overlay_selection": 3}),
            ("action", {"button": "A"}), ("wait", {"overlay": 0}),
        ]
        (flow.app / "config.json").write_text('{"ui_language":"zh-Hans"}', encoding="utf-8")
        rejected(flow.setup)


def main() -> None:
    test_language_navigation()
    assert scenario_names("plans") == PLAN_SCENARIOS and len(PLAN_SCENARIOS) == 7
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder) / 'not-created'
        reply = subprocess.run([sys.executable, '-X', 'utf8', str(ROOT / 'tools/automation/eden_runner.py'),
                                'run', '--suite', 'main', '--plans-language', 'en', '--eden', 'missing',
                                '--profile', 'missing', '--mcp-python', 'missing', '--out', str(output)],
                               capture_output=True, encoding='utf-8')
        assert reply.returncode == 2 and '--plans-language en requires --suite plans' in reply.stderr
        assert not output.exists()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        rules = {"week": [{"mode": "limit", "minutes": 120}] * 7}
        prepare_plans(root / "app", root / "out", rules)
        assert rules["bedtime_enabled"] is False and rules["scheduled_override_enabled"] is False
        assert not (root / "app/calendars/active.json").exists()
        assert read(next((root / "app/calendar-import").glob("*.json"))) == read(root / "out/fixtures/calendar-import.json")
    baseline = {"bedtime_week": [1], "bedtime_holiday_mode": "inherit", "bedtime_scheduled_present": False,
                "bedtime_enabled": False, "week": [120]}
    for section, changes in enumerate(({"bedtime_week": [2]}, {"bedtime_holiday_mode": "custom"}, {"bedtime_scheduled_present": True})):
        saved = {**baseline, **changes}
        verify_partition(baseline, saved, section)
        rejected(lambda: verify_partition(baseline, baseline, section))
        rejected(lambda: verify_partition(baseline, {**saved, "week": [90]}, section))
    status = {"state": {"day_index": 2489, "calendar_option_id": "EDENX", "calendar_source": "user_import",
                        "rule_source": "statutory_holiday", "forecast": [
                            {"day_index": 2489 + i, "mode": 1, "minutes": 90, "rule_source": "statutory_holiday", "calendar_covered": True}
                            for i in range(7)]}}
    ui = {"forecast": [[2489 + i, 1, 90, 3, True] for i in range(7)]}
    verify_forecast(status, ui, True)
    bad = deepcopy(status)
    bad["state"]["rule_source"] = "today_override"
    rejected(lambda: verify_forecast(bad, ui, True))
    bad = deepcopy(ui)
    bad["forecast"][6][2] = 120
    rejected(lambda: verify_forecast(status, bad, True))
    rules = {"bedtime_week": [{"enabled": True, "start_minute": 1200, "end_minute": 360}] * 7,
             "bedtime_enabled": False, "bedtime_calendar_enabled": False, "bedtime_scheduled_present": True,
             "bedtime_scheduled_start_day_index": 2489, "bedtime_scheduled_end_day_index": 2489}
    for name in ("holiday", "makeup", "scheduled"):
        rules.update({f"bedtime_{name}_mode": "custom", f"bedtime_{name}_start_minute": 1380, f"bedtime_{name}_end_minute": 480})
    ui = {"bedtime_windows": [[1, 1200, 360]] * 7 + [[2, 1380, 480]] * 3, "bedtime_dirty": False,
          "bedtime_draft_enabled": False, "bedtime_draft_calendar": False, "bedtime_draft_present": True,
          "bedtime_draft_start_day": 2489, "bedtime_draft_end_day": 2489}
    verify_windows(ui, rules)
    rejected(lambda: verify_windows({**ui, "bedtime_draft_start_day": 2488}, rules))
    rejected(lambda: verify_windows({**ui, "bedtime_dirty": True}, rules))
    print("PASS: plans language navigation, X01 fixture/source binding and X04 partition preservation gates")


if __name__ == "__main__":
    main()
