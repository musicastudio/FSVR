# Matching the engine to the hardware: what is still open, and what closes it

> **Worked through on 2026-09-21, the same day.** rgwan recorded all five files the request asked for. Items 1, 2, 4 and 6 are closed, item 3 turned into the first real measurement of the effect model, and item 6's diagnosis below was wrong in a way worth keeping. What each one became is marked in place; `docs/noise.md` is the working for item 1. The table below is the state before any of it, and the one at the bottom is after.
>
> **2026-09-24.** Items 7 and 8 closed off the data already recorded, and the drum's note 41 with them. Everything outside the effects is now inside the measurement; the effects are the one item left, at the bottom.
>
> **2026-09-24, session 4.** `21_keycode` read the key code table at every semitone and found the hold does not scale with it; `22_ures` read the unvoiced resonance carrier against the bandwidth register and the noise it takes with it. `docs/findings.md`. The table at the bottom is updated.

Written 2026-09-21 against every recording and register session in hand: rgwan's 0918 take of the 21 frozen request files, the 0919 `05b`, `08b` and `10_envelope2` takes, the 0920 `11_detune` and `12_fseqlevel` takes, both register sessions, and `captures/FS1R DEMO.flac`. Everything below is measured off those unless it says otherwise. The numbers come from `python tools/analyze_capture.py captures/hardware/*.wav --compare` run today against the current build.

Three of those takes were not in the comparison at all. `05b_unvoiced2`, `08b_panlevel_perfpn` and `12_fseqlevel` have their own generators, and those generators never wrote a manifest row, so `analyze_capture.py` could not see them and each needed a bespoke reader. `tools/make_capture_0921.py` merges the rows now, after checking that each generator still reproduces its `.mid` byte for byte, which is what says a row describes the take that was played. All three round-trip, and the first thing that fell out is item 6 below.

## Where the engine stands, file by file

Level is the median absolute difference over a file's segments; shape is the mean rms over the octave bands with each segment's own level divided out; envelope is the mean rms between the two aligned curves.

| file | level | shape | envelope | reading |
|---|---|---|---|---|
| `01_reference_1` / `_2` | 0.05 / 0.05 | 0.21 | | the dB and Hz scale is settled |
| `02_envelope_1` / `_2` / `_3` | 0.20 / 0.06 / 0.35 | | 0.64 / 0.43 / 0.64 | the amplitude EG is settled |
| `03_fm_1` / `_2` | 0.05 / 0.05 | 0.19 / 0.44 | | the modulation index is settled |
| `04_formant_1` | 0.06 | 0.77 | | the window law is settled |
| `04_formant_2` | 0.29 | **2.52** | | the formant's skirt family, item 8 |
| `04_formant_3` | 0.29 | 0.73 | | the resonant forms are settled |
| `05_unvoiced_1` | **7.40** | **3.77** | | the noise formant, item 1 |
| `05_unvoiced_2` | **2.86** | **6.36** | | the noise formant, item 1 |
| `05b_unvoiced2` | **7.13** | **4.06** | | the same at note 36, newly readable |
| `06_filter_1` | 0.01 | 0.76 | | the filter response is settled |
| `06_filter_2` | 0.00 | 0.24 | **5.43** | the filter EG does nothing, item 4 |
| `07_modulation_1` | 0.05 | | **2.27** | AM sensitivity, item 5 |
| `07_modulation_2` | 0.05 | | 0.81 | the LFO and the frequency EG are settled |
| `08_panlevel_1` | 0.20 | 1.71 | | the pan law is settled |
| `08_panlevel_2` | 0.36 | **5.03** | | the balance segments, items 1 and 2 |
| `08b_panlevel_perfpn` | 0.22 | | | the performance pan is settled, newly readable |
| `09_effects_1` / `_2` / `_3` | 15.97 / 32.71 / 28.93 | | 17.58 / 16.60 / 12.76 | **not a measurement**, item 3 |
| `10_envelope2` | 0.47 | | 0.81 | settled, with a scoring artifact, item 9 |
| `11_detune_1` / `_2` | 0.05 / 0.18 | 0.23 | | detune is settled |
| `12_fseqlevel` | **52.44** | | **33.56** | the engine render is silent, item 6 |

Eleven of the twenty-four frozen files agree with the unit to a quarter of a decibel and their shapes and envelopes with them. What is left is concentrated in five places, and two of those are not calibration at all.

## 1. The noise formant is the largest real gap, and most of its law is now measured

> **Closed.** `13_unvoiced3` came back and settled the rest: the corner clamps at register 77, the skirt multiplies it by 1.35 a step, the level is a table and the resonance is a threshold. The engine carries all of it. Level error 7.40 to **0.67**, 2.86 to **0.94**, 7.13 to **0.55** and 6.15 to **0.61** over the four unvoiced files. `docs/noise.md`.

> **2026-11-02.** The band this item measured is now made by the patents' clamped random walk instead of two one-poles, which gets the noise's amplitude statistics right as well as its spectrum: against WouterVanNifterick's takes the product's kurtosis is within 0.03 at the median where the two poles were 1.85 out, the level within 0.17 dB at the 90th percentile where they were 0.63, and the band shape where it was (`noise.md` 2026-11-02). The numbers in this file against rgwan's captures are still the two-pole engine's: `tools/bench_captures.py` has not been run on the walk, as the captures are not on the machine that made the change.

Eighty-one segments over three files, 7.1 to 7.4 dB of level error and 3.8 to 6.4 dB of shape error, and the model behind them is a reading of the patent with no measurement anywhere in it: a cascade of `1 + skirt` one-poles whose cutoff spans nine octaves from 20 Hz, ring modulated up to the centre, with a level term that holds the RMS constant.

Reading the two bandwidth sweeps together settles five things, and none of them needs another recording.

**The width is linear in the bandwidth byte, not exponential.** Half-power full width against the byte, pooling the note 60 and note 36 sweeps at a 1 kHz centre and skirt 0, over the range where the band still clears DC:

| bandwidth | 4 | 8 | 12 | 16 | 20 | 24 | 28 | 32 | 40 | 44 |
|---|---|---|---|---|---|---|---|---|---|---|
| width, Hz | 105 | 205 | 349 | 517 | 687 | 778 | 1027 | 1133 | 1449 | 1647 |

That is 38.6 Hz per byte through the origin, flat in the residual, with no trend in `log2` of it. The engine's `NOISE_BASE_HZ * 2^(byte / 127 * NOISE_OCT)` gives 20 Hz at byte 0 and 10 kHz at the top and is the wrong shape everywhere.

**It is an absolute frequency.** The fundamental moves two octaves between the two sweeps and the width does not move: 108 against 102 at byte 4, 1509 against 1389 at byte 40, 1781 against 1819 at byte 64. So the noise formant's bandwidth is a width in hertz, exactly as the formant window's turned out to be, and for the same reason nothing before this could see it: every unvoiced segment ever recorded sat at one centre.

**The level goes as the reciprocal of the width.** Over the unfolded range the width grows 11.45 dB and the total falls 5.88 dB at note 36, and 11.9 against 5.8 at note 60, so total power is proportional to `1 / width` to within a fiftieth. The engine's `nscale` puts the output RMS at `a^(1 - 2 * NOISE_BW_POW)` and `NOISE_BW_POW` is 0.5, which is flat, which is what the engine does. **`NOISE_BW_POW` is 1.0**, measured at two fundamentals over ten bandwidths each. The demo said moving it was worth less than a decibel; the demo was the wrong instrument, since its vocal patches sit at two bandwidths and its effects are in the path.

**The skirt widens the band. The engine narrows it.** Hardware half-power width at a 1 kHz centre, bandwidth 40, against skirt:

| skirt | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| hardware, Hz | 1570 | 1887 | 2060 | 2496 | 2719 | 3308 | 3920 | 4673 |
| engine, Hz | 940 | 448 | 387 | 305 | 275 | 252 | 246 | 243 |

The engine's `stages = 1 + skirt` cascade is backwards. Whatever the chip does, opening the skirt lowers the slope rather than raising it, and it costs level the same way the voiced skirt does, 38.75 to 42.91 dB over the range.

**The unvoiced resonance is a threshold, not a ramp.** Bytes 0 to 3 give the same spectrum to the byte, byte 4 narrows it, and 5, 6 and 7 are a pure tone at the centre with the noise under it, rising 40.31 to 43.67 dB. The engine's `+ u.res / 7.0` DC term has the tone dominating from byte 2 and adds 8 dB over the range where the unit adds 4.

**Bandwidth 0 is not bandwidth 4.** The unit reads 32 dB below `ubw-4` at note 60 and is at the digital floor at note 36. The engine sounds normally at both.

**What still needs hardware here.** Every recorded unvoiced segment puts the band at a 1 kHz centre, and from bandwidth 40 up the band's lower edge is at DC: both sweeps stop widening at about 1750 Hz and both plateau in level at the same bandwidth, which is a band folding on itself and not a law. So the top half of the bandwidth range, the clamp, and the level law above it are all read through a fold. `13_unvoiced3` in the capture request puts the centre at 8 kHz, where nothing folds until the band is 16 kHz wide, and repeats the skirt and resonance sweeps there.

**Expected gain.** The linear width law, `NOISE_BW_POW` 1.0 and the skirt's direction are worth most of the 7.4 dB level and 3.8 dB shape on `05_unvoiced_1`, the 4.06 on `05b`, and the `umode-*` and `ures-*` segments of `05_unvoiced_2`. It is also what demo songs 8 and 15 have been waiting for, both of them vocal patches on eight unvoiced operators at bandwidth 7 or 8, both 10 to 20 dB quiet.

## 2. The voiced operator loses 3.01 dB when its unvoiced half is switched on

> **Closed, and the diagnosis in this section is wrong.** The unvoiced half costs the voiced operator nothing: a probe file with and without it reads 48.87 dB either way. What the balance segments carry is the expression sweep that runs earlier in the same file. The engine kept CC 11 at 112 across the performance bulk that follows it and the unit does not, which is 3.01 dB of VNBAL. `FUN_0000f2f0` walks the four parts calling `FUN_0000edf0`, the per-part controller reset, so loading a performance puts expression back to 0xFE; the engine had that reset on CC 121 alone. `08_panlevel_2` shape 5.03 to **3.30**.

This is an engine defect, not a constant, and it has been hiding inside a file that reads 0.36 dB overall.

`08_panlevel_2`'s balance segments play one voice with a 261.6 Hz voiced carrier and a 1 kHz noise band, so the two halves are separable in one spectrum. Power in the 240 to 290 Hz band, which is the carrier alone:

| balance | 0 | 16 | 32 | 48 | 64 | 80 | 96 | 112 |
|---|---|---|---|---|---|---|---|---|
| hardware | 48.92 | 48.92 | 48.94 | 48.92 | 48.93 | 42.07 | 34.38 | 28.32 |
| engine | 45.86 | 45.86 | 45.86 | 45.86 | 45.87 | 39.09 | 32.32 | 25.19 |

The taper is right on both sides and the offset is flat. The same carrier at the same level in the same algorithm, in `01_reference_1`'s `ratio-note60` and `level-99`, reads **48.92 on the unit and 48.87 in the engine** in the same band, so the scale is not in question. The only thing that changes between the two patches is that the balance voice sets the operator's unvoiced half to level 99: the voiced bytes are identical, the performance is `init_performance` in both, and `set_uop` writes a disjoint block at operator offset +35.

So the unit's voiced operator is untouched by its unvoiced neighbour and the engine's drops 3.01 dB, which is eight steps of `LEVEL_DB` to two decimal places. The noise half is 2.8 to 4.4 dB low in the same segments, and part of that is item 1, but the voiced 3.01 is separable and exact.

Worth finding before anything else in this list is fitted, for the reason `docs/formant.md` gives about `FLT_LOSS`: a constant fitted against a patch carrying an unexplained 3 dB absorbs it. `08_panlevel_2`'s 5.03 dB of shape error is partly this and partly item 1, and neither can be read until it is gone.

## 3. The effects comparison is not a measurement

> **Re-recorded, and it is a measurement now.** Fresh take, current MIDI files, alignment good, and the three files read 16.94, 27.16 and 28.83 dB. That is the first check the effect model has ever had against hardware and it is that far out: `variation-4` is silent in the engine and not on the unit, and the reverb and variation types scatter twenty to forty decibels either way. Too big for this pass and it is now the largest number in the set.

`09_effects_1` to `_3` read 16 to 33 dB and none of those numbers means anything. The three request files were regenerated on 2026-09-20 with `FX_DEFAULTS`, the factory parameter sets for the twenty-odd types no preset performance uses, and the recording is rgwan's from 2026-09-18, which went out with zeroed blocks for those types. The comparison is a new MIDI file against an old take.

Nothing here is modelling work. Re-record the three files as they stand and the numbers become readable for the first time. Until then the demo's own band error carries an effect model nobody has checked against an impulse response, and `tools/demo_probe.py`'s median of about 6 dB has that inside it.

## 4. The engine's filter EG never reaches the filter

> **Closed, and it was not the EG.** The EG, its byte decode and its timing were all right. The engine's corner could not go below 755 Hz, because reading the firmware's coefficient as a one-pole at 48 kHz puts it there at cutoff byte 0, and the source these segments use is one partial at 32.7 Hz. That reading could not have been right at both ends either: `-log(1 - a)` spans 14:1 over the whole byte range and the unit spans far more. The corner goes as the byte now, 28.5 Hz doubling every 9.1 bytes, fitted over the six cutoff segments of `06_filter_1` at 0.74 dB rms. `06_filter_2`'s envelopes 5.43 to **3.36** and `06_filter_1`'s shape 0.76 to **0.45**.

`06_filter_2` matches the unit to 0.00 dB in level and 0.24 dB in shape, and then its four filter EG envelopes read 5.43 dB. Reading the envelopes directly rather than through the level metric says why. Peak to trough over each four second note, 20 ms hop:

| segment | hardware | engine |
|---|---|---|
| `flteg-d127-t20` | 1.84 dB | 1.77 dB |
| `flteg-d127-t60` | 1.84 dB | 1.77 dB |
| `flteg-d0-t20` | **14.93 dB** | 1.77 dB |
| `flteg-d1-t20` | **14.12 dB** | 1.77 dB |

The engine's four envelopes are identical to a hundredth of a decibel, and 1.77 dB is what the amplitude EG alone does on that patch. So the filter EG is not merely mis-scaled, it does not move the corner at all, at any depth or any time. The unit sweeps 14 to 15 dB at depth bytes 0 and 1 and holds still at 127, which is the patch behaving: the voice starts at cutoff byte 40 with EG level 4 at 0 and level 1 at 100, so a negative depth drives the corner down through the comb and a positive one opens it above everything the source has.

That the engine's filter response itself is right makes this narrower than it looks. `06_filter_1`'s twenty static cutoff and resonance segments sit at 0.76 dB of shape error, so the coefficient reading and the ladder are close; it is the path from the EG to the coefficient that is dead. The firmware side of it is the open Tier 0 line, since the cutoff conversion is only called when a parameter changes, and the 0919 register retake answers where the EG runs: nothing in the coefficient region moves while a filter EG sweeps, and the staged cutoff word matches `0xC0D + 0xA9 * cut` to the integer, so VOP3-1 runs the EG itself off the shape the CPU hands it.

Four segments is not enough to fit a chip-side EG against. `14_sens` gives it ten, a depth sweep at six settings and a time sweep at four, on the same source and the same probe performance `06_filter` uses.

## 5. AM sensitivity, and a stale note about it

> **Measured at three depths now, and it is not a scaling.** `14_sens` says the engine matches the unit at LFO amplitude depth 33 and 66 and runs up to 20 % deep at 99: peak-to-peak 6.40 against 6.23 dB at depth 33 and 39.21 against 47.04 at depth 99, both at sensitivity 7. So the chip's AM attenuation saturates at the deep end and the linear `ams / 7` does not. One value of a variable would have read that as an 18 % trim and got it wrong everywhere else. Left as it is, with the data in hand.

`docs/captures/capture_0918.md` says "AM sensitivity is roughly twice as deep as the engine makes it", the hardware losing 11.29 dB over the sweep against the engine's 4.89. That reading predates both the analyzer's pan correction and the `LEVEL_DB` change and it now points the wrong way. Measured off the two envelopes today, 5 ms hop, the modulation depth as the 5th to 95th percentile spread in dB:

| ams | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| hardware | 0.98 | 6.39 | 11.24 | 20.44 | 24.45 | 29.48 | 34.16 | 38.89 |
| engine | 0.98 | 7.20 | 14.15 | 21.33 | 28.57 | 34.52 | 40.56 | 47.21 |

The mean level falls 14.38 dB on the unit over the sweep and 16.31 in the engine. Both are linear in `ams` to within the scatter, so the engine's `regAM * LEVEL_DB * ams / 7` has the right shape and is about 18 % too deep. That is one number, except that the whole sweep sits at LFO amplitude depth 99 and one value of a variable is what the formant window's two wrong answers were both built on. `14_sens` repeats it at three depths.

## 6. `12_fseqlevel`'s engine render is silent where the unit sounds

> **Closed, and this section's reading of it was wrong.** The engine plays the file correctly. The render did not, because `make_capture_set.py --render` never passed `-r eprom.bin` and that file is the only one in the set that assigns a preset Fseq, which lives in the EPROM and nowhere else. Four seconds of silence a segment, scored against the unit at 52 dB, and the number was measuring a missing argument. The renderer gets the EPROM now whether or not a file looks as though it needs one. 52.44 to **3.16** in level and 33 to **12.18** in envelope, with nothing in the engine touched. Worth keeping as written: I read a stale-tooling number as an engine fault and wrote it up with a table, which is the same mistake `captures/demo/render` produced on 2026-09-19.

Newly readable, and it disagrees with what `docs/formant.md` records for the same file. Segment rms, hardware against the render in `captures/engine/`:

| segment | `frmt-36` | `frmt-48` | `frmt-60` | `frmt-72` | `frmt-84` | `frmt-96` |
|---|---|---|---|---|---|---|
| hardware | −46.4 | −43.9 | −41.1 | −40.8 | −41.4 | −41.7 |
| engine | −97.5 | −95.0 | −95.0 | −100.8 | −106.9 | −105.6 |

The alignment is not the problem: the markers are found, the take is 1.12 s short at the head and the offset absorbs it, and the unit reads sensible levels through the same row. `formant.md` reports 6.3 dB of band error over this file after the ratio-operator clamp, and a render that is 50 to 65 dB quiet on six of its fourteen segments cannot be the render that produced it. Re-render and re-read before trusting either number. This is the same failure that `captures/demo/render` had on 2026-09-19, where fourteen stale files described a build that no longer existed.

## 7. The spectral skirt's "1" forms, and all1/all2's geometry

Both are fully recorded in `docs/skirt.md` and neither needs hardware.

`all1`, `odd1` and `res1` are no `sin^p` window at any exponent: fitting one freely leaves 6 to 17 dB against 0.01 dB for their partners, and the measured spectra show a narrow core that stops changing above skirt 4 sitting on a pedestal that keeps rising. The engine gives them their partners' law and is 10 to 14 dB out. Every line amplitude of all six forms over all eight skirts is tabulated in that document, so this is a modelling problem with its data already paid for.

`all1` and `all2`'s geometry is the larger of the two. The engine builds them as two overlapping grains under a carrier on the fundamental, which is exactly right at skirt 0 and 36 to 66 dB out by skirt 7, where the engine's group slides off the fundamental and the unit's stays on it. It moves both members of the pair the same way, so it is the construction and not the exponent.

## 8. The formant's window family

`04_formant_2` sits at 2.52 dB of shape error and no exponent of any `sin^p` window gets it below about 2.6, which the fit establishes by trying the whole range. The formant is the only form whose window length is a parameter rather than one or two grain periods, and a window generator with a fixed number of steps stretched over a variable time would behave differently. That is the lead and it is written here as one. Data recorded; no hardware needed.

## 9. Two scoring faults worth fixing while the numbers are being read

**Silent segments score as a 30 dB disagreement.** `10_envelope2`'s `hold-50` and `hold-70` report −29.02 and −34.99 dB, and both are silence: the unit is at bit-exact zero, −144 dB, and the engine at −115 and −109. The analyzer already skips a pair that are both at their own floors, but a float render's floor and a 24-bit capture's floor are 30 dB apart. An absolute threshold, anything under about −100 dB on both sides, removes two of the three worst segments in that file.

**The manifest loses its side files.** `make_capture_set.py` rewrites `manifest.json` from its own file list, so every re-run drops whatever `make_capture_unvoiced2.py`, `make_capture_perpan.py` and `make_capture_fseqlevel.py` added. That is why three recordings sat outside the comparison for two days and each grew a bespoke reader. `tools/make_capture_0921.py` puts the rows back and verifies each `.mid` regenerates byte for byte first; the durable fix is for the main generator to merge rather than replace.

## Where it landed, 2026-09-21

Every file re-rendered against the same recordings, with the engine and the tooling as they now stand.

| file | level | shape | envelope | against |
|---|---|---|---|---|
| `01_reference_1` / `_2` | 0.05 / 0.05 | 0.21 | | 0.05 / 0.05 |
| `02_envelope_1` / `_2` / `_3` | 0.20 / 0.06 / 0.35 | | 0.64 / 0.43 / 0.64 | unchanged |
| `03_fm_1` / `_2` | 0.05 / 0.05 | 0.19 / 0.44 | | unchanged |
| `04_formant_1` / `_2` / `_3` | 0.06 / 0.29 / 0.29 | 0.77 / 2.52 / 0.73 | | unchanged |
| `05_unvoiced_1` | **0.67** | **3.31** | | was 7.40 / 3.77 |
| `05_unvoiced_2` | **0.94** | **3.65** | | was 2.86 / 6.36 |
| `05b_unvoiced2` | **0.55** | **3.16** | | was 7.13 / 4.06 |
| `06_filter_1` | 0.01 | **0.45** | | was 0.76 |
| `06_filter_2` | 0.00 | 0.24 | **3.36** | was 5.43 |
| `07_modulation_1` / `_2` | 0.05 / 0.05 | | 2.27 / 0.81 | unchanged |
| `08_panlevel_1` | 0.20 | 1.71 | | unchanged |
| `08_panlevel_2` | **0.26** | **3.30** | | was 0.36 / 5.03 |
| `08b_panlevel_perfpn` | 0.22 | | | unchanged |
| `09_effects_1` / `_2` / `_3` | 16.94 / 27.16 / 28.83 | | 18.26 / 16.88 / 13.19 | a measurement now |
| `10_envelope2` | 0.47 | | 0.81 | unchanged |
| `11_detune_1` / `_2` | 0.05 / 0.18 | 0.23 | | unchanged |
| `12_fseqlevel` | **3.16** | | **12.18** | was 52.44 / 33.0 |
| `13_unvoiced3` | **0.61** | **1.23** | | was 6.15 / 2.96 |
| `14_sens` | **1.15** | | **3.57** | was 1.67 / 3.79 |

Seventeen of the twenty-nine files agree with the unit inside half a decibel in level. The demo moves with it: mean band tilt 2.56 to **2.45**, worst envelope correlation 0.835 to **0.895**, and Kalimba, which the bandwidth-0 split was settled on, 5.88 to 1.94.

Two tooling faults fixed along the way, both of which had been producing numbers nobody could read. `make_capture_set.py --render` never passed the EPROM, so any file using a preset rendered silence. And it rewrote `manifest.json` from its own file list every run, dropping the rows the side generators add, which is why three recordings sat outside the comparison for two days and each grew its own reader. It merges now.

## What is left, in order

1. **The effects**, 17 to 29 dB over three files and eighty-four segments. The largest number in the set by a long way, measured against real impulse responses for the first time, and a piece of work on its own scale: eighty-seven types modelled from the Data List with nothing checked. **The only item left.**
2. ~~**The unvoiced pedestal.**~~ **Closed 2026-09-22 by the two-pole reading, confirmed 2026-09-24**: octave bands from 125 Hz to 16 kHz agree inside 0.5 dB over eleven wideband segments. What the analyzer still reports on the 1 kHz files is its sub-100 Hz bands at the two floors. `docs/noise.md`.
3. ~~**AM sensitivity at deep modulation**~~ **Closed 2026-09-23 off the existing take**: the chip's AM word is seven bits and the sensitivity weights are {0,1,2,4,5,6,7,8}/8. `docs/findings.md`.
4. ~~**The "1" forms and all1/all2's geometry**, item 7, and **the formant's window family**, item 8.~~ **Closed 2026-09-24 off the skirt sweep's sixty partials and `04_formant_2`**: one skirt law, two window shapes. The "1" forms and the formant take the exponent on the rise and keep a `sin^2` fall; all1 and all2 are the window alone, read by phase. All six harmonic forms inside 0.5 to 1.8 dB at every skirt, `04_formant_2` 2.52 to **0.44**. `docs/skirt.md`.
5. ~~**The filter EG's remaining 3.36 dB.**~~ **Closed 2026-09-21 by `15_filter`**, and the filter with it. `06_filter` could never measure one: its source is a single 32.7 Hz partial, so its eleven resonance segments read flat to a millidecibel on the unit and only three of sixteen cutoff bytes said anything. Against a source with energy everywhere the corner is `17.4 * 2^(byte / 12)` to 0.098 octaves rms, the resonance peak reaches 19.9 dB where the engine topped out at 8.5, the feedback goes as the cube of table A, `RESO_COMP` is measured at zero, and the filter EG's segment rate was nearly three times too slow. `06_filter_2`'s envelopes 5.43 to **2.68**. `docs/filter.md`.
6. ~~**The silent-segment scoring artifact**~~ **Closed 2026-09-23**: `SILENT_DB` is -100 dB, the float render's post-EG residue was above the old -120 gate.
7. ~~**The drum's note 41.**~~ **Closed 2026-09-24**: key code 87 (the 2026-09-23 note said 86), and the key-code interpolation truncates rather than rounds, -8 where -7 was. `19_drums` 1.25 / 3.03 to **0.81 / 1.92**. `docs/aeg.md`.

## Where it landed, 2026-09-24

`python tools/bench_captures.py` prints this table; the columns are level, shape and envelope as above.

| file | level | shape | envelope | against 2026-09-23 |
|---|---|---|---|---|
| `01_reference_1` / `_2` | 0.05 / 0.05 | 0.21 | | unchanged |
| `02_envelope_1` / `_2` / `_3` | 0.20 / 0.06 / 0.34 | | 0.64 / 0.43 / 0.61 | unchanged |
| `03_fm_1` / `_2` | 0.05 / 0.05 | **0.01** / 0.54 | | 0.19 / 0.56 |
| `04_formant_1` / `_2` / `_3` | 0.06 / **0.07** / 0.29 | 0.77 / **0.44** / 2.14 | | 0.29 / 2.52 / 0.73, `_3`'s move is one six-band segment |
| `05_unvoiced_1` / `_2` / `05b` | 0.21 / 0.27 / 0.41 | 3.13 / 3.64 / 3.07 | | unchanged; the sub-100 Hz bands, see `noise.md` |
| `06_filter_1` / `_2` | 0.01 / 0.01 | 0.32 / 0.17 | **1.78** | 0.44 / 0.24, 2.52 |
| `07_modulation_1` / `_2` | 0.05 / 0.05 | | 2.21 / 0.81 | unchanged |
| `08_panlevel_1` / `_2` / `08b` | 0.20 / 0.21 / 0.22 | 1.52 / 3.23 | | 1.60 / 3.35 |
| `09_effects_1` / `_2` / `_3` | 14.56 / 20.38 / 30.42 | | 19.12 / 16.88 / 19.17 | unchanged, the effects |
| `10_envelope2` | 0.41 | | 0.68 | unchanged |
| `11_detune_1` / `_2` | 0.05 / 0.18 | 0.14 | | 0.23 |
| `12_fseqlevel` | 3.16 | | 12.16 | unchanged |
| `13_unvoiced3` | 1.05 | **0.86** | | 1.03, the resonance carrier's noise cut |
| `14_sens` | **1.17** | | 3.10 | 1.66, 3.14 |
| `15_filter` | 1.74 | 4.22 | | unchanged |
| `16_velocity_1` / `_2` | 0.06 / 0.06 | | | unchanged |
| `17_egdecay` | 0.09 | | 0.43 | unchanged |
| `18_fmchain` | 0.05 | 0.13 | | 0.15 |
| `19_drums` | 1.22 | | 2.28 | 0.81, 1.92: the hold no longer scales with the key code, which the hits at note 60 had been leaning on |
| `20_fltmod` | 2.10 | 3.53 | 9.24 | unchanged, LFO2's waveform against the analyzer's alignment |
| `21_keycode` | 4.44 | | **0.57** | new; 7.99 / 2.43 on the old table. The level is the ten-second RMS window against a decay, not a level |
| `22_ures` | 1.08 | **0.62** | | new; 0.82 / 2.11 on the old carrier table. The level is the 8 kHz noise offset every unvoiced take at that centre carries |

Twenty-seven of the thirty-eight files agree with the unit inside half a decibel of level; the eleven that do not are the three effect files, the two whose numbers are the sub-100 Hz metric, `12_fseqlevel` and `20_fltmod` at frame and LFO rates the analyzer's alignment cannot hold, `14_sens` and `15_filter` at 1.2 and 1.7, and the two session-4 files whose level column is not a level (above). The demo, effects in: mean|err| 4.92 dB, tilt 1.96, envelope correlation median 0.976 and worst 0.908.
