// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// kernel/simd.h - a few lanes of doubles, floats and 32-bit integers, written as the scalar arithmetic they stand in for. Ours.
//
// A kernel file is compiled once per instruction set, FSVR_ISA picking the lane type and the namespace it lands
// in (0 scalar, 1 sse2, 2 avx2, 3 neon; kernel/README.md). Every operation here does per lane exactly what the
// scalar code does, in the same order, so a kernel over W slots or channels renders their samples to the bit;
// the hashes (dsp_check, tools/abcheck.py) hold it to that. The rules the exactness rests on:
//   - min(a, b) is a < b ? a : b and max(a, b) is a > b ? a : b, the second operand on a tie or a NaN, as the
//     x86 instructions are; the scalar code's clamps are written that way round (dsp/word.h).
//   - select(m, a, b) is a where the mask is set, b elsewhere: a masked path is never arithmetic with a 0 or 1.
//   - a + 0.0 stays an add (it turns a -0.0 into +0.0, as the chip does).
//   - nothing is fused: the build keeps a multiply and an add apart (CMakeLists.txt, build.bat).
//   - floor is exact (a rounding instruction, or the cut through an integer, which is exact under 2^31, the
//     bound every caller keeps); a conversion rounds as the scalar cast does, to nearest for a float, toward
//     zero for an integer.
// vd is W doubles; vf and vi are W floats and W 32-bit integers in a half-width register beside it, the table
// reads and the phase generator; vm and vmi are their masks.
#pragma once
#include <cstdint>
#include <cstring>
#include "math/floor.h"

#if FSVR_ISA == 0
#define FSVR_ISA_NS scalar
#elif FSVR_ISA == 1
#define FSVR_ISA_NS sse2
#elif FSVR_ISA == 2
#define FSVR_ISA_NS avx2
#elif FSVR_ISA == 3
#define FSVR_ISA_NS neon
#else
#error "FSVR_ISA is 0 scalar, 1 sse2, 2 avx2 or 3 neon"
#endif

// The lane type: the instruction set asked for where the target has it, the target's own otherwise (a universal
// macOS build compiles every kernel for both slices; the dispatch never picks a namespace the slice cannot run).
#if FSVR_ISA != 0 && (defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__))
#include <immintrin.h>
#if FSVR_ISA == 2
#ifndef __AVX2__
#error "the avx2 kernel is compiled with /arch:AVX2 or -mavx2"
#endif
#define FSVR_LANES_AVX2 1
#else
#define FSVR_LANES_SSE2 1
#endif
#elif FSVR_ISA != 0 && (defined(__aarch64__) || defined(_M_ARM64))
#include <arm_neon.h>
#define FSVR_LANES_NEON 1
#else
#define FSVR_LANES_SCALAR 1
#endif

namespace fsvr::simd::FSVR_ISA_NS {

#if FSVR_LANES_AVX2
constexpr int W = 4;
struct vd { __m256d v; };
struct vm { __m256d v; };                      // a mask: every bit of a lane set, or none
struct vf { __m128 v; };
struct vi { __m128i v; };                      // 32-bit lanes, signed or not as the operation says
struct vmi { __m128i v; };
inline vd load(const double* p) { return {_mm256_loadu_pd(p)}; }
inline void store(double* p, vd a) { _mm256_storeu_pd(p, a.v); }
inline vd set1(double x) { return {_mm256_set1_pd(x)}; }
inline vd add(vd a, vd b) { return {_mm256_add_pd(a.v, b.v)}; }
inline vd sub(vd a, vd b) { return {_mm256_sub_pd(a.v, b.v)}; }
inline vd mul(vd a, vd b) { return {_mm256_mul_pd(a.v, b.v)}; }
inline vd div(vd a, vd b) { return {_mm256_div_pd(a.v, b.v)}; }
inline vd min(vd a, vd b) { return {_mm256_min_pd(a.v, b.v)}; }
inline vd max(vd a, vd b) { return {_mm256_max_pd(a.v, b.v)}; }
inline vd neg(vd a) { return {_mm256_xor_pd(a.v, _mm256_set1_pd(-0.0))}; }
inline vd abs(vd a) { return {_mm256_andnot_pd(_mm256_set1_pd(-0.0), a.v)}; }
inline vd floor(vd a) { return {_mm256_floor_pd(a.v)}; }
inline vm lt(vd a, vd b) { return {_mm256_cmp_pd(a.v, b.v, _CMP_LT_OQ)}; }
inline vm le(vd a, vd b) { return {_mm256_cmp_pd(a.v, b.v, _CMP_LE_OQ)}; }
inline vm ge(vd a, vd b) { return {_mm256_cmp_pd(a.v, b.v, _CMP_GE_OQ)}; }
inline vm gt(vd a, vd b) { return {_mm256_cmp_pd(a.v, b.v, _CMP_GT_OQ)}; }
inline vm eq(vd a, vd b) { return {_mm256_cmp_pd(a.v, b.v, _CMP_EQ_OQ)}; }
inline vm ne(vd a, vd b) { return {_mm256_cmp_pd(a.v, b.v, _CMP_NEQ_UQ)}; }   // true when either is a NaN, as != is
inline vm isnan(vd a) { return {_mm256_cmp_pd(a.v, a.v, _CMP_UNORD_Q)}; }
inline vm mand(vm a, vm b) { return {_mm256_and_pd(a.v, b.v)}; }
inline vm mor(vm a, vm b) { return {_mm256_or_pd(a.v, b.v)}; }
inline vm mnot(vm a) { return {_mm256_xor_pd(a.v, _mm256_castsi256_pd(_mm256_set1_epi32(-1)))}; }
inline vm mtrue() { return {_mm256_castsi256_pd(_mm256_set1_epi32(-1))}; }
inline vm mfalse() { return {_mm256_setzero_pd()}; }
inline vd select(vm m, vd a, vd b) { return {_mm256_blendv_pd(b.v, a.v, m.v)}; }
inline vm mselect(vm m, vm a, vm b) { return {_mm256_blendv_pd(b.v, a.v, m.v)}; }
inline vm loadmask(const uint64_t* p) { return {_mm256_castsi256_pd(_mm256_loadu_si256((const __m256i*)p))}; }
inline void storemask(uint64_t* p, vm m) { _mm256_storeu_si256((__m256i*)p, _mm256_castpd_si256(m.v)); }
inline int movemask(vm m) { return _mm256_movemask_pd(m.v); }
inline bool any(vm m) { return movemask(m) != 0; }
inline bool all(vm m) { return movemask(m) == 15; }
// the half-width lanes
inline vi trunc_i(vd a) { return {_mm256_cvttpd_epi32(a.v)}; }
inline vd cvt_i2d(vi a) { return {_mm256_cvtepi32_pd(a.v)}; }
inline vf cvt_d2f(vd a) { return {_mm256_cvtpd_ps(a.v)}; }
inline vd cvt_f2d(vf a) { return {_mm256_cvtps_pd(a.v)}; }
inline vf fadd(vf a, vf b) { return {_mm_add_ps(a.v, b.v)}; }
inline vf fsub(vf a, vf b) { return {_mm_sub_ps(a.v, b.v)}; }
inline vf fmul(vf a, vf b) { return {_mm_mul_ps(a.v, b.v)}; }
inline vf fset1(float x) { return {_mm_set1_ps(x)}; }
inline vf fload(const float* p) { return {_mm_loadu_ps(p)}; }
inline vf fselect(vmi m, vf a, vf b) { return {_mm_blendv_ps(b.v, a.v, _mm_castsi128_ps(m.v))}; }
inline vi iload(const int32_t* p) { return {_mm_loadu_si128((const __m128i*)p)}; }
inline void istore(int32_t* p, vi a) { _mm_storeu_si128((__m128i*)p, a.v); }
inline vi iset1(int32_t x) { return {_mm_set1_epi32(x)}; }
inline vi iadd(vi a, vi b) { return {_mm_add_epi32(a.v, b.v)}; }
inline vi isub(vi a, vi b) { return {_mm_sub_epi32(a.v, b.v)}; }
inline vi ixor(vi a, vi b) { return {_mm_xor_si128(a.v, b.v)}; }
inline vi ishl(vi a, int n) { return {_mm_slli_epi32(a.v, n)}; }
inline vi ushr(vi a, int n) { return {_mm_srli_epi32(a.v, n)}; }
inline vmi ige(vi a, vi b) { return {_mm_or_si128(_mm_cmpgt_epi32(a.v, b.v), _mm_cmpeq_epi32(a.v, b.v))}; }
inline vmi ieq(vi a, vi b) { return {_mm_cmpeq_epi32(a.v, b.v)}; }
inline vmi ilt(vi a, vi b) { return {_mm_cmplt_epi32(a.v, b.v)}; }
inline vi iselect(vmi m, vi a, vi b) { return {_mm_blendv_epi8(b.v, a.v, m.v)}; }
inline vm widen(vmi m) { return {_mm256_castsi256_pd(_mm256_cvtepi32_epi64(m.v))}; }   // an int lane's mask onto its double
inline vmi narrow(vm m) { return {_mm256_castsi256_si128(_mm256_permutevar8x32_epi32(_mm256_castpd_si256(m.v), _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6)))}; }   // a double lane's mask onto its int
// A float read per lane: four loads, the table and the index of each
inline vf gather_f(const float* t, vi i) { return {_mm_i32gather_ps(t, i.v, 4)}; }
inline vf gather_f(const float* const* t, vi i) { int32_t k[4]; istore(k, i); return {_mm_set_ps(t[3][k[3]], t[2][k[2]], t[1][k[1]], t[0][k[0]])}; }

#elif FSVR_LANES_SSE2
constexpr int W = 2;
struct vd { __m128d v; };
struct vm { __m128d v; };
struct vf { __m128 v; };                       // lanes 0 and 1 carry the two
struct vi { __m128i v; };
struct vmi { __m128i v; };
inline vd load(const double* p) { return {_mm_loadu_pd(p)}; }
inline void store(double* p, vd a) { _mm_storeu_pd(p, a.v); }
inline vd set1(double x) { return {_mm_set1_pd(x)}; }
inline vd add(vd a, vd b) { return {_mm_add_pd(a.v, b.v)}; }
inline vd sub(vd a, vd b) { return {_mm_sub_pd(a.v, b.v)}; }
inline vd mul(vd a, vd b) { return {_mm_mul_pd(a.v, b.v)}; }
inline vd div(vd a, vd b) { return {_mm_div_pd(a.v, b.v)}; }
inline vd min(vd a, vd b) { return {_mm_min_pd(a.v, b.v)}; }
inline vd max(vd a, vd b) { return {_mm_max_pd(a.v, b.v)}; }
inline vd neg(vd a) { return {_mm_xor_pd(a.v, _mm_set1_pd(-0.0))}; }
inline vd abs(vd a) { return {_mm_andnot_pd(_mm_set1_pd(-0.0), a.v)}; }
inline vm lt(vd a, vd b) { return {_mm_cmplt_pd(a.v, b.v)}; }
inline vm le(vd a, vd b) { return {_mm_cmple_pd(a.v, b.v)}; }
inline vm ge(vd a, vd b) { return {_mm_cmpge_pd(a.v, b.v)}; }
inline vm gt(vd a, vd b) { return {_mm_cmpgt_pd(a.v, b.v)}; }
inline vm eq(vd a, vd b) { return {_mm_cmpeq_pd(a.v, b.v)}; }
inline vm ne(vd a, vd b) { return {_mm_cmpneq_pd(a.v, b.v)}; }
inline vm isnan(vd a) { return {_mm_cmpunord_pd(a.v, a.v)}; }
inline vm mand(vm a, vm b) { return {_mm_and_pd(a.v, b.v)}; }
inline vm mor(vm a, vm b) { return {_mm_or_pd(a.v, b.v)}; }
inline vm mnot(vm a) { return {_mm_xor_pd(a.v, _mm_castsi128_pd(_mm_set1_epi32(-1)))}; }
inline vm mtrue() { return {_mm_castsi128_pd(_mm_set1_epi32(-1))}; }
inline vm mfalse() { return {_mm_setzero_pd()}; }
inline vd select(vm m, vd a, vd b) { return {_mm_or_pd(_mm_and_pd(m.v, a.v), _mm_andnot_pd(m.v, b.v))}; }
inline vm mselect(vm m, vm a, vm b) { return {_mm_or_pd(_mm_and_pd(m.v, a.v), _mm_andnot_pd(m.v, b.v))}; }
inline vm loadmask(const uint64_t* p) { return {_mm_castsi128_pd(_mm_loadu_si128((const __m128i*)p))}; }
inline void storemask(uint64_t* p, vm m) { _mm_storeu_si128((__m128i*)p, _mm_castpd_si128(m.v)); }
inline int movemask(vm m) { return _mm_movemask_pd(m.v); }
inline bool any(vm m) { return movemask(m) != 0; }
inline bool all(vm m) { return movemask(m) == 3; }
// SSE2 has no rounding instruction: the cut through an integer, exact under 2^31, a zero kept as itself
inline vd floor(vd a) {
    const vd t = {_mm_cvtepi32_pd(_mm_cvttpd_epi32(a.v))};
    const vd r = sub(t, {_mm_and_pd(_mm_cmpgt_pd(t.v, a.v), _mm_set1_pd(1.0))});
    return select(eq(a, set1(0.0)), a, r);
}
inline vi trunc_i(vd a) { return {_mm_cvttpd_epi32(a.v)}; }
inline vd cvt_i2d(vi a) { return {_mm_cvtepi32_pd(a.v)}; }
inline vf cvt_d2f(vd a) { return {_mm_cvtpd_ps(a.v)}; }
inline vd cvt_f2d(vf a) { return {_mm_cvtps_pd(a.v)}; }
inline vf fadd(vf a, vf b) { return {_mm_add_ps(a.v, b.v)}; }
inline vf fsub(vf a, vf b) { return {_mm_sub_ps(a.v, b.v)}; }
inline vf fmul(vf a, vf b) { return {_mm_mul_ps(a.v, b.v)}; }
inline vf fset1(float x) { return {_mm_set1_ps(x)}; }
inline vf fload(const float* p) { return {_mm_set_ps(0.f, 0.f, p[1], p[0])}; }
inline vf fselect(vmi m, vf a, vf b) { const __m128 k = _mm_castsi128_ps(m.v); return {_mm_or_ps(_mm_and_ps(k, a.v), _mm_andnot_ps(k, b.v))}; }
inline vi iload(const int32_t* p) { return {_mm_loadl_epi64((const __m128i*)p)}; }
inline void istore(int32_t* p, vi a) { _mm_storel_epi64((__m128i*)p, a.v); }
inline vi iset1(int32_t x) { return {_mm_set1_epi32(x)}; }
inline vi iadd(vi a, vi b) { return {_mm_add_epi32(a.v, b.v)}; }
inline vi isub(vi a, vi b) { return {_mm_sub_epi32(a.v, b.v)}; }
inline vi ixor(vi a, vi b) { return {_mm_xor_si128(a.v, b.v)}; }
inline vi ishl(vi a, int n) { return {_mm_slli_epi32(a.v, n)}; }
inline vi ushr(vi a, int n) { return {_mm_srli_epi32(a.v, n)}; }
inline vmi ige(vi a, vi b) { return {_mm_or_si128(_mm_cmpgt_epi32(a.v, b.v), _mm_cmpeq_epi32(a.v, b.v))}; }
inline vmi ieq(vi a, vi b) { return {_mm_cmpeq_epi32(a.v, b.v)}; }
inline vmi ilt(vi a, vi b) { return {_mm_cmplt_epi32(a.v, b.v)}; }
inline vi iselect(vmi m, vi a, vi b) { return {_mm_or_si128(_mm_and_si128(m.v, a.v), _mm_andnot_si128(m.v, b.v))}; }
inline vm widen(vmi m) { return {_mm_castsi128_pd(_mm_unpacklo_epi32(m.v, m.v))}; }
inline vmi narrow(vm m) { return {_mm_shuffle_epi32(_mm_castpd_si128(m.v), _MM_SHUFFLE(2, 0, 2, 0))}; }
inline vf gather_f(const float* t, vi i) { int32_t k[2]; istore(k, i); return {_mm_set_ps(0.f, 0.f, t[k[1]], t[k[0]])}; }
inline vf gather_f(const float* const* t, vi i) { int32_t k[2]; istore(k, i); return {_mm_set_ps(0.f, 0.f, t[1][k[1]], t[0][k[0]])}; }

#elif FSVR_LANES_NEON
constexpr int W = 2;
struct vd { float64x2_t v; };
struct vm { uint64x2_t v; };
struct vf { float32x2_t v; };
struct vi { int32x2_t v; };
struct vmi { uint32x2_t v; };
inline vd load(const double* p) { return {vld1q_f64(p)}; }
inline void store(double* p, vd a) { vst1q_f64(p, a.v); }
inline vd set1(double x) { return {vdupq_n_f64(x)}; }
inline vd add(vd a, vd b) { return {vaddq_f64(a.v, b.v)}; }
inline vd sub(vd a, vd b) { return {vsubq_f64(a.v, b.v)}; }
inline vd mul(vd a, vd b) { return {vmulq_f64(a.v, b.v)}; }
inline vd div(vd a, vd b) { return {vdivq_f64(a.v, b.v)}; }
// Written as the compares they stand for, so a tie goes the x86 way (the second operand) on this side too.
inline vd min(vd a, vd b) { return {vbslq_f64(vcltq_f64(a.v, b.v), a.v, b.v)}; }
inline vd max(vd a, vd b) { return {vbslq_f64(vcgtq_f64(a.v, b.v), a.v, b.v)}; }
inline vd neg(vd a) { return {vnegq_f64(a.v)}; }
inline vd abs(vd a) { return {vabsq_f64(a.v)}; }
inline vd floor(vd a) { return {vrndmq_f64(a.v)}; }
inline vm lt(vd a, vd b) { return {vcltq_f64(a.v, b.v)}; }
inline vm le(vd a, vd b) { return {vcleq_f64(a.v, b.v)}; }
inline vm ge(vd a, vd b) { return {vcgeq_f64(a.v, b.v)}; }
inline vm gt(vd a, vd b) { return {vcgtq_f64(a.v, b.v)}; }
inline vm eq(vd a, vd b) { return {vceqq_f64(a.v, b.v)}; }
inline vm ne(vd a, vd b) { return {veorq_u64(vceqq_f64(a.v, b.v), vdupq_n_u64(~0ull))}; }
inline vm isnan(vd a) { return {veorq_u64(vceqq_f64(a.v, a.v), vdupq_n_u64(~0ull))}; }
inline vm mand(vm a, vm b) { return {vandq_u64(a.v, b.v)}; }
inline vm mor(vm a, vm b) { return {vorrq_u64(a.v, b.v)}; }
inline vm mnot(vm a) { return {veorq_u64(a.v, vdupq_n_u64(~0ull))}; }
inline vm mtrue() { return {vdupq_n_u64(~0ull)}; }
inline vm mfalse() { return {vdupq_n_u64(0)}; }
inline vd select(vm m, vd a, vd b) { return {vbslq_f64(m.v, a.v, b.v)}; }
inline vm mselect(vm m, vm a, vm b) { return {vbslq_u64(m.v, a.v, b.v)}; }
inline vm loadmask(const uint64_t* p) { return {vld1q_u64(p)}; }
inline void storemask(uint64_t* p, vm m) { vst1q_u64(p, m.v); }
inline int movemask(vm m) { return (int)(vgetq_lane_u64(m.v, 0) >> 63) | (int)(vgetq_lane_u64(m.v, 1) >> 63) << 1; }
inline bool any(vm m) { return movemask(m) != 0; }
inline bool all(vm m) { return movemask(m) == 3; }
inline vi trunc_i(vd a) { return {vmovn_s64(vcvtq_s64_f64(a.v))}; }
inline vd cvt_i2d(vi a) { return {vcvtq_f64_s64(vmovl_s32(a.v))}; }
inline vf cvt_d2f(vd a) { return {vcvt_f32_f64(a.v)}; }
inline vd cvt_f2d(vf a) { return {vcvt_f64_f32(a.v)}; }
inline vf fadd(vf a, vf b) { return {vadd_f32(a.v, b.v)}; }
inline vf fsub(vf a, vf b) { return {vsub_f32(a.v, b.v)}; }
inline vf fmul(vf a, vf b) { return {vmul_f32(a.v, b.v)}; }
inline vf fset1(float x) { return {vdup_n_f32(x)}; }
inline vf fload(const float* p) { return {vld1_f32(p)}; }
inline vf fselect(vmi m, vf a, vf b) { return {vbsl_f32(m.v, a.v, b.v)}; }
inline vi iload(const int32_t* p) { return {vld1_s32(p)}; }
inline void istore(int32_t* p, vi a) { vst1_s32(p, a.v); }
inline vi iset1(int32_t x) { return {vdup_n_s32(x)}; }
inline vi iadd(vi a, vi b) { return {vadd_s32(a.v, b.v)}; }
inline vi isub(vi a, vi b) { return {vsub_s32(a.v, b.v)}; }
inline vi ixor(vi a, vi b) { return {veor_s32(a.v, b.v)}; }
inline vi ishl(vi a, int n) { return {vshl_s32(a.v, vdup_n_s32(n))}; }
inline vi ushr(vi a, int n) { return {vreinterpret_s32_u32(vshl_u32(vreinterpret_u32_s32(a.v), vdup_n_s32(-n)))}; }
inline vmi ige(vi a, vi b) { return {vcge_s32(a.v, b.v)}; }
inline vmi ieq(vi a, vi b) { return {vceq_s32(a.v, b.v)}; }
inline vmi ilt(vi a, vi b) { return {vclt_s32(a.v, b.v)}; }
inline vi iselect(vmi m, vi a, vi b) { return {vbsl_s32(m.v, a.v, b.v)}; }
// By sign: a set lane is -1 and widens to all ones. The unsigned widening put zeros in the high half, half a mask,
// which a select then spliced into a double and movemask read as clear (the macOS leg's segments check).
inline vm widen(vmi m) { return {vreinterpretq_u64_s64(vmovl_s32(vreinterpret_s32_u32(m.v)))}; }
inline vmi narrow(vm m) { return {vmovn_u64(m.v)}; }
inline vf gather_f(const float* t, vi i) { int32_t k[2]; istore(k, i); const float r[2] = {t[k[0]], t[k[1]]}; return {vld1_f32(r)}; }
inline vf gather_f(const float* const* t, vi i) { int32_t k[2]; istore(k, i); const float r[2] = {t[0][k[0]], t[1][k[1]]}; return {vld1_f32(r)}; }

#else
constexpr int W = 1;
struct vd { double v; };
struct vm { uint64_t v; };
struct vf { float v; };
struct vi { int32_t v; };
struct vmi { uint32_t v; };
inline vd load(const double* p) { return {*p}; }
inline void store(double* p, vd a) { *p = a.v; }
inline vd set1(double x) { return {x}; }
inline vd add(vd a, vd b) { return {a.v + b.v}; }
inline vd sub(vd a, vd b) { return {a.v - b.v}; }
inline vd mul(vd a, vd b) { return {a.v * b.v}; }
inline vd div(vd a, vd b) { return {a.v / b.v}; }
inline vd min(vd a, vd b) { return {a.v < b.v ? a.v : b.v}; }
inline vd max(vd a, vd b) { return {a.v > b.v ? a.v : b.v}; }
inline vd neg(vd a) { return {-a.v}; }
inline vd abs(vd a) { return {a.v < 0 ? -a.v : a.v == 0 ? 0.0 : a.v}; }
inline vd floor(vd a) { return {fm::floor_exact(a.v)}; }
inline vm mk(bool b) { return {b ? ~0ull : 0ull}; }
inline vm lt(vd a, vd b) { return mk(a.v < b.v); }
inline vm le(vd a, vd b) { return mk(a.v <= b.v); }
inline vm ge(vd a, vd b) { return mk(a.v >= b.v); }
inline vm gt(vd a, vd b) { return mk(a.v > b.v); }
inline vm eq(vd a, vd b) { return mk(a.v == b.v); }
inline vm ne(vd a, vd b) { return mk(a.v != b.v); }
inline vm isnan(vd a) { return mk(a.v != a.v); }
inline vm mand(vm a, vm b) { return {a.v & b.v}; }
inline vm mor(vm a, vm b) { return {a.v | b.v}; }
inline vm mnot(vm a) { return {~a.v}; }
inline vm mtrue() { return {~0ull}; }
inline vm mfalse() { return {0ull}; }
inline vd select(vm m, vd a, vd b) { return m.v ? a : b; }
inline vm mselect(vm m, vm a, vm b) { return m.v ? a : b; }
inline vm loadmask(const uint64_t* p) { return {*p}; }
inline void storemask(uint64_t* p, vm m) { *p = m.v; }
inline int movemask(vm m) { return m.v ? 1 : 0; }
inline bool any(vm m) { return m.v != 0; }
inline bool all(vm m) { return m.v != 0; }
inline vi trunc_i(vd a) { return {(int32_t)a.v}; }
inline vd cvt_i2d(vi a) { return {(double)a.v}; }
inline vf cvt_d2f(vd a) { return {(float)a.v}; }
inline vd cvt_f2d(vf a) { return {(double)a.v}; }
inline vf fadd(vf a, vf b) { return {a.v + b.v}; }
inline vf fsub(vf a, vf b) { return {a.v - b.v}; }
inline vf fmul(vf a, vf b) { return {a.v * b.v}; }
inline vf fset1(float x) { return {x}; }
inline vf fload(const float* p) { return {*p}; }
inline vf fselect(vmi m, vf a, vf b) { return m.v ? a : b; }
inline vi iload(const int32_t* p) { return {*p}; }
inline void istore(int32_t* p, vi a) { *p = a.v; }
inline vi iset1(int32_t x) { return {x}; }
inline vi iadd(vi a, vi b) { return {(int32_t)((uint32_t)a.v + (uint32_t)b.v)}; }
inline vi isub(vi a, vi b) { return {(int32_t)((uint32_t)a.v - (uint32_t)b.v)}; }
inline vi ixor(vi a, vi b) { return {a.v ^ b.v}; }
inline vi ishl(vi a, int n) { return {(int32_t)((uint32_t)a.v << n)}; }
inline vi ushr(vi a, int n) { return {(int32_t)((uint32_t)a.v >> n)}; }
inline vmi ige(vi a, vi b) { return {a.v >= b.v ? ~0u : 0u}; }
inline vmi ieq(vi a, vi b) { return {a.v == b.v ? ~0u : 0u}; }
inline vmi ilt(vi a, vi b) { return {a.v < b.v ? ~0u : 0u}; }
inline vi iselect(vmi m, vi a, vi b) { return m.v ? a : b; }
inline vm widen(vmi m) { return {m.v ? ~0ull : 0ull}; }
inline vmi narrow(vm m) { return {m.v ? ~0u : 0u}; }
inline vf gather_f(const float* t, vi i) { return {t[i.v]}; }
inline vf gather_f(const float* const* t, vi i) { return {t[0][i.v]}; }
#endif

// A lane out and in, for the rare scalar fixup.
inline double lane(vd a, int i) { double t[W]; store(t, a); return t[i]; }
inline vd setlane(vd a, int i, double x) { double t[W]; store(t, a); t[i] = x; return load(t); }
inline int32_t ilane(vi a, int i) { int32_t t[W]; istore(t, a); return t[i]; }
inline vi isetlane(vi a, int i, int32_t x) { int32_t t[W]; istore(t, a); t[i] = x; return iload(t); }
// A 32-bit lane read as unsigned, exactly, onto its double: the signed conversion and 2^32 back where it went negative.
inline vd cvt_u2d(vi a) { const vd d = cvt_i2d(a); return select(lt(d, set1(0.0)), add(d, set1(4294967296.0)), d); }

}  // namespace fsvr::simd::FSVR_ISA_NS
