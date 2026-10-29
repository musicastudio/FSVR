# src/engine: the firmware's side of the FS1R

`struct Synth`, the whole machine, and what the FS1R's CPU does every note, tick and MIDI byte. **KNOWN**: rewritten from the disassembly; every claim cites its `FUN_` address. A disagreement with the unit is a bug here, not a calibration.

| File | What |
|---|---|
| `synth.h` | `struct Synth`: the performance, the channels, the filter and effect sections, and every engine method's declaration; ours: `MAX_CHAN` 128 channels held and `nchan` of them allocated, the unit's 32 by default (`poly`, latched by `filter_latch`; whatever walks the channels walks `chans()`), and `ctlSerial`, which counts the ticks and render calls so the control refresh skips what cannot have moved |
| `scales.h` | the firmware's byte-to-register scales: velocity, key tracking, pan, the 0..99 to 0..127 conversions |
| `notes.cpp` | note on and off, channel allocation (over `nchan` channels, the note reserves scaled to them: ours, `docs/Differences.md`), operator setup, pitch, portamento, the pitch EG, LFO1, the 192.3 Hz tick, the filter and polyphony latch, and what a part byte written does to the notes (`part_written`, `silence_part`) |
| `controllers.cpp` | the eight controller sets, the three scalers and the 48 destinations |
| `fseq.cpp` | formant sequence playback and MIDI clock |
| `midi.cpp` | MIDI in, controllers, RPN/NRPN, sysex replies and dumps |
| `tuning.h` | our cost knobs: control-rate decimation, queue caps. Ours, and changing one must not change the output |
| `render.cpp` | the sample loop: tick, channels (`tonegen/`), filter voices (`filters/`), part buses, effects (`effects/`); ours: it runs in segments between ticks, the sounding channels through the lane kernel W at a time (`tonegen/lanes.h`, `kernel/`) and the rest one by one over the segment, then the mix a sample at a time, with the serial loop kept as the reference (`FSVR_RENDER=serial`), and it sleeps while nothing sounds |

## Structure
`Synth` owns state and sequencing; the parts it drives are in their own folders and do not know about `Synth`:
`presets/` the data it decodes, `tonegen/` the tone generator (a channel renders from a `Voice`, not from `Synth`), `filters/`, `effects/`. The public API is `src/fs1r.h` (`fsvr::Device`, in `src/device/device.cpp`).
