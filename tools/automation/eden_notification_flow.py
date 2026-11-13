"""Normal UI journeys for notification preferences; no receiver is simulated."""
from __future__ import annotations

from pathlib import Path

from tools.automation.eden_main_flow import MainFlow
from tools.automation.eden_runner import digest, read, require, save

NOTIFICATION_SCENARIOS = ("setup", "notification_controls", "notification_themes", "notification_reopen")
BAD_TEST_JOURNAL = b'{"eden_fixture":"invalid notification ownership journal"}\n'

# These are observations of normal input, not commands or a receiver simulation.
PHASES = {
    "setup-open": {"notification_preview": 0, "notification_feedback": 0, "overlay_selection": 0},
    "setup-test": {"notification_preview": 1, "notification_feedback": 2, "overlay_selection": 2},
    "setup-details": {"notification_preview": 0, "notification_feedback": 2, "overlay_selection": 2},
    "setup-short": {"notification_preview": 1, "notification_feedback": 2, "overlay_selection": 2},
    "setup-probed": {"notification_preview": 1, "notification_feedback": 1, "overlay_selection": 1},
    "setup-pad-back": {"overlay": 0},
    "setup-reopened": {"notification_preview": 0, "notification_feedback": 0, "overlay_selection": 0},
    "setup-touch-back": {"overlay": 0},
    "controls-disabled": {"notification_preview": 0, "overlay_selection": 0},
    "controls-test": {"notification_preview": 1, "notification_feedback": 3, "overlay_selection": 2},
    "controls-details": {"notification_preview": 0, "notification_feedback": 3, "overlay_selection": 2},
    "controls-short": {"notification_preview": 1, "notification_feedback": 3, "overlay_selection": 2},
    "controls-probed": {"notification_preview": 1, "notification_feedback": 1, "overlay_selection": 1},
    "reopen-disabled": {"notification_preview": 0, "overlay_selection": 0},
    "reopen-test": {"notification_preview": 1, "notification_feedback": 4, "overlay_selection": 2},
    "reopen-details": {"notification_preview": 0, "notification_feedback": 4, "overlay_selection": 2},
    "reopen-short": {"notification_preview": 1, "notification_feedback": 4, "overlay_selection": 2},
    "reopen-back": {"overlay": 0},
}


def phase_expected(name: str) -> dict:
    setup = name.startswith("setup-")
    return {"overlay": 48, "view": 3 if setup else 1, "waiting": False,
            "reason_notifications_enabled": False, "reason_external_notifications": 0 if setup else 1,
            **({"setup_step": 2} if setup else {"parent_page": 3}), **PHASES[name]}


def verify_notification_phases(output: Path) -> None:
    phases = {name: read(output / f"notification-evidence/phases/{name}.json") for name in PHASES}
    previous_step = -1
    for name, phase in phases.items():
        require(type(phase.get("step")) is int and phase["step"] > previous_step,
                f"notification phase order differs: {name}")
        previous_step = phase["step"]
        expected = phase_expected(name)
        require(all(type(phase["ui"].get(key)) is type(value) and phase["ui"][key] == value
                    for key, value in expected.items()), f"notification phase UI differs: {name}")
        require(phase["config"].get("reason_notifications_enabled", False) is False,
                f"standalone test enabled automatic reminders: {name}")
    for prefix in ("setup-", "controls-", "reopen-"):
        group = [value for name, value in phases.items() if name.startswith(prefix)]
        first = group[0]
        for phase in group[1:]:
            require(all(phase[key] == first[key] for key in ("rules", "writes", "config", "manager_sha256")),
                    f"notification-only actions changed persisted state: {prefix}")
            require(all(phase["ui"][key] == first["ui"][key]
                        for key in ("reason_detail_ready", "reason_overlay_installed")),
                    f"example switching changed environment: {prefix}")
    for prefix, manager in (("setup-", "manager-before-enable.ini"),
                            ("controls-", "manager-enabled.ini"), ("reopen-", "manager-after-disable.ini")):
        require(all(phase["manager_sha256"] == digest(output / "notification-evidence" / manager)
                    for name, phase in phases.items() if name.startswith(prefix)), "phase manager evidence differs")
    evidence = output / "notification-evidence"
    require((evidence / "bad-journal-before.json").read_bytes() == BAD_TEST_JOURNAL ==
            (evidence / "bad-journal-after.json").read_bytes(), "unknown test journal was overwritten or lost")
    final = read(output / "app-final/config.json")
    require(phases["reopen-back"]["config"] == final,
            "final notification preferences differ from the returned page")
    require(final.get("setup_wizard_step") == 0 and
            read(output / "checkpoints/setup.json")["ui"].get("setup_active") is True,
            "optional notification checks prevented setup completion")
    setup = [(path, read(path)) for path in (output / "receipts").glob("*.json")]
    setup = [(path, row) for path, row in setup if row.get("type") == "complete_setup"]
    require(len(setup) == 1 and setup[0][1].get("status") == "ok" and
            setup[0][1].get("request_id") == setup[0][0].stem, "setup completion receipt missing or ambiguous")


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
    version = controls.get("phase_evidence_version")
    if version is None:
        # Previously sealed four-journey reports keep their original scope.
        require(not (output / "notification-evidence/phases").exists(), "phase evidence version missing")
    else:
        require(type(version) is int and version == 1, "unknown notification phase evidence version")
        verify_notification_phases(output)


class NotificationFlow(MainFlow):
    def write_ids(self) -> list[str]:
        return sorted(p.stem for p in (self.app / "results").glob("*.json") if read(p).get("type") != "status")

    def notification_phase(self, name: str) -> None:
        value = self.wait_ui(**phase_expected(name))
        save(self.output / f"notification-evidence/phases/{name}.json", {
            "step": self.step, "ui": value["ui"], "rules": read(self.app / "rules.json"),
            "writes": self.write_ids(), "config": read(self.app / "config.json"),
            "manager_sha256": digest(self.runtime / "user/sdmc/config/ultrahand/config.ini")})

    def setup_preferences(self) -> None:
        before = (self.app / "rules.json").read_bytes()
        self.action(target="setup_notifications")
        self.notification_phase("setup-open")
        self.capture("setup-notification-overlay")
        # Send directly: the numbered environment check is advice, not a gate.
        self.action(target="notification_2")
        self.notification_phase("setup-test")
        self.capture("setup-notification-not-ready")
        self.action(button="L")
        self.notification_phase("setup-details")
        self.action(target="notice_preview_1")
        self.notification_phase("setup-short")
        self.capture("setup-notification-short")
        self.action(target="notification_1")
        self.notification_phase("setup-probed")
        self.action(button="B")
        self.notification_phase("setup-pad-back")
        self.action(target="setup_notifications")
        self.notification_phase("setup-reopened")
        self.action(target="overlay_back")
        self.notification_phase("setup-touch-back")
        require((self.app / "rules.json").read_bytes() == before and
                not read(self.app / "config.json").get("reason_notifications_enabled", False),
                "optional setup preview/probe enabled hints or changed rules")
        self.checked("setup_notifications", ["direct unavailable test forces the brief example without enabling hints",
            "example switching preserves the real result", "probe preserves the selected example without waiting",
            "controller and touch back return to the same setup step"])

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
        self.action(button="A")
        self.notification_phase("controls-disabled")
        self.action(button="Down")
        self.wait_ui(overlay_selection=1)
        self.action(button="Right")
        self.wait_ui(overlay_selection=2)
        self.action(button="A")
        self.notification_phase("controls-test")
        self.capture("notification-test-published")
        self.action(target="notice_preview_0")
        self.notification_phase("controls-details")
        self.action(button="R")
        self.notification_phase("controls-short")
        self.action(button="Left")
        self.wait_ui(overlay_selection=1)
        self.action(button="A")
        self.notification_phase("controls-probed")
        self.action(button="Up")
        self.wait_ui(overlay_selection=0)
        self.action(button="A")
        self.wait_ui(reason_notifications_enabled=True)
        self.action(button="B")
        self.wait_ui(view=1, parent_page=3, overlay=0)
        after = read(self.app / "config.json")
        require(rules == read(self.app / "rules.json") and writes == self.write_ids() and
                sound == after["sound_effects_enabled"], "notification controls changed business state or sound")
        self.checked("notification_controls", ["toggle persists only notification preference",
            "manager config prepared and read back", "pad navigation sends before checking the environment",
            "standalone test keeps automatic hints off and forces the brief example",
            "example switches retain feedback; publication is not receiver display",
            "rules, write requests and sound stay unchanged"],
            rules_before=rules, rules_after=read(self.app / "rules.json"), writes_before=writes,
            writes_after=self.write_ids(), sound_before=sound, sound_after=after["sound_effects_enabled"],
            receiver_display_verified=False, phase_evidence_version=1)

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
        # A declared bad ownership input gives a deterministic failure without
        # racing the 10-second limit or injecting business/authentication state.
        journal = self.app / "support/notification-test-owned.json"
        require(self.pid is None and not journal.exists(), "bad journal fixture requires a stopped clean test instance")
        with journal.open("xb") as target:
            target.write(BAD_TEST_JOURNAL)
        evidence = self.output / "notification-evidence"
        (evidence / "bad-journal-before.json").write_bytes(journal.read_bytes())
        self.start()
        self.wait_ui(view=0, overlay=0, setup_review_active=False)
        require(read(self.app / "config.json")["reason_notifications_enabled"] is True,
                "reopen lost notification preference")
        self.page(3)
        self.action(target="card_1")
        self.wait_ui(overlay=48, reason_notifications_enabled=True, reason_external_notifications=1, notification_preview=0)
        self.capture("notification-persisted")
        self.action(target="notification_0")
        self.notification_phase("reopen-disabled")
        self.action(target="notification_2")
        self.notification_phase("reopen-test")
        self.capture("notification-test-failed")
        self.action(button="L")
        self.notification_phase("reopen-details")
        self.action(target="notice_preview_1")
        self.notification_phase("reopen-short")
        manager = self.runtime / "user/sdmc/config/ultrahand/config.ini"
        expected = (self.output / "notification-evidence/manager-enabled.ini").read_bytes()
        require(manager.read_bytes() == expected, "disabling own hints changed global manager preference")
        target = self.output / "notification-evidence/manager-after-disable.ini"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(manager.read_bytes())
        self.action(button="B")
        self.notification_phase("reopen-back")
        require(journal.read_bytes() == BAD_TEST_JOURNAL, "failed test overwrote unknown ownership")
        (evidence / "bad-journal-after.json").write_bytes(journal.read_bytes())
        self.checked("notification_reopen", ["normal close/reopen retains enabled preference and dark theme",
            "resolved setup choices do not reopen guide", "touch can disable hints without disabling global notices",
            "bad journal test forces brief preview without enabling hints or blocking return",
            "switching examples preserves failure; unknown journal remains untouched"],
            enabled_after_reopen=True)
