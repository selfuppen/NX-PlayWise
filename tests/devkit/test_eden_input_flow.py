from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import sys
import json
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.automation.eden_input_flow import (
    INPUT_SCENARIOS, INPUT_HOLD_SCENARIOS, STYLES, LANGUAGES, InputFlow, InputHoldFlow, verify_matrix, load_matrix,
)
from tools.automation.eden_runner import GateError, save, scenario_names


def rejected(rows: list, language: str) -> None:
    try:
        verify_matrix(rows, language)
        subset = [row for row in rows if row['style'] in ('teal', 'amethyst')]
        verify_matrix(subset, language, ('teal', 'amethyst'))
        rejected(subset, language)
    except GateError:
        return
    raise AssertionError("incomplete/changed input matrix accepted")


def test_preference_navigation() -> None:
    class SetupBase:
        def setup(self) -> None:
            self.calls.append(("setup",))

    for flow_type in (InputFlow, InputHoldFlow):
        for language in LANGUAGES:
            class Flow(flow_type, SetupBase):
                def page(self, index: int) -> None:
                    self.calls.append(("page", index))

                def action(self, **kwargs: object) -> None:
                    self.calls.append(("action", kwargs))

                def wait_ui(self, **expected: object) -> dict:
                    self.calls.append(("wait", expected))
                    if expected.get("overlay") == 21:
                        self.theme_waits += 1
                        return {"ui": {"overlay_selection": 0 if self.theme_waits % 2 else 3}}
                    return {"ui": expected}

                def enter_pin(self, digits: str) -> None:
                    assert digits == "110"
                    self.calls.append(("pin",))

                def capture(self, name: str) -> None:
                    self.calls.append(("capture", name))

            with tempfile.TemporaryDirectory() as folder:
                flow = Flow()
                flow.app = Path(folder)
                flow.calls, flow.theme_waits = [], 0
                flow.identity = {"input_language": language}
                save(flow.app / "config.json", {"ui_language": language, "theme": "dark", "theme_style": "amethyst"})
                flow.setup()
                prefix = [("setup",), ("page", 0), ("action", {"target": "header_language"}),
                          ("wait", {"overlay": 37, "overlay_selection": 0})]
                buttons = ["Right"] if language == "zh-Hans" else ["Down"] + (["Right"] if language == "en" else [])
                prefix += [("action", {"button": button}) for button in buttons + ["A"]]
                prefix += [("wait", {"overlay": 0})]
                assert flow.calls[:len(prefix)] == prefix, (flow_type.__name__, language, flow.calls)
                assert not any(call == ("action", {"target": "card_3"}) for call in flow.calls)
                if flow_type is InputHoldFlow:
                    assert ("action", {"target": "card_5"}) in flow.calls

                # Exercise the shared theme method itself, not a replaced mock.
                flow.calls, flow.theme_waits = [], 0
                flow.theme("amethyst", "dark")
                assert flow.calls == [
                    ("page", 3), ("action", {"target": "card_5"}), ("wait", {"overlay": 21}),
                    ("action", {"button": "Right"}), ("action", {"button": "Right"}),
                    ("action", {"button": "Down"}), ("wait", {"overlay": 21}),
                    ("action", {"button": "Right"}), ("action", {"button": "Right"}),
                    ("action", {"button": "Right"}), ("action", {"button": "A"}), ("wait", {"overlay": 0}),
                ]


def main() -> None:
    test_preference_navigation()
    assert scenario_names("input") == INPUT_SCENARIOS
    assert scenario_names("input-hold") == INPUT_HOLD_SCENARIOS
    for language in LANGUAGES:
        rows = [{"style": style, "theme": mode,
                 "config": {"ui_language": language, "theme_style": style, "theme": mode},
                 "rules_before": {"bedtime_enabled": False}, "rules_after": {"bedtime_enabled": False},
                 "writes_before": ["setup"], "writes_after": ["setup"]}
                for style in STYLES for mode in ("light", "dark")]
        verify_matrix(rows, language)
        rejected(rows[:-1], language)
        rejected(rows[:-1] + [rows[0]], language)
        for field, changed in (("rules_after", {}), ("writes_after", ["setup", "unexpected"])):
            bad = deepcopy(rows)
            bad[0][field] = changed
            rejected(bad, language)
        bad = deepcopy(rows)
        bad[0]["config"]["theme_style"] = "teal"
        rejected(bad, language)
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "matrix.json"
            for document in (rows, {"schema_version": 1, "rows": rows}):
                target.write_text(json.dumps(document), encoding="utf-8")
                assert load_matrix(target) == rows
                verify_matrix(load_matrix(target), language)
            for document in ('{"schema_version":1,"rows":[],"rows":[]}',
                             '{"schema_version":true,"rows":[]}', '{"schema_version":2,"rows":[]}'):
                target.write_text(document, encoding="utf-8")
                try:
                    load_matrix(target)
                except GateError:
                    continue
                raise AssertionError("duplicate/invalid matrix schema accepted")
    print("PASS: X05 input/hold preference navigation, language/theme matrix and discard/no-write gates")


if __name__ == "__main__":
    main()
