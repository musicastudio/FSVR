// patchfiles/container_minidexed.cpp - a MiniDexed performance.ini: Key=Value lines, eight tone generators
// whose keys end in their number 1-8, each voice in VoiceDataN as 156 hex bytes (a VCED and its operator
// on/off byte). Ours, after MiniDexed's performanceconfig.cpp.
//
// Rewrapped as one FS1R performance bulk and then the VCED of each part, in part order, which is how an
// imported performance is kept; the formats read it back from there.
#include "dx7.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace fsvr::patchfiles {

constexpr int TGS = 8, MIDI_OMNI = 16;

namespace {
struct Tg {
    bool seen = false;
    int channel = 0, volume = 100, pan = 64, detune = 0, shift = 0, low = 0, high = 127;
    Bytes voice;
};
}

static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static Bytes hexBytes(const std::string& s) {
    Bytes out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ' ' || s[i] == '\t') { ++i; continue; }
        char* end = nullptr;
        const long v = std::strtol(s.c_str() + i, &end, 16);
        const size_t n = (size_t)(end - (s.c_str() + i));
        if (!n || n > 2) return {};
        out.push_back((uint8_t)v);
        i += n;
    }
    return out;
}

bool miniDexedStream(const Bytes& file, const std::string& fileName, Bytes& out, Report& report) {
    // Text starts on a printable byte, a line break or a UTF-8 byte order mark: that is looked at first.
    const uint8_t first = file.empty() ? 0 : file[0];
    if (!(first >= 0x20 && first < 0x7F) && first != '\t' && first != '\r' && first != '\n' && first != 0xEF) return false;
    if (std::memchr(file.data(), 0, file.size())) return false;
    const std::string doc(file.begin(), file.end());
    Tg tg[TGS];
    std::string name;
    bool ours = false;
    for (size_t at = 0; at < doc.size();) {
        size_t end = doc.find('\n', at);
        if (end == std::string::npos) end = doc.size();
        std::string line = doc.substr(at, end - at);
        at = end + 1;
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "Name") { name = value; continue; }
        const char last = key.empty() ? 0 : key.back();
        if (last < '1' || last > '0' + TGS) continue;
        Tg& t = tg[last - '1'];
        key.pop_back();
        const int v = std::atoi(value.c_str());
        if (key == "VoiceData") { t.voice = hexBytes(value); ours = true; }
        else if (key == "MIDIChannel") { t.channel = v; ours = true; }
        else if (key == "Volume") t.volume = v;
        else if (key == "Pan") t.pan = v;
        else if (key == "Detune") t.detune = v;
        else if (key == "NoteShift") t.shift = v;
        else if (key == "NoteLimitLow") t.low = v;
        else if (key == "NoteLimitHigh") t.high = v;
        else continue;
        t.seen = true;
    }
    if (!ours) return false;
    const std::string where = fileName.empty() ? "" : fileName + ", ";
    PartSetup parts[4];
    Bytes voices;
    std::string dropped;
    int n = 0, clamped = 0, same[4] = {0, 1, 2, 3};
    bool silent = false;
    for (int k = 0; k < TGS; ++k) {
        const Tg& t = tg[k];
        if (!t.seen) continue;
        const std::string tgName = "TG" + std::to_string(k + 1);
        if (t.voice.size() < VCED_SIZE) {
            report.skipped.push_back(where + "MiniDexed " + tgName + ": no voice data (it names a voice on the MiniDexed's SD card)");
            continue;
        }
        if (!t.volume) { silent = true; continue; }   // ours: a TG at volume 0 takes no part
        if (n == 4) { dropped += (dropped.empty() ? "" : ", ") + tgName; continue; }
        PartSetup& s = parts[n++];
        s.on = true;
        // MIDIChannel 0 is off, 1-16 a channel, and anything above omni; ours: omni plays on the performance channel.
        s.channel = t.channel == 0 ? OFF : t.channel <= MIDI_OMNI ? t.channel - 1 : PFM;
        s.volume = t.volume;
        s.pan = 128 - t.pan;   // ours, as MiniDexed plays it: its Pan=0 sounds hard right
        s.detune = t.detune;   // ours: one MiniDexed detune step, one FS1R part detune step
        s.shift = t.shift;
        s.low = t.low;
        s.high = t.high;
        uint8_t vced[VCED_SIZE];
        std::memcpy(vced, t.voice.data(), VCED_SIZE);
        clamped += clampVced(vced);
        const Bytes m = vcedMessage(vced);
        for (int j = 0; j < n - 1; ++j)
            if (std::equal(m.begin(), m.end(), voices.begin() + (std::ptrdiff_t)(j * m.size()))) { same[n - 1] = j; break; }
        voices.insert(voices.end(), m.begin(), m.end());
    }
    if (!dropped.empty()) report.warnings.push_back(where + "MiniDexed " + dropped + " not read (the FS1R has four parts)");
    if (clamped) report.warnings.push_back(where + std::to_string(clamped) + " MiniDexed voices had bytes out of the DX7's ranges, clamped");
    if (!n) {   // nothing to play; each TG is reported
        if (silent) report.skipped.push_back(where + "a MiniDexed performance whose TGs are all at volume 0");
        return true;
    }
    arrangeParts(parts, same);
    if (name.empty()) {
        name = fileName.substr(0, fileName.rfind('.'));
        if (name.empty()) name = "MiniDexed";
    }
    const Bytes perf = performanceBulk(name, parts);
    out.insert(out.end(), perf.begin(), perf.end());
    out.insert(out.end(), voices.begin(), voices.end());
    return true;
}

} // namespace fsvr::patchfiles
