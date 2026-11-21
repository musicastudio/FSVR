// patchfiles/container_smf.cpp - a Standard MIDI File's sysex events, in order, as one stream. Ours.
//
// An F0 event opens a message; F7 events continue it until a packet ends in F7. A meta or channel event in
// between drops an unfinished message. Channel events keep running status (SMF 1.0), so note data is never
// read as an event.
#include "reading.h"
#include <algorithm>

namespace fsvr::patchfiles {

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static size_t vlq(const Bytes& b, size_t at, size_t end, size_t& value) {
    value = 0;
    while (at < end) {
        const uint8_t c = b[at++];
        value = value << 7 | (c & 0x7F);
        if (c < 0x80) break;
    }
    return at;
}

// One data byte after a program change or channel pressure, two after the others.
static size_t dataBytes(uint8_t status) { return (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 1 : 2; }

bool smfStream(const Bytes& b, Bytes& out, Report& report) {
    if (b.size() < 14 || b[0] != 'M' || b[1] != 'T' || b[2] != 'h' || b[3] != 'd') return false;
    for (size_t at = 8 + (size_t)be32(b.data() + 4); at + 8 <= b.size();) {
        const size_t start = at + 8, end = std::min(start + (size_t)be32(b.data() + at + 4), b.size());
        if (b[at] == 'M' && b[at + 1] == 'T' && b[at + 2] == 'r' && b[at + 3] == 'k') {
            Bytes msg;
            uint8_t running = 0;
            for (size_t pos = start, n; pos < end;) {
                pos = vlq(b, pos, end, n);   // the delta time
                if (pos >= end) break;
                const uint8_t status = b[pos];
                if (status == 0xF0 || status == 0xF7) {
                    pos = vlq(b, pos + 1, end, n);
                    const size_t stop = std::min(pos + n, end);
                    if (status == 0xF0) msg.assign({0xF0});
                    if (!msg.empty()) msg.insert(msg.end(), b.begin() + (long)pos, b.begin() + (long)stop);
                    if (!msg.empty() && msg.back() == 0xF7) { out.insert(out.end(), msg.begin(), msg.end()); msg.clear(); }
                    pos = stop;
                    running = 0;
                } else if (status == 0xFF) {
                    pos = std::min(vlq(b, pos + 2, end, n) + n, end);
                    msg.clear();
                    running = 0;
                } else if (status >= 0x80) {
                    running = status;
                    pos += 1 + dataBytes(status);
                    msg.clear();
                } else {
                    pos += running ? dataBytes(running) : 1;
                    msg.clear();
                }
            }
        }
        at = start + (end - start);
        if (end == b.size()) break;
    }
    if (out.empty()) report.skipped.push_back("a MIDI file with no sysex in it");
    return true;
}

} // namespace fsvr::patchfiles
