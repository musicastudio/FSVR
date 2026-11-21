// patchfiles/format_amem.cpp - a DX7II, DX7s or TX802 bank's supplements, F0 43 0n 06 08 60 <32 x 35> cs F7,
// and the DX7II's bank select before it, F0 43 1n 19 4D vv F7. Ours.
//
// A bank goes out as bank select, AMEM, VMEM. The AMEM waits for the next VMEM, in this file or the next one
// read with it, and gives each of its voices the slot's ACED when the slot would change the conversion; a
// DX7 voice's default slot would not, so a DX7II bank loads as the DX7 bank of the same voices does.
#include "dx7.h"

namespace fsvr::patchfiles {

constexpr uint8_t BANK_SELECT = 0x4D;

bool readAmem(const Message& m, Reading& r) {
    if (m.len == 7 && m.p[1] == 0x43 && (m.p[2] & 0xF0) == 0x10 && m.p[3] == 0x19 && m.p[4] == BANK_SELECT) {
        r.pending.half = m.p[5] & 1;
        return true;
    }
    if (!dxBulk(m, 0x06) || m.p[4] != 0x08 || m.p[5] != 0x60 || m.len < 6 + AMEM_SIZE + 2) return false;
    checkSum(m, 6, AMEM_SIZE, "AMEM", r);
    r.pending.amem.assign(m.p + 6, m.p + 6 + AMEM_SIZE);
    r.amem = std::make_shared<const Bytes>(m.p, m.p + m.len);
    r.amemAt = m.at;
    return true;
}

} // namespace fsvr::patchfiles
