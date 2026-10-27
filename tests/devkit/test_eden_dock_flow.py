from __future__ import annotations

from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.automation.eden_dock_flow import DOCK_SCENARIOS, install_fixture, legacy_fixture, state_snapshot, verify_preview
from tools.automation.eden_runner import GateError, read, save, scenario_names


def rejected(function) -> None:
    try:
        function()
    except GateError:
        return
    raise AssertionError("invalid dock evidence accepted")


def main() -> None:
    assert scenario_names("dock") == DOCK_SCENARIOS
    before = {"dock_day_index": 99, "dock_counter_source": 2, "buffer_claimed": True,
              "v2_failed_attempts": 3, "eye_usage_total_ns": 1800000000000}
    fixture = legacy_fixture(before, 100)
    assert before["dock_day_index"] == 99 and fixture["dock_day_index"] == 88
    assert fixture["buffer_claimed"] is True and fixture["v2_failed_attempts"] == 3
    assert fixture["eye_usage_total_ns"] == before["eye_usage_total_ns"]
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "state.json"
        save(path, before)
        rejected(lambda: install_fixture(path, fixture, 123))
        assert read(path) == before
        install_fixture(path, fixture, None)
        assert read(path) == fixture
        with patch("tools.automation.eden_dock_flow.read_observation_file",
                   side_effect=[PermissionError("rename gap"), (fixture, 0)]) as reader:
            assert state_snapshot(path) == fixture and reader.call_count == 2
        with patch("tools.automation.eden_dock_flow.read_observation_file", side_effect=GateError("invalid JSON")) as reader:
            rejected(lambda: state_snapshot(path))
            assert reader.call_count == 1
    ui = {"overlay": 20, "numpad_purpose": 11, "dock_editor_minutes": 94,
          "dock_after_preview": 3, "dock_usage_known": True, "dock_used": 30, "dock_after_remaining": 64}
    verify_preview(ui, 94, 3)
    rejected(lambda: verify_preview({**ui, "dock_after_remaining": 94}, 94, 3))
    rejected(lambda: verify_preview({**ui, "dock_usage_known": False}, 94, 3))
    verify_preview({**ui, "dock_editor_minutes": 5, "dock_after_preview": 4, "dock_after_remaining": 0}, 5, 4)
    verify_preview({**ui, "dock_after_preview": 0, "dock_after_remaining": -1}, 94, 0)
    rejected(lambda: verify_preview({**ui, "dock_after_preview": 0}, 94, 0))
    rejected(lambda: verify_preview({**ui, "dock_after_preview": False, "dock_after_remaining": -1}, 94, 0))
    rejected(lambda: verify_preview({**ui, "numpad_purpose": 1}, 94, 3))
    print("PASS: legacy fixture isolation and non-TV preview arithmetic/staleness gates")


if __name__ == "__main__":
    main()
