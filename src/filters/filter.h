// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// The FS1R's voice filters: one voice (filters/voice_filter.h, run on lanes by kernel/isa/bank_kernel.cpp) per
// firmware's filter host. In: the channels' samples. Out: the same channels, filtered. No chip is emulated:
// the host's register writes are read for what they mean to a voice (Filter::write).
//
// The firmware's host drives sixteen slots, the most the unit's one filter chip has. A Filter has a bank of
// sixteen voices for every sixteen channels the engine can allocate, each with its own host and modulators, so
// channel n is slot n & 15 of bank n >> 4. The unit uses bank 0 only; the rest are ours (docs/Differences.md,
// 32 filtered voices and more than 32 voices).
#pragma once
#include <cstdint>
#include "modulators/modulators.h"
#include "filters/filter_host.h"
#include "filters/lanes.h"

struct Filter {
    static constexpr int SLOTS = 16, BANKS = 8, VOICES = SLOTS * BANKS;   // BANKS: Synth::MAX_CHAN / SLOTS

    // A bank: the firmware's host, the sixteen EGs and LFO2s it programs, and sixteen voices.
    struct Bank final : fsvr::vop3host::WriteSink {
        fsvr::vop3host::FilterHost host;
        Modulators mod;
        fsvr::filters::Lanes lanes;                          // the sixteen voices, a number a row (the lane kernel runs them)
        fsvr::filters::Shared shared = {};

        Bank() { host.sink = this; host.boot(); reset(); }
        Bank(const Bank&) = delete;                          // the host writes to this bank
        Bank& operator=(const Bank&) = delete;
        void write(uint32_t reg, uint32_t v) override;
        uint32_t read(uint32_t reg) override { return mod.read(reg); }
        void setType(int slot, int type) override;
        void clear(int slot) override;
        void reset() { host.freeSlots(); host.clearStates(); rest = 0; asleep = false; }   // as at a performance's start
        void sync(int part, const uint8_t* voice, const uint8_t* rec, int cutoffMod, int lfo2Depth, int lfo2Speed);
        void run(double v[SLOTS]);                           // one sample of the sixteen slots, in place
        // Ours: whether the bank has had nothing to do for a second and may rest (filter.cpp). Called with the
        // sample's input before run; an input or a register write wakes it.
        bool resting(const double* in);

    private:
        uint16_t select = 0;
        int rest = 0; bool asleep = false;                   // samples with nothing to do, and resting on them
        double inPrev[SLOTS] = {}, egPrev[SLOTS] = {}, lfoPrev[SLOTS] = {}, outPrev[SLOTS] = {};
        void coef(int step, uint16_t v);
    };

    Bank bank[BANKS];

    fsvr::vop3host::FilterHost& host(int ch) { return bank[ch >> 4].host; }   // the host of channel ch's slot
    void reset() { for (Bank& b : bank) b.reset(); }
    // A part's voice and record, and its three controller offsets, into the first `banks` banks.
    void sync(int banks, int part, const uint8_t* voice, const uint8_t* rec, int cutoffMod, int lfo2Depth, int lfo2Speed)
    {
        for (int b = 0; b < banks; b++) bank[b].sync(part, voice, rec, cutoffMod, lfo2Depth, lfo2Speed);
    }
    void flush(int banks) { for (int b = 0; b < banks; b++) bank[b].host.flush(); }
    // One sample of the first 16 * banks channels, in place. Ours: a bank past the second rests while it has
    // nothing to do (Bank::resting); banks 0 and 1 always run, so the unit's bank and the 32-voice engine render
    // as they always did.
    static constexpr int ALWAYS = 2;
    void run(int banks, double* v) {
        for (int b = 0; b < banks; b++) {
            double* in = v + SLOTS * b;
            if (b >= ALWAYS && bank[b].resting(in)) continue;
            bank[b].run(in);
        }
    }
};
