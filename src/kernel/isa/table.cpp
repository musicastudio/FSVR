// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// This instruction set's table of kernels, and the checks that its build fused nothing and that its lanes do what
// simd.h says. Compiled once per set.
#include "kernel/dispatch.h"
#include "kernel/simd.h"

namespace fsvr::kernel::FSVR_ISA_NS {

void filter_bank(fsvr::filters::Lanes& L, const fsvr::filters::Shared& g, const double* in, const double* eg, const double* lfo, double* out);

namespace {
// a * a + c with a = 1 + 2^-27 and c = -1 is 2^-26 in two roundings and 2^-26 + 2^-54 in one (math/fastmath.cpp has the scalar twin)
bool fma_contracted() {
    using namespace fsvr::simd::FSVR_ISA_NS;
    volatile double av = 1.0 + 1.0 / (1 << 27), cv = -1.0;
    const vd a = set1(av), c = set1(cv);
    return lane(add(mul(a, a), c), 0) != 1.0 / (1 << 26);
}
// The lane operations whose instructions differ between the sets, held to what simd.h says: an integer lane's mask
// widens to the whole of its double lane's and narrows back, a select takes whole lanes, movemask reads the lanes
// set, a tie goes to the second operand, floor keeps a zero's sign, and the conversions round as the casts do.
// The neon widening zero-extended once, half a mask, and only the macOS leg's segments check said so.
bool lanes_ok() {
    using namespace fsvr::simd::FSVR_ISA_NS;
    bool ok = true;
    for (int l = 0; l < W; l++) {
        int32_t one[W] = {}; one[l] = 1;
        const vmi mi = ieq(iload(one), iset1(1));          // the one lane set, as a compare leaves it
        const vm md = widen(mi);
        uint64_t bits[W]; storemask(bits, md);
        for (int k = 0; k < W; k++) ok = ok && bits[k] == (k == l ? ~0ull : 0ull);
        ok = ok && movemask(md) == 1 << l && any(md) && all(md) == (W == 1);
        double a[W], b[W], out[W];
        for (int k = 0; k < W; k++) { a[k] = -1.5; b[k] = 2.5; }
        store(out, select(md, load(a), load(b)));
        for (int k = 0; k < W; k++) ok = ok && out[k] == (k == l ? -1.5 : 2.5);
        int32_t back[W]; istore(back, iselect(narrow(md), iset1(1), iset1(0)));
        for (int k = 0; k < W; k++) ok = ok && back[k] == (k == l ? 1 : 0);
    }
    volatile double zero = 0.0;
    const double nanv = zero / zero;
    ok = ok && 1.0 / lane(max(set1(-0.0), set1(0.0)), 0) > 0 && 1.0 / lane(min(set1(0.0), set1(-0.0)), 0) < 0;
    ok = ok && lane(max(set1(nanv), set1(1.0)), 0) == 1.0 && lane(min(set1(1.0), set1(nanv)), 0) != lane(min(set1(1.0), set1(nanv)), 0);
    ok = ok && lane(floor(set1(-0.5)), 0) == -1.0 && 1.0 / lane(floor(set1(-0.0)), 0) < 0 && lane(floor(set1(2.0)), 0) == 2.0;
    ok = ok && ilane(trunc_i(set1(-2.7)), 0) == -2 && lane(cvt_u2d(iset1((int32_t)0x80000000u)), 0) == 2147483648.0;
    return ok;
}
#define FSVR_STR_(x) #x
#define FSVR_STR(x) FSVR_STR_(x)
const fsvr::Kernels TABLE = {FSVR_STR(FSVR_ISA_NS), fsvr::simd::FSVR_ISA_NS::W, &filter_bank, &fma_contracted, &lanes_ok};
}

const fsvr::Kernels& table() { return TABLE; }

}  // namespace fsvr::kernel::FSVR_ISA_NS
