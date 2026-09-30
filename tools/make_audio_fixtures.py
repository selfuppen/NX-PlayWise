#!/usr/bin/env python3
"""Generate embedded Switch-style audio PCM fixtures for Companion NRO."""
from __future__ import annotations

import math
import struct
from pathlib import Path

SAMPLE_RATE = 48000

def generate_focus() -> list[int]:
    """Warm, gentle Switch-style micro-tick / dial wheel click (~24ms).
    
    Soft marimba/bubble tick with warm harmonic body and smooth cosine attack.
    Ideal for rapid navigation and dial wheel stepping without auditory fatigue.
    """
    duration = 0.024
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        progress = i / total_samples
        # Warm, organic pitch glide from ~1100 Hz down to ~720 Hz
        freq = 1100.0 * math.exp(-2.8 * progress) + 720.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        # Smooth cosine attack (1.5ms) followed by natural exponential decay
        if t < 0.0015:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.0015))
        else:
            env = math.exp(-125.0 * (t - 0.0015))
        # Warm harmonic richness: fundamental + warm 2nd harmonic + subtle body
        val = 0.78 * math.sin(phase) + 0.18 * math.sin(2.0 * phase) + 0.04 * math.sin(3.0 * phase)
        sample = int(val * env * 14000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_confirm() -> list[int]:
    """Warm, uplifting dual-tone chime (C6 -> E6, ~85ms).
    
    Switch-style pleasant acoustic chime with smooth attack, rich layered overtones
    and warm rounded resonance.
    """
    duration = 0.085
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        # Tone 1: C6 (1046.5 Hz) with warm harmonics
        freq1 = 1046.5
        phase1 += 2.0 * math.pi * freq1 / SAMPLE_RATE
        if t < 0.002:
            env1 = 0.5 * (1.0 - math.cos(math.pi * t / 0.002))
        else:
            env1 = math.exp(-38.0 * (t - 0.002))
        val1 = 0.72 * math.sin(phase1) + 0.22 * math.sin(2.0 * phase1) + 0.06 * math.sin(3.0 * phase1)

        # Tone 2: E6 (1318.5 Hz) starting at t = 18ms with subtle G6 shimmer
        t2 = t - 0.018
        if t2 >= 0:
            freq2 = 1318.5
            phase2 += 2.0 * math.pi * freq2 / SAMPLE_RATE
            if t2 < 0.002:
                env2 = 0.5 * (1.0 - math.cos(math.pi * t2 / 0.002))
            else:
                env2 = math.exp(-28.0 * (t2 - 0.002))
            val2 = 0.70 * math.sin(phase2) + 0.24 * math.sin(2.0 * phase2) + 0.06 * math.sin(3.0 * phase2)
        else:
            val2 = 0.0
            env2 = 0.0

        sample = int((val1 * env1 * 0.48 + val2 * env2 * 0.58) * 22000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_cancel() -> list[int]:
    """Gentle descending warm tone (G5 -> E5, ~70ms).
    
    Soft, rounded dismissal sound with cozy acoustic decay.
    """
    duration = 0.070
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        progress = i / total_samples
        # Gentle glide from G5 (784 Hz) to E5 (659 Hz)
        freq = 784.0 * (1.0 - 0.16 * progress)
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.002:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.002))
        else:
            env = math.exp(-34.0 * (t - 0.002))
        val = 0.82 * math.sin(phase) + 0.15 * math.sin(2.0 * phase) + 0.03 * math.sin(3.0 * phase)
        sample = int(val * env * 18000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_error() -> list[int]:
    """Polite, low-frequency double wood-tap warning (~115ms).
    
    Soft muted double bump without harsh distortion or aggressive buzz.
    """
    duration = 0.115
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        # Two soft, polite pulses
        in_pulse1 = (t < 0.040)
        in_pulse2 = (t >= 0.055 and t < 0.105)
        if not (in_pulse1 or in_pulse2):
            samples.append(0)
            continue
        pulse_t = t if in_pulse1 else (t - 0.055)
        # Soft pitch drop 280Hz -> 200Hz
        freq = 280.0 * math.exp(-8.0 * pulse_t) + 200.0
        if in_pulse1:
            phase1 += 2.0 * math.pi * freq / SAMPLE_RATE
            phase = phase1
        else:
            phase2 += 2.0 * math.pi * freq / SAMPLE_RATE
            phase = phase2

        if pulse_t < 0.0025:
            env = 0.5 * (1.0 - math.cos(math.pi * pulse_t / 0.0025))
        else:
            env = math.exp(-36.0 * (pulse_t - 0.0025))
        val = 0.76 * math.sin(phase) + 0.19 * math.sin(2.0 * phase) + 0.05 * math.sin(3.0 * phase)
        sample = int(val * env * 17000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_popup() -> list[int]:
    """Elegant crystalline notification bell (E6 + A6, ~110ms).
    
    Airy, clear modal chime with gentle sparkle decay.
    """
    duration = 0.110
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        # Bell 1: E6 (1318.5 Hz)
        freq1 = 1318.5
        phase1 += 2.0 * math.pi * freq1 / SAMPLE_RATE
        if t < 0.002:
            env1 = 0.5 * (1.0 - math.cos(math.pi * t / 0.002))
        else:
            env1 = math.exp(-28.0 * (t - 0.002))
        val1 = 0.75 * math.sin(phase1) + 0.20 * math.sin(2.0 * phase1) + 0.05 * math.sin(3.0 * phase1)

        # Bell 2: A6 (1760.0 Hz) starting at t = 20ms
        t2 = t - 0.020
        if t2 >= 0:
            freq2 = 1760.0
            phase2 += 2.0 * math.pi * freq2 / SAMPLE_RATE
            if t2 < 0.002:
                env2 = 0.5 * (1.0 - math.cos(math.pi * t2 / 0.002))
            else:
                env2 = math.exp(-22.0 * (t2 - 0.002))
            val2 = 0.78 * math.sin(phase2) + 0.18 * math.sin(2.0 * phase2) + 0.04 * math.sin(3.0 * phase2)
        else:
            val2 = 0.0
            env2 = 0.0

        sample = int((val1 * env1 * 0.45 + val2 * env2 * 0.55) * 21000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_success() -> list[int]:
    """Rich ascending major triad arpeggio (C6, E6, G6, C7, ~250ms).
    
    Warm, layered celebratory chime with natural harmonic sustain and sparkle.
    """
    duration = 0.250
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    notes = [
        (0.000, 1046.5, 28.0, 0.38), # C6
        (0.035, 1318.5, 24.0, 0.42), # E6
        (0.070, 1568.0, 20.0, 0.46), # G6
        (0.105, 2093.0, 12.0, 0.54), # C7 (lingering shimmering tail)
    ]
    phases = [0.0] * len(notes)
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        total_val = 0.0
        for n_idx, (start_t, freq, decay, weight) in enumerate(notes):
            if t >= start_t:
                dt = t - start_t
                phases[n_idx] += 2.0 * math.pi * freq / SAMPLE_RATE
                if dt < 0.0025:
                    env = 0.5 * (1.0 - math.cos(math.pi * dt / 0.0025))
                else:
                    env = math.exp(-decay * (dt - 0.0025))
                tone = 0.74 * math.sin(phases[n_idx]) + 0.20 * math.sin(2.0 * phases[n_idx]) + 0.06 * math.sin(3.0 * phases[n_idx])
                total_val += tone * env * weight
        sample = int(total_val * 21000.0)
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
