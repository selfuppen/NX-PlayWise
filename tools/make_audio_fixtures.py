#!/usr/bin/env python3
"""Generate embedded Switch-style audio PCM fixtures for Companion NRO."""
from __future__ import annotations

import math
import struct
from pathlib import Path

SAMPLE_RATE = 48000

def generate_focus() -> list[int]:
    """Short crisp click (pitch glide 2200Hz -> 700Hz, ~25ms)."""
    duration = 0.025
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        progress = i / total_samples
        # Exponential pitch drop
        freq = 2200.0 * math.exp(-3.5 * progress) + 700.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        # Envelope: 1ms linear attack, fast exponential decay
        if t < 0.001:
            env = t / 0.001
        else:
            env = math.exp(-120.0 * (t - 0.001))
        # Sine wave with subtle harmonic
        val = 0.85 * math.sin(phase) + 0.15 * math.sin(2.0 * phase)
        sample = int(val * env * 24000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_confirm() -> list[int]:
    """Bright two-tone chime (C6 -> E6, ~85ms)."""
    duration = 0.085
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        # Tone 1: C6 (1046.5 Hz), start at 0
        freq1 = 1046.5
        phase1 += 2.0 * math.pi * freq1 / SAMPLE_RATE
        env1 = math.exp(-45.0 * t) if t >= 0 else 0.0
        val1 = 0.7 * math.sin(phase1) + 0.3 * math.sin(2.0 * phase1)

        # Tone 2: E6 (1318.5 Hz), start at 0.018s
        t2 = t - 0.018
        if t2 >= 0:
            freq2 = 1318.5
            phase2 += 2.0 * math.pi * freq2 / SAMPLE_RATE
            env2 = math.exp(-35.0 * t2)
            val2 = 0.75 * math.sin(phase2) + 0.25 * math.sin(2.0 * phase2)
        else:
            val2 = 0.0
            env2 = 0.0

        sample = int((val1 * env1 * 0.5 + val2 * env2 * 0.6) * 26000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_cancel() -> list[int]:
    """Gentle descending tone (880Hz -> 440Hz, ~70ms)."""
    duration = 0.070
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        progress = i / total_samples
        freq = 880.0 * (1.0 - 0.5 * progress)
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.003:
            env = t / 0.003
        else:
            env = math.exp(-40.0 * (t - 0.003))
        val = 0.9 * math.sin(phase) + 0.1 * math.sin(2.0 * phase)
        sample = int(val * env * 22000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_error() -> list[int]:
    """Double low buzz (220Hz + 330Hz, ~130ms)."""
    duration = 0.130
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        # Gap between 0.045 and 0.065
        in_pulse1 = (t < 0.045)
        in_pulse2 = (t >= 0.065 and t < 0.125)
        if not (in_pulse1 or in_pulse2):
            samples.append(0)
            continue
        pulse_t = t if in_pulse1 else (t - 0.065)
        env = math.exp(-15.0 * pulse_t)
        phase1 += 2.0 * math.pi * 220.0 / SAMPLE_RATE
        phase2 += 2.0 * math.pi * 330.0 / SAMPLE_RATE
        # Richer waveform with odd harmonics for buzz character
        val = 0.6 * math.sin(phase1) + 0.25 * math.sin(3.0 * phase1) + 0.3 * math.sin(phase2)
        sample = int(val * env * 20000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_popup() -> list[int]:
    """Bright notification bell (1568Hz + 2093Hz, ~110ms)."""
    duration = 0.110
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        phase1 += 2.0 * math.pi * 1568.0 / SAMPLE_RATE
        env1 = math.exp(-30.0 * t)
        val1 = 0.8 * math.sin(phase1) + 0.2 * math.sin(2.0 * phase1)

        t2 = t - 0.025
        if t2 >= 0:
            phase2 += 2.0 * math.pi * 2093.0 / SAMPLE_RATE
            env2 = math.exp(-25.0 * t2)
            val2 = 0.85 * math.sin(phase2) + 0.15 * math.sin(2.0 * phase2)
        else:
            val2 = 0.0
            env2 = 0.0

        sample = int((val1 * env1 * 0.45 + val2 * env2 * 0.55) * 25000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_success() -> list[int]:
    """Ascending major triad arpeggio (C6, E6, G6, C7, ~240ms)."""
    duration = 0.240
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    notes = [
        (0.000, 1046.5, 35.0, 0.4), # C6
        (0.040, 1318.5, 30.0, 0.45), # E6
        (0.080, 1568.0, 22.0, 0.5), # G6
        (0.120, 2093.0, 15.0, 0.55), # C7
    ]
    phases = [0.0] * len(notes)
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        total_val = 0.0
        for n_idx, (start_t, freq, decay, weight) in enumerate(notes):
            if t >= start_t:
                dt = t - start_t
                phases[n_idx] += 2.0 * math.pi * freq / SAMPLE_RATE
                env = math.exp(-decay * dt)
                tone = 0.8 * math.sin(phases[n_idx]) + 0.2 * math.sin(2.0 * phases[n_idx])
                total_val += tone * env * weight
        sample = int(total_val * 24000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def build_c_source(dest_header: Path, dest_source: Path) -> None:
    sound_generators = [
        ("FOCUS", generate_focus()),
        ("CONFIRM", generate_confirm()),
        ("CANCEL", generate_cancel()),
        ("ERROR", generate_error()),
        ("POPUP", generate_popup()),
        ("SUCCESS", generate_success()),
    ]

    header_lines = [
        "/* Auto-generated by tools/make_audio_fixtures.py - DO NOT EDIT MANUALLY */",
        "#ifndef PTC_AUDIO_DATA_H",
        "#define PTC_AUDIO_DATA_H",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "typedef struct {",
        "    const int16_t *samples;",
        "    size_t sample_count;",
        "} PtcAudioPcmClip;",
        "",
    ]
    for name, _ in sound_generators:
        header_lines.append(f"extern const int16_t g_ptc_audio_{name.lower()}_samples[];")
        header_lines.append(f"extern const size_t g_ptc_audio_{name.lower()}_sample_count;")
    header_lines.extend([
        "",
        "const PtcAudioPcmClip *ptc_audio_get_clip(int sound_id);",
        "",
        "#endif /* PTC_AUDIO_DATA_H */",
        "",
    ])

    source_lines = [
        "/* Auto-generated by tools/make_audio_fixtures.py - DO NOT EDIT MANUALLY */",
        '#include "ptc_audio_data.h"',
        '#include "ptc_audio.h"',
        "",
    ]
    for name, samples in sound_generators:
        source_lines.append(f"const int16_t g_ptc_audio_{name.lower()}_samples[{len(samples)}] = {{")
        # Format 12 integers per line
        for chunk_idx in range(0, len(samples), 12):
            chunk = samples[chunk_idx:chunk_idx+12]
            source_lines.append("    " + ", ".join(f"{s}" for s in chunk) + ",")
        source_lines.append("};")
        source_lines.append(f"const size_t g_ptc_audio_{name.lower()}_sample_count = {len(samples)};")
        source_lines.append("")

    source_lines.extend([
        "const PtcAudioPcmClip *ptc_audio_get_clip(int sound_id)",
        "{",
        "    static const PtcAudioPcmClip clips[] = {",
        "        {NULL, 0}, /* PTC_SE_NONE */",
        "        {g_ptc_audio_focus_samples, sizeof(g_ptc_audio_focus_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_confirm_samples, sizeof(g_ptc_audio_confirm_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_cancel_samples, sizeof(g_ptc_audio_cancel_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_error_samples, sizeof(g_ptc_audio_error_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_popup_samples, sizeof(g_ptc_audio_popup_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_success_samples, sizeof(g_ptc_audio_success_samples) / sizeof(int16_t)},",
        "    };",
        "    if (sound_id <= PTC_SE_NONE || sound_id > PTC_SE_SUCCESS) {",
        "        return NULL;",
        "    }",
        "    return &clips[sound_id];",
        "}",
        "",
    ])

    dest_header.write_text("\n".join(header_lines), encoding="utf-8")
    dest_source.write_text("\n".join(source_lines), encoding="utf-8")
    print(f"Generated {dest_header} and {dest_source}")

if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    nro_dir = root / "companion" / "nro"
    build_c_source(nro_dir / "ptc_audio_data.h", nro_dir / "ptc_audio_data.c")
