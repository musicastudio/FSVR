# CPU cost, and why A020 Vox Morph is the worst case

A user report: "A020 Vox Morph is very CPU hungry. With 5 to 8 voices it brings my Intel i7 to the limit" (AU build). This is what the engine does, measured, and what would make it cheaper. Nothing here is a fidelity question: the constants in `cal.h` are not involved, and every option below has to leave the output bit-identical or it is not an optimisation.

## Measuring (2026-10-28)

Every optimisation commit names its tier. **Tier A**: the nineteen `tools/regress.py` cases and the Fat Line demo render bit-identical 16-bit audio. **Tier B**: no sample moves more than one 16-bit step, and `regress.py` and the demo ledger read the same. Structural work (loops, layout, build flags, dispatch) is Tier A; a change of arithmetic, a table in place of a function, may be Tier B and says so in its commit.

`bin\perfbench.exe` (`build.bat perf`, `tools/perfbench.cpp`) plays fixed scenarios through `fsvr::Device` from the factory banks, so it needs no EPROM, and reports the CPU time inside `process()` as a share of one core at the render rate, the minimum and the median of three runs pinned to one core, with the cycles a sample costs beside it; it hashes the 16-bit output, `--save` keeps the renders and `--ref` compares a later build against them with the same A/B verdict. A scenario names its performance and may put one factory voice in every part and write the parts' Mono/Poly and filter switches, as an editor would; `--perf` and `--release` try another shell or release length without changing a scenario's own definition. `python tools/abcheck.py` renders the regress cases and the demo and, against `--ref`, prints the largest step, the samples moved and the first frame that moved; `--build-base REV` builds a revision apart under `build/ab` and renders its set, so a baseline never shares `build/` with the tree under test. The protocol on the dev box, an i9-14900KF: core 2 (a P-core), the median of three, the two builds alternated in one session, nothing else timed meanwhile. Windows counts thread time in 15.6 ms steps, so a 10 s run resolves 0.16 % of a core; the cycles per sample are the finer number. These numbers are not comparable with the Ryzen 9 3900X rows below, which are kept as history.

### Phase 0: before any change (2026-10-28)

`perfbench --set standard --runs 3` on the dev box: MSVC 19.44 `/O2`, LUT maths, FTZ, 48 kHz in 512-frame blocks, the median of three on one P-core of an i9-14900KF (the minimum is within 0.7 of each). The chord is eight notes; Zap! is a one-channel patch, so eight notes cost what one does and its 6.6 % is the filter chip and the effects; Vox Morph is two layered parts, sixteen channels for eight notes, and held32 fills all 32 channels, the ceiling at Full.

| scenario | % of a core | cycles a sample |
|---|---|---|
| idle-zap | 0.62 | 385 |
| held1-zap | 6.56 | 4299 |
| held8-zap | 6.56 | 4435 |
| held8-homy | 16.09 | 10423 |
| held8-voxmorph | 21.25 | 14361 |
| held8-fatline | 14.53 | 9562 |
| held8-fulltines | 14.22 | 9450 |
| held32-voxmorph | 36.09 | 24092 |
| struck8-zap | 6.72 | 4425 |
| struck8-voxmorph | 20.16 | 13525 |
| struck8-fatline | 17.34 | 11585 |
| struck8-fulltines | 8.59 | 5505 |
| demo-fatline (29.9 s) | 16.38 | 10822 |

### Phase 1: the scalar steps (2026-10-28), every one Tier A

Each step was timed alternated against the build before it, two or three rounds in one session, 10 s a scenario; the cycles a sample are the number, since the thread clock counts in 15.6 ms. `build.bat` and `tools/abcheck.py --tier A` passed on every step, with every hash the baseline's.

| step | held1-zap | held8-voxmorph | held8-fatline | struck8-fatline | demo-fatline |
|---|---|---|---|---|---|
| floor as `roundsd` behind a runtime test, the sine table padded, the slew table filled once | -0.7 % | 0 % | | -3.5 % | |
| the control refresh skipped under a serial it already computed under | | -4.9 % | -10.6 % | -8.6 % | -5.4 % |
| link-time optimisation (`/GL`, `/LTCG`) | +1 % | +1.3 % | +3.7 % | +2.7 % | |
| no stack cookies (`/GS-`) | +2 % | +0.8 % | +2 % | +1 % | |
| the loop in segments between ticks, every channel over a segment, then the mix a sample at a time (Phase 2) | -2.8 % | -4.9 % | -6.6 % | -8.2 % | |
| the filter bank's sixteen voices four at a time on the avx2 lane kernel (Phase 3.2; Zap! is one channel, its cost the filter and the effects) | -14 % | -6 % | | -8.5 % | |
| the tone generator's channels four at a time on the avx2 lane kernel, a lone channel left scalar (Phase 3.3; Full Tines -16 %, held32 Vox Morph -9 %) | 0 % | -9 % | -13 % | -4 % | |

Phase 1's end state on the standard set, against the baseline above: Zap! 4235 cycles a sample (-1.5 %), Homy 9205 (-11.7 %), Vox Morph 13632 (-5.1 %), Fat Line 8296 (-13.2 %), Full Tines 8294 (-12.2 %), held32 22765 (-5.5 %), struck Vox Morph 12620 (-6.7 %), struck Fat Line 10953 (-5.5 %), the demo 10473 (-3.2 %), every hash the baseline's. The segment loop (Phase 2) held32 Vox Morph 22738 to 21575 (-5.1 %) on top of that.

The tone generator's kernel (`src/kernel/isa/group_kernel.cpp`) is exact on scalar, sse2 and avx2 lanes, by the self check's segments scenario and `tools/abcheck.py` under each; its first form was a wash, 3 % better on Vox Morph and 9 to 12 % worse on Fat Line, because a frame of four channels ran about as many instructions as four scalar frames: every lane takes every path the scalar code could branch into, and the three interpolated table reads a sample each cost two gathers. Taking out what the scalar loop skips (an EG past its last segment, a level lookup under the gate, the liveness pass, a control refresh with nothing stale, the frequency-EG scans) and reading a table with one AVX2 gather gave the row above. What is left in it is the arithmetic the tiers keep: double precision lerps where the chip, and Yamaha's own software engine, read an integer table once.

### The programme's end state (2026-10-28), against Phase 0

`perfbench --set standard --runs 3` on the same P-core, the median of three, every hash the baseline's (Tier A throughout), the avx2 kernels chosen at load:

| scenario | % of a core | cycles a sample | against Phase 0 |
|---|---|---|---|
| idle-zap | 0.47 | 318 | -17 % |
| held1-zap | 5.47 | 3583 | -17 % |
| held8-zap | 5.31 | 3534 | -20 % |
| held8-homy | 11.09 | 7635 | -27 % |
| held8-voxmorph | 16.41 | 11011 | -23 % |
| held8-fatline | 8.91 | 6070 | -37 % |
| held8-fulltines | 9.22 | 5931 | -37 % |
| held32-voxmorph | 28.12 | 18615 | -23 % |
| struck8-zap | 5.47 | 3578 | -19 % |
| struck8-voxmorph | 17.03 | 11270 | -17 % |
| struck8-fatline | 12.50 | 8253 | -29 % |
| struck8-fulltines | 6.56 | 4255 | -23 % |
| demo-fatline (29.9 s) | 10.33 | 6976 | -36 % |

What is left is the arithmetic the tiers keep, and the lane kernels' bound above; a channel pool across groups (the plan's last phase) and a float32 kernel on integer tables, which would be Tier B, are the levers past it.

### Four parts on all 32 channels (2026-10-28, later the same day)

A four-part performance with a long release, eight notes, so every one of the 32 channels is taken and the next chord steals from releases still sounding. The factory set has sixteen four-part performances, drum kits and one voice layered four times, and no pad among them, so the `superpad4` scenarios build one as an editor would: B053 Teck Hook's shell, its four parts written poly (they are mono, and eight notes on a mono part take one channel: the first try measured four channels) and their filters off (a filter EG closes at key-off, which cut the release whatever the amplitude EG's time), with A109 SuperPad in every part, eight voiced and eight unvoiced operators a channel and an eight-second release. `held8-superpad4` holds the chord for 10 s, 32 channels with every operator live; `struck8-superpad4` strikes it every 0.5 s, so each chord takes all 32 channels from the releases of the one before; `held8-superpad4f` keeps Teck Hook's filters on in every part, 32 channels and 32 filter slots, the heaviest case the engine has. The bench's `ch` column is the channels the engine still computes at the end of the render (`Device::renderingChannels`), 32 on all three rows.

Phase 0 and the end state alternated in one session, three rounds each, the median, every hash the same (Tier A):

| scenario | Phase 0 % of a core | Phase 0 cycles a sample | now % | now cycles | change |
|---|---|---|---|---|---|
| held8-superpad4 | 24.22 | 15937 | 17.97 | 11793 | -26 % |
| struck8-superpad4 | 20.00 | 13321 | 15.78 | 10371 | -22 % |
| held8-superpad4f | 32.50 | 21168 | 21.56 | 14033 | -34 % |

So the full machine, 32 channels of a 16-operator pad with every filter slot busy, is a fifth of a P-core where it was a third.

`dsp_bench` on the filter bank alone, cycles a sample for sixteen slots: 1123 on the scalar lanes, 719 on sse2, 672 on avx2, the same bytes out of all three (`dsp_check`). The modulators and the LATE schedule stay scalar around the kernel.

The two compiler experiments measured slower on MSVC 19.44, consistently over both rounds, and were dropped; `/Ob3` was not tried after the extra inlining of LTO lost, and `/favor` is one vendor's. The floor's first form, an integer cut with a branch on the sign, was 4 ns against the library's 1.1 ns (`src/math/floor.h` has the microbenchmark's numbers): it mispredicts on modulated phases, and it read as a 3 to 9 % loss across the set before the alternated rounds showed the rest of that loss was the session.

### More than 32 voices (2026-10-29)

Polyphony is a setting (Options > Special, `docs/Differences.md`): the unit's 32 channels, or 64, 96 or 128, each with a filter voice. The engine holds 128 channels and walks the number set, so at 32 it computes nothing it did not before: the standard set renders every hash the baseline's (Tier A), and two alternated rounds of the quick set put the two builds inside one another's noise (held8-voxmorph 12882 and 12931 cycles a sample against 12848 and 13377, struck8-fatline 7861 and 7840 against 7756 and 7761). Three rows say what more costs, on the same P-core, the median of three at Full:

| scenario | channels | % of a core | cycles a sample |
|---|---|---|---|
| held1-zap-p128: one filtered note at 128 voices | 1 | 6.56 | 4473 |
| held64-voxmorph-p128: 64 keys of two layered parts | 128 | 107.97 | 71899 |
| held32-superpad4f-p128: 32 keys of the four-part pad, every filter slot busy | 128 | 76.56 | 50775 |

A lone filtered note at 128 voices costs 1.3 points over its 5.3 % at 32, the first second of six more filter banks: a bank past the second rests once its sixteen slots have had no input, no output, no state and every EG holding for a second (`Filter::Bank::resting`) and wakes on a register write or an input, so eight banks cost two once the machine has settled, and the row's hash is the 32-voice row's. A Vox Morph channel costs 534 cycles a sample at 128 channels against 471 at 32 ((71899 - 3484) / 128 against (18568 - 3484) / 32); the difference is most likely the cache, 840 KB of channel state and 262 KB of segment rows against 210 and 64 KB, which a profile would settle. The mix loop walks the channels that rendered a segment rather than every channel, and the filter input is zeroed for the banks in use only, both Tier A.

So 64 voices of the heaviest patch are about half a P-core and 128 are more than one, at Full and single-threaded; Quality / Speed takes a quarter to a third off (below). Past that the lever is threads, and the code is shaped for them: a channel's segment is a function of its own state and the tick-constant registers, and `render_segment` renders every channel over the segment into its own `chanOut` row before the mix sums the rows in channel order, so the groups can go to a pool while the mix stays serial and the output stays bit-identical at any thread count, provable by `abcheck` and the bench's hashes. What has to change first, read out of the code: `Synth::group` is one lane-kernel scratch and becomes one per worker; `busy[]` is written by every channel (`render.cpp`, `channel.cpp`) and becomes a row per worker; FTZ/DAZ is per thread (`tuning::NoDenormals` sets the caller's MXCSR) and every worker must take the audio thread's; a work item cannot be handed off part way, since the kernel and `render_chan` write the channel as they go, so the barrier has no timeout and a worker that stalls turns the pool off for the instance; a lane that flips mid-segment is re-rendered scalar from the segment's start, so an item is a group and its flipped lanes together; `init_tables()` rewrites the global tables on every `Device` construction and needs a once-only guard. Amdahl at 128 Vox Morph channels with the banks at rest: two threads about 1.9x, four about 3.4x, flat past that; the gate is on work size, since the plug-in splits blocks at every MIDI event. Off by default, since hosts already spread instances over cores; `FSVR_THREADS=N` beside `FSVR_SIMD` so `abcheck --tier A` proves 1, 2 and 4 threads hash alike; the bench to report wall time as well as the cycles summed over every thread, which `QueryThreadCycleTime` on the calling thread does not. The go/no-go was held64-voxmorph-p128 above 50 % with the banks at rest and the mix list in: it reads 108 %.

## What the patch asks for

A020 is two layered parts of PrB 097 `SpacySweep` (algorithm 8) on the performance channel, so every key is two channels. Each channel runs five voiced sine operators (three sit at output level 0) and **all eight unvoiced operators** at levels 39 to 71 with their own EGs. The unvoiced path is a noise generator, two one-poles and a resonance carrier per operator per sample, and no other kind of preset uses eight of them at once. Eight held keys is 16 channels × (8 voiced + 8 unvoiced) = 256 operator paths per sample at 48 kHz. On the hardware this costs nothing extra: the YMP706 runs every slot of every channel whether it sounds or not.

## Measured

`build/perfbench.cpp` (not committed: a twenty-line harness that loads a ROM performance through `Synth`, holds N notes and times `render` over four seconds) on one core of a Ryzen 9 3900X, gcc `-O2`, denormals flushed as the plugin does:

| performance | 8 held notes | channels | load, one core |
|---|---|---|---|
| A001 Zap ! | | 1 | 4 % |
| B014 Full Tines | | 16 | 41 % |
| A014 Homy | | 16 | 25 % |
| A020 Vox Morph | | 16 | 53 % |

A laptop i7 core is one and a half to two times slower than this, and a DAW shares it, so "5 to 8 voices hits the limit" is the engine as built, not the AU wrapper. Denormals are flushed while the engine renders (`tuning::NoDenormals`, FTZ and DAZ on x86, FZ on ARM); without it Vox Morph read 120 % on the same core. Until 2026-10-14 only the plug-in set it, around its `process`, so the console and `render_capture` ran without; the filter chip, whose states ring down into denormals, made that cost up to 31 µs a pass, and the flush moved into `Synth::render`. The nineteen regression renders hash the same with it and without.

The profile (`-fno-inline -pg`) is flat: `fsin` 14 %, `EG::tick` 10 %, the unvoiced block about 15 %, `db2lin_fast` 5 %, the rest spread across `render_chan`. There is no single hot spot; it is the count of operator paths.

## What would make it cheaper

In order of cost to do, each with the measurement that says what it buys:

1. **Gate at −90 dB, not −100.** `render_chan` skips an operator whose EG-minus-attenuation is under −100 dB. An operator at output level 0 sits at exactly −96 dB (register 255 × 0.376 dB), so it never gates out: Vox Morph renders three silent voiced operators per channel forever. Raising the gate to −90 dB takes 8 notes from 53 % to 46 %. −96 dB is below the 16-bit floor after `OUT_GAIN`, so the render is unchanged, but this needs the capture bench and the demo ledger run before it ships, like any edit in `src/tonegen/`. One constant.
2. **Skip an unvoiced operator whose EG has finished.** `ueg.done()` is already tracked for the channel's liveness; the per-sample block does not test it. Vox Morph's unvoiced EGs decay to L4 = 0, so after the decay every one of its 128 noise paths is computing silence. Not measured yet; likely the larger win on this patch after the notes have been held a second.
3. **`-O3`.** 128 % → 93 % at `-O2` → `-O3` without FTZ on the bench. The release builds at `-O2` (CMake Release). Free, but check the regression renders hash-identical first, since the compiler may reassociate the EG maths.
4. **Vectorise the eight operators.** The operator loop is written per operator with a scalar `fsin` table read. Eight lanes of phase, level and EG would map onto SSE/NEON, but the algorithm routing (`Cb`, `H`, `S`, feedback) is serial between operators, so only the EG and level path vectorises cleanly. The real change, and the last one worth doing.

`-ffast-math` reads 33 % on the same bench, but that number is mostly FTZ, which the plugin already has, plus reassociation that changes the output; it is not an option.

## What not to do

Do not touch `tuning::CTL_DECIMATION` to chase this: its rule is that changing it must not change the output, and at 16 the control maths is already well under the operator loop in the profile. Do not lower the voiced gate under the 16-bit floor to chase the number further, and do not skip operators by their *level byte*: a level-0 operator can still be a modulator with a control set or an Fseq track driving it.

## The sample loop's maths: table, CORDIC, or both (2026-09-28)

Everything the per-sample path asks of libm (the sine by phase, 2^x for the pitch EGs and the effect sweeps, 10^(dB/20) for the level path, tanh in the enhancer) now goes through `src/math/fastmath.h`, and `FSVR_MATH` picks how it is computed: `0` raw libm, `1` a table with linear interpolation, `2` CORDIC in Q40 fixed point, `3` a hybrid where the table seeds a short CORDIC rotation over the residual. `set FSVR_MATH=n` before `build.bat`, or `-DFSVR_MATH=RAW|LUT|CORDIC|HYBRID` to CMake. The self check prints which one is built and its worst error against libm over a sweep of each function; all four pass the nineteen render cases against a fingerprint of the tree before the change.

Thirty seconds of one note, `bin/fsvr_console.exe -r ROM -P n -n 60 -d 30`, best of three on the desktop, MSVC `/O2`, no FTZ:

| backend | A020 Vox Morph (`-P 19`) | `-P 24`, regress.py's perf-everybody | worst error vs libm |
|---|---|---|---|
| 1 LUT (default) | 1.86 s | 1.75 s | 3e-7 (sin), 7e-6 relative (dB table) |
| 0 raw libm | 2.79 s | 3.18 s | 0 |
| 3 hybrid | 6.77 s | 8.19 s | 1.2e-7 |
| 2 CORDIC | 8.96 s | 10.98 s | 5e-10 |

The table wins by a wide margin and is the default. One trap on the way: the table's 2^x first scaled its result with `ldexp`, which is a CRT call on MSVC and cost more than the `pow` it replaced; it now writes the exponent field of the double directly, which is exact. A CORDIC step is a serial chain of a shift, an xor and two adds on x, y and z, and even branchless and cut to the fifteen steps the residual needs, fifteen of them cost more than the two loads and one multiply of a table read on any core with a fast FPU; the branchy first version was worse again (9.1 s and 11.3 s on the hybrid) because the sign of the residual mispredicts half the time. The hybrid is more accurate than the table and independent of table size, which is what it would be for on a target without that FPU, and it is kept for that. Raw libm is kept as the reference the self check measures against and for A/B renders; it is a sin() per operator per sample and was never what the engine shipped with.

The grain windows (`g_win`) are not part of this. They are a modelled waveform read by phase, not a function of x that another method could compute, and they stay a table on every backend.

## Fat Line: 32 channels alive for a one-note line (2026-09-28)

jameshansen: the Fat Line demo sits at about 50 % of a core. Measured with a counting build of the engine (`__rdtsc` around each section, counters per sample) on the demo's own song, 27.9 s, plain MSVC `/O2` build:

| | before | after |
|---|---|---|
| render time | 12.0 s (43 % of a core) | 6.1 s (22 %) |
| channels rendering per sample | 28.5 of 32 | 28.5, of which 14.8 silent and free |
| voiced operators working per sample | 100.5 | 70.9 |
| operator slots only ticking an EG per sample | 127.9 | 39.1 |
| refresh_ctl share of the render | 25 % | 12 % |

**What the song does.** Parts 1 and 2 are DigiSQ2, whose operators 3 and 4 have a release time of 99, the slowest setting. After every 70 ms note of the bass line they hold near 0 dB as modulators for minutes, so a channel is never finished: the engine never freed one during the song, and 28.5 channels rendered on average for a line that holds one note at a time. Their carriers were done within a second (release times 28 to 43), their unvoiced operators sit at level 0 with the EG held at full, so each channel ran 8 voiced and 8 unvoiced EGs, the control refresh and the operator loop for nothing. Parts 3 and 4 alone render in 0.36 s; the effects are 3 % and the filters 5 %. The unit renders every one of these channels in full and it costs it nothing.

**What changed**, all in `render_chan` and `refresh_ctl`, all ours:

1. **A channel whose carriers and noise are over stops rendering.** Once every carrier's EG is done with no grain in flight, every unvoiced EG is done, and the channel has put out 64 samples under 1e-9 with no damp left, it is silent. Its remaining envelopes are all in their release and falling linearly, so the sample at which the last one passes -120 dB is arithmetic, and the channel costs nothing until the allocator may treat it as free on that sample. The render differs from before in one 16-bit sample by one step on the song (the filter's ring-down below 1e-9). Freeing the channel outright instead was tried and rejected: it changes which channel the firmware's allocator steals, and 132,000 samples moved by up to 193 steps.
2. **The control refresh skips finished operators.** A voiced operator whose EG is done, with no grain in flight and no attenuation under -20 dB (the carrier correction can reach -22.5), never reads its frequency maths again; an unvoiced operator whose EG is done is gated for good. Bit-identical.
3. **The control-rate maths reads the tables.** `word_hz` and `db2lin` go through `math/fastmath.h` like the sample loop does. That moves 0.4 % of the song's 16-bit samples by one step, from the 1/16 dB table's 6.6e-6 relative error.

The nineteen regression cases match a fingerprint of the tree before the change, and the demo ledger reads the same to the digit before and after (`tools/demo_scores.txt`, the two rows of 2026-09-28). Vox Morph's one-note render goes from 1.86 s to 1.54 s on the same rules.

**What is left** is real work: fourteen sounding channels of carrier release tails between -60 and -120 dB, and the unit computes those too. The -90 dB gate above is 5 % on this song (5.3 operators per sample sit between -100 and -90), doubling `CTL_DECIMATION` another 6 %, and beyond that it is the operator loop itself.

## The Quality / Speed slider (2026-10-17)

gentleclockdivider: with the slider at Full, a single sine operator shows more aliasing than at the low settings. Their analyser screenshot (Renoise at 44.1 kHz) has two lines either side of 3.9 kHz around a fundamental near 330 Hz. Measured here through `fsvr::Device` at each step and host rate, and timed on one core of the Ryzen 9 3900X, MSVC `/O2`. The scratch harnesses render a lone sine (operator 1 of a blank voice) and time held and played chords; they are not committed.

**The artefact was the host rate, not the slider.** The engine runs at 48 kHz and `Device` resampled to the host. Full and Good used a four-point cubic whose end slopes are twice the central difference (Paul Bourke's form), which is worse than a straight line on any smooth signal; Fast and Fastest used linear interpolation. Either leaves images of the signal around 48 kHz, and at a 44.1 kHz host they fold to 3900 Hz ± f. The worst line against the fundamental, at 44.1 kHz:

| note | f0 | Full, Good (cubic) | Fast, Fastest (linear) | now, every step |
|---|---|---|---|---|
| 36 | 66 Hz | -63.1 dBc at 3834 Hz | -89.7 | -89.6 |
| 60 | 262 Hz | -50.8 at 3638 | -85.8 at 3638 | -93.0 |
| 84 | 1046 Hz | -38.3 at 2854 | -61.5 at 2854 | -91.2 |
| 96 | 2094 Hz | -32.4 at 1806 | -49.7 at 1806 | -88.0 |
| 108 | 4186 Hz | -25.1 at 286 | -36.0 at 286 | -101.4 at 286 |
| 120 | 8372 Hz | | | -103.6 |

The -88 to -93 dBc next to the fundamental in the last column is the analysis window's own floor, and reads the same at a 48 kHz host, where nothing is resampled. At 48 and 96 kHz every step always measured clean. The resampler is now a 64-tap Kaiser-windowed sinc at every step (`Device::Impl`), cut at half the lower of the two rates, flat to 19.9 kHz and about 90 dB down by 24.2 kHz at 44.1 kHz, with 0.67 ms of delay. A001 Zap! costs 15.0 % of a core at 44.1 kHz and 48 kHz alike, so it is not a cost worth trading, and Fast no longer swaps it for linear.

**The steps under Full bought little.** Good gated at -90 dB, Fast and Fastest at -80 and -72 with the control maths every 48 and 64 samples. Eight held notes: A020 Vox Morph 52.8, 50.0, 47.8 and 47.9 % of a core from Full to Fastest; A088 Fat Line 39.0, 29.8, 29.5 and 28.4. Good takes out the operators parked at output level 0 (-96 dB) and nothing under it gates anything more. Split by section, one held note of A001 Zap! is 14.2 % of a core, of which the filter chip is 6.2 and the effect section 5.3, and those run whatever the step.

**An idle instance cost 12 % of a core**, 12.2 on Zap! and 12.5 on Vox Morph with nothing playing: the effect programs and the filter chip running on silence, which the unit does too and which costs it nothing. Neither settles to exact zero (filter states sit at 1e-12 and 3e-308 in limit cycles, a reverb's cells keep moving), so skipping a section whose state is zero never fires. The engine now sleeps once no channel has been active and its output has held one value for a second: each block computes its first sample and repeats it, and wakes the moment that sample moves or a note starts (`Synth::render`). Asleep it is 0.1 % of a core. Two things kept channels alive that played nothing: a silent channel counting down to the sample the allocator frees it on, which no longer holds the engine awake and keeps counting while it sleeps, and a carrier in a release of 99 at output level 0, 138 dB down and minutes from done, which now counts as finished once it falls under the gate (`render_chan`). Fat Line stays awake at Full: its unvoiced operators hold at level 0 with the EG at full, 95.6 dB down and inside the -100 dB gate. From Good on it sleeps.

**What the lower steps do now.** Fast and Fastest also advance the amplitude EGs 4 and 8 samples at a time and ramp each gain linearly across the block (`EG::advance`, `render_chan`), which takes the EG tick and the dB-to-gain conversion out of all but one sample in the block. Eight-note chords struck every half second and released after a quarter, ten seconds, % of a core (runs differ by about 3 %):

| | Full | Good | Fast | Fastest | Fast, EGs every sample | Fastest, EGs every sample |
|---|---|---|---|---|---|---|
| A020 Vox Morph | 108.8 | 95.3 | 80.5 | 74.2 | 91.5 | 82.2 |
| A088 Fat Line | 52.6 | 48.5 | 44.4 | 40.7 | 45.0 | 44.2 |
| B014 Full Tines | 40.3 | 37.8 | 30.3 | 28.1 | 32.0 | 30.8 |
| A001 Zap! | 25.8 | 24.8 | 23.8 | 23.7 | 23.7 | 24.1 |

Fastest is 32 % under Full on Vox Morph, 23 % on Fat Line and 30 % on Full Tines. Zap! moves 8 %, because 11 points of its 26 are the filter and the effects. Against Full, the loudest third-octave band of the held chord and of the release moves by at most 0.6 and 0.4 dB at Good on Vox Morph, Fat Line, Full Tines and Bassline 2, 0.6 and 1.5 at Fast and 0.8 and 2.4 at Fastest; Zap!'s release moves 4.5 to 4.8 dB at every step under Full, from the gate on its quiet modulators. No step sharpens an attack: the largest sample-to-sample step of the first 10 ms is 0.8 to 1.06 times Full's.

**Tried and dropped.** The filter's coefficients made at most once in 4 or 8 samples while its cutoff moves: Zap! read 23.2 % against 23.0 without, inside the noise. The filter's cost is its sixteen slots' arithmetic every sample, not its coefficients. What is left for a slow machine is that arithmetic and the effect programs whenever anything sounds, about 11 % of this core between them, and the operator loop (item 4 above).
