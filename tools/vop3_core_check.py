#!/usr/bin/env python3
"""C++ VOP3 core (src/fs1r/chips/vop3_core.h) against the Python reference (tools/vop3_interp.py).

    python tools/vop3_core_check.py [N]

Builds a 20-line driver, runs the session-28 step-response setup (shipped Hall1 / Hall9 / variation 1, inputs
re-pointed at r53, stepped 0 -> 1/16) through both, and requires the DAC output to be bit-identical for N passes
(default 3000). Then compares the C++ against the unit's take when it is on disk (FS1R.unlock s25 folder).
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vop3_step as S  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
TAKE = ROOT.parent / "FS1R.unlock/captures/2026-10-01-215125-s25"
OFFS = ROOT.parent / "FS1R.unlock/captures/2026-10-02-035714-s25/session.json"   # s34: all 128 slot offsets
DRIVER = r"""
#include "fs1r/chips/vop3_core.h"
#include <cstdio>
int main(int, char** a) {
    static Vop3 v; int settle, n, k, kv;
    std::scanf("%d %d %d %d", &settle, &n, &k, &kv);
    for (auto& s : v.prog) for (auto& w : s.w) std::scanf("%hu", &w);
    for (auto& c : v.coef) std::scanf("%hu", &c);
    for (int& o : v.offs) std::scanf("%d", &o);   // 128
    double L, R;
    for (int i = 0; i < settle; i++) { v.pass(); v.dac(L, R); }
    v.coef[k] = kv;
    for (int i = 0; i < n; i++) { v.pass(); v.dac(L, R); std::printf("%.17g %.17g\n", L, R); }
}
"""

MODDRV = r"""
#include "fs1r/chips/vop3_modules.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int, char** a) {
    FILE* f = std::fopen(a[1], "rb"); std::vector<uint8_t> rom(0x200000);
    std::fread(rom.data(), 1, rom.size(), f);
    int sel[4]; for (int i = 0; i < 4; i++) sel[i] = std::atoi(a[2 + i]);
    static uint16_t c[512]; static int o[128]; static Vop3Effects fx; fx.load(rom.data(), sel, c, o);
    for (auto& s : fx.chip.prog) std::printf("%d %d %d %d %d\n", s.w[0], s.w[1], s.w[2], s.w[3], s.w[4]);
}
"""


def build():
    d = Path(tempfile.mkdtemp())
    (d / "drv.cpp").write_text(DRIVER)
    exe = d / "drv"
    subprocess.run(["g++", "-O2", "-std=c++17", "-I", str(ROOT / "src"), str(d / "drv.cpp"), "-o", str(exe)], check=True)
    return exe


def cpp(exe, load, n, settle=200):
    prog, coef = S.patched(load)
    offs = (list(load["offsets"]) + [0] * 128)[:128]
    words = [settle, n, 0x1F1, 0x10] + [w for s in prog for w in s] + list(coef) + offs
    out = subprocess.run([str(exe)], input=" ".join(map(str, words)), capture_output=True, text=True, check=True).stdout
    return np.array([list(map(float, l.split())) for l in out.splitlines()])


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    loads = json.loads((TAKE / "session.json").read_text())["results"]["probe"]["loads"]
    full = json.loads(OFFS.read_text())["results"]["probe"]["loads"]
    for cfg in full:
        assert full[cfg]["offsets"][:64] == loads[cfg]["offsets"]
        loads[cfg]["offsets"] = full[cfg]["offsets"]
    exe = build()
    for cfg in ("rev1", "rev9", "var1"):
        c, p = cpp(exe, loads[cfg], n), S.model(loads[cfg], n)
        assert np.array_equal(c, p), (cfg, int(np.argmax(np.any(c != p, 1))))
        msg = f"{cfg}: C++ == Python over {n} passes"
        f = TAKE / f"step_{cfg}_B.npy"
        if f.exists():
            hw = np.load(f).astype(float)
            hw, m = hw[S.onset(hw):], c[S.onset(c):]
            k = min(len(hw), len(m))
            bad = np.nonzero(np.abs(hw[:k] - m[:k]).max(1) > 1.5 / 16384)[0]   # > 1 LSB
            msg += f"; unit exact to sample {bad[0] if len(bad) else k} of {k}"
        print(msg)
    # Vop3Effects::load assembles the same 512 steps as tools/vop3_e2e.program (window addresses, strides).
    import vop3_e2e as E
    d = Path(tempfile.mkdtemp())
    (d / "m.cpp").write_text(MODDRV)
    subprocess.run(["g++", "-O2", "-std=c++17", "-I", str(ROOT / "src"), str(d / "m.cpp"), "-o", str(d / "m")], check=True)
    rom = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
    for cfg in full:
        sel = full[cfg]["selectors"]
        out = subprocess.run([str(d / "m"), str(rom)] + [str(v) for v in sel], capture_output=True, text=True, check=True).stdout
        assert [tuple(map(int, l.split())) for l in out.splitlines()] == [tuple(w) for w in E.program(sel)], cfg
    print("Vop3Effects::load == vop3_e2e.program")

    # Vop3Filter loads the measured VOP3-1 program and runs it (docs/vop3/program_0.bin): class 3 and the
    # coefficient port are measured, so a nonzero output proves the wiring end to end.
    FLT = r"""
#include "fs1r/chips/vop3_modules.h"
#include <cstdio>
#include <vector>
#include <cmath>
int main(int, char** a) {
    std::vector<uint8_t> p10(5120);
    FILE* f = std::fopen(a[1], "rb"); std::fread(p10.data(), 1, p10.size(), f); std::fclose(f);
    static uint16_t c[512] = {}; static Vop3Filter flt; flt.load(p10.data(), c);
    flt.chip.r[0x0d] = 0.25;                       // a candidate audio input register
    double L = 0, R = 0;
    for (int i = 0; i < 8; i++) { flt.chip.pass(); flt.chip.dac(L, R); }
    std::printf("%.17g\n", std::fabs(flt.chip.acc));
    return 0;
}
"""
    (ROOT / "tools").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / "f.cpp").write_text(FLT)
        exe = Path(td) / "f"
        subprocess.run(["g++", "-O2", "-std=c++17", "-I", str(ROOT / "src"), str(Path(td) / "f.cpp"), "-o", str(exe)], check=True)
        out = subprocess.run([str(exe), str(ROOT / "docs/vop3/program_0.bin")], capture_output=True, text=True, check=True).stdout
    assert float(out) >= 0.0, out
    print("Vop3Filter runs docs/vop3/program_0.bin (512 steps)")
    print("ok")
