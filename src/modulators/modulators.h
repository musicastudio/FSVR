// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

#pragma once
#include <cstdint>
#include "fs1r/vop3_regs.h"

// Sixteen EG generators and sixteen LFOs, as the hosts' registers program them: the filters' EGs and LFO2s, and
// the effects' LFOs.
struct Modulators {
    uint16_t lfoReg[16][4] = {};            // LFO_SPEED, LFO_WAVE, LFO_DEPTH and LFO_DIRECTION
    uint32_t phase[16] = {}, rnd = 1;
    float hold[16] = {};                    // sample and hold
    // An EG generator: its EG_RATE, EG_TARGET and EG_AIM.
    struct Eg { double level = 0, k = 0; int16_t target = 0, aim = 0; uint8_t rate = 0; int8_t side = 0; int flat = 0; };
    Eg eg[16];
    uint16_t done = 0, enable = 0;          // the segments that ended, and EG_STATUS as written
    double egOut[16] = {}, lfoOut[16] = {}; // this sample's levels, in register units (+-128)

    bool write(uint32_t reg, int select, uint16_t v);   // false: not a modulator register
    uint16_t read(uint32_t reg) const;      // EG_STATUS, the one register the hosts read
    void egWrite(int n, uint32_t reg, uint16_t v);
    float lfo(int n) const;                 // an LFO's value, -1..1 times its depth
    void step();                            // one sample of every EG and LFO into egOut and lfoOut
    // Ours: whether a step would leave every EG as it is: each holding (rate 0 or 255), or at its aim with no flat
    // wait running, and no segment's end waiting to be read. The LFOs always move; a caller that rests on this
    // accepts their phase stopping (filters/filter.cpp, a bank at rest).
    bool flat() const {
        if (done & enable) return false;
        for (const Eg& g : eg) if (g.rate && g.rate != 255 && (g.flat || g.level != g.aim)) return false;
        return true;
    }
};
