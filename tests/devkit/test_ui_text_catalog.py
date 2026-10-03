from __future__ import annotations

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
COMPANION = ROOT / "companion"
KEY_ROW = re.compile(r'^PTC_UI_TEXT_KEY\((PTC_UI_T_[A-Z0-9_]+)\)$')
VALUE_ROW = re.compile(r'^PTC_UI_TEXT_VALUE\((PTC_UI_T_[A-Z0-9_]+),\s*("(?:\\.|[^"\\])*")\)$')
PARAM = re.compile(r"\{([a-z][a-z_0-9]*)\}")
PRINTF = re.compile(r"%(?:\d+\$)?[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|h|l|z|t)?([diuoxXfFeEgGaAcsp%])")
QUOTED = re.compile(r'"(?:\\.|[^"\\])*"')
INCLUDE_ROW = re.compile(r'^#include "ui_text/([a-z_]+)/([a-z_]+)\.inc"$')


def catalog_files(name: str, directory: str) -> list[tuple[str, Path]]:
    files = []
    for line in (COMPANION / name).read_text(encoding="utf-8").splitlines():
        match = INCLUDE_ROW.fullmatch(line)
        assert match and match.group(1) == directory, f"malformed catalog include: {name}: {line}"
        module = match.group(2)
        path = COMPANION / "ui_text" / directory / f"{module}.inc"
        assert path.is_file() and path.read_text(encoding="utf-8").strip(), f"empty catalog module: {path}"
        files.append((module, path))
    modules = [module for module, _ in files]
    assert modules and len(modules) == len(set(modules)), f"duplicate catalog module in {name}"
    assert {path for _, path in files} == set((COMPANION / "ui_text" / directory).glob("*.inc")), (
        f"unlisted catalog module in {directory}"
    )
    return files


def main() -> None:
    keys = []
    key_files = catalog_files("ui_text_keys.inc", "keys")
    module_keys = {}
    for module, path in key_files:
        module_keys[module] = []
        for line in path.read_text(encoding="utf-8").splitlines():
            match = KEY_ROW.fullmatch(line)
            assert match, f"malformed text key: {path}: {line[:80]}"
            module_keys[module].append(match.group(1))
            keys.append(match.group(1))
    assert keys and len(keys) == len(set(keys)), "text keys must be present and unique"
    locales = []
    for name in ("zh_hans", "zh_hant", "en"):
        values = {}
        files = catalog_files(f"ui_text_{name}.inc", name)
        assert [module for module, _ in files] == list(module_keys), f"{name} module order mismatch"
        for module, path in files:
            file_keys = []
            for line in path.read_text(encoding="utf-8").splitlines():
                match = VALUE_ROW.fullmatch(line)
                assert match, f"malformed {name} text row: {path}: {line[:80]}"
                key = match.group(1)
                assert key not in values, f"duplicate {name} text key: {key}"
                values[key] = json.loads(match.group(2))
                file_keys.append(key)
            assert file_keys == module_keys[module], f"{name}/{module} keys differ from catalog"
        assert list(values) == keys, f"missing, extra, or reordered {name} text key"
        locales.append(values)
    for key in keys:
        values = [locale[key] for locale in locales]
        assert all(values), f"missing translation: {key}"
        parameters = [set(PARAM.findall(value)) for value in values]
        assert parameters[0] == parameters[1] == parameters[2], f"parameter mismatch: {key}"
        signatures = [tuple(kind for kind in PRINTF.findall(value) if kind != "%") for value in values]
        assert signatures[0] == signatures[1] == signatures[2], f"printf parameter mismatch: {key}: {signatures}"
        for value in values:
            assert value.count("{") == value.count("}") == len(PARAM.findall(value)), (
                f"invalid parameter syntax: {key}"
            )
    language_code = (COMPANION / "ui_language.c").read_text(encoding="utf-8")
    assert "ui_language_en.inc" not in language_code and "ui_language_map.inc" not in language_code, (
        "display must not depend on exact Chinese phrase matches or character conversion"
    )
    sources = list(COMPANION.glob("*.c")) + list((COMPANION / "nro").glob("*.c"))
    sources += list((COMPANION / "overlay").glob("*.c"))
    sources += list((COMPANION / "overlay/source").glob("*.cpp"))
    sources.append(ROOT / "tests/ui_preview/render.c")
    for path in sources:
        source = path.read_text(encoding="utf-8")
        assert not re.search(r"\bif\s*\(\s*is_en\s*\)", source), (
            f"language-specific display branch in {path.relative_to(ROOT)}"
        )
        for literal in QUOTED.findall(source):
            if re.search(r"[\u3400-\u9fff]", literal):
                assert path.name == "ui_render_overlay_plan.c" and literal in (
                    '"无"', '"日"', '"%u月%u日"'
                ), f"unkeyed Chinese display literal in {path.relative_to(ROOT)}: {literal[:80]}"
    print(f"PASS: {len(keys)} keyed UI messages; no legacy Chinese display lookup")


if __name__ == "__main__":
    main()
