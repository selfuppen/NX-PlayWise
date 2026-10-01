#!/usr/bin/env python3
"""Generate embedded Switch-style audio PCM fixtures for Companion NRO."""
from __future__ import annotations

import math
import struct
from pathlib import Path

SAMPLE_RATE = 48000

def generate_focus() -> list[int]:
    """Scheme A: Warm, gentle rosewood / marimba micro-tap (~26ms).
    
    Subtle warm descent (460Hz -> 340Hz) with smooth cosine attack and natural
    woody body resonance. Optimized for rapid list navigation without fatigue.
    """
    duration = 0.026
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        prog = i / total_samples
        freq = 460.0 * math.exp(-1.5 * prog) + 120.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.0022:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.0022))
        else:
            env = math.exp(-88.0 * (t - 0.0022))
        val = 0.84 * math.sin(phase) + 0.13 * math.sin(2.0 * phase) + 0.03 * math.sin(3.0 * phase)
        sample = int(val * env * 15000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_step() -> list[int]:
    """Micro-gear step tick for dial wheel and value increments (+/-5m, date) (~14ms).
    
    Crisp, pleasant tactile micro-click with ultra-fast damping.
    """
    duration = 0.014
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        prog = i / total_samples
        freq = 580.0 * math.exp(-1.2 * prog) + 160.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.0010:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.0010))
        else:
            env = math.exp(-190.0 * (t - 0.0010))
        val = 0.88 * math.sin(phase) + 0.12 * math.sin(2.0 * phase)
        sample = int(val * env * 12500.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_toggle() -> list[int]:
    """Soft dual-elastic switch / mode flip sound (~36ms).
    
    Used for on/off switches, rule mode toggle (limit/unlimited), and master toggle.
    """
    duration = 0.036
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        prog = i / total_samples
        freq = 400.0 + 250.0 * math.sin(0.5 * math.pi * prog)
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.0015:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.0015))
        else:
            env = math.exp(-90.0 * (t - 0.0015))
        val = 0.78 * math.sin(phase) + 0.20 * math.sin(2.0 * phase) + 0.02 * math.sin(3.0 * phase)
        sample = int(val * env * 16000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_keystroke() -> list[int]:
    """Soft typewriter key tap for PIN digits, numpad keys and backspace (~18ms)."""
    duration = 0.018
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        prog = i / total_samples
        freq = 500.0 * math.exp(-1.8 * prog) + 180.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.0012:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.0012))
        else:
            env = math.exp(-130.0 * (t - 0.0012))
        val = 0.85 * math.sin(phase) + 0.15 * math.sin(2.0 * phase)
        sample = int(val * env * 14000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_tab() -> list[int]:
    """Smooth lateral glide chime for top-level page and section tab switches (~45ms)."""
    duration = 0.045
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        prog = i / total_samples
        freq1 = 660.0 * (1.0 - 0.12 * prog)
        phase1 += 2.0 * math.pi * freq1 / SAMPLE_RATE
        if t < 0.0020:
            env1 = 0.5 * (1.0 - math.cos(math.pi * t / 0.0020))
        else:
            env1 = math.exp(-65.0 * (t - 0.0020))
        val1 = 0.75 * math.sin(phase1) + 0.25 * math.sin(2.0 * phase1)

        t2 = t - 0.008
        if t2 >= 0:
            freq2 = 880.0 * (1.0 - 0.10 * (i / total_samples))
            phase2 += 2.0 * math.pi * freq2 / SAMPLE_RATE
            if t2 < 0.0020:
                env2 = 0.5 * (1.0 - math.cos(math.pi * t2 / 0.0020))
            else:
                env2 = math.exp(-55.0 * (t2 - 0.0020))
            val2 = 0.80 * math.sin(phase2) + 0.20 * math.sin(2.0 * phase2)
        else:
            val2 = 0.0
            env2 = 0.0

        sample = int((val1 * env1 * 0.52 + val2 * env2 * 0.48) * 17000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_confirm() -> list[int]:
    """Warm, uplifting dual-tone chime (C6 -> E6, ~85ms)."""
    duration = 0.085
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        freq1 = 1046.5
        phase1 += 2.0 * math.pi * freq1 / SAMPLE_RATE
        if t < 0.002:
            env1 = 0.5 * (1.0 - math.cos(math.pi * t / 0.002))
        else:
            env1 = math.exp(-38.0 * (t - 0.002))
        val1 = 0.72 * math.sin(phase1) + 0.22 * math.sin(2.0 * phase1) + 0.06 * math.sin(3.0 * phase1)

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
    """Gentle descending warm tone (G5 -> E5, ~70ms)."""
    duration = 0.070
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        prog = i / total_samples
        freq = 784.0 * (1.0 - 0.16 * prog)
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        if t < 0.002:
            env = 0.5 * (1.0 - math.cos(math.pi * t / 0.002))
        else:
            env = math.exp(-34.0 * (t - 0.002))
        val = 0.82 * math.sin(phase) + 0.15 * math.sin(2.0 * phase) + 0.03 * math.sin(3.0 * phase)
        sample = int(val * env * 18000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_popup() -> list[int]:
    """Elegant crystalline notification bell (E6 + A6, ~110ms)."""
    duration = 0.110
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        freq1 = 1318.5
        phase1 += 2.0 * math.pi * freq1 / SAMPLE_RATE
        if t < 0.002:
            env1 = 0.5 * (1.0 - math.cos(math.pi * t / 0.002))
        else:
            env1 = math.exp(-28.0 * (t - 0.002))
        val1 = 0.75 * math.sin(phase1) + 0.20 * math.sin(2.0 * phase1) + 0.05 * math.sin(3.0 * phase1)

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

def generate_danger() -> list[int]:
    """Deep, solemn resonant triad chord for danger/emergency dialogs (~140ms)."""
    duration = 0.140
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    notes = [
        (0.000, 349.23, 32.0, 0.38), # F4
        (0.010, 440.00, 30.0, 0.35), # A4
        (0.020, 523.25, 26.0, 0.32), # C5
    ]
    phases = [0.0] * len(notes)
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        total_val = 0.0
        for n_idx, (start_t, freq, decay, weight) in enumerate(notes):
            if t >= start_t:
                dt = t - start_t
                phases[n_idx] += 2.0 * math.pi * freq / SAMPLE_RATE
                if dt < 0.003:
                    env = 0.5 * (1.0 - math.cos(math.pi * dt / 0.003))
                else:
                    env = math.exp(-decay * (dt - 0.003))
                tone = 0.78 * math.sin(phases[n_idx]) + 0.18 * math.sin(2.0 * phases[n_idx]) + 0.04 * math.sin(3.0 * phases[n_idx])
                total_val += tone * env * weight
        sample = int(total_val * 20000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_success() -> list[int]:
    """Rich ascending major triad arpeggio (C6, E6, G6, C7, ~250ms)."""
    duration = 0.250
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    notes = [
        (0.000, 1046.5, 28.0, 0.38), # C6
        (0.035, 1318.5, 24.0, 0.42), # E6
        (0.070, 1568.0, 20.0, 0.46), # G6
        (0.105, 2093.0, 12.0, 0.54), # C7
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

def generate_claim_buffer() -> list[int]:
    """Joyful, sparkly major chord burst for claiming daily buffer (~140ms)."""
    duration = 0.140
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    notes = [
        (0.000, 783.99, 36.0, 0.28),  # G5
        (0.012, 1046.5, 30.0, 0.32),  # C6
        (0.024, 1318.5, 26.0, 0.36),  # E6
        (0.036, 1567.98, 20.0, 0.42), # G6
    ]
    phases = [0.0] * len(notes)
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        total_val = 0.0
        for n_idx, (start_t, freq, decay, weight) in enumerate(notes):
            if t >= start_t:
                dt = t - start_t
                phases[n_idx] += 2.0 * math.pi * freq / SAMPLE_RATE
                if dt < 0.0020:
                    env = 0.5 * (1.0 - math.cos(math.pi * dt / 0.0020))
                else:
                    env = math.exp(-decay * (dt - 0.0020))
                tone = 0.76 * math.sin(phases[n_idx]) + 0.19 * math.sin(2.0 * phases[n_idx]) + 0.05 * math.sin(3.0 * phases[n_idx])
                total_val += tone * env * weight
        sample = int(total_val * 20000.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def generate_error() -> list[int]:
    """Polite, low-frequency double wood-tap warning (~115ms)."""
    duration = 0.115
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        in_pulse1 = (t < 0.040)
        in_pulse2 = (t >= 0.055 and t < 0.105)
        if not (in_pulse1 or in_pulse2):
            samples.append(0)
            continue
        pulse_t = t if in_pulse1 else (t - 0.055)
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

def generate_hold_confirm() -> list[int]:
    """Rich, powerful resonant lock-in chime for hold-to-confirm completion (~210ms).
    
    Features a deep tactile sub-bass foundation (160Hz -> 80Hz) followed by an authoritative
    ascending crystalline power chime (C5 523.25Hz -> G5 783.99Hz -> C6 1046.5Hz) with rich decay.
    """
    duration = 0.210
    total_samples = int(SAMPLE_RATE * duration)
    samples: list[int] = []
    bass_phase = 0.0
    notes = [
        (0.015, 523.25, 22.0, 0.35),   # C5
        (0.040, 783.99, 18.0, 0.38),   # G5
        (0.065, 1046.50, 14.0, 0.45),  # C6
        (0.090, 1567.98, 12.0, 0.22),  # G6 shimmer overtone
    ]
    phases = [0.0] * len(notes)
    for i in range(total_samples):
        t = i / SAMPLE_RATE
        total_val = 0.0

        if t < 0.045:
            bass_prog = t / 0.045
            bass_freq = 160.0 * math.exp(-2.0 * bass_prog) + 70.0
            bass_phase += 2.0 * math.pi * bass_freq / SAMPLE_RATE
            if t < 0.003:
                bass_env = 0.5 * (1.0 - math.cos(math.pi * t / 0.003))
            else:
                bass_env = math.exp(-45.0 * (t - 0.003))
            total_val += math.sin(bass_phase) * bass_env * 0.40

        for n_idx, (start_t, freq, decay, weight) in enumerate(notes):
            if t >= start_t:
                dt = t - start_t
                phases[n_idx] += 2.0 * math.pi * freq / SAMPLE_RATE
                if dt < 0.0025:
                    env = 0.5 * (1.0 - math.cos(math.pi * dt / 0.0025))
                else:
                    env = math.exp(-decay * (dt - 0.0025))
                tone = 0.76 * math.sin(phases[n_idx]) + 0.19 * math.sin(2.0 * phases[n_idx]) + 0.05 * math.sin(3.0 * phases[n_idx])
                total_val += tone * env * weight

        sample = int(total_val * 21500.0)
        samples.append(max(-32767, min(32767, sample)))
    return samples

def build_c_source(dest_header: Path, dest_source: Path) -> None:
    sound_generators = [
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
        "        {g_ptc_audio_step_samples, sizeof(g_ptc_audio_step_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_toggle_samples, sizeof(g_ptc_audio_toggle_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_keystroke_samples, sizeof(g_ptc_audio_keystroke_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_tab_samples, sizeof(g_ptc_audio_tab_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_confirm_samples, sizeof(g_ptc_audio_confirm_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_cancel_samples, sizeof(g_ptc_audio_cancel_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_popup_samples, sizeof(g_ptc_audio_popup_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_danger_samples, sizeof(g_ptc_audio_danger_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_success_samples, sizeof(g_ptc_audio_success_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_claim_buffer_samples, sizeof(g_ptc_audio_claim_buffer_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_error_samples, sizeof(g_ptc_audio_error_samples) / sizeof(int16_t)},",
        "        {g_ptc_audio_hold_confirm_samples, sizeof(g_ptc_audio_hold_confirm_samples) / sizeof(int16_t)},",
        "    };",
        "    if (sound_id <= PTC_SE_NONE || sound_id > PTC_SE_HOLD_CONFIRM) {",
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
