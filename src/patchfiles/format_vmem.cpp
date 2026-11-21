// patchfiles/format_vmem.cpp - a DX7 32-voice bank, F0 43 0n 09 <count> <32 x 128 packed> cs F7. Ours.
//
// Unpacked to VCED here, since the FS1R takes only VCED. The byte count is not trusted: the DX7 sends 20 00
// and the DX5 10 00 for the same 4096 bytes. A bad checksum is a warning, and bytes out of the DX7's ranges
// are clamped to them. An AMEM read before it gives each voice its supplement.
#include "dx7.h"
#include <cstring>

namespace fsvr::patchfiles {

// A DX7 voice's AMEM slot, as a DX7II gives a voice loaded from a DX7 bank: pitch bend range 2, the rest 0.
constexpr size_t AMEM_BEND = 5;   // pitch bend range, bits 2-5
constexpr int DEFAULT_BEND = 2;

bool readVmem(const Message& m, Reading& r) {
    if (!dxBulk(m, 0x09) || m.len < 6 + VMEM_SIZE + 2) return false;
    checkSum(m, 6, VMEM_SIZE, "VMEM", r);
    if (!(m.p[4] == 0x20 && m.p[5] == 0x00))
        r.warn(m, "VMEM byte count " + std::to_string(m.p[4]) + " " + std::to_string(m.p[5]) + " read as the 4096 bytes it holds");
    const auto whole = std::make_shared<const Bytes>(m.p, m.p + m.len);
    Bytes amem;
    amem.swap(r.pending.amem);
    std::shared_ptr<const Bytes> amemMessage;
    amemMessage.swap(r.amem);
    // The voices' numbers, for a performance that names them: 32 from the bank select's half, or, with none
    // before it, on from the last bank read (ours: a TX802's banks come as four files with no bank select).
    const bool selected = r.pending.half >= 0;
    const int base = selected ? 32 * r.pending.half : r.pending.next;
    r.pending.half = -1;
    r.pending.next = (base + 32) % 128;
    if (std::any_of(r.numbered.begin() + base, r.numbered.begin() + base + 32, [](int k) { return k >= 0; })) newDump(r);
    int clamped = 0;
    for (int k = 0; k < 32; ++k) {
        uint8_t vced[VCED_SIZE], aced[ACED_SIZE];
        unpackRecord(m.p + 6 + RECORD * k, vced);
        clamped += clampVced(vced);
        const bool supplement = !amem.empty() && (amemToAced(amem.data() + AMEM_SLOT * k, aced), acedMatters(aced));
        Item it = dxVoice(vced, supplement ? aced : nullptr);
        it.at = m.at;
        it.len = m.len;
        it.slot = k;
        it.vmem = whole;
        it.amem = amemMessage;
        it.amemAt = r.amemAt;
        r.numbered[(size_t)(base + k)] = (int)r.bank.voices.size();
        r.selected[(size_t)(base + k)] = selected;
        r.bank.voices.push_back(std::move(it));
    }
    if (clamped) r.warn(m, std::to_string(clamped) + " VMEM voices had bytes out of the DX7's ranges, clamped");
    r.open = -1;
    return true;
}

static Bytes resummed(Bytes m) {
    m[6 + VMEM_SIZE] = checksum(m.data() + 6, VMEM_SIZE);
    return m;
}

} // namespace fsvr::patchfiles

namespace fsvr {

using namespace patchfiles;

std::vector<uint8_t> vmemRenamed(const Item& it, const std::string& name) {
    if (!it.vmem || it.slot < 0 || it.vmem->size() < 6 + VMEM_SIZE + 2) return it.vmem ? *it.vmem : Bytes{};
    Bytes m = *it.vmem;
    uint8_t* p = m.data() + 6 + RECORD * (size_t)it.slot + RECORD_NAME;
    for (size_t k = 0; k < NAME_LENGTH; ++k) {
        const char c = k < name.size() ? name[k] : ' ';
        p[k] = (uint8_t)(c >= 32 && c < 127 ? c : ' ');
    }
    return resummed(m);
}

std::vector<uint8_t> vmemBlanked(const Item& it) {
    if (!it.vmem || it.slot < 0 || it.vmem->size() < 6 + VMEM_SIZE + 2) return it.vmem ? *it.vmem : Bytes{};
    Bytes m = *it.vmem;
    uint8_t vced[VCED_SIZE];
    initVoice(vced);
    packRecord(vced, m.data() + 6 + RECORD * (size_t)it.slot);
    return resummed(m);
}

std::vector<uint8_t> amemBlanked(const Item& it) {
    if (!it.amem || it.slot < 0 || it.amem->size() < 6 + AMEM_SIZE + 2) return it.amem ? *it.amem : Bytes{};
    Bytes m = *it.amem;
    uint8_t* s = m.data() + 6 + AMEM_SLOT * (size_t)it.slot;
    std::fill(s, s + AMEM_SLOT, 0);
    s[AMEM_BEND] = DEFAULT_BEND << 2;
    m[6 + AMEM_SIZE] = checksum(m.data() + 6, AMEM_SIZE);
    return m;
}

} // namespace fsvr
