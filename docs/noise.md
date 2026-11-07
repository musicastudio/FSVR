# The noise formant, read off the hardware

The unvoiced operator was the last part of the engine with no measurement anywhere in it. A cascade of `1 + skirt` one-poles whose cutoff spanned nine octaves from 20 Hz, ring modulated up to the centre, with a level term that held the RMS constant: a reading of the patent, fitted against nothing. It read 7.1 to 7.4 dB out in level over eighty-one segments and 3.8 to 6.4 dB out in band shape, the largest gap left in the engine.

`13_unvoiced3`, recorded 2026-09-21, settles it. Every unvoiced segment recorded before it put the band at a 1 kHz centre, and from bandwidth 40 up the band's lower edge sits at DC, so both earlier takes stop widening at about 1750 Hz and both plateau in level at the same bandwidth. That is a band folding on itself, not a law, and it hid the top half of the range. At an 8 kHz centre nothing folds until the band is 16 kHz wide.

All three laws below are read rather than modelled.

## The corner is linear in the bandwidth register, and it clamps

Fitting a two one-pole band to each of the twenty-five bandwidths recovers a corner that is flat in `fc / register`:

| register | 10 | 15 | 20 | 25 | 30 | 36 | 41 |
|---|---|---|---|---|---|---|---|
| fitted corner, Hz | 203 | 263 | 342 | 418 | 500 | 624 | 733 |
| Hz per register | 20.3 | 17.6 | 17.1 | 16.7 | 16.7 | 17.3 | 17.9 |

So `fc = 17 Hz * register`, a straight line through the origin. `NOISE_BASE_HZ * 2^(register / 127 * NOISE_OCT)` gave 20 Hz at register 0 and 10 kHz at the top and was the wrong shape over the whole range.

**It stops at register 77.** Every spectrum from sysex bandwidth 60 up is the same band at the same level: 29.6 to 29.75 dB over ten settings, the same widths, the same shape. Sysex 60 is register 77.

**And it is an absolute width in hertz**, which the two earlier takes had already said without being read. `05_unvoiced_1` is at note 60 and `05b_unvoiced2` at note 36, two octaves apart, and the width does not move: 108 Hz against 102 at bandwidth 4, 1509 against 1389 at 40, 1781 against 1819 at 64. The same question the formant window fell into twice, answered the same way, by a second value of the variable.

## The skirt multiplies the corner

The engine gave the skirt a cascade of `1 + skirt` one-poles, which makes the band **narrower** as the skirt opens. The unit makes it wider. Fitted corner at bandwidth 20, over the eight settings:

| skirt | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| corner, Hz | 418 | 553 | 690 | 951 | 1235 | 1736 | 2442 | 3367 |

That is 8.06 over seven steps, so 1.35 a step. Half-power width follows it, 662 Hz to 1775 at a 1 kHz centre where the engine went 940 down to 243.

**The skirt also moves the level, and by a different law.** At bandwidth 60 the skirt lifts the total 29.94 to 42.19 dB over its eight settings; at bandwidth 20 it does nothing at all, 43.64 to 43.81. Both fall out of one rule: each skirt step reads like a register 5.5 lower on the level table below. At register 77 that walks back up the falling part of the table and is worth twelve decibels; at register 25 the table is already flat there and the skirt cannot move it.

## The level is a table

Total output against the bandwidth register, with the band's own RMS normalised out:

| register | 0 | 5 | 10 | 15 | 20 | 25 | 30 | 36 | 41 | 46 | 51 | 56 | 61 | 67 | 72 | 77+ |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dB | -19.9 | -0.30 | -0.07 | 0.00 | -0.36 | -1.11 | -1.77 | -2.77 | -3.63 | -4.69 | -6.07 | -7.65 | -9.41 | -12.17 | -14.24 | -15.14 |

Flat to a tenth of a decibel from register 5 to 20, then fifteen decibels of fall, then a floor. Nothing analytic fits both ends: a power law in the corner is monotone where this is flat and then steep, and the engine's constant-RMS normalisation is the flat part alone. So the engine carries the measurement on a five-register grid and interpolates, the way it already carries `FRMDET`, `FEGLVL` and `EG_KEYOFF`.

**Register 0 is not on that curve.** The unit puts the noise twenty decibels under the flat region there, where the engine sounded a full band.

## The resonance is a threshold

Voice byte `res` adds a carrier beside the band. Measured at bandwidth 20, an 8 kHz centre:

| res | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| half-power width, Hz | 674 | 598 | 715 | 668 | 381 | 64 | 64 | 64 |
| total, dB | 43.76 | 43.87 | 43.78 | 43.69 | 43.95 | 44.60 | 45.74 | 46.87 |

Settings 0 to 3 are the same spectrum to the byte. 4 narrows it. 5, 6 and 7 are a tone with the noise under it, 64 Hz being the analysis floor. Reading the rises as `1 + d^2` against a band at unit RMS gives the carrier amplitude directly: 0, 0, 0, 0, 0.21, 0.46, 0.76, 1.02. The engine's `res / 7` ramp had the tone taking over from setting 2 and added twelve decibels across the range where the unit adds three.

## What the demo settled that the capture set could not

Bandwidth register 0 silences the noise. Whether it also takes the resonance carrier down with it is not in any recording: every segment the set has at bandwidth 0 is at resonance 0 too.

Demo song 6, Kalimba, answers it. Its patch runs six unvoiced operators and two of them sit at bandwidth 0 with resonance 5 and 7 at levels 99. Taking the carrier down with the noise costs that song **5.88 dB of band tilt**; leaving it at the table's own level costs **1.94**, and nothing else in the fifteen moves by a digit. The capture set does not move either, to every digit, which is what says the demo is reading the one thing the set is blind to rather than absorbing an error.

## Where it stands

| file | level before | level after | shape before | shape after |
|---|---|---|---|---|
| `05_unvoiced_1` | 7.40 | **0.67** | 3.77 | 3.31 |
| `05_unvoiced_2` | 2.86 | **0.94** | 6.36 | 3.65 |
| `05b_unvoiced2` | 7.13 | **0.55** | 4.06 | 3.16 |
| `13_unvoiced3` | 6.15 | **0.61** | 2.96 | **1.23** |

The level is settled. What is left is band shape at a 1 kHz centre, 3.2 to 3.7 dB against 1.23 at 8 kHz, and it is one thing: **the unit carries a wideband pedestal the two-pole cascade rolls off too hard.** Band by band on `ubw-64`, hardware minus engine, with each side's own level divided out:

| octave | 125-250 | 250-500 | 500-1k | 1k-2k | 2k-4k | 4k-8k | 8k-16k |
|---|---|---|---|---|---|---|---|
| dB | -1.81 | -1.66 | +0.52 | +0.01 | -0.57 | +4.22 | +10.08 |

The core matches inside 0.6 dB and everything above 4 kHz is short. The width measurements say the same thing from the other side: the half-power width saturates near 1500 Hz while the twenty-decibel width keeps opening past 9 kHz, which is a narrow core on a broad pedestal and not any `1/(1 + (f/fc)^2)^N`. That is the same energy STATUS.md's "top octave" item has been chasing, and a file that sweeps the bandwidth at an 8 kHz centre with the analysis reaching to Nyquist is what would model it.

## Reproducing this

```
python tools/make_capture_0921.py
python tools/make_capture_set.py --render
python tools/analyze_capture.py captures/hardware/13_unvoiced3.wav --compare
```

## 2026-09-22, the pedestal is the second pole, and the noise is averaged two samples at a time

The 2026-09-21 reading above put two identical one-poles on the band and left a wideband pedestal it could not model. Refitting every hardware segment with the two poles free settles it: the band is two digital one-poles in series whose coefficients are **not the same**, and the pedestal is the second pole's own floor. A digital one-pole at coefficient `a` passes `(a / (2 - a))^2` of everything at Nyquist, which is white, sits at the same level whichever centre the band is moved to, and rises with the register twelve decibels an octave, all three of which the pedestal does.

Fitting `a1`, `a2` and a peak gain to each spectrum, ring modulated to the centre and multiplied by the roll-off below, over 200 Hz to 23 kHz:

| register | 5 | 10 | 15 | 20 | 25 | 30 | 36 | 41 | 46 | 51 | 56 | 61 | 67 | 72 | 77 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `a1` | .0030 | .0066 | .0095 | .0154 | .0236 | .0272 | .0348 | .0353 | .0399 | .0479 | .0520 | .0571 | .0566 | .0615 | .0605 |
| `a2` | .071 | .080 | .097 | .106 | .114 | .136 | .161 | .203 | .241 | .285 | .372 | .453 | .690 | .880 | .973 |
| peak, dB | -8.2 | -10.4 | -11.7 | -14.1 | -16.4 | -17.7 | -19.7 | -20.8 | -22.5 | -24.6 | -26.7 | -29.0 | -31.9 | -34.4 | -35.3 |
| rms, dB | 0.90 | 0.77 | 0.78 | 0.86 | 0.75 | 0.78 | 0.74 | 0.75 | 0.77 | 0.69 | 0.80 | 0.64 | 0.66 | 0.67 | 0.65 |

Three things fall out of the numbers. The first pole is linear in the register to about 55 and then stops. The second doubles every sixteen registers from 25 up, `2^((reg - 25) / 16)`, until it reaches 1 at the clamp, which is where the band becomes one pole wide and the "pedestal" reaches the core. And the peak falls one `LEVEL_DB` per register, -0.376 dB, from register 25 to 77 to the decibel, so the level table of 2026-09-21 was this line seen through the width.

**It is the same band at every centre.** The 1 kHz take gives `a1` .0162, .0361 and .0628 at registers 20, 41 and 77 against .0154, .0353 and .0605 here, `a2` .106, .207 and .994 against .106, .203 and .973, and the peak -14.2, -20.9 and -35.7 against -14.1, -20.8 and -35.3. The 4 kHz and 8 kHz centres at register 51, skirt 2 agree to 6 % and 0.4 dB. That is two files, three centres and two dates, so the coefficients are measured rather than fitted.

**The skirt is two controls, not a register shift.** At register 25 a skirt step multiplies `a1` by 1.20 and `a2` by 1.285 and takes 0.84 dB off the peak, which is the "5.5 registers a step" of the earlier reading. At register 77 `a2` is already 1 and the skirt multiplies `a1` by 1.40 a step with the peak flat. At 51 it is 1.315 and 1.135. The engine carries the three slices and interpolates between them.

**The noise is averaged two samples at a time.** On every wideband segment the unit's noise is flat to 8 kHz and then falls 2.8, 6.8, 13 and 27 dB at 12, 16, 20 and 23 kHz against a white band. The same curve comes off `15_filter`'s source at a 1 kHz centre and `13_unvoiced3`'s at 8 kHz, so it is after the ring modulator, and `01_reference`'s fixed sines read the same level at 22 kHz as at 55 Hz, so it is the unvoiced path alone. A two-sample mean, `cos^2` of half the angular frequency with its null at Nyquist, lands on it at 0.8 dB rms over forty bins. Computing the band at 24 kHz and holding it was tried and is wrong: it folds the 8 kHz band onto 16 kHz at -6 dB, and the unit has nothing there.

Where it stands, band shape as rms over sixth-octave bands from 100 Hz within 40 dB of the peak, which is the metric `tools/analyze_capture.py`'s does not use: its sixth-octave bands below 100 Hz hold one or two FFT bins each and sit at the recording's floor, and on these files they carry most of its number.

| file | shape before | shape after | level scatter before | after |
|---|---|---|---|---|
| `05_unvoiced_1` | 2.65 | **1.19** | 0.40 | 0.18 |
| `05_unvoiced_2` | 1.42 | **0.70** | 0.23 | 0.19 |
| `05b_unvoiced2` | 2.91 | **1.55** | 0.57 | 0.14 |
| `13_unvoiced3` | 2.23 | **0.61** | 0.23 | 0.17 |
| `15_filter`, source | 2.16 | **0.52** | 0.04 | 0.03 |

The resonance carrier's table was read against a band whose RMS the engine had at `1 / sqrt(3)` of what it assumed, the uniform noise it ran on; the noise source is unit variance now and the table is scaled by `sqrt(3)`, which keeps the three recordings and Kalimba where they were. The tone still scatters 3 dB between the two takes in opposite directions at settings 5 and 7, so its law against the register is not read.

Reproducing this: `python tools/fit_noise_band.py` prints the table above off `captures/hardware/`.

## 2026-09-24, the pedestal is closed and the shape number is the metric

Read again against the current engine, octave bands of hardware minus engine with each side's level divided out, over eleven wideband segments across `05_unvoiced_1`, `13_unvoiced3` and `05_unvoiced_2`: -0.5 to +0.5 dB in every octave from 125 Hz to 16 kHz, the 4 to 8 kHz and 8 to 16 kHz bands included. The two-pole reading above had already closed it. The 3.1 to 3.7 dB that `tools/analyze_capture.py --compare` still prints as shape for the 1 kHz files is its sixth-octave bands under 100 Hz, one or two FFT bins each at the recording's floor on one side and the render's on the other; the same files over 100 Hz read 1.9 to 2.2, and over the bands within 40 dB of the peak 1.2 to 1.6. A gate on those bands was tried in the analyzer and taken out, because it also empties the files whose spectra are three lines. The number is the metric's, not the engine's, and it is left as it is so the ledger stays comparable.

What is left in the unvoiced chain is the resonance carrier's level against the register, which the two takes still put 2 to 3 dB apart in opposite directions at settings 6 and 7. Not a shape, and not above 4 kHz.

## 2026-11-02, the noise is a clamped random walk, and it runs free

The two one-poles above have the band right and the noise wrong. WouterVanNifterick recorded the unvoiced operator on their own unit, off its analogue out, at a 1 kHz and an 8 kHz centre over every bandwidth, skirt and resonance (2026-11-01 and 2026-11-02), and read what the spectrum cannot see: the noise's amplitude, demodulated from the centre. Below bandwidth 44 it is sine valued and bimodal, the product's kurtosis near -1.3. At 52 to 56 it comes in bursts, super-Gaussian. Above 60 it is Gaussian. White noise through any linear filter is Gaussian at every setting, and FSVR's was.

**The generator is the patents' correlative noise.** US5610354 (FIG. 14) draws the unvoiced operator as white noise through a low-pass set by the skirt, an adder for the resonance, a "correlative noise" circuit set by the bandwidth, and a multiplier by the centre's waveform. US5744741 and JPH04346502A spell the correlative circuit out: each sample is drawn at random within a step of the last. Yamaha's S-YXG100 soft synth computes the same walk in floating point. In words: white noise on [-1, 1) through a one-pole whose coefficient is the skirt's is the drive. The drive two samples back, lifted by the resonance's drift and capped at 1, places the walk's next value between the walk less the width and the walk plus it, both bounds clamped to the rails at -1 and 1. The walk times the centre's sine is the operator. (The two-sample mean of 2026-09-22 stood after it here until 2026-11-07, when the unit's digital tap showed it is not there; see the section of that date.) A narrow width walks a slow path that rides the sine, which is the bimodal noise; a width past the rails lets the walk be the drive itself, which is the Gaussian one. Bandwidth register 0 is width 0: the walk stands still and the operator is the bare carrier at whatever level the walk stood. That replaces the 2026-09-21 reading that register 0 silences the noise and leaves a resonance carrier: there is no separate carrier, the resonance is the drift, and it no longer depends on the register.

**The tables** are `cal::WALK_WIDTH` (by bandwidth register, log-linear between the fitted settings, 2 from register 82, setting 64, where the walk is past its rails), `WALK_COEF` (the drive's one-pole by skirt, 1/16 at skirt 0), `WALK_DRIFT` (by resonance) and one gain, `WALK_GAIN`. They are fitted on those takes, two centres on one unit, by level, band shape and kurtosis, with `tools/fs1r/hw` in WouterVanNifterick's FS1x repository as the method, and copied from its commit 0ae343b2 without change so the two projects stay comparable. The drift is the S-YXG100's own table, 0 and then 2^-8 to 2^-2; fitted free, resonances 4 to 6 land on it and resonance 7 reads 0.35 against its 0.25. Under the 2026-09-19 rule they are fitted rather than measured until a second unit agrees.

**Nothing resets.** In the `sync` take (20261101-2033) 32 mono notes over 100 s land on one 3.4389 Hz carrier that never stopped, and at bandwidth 0 the frozen level repeats exactly over 24 mono notes and with period 32 in poly, one level a channel. So the carrier and the walk belong to the channel: a note never resets them, a carrier steps on while its operator is under the gate, and a channel that renders nothing has its carriers run on at the centres they last played, worked out when a note next takes it (`keep_unvoiced`). Each channel's carriers and generators start once, from seeds of their own, and its walks at 0 (`seed_unvoiced`; the walk's start at 0 is rgwan's unit, 2026-11-07). The walk holds while nobody can hear it, which no recording can tell from running. The mono rule this needs was already the firmware's (2026-10-26): `FUN_000112C0` takes back the first channel whose owner word, `DAT_010289FC`, is the part, and the poly allocator writes the same word, so a mono part keeps the channel its last note played.

**Against the takes.** Every segment of `spectrum` (20261101-2039), `window-3hz` (20261101-2027) and `sync` rendered on `render_capture` with the voice and performance bulks the unit was sent, read by FS1x's `tools/fs1r/hw/analyze.py` at its commit 9de1dfc4, each engine render through the unvoiced band's analogue path, and summarised as FS1x's EXP-0177 summarises its own: bandwidth 0 rows aside (106 and 40 segments), the median and the 90th percentile of each difference, band shape as the rms of the sixth-octave differences with the segment's level taken off, within 30 dB of the peak.

| | kurtosis difference | crest | envelope spread | band shape, under 16 kHz | under 7 kHz | level, median / abs p90 |
|---|---|---|---|---|---|---|
| `spectrum`, two one-poles | 1.85 / 2.39 | 2.16 | 0.207 | 0.43 / 0.68 | 0.42 | -0.35 / 0.63 |
| `spectrum`, the walk | **0.03 / 0.09** | **0.09** | **0.005** | 0.41 / 0.59 | 0.42 | +0.07 / **0.18** |
| `spectrum`, FS1x's engine, the same tables | 0.03 / 0.10 | 0.07 | 0.006 | 0.39 / 0.57 | 0.38 | -0.04 / 0.25 |
| `window-3hz`, two one-poles | 0.60 / 1.35 | 0.81 | 0.060 | 0.41 / 0.67 | 0.43 | -0.24 / 0.48 |
| `window-3hz`, the walk | **0.07 / 0.10** | **0.18** | **0.008** | 0.46 / 0.56 | 0.47 | +0.10 / **0.23** |
| `window-3hz`, FS1x's engine, the same tables | 0.04 / 0.10 | 0.16 | 0.008 | 0.44 / 0.61 | 0.44 | +0.04 / 0.25 |

The walk lands on FS1x's engine: the waveform's statistics go from wrong to right, the level holds tighter with one gain for every setting than the two-pole fit's level table did, and the band shape is where it was, a little better at the 90th percentile and 0.05 dB worse at the median on `window-3hz`. The half-power width reads 1.00 of the unit's where the two poles read 1.05.

| `sync`, mono | R0 | Rf | Rg |
|---|---|---|---|
| the unit | 0.140 | 0.997 | 0.994 |
| two one-poles, reset at every note | 0.988 | 0.341 | 0.095 |
| the walk, free running | **0.150** | **0.997** | **0.994** |

R0 is how far the carrier's phase at each note agrees with a restart, Rf with one oscillator left running; poly reads as the unit does (R0 0.083 and Rf 0.329 against 0.172 and 0.384, every note on a channel of its own).

These rows read FSVR's left channel alone. FSVR's right channel lagged its left by two samples in its output path when they were read (since made one sample, as the unit's, docs/findings.md 2026-10-28, FSVR#97), and `analyze.py` sums the channels, so as rendered a comb takes 6 dB off at 8 kHz and 21 dB at 12 kHz on both engines alike (band shape 4.9 and 5.1 dB). That is the output path, not this generator, and whether the unit's analogue out does the same is not claimed here either way. Nor is FS1x's out-path take (its F-0453), which reads the unit's voiced output as carrying a two-sample mean of its own off the analogue out, where the 2026-09-22 reading above found none on rgwan's digital tap.

**What is open.** The tables against a second unit: `tools/bench_captures.py` on rgwan's `05_unvoiced_1`, `05_unvoiced_2`, `13_unvoiced3` and `22_ures`, which the two one-poles were fitted on, has not been run on the walk (the captures are not on the machine that made the change). The drift at resonance 7. Kalimba (demo song 6), whose two bandwidth-0 operators settled the old carrier, has not been read again. The walk's bit widths and its period: US5744741 reads as a 12-bit drive and a 16-bit walk, and nothing here measures either. `tools/fit_noise_band.py` still runs and reproduces the 2026-09-22 table off rgwan's captures; it describes the two-pole model and stays as its history.

## 2026-11-03, the width is the register over 64

The walk's width above was a table: the widths fitted setting by setting, interpolated log-linearly between them, with 2 standing in past the rails. It carried every fit's scatter, plateaus at registers 10 to 12 and 18 to 20 and jitter at the bottom, and it jumped from 1.28 to 2 at register 82. The fitted widths themselves say what the law is:

| settings (registers) | fitted width over register / 64 |
|---|---|
| 24 to 57 (30 to 73), the best-determined fits | 0.998 to 1.028, most 1.000 to 1.010 |
| 2 to 22 (2 to 28), fitted from level | 0.84 to 1.08 |
| 58 to 63 (74 to 81), fitted from kurtosis | 1.007 to 1.030 |
| 64 to 99 (82 to 127) | 2, a stand-in for "past the rails", not a fit |

**So the width is the register over 64** (`cal::WALK_STEP`). Register 0 freezes the walk, 64 is one rail, and the 7-bit register tops out at 1.98. Past about register 90 the walk is the drive whatever its width: the unit's kurtosis is flat from setting 66 up, and the law renders those settings as the table's 2 did, to two decimals, without the jump. Read as fixed point it is the register with six fraction bits; in a 16-bit walk with its rails at ±2^15, which is how US5744741 reads, the register shifted nine places up into the patent's multiply. 1/64 is also the S-YXG100's own smallest step. That is a reading, not a measurement: the bit widths are open.

Against the takes, read as in the section above (FS1x's `analyze.py` at 9de1dfc4, FSVR's left channel, bandwidth 0 aside), the table against the law, median / 90th percentile:

| | kurtosis difference | band shape, under 16 kHz | level, median / abs p90 |
|---|---|---|---|
| `spectrum`, 106 segments, table | 0.03 / 0.09 | 0.41 / 0.59 | +0.07 / 0.18 |
| `spectrum`, register / 64 | 0.03 / 0.08 | **0.38 / 0.56** | -0.02 / **0.17** |
| `window-3hz`, 40 segments, table | 0.07 / 0.10 | 0.46 / 0.56 | +0.10 / 0.23 |
| `window-3hz`, register / 64 | 0.07 / 0.11 | **0.43 / 0.51** | +0.04 / **0.18** |
| `gen-fine`, 91 segments, table | 0.06 / 0.13 | 0.46 / 0.58 | +0.06 / 0.22 |
| `gen-fine`, register / 64 | 0.06 / 0.13 | **0.44 / 0.55** | +0.03 / **0.20** |

`gen-fine` (take 20261101-2238) steps the bandwidth from 2 to 98, mostly at a 3.4 Hz centre where the walk's own shape is what the analysis reads; FS1x's engine with its table reads 0.05 / 0.14, 0.46 / 0.60 and -0.02 / 0.40 there. Setting by setting at skirt 0, the mean absolute residual in kurtosis and level goes 0.134 and 0.209 dB to 0.139 and 0.155 over settings 2 to 22, 0.064 and 0.071 to 0.059 and 0.056 over 24 to 57, 0.077 and 0.125 to 0.050 and 0.063 over 58 to 63, and stays where it was over 64 to 98, the take's disturbed patches at 80 to 84 included. The `sync` take does not move. So the law reads as well as the table or better on three sets and has one number in it where the table had 128. FS1x keeps its table; the same law would serve it.

## 2026-11-07, on rgwan's unit: no two-sample mean, and a channel's walk starts at 0

James asked for PR #110 to be checked on rgwan's rig, the analogue lowpasses in particular. The rig records the unit's digital tap only (S/PDIF, Scarlett inputs 5 and 6; inputs 1 to 4 read the -101 dB floor with a note playing), so no analogue stage is in these takes. Six short takes, all in `FS1R.unlock/captures/2026-11-07-pr110` with their `.mid`, manifest, builder and reader; FSVR renders the same `.mid` files through `render_capture`.

**There is no two-sample mean on the tap.** The unit's left channel alone carries the walk's band to 20 kHz: at bandwidth 96 it reads -47.4 dB at 1, 8, 12 and 16 kHz centres alike. With the mean, PR #110 read 1.5, 3.1 and 5.9 dB low at 8, 12 and 16 kHz, and its band shape was 5 to 6 dB rms off on every wideband segment, 1 kHz included. Without it: level within 0.16 dB and shape within 0.42 dB on every segment. The cos^2 is real in the L+R sum: the right channel is one sample behind the left on the unit, voiced and unvoiced alike, as `docs/findings.md` 2026-10-28 has it, and a voiced sine reads the same sum loss (1.2, 3.0, 6.0 and 11.8 dB at 8, 12, 16 and 20 kHz) on the unit and in FSVR. So the mean was the channel pair's, not the generator's. The takes the PR was scored on were off WouterVanNifterick's analogue out, where FS1x's F-0453 reads the same cos^2 on the voiced output too. That fits an output-stage lowpass, not one in the unvoiced path. Nothing on this rig can measure the analogue out, so FSVR models the tap.

**The part filter on the walk matches.** LPF24 and LPF12 at cutoffs 64 and 96 on a 1 kHz, bandwidth 96 walk: level within 0.21 dB, shape within 0.24 dB, with or without the change. The filter was not touched.

**Bandwidth 0.** A walk that has moved and is then frozen by a parameter change (6p 01 27 = 0) is a sine at a steady level over the following mono notes (-46 dB on the unit, resonance 7 and 0 alike), as PR #110 has it. A channel whose walk has never moved is digital zero at bandwidth 0, on the first notes after power-on and in poly, where a note can take a fresh channel. PR #110 seeded each walk at a random point, so it sounded at about -28 dB there; the walk now starts at 0. Reached by voice bulk instead, the unit's level at bandwidth 0 changed from bulk to bulk (-61 dB, then zero, then -39 dB, over seven bulks, mono). FSVR holds the level across a bulk. What a bulk does to the walk is open.

`tools/bench_captures.py` on rgwan's unvoiced captures, level median against PR #110 as pushed: `05_unvoiced_1` 0.16 to **0.06** dB, `05_unvoiced_2` 0.13 to 0.06, `05b_unvoiced2` 0.16 to 0.15, `13_unvoiced3` 1.31 to **0.09**, `22_ures` 1.19 to **0.08**, `15_filter` 0.44 to 0.15 (main read 0.23, 0.24, 0.40, 1.06, 1.08 and 0.40). Shape moves 0.0 to 0.2 dB, down in every file. So the walk's tables hold on a second unit once the mean is out. The 2026-09-22 reading of the mean was made before the dry pair's one-sample offset was known, and its working is not re-traced here.

