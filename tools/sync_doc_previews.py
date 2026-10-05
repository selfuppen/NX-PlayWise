#!/usr/bin/env python3
"""Copy the documented UI scenes from a fresh host render into the guide."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]
PREVIEW_FILES = (
    "child/child-light.png",
    "child/child-details-light.png",
    "child/child-buffer-claimed-light.png",
    "parent/parent-dark.png",
    "parent/parent-details-decision-light.png",
    "parent/parent-details-usage-light.png",
    "parent/quota-editor-light.png",
    "parent/quota-positive-confirm-light.png",
    "parent/quick-add-confirm-dark.png",
    "parent/unlimited-confirm-dark.png",
    "setup/setup-step-1-light.png",
    "setup/setup-pctl-help-light.png",
    "setup/setup-step-2-light.png",
    "setup/setup-step-3-light.png",
    "grant/grant-entry-light.png",
    "grant/grant-duration-editor-light.png",
    "grant/grant-issued-light.png",
    "redeem/redeem-input-light.png",
    "redeem/redeem-confirm-light.png",
    "redeem/redeem-confirm-hold-dark.png",
    "redeem/redeem-success-light.png",
    "plan/plan-root-light.png",
    "plan/plan-draft-today-light.png",
    "plan/plan-minute-editor-light.png",
    "plan/weekly-confirm-change-light.png",
    "holiday/holiday-draft-light.png",
    "holiday/holiday-makeup-duration-editor-light.png",
    "holiday/holiday-confirm-light.png",
    "scheduled/scheduled-draft-light.png",
    "scheduled/scheduled-duration-editor-light.png",
    "bedtime/bedtime-section-0-dark.png",
    "bedtime/bedtime-section-1-dark.png",
    "bedtime/bedtime-section-2-dark.png",
    "bedtime/bedtime-window-input-dark.png",
    "bedtime/bedtime-time-editor-dark.png",
    "bedtime/bedtime-special-custom-dark.png",
    "autonomy/autonomy-policy-light.png",
    "settings/settings-root-dark.png",
    "settings/settings-pin-dark.png",
    "settings/settings-theme-dark.png",
    "settings/settings-language-dark.png",
    "settings/settings-shortcut-dark.png",
    "settings/settings-album-dark.png",
    "settings/settings-activity-dark.png",
    "settings/settings-redemption-dark.png",
    "settings/settings-grant-manager-dark.png",
    "settings/settings-software-info-dark.png",
    "overlay/overlay-code-entry.png",
    "overlay/overlay-code-confirm.png",
    "overlay/overlay-code-success.png",
    "overlay/overlay-parent-actions.png",
    "overlay/overlay-bedtime.png",
    "overlay/overlay-eye-care.png",
    "overlay/overlay-unknown.png",
    "support/support-guide-1-light.png",
    "support/support-guide-2-light.png",
    "support/support-guide-3-light.png",
    "support/support-guide-4-light.png",
    "support/support-healthy-dark.png",
    "support/support-failed-light.png",
    "support/support-event-details-light.png",
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
    changed_zh = synchronize(ROOT / "build" / "ui-previews", ROOT / "docs" / "images" / "usage", check=args.check)
    changed_en = synchronize(ROOT / "build" / "ui-previews" / "en", ROOT / "docs" / "images" / "usage-en", check=args.check)
    changed = changed_zh + changed_en
    print(f"PASS: {len(PREVIEW_FILES) * 2} documented UI previews checked ({len(PREVIEW_FILES)} zh + {len(PREVIEW_FILES)} en); {changed} updated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
