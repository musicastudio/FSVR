// patchfiles/format_fs1r.cpp - the FS1R's own bulks: performances, voices and Fseqs at any of their addresses
// (Data List 3.2.1). Ours. Voice and Fseq bulks right after a performance are its parts' voices and its Fseq.
#include "dx7.h"
#include <algorithm>

namespace fsvr::patchfiles {

bool readFs1r(const Message& m, Reading& r) {
    if (m.len < 11 || m.p[1] != 0x43 || (m.p[2] & 0xF0) != 0 || m.p[3] != 0x5E) return false;
    const int ah = m.p[6];
    const uint8_t* d = m.p + 9;
    const size_t dl = m.len - 11;
    Item it;
    it.address = ah;
    it.syx.assign(m.p, m.p + m.len);
    it.at = m.at;
    it.len = m.len;
    Bank& bank = r.bank;
    if ((ah == 0x10 || ah == 0x11) && dl >= 400) {
        it.name = text(d, 12);
        it.category = d[14];
        it.number = ah == 0x11 ? m.p[8] : -1;
        bank.perfs.push_back(it);
        r.open = (int)bank.perfs.size() - 1;
    } else if (((ah >= 0x40 && ah <= 0x43) || ah == 0x51) && dl >= 608) {
        it.name = text(d, 10);
        it.category = d[0x0E];
        it.number = ah == 0x51 ? m.p[8] : -1;
        bank.voices.push_back(it);
        if (ah <= 0x43 && r.open >= 0) bank.perfs[(size_t)r.open].partVoice[ah - 0x40] = (int)bank.voices.size() - 1;
        else r.open = -1;
    } else if ((ah == 0x60 || ah == 0x61 || ah == 0x70) && dl >= 32) {
        it.name = text(d, 8);
        const int total = 128 * ((d[0x1B] & 3) + 1), end = d[0x1E] << 7 | d[0x1F];
        it.frames = end ? std::min(total, end + 1) : total;
        it.number = ah == 0x61 ? m.p[8] : -1;
        bank.fseqs.push_back(it);
        if (r.open >= 0) bank.perfs[(size_t)r.open].fseq = (int)bank.fseqs.size() - 1;
        r.open = -1;
    } else {
        r.open = -1;
        return false;   // the system bulk, or one cut short: the dispatcher reports it
    }
    return true;
}

} // namespace fsvr::patchfiles
