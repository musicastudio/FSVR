#!/usr/bin/env python3
"""Every dynamic take on disk against the C++ core, one table: the rig loop's scoreboard.

    python tools/vop3_board.py [-D NAME ...]     (-D compiles a rule variant behind #ifdef in vop3_core.h)

Per take and channel: the first sample where model and unit differ by more than 1 LSB, and the error rms over the take
relative to the unit's rms. A rule variant is better only if no take's first-bad moves earlier.
Takes: FS1R.unlock s25 folders with step_<name>.npy, paired with their probes.json; the slot offsets come from
the take's own session.json (64 slots before s34; the upper 64 then come from the s34 dump, which matched the
first 64 exactly on Hall1 and Hall9).
"""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vop3_cpp as C  # noqa: E402

CAP = Path(__file__).resolve().parents[2] / "FS1R.unlock/captures"
FULL = json.loads((CAP / "2026-10-02-035714-s25/session.json").read_text())["results"]["probe"]["loads"]
LSB = 1.5 / 16384


def onset(a):
    d = np.abs(a - a[0]).max(1) > 1e-5
    return int(np.argmax(d)) if d.any() else -1


def board(defs=(), n=6000, only=None):
    rows = []
    for d in sorted(CAP.glob("*-s25")):
        if only and d.name not in only:
            continue
        if not list(d.glob("step_*.npy")) or not (d / "session.json").exists():
            continue
        spec = json.loads((d / "probes.json").read_text())
        loads = json.loads((d / "session.json").read_text())["results"]["probe"]["loads"]
        for cname in spec["configs"]:
            ps = [p for p in spec["probes"] if p["config"] == cname]
            load = dict(loads[cname])
            if len(load["offsets"]) == 64 and cname in FULL and FULL[cname]["offsets"][:64] == load["offsets"]:
                load["offsets"] = FULL[cname]["offsets"]
            sim = C.seq(load, ps, n + 64, defs=defs)
            for p in ps:
                f = d / f"step_{p['name']}.npy"
                if not f.exists():
                    continue
                hw, m = np.load(f).astype(float), sim[p["name"]]
                # s38t: a segment whose probe patches NOTHING (no steps/coefs/offs/regs of its own) and that
                # follows one which did patch something measures the previous segment's decay, not its own
                # response - the model cannot know that tail. `ps.index(p)` is unreliable when probes repeat, so
                # compare against the probe that precedes this one in the same config.
                # ponytail: only a probe that patches NOTHING counts as a tail, so `_C` (which sets a constant)
                # stays data; its divergence is carried state the model cannot know (docs/vop3_isa.md s38t).
                i_ = next((j for j, q in enumerate(ps) if q is p), 0)
                prev = ps[i_ - 1] if i_ > 0 else None
                tail = bool(p.get("keep") and not (p.get("steps") or p.get("coefs") or p.get("offs") or p.get("regs"))
                            and prev and (prev.get("steps") or prev.get("coefs") or prev.get("offs") or prev.get("regs")))
                a, b = onset(hw), onset(m)
                if a < 0 or b < 0:
                    rows.append((d.name, p["name"], -1, -1, float("nan"), False))
                    continue
                hw, m = hw[a:][:n], m[b:][:n]
                k = min(len(hw), len(m))
                e = np.abs(hw[:k] - m[:k])
                fb = [int(np.argmax(e[:, c] > LSB)) if (e[:, c] > LSB).any() else k for c in (0, 1)]
                rel = np.sqrt(((hw[:k] - m[:k]) ** 2).mean()) / (np.sqrt((hw[:k] ** 2).mean()) + 1e-12)
                rows.append((d.name, p["name"], fb[0], fb[1], rel, tail))
    return rows


if __name__ == "__main__":
    defs = [a for i, a in enumerate(sys.argv[1:]) if sys.argv[i] == "-D"]
    rows = board(defs)
    for dn, nm, fl, fr, rel, tail in rows:
        print(f"{dn[:22]:22s} {nm:14s} first bad L {fl:5d} R {fr:5d}  err/unit rms {rel:.4f}"
              + ("   (tail)" if tail else ""))
    up = [r for r in rows if (r[1].startswith("u") or r[1].endswith("_B")) and not r[5]]   # step-up, not tails
    print(f"{len(rows)} takes; step-up segments {len(up)}: sum first-bad L {sum(r[2] for r in up)} R {sum(r[3] for r in up)}")
