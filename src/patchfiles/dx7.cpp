// patchfiles/dx7.cpp - DX7 voice data, packed and unpacked. Ours.
#include "dx7.h"
#include <algorithm>
#include <cstring>

namespace fsvr::patchfiles {

uint8_t checksum(const uint8_t* data, size_t n) {
    int sum = 0;
    for (size_t i = 0; i < n; ++i) sum += data[i];
    return (uint8_t)(-sum & 0x7F);
}

void checkSum(const Message& m, size_t data, size_t n, const char* what, Reading& r) {
    if (data + n >= m.len) return;
    // Ours: a checksum with its top bit set, as some librarians write the sum's low byte, is the right one.
    const uint8_t want = checksum(m.p + data, n), got = m.p[data + n];
    if ((got & 0x7F) != want) r.warn(m, std::string(what) + " checksum is " + std::to_string(got) + ", not " + std::to_string(want) + "; read anyway");
}

std::string text(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s += p[i] >= 32 && p[i] < 127 ? (char)p[i] : ' ';
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static Bytes message(uint8_t format, uint8_t c1, uint8_t c2, const uint8_t* data, size_t n) {
    Bytes m = {0xF0, 0x43, 0x00, format, c1, c2};
    m.insert(m.end(), data, data + n);
    m.push_back(checksum(data, n));
    m.push_back(0xF7);
    return m;
}

Bytes vcedMessage(const uint8_t* vced) { return message(0x00, 0x01, 0x1B, vced, VCED_SIZE); }
Bytes acedMessage(const uint8_t* aced) { return message(0x05, 0x00, 0x31, aced, ACED_SIZE); }

// A VMEM record (128 bytes) as VCED (155): six operators, OP6 first, 17 packed bytes each to 21, then the
// voice's common block. Bits set outside a field are dropped without a word, as the DX7 ignores them: a third
// of the DX banks in circulation carry some.
void unpackRecord(const uint8_t* v, uint8_t* o) {
    for (int op = 0; op < 6; ++op) {
        const uint8_t* p = v + op * 17;
        uint8_t* d = o + op * 21;
        for (int k = 0; k < 11; ++k) d[k] = p[k] & 0x7F;              // rates, levels, break point, depths
        d[11] = p[11] & 3; d[12] = p[11] >> 2 & 3;                      // curves
        d[13] = p[12] & 7; d[20] = p[12] >> 3 & 15;                     // rate scaling, detune
        d[14] = p[13] & 3; d[15] = p[13] >> 2 & 7;                      // amp mod, key velocity sensitivity
        d[16] = p[14] & 0x7F;                                           // output level
        d[17] = p[15] & 1; d[18] = p[15] >> 1 & 31; d[19] = p[16] & 0x7F;   // mode, coarse, fine
    }
    for (int k = 0; k < 8; ++k) o[126 + k] = v[102 + k] & 0x7F;         // pitch EG
    o[134] = v[110] & 31; o[135] = v[111] & 7; o[136] = v[111] >> 3 & 1;
    for (int k = 0; k < 4; ++k) o[137 + k] = v[112 + k] & 0x7F;         // LFO speed, delay, depths
    o[141] = v[116] & 1; o[142] = v[116] >> 1 & 7; o[143] = v[116] >> 4 & 7;
    o[144] = v[117] & 0x7F;
    for (size_t k = 0; k < NAME_LENGTH; ++k) o[VCED_NAME + k] = v[RECORD_NAME + k] & 0x7F;
}

void packRecord(const uint8_t* o, uint8_t* v) {
    for (int op = 0; op < 6; ++op) {
        const uint8_t* d = o + op * 21;
        uint8_t* p = v + op * 17;
        for (int k = 0; k < 11; ++k) p[k] = d[k];
        p[11] = (uint8_t)(d[11] | d[12] << 2);
        p[12] = (uint8_t)(d[13] | d[20] << 3);
        p[13] = (uint8_t)(d[14] | d[15] << 2);
        p[14] = d[16];
        p[15] = (uint8_t)(d[17] | d[18] << 1);
        p[16] = d[19];
    }
    for (int k = 0; k < 8; ++k) v[102 + k] = o[126 + k];
    v[110] = o[134]; v[111] = (uint8_t)(o[135] | o[136] << 3);
    for (int k = 0; k < 4; ++k) v[112 + k] = o[137 + k];
    v[116] = (uint8_t)(o[141] | o[142] << 1 | o[143] << 4);
    v[117] = o[144];
    for (size_t k = 0; k < NAME_LENGTH; ++k) v[RECORD_NAME + k] = o[VCED_NAME + k];
}

// The top of each VCED byte's range: per operator rates and levels, break point, depths, curves, rate
// scaling, amp mod sense, velocity sense, output level, mode, coarse, fine, detune; then the pitch EG,
// algorithm, feedback, oscillator sync, LFO speed, delay, depths, sync, wave, pitch mod sense, transpose.
static const uint8_t OP_MAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
static const uint8_t COMMON_MAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};

bool clampVced(uint8_t* v) {
    bool clamped = false;
    auto clamp = [&](uint8_t& b, uint8_t top) { if (b > top) { b = top; clamped = true; } };
    for (int op = 0; op < 6; ++op)
        for (int k = 0; k < 21; ++k) clamp(v[op * 21 + k], OP_MAX[k]);
    for (int k = 0; k < 19; ++k) clamp(v[126 + k], COMMON_MAX[k]);
    for (size_t k = 0; k < NAME_LENGTH; ++k)   // a name is ASCII 32-126; anything else is a space
        if (uint8_t& c = v[VCED_NAME + k]; c < 32 || c > 126) { c = ' '; clamped = true; }
    return clamped;
}

constexpr uint8_t BREAK_C3 = 39;   // keyboard level scaling break point, counted from A-1

void initVoice(uint8_t* v) {
    std::memset(v, 0, VCED_SIZE);
    for (int op = 0; op < 6; ++op) {
        uint8_t* d = v + op * 21;
        for (int k = 0; k < 4; ++k) d[k] = 99;
        d[4] = d[5] = d[6] = 99;   // levels 1-3; level 4 is 0
        d[8] = BREAK_C3;
        d[18] = 1;                 // coarse 1
        d[20] = 7;                 // detune centred
    }
    v[5 * 21 + 16] = 99;           // OP1 (stored last) at full output
    for (int k = 0; k < 4; ++k) { v[126 + k] = 99; v[130 + k] = 50; }
    v[136] = 1; v[137] = 35; v[141] = 1; v[143] = 3; v[144] = 24;
    std::memcpy(v + VCED_NAME, "INIT VOICE", NAME_LENGTH);
}

void amemToAced(const uint8_t* a, uint8_t* u) {
    std::memset(u, 0, ACED_SIZE);
    for (int i = 0; i < 6; ++i) u[i] = a[0] >> i & 1;                           // scaling mode, OP6 first
    for (int i = 0; i < 3; ++i) { u[6 + 2 * i] = a[1 + i] & 7; u[7 + 2 * i] = a[1 + i] >> 3 & 7; }   // amp mod sense, OP6 first
    u[12] = a[4] & 3;                                                            // PEG range
    u[13] = a[4] >> 2 & 1;                                                       // LFO key trigger
    u[14] = a[4] >> 3 & 1;                                                       // PEG velocity switch
    u[15] = a[5] & 3;                                                            // poly/mono, unison
    u[16] = a[5] >> 2 & 15;                                                      // pitch bend range
    u[17] = a[6] & 15; u[18] = a[6] >> 4 & 3;                                    // pitch bend step, mode
    u[19] = a[4] >> 4 & 7;                                                       // random pitch
    u[20] = a[7] & 1; u[21] = a[7] >> 1 & 15; u[22] = a[8] & 0x7F;               // portamento mode, step, time
    for (int i = 0; i < 3; ++i) u[23 + i] = a[9 + i] & 0x7F;                     // mod wheel
    for (int i = 0; i < 4; ++i) u[26 + i] = a[12 + i] & 0x7F;                    // foot controller 1
    for (int i = 0; i < 4; ++i) u[30 + i] = a[16 + i] & 0x7F;                    // breath
    for (int i = 0; i < 4; ++i) u[34 + i] = a[20 + i] & 0x7F;                    // aftertouch
    u[38] = a[24] & 7;                                                           // PEG rate scaling
    for (int i = 0; i < 4; ++i) u[39 + i] = a[26 + i] & 0x7F;                    // foot controller 2
    for (int i = 0; i < 4; ++i) u[43 + i] = a[30 + i] & 0x7F;                    // MIDI in controller
    u[47] = a[34] & 7; u[48] = a[34] >> 3 & 1;                                   // unison detune, FC1 as CS1
}

bool acedMatters(const uint8_t* u) {
    for (int i = 6; i < 12; ++i)
        if (u[i]) return true;
    return u[0x0C] || u[0x0E] || u[0x26];
}

Item dxVoice(const uint8_t* vced, const uint8_t* aced) {
    Item it;
    it.name = text(vced + VCED_NAME, NAME_LENGTH);
    if (aced) it.syx = acedMessage(aced);
    const Bytes v = vcedMessage(vced);
    it.syx.insert(it.syx.end(), v.begin(), v.end());
    return it;
}

} // namespace fsvr::patchfiles
