# src/tonegen: the tone generator

The FS1R's tone generator chip, modelled: 32 channels of eight voiced and eight unvoiced (noise) operators on one of 88 algorithms. **INFERRED**: nothing public documents the chip, so its behaviour is measured off recordings; every constant is in `src/fs1r/cal.h` with the document that measured it.

| File | What |
|---|---|
| `envelope.h` | `EG` (the amplitude EG: hold and four segments) and `FreqEG` (the frequency EG ramp), the rate ladder `rate_secs` and key rate scaling; `EG::advance`, n samples at once for the Fast steps |
| `operator.h` | one voiced operator: the phase generator (`op_increment`, a frequency word to the chip's integer phase increment, measured), `op_sample` (sine and the stored all1/all2 waves, odd and res as a window and a carrier read by phase with the modulation in both, the formant's grains), the windows, `OpState` (its integer phase, and each sample's output for `tools/wavetrace`) |
| `operator.cpp` | `init_tables` (the grain windows and the phase generator's mantissa table), `op_wave` (an operator alone, for displays) |
| `noise.h` | the unvoiced operator: its noise band (`noise_band`) and resonance carrier (`ures`) |
| `channel.h`, `.cpp` | `Chan`: eight operator pairs on the algorithm or, ours, the custom matrix (#92, docs/Differences.md), the feedback bus, the level slew, the note-on damp, the chip's per-operator cut (`cal::OP_CUT_DB`, `NOISE_CUT_DB`), when a channel goes silent; `refresh_ctl` (control rate) and `render_chan` (one sample, its EGs a block at a time on the Fast steps) |

## Structure
Reads down: envelope, operator and noise are per operator; a channel runs eight of them through `FS1R_ALG`. A channel renders from its `Chan` and the part's `Voice` only. `Synth` (`engine/`) writes the channel's registers (`regPitch`, `regLevel`...) on the tick and calls `render_chan` per sample.

## Rules
- Changing anything here changes the sound: run the capture bench and the demo renders before shipping (`AGENTS.md`, Before you say it works).
- The per-sample path goes through `math/fastmath.h`; the control-rate maths may use libm.
