// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// kernel/dispatch.h - the lane kernels the engine runs, chosen once for the CPU it is on. Ours.
//
// Every kernel is compiled once per instruction set (kernel/README.md) and each build of it fills a table: the
// DSP's here (the filter bank), the tone generator's in kernel/tonegen.h. isa() picks the best the CPU has, or
// what FSVR_SIMD names (scalar, sse2, avx2, neon; one the CPU lacks falls back), and never changes its mind, so a
// render thread reads either table without a lock.
#pragma once
#include "filters/lanes.h"

namespace fsvr {

// The instruction set the kernels run on, chosen once: the best the CPU has, or what FSVR_SIMD asks for.
enum class Isa { scalar, sse2, avx2, neon };
Isa isa();

struct Kernels {
    const char* name;                          // scalar, sse2, avx2 or neon
    int lanes;                                 // doubles a register holds
    // The filter bank's sixteen slots, one sample: the slots' state, the program's shared constants, the inputs,
    // EG levels and LFO2s as the host hands them this sample, and the slots' outputs (filters/filter.cpp).
    void (*filter_bank)(filters::Lanes& L, const filters::Shared& g, const double* in, const double* eg, const double* lfo, double* out);
    bool (*fma_contracted)();                  // the build fused a multiply into an add: the self check fails on it
    bool (*lanes_ok)();                        // the masks, selects, ties, floor and conversions do what simd.h says (isa/table.cpp)
};

const Kernels& kernels();

}  // namespace fsvr
