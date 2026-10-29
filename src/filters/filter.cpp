// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// Built without FMA contraction, like the hosts.
#include "filters/filter.h"
#include "fs1r/cal.h"
#include "kernel/dispatch.h"
#include "dsp/word.h"
#include <cmath>

namespace {
using fsvr::filters::Lanes;
namespace word = fsvr::word;

// OPEN: the level a channel reaches the filter at. A channel at its clip level is taken to be a full-scale
// serial word. What comes back is scaled so that the passband loses cal::FLT_LOSS, as measured on the unit.
const double TO_FILTER = cal::WORD / cal::CHAN_CLIP;
const double FROM_FILTER = cal::CHAN_CLIP * cal::FLT_LOSS / cal::WORD;

// The unit's serial schedule. The chip runs sixteen voices in four pages over one pass and moves each voice's
// input, EG level and LFO2 in, and its output out, at fixed steps; where that step falls on the wrong side of
// the voice's page, the voice hears or answers a sample late. By the slot's chip channel, (s + 2) & 15, read
// off the firmware's filter program.
struct Late { bool in, eg, lfo, out; };
constexpr Late LATE[16] = {
    {0,0,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,1,1,0}, {0,0,0,1}, {0,0,0,1},
    {0,0,1,1}, {0,1,1,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,1,1,1}, {1,1,1,0}, {1,1,1,0},
};
}

// A register write from the host (FilterHost, in the firmware's layout of the constants). Constants land in
// the slot's VoiceFilter; the EG and LFO registers go to the modulators.
void Filter::Bank::write(uint32_t reg, uint32_t value) {
    using namespace vop3;
    const uint16_t v = (uint16_t)value;
    rest = 0; asleep = false;                  // ours: a write is something to do (resting)
    reg &= 0xff;
    if (reg == SELECT) { select = v; return; }
    const int step = select & 511;
    if (select & AUTO_INCREMENT) select = (uint16_t)((select & ~511) | ((step + 1) & 511));
    if (reg == COEF) coef(step, v);
    else mod.write(reg, step, v);   // CONFIG, the slots and the rest mean nothing here
}

void Filter::Bank::clear(int s) {
    lanes.x1[s] = lanes.s2[s] = lanes.s3[s] = lanes.s4[s] = lanes.s5[s] = 0.0;
}

// A constant: the program's shared ones in steps 11 to 14, else one of a slot's words in its page.
void Filter::Bank::coef(int step, uint16_t v) {
    using namespace word;
    const double k = int16_t(v) / 32768.0;
    if (step < 16) {
        if (step == 11) shared.c63 = cell(wrap_sum(128 * k + 0.0));
        if (step == 12) shared.eg_zero = wrap(128 * k + 0.0);
        if (step == 13) shared.c62 = cell(wrap_sum(128 * k + 0.0));
        if (step == 14) shared.c61 = cell(wrap_sum(128 * k + 0.0));
        for (int s = 0; s < SLOTS; s++) lanes.memo_fc[s] = NAN;
        return;
    }
    // slot s has the page at 16 + 124 * (((s >> 2) + 1) & 3) and the lane s & 3 in it
    const int g = (step - 16) / 124, rel = (step - 16) % 124;
    const int first = 4 * ((g + 3) & 3);
    struct Word { int first, stride; double (Lanes::*field)[Lanes::N]; };
    static constexpr Word WORDS[] = {
        {0, 3, &Lanes::eg_depth},   {1, 3, &Lanes::lfo_depth}, {2, 3, &Lanes::cutoff},
        {12, 1, &Lanes::exp_slope}, {28, 1, &Lanes::type_k},   {32, 1, &Lanes::reso_a},
        {40, 1, &Lanes::reso_bias}, {44, 1, &Lanes::fb_scale}, {52, 4, &Lanes::gain},
        {54, 4, &Lanes::reso_b},    {71, 4, &Lanes::stage_k},  {84, 2, &Lanes::damp_k},
        {96, 1, &Lanes::pole_k},    {116, 1, &Lanes::ramp},
    };
    for (const Word& w : WORDS) {
        const int i = rel - w.first;
        if (i >= 0 && i % w.stride == 0 && i / w.stride < 4) { const int s = first + i / w.stride; (lanes.*w.field)[s] = k; lanes.memo_fc[s] = NAN; return; }
    }
    // the rest: the second serial bank's gain, always 0 on the FS1R, and words no step multiplies by
}

// The slot's output: which tap the type reads, and its scale. The BPF's is x1, the others x2.
void Filter::Bank::setType(int s, int type) {
    using namespace fsvr::vop3host::filter;
    lanes.route[s] = type == BPF ? 1 : 2;
    // ponytail: a type byte past BEF makes the unit read whatever register the byte names, which is another
    // voice's state and not a filter. It plays silence here (docs/Differences.md): no tap.
    static constexpr Lanes::Tap TAP[6] = {Lanes::LPF24, Lanes::LPF18, Lanes::LPF12, Lanes::HPF, Lanes::BPF, Lanes::BEF};
    for (int t = 0; t < Lanes::TAPS; t++) lanes.tap[t][s] = 0;
    if (type >= 0 && type <= BEF) lanes.tap[TAP[type]][s] = ~0ull;
}

void Filter::Bank::sync(int p, const uint8_t* voiceBytes, const uint8_t* rec, int cutoffMod, int lfo2Depth, int lfo2Speed) {
    for (int i = 0; i < host.VOICE_BYTES; i++) host.setByte(host.VOICE + i, p, voiceBytes[i]);
    for (int i = 0; i < host.PART_BYTES; i++) host.setByte(host.PART + i, p, rec[i]);
    if (cutoffMod != host.partCutoffMod[p]) host.setPartCutoffMod(p, cutoffMod);
    if (lfo2Depth != host.partLfo2Depth[p]) host.setPartLfo2Depth(p, lfo2Depth);
    if (lfo2Speed != host.partLfo2Speed[p]) host.setPartLfo2Speed(p, lfo2Speed);
    host.flush();
}

// The modulators step, then every slot's voice, W at a time on the lane kernel. An EG that ended its segment is
// served in the sample it reports.
void Filter::Bank::run(double v[SLOTS]) {
    mod.step();
    double in[SLOTS], eg[SLOTS], lfo[SLOTS], out[SLOTS];
    for (int s = 0; s < SLOTS; s++) {
        const int ch = (s + 2) & 15;
        const Late& late = LATE[ch];
        const double i = v[s] * TO_FILTER, e = mod.egOut[ch], l = mod.lfoOut[ch];
        in[s] = late.in ? inPrev[s] : i; eg[s] = late.eg ? egPrev[s] : e; lfo[s] = late.lfo ? lfoPrev[s] : l;
        inPrev[s] = i; egPrev[s] = e; lfoPrev[s] = l;
    }
    fsvr::kernels().filter_bank(lanes, shared, in, eg, lfo, out);
    for (int s = 0; s < SLOTS; s++) {
        v[s] = (LATE[(s + 2) & 15].out ? outPrev[s] : out[s]) * FROM_FILTER;
        outPrev[s] = out[s];
    }
    if (mod.read(vop3::EG_STATUS)) host.egInterrupt();
}

// Ours: a bank past the unit's (Filter::run) rests once its sixteen slots have had nothing to do for a second, as
// the engine does while nothing sounds: no input, no output, every filter's state at zero and every EG holding. An
// input or a register write wakes it. What stops meanwhile is its LFO2s, which come back at another phase, as the
// effect LFOs do after the engine sleeps (docs/Differences.md, more than 32 voices).
bool Filter::Bank::resting(const double* in) {
    for (int s = 0; s < SLOTS; s++) if (in[s] != 0.0) { rest = 0; asleep = false; return false; }
    if (asleep) return true;
    if (++rest < SR) return false;
    rest = SR / 2;                             // not still yet: look again in half a second
    for (int s = 0; s < SLOTS; s++)
        if (outPrev[s] != 0.0 || lanes.x1[s] != 0.0 || lanes.s2[s] != 0.0 || lanes.s3[s] != 0.0 || lanes.s4[s] != 0.0 || lanes.s5[s] != 0.0) return false;
    if (!mod.flat()) return false;
    asleep = true;
    return true;
}
