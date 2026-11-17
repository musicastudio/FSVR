// fs1r/cal.h - the calibration surface: every FS1R quantity we have measured but not read.
//
// INFERRED. Neither custom chip has public register documentation, so what they do with a register
// value is modelled. Most of these start as models from the DX7 lineage (same design family), the
// formant synthesis patent or the Data List, and are replaced by a measurement as recordings arrive.
// A few carry "(demo)": those are fitted to rgwan's recording of the built-in demo, which is real
// hardware but a coarse reference, fifteen songs of mixed patches with their effects in the path.
// The capture set is what settles any of them properly.
//
// They all live here so calibrating against a recording is one table edit rather than a hunt through
// the engine. Names match STATUS.md's INFERRED list, and docs/findings.md carries the working behind
// each measurement.
//
// A value here is a claim about the hardware, so it is changed by measuring, never by taste. A number
// fitted against one recording is not measured: three conclusions in two days were written up with a
// residual quoted and all three turned out wrong the same way. See the 2026-09-19 modulation index
// entry in docs/findings.md.
//
// Facts read off the board are in fs1r/hardware.h. Knobs that are ours rather than the hardware's,
// and which must never change the output, are in engine/tuning.h.
#pragma once
#include <cmath>
#include "fs1r/hardware.h"

namespace cal {
static const double WORD         = 8.0;     // a full-scale serial word, in the DSP's register units (dsp/word.h)
static const double FM_INDEX     = 3.369;   // cycles of phase deviation at full modulator level. MEASURED: two
                                            // sweeps of algorithm 8's modulator level register, 2026-09-19, whose
                                            // sidebands give 21.139 and 21.205 radians at register 0 over 25 and
                                            // 24 fitted steps, 3.3644 and 3.3749 cycles. The 4.0 this replaces
                                            // came off the first of those two takes read one step early: the
                                            // analyzer anchored the sweep on where the tone starts, and the rig
                                            // brings its note up 1.55 s before the first step writes anything, so
                                            // every spectrum was scored against its neighbour's register value.
                                            // The error is exactly one step of the sweep, 10^(4*LEVEL_DB/20) =
                                            // 1.1887, and 25.116/1.1887 is 21.13. FS1R.unlock's sweep_check.py
                                            // anchors on the ladder's first step now and both takes agree.
// The operator phase generator. MEASURED 2026-10-18 on rgwan's unit, off the S/PDIF tap (docs/findings.md):
// an operator's phase increment is an integer in units of 2^-OP_PHASE_BITS of a cycle a sample, and a
// frequency word w (1024 to the octave) becomes M[w & 1023] shifted left by (w >> 10) - OP_OCTAVE0, or right
// with the bits below dropped, where M[i] = floor(OP_MANT_ONE * 2^(i / 1024)) (tonegen/operator.cpp). One take
// fitted it; a second predicted twelve increments before it was recorded, fixed and ratio-mode, and hit all
// twelve to 0.001 of a step, every floor-or-round choice a floor. A saturated word's 23982 Hz line off
// 12_fseqlevel (2026-09-21) agrees, 4093 << 10 steps. Yamaha's US5744741 gives the same shape: a pitch
// frequency number in log form, a log/linear converter and a shifter, accumulated into the phase.
static const int    OP_PHASE_BITS = 23;
static const int    OP_MANT_ONE   = 2048;  // M[0]: the table runs 2048..4093, twelve bits
static const int    OP_OCTAVE0    = 21;    // the octave whose increment is M itself
static const double LEVEL_DB     = 0.376287;// dB per step of the 8-bit level registers (LEVTAB doubled). MEASURED:
                                            // the same sweep's sideband ladder reads 0.3761 with a 0.005 dB residual
                                            // over 36 dB, and its own level ladder reads 0.3767 over the top 21 dB.
                                            // Both land on 20*log10(2)/16, a halving every sixteen steps, which is
                                            // what a binary attenuator does. The 0.3795 this replaces came off the
                                            // 0918 recording, where the fit ran into the output path's own droop
                                            // below -68 dB (see fs1r_capture_session2_results.md) and read high.
static const double EG_LEVEL_DB  = 1.5;     // dB per step of the 6-bit EG level registers (LEVTAB >> 1)
// An operator is cut, exactly zero, once its attenuation (level register plus EG) reaches this many dB, the
// voiced and the noise half each at their own. MEASURED 2026-10-26 on rgwan's unit off the S/PDIF tap (docs/findings.md):
// a voiced sine sounds at level register 236 (88.8 dB) and is digital zero at 237 (89.2), and the same cut holds
// with the attenuation in the EG instead (register 15 plus EG step 55 sounds, step 57 is zero); the noise sounds
// at 251 (94.4) and is zero at 252 (94.8), and at register 15 plus EG step 55, but not 61. The part level is not in
// it: at part volume 64 a sine at register 199 still plays. The CPU writes 254 or 255 for output level 0
// (FUN_00012E2C, FUN_00012FCC) with no case of its own, so this is the chip's.
static const double OP_CUT_DB    = 236.5 * LEVEL_DB;
static const double NOISE_CUT_DB = 251.5 * LEVEL_DB;
// dB per step of the carrier level correction (voice 0x2D-0x34). MEASURED 2026-10-26 (FS1R.unlock 2026-10-26-open):
// corrections 4 and 8 take a carrier 6.03 and 12.05 dB down, 1.506 a step; on a modulator after an edit
// the same steps lower its index as an output level would.
static const double CARRIER_DB   = 1.5;
static const double FEEDBACK     = 0.53;    // feedback gain = FEEDBACK * 2^(fb - 7). MEASURED: 03_fm_2 feedback-1..6, the
                                            // second harmonic sat a steady 0.5 dB under the unit at 0.5
static const double EG_ATTACK_K  = 0.0625;  // rising EG time constant as a fraction of rate_secs. MEASURED:
                                            // the eleven attack-rate segments of 02_envelope_2 fit an
                                            // exponential in dB with tau/rate_secs = 0.0615 over rates 36 to 44,
                                            // where the 1 ms envelope resolves the whole rise. 1/16 is also what
                                            // the DX7 EGS does, its attack increment being (17 - level/2^24) times
                                            // the decay's, so the constant and the lineage agree. The 0.25 this
                                            // replaces left a rate-24 attack 45 dB below the unit's after 400 ms.
static const double EG_OVERSHOOT = 3.9;     // dB past the TOP OF THE SCALE that a rising EG aims at, not past
                                            // its own target: 10_envelope2's attackto-70 climbs to a target 21 dB
                                            // down at the same speed a climb to full does, which only an approach
                                            // aimed at the top and clamped at the target can do. The DX7's EGS is
                                            // the same shape, its rising increment being (17 - level/2^24) where
                                            // 16 is full scale. MEASURED 3.94 with the floor below free and
                                            // EG_ATTACK_K pinned at 1/16, 0.85 dB rms over 780 envelope points of
                                            // ten segments that reach three different targets from three levels.
static const double EG_ATTACK_FLOOR = -54.5;// dB the EG jumps to when a segment starts rising from below it.
                                            // MEASURED: attackfrom-20 starts its rise at L4 = 58.5 dB down and the
                                            // unit is at 52.7 dB one millisecond in, where attackfrom-50 starts at
                                            // 36 dB down and stays there, so the floor is a jump the chip takes
                                            // from anywhere below it and not merely where a rise from silence
                                            // begins. The DX7 does the same with a jump target of 1716 of 4096,
                                            // which is 55.8 dB below full.
static const double EG_HOLD_FRAC = 0.5;     // the hold segment as a fraction of a full traverse at the hold rate.
static const double EG_HOLD_LAG  = 0.0081;  // ... plus this, fixed. MEASURED over seven hold settings from 19 ms
                                            // to 3.65 s: a free fit of both terms over the five slowest lands on
                                            // 0.4998 of a traverse, so the fraction is exactly a half, and the
                                            // lag is then the mean of what the seven leave over. It scatters by
                                            // about six milliseconds either way, which is two note-ons quantised
                                            // to the 192.3 Hz tick and is as well as a recording can place it.
                                            // Any pure fraction of rate_secs misses the fastest holds by 80 %.
                                            // Hold register 0x3F means no hold at all, which is why the firmware
                                            // leaves that one value alone.
static const double FEG_SEMIS    = 48.0;    // frequency EG range at the full register swing of 128 (four octaves).
                                            // The sysex byte reaches the register through FEGLVL, which is measured
// The frequency EG holds its start level this many samples before it moves. MEASURED 2026-10-26 (FS1R.unlock
// 2026-10-26-open, takes 15 and 16): over 115 settings the phase a ramp adds fits 0.120 cycle rms with 0, 0.059 with 3.
static const int    FEG_LATCH    = 3;
static const double FEG_TRAVERSE = 24.0;    // semitones the operator frequency EG covers in one traverse at
                                            // rate_secs(word), the amplitude EG's own rate ladder. MEASURED
                                            // 2026-09-25 off 07_modulation_2: the two attack segments at time 40
                                            // (rate word 37, a 546 ms traverse) ramp at 4393 and 4391 cents per
                                            // second, which is 23.99 and 23.98 semitones per traverse. So the chip
                                            // steps one EG engine for both and only the unit differs: 0.375 dB per
                                            // step on the amplitude side, a quarter tone here, a 96-unit scale
                                            // quartered either way. The ramp is linear and its slope does not
                                            // depend on the swing, which is what separates this from the filter
                                            // EG's exponential approach (FEG_SEG_S, a different chip).
static const double FEG_SEG_S    = 1.33e-4; // MEASURED, FS1R.unlock session 3 `flteg` 2026-09-23: a filter EG segment whose aim
                                            // lies half its swing past its target, which is how the CPU sends them, takes
                                            // FEG_SEG_S * 2^(word / FEG_RATE_OCT) for the rate word the CPU hands VOP3-1
                                            // (register 0x2B). Eight attack times off the CPU's own stage word, words 119
                                            // to 224, 52 ms to 3.0 s, fit to 1.2% rms with a 24 ms offset that is the
                                            // sampler's; the same word at swings of 128 and 512 took the same 0.23 s, so
                                            // the approach is exponential and a segment is ln 3 of its time constants.
static const double FEG_RATE_OCT = 15.5;    // rate words to a doubling of that time
static const double FEG_FLAT_S   = 0.78;    // MEASURED, flteg: a filter EG segment with no swing holds 0.78 s (0.71 to
                                            // 0.86 over eleven notes, the sampler's 26 ms either side) at every rate word.
static const double WIN_SKIRT    = 2.0;     // the grain window is sin^p, p = WIN_SKIRT * step^skirt. The
                                            // skirt is the one shape control all1, all2, odd1 and odd2
                                            // have, voice byte 6 being the formant's bandwidth and
                                            // res1/res2's resonance, which those four do not read.
                                            // MEASURED: skirt 0 is sin^2 on every form, which puts the
                                            // two partials either side of a one-period grain 6.02 dB
                                            // down, exactly as the unit does on res1, res2, odd1 and odd2
static const double WIN_SKIRT_STEP = 2.0;   // what the skirt multiplies p by, per step, on every form.
                                            // MEASURED on odd2 and res2, whose grain is one period long
                                            // under a carrier at an integer multiple of the grain rate,
                                            // so their line amplitudes are the window's own Fourier
                                            // coefficients: p = 2, 4, 8, 16, 32, 64, 128 reproduces them
                                            // to 0.01 dB at skirt 1 and 0.7 dB at skirt 6. The "1" forms
                                            // and the formant take the same p on the window's rising half
                                            // only, with the fall held at sin^2 (tonegen/operator.cpp, init_tables);
                                            // MEASURED 2026-09-24, all six harmonic forms inside 0.5 to
                                            // 1.8 dB per skirt and 04_formant_2's sixteen skirt segments
                                            // inside 0.4 to 2.4, where the sqrt(2)-per-step symmetric fit
                                            // the formant had could not get under 2.5. tools/check_skirt.py
static const double FORM_LEVEL   = 0.520;   // what a grain train is worth on every form but the formant,
                                            // against a grain sum normalised by its own window length.
                                            // MEASURED: it puts all1, all2, odd1, odd2 and both resonant
                                            // forms within 0.14 dB of the unit at once, which the ad-hoc
                                            // root-of-the-grain-rate factor it replaces could not. The
                                            // 3.5 dB between the all forms and the odd ones is not a
                                            // constant at all: it falls out of the odd forms retriggering
                                            // twice a period under a window half as long. docs/formant.md.
static const double FRMT_BW_HZ0  = 3.1;     // the formant window's bandwidth in Hz at byte 0 ...
static const double FRMT_BW_DB   = 8.0;     // ... doubling every FRMT_BW_DB steps, so the window lasts
                                            // 1 / (FRMT_BW_HZ0 * 2^(bw / FRMT_BW_DB)) seconds.
                                            //
                                            // MEASURED, and the units are the finding. The window is a
                                            // fixed TIME, not a fixed number of periods of the fundamental:
                                            // sweeping the bandwidth at note 36 and at note 60 gives groups
                                            // of the same width in Hz, 573 against 559 at byte 56 and 4278
                                            // against 4172 at byte 88, while their widths in partials differ by
                                            // the four the two fundamentals differ by. Which is what a
                                            // bandwidth ought to be, and is not what this engine did: it
                                            // held the window at a fixed fraction of the period, so fitting
                                            // it against note 60 gave a knee at byte 44 and against note 36
                                            // a knee at 27, the same law read in the wrong units twice.
                                            // docs/formant.md.
static const double FRMT_WL_MAX  = 2.0;     // and the window never runs longer than this many periods,
                                            // which is two grains overlapping and is what the engine has
                                            // slots for. Below the bandwidth where it binds the group is
                                            // narrower than one partial at either note recorded, so no
                                            // measurement here can see it and the clamp is INFERRED.
static const double FRMT_NORM    = 0.0;     // how a formant's level follows its window length: 0 leaves the
                                            // window's peak at 1, so a wide bandwidth is quiet; 1 holds the
                                            // spectral peak instead, which is what a FOF generator does
// The noise formant, MEASURED 2026-09-22 off 13_unvoiced3 (an 8 kHz centre, forty-one segments over the
// bandwidth and both skirt sweeps) and confirmed off 05_unvoiced_1 and 05_unvoiced_2 (a 1 kHz and a 4 kHz
// centre), which give the same coefficients to a few percent at every register they share. docs/noise.md
// is the working. The band is two digital one-poles in series on white noise, ring modulated up to the
// centre, and the two poles are NOT the same pole: the first sets the core's width and the second, much
// wider, is what the 2026-09-21 reading called the pedestal. It is the second pole's own floor, a
// digital one-pole at coefficient a passing (a / (2 - a))^2 of everything at Nyquist, which is why the
// floor is white, why it sits at the same level whichever centre the band is moved to, and why it rises
// with the bandwidth register twelve decibels an octave. Fitting a1, a2 and a peak gain to every
// segment leaves 0.6 to 0.9 dB rms over 200 Hz to 23 kHz, where two identical poles plus a flat pedestal
// left 0.7 to 1.4 and the identical poles alone 2 to 10.
//
// The three tables are on the register breakpoints of NOISE_REG. Register 0 is off the shape entirely and
// carries the 2026-09-21 reading, the noise 19.6 dB under register 5 with the resonance carrier left at
// its level; nothing above register 77 changes, which is where the firmware's own clamp sits.
static const int    NOISE_REG[16] = {0, 5, 10, 15, 20, 25, 30, 36, 41, 46, 51, 56, 61, 67, 72, 77};
static const double NOISE_A1[16]  = {0.0030, 0.0030, 0.0066, 0.0095, 0.0154, 0.0236, 0.0272, 0.0348,
                                     0.0353, 0.0399, 0.0479, 0.0520, 0.0571, 0.0566, 0.0615, 0.0605};
static const double NOISE_A2[16]  = {0.0707, 0.0707, 0.0803, 0.0973, 0.1058, 0.1136, 0.1357, 0.1610,
                                     0.2027, 0.2407, 0.2849, 0.3724, 0.4526, 0.6899, 0.8797, 0.9725};
// The peak of the band, in dB on the recording's own scale; NOISE_LEVEL_DB moves it onto the engine's.
// From register 25 up it is a straight line at one LEVEL_DB per register, -0.376 dB, to the decibel.
static const double NOISE_G_DB[16] = {-27.8, -8.2, -10.4, -11.7, -14.1, -16.4, -17.7, -19.7,
                                      -20.8, -22.5, -24.6, -26.7, -29.0, -31.9, -34.4, -35.3};
static const int    NOISE_BW_CLAMP = 77;
// What one skirt step multiplies a1 and a2 by, and adds to the peak in dB, at registers 25, 51 and 77;
// the engine interpolates between the three and holds the ends. The skirt opens the second pole at low
// registers, where the first barely moves, and the first pole at high ones, where the second has already
// reached 1 and there is nothing left to open. Neither is a register shift, which is what the 2026-09-21
// reading assumed; that held only at register 25.
static const double NOISE_SK_REG[3] = {25, 51, 77};
static const double NOISE_SK_M1[3]  = {1.200, 1.315, 1.400};
static const double NOISE_SK_M2[3]  = {1.285, 1.135, 0.966};
static const double NOISE_SK_DG[3]  = {-0.84, -0.64, 0.24};
static const double NOISE_LEVEL_DB = 31.5;  // the recording's scale onto the engine's, one number for all
                                            // of it, set so 13_unvoiced3's levels read zero in the median
// The unvoiced output is the mean of the sample and the one before it. MEASURED: on every wideband
// segment the unit's noise is flat to 8 kHz and then falls 2.8, 6.8, 13 and 27 dB at 12, 16, 20 and 23
// kHz against the engine's white band, the same curve at a 1 kHz centre (15_filter) and an 8 kHz one
// (13_unvoiced3), so it sits after the ring modulator. A two-sample mean, cos^2 of half the angular
// frequency, lands on it at 0.8 dB rms over forty bins; a sine at 22 kHz on the voiced path reads the
// same level as one at 55 Hz, so it is the unvoiced path alone.
// The unvoiced resonance adds a carrier beside the band, and it is a threshold rather than a ramp.
// MEASURED: settings 0 to 3 give the same spectrum to the byte, 4 narrows it, and 5, 6 and 7 are a tone
// with the noise under it, the total rising 0.8, 2.0 and 3.1 dB over setting 0. Reading those rises as
// 1 + d^2 against a band at unit RMS gave 0.21, 0.46, 0.76 and 1.02, and the engine then set the carrier
// against a band whose RMS was actually 1 / sqrt(3), the uniform noise it ran on. The table below is the
// same carrier against the band's true RMS, sqrt(3) times the reading, which keeps what the three
// recordings and Kalimba settled. The tone still scatters 3 dB between the 8 kHz take at register 25
// and the 1 kHz take at register 51, in opposite directions at settings 5 and 7, so its law against the
// register is not read yet. The engine's res / 7 ramp had the tone taking over from setting 2 and added
// 12 dB across the range where the unit adds 3.
// MEASURED 2026-09-24, 22_ures: five bandwidth registers by the four settings that carry a tone, at 8 kHz,
// left channel only (the recording's right channel lags the left by one sample, which costs a mono sum
// 1.25 dB at 8 kHz). The carrier is a sine to the sample (envelope cv 0.003 at register 10, setting 7)
// and its amplitude against the band's RMS depends on the register as much as on the setting: 1.50 at
// register 10 where it is 2.37 at 87. And the noise under it is not left alone: a narrow band loses
// 23 dB of noise at setting 7 where a wide one loses nothing. Both are read together, interpolated
// across the register and clamped outside 10..87; settings 0 to 3 are the plain band (13_unvoiced3).
// The whole take, tone and noise alike, sits 1.1 dB over the engine at 8 kHz, as 13_unvoiced3 does,
// and that offset is the noise level's own against the centre frequency, not this table's: the DC is
// against the unit's band and the noise column is relative to setting 4.
// ponytail: the noise loss is a gain; the unit also reshapes the band (the centre drops more than the
// skirts at register 10), which a gain cannot see. The 8 kHz take cannot say whether the carrier scales
// with the centre frequency.
static const int    URES_REG[5]         = {10, 25, 46, 67, 87};
static const double URES_DC[5][4]       = {{0.60, 1.18, 1.40, 1.50}, {0.37, 0.86, 1.20, 1.42}, {0.31, 0.55, 1.07, 1.57},
                                           {0.34, 0.58, 1.26, 2.38}, {0.33, 0.65, 1.09, 2.37}};       // settings 4..7
static const double URES_NOISE_DB[5][4] = {{0.0, -4.9, -13.2, -23.4}, {0.0, -2.1, -9.5, -19.1}, {0.0, -1.0, -4.9, -11.3},
                                           {0.0, 0.0, -0.2, -0.9}, {0.0, -0.3, -0.1, 0.5}};
static const double PMS_FRAC[8]  = {0, 0.038, 0.076, 0.125, 0.212, 0.365, 0.620, 1.0};        // per-op pitch mod sensitivity. MEASURED 2026-09-24
                                            // off 07_modulation_1's eight pms segments at pitch depth 99: the
                                            // fundamental's swing at pms 1..6 is 0.038, 0.076, 0.125, 0.212, 0.365,
                                            // 0.620 of pms 7's, where the DX7 curve the engine carried gave 0.026,
                                            // 0.053, 0.089, 0.161, 0.277, 0.497, a third shy at every step.
// Per-op freq mod sensitivity, in 256ths of FMS 7's swing. MEASURED 2026-11-17 on Wouter's FS1R (Montage
// local/fs1r-hw/fsvr115fms/fms1, findings 2026-11-17): a fixed 1 kHz sine under a square LFO1 swings 92.9, 184.9,
// 305.4, 518.8, 889.6, 1482.1 and 2373.2 cents at FMS 1..7, FMD 99, each within 0.3 % of these 256ths. FMS 7 is
// the engine's full LFO word to 0.1 cents at FMD 99, 66 and 33, and FMS 5 keeps its ratio at FMD 66. Unmeasured
// on the noise half, which takes the same table. The linear fms / 7 the engine had was twice the unit at FMS 5.
static const double FMS_FRAC[8]  = {0, 10 / 256.0, 20 / 256.0, 33 / 256.0, 56 / 256.0, 96 / 256.0, 160 / 256.0, 1.0};
// The output path, MEASURED from rgwan's recording of the whole capture set on 2026-09-18. These three
// are no longer inferred: they come off the digital tap itself. captures/analysis/ holds the numbers and
// docs/capture_0918.md the working.
static const double OUT_GAIN     = 0.14992; // fixed gain between the summed bus and the digital tap. Nine
                                            // single-operator segments agree to 0.005 dB.
static const double FX_DRY_GAIN  = 5.646458;// the effect chip model's dry path at performance volume 127 with a
                                            // full-scale word in, 15.04 dB. 
static const double LEVEL_SLEW_MS= 1.6;     // the voiced level register does not step, it glides, one pole with
                                            // this time constant. MEASURED 2026-09-21 off 12_fseqlevel, whose
                                            // Fseq rewrites the level thirty to eighty dB at a time: the octave
                                            // band error over its eight formant segments bottoms at 4.74 dB
                                            // here, against 9.7 for a register that steps and 10.6 for one
                                            // latched to the grain. Flat between 1.2 and 2.0, so the last digit
                                            // is not measured. The unvoiced level glides on it too:
                                            // MEASURED 2026-11-07, 1.7-2.0 ms 10-90 % on the unit at three notes
                                            // against 1.5-1.9 here (FSVR#103). docs/formant.md.
static const double DAMP_MS      = 1.0;     // the note-on damp: what is left of the previous note in a channel
                                            // the allocator reuses decays with this time constant instead of
                                            // being cut to zero. INFERRED, and the one constant here with no
                                            // measurement behind it: FUN_00023000 proves the damp EXISTS (it
                                            // sets the taken channel's EG stage word to 4, the release stage,
                                            // and writes register 0xFC/FD, the mask coming out of the one-hot
                                            // table at 0x35B260), but nothing says how fast the chip fades.
                                            // 1.0 ms is the order of the level register's own glide and it
                                            // removes the step. The demo songs cannot pin it, since a damp only
                                            // happens when the allocator runs out and the four songs that do it
                                            // never do it twice the same way. captures/requests/23_damp.mid
                                            // (tools/make_capture_damp.py) was built and recorded for it, and
                                            // came back with NO damp in it: the unit did not take the channel
                                            // the file aimed at, so the tone survives every steal. That is the
                                            // file's designed-visible failure, not a number, so this is still
                                            // open. FS1R.unlock session6 reads the allocator arrays directly to
                                            // find which assumption broke. docs/findings.md 2026-09-25
static const double CHAN_CLIP    = 1.1919;  // the channel accumulator saturates here, hard and memoryless,
                                            // before the filter loop. stack-4 and stack-8 are driven 4x and
                                            // 8x past one carrier and recover the same ceiling to five places.
// The VOP3-1 filter loop costs nothing. MEASURED 2026-10-26 (FS1R.unlock 2026-10-26-open, takes 23 and 24): the
// part filter on against off on one voice, LPF24 at resonance 0, reads 0.00 dB at part volumes 127 to 40, cutoffs
// 127 to 80, and on the 06 files' own all1 source at note 24; input gains 0, 6, 18, 24 land on -12.06, -6.03,
// +6.03, +12.05. The 1.23 dB below was fitted off whole files against a reference whose level was itself off, and
// was absorbing those errors as the history under it says it did twice before.
static const double FLT_LOSS     = 1.0;     // was 0.8681: flat 1.23 dB the VOP3-1 filter loop was taken to cost, constant to 0.02 dB
                                            // across every type, every resonance from 0 to 100, every input
                                            // gain and every cutoff from 48 up. The 0.3190 this replaces read
                                            // 9.93 dB off the same recording on 2026-09-18, because both
                                            // filter files play at note 24 and every request file leaves the
                                            // performance's pan scaling at its extreme, so the segments are
                                            // panned hard and were being measured on the left channel alone
                                            // while the engine's own pan law was 28 % shy. Two errors that
                                            // cancelled; docs/aeg.md, "The pan was in front of everything".
                                            // Re-derived again on 2026-09-19: both filter files drive the
                                            // loop with an all1 operator, and the engine was giving that a
                                            // whole harmonic series the unit does not produce, so the loss
                                            // had been absorbing the spectral form's error too. 9.93, then
                                            // 5.69, now 1.23 dB. docs/formant.md.
}
using cal::FM_INDEX;
using cal::LEVEL_DB;
