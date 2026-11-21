// patchfiles/reading.h - what the dispatcher, the containers and the formats share. Ours.
#pragma once
#include "patchfiles.h"
#include <algorithm>

namespace fsvr::patchfiles {

using Bytes = std::vector<uint8_t>;

// One F0..F7 of a sysex stream, and where it starts.
struct Message {
    const uint8_t* p;
    size_t len, at;
};

// A DX performance's part as it goes onto an FS1R part (performance.cpp). The values are the FS1R's part
// bytes (Data List, Performance Part Parameter), except voice, which is the DX voice number the performance names.
constexpr int PFM = 0x10, OFF = 0x7F;   // receive on the performance channel; receive nothing
struct PartSetup {
    bool on = false;
    int voice = -1;
    int channel = PFM, volume = 127, pan = 64, detune = 0, shift = 0, low = 0, high = 127;
};

// A DX performance waiting for the end of the file, when its voices are looked up by number.
struct DxPerformance {
    std::string name, kind;              // kind names it in the report: "a DX7II performance", "a TX802 performance"
    bool tx802 = false;                  // its voice numbers run on to 255: presets A and B from 128
    bool silent = false;                 // a part was left off for its level 0
    PartSetup part[4];
    size_t at = 0, len = 0;
};

// One file being read: the bank it fills, the report, what waits for a later message, the performance the
// bulks right after it belong to, and the DX performances and numbered voices it holds.
struct Reading {
    Bank& bank;
    Report& report;
    Pending& pending;
    int open = -1;
    size_t acedAt = SIZE_MAX;            // the pending ACED's place in this file; SIZE_MAX when it came in an earlier one
    std::shared_ptr<const Bytes> amem;   // the pending AMEM's message and place, when it came in this file
    size_t amemAt = 0;
    std::vector<DxPerformance> dx;
    bool anyDx = false;                  // the file held DX performances
    std::vector<int> fromLibrary = std::vector<int>(128, -1);   // a library preset's voice in this bank, once copied in
    std::vector<int> numbered = std::vector<int>(128, -1);   // a DX voice number's voice in this bank
    std::vector<bool> selected = std::vector<bool>(128);     // and whether its bank came behind a bank select
    std::vector<int> before = std::vector<int>(128, -1);     // the same for the dumps before the last one in this file
    std::vector<bool> beforeSelected = std::vector<bool>(128);
    std::vector<std::string> borrowed;   // the files read before this one whose voices its performances play
    void warn(const Message& m, const std::string& what);
};

// An FS1R performance bulk (Data List 3.2.1, 400 bytes at 10 00 00) of four parts, built on the engine's own
// init performance; and the file's DX performances as items, their voices found by number (performance.cpp).
Bytes performanceBulk(const std::string& name, const PartSetup* parts);
// The parts that play, once their voices are known (voice: the same number for parts playing the same voice):
// one shared channel becomes the performance's, and parts all at the centre are spread across it.
void arrangeParts(PartSetup* parts, const int* voice);
void finishPerformances(Reading& r);
// A bank landing on numbers a bank of this file already holds starts another dump, as the next of several files
// kept one after the other does: the performances read so far find their voices first, and the banks before
// stay for the later performances that name numbers the new banks do not hold.
void newDump(Reading& r);

// Containers: a file to a sysex stream. The first four say whether the file is theirs and rewrap it into out.
bool smfStream(const Bytes& file, Bytes& out, Report& report);          // container_smf.cpp
bool headerlessStream(const Bytes& file, Bytes& out, Report& report);   // container_headerless.cpp
bool fsvrStream(const Bytes& file, Bytes& out, Report& report);         // container_fsvr.cpp
bool miniDexedStream(const Bytes& file, const std::string& name, Bytes& out, Report& report);   // container_minidexed.cpp
std::vector<Message> messages(const Bytes& stream);                     // container_sysex.cpp: a .syx, an .fs1
// A stream's Yamaha bulks with an F7 or a DX bulk's checksum missing, mended into out; false when none was.
bool repairStream(const Bytes& stream, Bytes& out, Report& report, const std::string& name);

// Formats: each takes the messages that are its own and says whether it did.
bool readFs1r(const Message& m, Reading& r);   // format_fs1r.cpp
bool readAced(const Message& m, Reading& r);   // format_aced.cpp
bool readVced(const Message& m, Reading& r);   // format_vced.cpp
bool readAmem(const Message& m, Reading& r);   // format_amem.cpp, with the DX7II bank select
bool readVmem(const Message& m, Reading& r);   // format_vmem.cpp
bool readDx7ii(const Message& m, Reading& r);  // format_dx7ii.cpp: PCED and PMEM
bool readTx802(const Message& m, Reading& r);  // format_tx802.cpp

// A Yamaha "LM  " bulk with this ten-byte tag: F0 43 0n 7E <count> <tag> <data> cs F7.
inline bool lmBulk(const Message& m, const char* tag) { return m.len >= 18 && m.p[1] == 0x43 && (m.p[2] & 0xF0) == 0 && m.p[3] == 0x7E && std::equal(tag, tag + 10, m.p + 6); }

// A Yamaha DX bulk: F0 43 0n <format> <count hi> <count lo>, whatever the device number.
inline bool dxBulk(const Message& m, uint8_t format) { return m.len >= 8 && m.p[1] == 0x43 && (m.p[2] & 0xF0) == 0 && m.p[3] == format; }

} // namespace fsvr::patchfiles
