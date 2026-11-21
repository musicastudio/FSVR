// patchfiles/patchfiles.h - patch files read into banks of performances, voices and Fseqs. Ours.
//
// One way in: a file's bytes and a name, sniffed by content, give a Bank and a Report of what was read and
// what was skipped. An item is the sysex the FS1R takes for it, so a DX voice is its ACED (when it has a
// supplement) and its VCED, and the engine's own conversion runs when it loads.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fsvr {

struct Item {
    std::string name;
    int category = 0;              // the unit's category byte (performances and FS1R voices; DX voices have none)
    int address = 0;               // the bulk's address high: 0x10/0x11, 0x40..0x43/0x51, 0x60/0x61/0x70; 0 for a DX voice
    int number = -1;               // address low of an internal memory bulk (0x11, 0x51, 0x61), -1 otherwise
    int frames = 0;                // an Fseq's length in frames, as its header's end step gives it
    std::vector<uint8_t> syx;      // the message stream that loads it (a DX voice: its ACED, if any, then its VCED)
    int partVoice[4] = {-1, -1, -1, -1};   // a performance: voices (0x40..0x43 bulks) that came right after it
    int fseq = -1;                         // and the Fseq that came with them
    bool fromDx = false;           // a performance built from a DX one: at and len are the whole DX bank's, and its part voices whole VMEMs
    bool ofVoice = false;          // and one made for a voice of a file with no performance, playing it alone on part 1
    size_t at = 0, len = 0;        // its bytes in the bank's file (a DX voice: from its ACED; a VMEM voice: the whole VMEM)
    int slot = -1;                 // a VMEM voice: its place in the bank, 0..31
    std::shared_ptr<const std::vector<uint8_t>> vmem;   // and the VMEM message it is in, as read
    std::shared_ptr<const std::vector<uint8_t>> amem;   // and the AMEM message before it in the same file, if any
    size_t amemAt = 0;                                  // where that AMEM is in the file
    const uint8_t* data() const { return syx.data() + 9; }   // a native bulk's data, after its address
};

struct Bank {
    std::string name, path;        // path: the file, empty for a bank built into the plug-in
    std::vector<Item> perfs, voices, fseqs;
    // The file as sysex when it was not (a .mid, a headerless bank): what the library keeps in its place,
    // and what the items' at and len count in. Empty when the file was sysex already.
    std::vector<uint8_t> rewrapped;
    // It holds DX performances and no voice bank of its own: read it after the files whose banks it names.
    bool namesVoices = false;
    // Its banks its TX802 performances took as the ROM's presets A01-B64: each one's voice, -1 where none.
    std::vector<int> tx802Presets;
    // The library's preset banks its performances played, as messages to keep before the file so it plays
    // them again when read alone. Empty when it took none from the library.
    std::vector<uint8_t> libraryPresets;
    bool empty() const { return perfs.empty() && voices.empty() && fseqs.empty(); }
    bool readonly() const { return path.empty(); }
};

struct Report {
    std::vector<std::string> warnings;   // read anyway: a bad checksum, out-of-range bytes clamped
    std::vector<std::string> skipped;    // not read, each with the reason
    std::string summary() const;         // "skipped 2: ...; 1 warning: ...", empty when there is nothing to say
};

// What waits for a later message across files read together: an ACED for its VCED, an AMEM for its VMEM,
// and the DX voices a performance names by number.
struct Pending {
    std::vector<uint8_t> aced;           // a whole ACED message
    std::vector<uint8_t> amem;           // the 1120 data bytes of an AMEM, 32 slots of 35
    int half = -1;                       // the DX7II bank select's half: 0 voices 1-32, 1 voices 33-64
    struct Voice {
        Item item;                       // an empty syx where none was
        bool selected = false;           // its bank came behind a bank select
        std::string file;                // the name of the file it was read in
    };
    std::vector<Voice> numbered;         // the VMEM voices read so far by number, 0..127
    std::vector<Voice> presets;          // a TX802's presets A01-B64 from the library, for a performance whose files lack them
    int next = 0;                        // the number a VMEM with no bank select before it starts at
    // The folder of the file read next, set before each: the numbering starts over in another, since a set of
    // banks and the performances that name them come together.
    std::string folder, numberedIn;
    std::string unpaired() const;        // a supplement still waiting, for the report: "an AMEM with no voice bank after it"
};

// Any supported file as one bank. Pass the same Pending to each of several files read together, so a
// supplement in one pairs with the bank in the next.
Bank readPatches(const std::vector<uint8_t>& bytes, const std::string& name, Report* report = nullptr, Pending* pending = nullptr);

// A VMEM voice's message with the slot renamed, or blanked to the DX7's INIT VOICE, its checksum redone.
std::vector<uint8_t> vmemRenamed(const Item& it, const std::string& name);
std::vector<uint8_t> vmemBlanked(const Item& it);
// Its AMEM message with the voice's slot back to a DX7 voice's default, so the blanked voice gets no ACED.
std::vector<uint8_t> amemBlanked(const Item& it);

} // namespace fsvr
