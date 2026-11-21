// patchfiles/container_sysex.cpp - a sysex stream: a .syx, and an .fs1, which is one performance and its
// voices as the same messages. Ours. Bytes outside an F0..F7, a librarian's header or fill, are not read.
//
// A damaged stream is mended first (repairStream): a Yamaha bulk that lost its F7, at the end of the file or
// before the next F0, gets it back, and a DX bulk one byte short of its fixed size lost its checksum, which is
// put back. The report says how many, and the mended stream is what the library keeps.
#include "dx7.h"

namespace fsvr::patchfiles {

std::vector<Message> messages(const Bytes& b) {
    std::vector<Message> out;
    const size_t n = b.size();
    for (size_t i = 0; i < n;) {
        if (b[i] != 0xF0) { ++i; continue; }
        size_t j = i + 1;
        while (j < n && b[j] != 0xF7 && b[j] != 0xF0) ++j;
        if (j >= n) break;
        if (b[j] == 0xF0) { i = j; continue; }   // never ended: the next message starts here
        out.push_back({b.data() + i, j - i + 1, i});
        i = j + 1;
    }
    return out;
}

// The data size a DX bulk's format fixes, 0 for any other message: VCED, ACED, AMEM, VMEM.
static size_t dxData(const uint8_t* p, size_t len) {
    if (len < 4 || p[1] != 0x43 || (p[2] & 0xF0) != 0) return 0;
    switch (p[3]) {
    case 0x00: return VCED_SIZE;
    case 0x05: return ACED_SIZE;
    case 0x06: return AMEM_SIZE;
    case 0x09: return VMEM_SIZE;
    }
    return 0;
}

bool repairStream(const Bytes& b, Bytes& out, Report& report, const std::string& name) {
    int unended = 0, unsummed = 0;
    Bytes s;
    const size_t n = b.size();
    for (size_t i = 0; i < n;) {
        if (b[i] != 0xF0) { s.push_back(b[i++]); continue; }
        size_t j = i + 1;
        while (j < n && b[j] != 0xF7 && b[j] != 0xF0) ++j;
        const bool ended = j < n && b[j] == 0xF7, yamaha = j - i >= 2 && b[i + 1] == 0x43;
        s.insert(s.end(), b.begin() + (std::ptrdiff_t)i, b.begin() + (std::ptrdiff_t)j);
        if (const size_t data = dxData(b.data() + i, j - i); data && j - i == 6 + data) {
            s.push_back(checksum(b.data() + i + 6, data));
            ++unsummed;
        }
        if (ended || yamaha) s.push_back(0xF7);
        unended += !ended && yamaha;
        i = ended ? j + 1 : j;
    }
    if (!unended && !unsummed) return false;
    const std::string where = name.empty() ? "" : name + ", ";
    if (unended) report.warnings.push_back(where + std::to_string(unended) + (unended == 1 ? " bulk had" : " bulks had") + " no F7, read to the next message or the end of the file");
    if (unsummed) report.warnings.push_back(where + std::to_string(unsummed) + (unsummed == 1 ? " DX bulk had" : " DX bulks had") + " no checksum, read without one");
    out = std::move(s);
    return true;
}

} // namespace fsvr::patchfiles
