// patchfiles/format_tx802.cpp - a TX802's performances, "LM  8952PM", each 84 bytes sent as 168 ASCII hex
// digits. Ours.
//
// The framing is the factory dumps' (11589 bytes for 64): one F0 43 0n 7E, then per performance its count 01 28,
// the tag, the digits and a checksum over the tag and digits, and one F7 at the end. A performance sent as a
// message of its own reads the same way.
//
// The record is eight rows of eight bytes, one byte per tone generator, then a 20-character name:
//   0  link (bits 5-7) and receive channel (0-15, 16 omni)   32  detune (bits 3-6, 7 centre) and output (bits 0-1)
//   8  voice (0-63 internal, 64-127 cartridge, 128-255 presets A and B)   40  note limit low   48  note limit high
//  24  output volume 0-99                                    56  note shift (bits 0-5, 24 centre)
// The link names the TG whose notes this one adds to: every factory performance that stacks a voice links its
// TGs so, the linked ones carrying no voice of their own. Settled against the factory set's 64 performances and
// its voice list (Patches/TX802/TX802_Factory): voice numbers count from 0, so 0x82 is A03, ReverbBras.
// A TG at volume 0, like one with no output, plays nothing and takes no part.
#include "dx7.h"
#include <cmath>

namespace fsvr::patchfiles {

constexpr size_t RECORD_SIZE = 84, TAG = 10, FRAME = 2 + TAG + 2 * RECORD_SIZE + 1, NAME_AT = 64, NAME_SIZE = 20;
constexpr int TGS = 8, OMNI = 16;

static int nibble(uint8_t c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static DxPerformance performance(const uint8_t* b, Reading& r) {
    DxPerformance d;
    d.kind = "a TX802 performance";
    d.tx802 = true;
    std::string name = text(b + NAME_AT, NAME_SIZE);
    name.erase(0, name.find_first_not_of(' '));
    d.name = name;
    std::string dropped;
    int parts = 0;
    for (int t = 0; t < TGS; ++t) {
        const int out = b[32 + t] & 3;
        if ((b[t] >> 5) != t || !out) continue;   // linked to another TG, or no output
        if (!(b[24 + t] & 0x7F)) { d.silent = true; continue; }   // ours: a TG at volume 0 takes no part
        if (parts == 4) { dropped += (dropped.empty() ? "TG" : ", TG") + std::to_string(t + 1); continue; }
        PartSetup& s = d.part[parts++];
        s.on = true;
        s.voice = b[8 + t];
        const int ch = b[t] & 0x1F;
        s.channel = ch >= OMNI ? PFM : ch;   // ours: omni plays on the performance channel
        s.volume = (int)std::lround(127 * std::min(b[24 + t] & 0x7F, 99) / 99.0);
        s.detune = (b[32 + t] >> 3 & 15) - 7;   // ours: one TX802 detune step, one FS1R part detune step
        s.pan = out == 1 ? 1 : out == 2 ? 127 : 64;   // ours: output I hard left, II hard right, both centre
        s.low = b[40 + t] & 0x7F;
        s.high = b[48 + t] & 0x7F;
        s.shift = (b[56 + t] & 0x3F) - 24;
    }
    if (!dropped.empty())
        r.report.warnings.push_back((r.bank.name.empty() ? "" : r.bank.name + ", ") + "TX802 performance \"" + name + "\": " + dropped + " not read (the FS1R has four parts)");
    return d;
}

bool readTx802(const Message& m, Reading& r) {
    if (!lmBulk(m, "LM  8952PM")) return false;
    bool any = false;
    for (size_t at = 4; at + FRAME < m.len; at += FRAME) {
        const uint8_t* f = m.p + at;
        if (!std::equal(f + 2, f + 2 + TAG, "LM  8952PM")) break;
        uint8_t rec[RECORD_SIZE];
        bool hex = true;
        for (size_t k = 0; k < RECORD_SIZE && hex; ++k) {
            const int hi = nibble(f[2 + TAG + 2 * k]), lo = nibble(f[2 + TAG + 2 * k + 1]);
            hex = hi >= 0 && lo >= 0;
            rec[k] = (uint8_t)(hi << 4 | lo);
        }
        if (!hex) { r.warn(m, "a TX802 performance that is not hex digits, and the rest after it, not read"); break; }
        const uint8_t want = checksum(f + 2, TAG + 2 * RECORD_SIZE), got = f[FRAME - 1];
        if (got != want) r.warn(m, "TX802 PMEM checksum is " + std::to_string(got) + ", not " + std::to_string(want) + "; read anyway");
        any = true;
        // Ours: a blank name is an empty slot, as the factory set's last ten are, and is not read.
        if (std::all_of(rec + NAME_AT, rec + NAME_AT + NAME_SIZE, [](uint8_t c) { return c == ' '; })) continue;
        DxPerformance d = performance(rec, r);
        d.at = m.at;
        d.len = m.len;
        r.dx.push_back(std::move(d));
    }
    r.open = -1;
    return any;
}

} // namespace fsvr::patchfiles
