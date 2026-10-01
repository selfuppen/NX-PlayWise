#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import math
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from make_audio_fixtures import (  # noqa: E402
    SAMPLE_RATE,
    generate_focus,
    generate_step,
    generate_toggle,
    generate_keystroke,
    generate_tab,
    generate_confirm,
    generate_cancel,
    generate_popup,
    generate_danger,
    generate_success,
    generate_claim_buffer,
    generate_error,
    generate_hold_confirm,
)


def main() -> int:
    generators = [
        ("FOCUS", generate_focus()),
        ("STEP", generate_step()),
        ("TOGGLE", generate_toggle()),
        ("KEYSTROKE", generate_keystroke()),
        ("TAB", generate_tab()),
        ("CONFIRM", generate_confirm()),
        ("CANCEL", generate_cancel()),
        ("POPUP", generate_popup()),
        ("DANGER", generate_danger()),
        ("SUCCESS", generate_success()),
        ("CLAIM_BUFFER", generate_claim_buffer()),
        ("ERROR", generate_error()),
        ("HOLD_CONFIRM", generate_hold_confirm()),
    ]
    assert len(generators) == 13, f"expected 13 sounds, got {len(generators)}"
    for name, samples in generators:
        assert len(samples) > 0, f"{name} has no samples"
        for sample in samples:
            assert -32768 <= sample <= 32767, f"{name} sample out of bounds: {sample}"

    hold_samples = dict(generators)["HOLD_CONFIRM"]
    hold_rms = math.sqrt(sum(sample * sample for sample in hold_samples) / len(hold_samples))
    assert hold_rms >= 3000, f"HOLD_CONFIRM is too quiet: RMS={hold_rms:.1f}"

    header_path = ROOT / "companion" / "nro" / "ptc_audio_data.h"
    source_path = ROOT / "companion" / "nro" / "ptc_audio_data.c"
    assert header_path.exists(), "ptc_audio_data.h missing"
    assert source_path.exists(), "ptc_audio_data.c missing"
    assert header_path.stat().st_size > 0, "ptc_audio_data.h is empty"
    assert source_path.stat().st_size > 0, "ptc_audio_data.c is empty"
    audio_source = (ROOT / "companion" / "nro" / "ptc_audio.c").read_text(encoding="utf-8")
    main_source = (ROOT / "companion" / "nro" / "main.c").read_text(encoding="utf-8")
    assert "armDCacheFlush(g_audio_pcm_pools[buf_idx]" in audio_source, \
        "audout PCM data must be flushed before submission"
    assert "Never mutate a descriptor or PCM pool that audout still owns" in audio_source, \
        "audio queue must not overwrite a busy audout buffer"
    assert "ptc_audio_play(PTC_SE_HOLD_CONFIRM);" in main_source, \
        "completed danger holds must play the dedicated confirmation cue"
    assert "ptc_audio_play(PTC_SE_STEP);" in main_source, \
        "danger hold progress must play audio feedback during press-and-hold"

    print("PASS: audio fixtures verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
