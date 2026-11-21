// patchfiles/performance.cpp - a DX performance as an FS1R performance bulk and the voices of its parts. Ours.
//
// The unit has no such conversion: the firmware takes only VCED and ACED. So every choice here and in the
// formats that fill a PartSetup is ours, not the hardware's. The bulk starts from the engine's init
// performance (init_perf), and a DX performance changes its name, its effects (dxEffects) and the part bytes
// PartSetup names.
#include "dx7.h"
#include "effects/host/effect_host.h"
#include "presets/performance.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace fsvr::patchfiles {

constexpr size_t PERF_SIZE = 400, COMMON = 192, PART = 52, PERF_NAME = 12;
constexpr uint8_t BANK_OFF = 0, BANK_INT = 1;

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Ours: a part is never panned all the way, so a voice set hard to one side still bleeds a little into the other.
constexpr int PAN_CENTRE = 64, PAN_LEFTMOST = 16, PAN_RIGHTMOST = 112;

// Ours: the effects a DX performance gets, after Yamaha's own Best of DX bonus set (presets/bonus), which gives its
// DX sounds Hall1 at return 64 and a chorus. Here the chorus is the insertion, on every part that plays, a little
// drier than its type's default, and sends to the reverb. Each type's other parameters are the unit's defaults for
// it, as choosing the type on the unit sets them.
constexpr uint8_t HALL1 = 1, CHORUS = 1, REVERB_RETURN = 64, INSERTION_LEVEL = 127, INSERTION_TO_REVERB = 16;
constexpr uint8_t CHORUS_DRY_WET = 48;   // 64 is D=W
constexpr uint8_t PART_INSERTION_SW = 0x14;

static void dxEffects(Perf& P) {
    namespace H = vop3host;
    uint8_t* common = P.c;   // c and fx are the 192 common bytes in a row
    auto fx = std::make_unique<H::EffectHost>();
    for (uint32_t i = 0; i < H::COMMON_BYTES; ++i) fx->store(i, common[i]);
    fx->store(H::REVERB_TYPE, HALL1);
    fx->reverbTypeChanged();
    fx->store(H::INSERTION_TYPE, CHORUS);
    fx->insertionTypeChanged();
    std::copy(fx->perf, fx->perf + H::COMMON_BYTES, common);
    common[H::REVERB_PAN] = common[H::VARIATION_PAN] = common[H::INSERTION_PAN] = PAN_CENTRE;
    common[H::REVERB_RETURN] = REVERB_RETURN;
    common[H::INSERTION_LEVEL] = INSERTION_LEVEL;
    common[H::INSERTION_TO_REVERB] = INSERTION_TO_REVERB;
    common[H::INSERTION_PARAM_BYTES + 2 * H::DRY_WET] = 0;
    common[H::INSERTION_PARAM_BYTES + 2 * H::DRY_WET + 1] = CHORUS_DRY_WET;
}

Bytes performanceBulk(const std::string& name, const PartSetup* parts) {
    auto P = std::make_unique<Perf>();
    init_perf(*P);
    dxEffects(*P);
    for (size_t k = 0; k < PERF_NAME; ++k) {
        const char c = k < name.size() ? name[k] : ' ';
        P->c[k] = (uint8_t)(c >= 32 && c < 127 ? c : ' ');
    }
    for (int i = 0; i < 4; ++i) {
        const PartSetup& s = parts[i];
        uint8_t* p = P->part[i].p;
        // Ours: a part with no DX voice is off, so it neither sounds nor names a voice the library would look up.
        p[1] = s.on ? BANK_INT : BANK_OFF;
        p[2] = (uint8_t)clamp(s.voice, 0, 127);
        p[4] = (uint8_t)(s.on ? s.channel : OFF);
        p[8] = (uint8_t)clamp(24 + s.shift, 0, 48);
        p[9] = (uint8_t)clamp(64 + s.detune, 0, 127);
        p[0x0B] = (uint8_t)clamp(s.volume, 0, 127);
        p[0x0E] = (uint8_t)clamp(s.pan, PAN_LEFTMOST, PAN_RIGHTMOST);
        p[0x0F] = (uint8_t)clamp(s.low, 0, 127);
        p[0x10] = (uint8_t)clamp(s.high, 0, 127);
        p[PART_INSERTION_SW] = s.on;
    }
    Bytes m = {0xF0, 0x43, 0x00, 0x5E, PERF_SIZE >> 7, PERF_SIZE & 0x7F, 0x10, 0x00, 0x00};
    m.insert(m.end(), P->c, P->c + sizeof P->c);
    m.insert(m.end(), P->fx, P->fx + sizeof P->fx);
    for (const Part& pt : P->part) m.insert(m.end(), pt.p, pt.p + PART);
    m.push_back(checksum(m.data() + 4, m.size() - 4));
    m.push_back(0xF7);
    static_assert(sizeof(Perf::c) + sizeof(Perf::fx) == COMMON, "the 192 common bytes");
    return m;
}

// Ours: how far either side of the centre parts left there are spread, and further for one voice detuned
// against itself, which is played for its width.
constexpr int PAN_SPREAD = 16, PAN_SPREAD_DETUNED = 32;

void arrangeParts(PartSetup* parts, const int* voice) {
    int on[4], n = 0;
    for (int i = 0; i < 4; ++i)
        if (parts[i].on) on[n++] = i;
    if (!n) return;
    // Ours: parts that all receive on one channel, as a DX performance's TGs mostly do, receive on the
    // performance's, so it plays from whichever channel the performance is set to.
    const PartSetup& first = parts[on[0]];
    if (first.channel != OFF && std::all_of(on, on + n, [&](int i) { return parts[i].channel == first.channel; }))
        for (int k = 0; k < n; ++k) parts[on[k]].channel = PFM;
    // Ours: a part alone is centred, wherever its format sent it (a TX802 TG alone on output I). Parts all panned
    // alike, as a TX802's TGs all on one output are, are spread around the centre; parts panned apart keep theirs.
    if (n == 1) { parts[on[0]].pan = PAN_CENTRE; return; }
    if (!std::all_of(on, on + n, [&](int i) { return parts[i].pan == first.pan; })) return;
    const bool detuned = std::all_of(on, on + n, [&](int i) {
        const PartSetup& s = parts[i];
        return voice[i] == voice[on[0]] && s.shift == first.shift && s.low == first.low && s.high == first.high;
    }) && std::any_of(on, on + n, [&](int i) { return parts[i].detune != first.detune; });
    const int width = detuned ? PAN_SPREAD_DETUNED : PAN_SPREAD;
    // The flattest part goes left; parts tuned alike keep their order.
    std::stable_sort(on, on + n, [&](int a, int b) { return parts[a].detune < parts[b].detune; });
    for (int k = 0; k < n; ++k)
        parts[on[k]].pan = PAN_CENTRE + (int)std::lround(width * (2.0 * k / (n - 1) - 1));
}

// Which banks a voice may come from: any, only those behind a bank select, or only those with none.
enum class From { Any, Selected, Unselected };

// A voice number (0-127) in banks of the kind given: one read in this file, else one read earlier in the files
// read with it, copied in.
static int voiceFor(Reading& r, int number, From from) {
    if (number < 0 || number > 127) return -1;
    const size_t n = (size_t)number;
    auto fits = [&](bool selected) { return from == From::Any || selected == (from == From::Selected); };
    if (r.numbered[n] >= 0 && fits(r.selected[n])) return r.numbered[n];
    if (r.before[n] >= 0 && fits(r.beforeSelected[n])) return r.before[n];
    const std::vector<Pending::Voice>& earlier = r.pending.numbered;
    if (n >= earlier.size() || earlier[n].item.syx.empty() || !fits(earlier[n].selected)) return -1;
    if (std::find(r.borrowed.begin(), r.borrowed.end(), earlier[n].file) == r.borrowed.end()) r.borrowed.push_back(earlier[n].file);
    Item it = earlier[n].item;
    it.at = it.len = 0;   // its bytes are in another file
    it.slot = -1;
    it.vmem.reset();
    r.bank.voices.push_back(std::move(it));
    const int k = (int)r.bank.voices.size() - 1;
    if (r.numbered[n] < 0) { r.numbered[n] = k; r.selected[n] = earlier[n].selected; }
    return k;
}

// Ours, settled against the TX802 factory set (P.SYX and A1, A2, B1, B2 beside it): a TX802 voice number counts
// I01-I64, C01-C64, then the ROM's presets A01-A64 and B01-B64, and the factory performances name the presets
// (0x82 is A03, ReverbBras). The presets cannot be in a dump of the unit's memory. The set sends them as the four
// files A1, A2, B1, B2, VMEMs with no bank select, which number on 0-127 in that order; so a preset voice is
// looked up at its number less 128, among banks that came with no bank select. When the TX802 performances read
// name a preset voice, those banks are taken to be the presets, and none of them then stands for a voice of
// the unit's memory.
constexpr int TX802_PRESETS = 128;

static bool presetBanks(const Reading& r) {
    for (const DxPerformance& d : r.dx)
        for (const PartSetup& s : d.part)
            if (d.tx802 && s.on && s.voice >= TX802_PRESETS) return true;
    return false;
}

// A preset the files read do not hold, from the library's bank of them (Pending::presets), copied in once.
static int libraryPreset(Reading& r, int n) {
    const std::vector<Pending::Voice>& lib = r.pending.presets;
    if (n < 0 || (size_t)n >= lib.size() || lib[(size_t)n].item.syx.empty()) return -1;
    int& k = r.fromLibrary[(size_t)n];
    if (k >= 0) return k;
    Item it = lib[(size_t)n].item;
    it.at = it.len = 0;   // its bytes are in the library's bank
    it.slot = -1;
    it.vmem.reset();
    it.amem.reset();
    r.bank.voices.push_back(std::move(it));
    return k = (int)r.bank.voices.size() - 1;
}

static int partVoice(Reading& r, const DxPerformance& d, int number, bool presets) {
    if (!d.tx802) return voiceFor(r, number, From::Any);
    if (number >= TX802_PRESETS) {
        const int k = voiceFor(r, number - TX802_PRESETS, From::Unselected);
        return k >= 0 ? k : libraryPreset(r, number - TX802_PRESETS);
    }
    return voiceFor(r, number, presets ? From::Selected : From::Any);
}

// The banks this file's TX802 performances took as the presets, for the files read later that lack them.
static void recordPresets(Reading& r) {
    r.bank.tx802Presets.resize(TX802_PRESETS, -1);
    for (size_t v = 0; v < (size_t)TX802_PRESETS; ++v) {
        const int k = r.numbered[v] >= 0 && !r.selected[v] ? r.numbered[v] : r.before[v] >= 0 && !r.beforeSelected[v] ? r.before[v] : -1;
        if (k >= 0 && r.bank.voices[(size_t)k].slot >= 0) r.bank.tx802Presets[v] = k;
    }
}

// The library's preset banks as their messages, each AMEM before its VMEM, in the presets' order: kept before
// the file, they number 1-128 with no bank select as they did in the library's bank.
static Bytes libraryBanks(const Reading& r) {
    Bytes out;
    const void* last = nullptr;
    for (const Pending::Voice& v : r.pending.presets) {
        if (!v.item.vmem || v.item.vmem.get() == last) continue;
        last = v.item.vmem.get();
        if (v.item.amem) out.insert(out.end(), v.item.amem->begin(), v.item.amem->end());
        out.insert(out.end(), v.item.vmem->begin(), v.item.vmem->end());
    }
    return out;
}

// Ours: a dump of the internal memory and one of the cartridge both send their banks as voices 1-64, and
// nothing in either says which it is. A file whose banks are all in 1-64 and whose performances name a
// cartridge voice (65-128) is taken to be the cartridge's dump, as the DX7IIFD factory CART.SYX is.
static void cartridgeBanks(Reading& r) {
    bool low = false, high = false, names = false;
    for (int v = 0; v < 128; ++v)
        if (r.numbered[(size_t)v] >= 0) (v < 64 ? low : high) = true;
    for (const DxPerformance& d : r.dx)
        for (const PartSetup& s : d.part) names |= s.on && s.voice >= 64 && s.voice < 128;
    if (!low || high || !names) return;
    for (size_t v = 0; v < 64; ++v) {
        r.numbered[v + 64] = r.numbered[v];
        r.selected[v + 64] = r.selected[v];
        r.numbered[v] = -1;
    }
}

// The DX performances read so far as items, their voices found by number in the banks read so far.
static void resolve(Reading& r) {
    if (!r.dx.empty()) r.anyDx = true;
    cartridgeBanks(r);
    const bool presets = presetBanks(r);
    if (presets) recordPresets(r);
    for (DxPerformance& d : r.dx) {
        int found = 0, missing = 0, voice[4] = {-1, -1, -1, -1};
        bool preset = false;   // it names a preset none of the files read nor the library holds
        for (int i = 0; i < 4; ++i) {
            PartSetup& s = d.part[i];
            if (!s.on) continue;
            voice[i] = partVoice(r, d, s.voice, presets);
            if (voice[i] < 0) { s.on = false; ++missing; preset |= d.tx802 && s.voice >= TX802_PRESETS; } else ++found;
        }
        const std::string what = (r.bank.name.empty() ? "" : r.bank.name + ", ") + d.kind;
        if (!found && !missing && d.silent) { r.report.skipped.push_back(what + " whose parts are all at level 0"); continue; }
        if (!found && preset) {
            r.report.skipped.push_back(what + " whose TX802 presets are neither in the files read nor in the library (import presets A and B with a TX802 performance file first)");
            continue;
        }
        if (!found) { r.report.skipped.push_back(what + " whose voices are not in the files read"); continue; }
        if (missing) r.report.warnings.push_back(what + " voice not in the files read, its part left off");
        arrangeParts(d.part, voice);
        Item it;
        it.name = text(reinterpret_cast<const uint8_t*>(d.name.data()), std::min(d.name.size(), PERF_NAME));
        it.address = 0x10;
        it.fromDx = true;
        it.syx = performanceBulk(it.name, d.part);
        it.at = d.at;
        it.len = d.len;
        for (int i = 0; i < 4; ++i) it.partVoice[i] = d.part[i].on ? voice[i] : -1;
        r.bank.perfs.push_back(std::move(it));
    }
    r.dx.clear();
}

void newDump(Reading& r) {
    resolve(r);
    for (size_t v = 0; v < 128; ++v)
        if (r.numbered[v] >= 0) {
            r.before[v] = r.numbered[v];
            r.beforeSelected[v] = r.selected[v];
            r.numbered[v] = -1;
        }
}

// Ours: a file of DX voices and no performance gives each voice a performance of its own, playing it alone on
// part 1, so the bank plays from the performance list too. Rebuilt on every read, never stored.
static void voicePerformances(Reading& r) {
    if (!r.bank.perfs.empty() || r.anyDx) return;   // it had performances, even ones skipped
    for (size_t k = 0; k < r.bank.voices.size(); ++k) {
        const Item& v = r.bank.voices[k];
        if (v.address || !v.len) continue;   // this file's DX voices: an FS1R voice has an address, a borrowed one no bytes
        PartSetup part[4];
        part[0].on = true;
        const int voice[4] = {(int)k, -1, -1, -1};
        arrangeParts(part, voice);
        Item it;
        it.name = v.name;
        it.address = 0x10;
        it.fromDx = it.ofVoice = true;
        it.syx = performanceBulk(it.name, part);
        it.at = v.at;
        it.len = v.len;
        it.partVoice[0] = (int)k;
        r.bank.perfs.push_back(std::move(it));
    }
}

void finishPerformances(Reading& r) {
    resolve(r);
    voicePerformances(r);
    if (!r.borrowed.empty()) {
        std::string files;
        for (const std::string& f : r.borrowed) files += (files.empty() ? "" : ", ") + (f.empty() ? std::string("a file") : f);
        r.report.warnings.push_back((r.bank.name.empty() ? "" : r.bank.name + ", ") + "performances play voices from " + files + " read with it");
    }
    const auto used = std::find_if(r.fromLibrary.begin(), r.fromLibrary.end(), [](int k) { return k >= 0; });
    if (used != r.fromLibrary.end()) {
        r.bank.libraryPresets = libraryBanks(r);
        r.report.warnings.push_back((r.bank.name.empty() ? "" : r.bank.name + ", ") + "performances play the TX802 presets of the library's \"" +
                                    r.pending.presets[(size_t)(used - r.fromLibrary.begin())].file + "\", kept with it");
    }
    // This file's banks, by number, for the performances in the files read after it.
    r.pending.numbered.resize(128);
    for (size_t v = 0; v < 128; ++v) {
        const bool now = r.numbered[v] >= 0;
        const int k = now ? r.numbered[v] : r.before[v];
        if (k >= 0 && r.bank.voices[(size_t)k].slot >= 0) r.pending.numbered[v] = {r.bank.voices[(size_t)k], now ? r.selected[v] : r.beforeSelected[v], r.bank.name};
    }
}

} // namespace fsvr::patchfiles
