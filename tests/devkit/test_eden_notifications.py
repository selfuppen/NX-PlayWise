from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.automation.eden_notification_flow import (
    BAD_TEST_JOURNAL, NOTIFICATION_SCENARIOS, NotificationFlow, verify_notifications,
)
from tools.automation.eden_runner import GateError, read, save, scenario_names


def test_notification_evidence() -> None:
    assert scenario_names("notifications") == NOTIFICATION_SCENARIOS
    with tempfile.TemporaryDirectory() as folder:
        output = Path(folder)
        for name in ("setup_notifications", *NOTIFICATION_SCENARIOS[1:]):
            save(output / f"assertions/{name}.json", {"passed": True, "checks": ["UI verified"]})
        controls = output / "assertions/notification_controls.json"
        controls.write_text(json.dumps({"passed": True, "checks": ["UI verified"],
            "rules_before": {"minutes": 120}, "rules_after": {"minutes": 120},
            "writes_before": ["setup"], "writes_after": ["setup"], "sound_before": True,
            "sound_after": True, "receiver_display_verified": False}), encoding="utf-8")
        reopened = output / "assertions/notification_reopen.json"
        reopened.write_text(json.dumps({"passed": True, "checks": ["UI verified"],
                                      "enabled_after_reopen": True}), encoding="utf-8")
        save(output / "app-final/config.json", {"reason_notifications_enabled": False,
            "theme": "dark", "sound_effects_enabled": True})
        save(output / "checkpoints/setup.json", {"ui": {"setup_review_active": False}})
        for mode in ("light", "dark"):
            for kind, index in (("overlay", 0), ("short", 1)):
                save(output / f"checkpoints/notification-{mode}-{kind}.json", {"ui": {
                    "overlay": 48, "notification_preview": index, "reason_notifications_enabled": True}})
        manager = output / "notification-evidence/manager-after-disable.ini"
        manager.parent.mkdir(parents=True)
        (manager.parent / "manager-before-enable.ini").write_bytes(b"[ultrahand]\nnotifications=false\nother=retained\n")
        (manager.parent / "manager-enabled.ini").write_bytes(b"[ultrahand]\nnotifications=true\nother=retained\n")
        manager.write_bytes(b"[ultrahand]\nnotifications=true\nother=retained\n")
        verify_notifications(output)
        for path in manager.parent.glob("*.ini"):
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        verify_notifications(output)
        before = controls.read_text(encoding="utf-8")
        for change in ({"receiver_display_verified": True}, {"writes_after": ["setup", "unexpected-write"]},
                       {"sound_after": False}, {"rules_after": {"minutes": 0}}):
            controls.write_text(json.dumps({**json.loads(before), **change}), encoding="utf-8")
            try:
                verify_notifications(output)
            except GateError:
                pass
            else:
                raise AssertionError("invalid notification evidence accepted")
        controls.write_text(before, encoding="utf-8")
        verify_notifications(output)
        manager.write_bytes(b"[ultrahand]\nnotifications=false\nother=retained\n")
        try:
            verify_notifications(output)
        except GateError:
            pass
        else:
            raise AssertionError("global notice disable was accepted")


class FlowHarness(NotificationFlow):
    """Host action model: exercise journey wiring, never an Eden/display result."""
    def __init__(self, root: Path) -> None:
        self.runtime, self.output = root / "runtime", root / "output"
        self.app = self.runtime / "user/sdmc/switch/playwise-eden"
        self.step, self.pid = 0, 1
        self.actions: list[dict] = []
        self.phase_actions: dict[str, list[dict]] = {}
        self.ui = {"view": 3, "parent_page": 3, "setup_step": 2, "overlay": 0,
                   "waiting": False, "reason_notifications_enabled": False,
                   "reason_external_notifications": 0, "reason_detail_ready": False,
                   "reason_overlay_installed": False, "setup_review_active": False}
        save(self.app / "rules.json", {"minutes": 120})
        save(self.app / "config.json", {"reason_notifications_enabled": False,
             "sound_effects_enabled": True, "theme": "dark"})
        (self.app / "support").mkdir()
        self.manager = self.runtime / "user/sdmc/config/ultrahand/config.ini"
        self.manager.parent.mkdir(parents=True)
        self.manager.write_bytes(b"[ultrahand]\nnotifications=false\nother=retained\n")

    def page(self, index: int) -> None:
        assert index == 3
        self.ui.update(view=1, parent_page=index, overlay=0)

    def action(self, button: str | None = None, target: str | None = None) -> None:
        self.step += 1
        self.actions.append({"button": button, "target": target})
        if target in ("setup_notifications", "card_1"):
            self.ui.update(overlay=48, overlay_selection=0, notification_preview=0, notification_feedback=0)
        elif button == "B" or target == "overlay_back":
            self.ui["overlay"] = 0
        elif button in ("L", "R") or target in ("notice_preview_0", "notice_preview_1"):
            self.ui["notification_preview"] = int(button == "R" or target == "notice_preview_1")
        elif button in ("Up", "Down", "Left", "Right"):
            self.ui["overlay_selection"] = {"Up": 0, "Down": 1, "Left": 1, "Right": 2}[button]
        elif button == "A" or target in ("notification_0", "notification_1", "notification_2"):
            selection = int(target[-1]) if target else self.ui["overlay_selection"]
            self.ui["overlay_selection"] = selection
            if selection == 0:
                config = read(self.app / "config.json")
                config["reason_notifications_enabled"] = not config["reason_notifications_enabled"]
                (self.app / "config.json").write_text(json.dumps(config), encoding="utf-8")
                self.ui["reason_notifications_enabled"] = config["reason_notifications_enabled"]
                self.ui["notification_feedback"] = 0
                if config["reason_notifications_enabled"]:
                    self.manager.write_bytes(self.manager.read_bytes().replace(b"notifications=false", b"notifications=true"))
                    self.ui["reason_external_notifications"] = 1
            elif selection == 1:
                self.ui["notification_feedback"] = 1
            else:
                self.ui["notification_preview"] = 1
                self.ui["notification_feedback"] = (2 if self.ui["reason_external_notifications"] == 0 else
                    4 if (self.app / "support/notification-test-owned.json").exists() else 3)
        else:
            raise AssertionError((button, target))

    def wait_ui(self, **expected: object) -> dict:
        assert all(self.ui.get(key) == value for key, value in expected.items()), (expected, self.ui)
        return {"ui": deepcopy(self.ui)}

    def notification_phase(self, name: str) -> None:
        super().notification_phase(name)
        self.phase_actions[name] = deepcopy(self.actions)

    def capture(self, name: str) -> None:
        save(self.output / f"checkpoints/{name}.json", {"ui": self.ui})

    def stop(self) -> None:
        self.pid = None

    def start(self) -> None:
        assert self.pid is None
        assert (self.app / "support/notification-test-owned.json").read_bytes() == BAD_TEST_JOURNAL
        self.pid = 2
        self.ui.update(view=0, overlay=0)


def current_report(root: Path) -> FlowHarness:
    flow = FlowHarness(root)
    flow.setup_preferences()
    flow.ui.update(view=0, setup_step=0, setup_active=True)
    config = read(flow.app / "config.json")
    config["setup_wizard_step"] = 0
    (flow.app / "config.json").write_text(json.dumps(config), encoding="utf-8")
    setup_result = {"request_id": "host-setup", "type": "complete_setup", "status": "ok"}
    save(flow.app / "results/host-setup.json", setup_result)
    save(flow.output / "receipts/host-setup.json", setup_result)
    flow.capture("setup")
    flow.notification_controls()
    # Theme rendering is outside this host action model and remains an Eden journey.
    for mode in ("light", "dark"):
        for kind, index in (("overlay", 0), ("short", 1)):
            save(flow.output / f"checkpoints/notification-{mode}-{kind}.json", {"ui": {
                "overlay": 48, "notification_preview": index, "reason_notifications_enabled": True}})
    flow.checked("notification_themes", ["host fixture; not a rendering result"])
    flow.notification_reopen()
    save(flow.output / "app-final/config.json", read(flow.app / "config.json"))
    return flow


def test_notification_journeys_and_phase_rejection() -> None:
    with tempfile.TemporaryDirectory() as folder:
        flow = current_report(Path(folder))
        output = flow.output
        verify_notifications(output)
        # Direct test precedes the check in both setup and the parent journey.
        assert flow.phase_actions["setup-test"][-1] == {"button": None, "target": "notification_2"}
        assert not any(a["target"] == "notification_1" for a in flow.phase_actions["setup-test"])
        start = len(flow.phase_actions["controls-disabled"])
        assert flow.phase_actions["controls-test"][start:] == [
            {"button": "Down", "target": None}, {"button": "Right", "target": None},
            {"button": "A", "target": None}]
        assert flow.phase_actions["setup-touch-back"][-1]["target"] == "overlay_back"
        assert flow.phase_actions["reopen-back"][-1]["button"] == "B"
        assert flow.pid == 2 and len(NOTIFICATION_SCENARIOS) == 4

        mutations = [
            ("setup-test", "ui", "notification_preview", 0),
            ("controls-test", "ui", "notification_preview", 0),
            ("reopen-test", "ui", "notification_preview", 0),
            ("setup-details", "ui", "notification_feedback", 0),
            ("controls-short", "ui", "notification_feedback", 0),
            ("reopen-details", "ui", "notification_feedback", 3),
            ("controls-probed", "ui", "notification_preview", 0),
            ("reopen-test", "ui", "waiting", True),
            ("setup-pad-back", "ui", "setup_step", 3),
            ("setup-touch-back", "ui", "overlay", 48),
            ("reopen-back", "ui", "parent_page", 0),
            ("controls-test", "ui", "reason_notifications_enabled", True),
            ("controls-test", "config", "reason_notifications_enabled", True),
            ("setup-short", "config", "sound_effects_enabled", False),
            ("reopen-short", "rules", "minutes", 0),
            ("controls-details", "ui", "reason_detail_ready", True),
            ("controls-details", "ui", "overlay_selection", 0),
        ]
        for phase, section, key, value in mutations:
            path = output / f"notification-evidence/phases/{phase}.json"
            original = path.read_bytes()
            changed = read(path)
            changed[section][key] = value
            path.write_text(json.dumps(changed), encoding="utf-8")
            try:
                verify_notifications(output)
            except GateError:
                pass
            else:
                raise AssertionError(f"invalid phase accepted: {phase}/{key}")
            path.write_bytes(original)
        path = output / "notification-evidence/phases/controls-test.json"
        original = path.read_bytes()
        for key, value in (("writes", ["unexpected"]), ("step", 0), ("manager_sha256", "0" * 64)):
            changed = json.loads(original)
            changed[key] = value
            path.write_text(json.dumps(changed), encoding="utf-8")
            try:
                verify_notifications(output)
            except GateError:
                pass
            else:
                raise AssertionError(f"invalid phase accepted: {key}")
        path.unlink()
        try:
            verify_notifications(output)
        except OSError:
            pass
        else:
            raise AssertionError("missing phase accepted")
        path.write_bytes(original)
        journal = output / "notification-evidence/bad-journal-after.json"
        journal.write_bytes(b"{}")
        try:
            verify_notifications(output)
        except GateError:
            pass
        else:
            raise AssertionError("overwritten unknown journal accepted")
        journal.write_bytes(BAD_TEST_JOURNAL)
        controls = output / "assertions/notification_controls.json"
        original = controls.read_bytes()
        for version in (None, True, 2):
            changed = json.loads(original)
            if version is None:
                del changed["phase_evidence_version"]
            else:
                changed["phase_evidence_version"] = version
            controls.write_text(json.dumps(changed), encoding="utf-8")
            try:
                verify_notifications(output)
            except GateError:
                pass
            else:
                raise AssertionError("missing or unknown phase version accepted")
        controls.write_bytes(original)
        receipt = output / "receipts/host-setup.json"
        original = receipt.read_bytes()
        changed = json.loads(original)
        changed["status"] = "error"
        receipt.write_text(json.dumps(changed), encoding="utf-8")
        try:
            verify_notifications(output)
        except GateError:
            pass
        else:
            raise AssertionError("failed setup completion accepted")
        receipt.write_bytes(original)
        verify_notifications(output)


def test_bad_journal_fixture_guards() -> None:
    for existing in (False, True):
        with tempfile.TemporaryDirectory() as folder:
            flow = FlowHarness(Path(folder))
            journal = flow.app / "support/notification-test-owned.json"
            if existing:
                journal.write_bytes(b"existing ownership sentinel")
            else:
                flow.stop = lambda: None  # A still-live owner must prohibit preparation.
            try:
                flow.notification_reopen()
            except GateError:
                pass
            else:
                raise AssertionError("unsafe bad journal preparation accepted")
            assert journal.exists() is existing
            if existing:
                assert journal.read_bytes() == b"existing ownership sentinel"


if __name__ == "__main__":
    test_notification_evidence()
    test_notification_journeys_and_phase_rejection()
    test_bad_journal_fixture_guards()
    print("PASS: notification action journeys, phase evidence and rejected preview/result/business mutations")
