# src/effects: the effects

The FS1R's effect section: reverb, variation, insertion and the mixer, as plain DSP functions over one shared state, plus the firmware's effect host that programs them. Bit for bit what the firmware's effect programs compute.

| File | What |
|---|---|
| `effects.h`, `.cpp` | `Effects`: the section. Runs the host, its LFOs and the five loaded programs once per sample, and starts the section over on a performance load |
| `programs.h` | `State` (registers, cells, delay memory) and the program list. Start here |
| `base.cpp` | the base program: mixer, sends and returns, master EQ, the reverb |
| `aux_window.cpp` | the send-level smoothers. Hand-written, the style the rest should move towards |
| `dly_mNN.cpp` | the reverb delay window's programs |
| `var_mNN.cpp` | the variation window's programs (`var_m07.cpp` the 3-band EQ, hand-written) |
| `ins_mNN.cpp` | the insertion window's programs |
| `host/` | the firmware's effect host (KNOWN, cites `FUN_`): performance bytes into parameters, LFOs and coefficients. `effect_host.h` is its interface, `fx_*.cpp` one file per effect family |

Each program file's header names the effect types that load it. `NN` is the firmware's program number in its window; the `*_m11`/`*_m14` programs are the clear programs that silence a window. A clear program's body is a run of writes that zero its window's registers, and the host loads it to silence a window while it reprograms it; the unit's chip runs it for as long as the upload takes, so `Effects::load` runs it once, an upload costing no samples here.

## Rules
- Bit-exact: built without FMA contraction. `dsp_check` pins the output; ctest runs it. A readability rewrite of a program must leave `dsp_check` and the bench hash unchanged.
- The host talks to the section only through `dsp/sink.h`.
- A new type inherits nothing. Registers, cells and delay memory are each one program's state and the next program's something else, so a window reload clears what the clear program cannot reach and a performance load clears the lot, with a fade over the mixer words that land with it. The reasoning, the measurements and what the firmware can and cannot do are in `docs/Differences.md`; the invariants are checks 5b and 5c of `src/selftest/selftest.cpp`.
