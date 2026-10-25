"""X05 mixed touch/controller journeys over the language and theme matrix."""
from __future__ import annotations

from pathlib import Path

from tools.automation.eden_plan_flow import PlanFlow
from tools.automation.eden_runner import decode, read, require, save

INPUT_SCENARIOS = ("setup", "input_matrix", "input_cancel_hold", "input_reopen")
INPUT_HOLD_SCENARIOS = ("setup", "input_cancel_hold", "input_reopen")
STYLES = ("slate", "indigo", "teal", "amethyst")
LANGUAGES = ("zh-Hans", "zh-Hant", "en")


def load_matrix(path: Path) -> list:
    # The first sealed v1 batch used an array root. Preserve it byte-for-byte;
    # wrapping lets the existing strict duplicate-key decoder validate it too.
    value = decode('{"document":' + path.read_text(encoding="utf-8") + '}')["document"]
    if isinstance(value, dict):
        require(type(value.get("schema_version")) is int and value["schema_version"] == 1,
                "unknown input matrix schema")
        value = value.get("rows")
    require(isinstance(value, list), "input matrix rows must be an array")
    return value


def verify_matrix(rows: list, language: str, styles: tuple[str, ...] = STYLES) -> None:
    require(styles and len(set(styles)) == len(styles) and all(style in STYLES for style in styles),
            "invalid input matrix plan")
    require(language in LANGUAGES and isinstance(rows, list) and len(rows) == len(styles) * 2, "incomplete input matrix")
    require({(r["style"], r["theme"]) for r in rows} ==
            {(style, mode) for style in styles for mode in ("light", "dark")}, "duplicate/missing theme pair")
    for row in rows:
        require(row["config"]["ui_language"] == language and row["config"]["theme"] == row["theme"] and
                row["config"]["theme_style"] == row["style"], "matrix preferences mismatch")
        require(row["rules_before"] == row["rules_after"] and row["writes_before"] == row["writes_after"],
                "discarded input journey wrote business rules")


def verify_input(output: Path, include_matrix: bool = True) -> None:
    language = read(output / "identity.json")["input_language"]
    if include_matrix:
        rows = load_matrix(output / "input-evidence/matrix.json")
        verify_matrix(rows, language, tuple(read(output / "identity.json").get("input_styles", STYLES)))
        for row in rows:
            for editor, overlay in (("weekly", 31), ("special", 32)):
                value = read(output / f"checkpoints/{row['style']}-{row['theme']}-{editor}.json")["ui"]
                require(value["overlay"] == overlay and value["bedtime_dirty"] is True, "editor checkpoint missing")
            returned = read(output / f"checkpoints/{row['style']}-{row['theme']}-returned.json")["ui"]
            require(returned["overlay"] == 0 and returned["plan_page"] == 0 and returned["selected_index"] == 3,
                    "touch return lost source focus or leaked modal input")
    cancelled = read(output / "input-evidence/cancel.json")
    require(cancelled["rules_before"] == cancelled["rules_after"] and
            cancelled["writes_before"] == cancelled["writes_after"], "cancel submitted a calendar activation")
    confirm = read(output / "checkpoints/hold-confirm.json")["ui"]
    require(confirm["overlay"] == 3 and confirm["confirm_hold_required"] is True, "held dialog absent")
    rows = [read(p) for p in (output / "receipts").glob("*.json")]
    applied = [r for r in rows if r.get("type") == "activate_holiday_calendar"]
    require(len(applied) == 1 and applied[0]["status"] == "ok", "held calendar receipt missing/duplicated")
    final = read(output / "app-final/config.json")
    require(final["ui_language"] == language and final["theme_style"] == "amethyst" and final["theme"] == "dark",
            "reopen lost matrix preferences")
    require(read(output / "app-final/calendars/active.json")["option_id"] == "EDENX", "reopen lost applied calendar")
    for name in (INPUT_SCENARIOS if include_matrix else INPUT_HOLD_SCENARIOS)[1:]:
        value = read(output / f"assertions/{name}.json")
        require(value.get("passed") is True and value.get("checks"), "missing input assertions")


class InputFlow(PlanFlow):
    def setup(self) -> None:
        super().setup()
        self.page(3)
        self.action(target="card_3")
        self.wait_ui(overlay=37, overlay_selection=0)
        language = self.identity["input_language"]
        if language == "zh-Hans":
            self.action(button="Right")
        else:
            self.action(button="Down")
            if language == "en":
                self.action(button="Right")
        self.action(button="A")
        self.wait_ui(overlay=0)
        require(read(self.app / "config.json")["ui_language"] == language, "normal language selection failed")
        self.page(2)
        self.action(target="card_1")
        self.wait_ui(overlay=22, pin_length=0)
        self.enter_pin("110")
        self.wait_ui(overlay=7)
        self.capture("optical-pairing-qr")
        self.action(button="B")
        self.wait_ui(overlay=0)

    def write_ids(self) -> list[str]:
        return sorted(p.stem for p in (self.app / "results").glob("*.json") if read(p).get("type") != "status")

    def theme(self, style: str, mode: str) -> None:
        self.page(3)
        self.action(target="card_1")
        current = self.wait_ui(overlay=21)["ui"]["overlay_selection"]
        desired = ("system", "light", "dark").index(mode)
        for _ in range((desired - current) % 3):
            self.action(button="Right")
        self.action(button="Down")
        current = self.wait_ui(overlay=21)["ui"]["overlay_selection"] - 3
        for _ in range((STYLES.index(style) - current) % 4):
            self.action(button="Right")
        self.action(button="A")
        self.wait_ui(overlay=0)
        saved = read(self.app / "config.json")
        require(saved["theme_style"] == style and saved["theme"] == mode, "theme save differs")

    def mixed_editor(self, style: str, mode: str, special: bool) -> None:
        self.plan(3, page=3)
        self.wait_ui(bedtime_section=0, bedtime_dirty=False)
        if special:
            self.action(button="R")
            self.wait_ui(bedtime_section=1)
        self.action(target=f"bedtime_field_{1 if special else 0}")
        overlay = 32 if special else 31
        self.wait_ui(overlay=overlay)
        if special:
            current = ("inherit", "disabled", "custom").index(read(self.app / "rules.json")["bedtime_holiday_mode"])
            for _ in range((2 - current) % 3 or 3):
                self.action(button="X")
        self.action(button="Y")
        self.wait_ui(bedtime_dirty=True)
        self.capture(f"{style}-{mode}-{'special' if special else 'weekly'}")
        self.action(target="bedtime_editor_1")
        self.wait_ui(overlay=20)
        self.action(button="Right")
        self.action(button="Plus")
        self.wait_ui(overlay=overlay)
        self.action(target="overlay_complete")
        self.wait_ui(overlay=0, bedtime_dirty=True)
        self.action(target="plan_back")
        self.wait_ui(overlay=33, bedtime_dirty=True)
        self.action(button="X")
        self.wait_ui(overlay=0, plan_page=0, bedtime_dirty=False, selected_index=3)

    def input_matrix(self) -> None:
        rows = []
        styles = tuple(self.identity.get("input_styles", STYLES))
        for style in styles:
            for mode in ("light", "dark"):
                self.theme(style, mode)
                before, writes = read(self.app / "rules.json"), self.write_ids()
                self.mixed_editor(style, mode, False)
                self.mixed_editor(style, mode, True)
                self.capture(f"{style}-{mode}-returned")
                row = {"style": style, "theme": mode, "config": read(self.app / "config.json"),
                       "rules_before": before, "rules_after": read(self.app / "rules.json"),
                       "writes_before": writes, "writes_after": self.write_ids()}
                save(self.output / f"input-evidence/cells/{style}-{mode}.json", row)
                rows.append(row)
        verify_matrix(rows, self.identity["input_language"], styles)
        save(self.output / "input-evidence/matrix.json", {"schema_version": 1, "rows": rows})
        self.checked("input_matrix", [f"{len(styles) * 2} declared theme pairs through normal preferences", "touch opens both editor types; controller edits time",
                                      "touch completes and returns; discard retains exact rules and source focus"])

    def input_cancel_hold(self) -> None:
        self.theme("amethyst", "dark")
        self.calendar_import()
        self.plan(1, page=2)
        self.action(target="holiday_manage")
        self.wait_ui(overlay=40, calendar_tab=0, calendar_count=2)
        self.action(button="Down")
        self.wait_ui(calendar_selected=1)
        before, writes = read(self.app / "rules.json"), self.write_ids()
        self.action(button="A")
        self.wait_ui(overlay=3, confirm_hold_required=True)
        self.action(target="overlay_back")
        self.wait_ui(overlay=40)
        cancelled = {"rules_before": before, "rules_after": read(self.app / "rules.json"),
                     "writes_before": writes, "writes_after": self.write_ids()}
        require(cancelled["rules_before"] == cancelled["rules_after"] and
                cancelled["writes_before"] == cancelled["writes_after"], "touch cancel applied calendar")
        save(self.output / "input-evidence/cancel.json", cancelled)
        before_ids = self.ids()
        self.action(button="A")
        self.wait_ui(overlay=3, confirm_hold_required=True)
        self.capture("hold-confirm")
        self.action(button="A", hold_ms=2500)
        result = self.wait_receipt("activate_holiday_calendar", before_ids)
        require(result["status"] == "ok", "held calendar activation rejected")
        self.wait_ui(overlay=40, waiting=False)
        self.action(button="B")
        self.action(button="B")
        self.wait_ui(overlay=0, plan_page=0)
        self.checked("input_cancel_hold", ["touch cancels held confirmation without writing",
                                           "controller holds once; one matching successful activation receipt"])

    def input_reopen(self) -> None:
        before, config = read(self.app / "rules.json"), read(self.app / "config.json")
        self.stop()
        self.start()
        self.wait_ui(view=0, overlay=0)
        require(read(self.app / "rules.json") == before and read(self.app / "config.json") == config,
                "reopen changed persisted preferences or rules")
        self.page(3)
        self.capture("input-preferences-reopened")
        self.page(2)
        self.action(target="card_1")
        self.wait_ui(overlay=22, pin_length=0)
        self.enter_pin("110")
        self.wait_ui(overlay=7)
        self.capture("optical-pairing-qr-reopened")
        self.checked("input_reopen", ["new NRO instance retains exact rules and language/theme preferences",
                                      "parent PIN and preference navigation still work after activation"])


class InputHoldFlow(InputFlow):
    """Complete hold/reopen coverage without replaying an already passed matrix."""

    def setup(self) -> None:
        super().setup()
        self.theme("amethyst", "dark")
