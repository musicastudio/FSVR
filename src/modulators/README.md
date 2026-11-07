# src/modulators: EG generators and LFOs

Sixteen EG generators and sixteen LFOs, as the filter and effect hosts' registers program them: the filters' EGs and LFO2s, and the effects' LFOs. Built with the DSP's floating-point flags (no FMA contraction), because the filters and effects are bit-exact.

| File | What |
|---|---|
| `modulators.h` | `Modulators`: `write` (a host register write), `step` (one sample), `egOut`/`lfoOut` read by the filters and effects; `flat`, ours, says whether a step would change no EG, for a filter bank at rest |
| `modulators.cpp` | the EG and LFO stepping, the LFO wave shapes and ranges |

## Rules
- `dsp_check` pins every output that goes through these; it must stay `ok`.
