from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import sys
import json
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.automation.eden_input_flow import INPUT_SCENARIOS, INPUT_HOLD_SCENARIOS, STYLES, LANGUAGES, verify_matrix, load_matrix
from tools.automation.eden_runner import GateError, scenario_names


def rejected(rows: list, language: str) -> None:
    try:
        verify_matrix(rows, language)
        subset = [row for row in rows if row['style'] in ('teal', 'amethyst')]
        verify_matrix(subset, language, ('teal', 'amethyst'))
        rejected(subset, language)
    except GateError:
        return
    raise AssertionError("incomplete/changed input matrix accepted")


def main() -> None:
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
    print("PASS: X05 complete language/theme matrix and discard/no-write failure gates")


if __name__ == "__main__":
    main()
