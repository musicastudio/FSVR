// patchfiles/read.cpp - readPatches: the containers in a fixed order, then the formats on each message. Ours.
#include "dx7.h"
#include <map>

namespace fsvr {

using namespace patchfiles;

void patchfiles::Reading::warn(const Message& m, const std::string& what) {
    report.warnings.push_back((bank.name.empty() ? "" : bank.name + ", ") + "byte " + std::to_string(m.at) + ": " + what);
}

// Why a message no format took was skipped.
static std::string skipped(const Message& m) {
    const uint8_t* p = m.p;
    if (m.len < 4 || p[1] != 0x43) return "a message that is not a Yamaha bulk";
    if ((p[2] & 0xF0) == 0x10) return "a parameter change";
    if ((p[2] & 0xF0) == 0x20) return "a dump request";
    if ((p[2] & 0xF0) != 0) return "a Yamaha message FSVR does not read";
    switch (p[3]) {
    case 0x5E: return "an FS1R system bulk or one cut short";
    case 0x03: case 0x04: return "a 4-op voice (the FS1R has no conversion for it)";
    // The 64 records of 64 bytes hold two voice numbers, two 16-byte blocks of controller settings and a
    // 30-character name, but neither manual gives the byte layout and the factory dump's checksum does not
    // verify, so which byte is the play mode, split point or balance is not known.
    case 0x02: return "a DX1/DX5 performance bank (its layout is not documented, so FSVR does not read it)";
    case 0x7E:
        if (m.len >= 16) return "a \"" + text(p + 6, 10) + "\" bulk FSVR does not read yet";
        break;
    case 0x00: case 0x05: case 0x06: case 0x09: return "a DX bulk cut short";
    }
    return "a Yamaha bulk of a kind FSVR does not read";
}

std::string Report::summary() const {
    auto list = [](const std::vector<std::string>& v) {
        std::map<std::string, int> n;
        std::vector<std::string> order;
        for (auto& s : v) if (!n[s]++) order.push_back(s);
        std::string out;
        for (auto& s : order) out += (out.empty() ? "" : "; ") + s + (n[s] > 1 ? " (" + std::to_string(n[s]) + ")" : "");
        return out;
    };
    std::string s;
    if (!skipped.empty()) s = "skipped " + std::to_string(skipped.size()) + ": " + list(skipped);
    if (!warnings.empty()) s += (s.empty() ? "" : "; ") + std::to_string(warnings.size()) + (warnings.size() == 1 ? " warning: " : " warnings: ") + list(warnings);
    return s;
}

Bank readPatches(const std::vector<uint8_t>& bytes, const std::string& name, Report* report, Pending* pending) {
    Report ownReport;
    Pending ownPending;
    Bank bank;
    bank.name = name;
    Report& rep = report ? *report : ownReport;
    Reading r{bank, rep, pending ? *pending : ownPending};
    if (r.pending.folder != r.pending.numberedIn) {   // banks in another folder are another set
        r.pending.numbered.clear();
        r.pending.next = 0;
        r.pending.numberedIn = r.pending.folder;
    }
    Bytes stream;
    const Bytes* s = &bytes;
    if (smfStream(bytes, stream, rep) || fsvrStream(bytes, stream, rep) || miniDexedStream(bytes, name, stream, rep) || headerlessStream(bytes, stream, rep)) {
        bank.rewrapped = std::move(stream);
        s = &bank.rewrapped;
    }
    if (Bytes mended; repairStream(*s, mended, rep, name)) {
        bank.rewrapped = std::move(mended);
        s = &bank.rewrapped;
    }
    const std::vector<Message> ms = messages(*s);
    if (ms.empty() && s == &bytes) rep.skipped.push_back((name.empty() ? std::string("a file") : name) + ": not a patch file FSVR knows");
    for (const Message& m : ms) {
        if (readFs1r(m, r) || readAced(m, r) || readVced(m, r) || readAmem(m, r) || readVmem(m, r) || readDx7ii(m, r) || readTx802(m, r)) continue;
        r.open = -1;
        rep.skipped.push_back(skipped(m));
    }
    finishPerformances(r);   // a performance's voices may come after it in the file
    bank.namesVoices = r.anyDx && std::none_of(bank.voices.begin(), bank.voices.end(), [](const Item& it) { return it.len > 0; });   // borrowed voices have none
    if (!pending && !ownPending.unpaired().empty()) rep.skipped.push_back((name.empty() ? std::string("a file") : name) + ": " + ownPending.unpaired());
    return bank;
}

std::string Pending::unpaired() const {
    if (!amem.empty()) return "an AMEM with no voice bank after it";
    if (!aced.empty()) return "an ACED with no voice after it";
    return "";
}

} // namespace fsvr
