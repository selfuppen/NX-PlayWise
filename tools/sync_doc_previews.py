#!/usr/bin/env python3
"""Copy the documented UI scenes from a fresh host render into the guide."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]
PREVIEW_FILES = (
    "child/child-light.png",
    "parent/parent-dark.png",
    "parent/parent-details-decision-light.png",
    "parent/quick-add-confirm-dark.png",
    "setup/setup-step-1-light.png",
    "setup/setup-step-2-light.png",
    "setup/setup-step-3-light.png",
    "setup/setup-step-4-light.png",
    "setup/setup-step-5-light.png",
    "grant/grant-entry-light.png",
    "grant/grant-issued-light.png",
    "redeem/redeem-input-light.png",
    "redeem/redeem-confirm-light.png",
    "redeem/redeem-success-light.png",
    "plan/plan-root-light.png",
    "plan/plan-draft-today-light.png",
    "holiday/holiday-draft-light.png",
    "scheduled/scheduled-draft-light.png",
    "bedtime/bedtime-section-0-dark.png",
    "autonomy/autonomy-policy-light.png",
    "settings/settings-root-dark.png",
    "support/support-healthy-dark.png",
    "support/support-failed-light.png",
)


def synchronize(preview_dir: Path, docs_dir: Path, *, check: bool = False) -> int:
    """Require every selected scene; return the number of changed files."""
    missing = [name for name in PREVIEW_FILES if not (preview_dir / name).is_file()]
    if missing:
        raise FileNotFoundError("missing generated UI previews: " + ", ".join(missing))

    changed = 0
    for name in PREVIEW_FILES:
        source = preview_dir / name
        target = docs_dir / name
        if not target.is_file() or source.read_bytes() != target.read_bytes():
            if check:
                raise ValueError(f"documentation preview is stale or missing: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            changed += 1
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail when documented screenshots differ from generated scenes")
    args = parser.parse_args()
    changed = synchronize(ROOT / "build" / "ui-previews", ROOT / "docs" / "images" / "usage", check=args.check)
    print(f"PASS: {len(PREVIEW_FILES)} documented UI previews checked; {changed} updated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
