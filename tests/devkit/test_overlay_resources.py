from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("overlay_catalog", ROOT / "tools/build/overlay_catalog.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
assert module.OUTPUT.read_text(encoding="utf-8") == module.generate(), "Overlay catalog stale"
assert module.generate() == module.generate(), "Catalog must be deterministic"
print("Overlay catalog dependency closure and generated contents verified")
