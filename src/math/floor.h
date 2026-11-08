// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// math/floor.h - floor as one instruction where the build or the CPU has it, else as an exact branchless cut. Ours.
//
// The sample loop takes a floor two or three times an operator. On the x64 baseline (SSE2) the library's floor
// is a call that tests for SSE4.1 and rounds; the same test once, at load, and the rounding instruction inline
// is a fifth faster, and every x86 since 2008 has it. Where it is missing, and on any other target without a
// rounding instruction, the cut through an integer gives the same bits as std::floor for every double: a
// magnitude at or past 2^52 is whole already (or not a number), under it the truncation is exact, and a zero is
// returned as itself so a -0.0 stays one. Measured 2026-10-28: 1.1 ns the library, 0.9 ns roundsd, 1.1 ns the
// cut; the cut with a branch on the sign instead of a select was 4 ns, mispredicting on modulated phases.
#pragma once
#include <cmath>
#include <cstdint>
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace fm {

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
static inline bool has_sse41() {
#ifdef _MSC_VER
    int r[4]; __cpuid(r, 1); return (r[2] >> 19) & 1;
#else
    unsigned a, b, c, d; return __get_cpuid(1, &a, &b, &c, &d) && (c >> 19 & 1);
#endif
}
extern const bool g_sse41;                 // has_sse41(), defined once in fastmath.cpp
#endif

static inline double floor_cut(double x) {
    if (!(std::fabs(x) < 4503599627370496.0)) return x;
    const double t = (double)(int64_t)x;
    const double r = t - (double)(t > x);
    return x == 0.0 ? x : r;
}

static inline double floor_exact(double x) {
#if defined(__SSE4_1__) || defined(__AVX__) || defined(__aarch64__) || defined(_M_ARM64)
    return std::floor(x);                                                                  // one instruction
#elif defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_IX86))
    // cl.exe only: it emits roundsd with no /arch, where clang-cl, which defines _MSC_VER and _M_X64 too,
    // rejects the intrinsic without sse4.1 in the target. Giving clang the feature per function would cost
    // the inlining this is here for, so it takes the cut, as every other non-MSVC x86 build does.
    if (g_sse41) return _mm_cvtsd_f64(_mm_floor_sd(_mm_setzero_pd(), _mm_set_sd(x)));
    return floor_cut(x);
#else
    return floor_cut(x);
#endif
}

}  // namespace fm
