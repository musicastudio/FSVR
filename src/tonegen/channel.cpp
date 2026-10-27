// tonegen/channel.cpp - a channel's control-rate refresh and its sample. INFERRED: the constants are in fs1r/cal.h.
#include "tonegen/channel.h"
#include "fs1r/algorithms.h"
#include "fs1r/tables.h"

bool is_carrier(const Voice& V, int o) { return V.custom ? V.v[o].level > 0 : FS1R_ALG[V.alg][2 * o + 1] & 1; }

void refresh_ctl(Chan& C, const Voice& V, bool fs) {
    const unsigned char* alg = FS1R_ALG[V.alg];
    C.f0 = op_hz(op_increment(C.regPitch + 0x1243 + C.regPM));    // channel fundamental incl. LFO pitch mod (pms 7)
    C.fbGain = V.fb ? cal::FEEDBACK * pow(2.0, V.fb - 7) : 0.0;    // INFERRED feedback scale
    // Ours (#92, docs/Differences.md): the custom matrix. A depth into another operator is on the output level's
    // curve (0 off), so a stock algorithm converts by copying each modulator's level; the diagonal is the operator's
    // own feedback, 0 off, else the feedback scale at fb = 7d / 99 (99 is the unit's 7). Common feedback (0x3D) plays no part.
    if (V.custom) for (int d = 0; d < 8; d++) {
        auto lvl = [](int l) { return l ? db2lin(-LEVEL_DB * 2 * LEVTAB[std::min(99, l)]) : 0.0; };
        C.mixG[d] = lvl(V.v[d].level);
        for (int s = 0; s < 8; s++) { const int m = V.mat[d][s]; C.matG[d][s] = s != d ? lvl(m) : m ? cal::FEEDBACK * pow(2.0, m * 7.0 / 99 - 7) : 0.0; }
    }
    for (int o = 0; o < 8; o++) {
        OpState& s = C.op[o]; const OpV& v = V.v[o]; const OpU& u = V.u[o];
        s.att = C.regLevel[o] * LEVEL_DB + am_att(C.regAM, v.ams);
        if (!V.custom && ((alg[2 * o + 1] & 1) || (V.corrEdit >> o & 1))) s.att += cal::CARRIER_DB * V.corr[o];   // carrier level correction (the 0x200 word), on a modulator after an edit
        // Ours, and bit-identical: an operator whose EG is over, with no grain in flight and no level
        // boost that could lift it back over the -100 dB gate before the next refresh, never reads its
        // frequency maths again. The carrier correction can reach -22.5 dB, hence the -20 dB bound on
        // both the target attenuation and the slewed one. MEASURED 2026-09-28 on the Fat Line demo:
        // refresh_ctl was a quarter of the render and half of that was for finished operators.
        if (s.eg.done() && !s.g[0].on && !s.g[1].on && s.att >= -20 && s.attS >= -20) goto unvoiced;
        {
        int pmw = (int)(C.regPM * cal::PMS_FRAC[v.pms]) + C.vcFreq[o][0];
        // The detune word goes into the frequency word rather than onto the result: it is a pitch
        // word offset on the chip, and the formant branch already carries it inside frmtWord.
        int det = v.form == 7 ? 0 : C.detW[o];
        double w;
        // A frame's frequency word goes into the operator's own frequency register, it does not stand
        // in for the whole register chain. A ratio operator still has the channel pitch added on top,
        // which is what the format means for one: the word is an absolute formant centre, so a ratio
        // operator handed one runs off the end of the register. MEASURED 2026-09-21: 12_fseqlevel's
        // three sine segments sum to 39632..45056 at notes 36, 60 and 84 and the unit gives the same
        // 23982 Hz line in all three, the saturated word, where the engine read the frame word alone
        // and sang at 275 Hz. Formant and fixed operators do not add the channel pitch.
        // Transpose is the formant operator's pitch, the grain rate, not this centre. Detune is the
        // other half of the 0x230 word (FUN_00013bc6) and stays on the centre the coarse and fine set.
        // A frame replaces that centre and does not take the word.
        if (fs && C.fseqOp[o]) w = C.fqWord[o] + pmw + det + (v.form != 7 && !v.fixed ? C.regPitch : 0);
        else if (v.form == 7) w = C.freqWord[o] + C.fbW[o] + C.detW[o] + pmw + (C.regFM * v.fms) / 7;
        else if (!v.fixed) w = C.freqWord[o] + C.regPitch + pmw + det;
        else w = C.freqWord[o] + C.fbW[o] + pmw + det + (C.regFM * v.fms) / 7;
        // The word through the phase generator (cal.h): the increment the chip steps the phase by, and in hertz
        // the frequency the grain forms run their carriers at.
        s.word = w; s.inc = op_increment(w); s.fop = op_hz(s.inc);
        // The formant's window runs at the channel pitch through the operator's own pitch mod sense, not
        // through the full LFO word: rgwan's demo take of 15_Human (pms 1..4, pmd 12) wobbles its formant
        // harmonics 33..48 cents, the same as its pms 2 sine carriers, where a window on C.f0 gave 541.
        // The transpose word rides that pitch. The width stays the same number of Hz, so the window
        // length in periods follows the grain rate. The grain rate goes through the phase generator as
        // every other rate does: on the unit a fixed formant and a fixed sine on the same word hold still
        // against each other, where the smooth law put the formant 0.73 cents under (2026-10-24).
        double base = C.regPitch + 0x1243 + pmw - C.vcFreq[o][0];
        int trans = v.form == 7 ? C.frmtWord[o] - 0x1243 - C.detW[o] : 0;
        s.fw = op_hz(op_increment(base + trans));
        s.bw = clampi(C.bwReg[o] + C.vcBw[o][0], 0, 99);                 // register 0x218, the formant's
        s.ratio = v.form == 7 ? 0 : clampi(C.frmtWord[o], 0, 99);         // register 0x230, every other form's
        double wl = C.f0 / (cal::FRMT_BW_HZ0 * fm::exp2(s.bw / cal::FRMT_BW_DB));
        if (trans) wl *= s.fw / op_hz(op_increment(base));
        s.wl7 = std::min(cal::FRMT_WL_MAX, wl);
        }
        unvoiced:
        if (s.ueg.done()) continue;   // the same, and simpler: the unvoiced attenuation is never negative, so a finished noise EG is gated for good
        // unvoiced (noise formant) operator
        s.uatt = C.regULevel[o] * LEVEL_DB + am_att(C.regAM, u.ams);
        double nf;
        if (fs && C.fseqUOp[o]) nf = op_hz(op_increment(C.fquWord[o]));
        else if (u.mode == 2 && v.form == 7) nf = op_hz(op_increment(C.freqWord[o] + C.detW[o]));
        // MEASURED: link-ff on a voiced partner that is not a formant operator reaches register 0x300 as
        // link-fo, not as itself. uv-linkff-sine's image reads mode 1 where the sysex asked for 2.
        else if (u.mode) nf = C.f0;
        else nf = op_hz(op_increment(C.ufreqWord[o] + C.ufbW[o] + (C.regFM * u.fms) / 7 + C.vcFreq[o][1]));
        s.nf = nf * fm::exp2(u.transpose / 12.0);
        // MEASURED noise formant, docs/noise.md and cal.h: two unequal digital one-poles on white noise,
        // their coefficients and the band's peak gain read off the tables against the register and the
        // skirt. The gain is the band's PEAK, not its RMS, which is what the fit measured; the RMS follows
        // from the coefficients and is what the resonance carrier is set against.
        int ureg = clampi(C.ubwReg[o] + C.vcBw[o][1], 0, 127);
        NoiseBand nb = noise_band(ureg, u.skirt);
        s.na = nb.a1; s.na2 = nb.a2;
        double resDc, resNoise; ures(ureg, u.res, resDc, resNoise);
        s.nscale = db2lin(nb.gdb + cal::NOISE_LEVEL_DB) * resNoise;
        // Bandwidth register 0 silences the noise and leaves the resonance carrier alone. The capture set
        // cannot see the split, since every segment it has at bandwidth 0 is at resonance 0 too, and the
        // demo settles it: Kalimba runs two unvoiced operators at bandwidth 0 with resonance 5 and 7, and
        // taking the carrier down with the noise costs it 3.9 dB of band tilt where leaving it alone costs
        // nothing anywhere else. So the carrier is set against the band the register would give at 5.
        NoiseBand nc = ureg == 0 ? noise_band(5, u.skirt) : nb;
        s.nres = db2lin(nc.gdb + cal::NOISE_LEVEL_DB) * sqrt(noise_band_var(nc.a1, nc.a2)) * resDc;
    }
}

// The level slew over n samples at once, for the Fast steps' blocks.
static double slew_n(int n) { static double k[65] = {}; n = std::clamp(n, 1, 64); if (k[n] == 0) k[n] = 1.0 - pow(1.0 - LEVEL_SLEW_K, n); return k[n]; }

double render_chan(Chan& C, const Voice& V, bool fseq, double gateDb, int ctlDecimation, int egDecimation) {
    if (C.silent) {
        // Ours. The carriers and the noise are over, so the channel's output
        // is zero from here to its end; what is left is when the allocator may treat it as free, which
        // is the sample its last envelope passes -120 dB. Every envelope still running is in its release
        // and falling linearly, so that sample is arithmetic, and the channel costs nothing until then.
        // The hardware renders these channels in full, at no cost; on the Fat Line demo they were half
        // of the 32 channels for the whole song (docs/performance.md, 2026-09-28).
        if (C.freezeLeft < 0) {
            long long worst = 0; bool can = true;
            for (auto& s : C.op) for (const EG* e : {&s.eg, &s.ueg}) {
                if (e->done()) continue;
                if (e->stage != 4 || e->rising || e->rate <= 0) { can = false; break; }
                long long n = (long long)((e->cur + 120.0) / e->rate) + 2; if (n > worst) worst = n;
            }
            if (can) C.freezeLeft = worst;
        }
        if (C.freezeLeft >= 0) { if (--C.freezeLeft <= 0) C.active = false; }
        else {
            // An envelope still rising, or holding: tick them until they are all falling.
            bool alive = false;
            for (auto& s : C.op) { s.eg.tick(); s.ueg.tick(); if (!s.eg.done() || !s.ueg.done()) alive = true; }
            if (!alive) C.active = false;
        }
        C.last = 0.0;
        return 0.0;
    }
    if (C.ctlLeft-- <= 0) { C.ctlLeft = ctlDecimation - 1; refresh_ctl(C, V, fseq); }
    // Ours, the Fast steps (Device::setSpeed): with egDecimation n the amplitude EGs and the level slew advance n
    // samples at a block's start and each gain ramps linearly to where they land, which takes the EG tick and the
    // dB-to-gain conversion out of every sample but one in n. Full and Good, at 1, run them every sample.
    const bool decim = egDecimation > 1, block = decim && C.egLeft-- <= 0;
    if (block) C.egLeft = egDecimation - 1;
    const unsigned char* alg = FS1R_ALG[V.alg];
    double partV = C.partV, partU = C.partU;
    // The chip's own cut (cal.h) where it is above the speed's gate: an operator past it plays nothing at all.
    const double vGate = std::max(gateDb, -cal::OP_CUT_DB), uGate = std::max(gateDb, -cal::NOISE_CUT_DB);
    double Cb = 0, H = 0, S = 0, fbNew = 0, mix = 0;
    for (int o = 0; o < 8; o++) {
        OpState& s = C.op[o]; const OpV& v = V.v[o];
        unsigned char t0 = alg[2 * o], t1 = alg[2 * o + 1]; int F = (t0 >> 3) & 7;
        double in = F == 3 ? C.fbBus * C.fbGain : F == 4 ? Cb : F == 5 ? H : F == 6 ? S : 0.0;
        // Ours (#92): the custom matrix. An operator ahead of this one gives this sample's output, one behind it the
        // last sample's, and its own feedback the mean of its last two as the feedback bus does.
        if (V.custom) {
            in = 0;
            for (int k = 0; k < 8; k++) if (C.matG[o][k] != 0.0) in += C.matG[o][k] * (k == o ? (s.out + s.out1) * 0.5 : C.op[k].out);
        }
        double y = 0, gain;
        // The level register glides rather than stepping, so a frame write reaches the output over
        // about a millisecond instead of cutting the waveform. A silenced operator still runs while a
        // grain it already fired is in flight, so the last one fades out under its own window.
        if (!decim) {
            double egdb = s.eg.tick();
            if (s.attS < -1e8) s.attS = s.att; else s.attS += (s.att - s.attS) * LEVEL_SLEW_K;
            gain = egdb - s.attS > vGate ? db2lin_fast(egdb - s.attS) : 0.0;
        } else {
            if (block) {
                const double egdb = s.eg.advance(egDecimation);
                if (s.attS < -1e8) s.attS = s.att; else s.attS += (s.att - s.attS) * slew_n(egDecimation);
                s.gain = s.gto; s.gto = egdb - s.attS > vGate ? db2lin_fast(egdb - s.attS) : 0.0;
                s.dg = (s.gto - s.gain) / egDecimation;
            }
            gain = s.dg == 0.0 ? s.gain : (s.gain += s.dg);
        }
        if (gain != 0.0 || s.g[0].on || s.g[1].on) {
            double fop = s.fop; uint32_t inc = s.inc;
            // Every operator takes its frequency EG, fixed ones included. MEASURED 2026-10-24 off Wouter's
            // FS1R playing A123 Drum Kit 3 (all fixed operators, flat pitch EG, the kick sweeps down), and
            // 2026-10-25 on rgwan's rig: fixed 1 kHz sines under six FEG settings, onset-feg's among them,
            // track this ramp to within 1% from the first cycle (docs/findings.md 2026-10-25).
            // The EG moves the word, so the phase generator takes the moved word every sample of the ramp.
            if (s.feg.stage < 2) { inc = op_increment(s.word + s.feg.tick() * (1024.0 / 12.0)); fop = op_hz(inc); }
            y = op_sample(s, v, s.fw, fop, inc, s.ratio, in * FM_INDEX, gain, C.phOff[o]);
        } else s.phase += op_inc32(s.inc);   // a silent operator's phase generator runs on (Synth::note_on)
        // A read leaves the sum bus as it is; t1 bit 1 starts it over, after this operator's read and
        // before its add. INFERRED from the table, not measured: the bit is set wherever a second sum
        // starts and never over an add no operator has read yet, and so 65 and 80 fan out to every
        // reader as the Algorithm Sheet draws them (docs/ymp706_registers.md, the algorithm word).
        s.out1 = s.out; s.out = y;
        if (V.custom) mix += y * C.mixG[o] * partV; else {
        Cb = y; if (t0 & 2) H = y; if (t1 & 2) S = 0; if (t1 & 4) S += y; if (t0 & 4) fbNew = y;
        if (t1 & 1) mix += y * partV;
        }
        // unvoiced (noise formant) operator
        double ugain;
        if (!decim) { const double uegdb = s.ueg.tick(); ugain = uegdb - s.uatt > uGate ? db2lin_fast(uegdb - s.uatt) : 0.0; }
        else {
            if (block) { const double uegdb = s.ueg.advance(egDecimation); s.ugain = s.ugto; s.ugto = uegdb - s.uatt > uGate ? db2lin_fast(uegdb - s.uatt) : 0.0; s.dug = (s.ugto - s.ugain) / egDecimation; }
            ugain = s.dug == 0.0 ? s.ugain : (s.ugain += s.dug);
        }
        if (ugain != 0.0) {
            double nf = s.nf;
            if (s.ufeg.stage < 2) nf *= fm::exp2(s.ufeg.tick() / 12.0);
            s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
            // Uniform in [-1, 1) scaled to unit variance, so the tables in cal.h read in the band's own RMS.
            double nz = ((int32_t)s.rng) * (1.7320508 / 2147483648.0);
            s.lp[0] += (nz - s.lp[0]) * s.na; s.lp[1] += (s.lp[0] - s.lp[1]) * s.na2;
            nz = s.lp[1] * s.nscale + s.nres;
            s.nphase += nf / SR; if (s.nphase >= 1) s.nphase -= 1;
            // The unvoiced output is the mean of this sample and the last, see cal.h: the unit's noise
            // falls off above 8 kHz on a cos^2 that no voiced operator shows, whichever centre it is at.
            double u1 = nz * fsin(s.nphase);
            mix += 0.5 * (u1 + s.uprev) * ugain * partU;
            s.uprev = u1;
        }
    }
    C.fbBus = (fbNew + C.fbPrev) * 0.5; C.fbPrev = fbNew;
    bool alive = false;
    for (auto& s : C.op) if (!s.eg.done() || !s.ueg.done()) { alive = true; break; }
    if (!alive) C.active = false;
    // The channel goes silent once every carrier's EG is over with no grain in flight and every noise EG
    // is over: 64 samples of nothing, with no damp left from the note before. An EG falling in its release
    // under the gate is over too, since nothing it does from there is computed: a release time of 99 on an
    // operator at output level 0 kept A020 Vox Morph's channels computing silence for minutes, and with them
    // the engine awake (render.cpp). Only a level raised by a controller mid-release could bring one back.
    auto over = [&](const EG& e, double att, double gate) { return e.done() || (e.stage == 4 && !e.rising && e.cur - att <= gate); };
    bool carriersDone = true;
    for (int o = 0; o < 8; o++) { const OpState& s = C.op[o]; if ((is_carrier(V, o) && (!over(s.eg, s.attS, vGate) || s.g[0].on || s.g[1].on)) || !over(s.ueg, s.uatt, uGate)) { carriersDone = false; break; } }
    if (carriersDone && fabs(mix) < 1e-9 && C.damp == 0.0) { if (++C.quiet >= 64) C.silent = true; } else C.quiet = 0;
    // The channel accumulator saturates before the filter loop, which is where the capture puts it:
    // eight carriers stacked in one channel clip flat, while the filter's own output on ingain-12 rides
    // 4 dB above that ceiling, so nothing downstream of CHOUT can be what clips.
    mix = std::clamp(mix, -cal::CHAN_CLIP, cal::CHAN_CLIP);
    // Remember this channel's own output, so a note-on that takes this channel has something to fade,
    // and add whatever the previous note left behind (FUN_00023000, see note_on).
    C.last = mix;
    if (C.damp != 0.0) {
        mix += C.damp;
        C.damp *= DAMP_K;
        if (fabs(C.damp) < 1e-7) C.damp = 0.0;
        else C.active = true;                      // a damp still sounding keeps the channel rendering
    }
    return mix;
}
