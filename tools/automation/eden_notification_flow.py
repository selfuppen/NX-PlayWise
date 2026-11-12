"""Normal UI journeys for notification preferences; no receiver is simulated."""
from __future__ import annotations

from pathlib import Path

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import read, require, save

NOTIFICATION_SCENARIOS = ("setup", "notification_controls", "notification_themes", "notification_reopen")


def verify_notifications(output: Path) -> None:
    for name in ("setup_notifications", *NOTIFICATION_SCENARIOS[1:]):
        value = read(output / f"assertions/{name}.json")
        require(value.get("passed") is True and value.get("checks"), f"notification assertions missing: {name}")
    controls = read(output / "assertions/notification_controls.json")
    require(controls["rules_before"] == controls["rules_after"] and
            controls["writes_before"] == controls["writes_after"] and
            controls["sound_before"] == controls["sound_after"] and
            controls["receiver_display_verified"] is False, "notification operation affected business or overclaimed display")
    for mode in ("light", "dark"):
        for kind, index in (("overlay", 0), ("short", 1)):
            ui = read(output / f"checkpoints/notification-{mode}-{kind}.json")["ui"]
            require(ui["overlay"] == 48 and ui["notification_preview"] == index and
                    ui["reason_notifications_enabled"] is True, "notification preview checkpoint differs")
    config = read(output / "app-final/config.json")
    reopened = read(output / "assertions/notification_reopen.json")
    require(reopened["enabled_after_reopen"] is True and config["reason_notifications_enabled"] is False and
            config["theme"] == "dark" and config["sound_effects_enabled"] == controls["sound_before"],
            "notification reopen/disable persistence differs")
    before = (output / "notification-evidence/manager-before-enable.ini").read_bytes()
    enabled = (output / "notification-evidence/manager-enabled.ini").read_bytes()
    require(b"notifications=false" in before and enabled == before.replace(b"notifications=false", b"notifications=true", 1),
            "manager enable changed unrelated original bytes")
    require((output / "notification-evidence/manager-after-disable.ini").read_bytes() == enabled,
            "own disable changed manager setting or unrelated bytes")
    require(read(output / "checkpoints/setup.json")["ui"]["setup_review_active"] is False,
            "supplemental setup guide was not resolved")


class NotificationFlow(MainFlow):
    def write_ids(self) -> list[str]:
        return sorted(p.stem for p in (self.app / "results").glob("*.json") if read(p).get("type") != "status")

    def setup_preferences(self) -> None:
        before = (self.app / "rules.json").read_bytes()
        self.action(target="setup_notifications")
        self.wait_ui(view=3, overlay=48, reason_notifications_enabled=False, notification_preview=0)
        self.capture("setup-notification-overlay")
        self.action(button="R")
        self.wait_ui(notification_preview=1)
        self.capture("setup-notification-short")
        self.action(button="L")
        self.wait_ui(notification_preview=0)
        self.action(target="notice_preview_1")
        self.wait_ui(notification_preview=1)
        self.action(target="notice_preview_0")
        self.wait_ui(notification_preview=0)
        self.action(target="notification_1")
        self.wait_ui(notification_feedback=1, reason_notifications_enabled=False)
        self.action(target="notification_2")
        self.wait_ui(notification_feedback=2, view=3, waiting=False)
        self.capture("setup-notification-not-ready")
        self.action(button="B")
        self.wait_ui(view=3, setup_step=2, overlay=0)
        require((self.app / "rules.json").read_bytes() == before and
                not read(self.app / "config.json").get("reason_notifications_enabled", False),
                "optional setup preview/probe enabled hints or changed rules")
        self.checked("setup_notifications", ["setup opens notification-only dialog", "L/R and touch select examples",
            "probe and unavailable test return inline without waiting", "B returns to the same setup step"])

    def notification_controls(self) -> None:
        self.page(3)
        self.action(target="card_1")
        self.wait_ui(overlay=48, notification_preview=0, overlay_selection=0, reason_notifications_enabled=False)
        rules = read(self.app / "rules.json")
        writes = self.write_ids()
        sound = read(self.app / "config.json")["sound_effects_enabled"]
        manager = self.runtime / "user/sdmc/config/ultrahand/config.ini"
        evidence = self.output / "notification-evidence"
        evidence.mkdir(exist_ok=True)
        before_manager = manager.read_bytes()
        (evidence / "manager-before-enable.ini").write_bytes(before_manager)
        self.action(button="A")
        self.wait_ui(reason_notifications_enabled=True, reason_external_notifications=1)
        require(read(self.app / "config.json")["reason_notifications_enabled"] is True,
                "notification toggle was not saved")
        require(manager.read_bytes() == before_manager.replace(b"notifications=false", b"notifications=true", 1),
                "enabling manager notices changed unrelated original bytes")
        (evidence / "manager-enabled.ini").write_bytes(manager.read_bytes())
        self.action(button="R")
        self.wait_ui(notification_preview=1, overlay_selection=0)
        self.action(target="notice_preview_0")
        self.wait_ui(notification_preview=0, overlay_selection=0)
        self.action(button="Down")
        self.wait_ui(overlay_selection=1)
        self.action(button="A")
        self.wait_ui(notification_feedback=1)
        self.action(button="Right")
        self.wait_ui(overlay_selection=2)
        self.action(button="A")
        self.wait_ui(notification_feedback=3, waiting=False)
        self.capture("notification-test-published")
        self.action(button="Up")
        self.wait_ui(overlay_selection=0)
        self.action(button="B")
        self.wait_ui(view=1, parent_page=3, overlay=0)
        after = read(self.app / "config.json")
        require(rules == read(self.app / "rules.json") and writes == self.write_ids() and
                sound == after["sound_effects_enabled"], "notification controls changed business state or sound")
        self.checked("notification_controls", ["toggle persists only notification preference",
            "manager config prepared and read back", "pad navigation reaches probe and test",
            "test publication is distinct from receiver display", "rules, write requests and sound stay unchanged"],
            rules_before=rules, rules_after=read(self.app / "rules.json"), writes_before=writes,
            writes_after=self.write_ids(), sound_before=sound, sound_after=after["sound_effects_enabled"],
            receiver_display_verified=False)

    def notification_themes(self) -> None:
        for mode, desired in (("light", 1), ("dark", 2)):
            self.page(3)
            self.action(target="card_5")
            current = self.wait_ui(overlay=21)["ui"]["overlay_selection"]
            for offset in range((desired - current) % 3):
                self.action(button="Right")
                self.wait_ui(overlay_selection=(current + offset + 1) % 3)
            self.action(button="A")
            self.wait_ui(overlay=0)
            require(read(self.app / "config.json")["theme"] == mode, "notification theme selection failed")
            self.action(target="card_1")
            self.wait_ui(overlay=48, notification_preview=0, reason_notifications_enabled=True)
            self.capture(f"notification-{mode}-overlay")
            self.action(button="R")
            self.wait_ui(notification_preview=1)
            self.capture(f"notification-{mode}-short")
            self.capture(f"notification-{mode}-short-confirmed")
            self.action(target="overlay_back")
            self.wait_ui(view=1, parent_page=3, overlay=0)
        self.checked("notification_themes", ["normal theme settings save light and dark",
            "both screen examples render in both themes", "touch back returns to parent settings"])

    def notification_reopen(self) -> None:
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0, setup_review_active=False)
        require(read(self.app / "config.json")["reason_notifications_enabled"] is True,
                "reopen lost notification preference")
        self.page(3)
        self.action(target="card_1")
        self.wait_ui(overlay=48, reason_notifications_enabled=True, reason_external_notifications=1, notification_preview=0)
        self.capture("notification-persisted")
        self.action(target="notification_0")
        self.wait_ui(reason_notifications_enabled=False)
        manager = self.runtime / "user/sdmc/config/ultrahand/config.ini"
        expected = (self.output / "notification-evidence/manager-enabled.ini").read_bytes()
        require(manager.read_bytes() == expected, "disabling own hints changed global manager preference")
        target = self.output / "notification-evidence/manager-after-disable.ini"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(manager.read_bytes())
        self.action(button="B")
        self.wait_ui(view=1, parent_page=3, overlay=0)
        self.checked("notification_reopen", ["normal close/reopen retains enabled preference and dark theme",
            "resolved setup choices do not reopen guide", "touch can disable hints without disabling global notices"],
            enabled_after_reopen=True)
