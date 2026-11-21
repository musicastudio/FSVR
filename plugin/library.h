// The bank manager's data: .syx files read as banks of performances, voices and Fseqs. The factory set is
// one bank, built into the plug-in; the user's library is a folder of .syx files, one bank each, shared by
// every instance. Nothing here knows how the engine stores a patch: an item is the sysex that loads it.
#pragma once
#include "patchfiles/patchfiles.h"   // Item, Bank and readPatches, which reads every file the library takes
#include <cstdint>
#include <string>
#include <vector>

namespace fsvr {

// A bulk's F0..F7 with a new address and its checksum redone (a voice bound for a given part).
std::vector<uint8_t> readdress(const std::vector<uint8_t>& syx, int ah, int am, int al);
// An item's sysex with a new name (cut to the format's length), or a new category byte (a DX voice has
// none: unchanged), its checksum redone. A VMEM voice's is its whole VMEM, the name written in its slot.
std::vector<uint8_t> renamed(const Item& it, const std::string& name);
std::vector<uint8_t> recategorized(const Item& it, int category);
int nameLength(const Item& it);   // 12 a performance, 10 a voice, 8 an Fseq
// One FS1R native bulk: F0 43 00 5E bc bc ah am al <data> cs F7.
std::vector<uint8_t> bulk(int ah, int am, int al, const uint8_t* data, size_t size);

extern const char* const kCategories[23];   // the Data List's names, "--" first

// The user library: <Documents>/FSVR/Library (FSVR_LIBRARY overrides it), its .syx files by name (and .fs1,
// which other editors write: one performance and its four voices, sysex as a .syx is).
class Library {
public:
    Library();
    std::string dir;
    // The read-only banks built into the plug-in, set before the first rescan: they lead banks, ahead of the
    // folder's, and their items are numbered after the user's, so a U number already saved keeps its preset.
    std::vector<Bank> builtin;
    std::vector<Bank> banks;
    struct Ref { int bank, index; };
    std::vector<Ref> perfs, voices, fseqs;   // every item, the folder's banks first: U1, U2 ...
    // Reads the folder again when a file came, went or changed since the last look; true if it did.
    bool rescan(bool force = false);
    // Why the last look could not read the folder or one of its files, for the status line; empty when it could.
    std::string error;
    // Copies a file into the folder as a new bank named after it ("Name", "Name 2" ...) and rescans: a .syx
    // as it is, anything else as the sysex it was read as. The new bank's index, or -1 (err says why: "Nothing
    // FSVR can read in <file>: <reason>"); report says what was skipped.
    int import(const std::string& path, std::string& err, Report* report = nullptr);
    // Several files as one new bank, named name or else after their folder, each performance keeping the voices and Fseq
    // that came with it. A file holding internal memory (a unit's dump, whose performances name their voices
    // by number) is a bank of its own, as import() makes it, since its numbers would meet another file's.
    // The files are read as one stream, so a supplement in one pairs with the bank in the next. A file with no
    // FS1R performance (DX banks and their performances) is kept as it was, ahead of the rest; another
    // folder's such files make a bank of their own, since DX banks number per folder.
    // The new banks' indexes, the shared one first; err says what could not be read ("nothing FSVR can read in
    // <file> (<reason>)", or in how many files).
    std::vector<int> importAll(const std::vector<std::string>& paths, std::string& err, Report* report = nullptr, const std::string& name = "");
    // Writes bytes as a new bank called name (made unique the same way); its index, or -1.
    int add(const std::string& name, const std::vector<uint8_t>& bytes, std::string& err);
    int bankOf(const std::string& path) const;
    // Edits a bank's file in place (a built-in bank refuses, as remove() and rename() do): each (at, len) range of it becomes the bytes given. A bank left with
    // nothing in it goes the way remove() sends it. False (err says why) when it cannot be written.
    struct Splice { size_t at, len; std::vector<uint8_t> bytes; };
    bool splice(int bank, std::vector<Splice> edits, std::string& err);
    // A bank's file moves into the library's Deleted folder, out of the library but not lost.
    bool remove(int bank, std::string& err);
    bool rename(int bank, const std::string& name, std::string& err, std::string* path = nullptr);   // its file, so its name
    const Item* perf(int n) const { return n >= 1 && n <= (int)perfs.size() ? &banks[perfs[n - 1].bank].perfs[perfs[n - 1].index] : nullptr; }
    const Item* voice(int n) const { return n >= 1 && n <= (int)voices.size() ? &banks[voices[n - 1].bank].voices[voices[n - 1].index] : nullptr; }
    const Item* fseq(int n) const { return n >= 1 && n <= (int)fseqs.size() ? &banks[fseqs[n - 1].bank].fseqs[fseqs[n - 1].index] : nullptr; }
    int number(const std::vector<Ref>& list, int bank, int index) const;   // U number of a bank's item, 0 if none
private:
    std::string stamp_;
    std::string uniqueName(std::string stem) const;
};

// The files an import is given: each path, a folder's files walked through its subfolders in name order, hidden
// ones passed over. False when the folders hold more than kImportFiles (err says so), so a whole disk is refused.
constexpr size_t kImportFiles = 2000;
bool importPaths(const std::vector<std::string>& paths, std::vector<std::string>& files, std::string& err);

bool readFile(const std::string& utf8Path, std::vector<uint8_t>& out);
bool writeFile(const std::string& utf8Path, const std::vector<uint8_t>& bytes);

} // namespace fsvr
