// tonegen/operator.h - one operator: its waveforms, grains and level. INFERRED: the constants are in fs1r/cal.h.
#pragma once
#include <algorithm>
#include <cmath>
#include "fs1r/hardware.h"
#include "fs1r/cal.h"
#include "math/fastmath.h"
#include "presets/voice.h"
#include "tonegen/envelope.h"

// The chip's LFO amplitude modulation, measured off 14_sens (three LFO depths by eight sensitivities):
// the channel AM word is 7 bits, so it floors at 127 steps (48 dB) where the CPU can send 255, and the
// per-operator sensitivity weights it {0,1,2,4,5,6,7,8}/8, not k/7. docs/findings.md 2026-09-23.
static const int AMS_W[8] = {0, 1, 2, 4, 5, 6, 7, 8};
static inline double am_att(int regAM, int ams) { return std::min(regAM, 127) * LEVEL_DB * AMS_W[ams & 7] / 8.0; }
// One pole per sample coefficient for the voiced level register, cal::LEVEL_SLEW_MS as a rate.
const double LEVEL_SLEW_K = 1.0 - exp(-1.0 / (cal::LEVEL_SLEW_MS * 0.001 * SR));

// ------------------------------------------------------------------------------------------ tables (chip side)
extern float g_win[2][8][1025];
extern float g_winDC[2][8];
extern uint16_t g_opMant[1024];   // the phase generator's mantissas, floor(2048 * 2^(i / 1024)) (cal.h)
void init_tables();

// An operator's phase increment for a frequency word, in units of 2^-cal::OP_PHASE_BITS of a cycle a sample
// (MEASURED, cal.h). The word is a 16 bit register that saturates rather than wrapping (12_fseqlevel,
// 2026-09-21). The formant's grains, the channel pitch and the noise centre take the same law: a fixed formant
// holds still against a fixed sine on the same word (2026-10-24), and the rest is INFERRED from that. A word
// with a fraction, which the unit's never has (FSVR's 14-bit bend, the frequency EG's ramp), takes the same
// law at its fractional place. INFERRED: between two words there is nothing to measure on the unit.
static inline uint32_t op_increment(double w) {
    w = std::clamp(w, 0.0, 32767.0);
    const int wi = (int)w, i = wi & 1023; const double fr = w - wi;
    const uint32_t M = fr == 0.0 ? g_opMant[i] : (uint32_t)(cal::OP_MANT_ONE * fm::exp2((i + fr) / 1024.0));
    const int s = (wi >> 10) - cal::OP_OCTAVE0;
    return s >= 0 ? M << s : M >> -s;
}
// Ours: the phase is held at 2^32 to a cycle so that it wraps by itself; an increment in those units, and in hertz.
static inline uint32_t op_inc32(uint32_t inc) { return inc << (32 - cal::OP_PHASE_BITS); }
static inline double op_hz(uint32_t inc) { return inc * (double)SR / (double)(1u << cal::OP_PHASE_BITS); }
// The sine and the level path go through math/fastmath.h, which is where the backend is chosen.
static inline double fsin(double ph) { return fm::sin_turns(ph); }
static inline float fwin(int fam, int s, double x) { double y = x * 1024.0; int i = (int)y; if (i >= 1024) return 0.f; float f = (float)(y - i); const float* w = g_win[fam][s]; return w[i] + (w[i + 1] - w[i]) * f; }
// The per-sample level path: anything at or under -128 dB is silence, the rest is the backend's.
static inline double db2lin_fast(double db) { return db <= -128 ? 0.0 : fm::db2lin(db); }

// A grain runs at the carrier frequency it was fired with, latched beside the carrier phase the grain
// already resets. The level is not latched with it, it follows cal::LEVEL_SLEW_MS. MEASURED 2026-09-20
// and 2026-09-21, see op_sample.
struct WinGen { double w = 1.0, c = 0.0, fc = 0.0; bool on = false; };
struct OpState {
    uint32_t phase = 0;           // 2^32 to a cycle, stepped by op_inc32(inc): the phase generator (cal.h)
    WinGen g[2]; int nextGen = 0; double fphase = 0;   // the formant's grains
    double word = 0; uint32_t inc = 0;   // the operator's frequency word and its increment (refresh_ctl)
    EG eg; FreqEG feg;
    EG ueg; FreqEG ufeg; double nphase = 0; double lp[8] = {}; uint32_t rng = 0x12345678;
    double att = 0, attS = 0, fop = 0, fw = 0, wl7 = 1, uatt = 0, nf = 0, na = 1, na2 = 1, nscale = 0, nres = 0, uprev = 0; int bw = 0, ratio = 0;   // refresh_ctl
    double gain = 0, gto = 0, dg = 0, ugain = 0, ugto = 0, dug = 0;   // the Fast steps' gain ramps, voiced and unvoiced (render_chan)
    double out = 0, out1 = 0;                                                   // this sample's voiced output, for tools/wavetrace
};

// A grain runs at the carrier frequency it was fired with. The chip resets the carrier phase at every
// grain, and the frequency it then runs at is the one latched with that reset, so a write lands on the
// grain after it rather than bending the one in flight.
//
// `gain` is the operator's level and it is not latched here: it arrives already slewed, one pole at
// cal::LEVEL_SLEW_MS. MEASURED 2026-09-20 and 2026-09-21 against two recordings that disagree with
// each other unless the smoothing is a fixed time. Vokodrone's intro is part 4 alone under a 35.6 Hz
// Fseq, and a level applied per sample cut the grain in flight there, stepping the operator output
// from 0.149 to 0.033 between two samples: 12.3 dB of click above its own floor at the frame
// boundary where rgwan's recording has 1.2. Latching the level to the grain fixed that and went too
// far. 12_fseqlevel drives one formant operator with an Fseq over five octaves and three frame
// rates, and a latched level is 10.6 dB of octave band error against the unit, worse than the 9.7
// of no smoothing at all: at a low fundamental the grain is tens of milliseconds and the level
// cannot wait that long. A 1.6 ms glide is 4.74 dB, and still holds Vokodrone's boundary to 2.7 dB.
//
// The unvoiced operator's level is left stepping. Its own segments in the same file sit 15 to 26 dB
// dark above 640 Hz before any smoothing is applied, which is the noise formant's bandwidth law and
// not this, so they cannot judge a glide; slewing it there only makes them darker.
//
// The sine, the stored waves and the odd and res forms read the phase generator's own integer phase, stepped
// by `inc` (cal.h); the formant runs its grains' carriers at `fop`, the same increment in hertz. ph0 is ours,
// op byte 0x1C's initial phase in turns (docs/Differences.md): it turns the operator's own phase, where pm is
// what the modulators send.
inline double op_sample(OpState& s, const OpV& v, double f0, double fop, uint32_t inc, int ratio, double pm, double gain, double ph0 = 0) {
    if (v.form <= 6) {
        s.phase += op_inc32(inc);
        const double ph = s.phase * (1.0 / 4294967296.0) + ph0;
        if (v.form == 0) return gain * fsin(ph + pm);
        // all1 and all2 are a stored waveform read by phase, the same way the sine is: one period of
        // the skirt's window with its mean taken out, so a modulator reaches it as phase like any other
        // form. MEASURED 2026-09-24 off the skirt sweep's sixty partials: all2's lines are the symmetric
        // window's own Fourier row C(p, p/2 - k) to 0.4 to 0.7 dB at every skirt, and all1's the
        // asymmetric window's to 0.4 to 1.3, where the two-period grain under a carrier at fop that this
        // replaces was 36 to 66 dB out by skirt 7. At skirt 0 the DC-free sin^2 is -cos / 2, the single
        // partial the unit gives, and the mean is gone because the unit's is: every all1/all2 take reads
        // mean / rms under 0.03 where a raw sin^2 pulse train sits at 0.7. docs/skirt.md.
        if (v.form <= 2) {
            double x = ph + pm; x -= floor(x);
            return gain * cal::FORM_LEVEL * 2.0 * (fwin(v.form == 1, v.skirt, x) - g_winDC[v.form == 1][v.skirt]);
        }
        // odd and res: a window times a carrier, each addressed by its own phase word with the modulation added to
        // both, which is how Yamaha's patent for the chip's forms builds its windowed carrier (US5610354, FIG. 11:
        // adders 42a/42b put the modulation into the window's address, 45a/45b into the carrier's, and the
        // carrier restarts with the window). The window's and the carrier's phases are the operator's own, so
        // with no modulator this is the windowed grain train measured below, line for line. They used to be
        // grains fired by an accumulator of their own with the modulation on the carrier alone, so a fixed
        // operator at 0 Hz fired none and passed nothing, where a sine shapes whatever modulates it.
        // INFERRED from the patent; no take yet drives these forms with a modulator or at 0 Hz.
        //
        // odd1/odd2 are a group that starts at the operator's own frequency. MEASURED: the unit gives partials 1
        // and 3 on odd1 and odd2, where the fixed quarter-period window at DC this once was gave a full harmonic
        // series. A window twice a period under a carrier at the operator's frequency puts its lines at fop,
        // 3 fop, 5 fop and nowhere else.
        // res1 and res2 put a three-partial group on harmonic ratio + 1 and are otherwise identical. MEASURED on
        // 2026-09-19 by sweeping register 0x230 through all hundred values on each: the peak lands on partial
        // ratio + 1 at every one of the hundred, on both forms, with the two partials either side 6 dB down and
        // everything else at the noise floor, and the peak level flat across the whole range. docs/formant.md.
        // The window is one period, FIXED, and the bandwidth has nothing to do with it. MEASURED on 2026-09-19
        // by sweeping register 0x218 through all hundred values on each form: the formant's window opens and
        // every one of the other six is flat to the byte. Voice byte 6 reaches these forms as register 0x230,
        // the ratio above. The "1" and "2" of each pair differ in the window's shape (g_win), not its length:
        // a sin^2 window exactly one period long makes the two partials either side of res's peak 6.02 dB down
        // and kills everything beyond them, which is what the unit gives at every one of a hundred settings.
        double w, c;
        if (v.form <= 4) { w = 2 * ph + pm; c = ph + pm; }                                   // odd: the window twice a period
        else { const double f = ph - floor(ph); w = f + pm; c = (ratio + 1) * f + pm; }       // res: on partial ratio + 1
        w -= floor(w);
        return gain * cal::FORM_LEVEL * 2.0 * fwin(v.form == 3 || v.form == 5, v.skirt, w) * fsin(c);
    }
    // The formant: grains under a window at the fundamental (INFERRED bw curve), the asymmetric window, each
    // restarting its carrier, which is what holds its peak still while the fundamental moves.
    const bool modulated = pm != 0;   // ph0 is ours and does not keep a grain reading
    pm += ph0;
    const double wl = std::min(s.wl7, 2.0);
    s.fphase += f0 / SR;
    if (s.fphase >= 1) {
        s.fphase -= 1; WinGen& g = s.g[s.nextGen]; g.on = true; g.w = 0; g.fc = fop; g.c = 0.0;
        s.nextGen ^= 1;
    }
    double winc = f0 / (wl * SR), y = 0;
    for (int k = 0; k < 2; k++) {
        WinGen& g = s.g[k]; if (!g.on) continue;
        // The modulation goes into the window's address as well as the carrier's (US5610354 FIG. 11, adders
        // 42a/42b and 45a/45b), and the address wraps, so a modulated grain does not end where the window does:
        // it rests at the window's end and the modulation keeps reading it, until its slot fires again.
        // MEASURED 2026-10-26 (FS1R.unlock 2026-10-26-open, takes 7 and 8): a formant at 0 Hz under a sine at
        // level 85 plays at the same level whatever its bandwidth (32 to 72) or the note (36 to 84), within 0.3
        // dB of the unit at all twenty-one settings, where a window that ended with the grain lost 4 to 23 dB.
        // Unmodulated, a grain is its window and nothing after, as before.
        g.w += winc;
        // ponytail: a modulator at exactly 0.0 that sample reads as unmodulated; an input flag if that ever shows.
        if (g.w >= 1 && (!modulated || gain == 0)) { g.on = false; continue; }
        double wa = std::min(g.w, 1.0) + (modulated ? pm : 0.0); wa -= floor(wa);
        y += gain * fwin(1, v.skirt, wa) * fsin(g.c + pm); g.c += g.fc / SR;
    }
    if (cal::FRMT_NORM != 0.0) y *= pow(1.0 / wl, cal::FRMT_NORM);
    return y;
}

// Ours, for a display: voiced operator v alone through op_sample, n points over two periods (operator.cpp).
void op_wave(const OpV& v, float* out, int n);
