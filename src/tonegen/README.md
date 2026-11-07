# src/tonegen: the tone generator

The FS1R's tone generator chip, modelled: 32 channels of eight voiced and eight unvoiced (noise) operators on one of 88 algorithms. **INFERRED**: nothing public documents the chip, so its behaviour is measured off recordings; every constant is in `src/fs1r/cal.h` with the document that measured it.

| File | What |
|---|---|
| `envelope.h` | `EG` (the amplitude EG: hold and four segments) and `FreqEG` (the frequency EG ramp), the rate ladder `rate_secs` and key rate scaling; `EG::advance`, n samples at once for the Fast steps |
| `operator.h` | one voiced operator: the phase generator (`op_increment`, a frequency word to the chip's integer phase increment, measured), `op_sample` (sine and the stored all1/all2 waves, odd and res as a window and a carrier read by phase with the modulation in both, the formant's grains), the windows, `OpState` (its integer phase, and each sample's output for `tools/wavetrace`) |
| `operator.cpp` | `init_tables` (the grain windows, the phase generator's mantissa table and the level slew per block), `op_wave` (an operator alone, for displays) |
| `lanes.h` | `Group`: W channels side by side, a number a row, with the forms and the algorithm's buses decoded as masks, for `kernel/isa/group_kernel.cpp` (`Synth::render_segment` fills it) |
| `channel.h`, `.cpp` | `Chan`: eight operator pairs on the algorithm, each unvoiced operator a clamped random walk on the centre's sine (`cal::WALK_*`), the feedback bus, the level slew, the note-on damp, the chip's per-operator cut (`cal::OP_CUT_DB`, `NOISE_CUT_DB`), when a channel goes silent; `refresh_chan` and `refresh_op` (control rate, skipped under a `Synth::ctlSerial` they already computed under) and `render_chan` (one sample, its EGs a block at a time on the Fast steps), `render_chan_block` (a segment of them, for `Synth::render_segment`); `seed_unvoiced` and `keep_unvoiced`, the unvoiced carriers and walks that run free per channel, never reset by a note |

## Structure
Reads down: envelope and operator are per operator, the unvoiced half included; a channel runs eight of them through `FS1R_ALG`. A channel renders from its `Chan` and the part's `Voice` only. `Synth` (`engine/`) writes the channel's registers (`regPitch`, `regLevel`...) on the tick and calls `render_chan` per sample.

## Rules
- Changing anything here changes the sound: run the capture bench and the demo renders before shipping (`AGENTS.md`, Before you say it works).
- The per-sample path goes through `math/fastmath.h`; the control-rate maths may use libm.
