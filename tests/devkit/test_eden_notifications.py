from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.automation.eden_notification_flow import NOTIFICATION_SCENARIOS, verify_notifications
from tools.automation.eden_runner import GateError, save, scenario_names


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


if __name__ == "__main__":
    test_notification_evidence()
    print("PASS: notification suite evidence and rejected scope/business mutations")
