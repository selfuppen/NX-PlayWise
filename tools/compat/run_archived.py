#!/usr/bin/env python3
"""Opt-in old import names for an unchanged archived Python script.

This adapts imports only. It neither remaps old subprocess paths nor makes an
archived device script safe to replay; authorization and evidence rules apply.
"""
from __future__ import annotations

import argparse
import importlib
import importlib.abc
import importlib.util
from importlib.machinery import ModuleSpec
import json
from pathlib import Path
import runpy
import sys
from types import ModuleType
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]


class ArchivedImports(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def __init__(self) -> None:
        catalog = json.loads((ROOT / "tools/catalog.json").read_text(encoding="utf-8"))
        self.targets = {
            Path(item["path"]).stem: "tools." + item["path"][:-3].replace("/", ".")
            for item in catalog["scripts"] if item["path"].endswith(".py")
        }

    def find_spec(self, fullname: str, path: Sequence[str] | None = None,
                  target: ModuleType | None = None) -> ModuleSpec | None:
        if fullname in self.targets:
            return importlib.util.spec_from_loader(fullname, self)
        return None

    def create_module(self, spec: ModuleSpec) -> ModuleType | None:
        return None

    def exec_module(self, module: ModuleType) -> None:
        # Reuse the canonical module, including exception classes and globals.
        sys.modules[module.__name__] = importlib.import_module(self.targets[module.__name__])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script", type=Path)
    parser.add_argument("args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    script = args.script.resolve(strict=True)
    sys.path.insert(0, str(ROOT))
    sys.path.insert(0, str(script.parent))
    sys.meta_path.insert(0, ArchivedImports())
    sys.argv = [str(script), *args.args]
    runpy.run_path(str(script), run_name="__main__")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
