// patchfiles/format_aced.cpp - a DX7II, DX7s or TX802 voice's supplement, F0 43 0n 05 00 31 <49> cs F7. Ours.
// "Not interpreted until its following VCED bulk data is received" (Data List 3.2.2), so it waits for the
// next VCED, in this file or the next one read with it.
#include "dx7.h"

namespace fsvr::patchfiles {

bool readAced(const Message& m, Reading& r) {
    if (!dxBulk(m, 0x05) || m.p[4] != 0x00 || m.p[5] != 0x31 || m.len < 6 + ACED_SIZE + 2) return false;
    checkSum(m, 6, ACED_SIZE, "ACED", r);
    r.pending.aced.assign(m.p + 6, m.p + 6 + ACED_SIZE);
    r.acedAt = m.at;
    return true;
}

} // namespace fsvr::patchfiles
