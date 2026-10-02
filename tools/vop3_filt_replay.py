#!/usr/bin/env python3
"""Replay VOP3-1 (the per-voice filter) end to end against a recording: the filtered output the unit made from a
known input, replayed on the interpreter.

    python tools/vop3_filt_replay.py

The take is FS1R.unlock captures/2026-09-30-10 (FS1R.unlock session 9). It ran channel 4's filter program with the
operator on, and recorded, per segment: `ref` (the shipped program: the filtered voice) and `op5s1` (the same
program with the output stage's op code changed to 5, which the session-9 write-up reads as "outputs the INPUT
rather than the filtered signal"). So `op5s1` is the chip's own input to this channel, measured, and `ref` is what
the program made of it - the pair a replay needs.

What this settles: whether the interpreter's VOP3-1 path (program, banks, class, the sum bus, the input cell)
reproduces the unit's filter from the unit's own input. No register is involved: the input goes in as the chip's
audio input, as on VOP3-2.
"""
import csv
import json
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vop3_interp as V  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
TAKE = ROOT / "FS1R.unlock/captures/2026-09-30-10"
ROM = ROOT / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
PROG0, COEF0 = 0x00375E1A, 0x0037541A     # VOP3-1 variant 0, step*10 + k*2 -> reg 10-k


def decode(flac):
    import soundfile as sf
    x, sr = sf.read(str(flac), dtype="float64")
    if x.ndim > 1:
        x = x.mean(1)
    return x * 8, sr


def first_sound(x, sr, thresh=-90.0):
    b = sr // 100
    n = len(x) // b
    db = 20 * np.log10(np.sqrt((x[:n * b].reshape(n, b) ** 2).mean(1)) + 1e-12)
    i = int(np.argmax(db > thresh))
    return i * b / sr


def program(first, n):
    rom = ROM.read_bytes()
    raw = rom[PROG0 - 0x200000 + first * 10: PROG0 - 0x200000 + (first + n) * 10]
    coef = rom[COEF0 - 0x200000 + first * 2: COEF0 - 0x200000 + (first + n) * 2]
    return [struct.unpack_from(">5H", raw, i * 10) for i in range(n)], list(struct.unpack(">%dH" % n, coef))


def main():
    s = json.loads((TAKE / "session.json").read_text())
    probe = s["results"]["probe"]
    first = int(probe["first_step"], 16)
    n = 0x7C                                   # the group the channel's cutoff step sits in: 0x108..0x183
    steps, coef = program(first, n)
    # the firmware's own patch of this channel's type/gain step is in session.json; apply it
    for k, v in probe.get("firmware_patched_steps", {}).items():
        st = int(k, 16) - first
        if 0 <= st < n:
            steps[st] = tuple(int(w, 16) for w in v.split("->")[1].split())
    x, sr = decode(next(TAKE.glob("*.flac")))
    t_on = probe["note_on_t"]
    off = first_sound(x, sr) - t_on
    seg = {r["name"]: r for r in probe["segments"]}

    def cut(name, pad=0.25):
        r = seg[name]
        a = int((r["t"] + off + pad) * sr)
        b = int((r["t"] + off + r["hold"] - 0.15) * sr)
        return x[a:b]

    src, dst = cut("op5s1"), cut("ref")
    m = min(len(src), len(dst), 20000)
    src, dst = src[:m], dst[:m]
    print(f"take: {TAKE.name}  channel {probe['channel']}  group {first:#05x}..{first + n - 1:#05x}")
    print(f"input  segment op5s1: {m} samples, rms {src.std():.5f} ({20 * np.log10(src.std() + 1e-12):.1f} dBFS)")
    print(f"output segment ref  : rms {dst.std():.5f} ({20 * np.log10(dst.std() + 1e-12):.1f} dBFS)")

    best = None
    for cell in (1, 5):                        # the cells the model fills with the chip's audio input
        it = V.Interp(steps, coef, bank_of=lambda _: 0)
        it.offs = {}
        out = []
        for i in range(m + 8):
            v = src[i] if i < m else 0.0
            out.append(it.sample(inp=v))
        out = np.array(out)[:m]
        if out.std() < 1e-12:
            print(f"  cell {cell}: model output is silent")
            continue
        g = (out @ dst) / (out @ out)
        r = dst - g * out
        c = np.corrcoef(dst, out)[0, 1]
        print(f"  cell {cell}: model rms {out.std():.5f}  best gain {g:.4f}  corr {c:.5f}  "
              f"err rms {r.std():.5f} ({(20 * np.log10(r.std() / (dst.std() + 1e-12))):.1f} dB below the unit)")
        if best is None or r.std() < best[0]:
            best = (r.std(), cell, g, c)
    if best:
        print(f"best: cell {best[1]}, gain {best[2]:.4f}, corr {best[3]:.5f}")


if __name__ == "__main__":
    main()
