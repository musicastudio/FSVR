// selftest/patchfiles.cpp - the patch file reader driven with small files built here, one per format and container.
#include "engine/synth.h"
#include "patchfiles/patchfiles.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

using Bytes = std::vector<uint8_t>;
int fails = 0;
void ck(const char* what, bool ok) { if (!ok) { printf("  FAIL %s\n", what); fails++; } }

uint8_t sum7(const uint8_t* p, size_t n) { int s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return (uint8_t)(-s & 0x7F); }

// A Yamaha bulk: F0 43 0n <format> <count> <data> <checksum> F7, the checksum over the data.
Bytes dxBulk(uint8_t format, uint8_t c1, uint8_t c2, const Bytes& data) {
    Bytes m = {0xF0, 0x43, 0x00, format, c1, c2};
    m.insert(m.end(), data.begin(), data.end());
    m.push_back(sum7(data.data(), data.size())); m.push_back(0xF7);
    return m;
}

// An FS1R voice bulk (608 bytes) for part 1, named.
Bytes fs1rVoice(const char* name) {
    Bytes d(608, 0); memcpy(d.data(), name, strlen(name)); d[0x0E] = 5;
    Bytes m = {0xF0, 0x43, 0x00, 0x5E, 608 >> 7, 608 & 0x7F, 0x40, 0x00, 0x00};
    m.insert(m.end(), d.begin(), d.end());
    m.push_back(sum7(m.data() + 4, m.size() - 4)); m.push_back(0xF7);
    return m;
}

// A packed VMEM record, named, every byte valid.
Bytes record(const char* name) {
    Bytes r(128, 0);
    for (int op = 0; op < 6; op++) { uint8_t* p = r.data() + op * 17; for (int k = 0; k < 4; k++) { p[k] = 99; p[4 + k] = 99; } p[12] = 7 << 3; p[15] = 2; }
    for (int k = 0; k < 4; k++) { r[102 + k] = 99; r[106 + k] = 50; }
    r[117] = 24;
    char n[11]; snprintf(n, sizeof n, "%-10s", name); memcpy(r.data() + 118, n, 10);
    return r;
}

Bytes vmemData() {
    Bytes d;
    for (int v = 0; v < 32; v++) { char n[11]; snprintf(n, sizeof n, "VOICE%02d", v + 1); Bytes r = record(n); d.insert(d.end(), r.begin(), r.end()); }
    return d;
}

Bytes vmemNamed(const char* prefix) {
    Bytes d;
    for (int v = 0; v < 32; v++) { char n[11]; snprintf(n, sizeof n, "%s%02d", prefix, v + 1); Bytes r = record(n); d.insert(d.end(), r.begin(), r.end()); }
    return d;
}

Bytes cat(std::initializer_list<Bytes> parts) { Bytes b; for (auto& p : parts) b.insert(b.end(), p.begin(), p.end()); return b; }

// A DX7II performance record (51 bytes): play mode, voices A and B, dual detune, split point, note shifts, balance,
// total volume and a 20-character name; everything else 0.
Bytes dx7iiPerf(int mode, int a, int b, int ddtn, int split, int nsA, int nsB, int bal, int tvol, const char* name) {
    Bytes r(51, 0);
    r[0] = (uint8_t)mode; r[1] = (uint8_t)a; r[2] = (uint8_t)b; r[6] = (uint8_t)ddtn; r[7] = (uint8_t)split;
    r[13] = (uint8_t)nsA; r[14] = (uint8_t)nsB; r[15] = (uint8_t)bal; r[16] = (uint8_t)tvol;
    char n[21]; snprintf(n, sizeof n, "%-20s", name); memcpy(r.data() + 31, n, 20);
    return r;
}

// A Yamaha "LM  " bulk: F0 43 00 7E <count> <tag> <data> cs F7, the count and the checksum over the tag and data.
Bytes lmBulk(const char* tag, const Bytes& data) {
    Bytes body(tag, tag + 10); body.insert(body.end(), data.begin(), data.end());
    return dxBulk(0x7E, (uint8_t)(body.size() >> 7), (uint8_t)(body.size() & 0x7F), body);
}

// A TX802 PMEM as the factory dumps have it: one F0 43 00 7E, then per performance its count 01 28, the tag,
// its 84 bytes as 168 ASCII hex digits and a checksum over the tag and the digits; one F7 at the end.
Bytes tx802Pmem(const std::vector<Bytes>& records) {
    Bytes m = {0xF0, 0x43, 0x00, 0x7E};
    for (const Bytes& rec : records) {
        Bytes body = {'L', 'M', ' ', ' ', '8', '9', '5', '2', 'P', 'M'};
        for (uint8_t v : rec) { const char* hex = "0123456789ABCDEF"; body.push_back((uint8_t)hex[v >> 4]); body.push_back((uint8_t)hex[v & 15]); }
        m.push_back(0x01); m.push_back(0x28);
        m.insert(m.end(), body.begin(), body.end());
        m.push_back(sum7(body.data(), body.size()));
    }
    m.push_back(0xF7);
    return m;
}

// An FS1R performance's part byte.
int partByte(const fsvr::Item& perf, int part, int at) { return perf.data()[192 + 52 * part + at]; }

// The VCED a DX voice item sends, after its ACED.
const uint8_t* vcedOf(const fsvr::Item& it) {
    size_t f = it.syx.size(); while (f > 0 && it.syx[f - 1] != 0xF0) f--;
    return f && it.syx.size() >= f - 1 + 163 && it.syx[f + 2] == 0x00 && it.syx[f + 4] == 0x1B ? it.syx.data() + f + 5 : nullptr;
}
// The ACED before it, or null.
const uint8_t* acedOf(const fsvr::Item& it) {
    return it.syx.size() > 6 + 49 && it.syx[3] == 0x05 && it.syx[5] == 0x31 ? it.syx.data() + 6 : nullptr;
}

} // namespace

int selftest_patchfiles() {
    using namespace fsvr;
    fails = 0;

    // A sysex stream: an FS1R voice, and a DX VCED with the ACED before it, read as two voices.
    { Bytes vced(155, 0); memcpy(vced.data() + 145, "DX SINGLE ", 10);
      Bytes aced(49, 0); aced[6] = 7;
      const Bytes f = cat({fs1rVoice("Native"), dxBulk(0x05, 0x00, 0x31, aced), dxBulk(0x00, 0x01, 0x1B, vced)});
      Report r; const Bank b = readPatches(f, "Stream", &r);
      ck("a sysex stream reads an FS1R voice and a DX VCED", b.voices.size() == 2 && b.voices[0].name == "Native" && b.voices[0].address == 0x40 &&
                                                            b.voices[0].category == 5 && b.voices[1].name == "DX SINGLE" && !b.voices[1].address);
      ck("a DX VCED carries the ACED before it", b.voices.size() == 2 && acedOf(b.voices[1]) && acedOf(b.voices[1])[6] == 7 && vcedOf(b.voices[1]) &&
                                                 b.voices[1].at == fs1rVoice("Native").size() && b.voices[1].at + b.voices[1].len == f.size());
      ck("a clean stream reports nothing", r.skipped.empty() && r.warnings.empty()); }

    // A DX7 VMEM (20 00) and a DX5's (10 00) both read as 32 voices, each knowing its slot.
    for (uint8_t count : {0x20, 0x10}) {
        const Bytes f = dxBulk(0x09, count, 0x00, vmemData());
        const Bank b = readPatches(f, "Bank");
        ck(count == 0x20 ? "a DX7 VMEM reads as 32 voices" : "a DX5 VMEM (byte count 10 00) reads as 32 voices",
           b.voices.size() == 32 && b.voices[0].name == "VOICE01" && b.voices[31].name == "VOICE32" && b.voices[31].slot == 31 &&
           b.voices[5].at == 0 && b.voices[5].len == f.size() && vcedOf(b.voices[5]) && !acedOf(b.voices[5]));
    }

    // A bad checksum is a warning, not a rejection.
    { Bytes f = dxBulk(0x09, 0x20, 0x00, vmemData()); f[f.size() - 2] ^= 0x11;
      Report r; const Bank b = readPatches(f, "Bad", &r);
      ck("a VMEM with a bad checksum still reads, with a warning", b.voices.size() == 32 && r.warnings.size() == 1 && r.skipped.empty() &&
                                                                 r.summary().find("checksum") != std::string::npos); }

    // Bytes out of the DX7's ranges are clamped after unpacking: an EG rate of 127, algorithm 31 stays, transpose 60.
    { Bytes d = vmemData(); d[0] = 127; d[110] = 31; d[117] = 60;
      Report r; const Bank b = readPatches(dxBulk(0x09, 0x20, 0x00, d), "Wild", &r);
      const uint8_t* v = b.voices.empty() ? nullptr : vcedOf(b.voices[0]);
      ck("out-of-range VMEM bytes are clamped to the DX7's", v && v[0] == 99 && v[134] == 31 && v[144] == 48 && r.warnings.size() == 1); }

    // A VMEM voice renamed or blanked in its slot: the same 4104 bytes, the name and the checksum redone.
    { const Bytes f = dxBulk(0x09, 0x10, 0x00, vmemData());
      const Bank b = readPatches(f, "Edit");
      const Bytes named = vmemRenamed(b.voices[2], "Renamed"), blank = vmemBlanked(b.voices[3]);
      const Bank n = readPatches(named, ""), z = readPatches(blank, "");
      auto same = [&](const Bytes& x, size_t from, size_t to) { return std::equal(x.begin() + (long)from, x.begin() + (long)to, f.begin() + (long)from); };
      const size_t s2 = 6 + 128 * 2, s3 = 6 + 128 * 3;
      ck("a VMEM voice renamed in its slot", named.size() == f.size() && n.voices.size() == 32 && n.voices[2].name == "Renamed" &&
                                               n.voices[1].name == "VOICE02" && same(named, 0, s2 + 118) && same(named, s2 + 128, f.size() - 2) &&
                                               named[f.size() - 2] == sum7(named.data() + 6, 4096));
      ck("a VMEM voice blanked to INIT VOICE in its slot", blank.size() == f.size() && z.voices.size() == 32 && z.voices[3].name == "INIT VOICE" &&
                                                         z.voices[4].name == "VOICE05" && same(blank, 0, s3) && same(blank, s3 + 128, f.size() - 2) &&
                                                         blank[f.size() - 2] == sum7(blank.data() + 6, 4096));
      const uint8_t* init = z.voices.size() == 32 ? vcedOf(z.voices[3]) : nullptr;
      bool c3 = init != nullptr;
      for (int op = 0; c3 && op < 6; op++) c3 = init[op * 21 + 8] == 39;
      ck("INIT VOICE's break point is C3 (39), as the DX7's", c3); }

    // A DX7II bank: bank select, AMEM, VMEM. A slot that changes the conversion gives its voice the ACED, in the
    // layout the FS1R reads (amp mod senses at 6..11, PEG range 0x0C, its velocity switch 0x0E, PEG rate scaling
    // 0x26); a default slot (pitch bend range 2 and nothing else) gives none, as a DX7 bank of the voice would.
    { Bytes amem(32 * 35, 0);
      for (int k = 0; k < 32; k++) amem[k * 35 + 5] = 0x08;
      uint8_t* s = amem.data() + 35 * 1; s[1] = 5 | 3 << 3; s[3] = 7 << 3; s[4] = 2 | 1 << 3; s[24] = 6;   // voice 2
      const Bytes select = {0xF0, 0x43, 0x10, 0x19, 0x4D, 0x01, 0xF7};
      const Bytes f = cat({select, dxBulk(0x06, 0x08, 0x60, amem), dxBulk(0x09, 0x20, 0x00, vmemData())});
      Report r; const Bank b = readPatches(f, "DX7II", &r);
      const uint8_t* a = b.voices.size() == 32 ? acedOf(b.voices[1]) : nullptr;
      ck("an AMEM slot becomes its VMEM voice's ACED", a && a[6] == 5 && a[7] == 3 && a[10] == 0 && a[11] == 7 && a[0x0C] == 2 && a[0x0E] == 1 && a[0x26] == 6 &&
                                                       vcedOf(b.voices[1]) && b.voices[1].slot == 1 && r.skipped.empty() && r.warnings.empty());
      ck("a default AMEM slot gives no ACED", b.voices.size() == 32 && !acedOf(b.voices[0]) && b.voices[0].syx == readPatches(dxBulk(0x09, 0x20, 0x00, vmemData()), "").voices[0].syx);

      // The voice loads with its supplement: the ACED's 5 on DX operator 6 is the FS1R voice's amp mod sense.
      auto S = std::make_unique<Synth>();
      const Bytes& syx = b.voices.size() == 32 ? b.voices[1].syx : f;
      load_sysex(*S, nullptr, syx.data(), syx.size(), 0, 0);
      int ams = -1;
      for (int o = 0; o < 8; o++) { const uint8_t* p = S->perf.part[0].voice.raw + 112 + o * 62; if ((p[5] & 0x3F) == 0) ams = p[33] >> 4; }
      ck("a DX7II voice loads with its AMEM's amp mod sense and PEG range", ams == 5 && S->perf.part[0].voice.raw[0x3B] == 2);

      // Files read together are one stream: the AMEM in one pairs with the VMEM in the next.
      Pending p; Report r2;
      const Bank first = readPatches(cat({select, dxBulk(0x06, 0x08, 0x60, amem)}), "AMEM", &r2, &p);
      const Bank second = readPatches(dxBulk(0x09, 0x20, 0x00, vmemData()), "VMEM", &r2, &p);
      ck("an AMEM in one file pairs with the VMEM in the next", first.empty() && second.voices.size() == 32 && acedOf(second.voices[1]) &&
                                                                acedOf(second.voices[1])[6] == 5 && !acedOf(second.voices[2]) && r2.skipped.empty());

      // Read on its own, an AMEM with no bank after it is reported, not dropped without a word.
      Report r3; const Bank alone = readPatches(cat({select, dxBulk(0x06, 0x08, 0x60, amem)}), "AMEM", &r3);
      ck("an AMEM with no bank after it is reported", alone.empty() && r3.skipped.size() == 1 && r3.summary().find("AMEM") != std::string::npos);

      // A voice blanked in its slot takes its AMEM slot back to the default with it, so it loads as a plain INIT VOICE.
      const Item& v2 = b.voices.size() == 32 ? b.voices[1] : b.voices.at(0);
      const Bytes amemMsg = dxBulk(0x06, 0x08, 0x60, amem);
      ck("a VMEM voice knows its AMEM message", v2.amem && *v2.amem == amemMsg && v2.amemAt == select.size());
      if (v2.amem) {
          Bytes edited = f;
          const Bytes vm = vmemBlanked(v2), am = amemBlanked(v2);
          std::copy(vm.begin(), vm.end(), edited.begin() + (long)v2.at);
          std::copy(am.begin(), am.end(), edited.begin() + (long)v2.amemAt);
          Report r4; const Bank z = readPatches(edited, "", &r4);
          ck("a VMEM voice blanked with its AMEM slot loads as a plain INIT VOICE", am.size() == amemMsg.size() && z.voices.size() == 32 &&
                                                                                    z.voices[1].name == "INIT VOICE" && !acedOf(z.voices[1]) &&
                                                                                    z.voices[1].syx == readPatches(vmemBlanked(v2), "").voices[1].syx &&
                                                                                    acedOf(z.voices[0]) == nullptr && r4.warnings.empty());
      } }

    // A bank with no frame: 4096 bytes, or with a librarian's header before it, or two back to back. It comes back
    // rewrapped as the VMEM the DX7 sends, and the items count in that.
    { const Bytes d = vmemData(), framed = dxBulk(0x09, 0x20, 0x00, d);
      const Bank bare = readPatches(d, "Bare");
      ck("a headerless 4096-byte bank reads as a VMEM", bare.voices.size() == 32 && bare.voices[9].name == "VOICE10" && bare.rewrapped == framed &&
                                                       bare.voices[9].at == 0 && bare.voices[9].len == framed.size());
      Bytes header(128, 0xD2); header[0] = 'L';
      ck("a headerless bank after a 128-byte header reads", readPatches(cat({header, d}), "").voices.size() == 32);
      ck("two headerless banks back to back read as 64 voices", readPatches(cat({d, d}), "").voices.size() == 64);
      Bytes noise(4096, 0x00);
      Report r; const Bank none = readPatches(noise, "Noise", &r);
      ck("4096 bytes with no names in place are not a bank", none.empty() && r.skipped.size() == 1); }

    // Damaged files, as MDX_Tool repairs them: a bulk that lost its F7 reads to the next message or the end of
    // the file, a DX bulk one byte short lost its checksum, and both are mended in the stream the library keeps.
    // A checksum with its top bit set is the right one. A bare VCED is clamped as a bank's voices are, a name
    // keeps to ASCII, and a headerless bank with top bits set has them cleared.
    { const Bytes d = vmemNamed("MEND"), whole = dxBulk(0x09, 0x20, 0x00, d);
      Bytes unsummed(whole.begin(), whole.end() - 2);   // no checksum, no F7
      Report r; const Bank b = readPatches(unsummed, "Short", &r);
      ck("a VMEM with no checksum and no F7 reads, mended", b.voices.size() == 32 && b.voices[0].name == "MEND01" && b.rewrapped == whole &&
                                                            r.summary().find("no F7") != std::string::npos && r.summary().find("no checksum") != std::string::npos);
      Bytes vced(155, 0); memcpy(vced.data() + 145, "OPEN VCED ", 10);
      Bytes open = dxBulk(0x00, 0x01, 0x1B, vced);
      open.pop_back();   // its F7 lost, the bank right after it
      Report r2; const Bank two = readPatches(cat({open, whole}), "", &r2);
      ck("a bulk with no F7 before the next one does not swallow it", two.voices.size() == 33 && two.voices[0].name == "OPEN VCED" && two.voices[1].name == "MEND01");
      Report r3; const Bank fine = readPatches(whole, "", &r3);
      ck("a whole bulk is not mended", fine.rewrapped.empty() && r3.warnings.empty());

      Bytes wild(155, 0); memcpy(wild.data() + 145, "WILD\x01VOICE", 10); wild[134] = 40; wild[0] = 120;
      Report r4; const Bank w = readPatches(dxBulk(0x00, 0x01, 0x1B, wild), "", &r4);
      const uint8_t* v = w.voices.empty() ? nullptr : vcedOf(w.voices[0]);
      ck("a bare VCED out of range is clamped, its name kept to ASCII", v && v[134] == 31 && v[0] == 99 && v[149] == ' ' && w.voices[0].name == "WILD VOICE" &&
                                                                       r4.summary().find("clamped") != std::string::npos);

      Bytes topSum = whole;
      topSum[6 + 4096] |= 0x80;
      Bytes wrongSum = whole;
      wrongSum[6 + 4096] ^= 0x01;
      Report r5, r5b; readPatches(topSum, "", &r5); readPatches(wrongSum, "", &r5b);
      ck("a checksum with its top bit set is the right one; a wrong one is reported", r5.warnings.empty() &&
                                                                                      r5b.summary().find("checksum is") != std::string::npos);

      Bytes high = d;
      high[0] |= 0x80; high[118] |= 0x80;   // a rate, and a name's first letter
      Report r6; const Bank h = readPatches(high, "", &r6);
      ck("a headerless bank with top bits set reads, cleared", h.voices.size() == 32 && h.voices[0].name == "MEND01" && h.rewrapped == whole &&
                                                               r6.summary().find("2 bytes had their top bit set") != std::string::npos); }

    // Bare VCEDs: one of 155 bytes, one of 156 with the operator on/off byte (the FS1R's .Dx7Voice), three of 155.
    { Bytes v(155, 0); memcpy(v.data() + 145, "BARE VOICE", 10); v[0] = 99;
      const Bank one = readPatches(v, "");
      ck("a bare 155-byte VCED reads as a DX voice", one.voices.size() == 1 && one.voices[0].name == "BARE VOICE" && vcedOf(one.voices[0]) &&
                                                     vcedOf(one.voices[0])[0] == 99 && one.rewrapped == dxBulk(0x00, 0x01, 0x1B, v));
      Bytes w = v; w.push_back(0x3F);
      ck("a 156-byte .Dx7Voice reads as a DX voice", readPatches(w, "").voices.size() == 1);
      ck("three bare VCEDs read as three voices", readPatches(cat({v, v, v}), "").voices.size() == 3); }

    // A MIDI file's sysex events, one of them split over an F0 and an F7 packet, around a note and a tempo.
    { Bytes vced(155, 0); memcpy(vced.data() + 145, "IN A SMF  ", 10);
      const Bytes single = dxBulk(0x00, 0x01, 0x1B, vced), bank = dxBulk(0x09, 0x20, 0x00, vmemData());
      auto vlq = [](size_t n) { Bytes out; out.push_back((uint8_t)(n & 0x7F)); while (n >>= 7) out.insert(out.begin(), (uint8_t)(0x80 | (n & 0x7F))); return out; };
      Bytes trk = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0x90, 60, 100, 0x10, 60, 0};   // tempo, note on, running status off
      trk.push_back(0x00); trk.push_back(0xF0); { Bytes n = vlq(single.size() - 1); trk.insert(trk.end(), n.begin(), n.end()); }
      trk.insert(trk.end(), single.begin() + 1, single.end());
      const size_t half = 2000;
      trk.push_back(0x00); trk.push_back(0xF0); { Bytes n = vlq(half - 1); trk.insert(trk.end(), n.begin(), n.end()); }
      trk.insert(trk.end(), bank.begin() + 1, bank.begin() + (long)half);
      trk.push_back(0x05); trk.push_back(0xF7); { Bytes n = vlq(bank.size() - half); trk.insert(trk.end(), n.begin(), n.end()); }
      trk.insert(trk.end(), bank.begin() + (long)half, bank.end());
      const Bytes end = {0x00, 0xFF, 0x2F, 0x00}; trk.insert(trk.end(), end.begin(), end.end());
      Bytes smf = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k'};
      for (int k = 3; k >= 0; k--) smf.push_back((uint8_t)(trk.size() >> (8 * k)));
      smf.insert(smf.end(), trk.begin(), trk.end());
      Report r; const Bank b = readPatches(smf, "Song", &r);
      ck("a MIDI file's sysex events read in order", b.voices.size() == 33 && b.voices[0].name == "IN A SMF" && b.voices[32].name == "VOICE32" &&
                                                     b.rewrapped == cat({single, bank}) && r.skipped.empty()); }

    // A DX7II dump: two VMEMs behind their bank selects, then a PMEM naming voices by number, 0-31 the first
    // bank and 32-63 the second. Dual, split and single go onto parts 1 and 2.
    { Bytes pmem;
      for (const Bytes& rec : {dx7iiPerf(1, 1, 33, 3, 60, 36, 24, 75, 99, "  Dual Test"), dx7iiPerf(2, 0, 63, 0, 60, 24, 12, 50, 99, "Split Test"),
                               dx7iiPerf(0, 5, 0, 0, 60, 24, 24, 50, 66, "Single Test")}) pmem.insert(pmem.end(), rec.begin(), rec.end());
      for (int k = 3; k < 32; k++) { Bytes rec = dx7iiPerf(0, 2, 0, 0, 60, 24, 24, 50, 99, "Filler"); pmem.insert(pmem.end(), rec.begin(), rec.end()); }
      const Bytes sel0 = {0xF0, 0x43, 0x10, 0x19, 0x4D, 0x00, 0xF7}, sel1 = {0xF0, 0x43, 0x10, 0x19, 0x4D, 0x01, 0xF7};
      const Bytes f = cat({sel0, dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKA")), sel1, dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKB")), lmBulk("LM  8973PM", pmem)});
      Report r; const Bank b = readPatches(f, "DX7II", &r);
      auto voiceOf = [&](const Item& perf, int p) { return perf.partVoice[p] >= 0 ? b.voices[(size_t)perf.partVoice[p]].name : std::string("-"); };
      const bool ok = b.perfs.size() == 32 && b.voices.size() == 64;
      ck("a DX7II PMEM reads as 32 FS1R performances", ok && b.perfs[0].name == "Dual Test" && b.perfs[0].address == 0x10 && b.perfs[0].syx.size() == 411 &&
                                                         r.skipped.empty() && r.warnings.empty());
      if (ok) {
          const Item &dual = b.perfs[0], &split = b.perfs[1], &single = b.perfs[2];
          ck("a DX7II dual performance layers its voices on parts 1 and 2", voiceOf(dual, 0) == "BANKA02" && voiceOf(dual, 1) == "BANKB02" &&
                                                                           partByte(dual, 0, 1) == 1 && partByte(dual, 1, 1) == 1 && partByte(dual, 2, 1) == 0 &&
                                                                           partByte(dual, 0, 4) == 0x10 && partByte(dual, 1, 4) == 0x10 && partByte(dual, 2, 4) == 0x7F);
          ck("balance becomes part volume, dual detune part detune, note shifts each part's", partByte(dual, 0, 0x0B) == 64 && partByte(dual, 1, 0x0B) == 127 &&
                                                                                               partByte(dual, 0, 9) == 67 && partByte(dual, 1, 9) == 61 &&
                                                                                               partByte(dual, 0, 8) == 36 && partByte(dual, 1, 8) == 24);
          ck("a DX7II split puts voice A below the split point and B from it", voiceOf(split, 0) == "BANKA01" && voiceOf(split, 1) == "BANKB32" &&
                                                                              partByte(split, 0, 0x0F) == 0 && partByte(split, 0, 0x10) == 59 &&
                                                                              partByte(split, 1, 0x0F) == 60 && partByte(split, 1, 0x10) == 127 && partByte(split, 1, 8) == 12);
          ck("a DX7II single plays part 1 alone at its total volume", voiceOf(single, 0) == "BANKA06" && single.partVoice[1] < 0 && partByte(single, 1, 1) == 0 &&
                                                                     partByte(single, 0, 0x0B) == 85 && partByte(single, 0, 0x10) == 127);

          // It loads as the FS1R would take it: the performance bulk, then each part's voice into its part.
          auto S = std::make_unique<Synth>();
          load_sysex(*S, nullptr, dual.syx.data(), dual.syx.size(), 0, 0);
          for (int p = 0; p < 4; p++) if (dual.partVoice[p] >= 0) { const Bytes& v = b.voices[(size_t)dual.partVoice[p]].syx; load_sysex(*S, nullptr, v.data(), v.size(), 0, p); }
          ck("a DX7II performance loads with its voices in its parts", S->perf.part[1].p[9] == 61 && !memcmp(S->perf.part[1].voice.raw, "BANKB02", 7) &&
                                                                       !memcmp(S->perf.part[0].voice.raw, "BANKA02", 7) && !memcmp(S->perf.name, "Dual Test", 9));

          // Written out as the performance bulk and its part voices, as the library keeps an imported
          // performance, it reads back with each DX voice in the next part that plays one.
          const Bytes kept = cat({split.syx, b.voices[(size_t)split.partVoice[0]].syx, b.voices[(size_t)split.partVoice[1]].syx});
          Report r2; const Bank back = readPatches(kept, "Kept", &r2);
          ck("an FS1R performance followed by DX voices takes them into its parts", back.perfs.size() == 1 && back.voices.size() == 2 &&
                                                                                   back.perfs[0].partVoice[0] == 0 && back.perfs[0].partVoice[1] == 1 &&
                                                                                   back.perfs[0].partVoice[2] < 0 && back.voices[1].name == "BANKB32" && r2.skipped.empty());
      }

      // The factory's cartridge dump: its banks behind bank selects 0 and 1 like the internal ones, and a PMEM naming
      // cartridge voices (64-127) as well as internal ones. Its banks are the cartridge's, so read after the
      // internal dump, a performance plays one voice of each.
      Bytes cartPmem = dx7iiPerf(1, 1, 100, 0, 60, 24, 24, 50, 99, "Both");
      for (int k = 1; k < 32; k++) { Bytes rec = dx7iiPerf(0, 64, 0, 0, 60, 24, 24, 50, 99, "Cart"); cartPmem.insert(cartPmem.end(), rec.begin(), rec.end()); }
      const Bytes cart = cat({sel0, dxBulk(0x09, 0x20, 0x00, vmemNamed("CARTA")), sel1, dxBulk(0x09, 0x20, 0x00, vmemNamed("CARTB")), lmBulk("LM  8973PM", cartPmem)});
      Pending p; Report r3;
      readPatches(f, "INTERNAL", &r3, &p);
      const Bank c = readPatches(cart, "CART", &r3, &p);
      const bool cok = c.perfs.size() == 32;
      ck("a DX7II dump whose performances name cartridge voices has the cartridge's banks", cok &&
         c.voices[(size_t)c.perfs[0].partVoice[0]].name == "BANKA02" && c.voices[(size_t)c.perfs[0].partVoice[1]].name == "CARTB05" &&
         c.voices[(size_t)c.perfs[1].partVoice[0]].name == "CARTA01");
      Report r4; const Bank alone = readPatches(cart, "CART", &r4);
      ck("read alone, its internal voices are not there", alone.perfs.size() == 32 && alone.perfs[0].partVoice[0] < 0 &&
                                                          alone.voices[(size_t)alone.perfs[0].partVoice[1]].name == "CARTB05" && r4.warnings.size() == 1);

      // The two kept one after the other in one file, as the library keeps a folder of DX files, read as they did
      // apart: the second dump's banks land on the first's numbers, so the first's performances take their voices
      // before them, and the second's find the first's banks still.
      const Bank both = readPatches(cat({f, cart}), "BOTH");
      auto nameOf = [&](size_t perf, int part) { return both.perfs[perf].partVoice[part] >= 0 ? both.voices[(size_t)both.perfs[perf].partVoice[part]].name : std::string("-"); };
      ck("two DX7II dumps in one file read as they do apart", both.perfs.size() == 64 && both.voices.size() == 128 && nameOf(0, 0) == "BANKA02" &&
                                                              nameOf(0, 1) == "BANKB02" && nameOf(32, 0) == "BANKA02" && nameOf(32, 1) == "CARTB05" &&
                                                              nameOf(33, 0) == "CARTA01"); }

    // A PCED, the DX7II's edit buffer, after one bank: voice B (40) is not in it, so its part stays off and the
    // report says so. With no bank at all the performance has nothing to play and is skipped.
    { const Bytes pced = lmBulk("LM  8973PE", dx7iiPerf(1, 3, 40, 0, 60, 24, 24, 50, 99, "Edit Buffer"));
      Report r; const Bank b = readPatches(cat({dxBulk(0x09, 0x20, 0x00, vmemNamed("ONLY")), pced}), "PCED", &r);
      ck("a DX7II PCED reads as one performance", b.perfs.size() == 1 && b.perfs[0].name == "Edit Buffer" && b.perfs[0].partVoice[0] == 3 &&
                                                   b.perfs[0].partVoice[1] < 0 && partByte(b.perfs[0], 1, 1) == 0 && r.skipped.empty() &&
                                                   r.warnings.size() == 1 && r.summary().find("left off") != std::string::npos);
      Report r2; const Bank none = readPatches(pced, "Alone", &r2);
      ck("a DX7II performance with none of its voices is skipped, and reported", none.empty() && r2.skipped.size() == 1 &&
                                                                                 r2.summary().find("voices are not in the files read") != std::string::npos); }

    // Ours, on the parts a DX performance plays: a voice at level 0 is left off, one channel shared by all parts
    // becomes the performance's, and parts all at the centre are spread, further for one voice detuned against
    // itself. Dual detune is the DX7II's in dual only; its pan modes 1-3 keep voice A left and B right.
    { const Bytes bank = dxBulk(0x09, 0x20, 0x00, vmemNamed("V"));
      auto pced = [&](const Bytes& rec, Report& r) { return readPatches(cat({bank, lmBulk("LM  8973PE", rec)}), "", &r); };
      auto nameOf = [](const Bank& b, int p) { return b.perfs.size() == 1 && b.perfs[0].partVoice[p] >= 0 ? b.voices[(size_t)b.perfs[0].partVoice[p]].name : std::string("-"); };
      Report r1; const Bank same = pced(dx7iiPerf(1, 1, 1, 3, 60, 24, 24, 50, 99, "Same"), r1);
      ck("one voice detuned against itself spreads wide, the flatter part left", same.perfs.size() == 1 && partByte(same.perfs[0], 0, 0x0E) == 96 &&
                                                                                 partByte(same.perfs[0], 1, 0x0E) == 32 && partByte(same.perfs[0], 0, 4) == 0x10);
      Report r2; const Bank split = pced(dx7iiPerf(2, 1, 2, 3, 60, 24, 24, 50, 99, "Split"), r2);
      ck("a DX7II split takes no dual detune, and its two voices spread a little", split.perfs.size() == 1 && partByte(split.perfs[0], 0, 9) == 64 &&
                                                                                   partByte(split.perfs[0], 1, 9) == 64 && partByte(split.perfs[0], 0, 0x0E) == 48 &&
                                                                                   partByte(split.perfs[0], 1, 0x0E) == 80);
      Bytes apart = dx7iiPerf(1, 1, 2, 0, 60, 24, 24, 50, 99, "Apart");
      apart[20] = 2;
      Report r3; const Bank sides = pced(apart, r3);
      ck("a DX7II pan mode puts voice A left and B right", sides.perfs.size() == 1 && partByte(sides.perfs[0], 0, 0x0E) == 16 && partByte(sides.perfs[0], 1, 0x0E) == 112);
      Report r4; const Bank onlyA = pced(dx7iiPerf(1, 1, 2, 0, 60, 24, 24, 0, 99, "Only A"), r4);
      ck("a DX7II voice balanced to 0 leaves its part off", nameOf(onlyA, 0) == "V02" && nameOf(onlyA, 1) == "-" && partByte(onlyA.perfs[0], 1, 1) == 0 &&
                                                            partByte(onlyA.perfs[0], 0, 0x0E) == 64 && r4.warnings.empty());
      Report r5; const Bank onlyB = pced(dx7iiPerf(2, 1, 2, 0, 60, 24, 30, 100, 99, "Only B"), r5);
      ck("when voice A is the silent one, B plays on part 1", nameOf(onlyB, 0) == "V03" && nameOf(onlyB, 1) == "-" && partByte(onlyB.perfs[0], 0, 8) == 30 &&
                                                              partByte(onlyB.perfs[0], 0, 0x0F) == 60);
      Report r6; const Bank none = pced(dx7iiPerf(0, 1, 0, 0, 60, 24, 24, 50, 0, "Silent"), r6);
      ck("a performance at total volume 0 is skipped, and says why", none.perfs.empty() && r6.summary().find("all at level 0") != std::string::npos);

      // TX802: TGs leading, each with its channel, voice, volume and detune; the rest have no output.
      auto tx = [](std::initializer_list<std::array<int, 4>> tgs, const char* name) {
          Bytes rec(84, 0);
          for (int t = 0; t < 8; t++) { rec[t] = (uint8_t)(t << 5); rec[32 + t] = 0x38; rec[48 + t] = 127; rec[56 + t] = 0x18; }
          int t = 0;
          for (const auto& g : tgs) { rec[t] |= (uint8_t)g[0]; rec[8 + t] = (uint8_t)g[1]; rec[24 + t] = (uint8_t)g[2]; rec[32 + t] = (uint8_t)((7 + g[3]) << 3 | 3); t++; }
          char n[21]; snprintf(n, sizeof n, "%-20s", name); memcpy(rec.data() + 64, n, 20);
          return rec; };
      Report r7;
      const Bank t = readPatches(cat({bank, tx802Pmem({tx({{0, 1, 0, 0}, {0, 4, 90, 0}}, "Quiet TG1"), tx({{0, 1, 0, 0}}, "All Quiet"),
                                                       tx({{0, 1, 90, 0}, {1, 2, 90, 0}}, "Two Channels"), tx({{0, 1, 90, 3}, {0, 1, 90, -3}}, "Detuned")})}), "", &r7);
      const bool tok = t.perfs.size() == 3;
      ck("a TX802 TG at volume 0 takes no part, and a performance of none is skipped", tok && t.perfs[0].partVoice[0] >= 0 &&
         t.voices[(size_t)t.perfs[0].partVoice[0]].name == "V05" && t.perfs[0].partVoice[1] < 0 && r7.summary().find("all at level 0") != std::string::npos);
      ck("TGs on one channel receive on the performance's, TGs on two keep theirs", tok && partByte(t.perfs[0], 0, 4) == 0x10 &&
         partByte(t.perfs[1], 0, 4) == 0 && partByte(t.perfs[1], 1, 4) == 1 && partByte(t.perfs[1], 0, 0x0E) == 48 && partByte(t.perfs[1], 1, 0x0E) == 80);
      ck("a TX802 voice doubled and detuned spreads wide", tok && partByte(t.perfs[2], 0, 0x0E) == 96 && partByte(t.perfs[2], 1, 0x0E) == 32);
      // TGs all on output I are spread, not all left, as Ensemble 2 of the factory set has them; a TG alone there is centred.
      Bytes left = tx({{0, 1, 90, 0}, {0, 2, 90, 0}}, "All Left"), alone = tx({{0, 1, 90, 0}}, "Alone Left");
      for (Bytes* r : {&left, &alone}) for (int k = 0; k < 8; k++) (*r)[32 + k] = (uint8_t)(((*r)[32 + k] & ~3) | 1);
      const Bank l = readPatches(cat({bank, tx802Pmem({left, alone})}), "");
      ck("TGs all on one output spread, a TG alone is centred", l.perfs.size() == 2 && partByte(l.perfs[0], 0, 0x0E) == 48 && partByte(l.perfs[0], 1, 0x0E) == 80 &&
                                                                partByte(l.perfs[1], 0, 0x0E) == 64); }

    // A TX802 PMEM: eight tone generators per performance in eight rows of eight bytes (channel and link, voice,
    // -, volume, detune and output, note limits low and high, note shift), then the name. A TG linked to an
    // earlier one (the link, the top three bits of its first byte, names that TG) adds notes to it; the first
    // four that lead and have an output become parts, and the report names the rest. Two banks, no bank select:
    // voices 1-32, then 33-64.
    { Bytes layer(84, 0);
      const uint8_t rows[8][8] = {{0x00, 0x00, 0x41, 0x60, 0x90, 0xA0, 0xC0, 0xE0},    // TG2 linked to TG1; TG3 on channel 2; TG5 omni
                                  {0x82, 0x00, 0x85, 0xA0, 0x81, 0x83, 0x84, 0x00},    // voices 3, -, 6, 33, 2, 4, 5
                                  {0}, {99, 90, 49, 90, 90, 90, 90, 90},
                                  {0x3B, 0x3B, 0x49, 0x2A, 0x3B, 0x3B, 0x3B, 0x38},    // TG3 detune +2 on output I, TG4 -2 on II, TG8 no output
                                  {0, 0, 60, 0, 0, 0, 0, 0}, {59, 127, 127, 127, 127, 127, 127, 127},
                                  {0x18, 0x18, 0x0C, 0x58, 0x18, 0x18, 0x18, 0x18}};   // TG3 an octave down
      for (int g = 0; g < 8; g++) memcpy(layer.data() + 8 * g, rows[g], 8);
      memcpy(layer.data() + 64, "TX Layer            ", 20);
      Bytes plain(84, 0);
      for (int t = 0; t < 8; t++) { plain[24 + t] = 90; plain[32 + t] = 0x3B; plain[48 + t] = 127; plain[56 + t] = 0x18; }
      plain[8] = 0x80;
      memcpy(plain.data() + 64, "Plain               ", 20);
      const Bytes f = cat({dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKA")), dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKB")), tx802Pmem({layer, plain})});
      Report r; const Bank b = readPatches(f, "TX802", &r);
      const bool ok = b.perfs.size() == 2 && b.voices.size() == 64;
      auto voiceOf = [&](const Item& perf, int p) { return perf.partVoice[p] >= 0 ? b.voices[(size_t)perf.partVoice[p]].name : std::string("-"); };
      ck("a TX802 PMEM reads as FS1R performances", ok && b.perfs[0].name == "TX Layer" && b.perfs[1].name == "Plain" && r.skipped.empty());
      if (ok) {
          const Item& t = b.perfs[0];
          ck("a TX802 performance's first four leading TGs become the parts", voiceOf(t, 0) == "BANKA03" && voiceOf(t, 1) == "BANKA06" &&
                                                                             voiceOf(t, 2) == "BANKB01" && voiceOf(t, 3) == "BANKA02");
          ck("a TG's channel, volume, detune, output, limits and shift go onto its part", partByte(t, 0, 4) == 0 && partByte(t, 1, 4) == 1 && partByte(t, 3, 4) == 0x10 &&
             partByte(t, 0, 0x0B) == 127 && partByte(t, 1, 0x0B) == 63 && partByte(t, 1, 9) == 66 && partByte(t, 2, 9) == 62 && partByte(t, 0, 9) == 64 &&
             partByte(t, 0, 0x0E) == 64 && partByte(t, 1, 0x0E) == 16 && partByte(t, 2, 0x0E) == 112 && partByte(t, 0, 0x10) == 59 &&
             partByte(t, 1, 0x0F) == 60 && partByte(t, 1, 8) == 12 && partByte(t, 2, 8) == 24);
          ck("the report names the TGs a TX802 performance drops", r.warnings.size() == 1 && r.summary().find("TG6, TG7") != std::string::npos &&
                                                                   r.summary().find("TX Layer") != std::string::npos);
          ck("a TX802 performance of one TG plays one part", voiceOf(b.perfs[1], 0) == "BANKA01" && b.perfs[1].partVoice[1] < 0 && partByte(b.perfs[1], 1, 1) == 0);
      }

      // The factory set comes as five files, the four banks and then the performances: read together, the
      // performances find their voices in the files before.
      Pending p; Report r2;
      readPatches(dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKA")), "A1", &r2, &p);
      readPatches(dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKB")), "A2", &r2, &p);
      const Bank perfs = readPatches(tx802Pmem({layer}), "P", &r2, &p);
      ck("a TX802 PMEM finds its voices in the banks read before it", perfs.perfs.size() == 1 && perfs.voices.size() == 4 &&
                                                                      perfs.voices[(size_t)perfs.perfs[0].partVoice[2]].name == "BANKB01" &&
                                                                      perfs.voices[0].at == 0 && perfs.voices[0].len == 0 && perfs.voices[0].slot < 0);
      // A file of performances with no bank of its own says so, to be read after the files that have them.
      ck("a PMEM with no bank names voices of other files", perfs.namesVoices && readPatches(tx802Pmem({layer}), "").namesVoices &&
                                                            !b.namesVoices && !readPatches(dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKA")), "").namesVoices);

      // A file of DX voices and no performance gives each voice a performance playing it alone on part 1, on Pfm at
      // the centre; a file with performances, even ones all skipped, gets none.
      { const Bank v = readPatches(dxBulk(0x09, 0x20, 0x00, vmemNamed("SOLO")), "");
        const bool vok = v.perfs.size() == 32 && v.voices.size() == 32;
        ck("a voice bank alone gives one performance per voice", vok && v.perfs[4].name == "SOLO05" && v.perfs[4].fromDx && v.perfs[4].ofVoice && v.perfs[4].partVoice[0] == 4 &&
                                                                  v.perfs[4].partVoice[1] < 0 && partByte(v.perfs[4], 0, 1) == 1 && partByte(v.perfs[4], 0, 4) == 0x10 &&
                                                                  partByte(v.perfs[4], 0, 0x0E) == 64 && partByte(v.perfs[4], 1, 1) == 0 && partByte(v.perfs[4], 3, 1) == 0);
        // Its effects: Hall1 at return 64, and the chorus insertion on the part that plays, a little drier than D=W.
        const uint8_t* c = vok ? v.perfs[4].data() : nullptr;
        ck("a DX performance gets a hall and a chorus insertion on its parts", c && c[168] == 1 && c[170] == 64 && c[175] == 1 && c[179] == 127 &&
                                                                            c[136 + 19] == 48 && partByte(v.perfs[4], 0, 0x14) == 1 && partByte(v.perfs[4], 1, 0x14) == 0);
        Bytes vced(155, 0); memcpy(vced.data() + 145, "ONE VOICE ", 10);
        const Bank one = readPatches(dxBulk(0x00, 0x01, 0x1B, vced), "");
        ck("a bare VCED gives one performance", one.perfs.size() == 1 && one.perfs[0].name == "ONE VOICE" && one.perfs[0].partVoice[0] == 0);
        ck("a voice bank with performances gets only those", b.perfs.size() == 2); }   // and one whose are all skipped none: "total volume 0" above

      // The banks a TX802 PMEM took as its presets are recorded. A PMEM whose files lack them plays the library's,
      // given in Pending::presets, and keeps those banks to go before it: read so, behind a unit's own banks sent
      // with a bank select, it binds the same.
      ck("a bank whose TX802 performances name presets records them", b.tx802Presets.size() == 128 && b.tx802Presets[2] >= 0 &&
                                                                     b.voices[(size_t)b.tx802Presets[2]].name == "BANKA03" && b.tx802Presets[64] < 0);
      if (b.tx802Presets.size() == 128) {
          Pending q; Report r5;
          for (int k = 0; k < 64; k++) q.presets.push_back({b.voices[(size_t)b.tx802Presets[(size_t)k]], false, "TX802"});
          const Bytes select = {0xF0, 0x43, 0x10, 0x19, 0x4D, 0x00, 0xF7}, own = cat({select, dxBulk(0x09, 0x20, 0x00, vmemNamed("INT"))});
          const Bank lib = readPatches(cat({own, tx802Pmem({layer})}), "Fmori", &r5, &q);
          auto partOf = [](const Bank& k, int p) { return !k.perfs.empty() && k.perfs[0].partVoice[p] >= 0 ? k.voices[(size_t)k.perfs[0].partVoice[p]].name : std::string("-"); };
          ck("a TX802 PMEM plays the library's presets its files lack", lib.perfs.size() == 1 && partOf(lib, 0) == "BANKA03" && partOf(lib, 2) == "BANKB01" &&
                                                                         lib.libraryPresets.size() == 2 * (6 + 4096 + 2));
          const Bank kept = readPatches(cat({lib.libraryPresets, own, tx802Pmem({layer})}), "Kept");
          ck("kept before its own banks, the library's presets bind again", kept.perfs.size() == 1 && partOf(kept, 0) == "BANKA03" && partOf(kept, 2) == "BANKB01" &&
                                                                            kept.tx802Presets.size() == 128 && kept.tx802Presets[2] >= 0);
          Report r6; Pending none;
          const Bank lost = readPatches(tx802Pmem({layer}), "P", &r6, &none);
          ck("a TX802 PMEM with no presets anywhere says to import them", lost.perfs.empty() && r6.summary().find("import presets A and B") != std::string::npos);
      }

      // A blank name is an empty slot, and is not read.
      Bytes blank = plain;
      memset(blank.data() + 64, ' ', 20);
      ck("a blank-named TX802 performance is an empty slot", readPatches(cat({dxBulk(0x09, 0x20, 0x00, vmemNamed("BANKA")), tx802Pmem({blank, plain})}), "").perfs.size() == 1); }

    // Banks with no bank select number on only within one folder: a file from another starts again at 1, so a
    // performance there never plays a voice of the first folder's. A performance that plays voices of another
    // file read with it says which.
    { const Bytes single0 = lmBulk("LM  8973PE", dx7iiPerf(1, 0, 32, 0, 60, 24, 24, 50, 99, "Two Banks"));
      Pending p; Report r;
      p.folder = "X"; readPatches(dxBulk(0x09, 0x20, 0x00, vmemNamed("XBANK")), "X.syx", &r, &p);
      p.folder = "Y"; const Bank y = readPatches(cat({dxBulk(0x09, 0x20, 0x00, vmemNamed("YBANK")), single0}), "Y.syx", &r, &p);
      ck("a bank in another folder numbers from 1 again", y.perfs.size() == 1 && y.perfs[0].partVoice[0] >= 0 &&
                                                          y.voices[(size_t)y.perfs[0].partVoice[0]].name == "YBANK01" && y.perfs[0].partVoice[1] < 0);
      Pending q; Report r2;
      readPatches(dxBulk(0x09, 0x20, 0x00, vmemNamed("XBANK")), "X.syx", &r2, &q);
      const Bank z = readPatches(single0, "P.syx", &r2, &q);
      ck("a performance playing another file's voices names the file", z.perfs.size() == 1 && z.perfs[0].partVoice[0] >= 0 &&
                                                                       z.voices[(size_t)z.perfs[0].partVoice[0]].name == "XBANK01" &&
                                                                       r2.summary().find("voices from X.syx") != std::string::npos); }

    // A TX802 voice number counts I01-I64, C01-C64, then the ROM's presets A and B (128-255). The factory set's
    // preset banks come as files with no bank select, so a preset voice is found only in banks sent so, and those
    // are then the presets, not the internal voices. A voice the banks read cannot hold leaves its part off.
    { Bytes two(84, 0);
      two[0] = 0x00; two[1] = 0x20;                 // TG1 and TG2 both lead
      two[8] = 0x82; two[9] = 0x02;                 // preset A03, internal I03
      for (int t = 0; t < 8; t++) { two[24 + t] = 90; two[32 + t] = t < 2 ? 0x3B : 0x38; two[48 + t] = 127; two[56 + t] = 0x18; }
      memcpy(two.data() + 64, "Two Kinds           ", 20);
      const Bytes sel0 = {0xF0, 0x43, 0x10, 0x19, 0x4D, 0x00, 0xF7};
      auto voiceOf = [](const Bank& b, int p) { return b.perfs.size() == 1 && b.perfs[0].partVoice[p] >= 0 ? b.voices[(size_t)b.perfs[0].partVoice[p]].name : std::string("-"); };
      Report r; const Bank internal = readPatches(cat({sel0, dxBulk(0x09, 0x20, 0x00, vmemNamed("INT")), tx802Pmem({two})}), "TX802", &r);
      ck("a TX802 preset voice is not found in an internal bank", voiceOf(internal, 0) == "-" && voiceOf(internal, 1) == "INT03" &&
                                                                  r.summary().find("left off") != std::string::npos);
      Report r2; const Bank presets = readPatches(cat({dxBulk(0x09, 0x20, 0x00, vmemNamed("PRESETA")), tx802Pmem({two})}), "TX802", &r2);
      ck("a TX802 preset voice is found in a bank with no bank select, and an internal one is then not", voiceOf(presets, 0) == "PRESETA03" &&
                                                                                                         voiceOf(presets, 1) == "-" && r2.summary().find("left off") != std::string::npos); }

    // A DX1/DX5 performance bank (format 02, 64 x 64) is not read: its layout is not settled. It says so.
    { Report r; const Bank b = readPatches(dxBulk(0x02, 0x20, 0x00, Bytes(4096, 0x20)), "DX5", &r);
      ck("a DX1/DX5 performance bank is skipped with its reason", b.empty() && r.skipped.size() == 1 &&
                                                                  r.summary().find("DX1/DX5 performance") != std::string::npos); }

    // A MiniDexed performance.ini: eight TGs, the first four that carry VoiceData become parts. TG2 names a voice
    // on the SD card instead, so it is skipped and reported, and TG7 and TG8 do not fit. MIDIChannel 0 is off, and
    // pan comes in mirrored, as MiniDexed plays it (Pan=0 sounds hard right).
    { auto hexVoice = [](const char* name) {
          Bytes v(156, 0); for (int op = 0; op < 6; op++) v[op * 21 + 18] = 1; v[144] = 24; memcpy(v.data() + 145, name, strlen(name)); v[155] = 0x3F;
          std::string s; char h[4]; for (uint8_t b : v) { snprintf(h, sizeof h, "%02X ", b); s += h; } s.pop_back(); return s; };
      std::string ini = "# MiniDexed\r\nName=Four Layers\r\n";
      const char* names[8] = {"MD ONE", "", "MD THREE", "MD FOUR", "MD FIVE", "MD SIX", "MD SEVEN", "MD EIGHT"};
      for (int t = 1; t <= 8; t++) {
          const std::string n = std::to_string(t);
          ini += "BankNumber" + n + "=0\r\nVoiceNumber" + n + "=1\r\nMIDIChannel" + n + "=" + (t == 3 ? "0" : t == 4 ? "255" : n) + "\r\n";
          ini += "Volume" + n + "=" + (t == 1 ? "100" : "90") + "\r\nPan" + n + "=" + (t == 1 ? "0" : t == 3 ? "127" : "64") + "\r\n";
          ini += "Detune" + n + "=" + (t == 1 ? "5" : "0") + "\r\nNoteShift" + n + "=" + (t == 1 ? "-12" : "0") + "\r\n";
          ini += "NoteLimitLow" + n + "=" + (t == 5 ? "60" : "0") + "\r\nNoteLimitHigh" + n + "=127\r\n";
          ini += "VoiceData" + n + "=" + (*names[t - 1] ? hexVoice(names[t - 1]) : "") + "\r\n\r\n";
      }
      ini += "CompressorEnable=1\r\nReverbEnable=1\r\n";
      Report r; const Bank b = readPatches(Bytes(ini.begin(), ini.end()), "performance.ini", &r);
      const bool ok = b.perfs.size() == 1 && b.voices.size() == 4;
      ck("a MiniDexed performance.ini reads as one FS1R performance of four voices", ok && b.perfs[0].name == "Four Layers" && !b.rewrapped.empty());
      if (ok) {
          const Item& p = b.perfs[0];
          auto voiceOf = [&](int part) { return p.partVoice[part] >= 0 ? b.voices[(size_t)p.partVoice[part]].name : std::string("-"); };
          ck("the first four TGs with voice data are the parts", voiceOf(0) == "MD ONE" && voiceOf(1) == "MD THREE" && voiceOf(2) == "MD FOUR" && voiceOf(3) == "MD FIVE");
          ck("a MiniDexed TG's channel, volume, pan, detune, shift and limits go onto its part",
             partByte(p, 0, 4) == 0 && partByte(p, 1, 4) == 0x7F && partByte(p, 2, 4) == 0x10 && partByte(p, 0, 0x0B) == 100 &&
             partByte(p, 0, 0x0E) == 112 && partByte(p, 1, 0x0E) == 16 && partByte(p, 2, 0x0E) == 64 && partByte(p, 0, 9) == 69 &&
             partByte(p, 0, 8) == 12 && partByte(p, 3, 0x0F) == 60);
          ck("a TG with no voice data is skipped and reported, and TGs past four named", r.skipped.size() == 1 && r.summary().find("TG2") != std::string::npos &&
                                                                                         r.summary().find("TG7, TG8") != std::string::npos);
      }
      Report r2; const Bank again = readPatches(b.rewrapped, "performance", &r2);
      ck("a MiniDexed performance is kept as the bulk and voices it was read into", again.perfs.size() == 1 && again.perfs[0].partVoice[3] == 3 && r2.skipped.empty());
      std::string two = "Name=Two\r\n";
      for (int t = 1; t <= 3; t++) {
          const std::string n = std::to_string(t);
          two += "MIDIChannel" + n + "=1\r\nVolume" + n + "=" + (t == 2 ? "0" : "100") + "\r\nPan" + n + "=64\r\nVoiceData" + n + "=" +
                 hexVoice(t == 3 ? "MD B" : "MD A") + "\r\n";
      }
      Report r3; const Bank md = readPatches(Bytes(two.begin(), two.end()), "two.ini", &r3);
      ck("a MiniDexed TG at volume 0 takes no part; two on one channel receive on the performance's, spread", md.perfs.size() == 1 && md.voices.size() == 2 &&
         partByte(md.perfs[0], 0, 4) == 0x10 && partByte(md.perfs[0], 1, 4) == 0x10 && partByte(md.perfs[0], 0, 0x0E) == 48 &&
         partByte(md.perfs[0], 1, 0x0E) == 80 && md.voices[1].name == "MD B"); }

    // A .fsvr preset: XML whose <sysex> is the stream in base64, read as the stream it carries.
    { const Bytes s = fs1rVoice("In Preset");
      static const char k64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string b64;
      for (size_t i = 0; i < s.size(); i += 3) {
          const uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < s.size() ? s[i + 1] << 8 : 0) | (i + 2 < s.size() ? s[i + 2] : 0);
          for (int k = 0; k < 4; k++) b64 += k <= (int)std::min<size_t>(s.size() - i, 3) ? k64[v >> (18 - 6 * k) & 63] : '=';
          if (i % 57 == 54) b64 += "\n    ";   // wrapped, as an editor may leave it
      }
      const std::string xml = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<fsvr version=\"1\" type=\"performance\">\n  <name>A &amp; B</name>\n"
                              "  <sysex encoding=\"base64\">" + b64 + "</sysex>\n</fsvr>\n";
      Report r; const Bank b = readPatches(Bytes(xml.begin(), xml.end()), "Preset", &r);
      ck("a .fsvr reads as the sysex it carries", b.voices.size() == 1 && b.voices[0].name == "In Preset" && b.rewrapped == s && r.skipped.empty());
      const std::string other = "<?xml version=\"1.0\"?>\n<notfsvr><sysex>AAAA</sysex></notfsvr>\n";
      Report r2; const Bank none = readPatches(Bytes(other.begin(), other.end()), "Other", &r2);
      ck("XML that is not a .fsvr is not read", none.empty() && r2.skipped.size() == 1); }

    return fails;
}
