// patchfiles/container_headerless.cpp - DX7 voice data without its sysex frame, wrapped in the one it lost. Ours.
//
// - A 32-voice bank: 4096 bytes, or one bank with up to 256 bytes of a librarian's header or tail around it,
//   or several banks back to back.
// - Single voices: 155 bytes of VCED, or 156 with the operator on/off byte after it (the FS1R's own
//   .Dx7Voice export), or several 155-byte voices back to back.
// Most voices carry a printable name where the layout puts it, which is how a file is told from anything
// else of the same size. A byte with its top bit set, which a sysex stream cannot carry, has it cleared.
#include "dx7.h"
#include <algorithm>

namespace fsvr::patchfiles {

constexpr size_t SLACK = 256;   // a header or tail around a bank

static bool printable(const uint8_t* p) {
    for (size_t k = 0; k < NAME_LENGTH; ++k)
        if ((p[k] & 0x7F) < 32 || (p[k] & 0x7F) > 126) return false;
    return true;
}

// Whether count records of size bytes from at carry names at name, half of them at least.
static bool looksLike(const Bytes& b, size_t at, size_t count, size_t size, size_t name) {
    size_t named = 0;
    for (size_t r = 0; r < count; ++r) named += printable(b.data() + at + r * size + name);
    return named * 2 >= count;
}

static void wrapBank(const uint8_t* data, Bytes& out) {
    const Bytes m = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    out.insert(out.end(), m.begin(), m.end());
    out.insert(out.end(), data, data + VMEM_SIZE);
    out.push_back(checksum(data, VMEM_SIZE));
    out.push_back(0xF7);
}

static bool headerless(const Bytes& b, Bytes& out);

bool headerlessStream(const Bytes& file, Bytes& out, Report& report) {
    const size_t n = file.size();
    if (!n || file[0] == 0xF0) return false;
    const size_t high = (size_t)std::count_if(file.begin(), file.end(), [](uint8_t c) { return c & 0x80; });
    Bytes b(file);
    for (uint8_t& c : b) c &= 0x7F;
    const bool taken = headerless(b, out);
    if (taken && high) report.warnings.push_back(std::to_string(high) + " bytes had their top bit set, cleared");
    return taken;
}

static bool headerless(const Bytes& b, Bytes& out) {
    const size_t n = b.size();
    if (n % VMEM_SIZE == 0) {
        for (size_t at = 0; at < n; at += VMEM_SIZE)
            if (!looksLike(b, at, 32, RECORD, RECORD_NAME)) { out.clear(); break; }
            else wrapBank(b.data() + at, out);
        if (!out.empty()) return true;
    }
    if (n > VMEM_SIZE && n <= VMEM_SIZE + SLACK)
        for (size_t at : {(size_t)0, n - VMEM_SIZE})   // the bank after a header, or before a tail
            if (looksLike(b, at, 32, RECORD, RECORD_NAME)) { wrapBank(b.data() + at, out); return true; }
    const size_t count = n == VCED_SIZE + 1 ? 1 : n % VCED_SIZE == 0 ? n / VCED_SIZE : 0;
    if (count && looksLike(b, 0, count, count == 1 ? n : VCED_SIZE, VCED_NAME)) {
        for (size_t v = 0; v < count; ++v) {
            const Bytes m = vcedMessage(b.data() + v * VCED_SIZE);
            out.insert(out.end(), m.begin(), m.end());
        }
        return true;
    }
    return false;
}

} // namespace fsvr::patchfiles
