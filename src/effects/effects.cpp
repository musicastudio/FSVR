// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// Built without FMA contraction, like the host.
#include "effects/effects.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "fs1r/cal.h"
#include "fs1r/vop3_regs.h"

namespace vop3host = fsvr::vop3host;
namespace effects = fsvr::effects;

namespace {
const int TYPE_BYTE[3] = {vop3host::REVERB_TYPE, vop3host::VARIATION_TYPE, vop3host::INSERTION_TYPE};
// Bus sample to serial word, so that a dry performance leaves the chip at cal::OUT_GAIN.
const double BUS = cal::OUT_GAIN / cal::FX_DRY_GAIN * cal::WORD;

// The programs the host loads, by the window that holds them and the type msb they are filed under, with
// each window's clear entry (WINDOWS[w].clearEntry).
struct Loadable { int msb; effects::Program run; };
const Loadable DELAY[] = {{0, effects::dly_m00}, {8, effects::dly_m08}, {11, effects::dly_m11}};
const Loadable VARIATION[] = {
    {0, effects::var_m00}, {1, effects::var_m01}, {2, effects::var_m02}, {3, effects::var_m03}, {4, effects::var_m04},
    {5, effects::var_m05}, {6, effects::var_m06}, {7, effects::var_m07}, {8, effects::var_m08}, {9, effects::var_m09},
    {10, effects::var_m10}, {11, effects::var_m11}};
const Loadable INSERTION[] = {
    {0, effects::ins_m00}, {1, effects::ins_m01}, {2, effects::ins_m02}, {3, effects::ins_m03}, {4, effects::ins_m04},
    {5, effects::ins_m05}, {7, effects::ins_m07}, {10, effects::ins_m10}, {12, effects::ins_m12}, {13, effects::ins_m13},
    {14, effects::ins_m14}};

// What a window's clear program cannot reach, by window. Ours.
//
// The clear program zeroes the window's own registers and refreshes its input and tap cells, and that is
// all a chip program can do: it has no way to write the rest of the cell file, and no way at all to write
// delay memory, which the chip addresses by slot. So a type change used to hand the next program the last
// one's cells and the last one's delay memory as its own state. Both were measured to be loud: m[44] is the
// touch wah's envelope follower, and coming from Early Reflections it started life holding a delay tap,
// which opened the filter into the overdrive behind it and clipped (A073 R.P.M. into A072 Mouth Pop); the
// reverb delay's block fed a new echo a second of the performance before it (A071 Relaxxx into A072).
//
// The cells are the ones the window's programs write and its clear program does not, less those every
// window writes each sample (56-63, which are shared and always refreshed). The delay block is the span
// EffectHost::clearWindow spreads the window's slots over, which is the firmware's own statement of which
// block belongs to which window. The reverb window has no clear program and so no entry: its state lives
// in the base program, which every performance runs.
struct Leftover { int cell, cells; uint32_t ram, ramEnd; };
const Leftover LEFTOVER[5] = {
    {0, 0, 0, 0},                                                        // reverb: no clear program
    {0, 0, 0, 0},                                                        // aux: never reloaded
    {42, 10, vop3host::AREA[2], vop3host::AREA[3]},                      // insertion: m[42]-m[51]
    {0, 0, vop3host::AREA[3], vop3host::AREA[4]},                        // reverb delay: cells all covered
    {54, 2, vop3host::AREA[4], vop3host::TOP},                           // variation: m[54], m[55]
};
}

// A register write from the host. The constants are the programs' parameters, the slots their delay offsets,
// and the rest is the LFOs (or nothing the programs see).
void Effects::write(uint32_t reg, uint32_t value) {
    using namespace vop3;
    const uint16_t v = (uint16_t)value;
    reg &= 0xff;
    if (reg == SELECT) { select = v; return; }
    const int step = select & 511;
    if (select & AUTO_INCREMENT) select = (uint16_t)((select & ~511) | ((step + 1) & 511));
    switch (reg) {
        case COEF:      state.k[step] = int16_t(v) / 32768.0; break;
        case SLOT_HIGH:
        case SLOT_LOW: {
            uint16_t (&s)[2] = slot[step & 127];
            s[reg - SLOT_HIGH] = v;
            state.offset[step & 127] = (uint32_t(s[0] & 0x1ff) << 9) | (s[1] & 0x1ff);
            break;
        }
        default:        mod.write(reg, step, v); break;   // CONFIG and the mute mean nothing here
    }
}

// The host loads a window's program, by its type msb
void Effects::load(int window, int msb) {
    auto find = [&](auto const& list) -> effects::Program {
        for (const Loadable& p : list) if (p.msb == msb) return p.run;
        return nullptr;   // ponytail: the host routes every type to a listed msb; another would run nothing
    };
    int at = -1;
    switch (window) {
        case vop3host::REVERB_DELAY: stage[at = 2] = find(DELAY); break;
        case vop3host::VARIATION:    stage[at = 3] = find(VARIATION); break;
        case vop3host::INSERTION:    stage[at = 4] = find(INSERTION); break;
        default: return;                         // aux has the one program
    }
    if (msb != vop3host::WINDOWS[window].clearEntry) return;
    // The window is being reprogrammed, and the host has loaded the program that clears it
    // (EffectHost::clearWindow). The clear program zeroes the window's registers and nothing else
    // (ins_m14.cpp and the two others); the unit's chip runs it for as long as the upload takes, which is
    // where the window loses them, but an upload costs no samples here, so run it once. Then take the rest
    // of the window's state, which no chip program can reach (LEFTOVER). Together these are what make the
    // new program start from silence instead of from the old one's state.
    if (stage[at]) stage[at](state);
    const Leftover& left = LEFTOVER[window];
    std::fill_n(state.m + left.cell, left.cells, 0.0);
    std::fill(state.ram.begin() + left.ram, state.ram.begin() + left.ramEnd, 0.0);
}

// A whole performance. Ours: the effect section starts it from silence.
//
// A performance change rebuilds all five windows, and nothing the last performance's programs computed
// means anything to the new ones: a register is one program's filter state and the next program's
// something else, a cell likewise, and the delay memory is a second of the last performance's audio
// addressed by slots the new types place differently. Carried over, that state is what the next programs
// take for their own, and the measured results were bursts to full scale into the overdrives, wahs and
// echoes - the louder the better, since the state was loudest when something was playing.
//
// Clearing it costs the wet paths the time they take to refill, measured at about 30 ms to 90 % on a
// reverb-heavy performance and nothing at all on the dry path, which reads as the effects fading in
// rather than as a click. The firmware cannot do this: its clear programs reach a window's registers and
// no more, and the chip has no instruction that writes delay memory. k[] and offset[] are the host's and
// are not ours to touch; in[], lfo[] and out[] are rewritten every sample.
// The clearing leaves one thing behind: every mixer gain, return, pan and EQ word of the new performance
// lands between one sample and the next, and a gain step on audio that is still sounding is a click of its
// own, with nothing to do with what the programs carried. So the section fades in over FADE_SAMPLES.
void Effects::load(const uint8_t* common) {
    std::fill_n(state.r, 128, 0.0);
    std::fill_n(state.m, 64, 0.0);
    std::fill_n(state.t, 16, 0.0);
    std::fill(state.ram.begin(), state.ram.end(), 0.0);
    fade = level > 1e-5 ? FADE_SAMPLES : 0;      // nothing sounding, nothing to fade
    host.load(common);
}

void Effects::sync(uint8_t* c) {
    uint8_t was[vop3host::COMMON_BYTES]; std::memcpy(was, host.perf, sizeof was);
    bool type = false;
    for (int i : TYPE_BYTE)                      // types first: they load defaults, which the other changes then override
        if (c[i] != was[i]) { host.setByte(i, was[i] = c[i]); type = true; }
    for (uint32_t i = 0; i < vop3host::COMMON_BYTES; i++)
        if ((c[i] & 0x7f) != was[i]) { host.store(i, c[i]); host.byteChanged(i); }
    host.runPosted();
    const uint32_t from = vop3host::REVERB_PARAM_BYTES, to = vop3host::EQ_HIGH_SHAPE + 1;   // every effect byte
    if (type) std::memcpy(c + from, host.perf + from, to - from);   // the new type's defaults
}

void Effects::control(int dest, int offset) {
    int16_t* at = dest == 15 ? &host.sendReverbOffset : &host.sendVariationOffset;
    int param = 0;
    if (dest <= 14) {                            // an insertion parameter, by the type's row of the controller map
        param = vop3host::rom::INSERTION_CONTROL[host.readParameter(vop3host::INSERTION_TYPE)][dest - 1];
        if (param == 0xff) return;
        at = &host.controlOffset[param];
    }
    if (*at == offset) return;
    if (dest <= 14) host.setControlOffset(param, offset);
    else if (dest == 15) host.setSendReverbOffset(offset);   // insertion to reverb
    else host.setSendVariationOffset(offset);                   // insertion to variation
    host.runPosted();
}

// One sample. Serial channels k and k + 4 are the two halves of a frame and the left DAC plays channel 0, so a
// bus's left goes to channel k + 4.
void Effects::run(const double bus[8], double& l, double& r) {
    effects::State& s = state;
    for (int b = 0; b < 4; b++) {
        s.in[0][2 * (4 + b)] = s.in[0][2 * (4 + b) + 1] = bus[2 * b] * BUS;
        s.in[0][2 * b] = s.in[0][2 * b + 1] = bus[2 * b + 1] * BUS;
    }
    mod.step();
    std::copy(mod.egOut, mod.egOut + 16, s.in[3]);
    std::copy(mod.lfoOut, mod.lfoOut + 16, s.lfo);
    for (effects::Program p : stage) if (p) p(s);
    s.ptr = (s.ptr - 1) & (effects::State::RAM_WORDS - 1);
    l = fsvr::word::dac(s.out[0]) / cal::WORD;
    r = fsvr::word::dac(s.out[8]) / cal::WORD;
    if (fade > 0) {                              // ours: a performance load comes back up (load)
        const double g = 0.5 - 0.5 * std::cos(3.14159265358979323846 * (FADE_SAMPLES - --fade) / FADE_SAMPLES);
        l *= g;
        r *= g;
    }
    level = std::fmax(std::fabs(l) + std::fabs(r), level * 0.9999);   // ours: was anything sounding (load)
}
