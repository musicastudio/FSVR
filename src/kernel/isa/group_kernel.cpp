// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// The tone generator's channels W at a time: what render_chan (tonegen/channel.cpp) and op_sample (tonegen/operator.h)
// compute for one channel's sample, operation for operation on lanes, over a segment. Compiled once per instruction
// set (kernel/README.md); tools/abcheck.py holds the result to the scalar loop's, and the self check's segments
// scenario to the bit.
//
// A group's lanes are channels of any voices: the algorithm's buses and the forms are per-lane masks and table
// pages (tonegen/lanes.h), decoded once a segment. Where the scalar code branches, a lane takes both sides and
// selects; the few things that happen once a note (an EG segment's end, a control refresh, a frequency EG's
// ramp, a formant's grains) are done lane by lane in scalar code on the group's rows. A lane whose channel goes
// silent or inactive before the segment's end is put back as it was and left to the scalar loop (Group::flipAt).
// The Fast steps (egDecimation > 1) do not come here.
#include <cmath>
#include <cstring>
#include "kernel/simd.h"
#include "kernel/tonegen.h"
#include "tonegen/lanes.h"
#include "fs1r/algorithms.h"

namespace fsvr::kernel::FSVR_ISA_NS {

using namespace fsvr::simd::FSVR_ISA_NS;
using fsvr::tonegen::Group;
using Op = Group::Op;

namespace {

const double FM = FM_INDEX;

inline vm mask_of(bool b) { return b ? mtrue() : mfalse(); }
inline void setmask(uint64_t* p, int l, bool b) { p[l] = b ? ~0ull : 0ull; }

// ---- the tables, read W at a time, as math/fastmath.h and tonegen/operator.h read them one at a time ----

// fm::sin_turns: the turn's fraction on the 4096-step table, a float lerp, widened.
inline vd sin_turns(vd t) {
    const vd x = mul(sub(t, floor(t)), set1(4096.0));
    const vi i = trunc_i(x);
    const vf f = cvt_d2f(sub(x, cvt_i2d(i)));
    const vf a = gather_f(fm::g_sin, i), b = gather_f(fm::g_sin, iadd(i, iset1(1)));
    return cvt_f2d(fadd(a, fmul(fsub(b, a), f)));
}
// db2lin_fast: -128 dB and under is silence; the dB table in 1/16 dB steps; 16 dB and over is off the table and
// takes the library's pow, lane by lane.
inline vd db2lin_fast(vd db) {
    const vd x = mul(add(db, set1(128.0)), set1(16.0));
    vi i = trunc_i(x);
    i = iselect(ilt(i, iset1(0)), iset1(0), iselect(ige(i, iset1(2304)), iset1(2303), i));   // in range for every lane; the others are selected away below
    const vf f = cvt_d2f(sub(x, cvt_i2d(i)));
    const vf a = gather_f(fm::g_db2lin, i), b = gather_f(fm::g_db2lin, iadd(i, iset1(1)));
    vd r = cvt_f2d(fadd(a, fmul(fsub(b, a), f)));
    const vm over = ge(db, set1(16.0));
    if (any(over)) { const int m = movemask(over); for (int l = 0; l < W; l++) if (m >> l & 1) r = setlane(r, l, ::pow(10.0, lane(db, l) / 20.0)); }
    return select(le(db, set1(-128.0)), set1(0.0), r);
}
// fwin: the window page at x, a float lerp; past the page's end it is 0.
inline vf fwin(vi at, vd x) {
    const vd y = mul(x, set1(1024.0));
    vi i = trunc_i(y);
    const vmi over = ige(i, iset1(1024));
    i = iselect(over, iset1(0), i);
    const vf f = cvt_d2f(sub(y, cvt_i2d(i)));
    const float* base = &g_win[0][0][0];
    const vf a = gather_f(base, iadd(at, i)), b = gather_f(base, iadd(at, iadd(i, iset1(1))));
    return fselect(over, fset1(0.f), fadd(a, fmul(fsub(b, a), f)));
}

// ---- the amplitude EG, W at a time: EG::tick, with a segment's end done lane by lane ----

// EG::next on one lane: the stage's target and rate from the channel's own copy of the levels and rates.
void eg_next(int32_t* stage, double* cur, double* target, double* rate, uint64_t* rising, int l, const EG& e, int s) {
    stage[l] = s;
    if (s > 4) { target[l] = -200; return; }
    const double tg = e.L[s - 1] >= 63 ? -200.0 : -cal::EG_LEVEL_DB * e.L[s - 1];   // EG::lvl_db, restated: a class's inline member must not come from this file (kernel/README.md)
    const double secs = rate_secs(e.R[s - 1] + e.rs);
    const bool up = tg > cur[l];
    if (up && cur[l] < cal::EG_ATTACK_FLOOR) cur[l] = tg < cal::EG_ATTACK_FLOOR ? tg : cal::EG_ATTACK_FLOOR;
    target[l] = tg; setmask(rising, l, up);
    rate[l] = up ? 1.0 - exp(-1.0 / (secs * cal::EG_ATTACK_K * SR + 1)) : 96.0 / (secs * SR + 1);
}

struct EgRows { int32_t* stage; double* cur; double* target; double* rate; double* hold; uint64_t* rising; };

// One sample of W EGs; egs are the channels' copies, for the levels and rates a new segment needs.
inline vd eg_tick(const EgRows& r, const EG* const* egs) {
    const vi stage = iload(r.stage);
    if (!any(widen(ilt(stage, iset1(5))))) return load(r.cur);   // every lane past its last segment: EG::tick returns cur
    const vmi isHold = ieq(stage, iset1(0));
    const vmi isRun = ige(stage, iset1(1));
    const vmi isRun4 = ilt(stage, iset1(5));
    const vm run = mand(widen(isRun), widen(isRun4)), hold = widen(isHold);
    // the hold counts down
    vd holdLeft = load(r.hold);
    holdLeft = select(hold, sub(holdLeft, set1(1.0)), holdLeft);
    store(r.hold, holdLeft);
    // a running segment approaches or falls toward its target and stops on it
    vd cur = load(r.cur);
    const vd target = load(r.target), rate = load(r.rate);
    const vm rising = loadmask(r.rising);
    const vd risen = add(cur, mul(sub(set1(cal::EG_OVERSHOOT), cur), rate));
    const vd fallen = sub(cur, rate);
    const vd next = select(rising, risen, fallen);
    const vm hit = mand(run, mselect(rising, ge(next, target), le(next, target)));
    cur = select(run, select(hit, target, next), cur);
    store(r.cur, cur);
    // stage 4 ends on its target; stages 1 to 2 start the next segment, which is the channel's arithmetic
    istore(r.stage, iselect(narrow(mand(hit, widen(ieq(stage, iset1(4))))), iset1(5), stage));
    const vm holdEnd = mand(hold, le(holdLeft, set1(0.0)));
    const int ev = movemask(mor(holdEnd, mand(hit, widen(ilt(stage, iset1(3))))));
    if (ev) for (int l = 0; l < W; l++) if (ev >> l & 1) eg_next(r.stage, r.cur, r.target, r.rate, r.rising, l, *egs[l], r.stage[l] == 0 ? 1 : r.stage[l] + 1);
    return load(r.cur);
}
inline vm eg_done(const EgRows& r) {   // stage == 5 || (stage == 4 && cur <= -120)
    const vi stage = iload(r.stage);
    return mor(widen(ieq(stage, iset1(5))), mand(widen(ieq(stage, iset1(4))), le(load(r.cur), set1(-120.0))));
}
// render_chan's over(): done, or releasing under the gate
inline vm eg_over(const EgRows& r, vd att, double gate) {
    const vi stage = iload(r.stage);
    return mor(eg_done(r), mand(mand(widen(ieq(stage, iset1(4))), mnot(loadmask(r.rising))), le(sub(load(r.cur), att), set1(gate))));
}

// ---- the frequency EG on one lane: FreqEG::tick ----
double feg_tick(int32_t* stage, int32_t* hold, double* cur, const double* target, const double* katt, const double* kdec, int l) {
    if (hold[l] > 0) { hold[l]--; return cur[l]; }
    if (stage[l] == 0) {
        const double d = target[l] - cur[l];
        if (fabs(d) <= katt[l]) { cur[l] = target[l]; stage[l] = 1; } else cur[l] += d < 0 ? -katt[l] : katt[l];
    } else if (stage[l] == 1) {
        if (fabs(cur[l]) <= kdec[l]) { cur[l] = 0; stage[l] = 2; } else cur[l] += cur[l] < 0 ? kdec[l] : -kdec[l];
    }
    return cur[l];
}

// ---- the formant on one lane: op_sample's grains ----
double formant(Op& P, int l, double f0, double fop, double pm, double gain, const float* win) {
    const bool modulated = pm != 0;
    pm += P.ph0[l];
    const double wl = P.wl7[l] < 2.0 ? P.wl7[l] : 2.0;
    P.fphase[l] += f0 / SR;
    if (P.fphase[l] >= 1) {
        P.fphase[l] -= 1; const int k = P.nextGen[l];
        P.gOn[k][l] = ~0ull; P.gW[k][l] = 0; P.gFc[k][l] = fop; P.gC[k][l] = 0.0;
        P.nextGen[l] ^= 1;
    }
    const double winc = f0 / (wl * SR); double y = 0;
    for (int k = 0; k < 2; k++) {
        if (!P.gOn[k][l]) continue;
        P.gW[k][l] += winc;
        if (P.gW[k][l] >= 1 && (!modulated || gain == 0)) { P.gOn[k][l] = 0; continue; }
        double wa = (P.gW[k][l] < 1.0 ? P.gW[k][l] : 1.0) + (modulated ? pm : 0.0); wa -= fm::floor_exact(wa);
        // fwin(1, skirt, wa) on the asymmetric page, as operator.h's fwin reads it
        const double yy = wa * 1024.0; const int i = (int)yy; float wv = 0.f;
        if (i < 1024) { const float f = (float)(yy - i); wv = win[i] + (win[i + 1] - win[i]) * f; }
        y += gain * wv * fm::sin_turns(P.gC[k][l] + pm); P.gC[k][l] += P.gFc[k][l] / SR;
    }
    if (cal::FRMT_NORM != 0.0) y *= pow(1.0 / wl, cal::FRMT_NORM);
    return y;
}

// ---- the group from its channels, and back ----

void decode_op(Group& G, int l, int o) {
    Op& P = G.op[o];
    const Chan& C = *G.chan[l]; const Voice& V = *G.voice[l];
    const OpState& s = C.op[o]; const OpV& v = V.v[o];
    const unsigned char* alg = FS1R_ALG[V.alg];
    const unsigned char t0 = alg[2 * o], t1 = alg[2 * o + 1]; const int F = (t0 >> 3) & 7;
    setmask(P.inFb, l, F == 3); setmask(P.inCb, l, F == 4); setmask(P.inH, l, F == 5); setmask(P.inS, l, F == 6);
    setmask(P.toH, l, t0 & 2); setmask(P.sClr, l, t1 & 2); setmask(P.sAdd, l, t1 & 4); setmask(P.toFb, l, t0 & 4); setmask(P.carrier, l, t1 & 1);
    const int form = v.form;
    setmask(P.sine, l, form == 0); setmask(P.all, l, form == 1 || form == 2); setmask(P.odd, l, form == 3 || form == 4);
    setmask(P.res, l, form == 5 || form == 6); setmask(P.frmt, l, form == 7);
    const int fam = form == 7 ? 1 : (form == 1 || form == 3 || form == 5) ? 1 : 0;
    P.winAt[l] = (int32_t)(&g_win[fam][v.skirt][0] - &g_win[0][0][0]); P.skirt[l] = v.skirt;
    P.dc[l] = form == 1 || form == 2 ? g_winDC[form == 1][v.skirt] : 0.f;
    P.formA[l] = form == 3 || form == 4 ? 2.0 : 1.0;
    P.formB[l] = form == 5 || form == 6 ? (double)(s.ratio + 1) : 1.0;
}

// The control values a refresh writes (refresh_op), into the lane.
void load_ctl(Group& G, int l, int o) {
    Op& P = G.op[o]; const OpState& s = G.chan[l]->op[o];
    P.inc[l] = (int32_t)s.inc; P.word[l] = s.word; P.fop[l] = s.fop; P.fw[l] = s.fw; P.wl7[l] = s.wl7; P.ratio[l] = s.ratio;
    P.att[l] = s.att; P.uatt[l] = s.uatt; P.ninc[l] = s.ninc; P.ucoef[l] = s.ucoef; P.udrift[l] = s.udrift; P.uwidth[l] = s.uwidth;
    P.formB[l] = (G.voice[l]->v[o].form == 5 || G.voice[l]->v[o].form == 6) ? (double)(s.ratio + 1) : 1.0;
}

void gather(Group& G) {
    for (int l = 0; l < G.lanes; l++) {
        Chan& C = *G.chan[l];
        memcpy(G.snapOp[l], C.op, sizeof C.op);
        Group::Snap& S = G.snap[l];
        S.fbBus = C.fbBus; S.fbPrev = C.fbPrev; S.last = C.last; S.damp = C.damp; S.f0 = C.f0; S.fbGain = C.fbGain;
        S.quiet = C.quiet; S.ctlLeft = C.ctlLeft; S.freezeLeft = C.freezeLeft; S.ran = C.ran; S.silent = C.silent; S.active = C.active;
        S.ctlSerialC = C.ctlSerialC; memcpy(S.ctlSerialV, C.ctlSerialV, sizeof S.ctlSerialV); memcpy(S.ctlSerialU, C.ctlSerialU, sizeof S.ctlSerialU);
        G.fbBus[l] = C.fbBus; G.fbPrev[l] = C.fbPrev; G.fbGain[l] = C.fbGain; G.partV[l] = C.partV; G.partU[l] = C.partU;
        G.last[l] = C.last; G.damp[l] = C.damp; G.quiet[l] = C.quiet; G.ctlLeft[l] = C.ctlLeft;
        setmask(G.silent, l, C.silent); setmask(G.active, l, C.active);
        G.flipAt[l] = -1;
        for (int o = 0; o < Group::OPS; o++) {
            Op& P = G.op[o]; const OpState& s = C.op[o];
            decode_op(G, l, o); load_ctl(G, l, o);
            P.phase[l] = (int32_t)s.phase; P.ph0[l] = C.phOff[o];
            P.fegStage[l] = s.feg.stage; P.fegHold[l] = s.feg.hold; P.fegCur[l] = s.feg.cur; P.fegTarget[l] = s.feg.target; P.fegKatt[l] = s.feg.katt; P.fegKdec[l] = s.feg.kdec;
            P.egStage[l] = s.eg.stage; P.egCur[l] = s.eg.cur; P.egTarget[l] = s.eg.target; P.egRate[l] = s.eg.rate; P.egHold[l] = s.eg.holdLeft; setmask(P.egRising, l, s.eg.rising);
            P.attS[l] = s.attS; P.uattS[l] = s.uattS;
            for (int k = 0; k < 2; k++) { P.gW[k][l] = s.g[k].w; P.gC[k][l] = s.g[k].c; P.gFc[k][l] = s.g[k].fc; setmask(P.gOn[k], l, s.g[k].on); }
            P.nextGen[l] = s.nextGen; P.fphase[l] = s.fphase;
            P.uegStage[l] = s.ueg.stage; P.uegCur[l] = s.ueg.cur; P.uegTarget[l] = s.ueg.target; P.uegRate[l] = s.ueg.rate; P.uegHold[l] = s.ueg.holdLeft; setmask(P.uegRising, l, s.ueg.rising);
            P.ufegStage[l] = s.ufeg.stage; P.ufegHold[l] = s.ufeg.hold; P.ufegCur[l] = s.ufeg.cur; P.ufegTarget[l] = s.ufeg.target; P.ufegKatt[l] = s.ufeg.katt; P.ufegKdec[l] = s.ufeg.kdec;
            P.rng[l] = (int32_t)s.rng; P.lp0[l] = s.lp[0]; P.lp1[l] = s.lp[1]; P.walk[l] = s.walk; P.nphase[l] = s.nphase;
            P.out[l] = s.out;
        }
    }
    // An empty lane is a finished channel that reads nothing out of range and feeds nothing
    for (int l = G.lanes; l < W; l++) {
        G.fbBus[l] = G.fbPrev[l] = G.fbGain[l] = G.partV[l] = G.partU[l] = G.last[l] = G.damp[l] = 0; G.quiet[l] = 0; G.ctlLeft[l] = 1 << 30;
        G.silent[l] = 0; G.active[l] = 0; G.flipAt[l] = -1;
        for (int o = 0; o < Group::OPS; o++) {
            Op& P = G.op[o];
            P.phase[l] = P.inc[l] = 0; P.word[l] = P.fop[l] = P.fw[l] = 0; P.wl7[l] = 1; P.ratio[l] = 0;
            P.fegStage[l] = 2; P.fegHold[l] = 0; P.fegCur[l] = P.fegTarget[l] = P.fegKatt[l] = P.fegKdec[l] = 0;
            P.egStage[l] = 5; P.egCur[l] = P.egTarget[l] = -200; P.egRate[l] = P.egHold[l] = 0; P.egRising[l] = 0;
            P.att[l] = P.attS[l] = P.uattS[l] = P.ph0[l] = 0;
            for (int k = 0; k < 2; k++) { P.gW[k][l] = 1; P.gC[k][l] = P.gFc[k][l] = 0; P.gOn[k][l] = 0; }
            P.nextGen[l] = 0; P.fphase[l] = 0;
            P.sine[l] = ~0ull; P.all[l] = P.odd[l] = P.res[l] = P.frmt[l] = 0; P.winAt[l] = 0; P.skirt[l] = 0; P.dc[l] = 0.f; P.formA[l] = P.formB[l] = 1;
            P.uegStage[l] = 5; P.uegCur[l] = P.uegTarget[l] = -200; P.uegRate[l] = P.uegHold[l] = 0; P.uegRising[l] = 0;
            P.ufegStage[l] = 2; P.ufegHold[l] = 0; P.ufegCur[l] = P.ufegTarget[l] = P.ufegKatt[l] = P.ufegKdec[l] = 0;
            P.uatt[l] = P.ninc[l] = P.ucoef[l] = P.udrift[l] = P.uwidth[l] = 0; P.rng[l] = 1; P.lp0[l] = P.lp1[l] = P.walk[l] = P.nphase[l] = 0;
            P.inFb[l] = P.inCb[l] = P.inH[l] = P.inS[l] = P.toH[l] = P.sClr[l] = P.sAdd[l] = P.toFb[l] = P.carrier[l] = 0;
            P.out[l] = 0;
        }
    }
    for (int o = 0; o < Group::OPS; o++) {
        Op& P = G.op[o];
        P.fegOn = P.ufegOn = 0;
        for (int l = 0; l < G.lanes; l++) { if (P.fegStage[l] < 2) P.fegOn |= 1 << l; if (P.ufegStage[l] < 2) P.ufegOn |= 1 << l; }
        P.anySine = P.anyWin = P.anyAll = P.anyFrmt = P.anyRes = P.anyOdd = false;
        for (int l = 0; l < G.lanes; l++) {
            const int form = G.voice[l]->v[o].form;
            P.anySine |= form <= 6 && (form == 0 || form >= 3); P.anyWin |= form >= 1 && form <= 6; P.anyAll |= form == 1 || form == 2;
            P.anyFrmt |= form == 7; P.anyRes |= form == 5 || form == 6; P.anyOdd |= form == 3 || form == 4;
        }
    }
}

// A lane's channel after the segment, or back to what it was.
void scatter(Group& G, int l, int n) {
    Chan& C = *G.chan[l];
    if (G.flipAt[l] >= 0) {
        memcpy(C.op, G.snapOp[l], sizeof C.op);
        const Group::Snap& S = G.snap[l];
        C.fbBus = S.fbBus; C.fbPrev = S.fbPrev; C.last = S.last; C.damp = S.damp; C.f0 = S.f0; C.fbGain = S.fbGain;
        C.quiet = S.quiet; C.ctlLeft = S.ctlLeft; C.freezeLeft = S.freezeLeft; C.ran = S.ran; C.silent = S.silent; C.active = S.active;
        C.ctlSerialC = S.ctlSerialC; memcpy(C.ctlSerialV, S.ctlSerialV, sizeof C.ctlSerialV); memcpy(C.ctlSerialU, S.ctlSerialU, sizeof C.ctlSerialU);
        return;
    }
    C.fbBus = G.fbBus[l]; C.fbPrev = G.fbPrev[l]; C.last = G.last[l]; C.damp = G.damp[l]; C.quiet = G.quiet[l]; C.ctlLeft = G.ctlLeft[l];
    C.silent = G.silent[l] != 0; C.active = G.active[l] != 0;
    C.ran = G.now + n;                         // every sample stepped: the lane was not silent
    for (int o = 0; o < Group::OPS; o++) {
        Op& P = G.op[o]; OpState& s = C.op[o];
        s.phase = (uint32_t)P.phase[l];
        s.feg.stage = P.fegStage[l]; s.feg.hold = P.fegHold[l]; s.feg.cur = P.fegCur[l];
        s.eg.stage = P.egStage[l]; s.eg.cur = P.egCur[l]; s.eg.target = P.egTarget[l]; s.eg.rate = P.egRate[l]; s.eg.holdLeft = P.egHold[l]; s.eg.rising = P.egRising[l] != 0;
        s.attS = P.attS[l]; s.uattS = P.uattS[l];
        for (int k = 0; k < 2; k++) { s.g[k].w = P.gW[k][l]; s.g[k].c = P.gC[k][l]; s.g[k].fc = P.gFc[k][l]; s.g[k].on = P.gOn[k][l] != 0; }
        s.nextGen = P.nextGen[l]; s.fphase = P.fphase[l];
        s.ueg.stage = P.uegStage[l]; s.ueg.cur = P.uegCur[l]; s.ueg.target = P.uegTarget[l]; s.ueg.rate = P.uegRate[l]; s.ueg.holdLeft = P.uegHold[l]; s.ueg.rising = P.uegRising[l] != 0;
        s.ufeg.stage = P.ufegStage[l]; s.ufeg.hold = P.ufegHold[l]; s.ufeg.cur = P.ufegCur[l];
        s.rng = (uint32_t)P.rng[l]; s.lp[0] = P.lp0[l]; s.lp[1] = P.lp1[l]; s.walk = P.walk[l]; s.nphase = P.nphase[l];
        s.out = P.out[l];
    }
}

// A control refresh on one lane, as render_chan runs it at the top of a sample: the state it reads goes into
// the channel first, and what it writes comes back out.
void refresh_lane(Group& G, int l) {
    Chan& C = *G.chan[l];
    // Every serial current: the refresh would find nothing to do (channel.cpp, refresh_op) and change nothing
    bool current = C.ctlSerialC == G.ctlSerial;
    for (int o = 0; o < Group::OPS && current; o++) current = C.ctlSerialV[o] == G.ctlSerial && C.ctlSerialU[o] == G.ctlSerial;
    if (current) return;
    for (int o = 0; o < Group::OPS; o++) {
        Op& P = G.op[o]; OpState& s = C.op[o];
        s.eg.stage = P.egStage[l]; s.eg.cur = P.egCur[l]; s.ueg.stage = P.uegStage[l]; s.ueg.cur = P.uegCur[l];
        s.g[0].on = P.gOn[0][l] != 0; s.g[1].on = P.gOn[1][l] != 0; s.attS = P.attS[l]; s.uattS = P.uattS[l];
    }
    refresh_ctl(C, *G.voice[l], G.fseq[l], G.ctlSerial);
    G.fbGain[l] = C.fbGain;
    for (int o = 0; o < Group::OPS; o++) load_ctl(G, l, o);
}

}  // namespace

void render_group(Group& G, int n) {
    gather(G);
    const vd zero = set1(0.0), half = set1(0.5), two = set1(2.0), fm_index = set1(FM);
    const double vGate = G.gateDb > -cal::OP_CUT_DB ? G.gateDb : -cal::OP_CUT_DB, uGate = G.gateDb > -cal::NOISE_CUT_DB ? G.gateDb : -cal::NOISE_CUT_DB;
    const vd vGateV = set1(vGate), uGateV = set1(uGate);
    const vd slewK = set1(LEVEL_SLEW_K), dampK = set1(DAMP_K), clip = set1(cal::CHAN_CLIP), nclip = set1(-cal::CHAN_CLIP);
    const vd formLevel = set1(cal::FORM_LEVEL), one = set1(1.0), mone = set1(-1.0), wScale = set1(1.0 / 2147483648.0), walkGain = set1(cal::WALK_GAIN);
    const EG* egs[Group::OPS][W]; const EG* uegs[Group::OPS][W];
    for (int o = 0; o < Group::OPS; o++) for (int l = 0; l < W; l++) { const Chan& C = *G.chan[l < G.lanes ? l : 0]; egs[o][l] = &C.op[o].eg; uegs[o][l] = &C.op[o].ueg; }
    vm liveBefore = loadmask(G.active), silentBefore = loadmask(G.silent);

    for (int t = 0; t < n; t++) {
        // the control refresh, where a lane's count is up
        for (int l = 0; l < G.lanes; l++) if (G.ctlLeft[l]-- <= 0) { G.ctlLeft[l] = G.ctlDecimation - 1; refresh_lane(G, l); }

        const vd partV = load(G.partV), partU = load(G.partU), fbIn = mul(load(G.fbBus), load(G.fbGain));
        vd Cb = zero, H = zero, S = zero, fbNew = zero, mix = zero;
        vm alive = mfalse(), carriersDone = mtrue();
        for (int o = 0; o < Group::OPS; o++) {
            Op& P = G.op[o];
            const vd in = select(loadmask(P.inFb), fbIn, select(loadmask(P.inCb), Cb, select(loadmask(P.inH), H, select(loadmask(P.inS), S, zero))));
            // the amplitude EG, the level's glide, the gate
            const EgRows er{P.egStage, P.egCur, P.egTarget, P.egRate, P.egHold, P.egRising};
            const vd egdb = eg_tick(er, egs[o]);
            vd attS = load(P.attS); const vd att = load(P.att);
            attS = select(lt(attS, set1(-1e8)), att, add(attS, mul(sub(att, attS), slewK)));
            store(P.attS, attS);
            const vd d = sub(egdb, attS);
            const vm pass = gt(d, vGateV);
            const vd gain = any(pass) ? select(pass, db2lin_fast(d), zero) : zero;
            const vm g0 = loadmask(P.gOn[0]), g1 = loadmask(P.gOn[1]);
            const vm computed = mor(ne(gain, zero), mor(g0, g1));
                // the frequency EG moves the word on the lanes it runs on; the phase steps by the increment either way
            vi inc = iload(P.inc); vd fop = load(P.fop);
            if (P.fegOn) {
                const int m = movemask(computed) & P.fegOn;
                for (int l = 0; l < W; l++) if (m >> l & 1) {
                    const uint32_t ni = op_increment(P.word[l] + feg_tick(P.fegStage, P.fegHold, P.fegCur, P.fegTarget, P.fegKatt, P.fegKdec, l) * (1024.0 / 12.0));
                    inc = isetlane(inc, l, (int32_t)ni); fop = setlane(fop, l, op_hz(ni));
                    if (P.fegStage[l] >= 2) P.fegOn &= ~(1 << l);
                }
            }
            const vi phase = iadd(iload(P.phase), ishl(inc, 32 - cal::OP_PHASE_BITS));
            istore(P.phase, phase);
            // the sample: the forms as one formula, the formant's grains apart
            vd y = zero;
            if (any(computed)) {
                const vd pm = mul(in, fm_index);
                const vd ph = add(mul(cvt_u2d(phase), set1(1.0 / 4294967296.0)), load(P.ph0));
                const vd x0 = add(ph, pm);
                if (P.anySine || P.anyWin) {
                    const vm resM = loadmask(P.res), oddM = loadmask(P.odd), sineM = loadmask(P.sine), allM = loadmask(P.all);
                    const vd f = P.anyRes ? select(resM, sub(ph, floor(ph)), ph) : ph;
                    vd s = P.anySine ? sin_turns(P.anyRes ? select(resM, add(mul(load(P.formB), f), pm), x0) : x0) : set1(1.0);
                    const vd y1 = mul(gain, s);
                    vd y2 = y1;
                    if (P.anyWin) {
                        const vd w0 = P.anyOdd ? select(oddM, add(mul(two, ph), pm), add(f, pm)) : add(f, pm);
                        const vd w = sub(w0, floor(w0));
                        const vf wf = fwin(iload(P.winAt), w);
                        const vd wv = cvt_f2d(fsub(wf, fload(P.dc)));   // the all forms' mean; 0 on the others, and x - 0.0f is x
                        if (P.anyAll) s = select(allM, set1(1.0), s);
                        y2 = mul(mul(mul(mul(gain, formLevel), two), wv), s);
                    }
                    y = select(sineM, y1, y2);
                }
                if (P.anyFrmt) {
                    const vm frmtM = loadmask(P.frmt);
                    const int m = movemask(mand(frmtM, computed));
                    for (int l = 0; l < W; l++) if (m >> l & 1)
                        y = setlane(y, l, formant(P, l, P.fw[l], lane(fop, l), lane(pm, l), lane(gain, l), &g_win[0][0][0] + P.winAt[l]));
                }
                y = select(computed, y, zero);
            }
            store(P.out, y);
            Cb = y;
            H = select(loadmask(P.toH), y, H);
            S = select(loadmask(P.sClr), zero, S);
            S = select(loadmask(P.sAdd), add(S, y), S);
            fbNew = select(loadmask(P.toFb), y, fbNew);
            mix = select(loadmask(P.carrier), add(mix, mul(y, partV)), mix);

            // the unvoiced operator
            const EgRows ur{P.uegStage, P.uegCur, P.uegTarget, P.uegRate, P.uegHold, P.uegRising};
            const vd uegdb = eg_tick(ur, uegs[o]);
            vd uattS = load(P.uattS); const vd uatt = load(P.uatt);
            uattS = select(lt(uattS, set1(-1e8)), uatt, add(uattS, mul(sub(uatt, uattS), slewK)));
            store(P.uattS, uattS);
            const vd ud = sub(uegdb, uattS);
            const vm upass = gt(ud, uGateV);
            const vd ugain = any(upass) ? select(upass, db2lin_fast(ud), zero) : zero;
            const vm um = ne(ugain, zero);
            if (any(um)) {
                vd ninc = load(P.ninc);
                if (P.ufegOn) {
                    const int m = movemask(um) & P.ufegOn;
                    for (int l = 0; l < W; l++) if (m >> l & 1) {
                        ninc = setlane(ninc, l, P.ninc[l] * fm::exp2(feg_tick(P.ufegStage, P.ufegHold, P.ufegCur, P.ufegTarget, P.ufegKatt, P.ufegKdec, l) / 12.0));
                        if (P.ufegStage[l] >= 2) P.ufegOn &= ~(1 << l);
                    }
                }
                const vmi umi = narrow(um);
                vi rng = iload(P.rng);
                vi r = ixor(rng, ishl(rng, 13)); r = ixor(r, ushr(r, 17)); r = ixor(r, ishl(r, 5));
                rng = iselect(umi, r, rng);
                istore(P.rng, rng);
                // the walk, as render_chan steps it
                const vd white = mul(cvt_i2d(rng), wScale);
                const vd y1 = load(P.lp0), y2 = load(P.lp1);
                store(P.lp0, select(um, add(y1, mul(load(P.ucoef), sub(white, y1))), y1));
                store(P.lp1, select(um, y1, y2));
                const vd t = min(add(y2, load(P.udrift)), one);
                const vd walk = load(P.walk), width = load(P.uwidth);
                const vd lo = max(sub(walk, width), mone), hi = min(add(walk, width), one);
                const vd walkN = add(lo, mul(mul(sub(hi, lo), add(t, one)), half));
                store(P.walk, select(um, walkN, walk));
                // the carrier on every lane, heard or not: an unheard lane steps by its own increment
                vd np = add(load(P.nphase), ninc);
                np = select(ge(np, one), sub(np, one), np);
                store(P.nphase, np);
                const vd u1 = mul(mul(walkN, walkGain), sin_turns(np));
                mix = select(um, add(mix, mul(mul(u1, ugain), partU)), mix);
            } else {
                vd np = add(load(P.nphase), load(P.ninc));
                store(P.nphase, select(ge(np, one), sub(np, one), np));
            }
            // what the channel's tail asks of this operator: a running envelope, a carrier still heard
            const vm egDone = eg_done(er), uegDone = eg_done(ur);
            alive = mor(alive, mor(mnot(egDone), mnot(uegDone)));
            const vm carrierLive = mand(loadmask(P.carrier), mor(mnot(eg_over(er, attS, vGate)), mor(loadmask(P.gOn[0]), loadmask(P.gOn[1]))));
            carriersDone = mand(carriersDone, mand(mnot(carrierLive), eg_over(ur, uattS, uGate)));
        }
        // the channel's tail: the feedback bus, liveness, silence, the clip, the damp
        const vd fbPrev = load(G.fbPrev);
        store(G.fbBus, mul(add(fbNew, fbPrev), half)); store(G.fbPrev, fbNew);
        vm active = mand(loadmask(G.active), alive);
        const vd damp = load(G.damp);
        const vm quietNow = mand(carriersDone, mand(lt(abs(mix), set1(1e-9)), eq(damp, zero)));
        const vi quiet = iselect(narrow(quietNow), iadd(iload(G.quiet), iset1(1)), iset1(0));
        istore(G.quiet, quiet);
        vm silent = mor(loadmask(G.silent), widen(ige(quiet, iset1(64))));
        storemask(G.silent, silent);
        mix = min(max(mix, nclip), clip);
        store(G.last, mix);
        const vm damping = ne(damp, zero);
        mix = select(damping, add(mix, damp), mix);
        const vd dampN = mul(damp, dampK);
        const vm small = lt(abs(dampN), set1(1e-7));
        store(G.damp, select(damping, select(small, zero, dampN), damp));
        active = mor(active, mand(damping, mnot(small)));
        storemask(G.active, active);
        for (int l = 0; l < G.lanes; l++) G.out[l][t] = lane(mix, l);
        // a lane that went silent or inactive before the segment's end is the scalar loop's from its start
        const int flipped = movemask(mor(mand(silent, mnot(silentBefore)), mnot(active)));
        if (flipped && t < n - 1) for (int l = 0; l < G.lanes; l++) if ((flipped >> l & 1) && G.flipAt[l] < 0) G.flipAt[l] = t;
        silentBefore = silent;
    }
    for (int l = 0; l < G.lanes; l++) scatter(G, l, n);
}

#define FSVR_STR_(x) #x
#define FSVR_STR(x) FSVR_STR_(x)
namespace { const fsvr::TgKernels TG_TABLE = {FSVR_STR(FSVR_ISA_NS), W, &render_group}; }
const fsvr::TgKernels& tg_table() { return TG_TABLE; }

}  // namespace fsvr::kernel::FSVR_ISA_NS
