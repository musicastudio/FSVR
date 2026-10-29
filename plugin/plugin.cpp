// FSVR on Hollow: the FS1R engine (fs1rLib) behind the FSVR skin. The processor is a controller for
// fsvr::Device and nothing more (AGENTS.md): every param is a sysex parameter change into the engine, as
// the skin's data/fs1r_sysex.json addresses it, and what the engine holds comes back out of its own bulk
// dumps. Around that sit the parts of the GUI the unit never had: the bank manager (factory banks and a
// user library of .syx files, library.h, and its Save, Import and right-click menus), the morph square
// (four corner voices per part, blended into the one voice the engine plays), Import Audio (audio_fseq.h)
// and the output monitor.
//
// Threads: the audio thread sends param changes and MIDI and renders; a worker loads patches, files and
// libraries and brings what the engine did back into the params. ctl guards what both touch (the model of
// the engine's bytes, the morph corners); the audio thread only ever try-locks it.
#include <hollow/hollow.h>
#include "core/core.h"
#include "embedded_skin.h"
#include "audio_fseq.h"
#include "fs1r.h"
#include "display/egview.h"
#include "engine/tuning.h"
#include "library.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace hollow {   // the factory banks, embedded by plugin/CMakeLists.txt
extern const EmbeddedFile kFactoryFiles[];
extern const size_t kFactoryFileCount;
}

namespace fsvr {

using hollow::Json;
using hollow::State;

// ---- small helpers ---------------------------------------------------------------------------------

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64(const uint8_t* d, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        for (int k = 0; k < 4; ++k) s += k <= (int)std::min<size_t>(n - i, 3) ? kB64[v >> (18 - 6 * k) & 63] : '=';
    }
    return s;
}

static std::vector<uint8_t> unb64(const std::string& s) {
    std::vector<uint8_t> out;
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        const char* p = std::strchr(kB64, c);
        if (!p || !c) continue;
        v = v << 6 | (uint32_t)(p - kB64);
        if ((bits += 6) >= 8) out.push_back((uint8_t)(v >> (bits -= 8)));
    }
    return out;
}

static const hollow::EmbeddedFile* factoryFile(const char* name) {
    for (size_t i = 0; i < hollow::kFactoryFileCount; ++i)
        if (!std::strcmp(hollow::kFactoryFiles[i].path, name)) return &hollow::kFactoryFiles[i];
    return nullptr;
}

static std::vector<uint8_t> factoryBytes(const char* name) {
    const hollow::EmbeddedFile* f = factoryFile(name);
    return f ? std::vector<uint8_t>(f->data, f->data + f->size) : std::vector<uint8_t>();
}

// The skin's own copy of a data file: HOLLOW_SKIN_DIR's when set (live editing), else the embedded one.
static std::string skinFile(const std::string& path) {
#if defined(_MSC_VER)
#pragma warning(suppress : 4996)
#endif
    const char* dir = std::getenv("HOLLOW_SKIN_DIR");
    if (dir && *dir) {
        std::vector<uint8_t> b;
        if (readFile(std::string(dir) + "/" + path, b)) return std::string(b.begin(), b.end());
    }
    for (size_t i = 0; i < hollow::kSkinFileCount; ++i)
        if (path == hollow::kSkinFiles[i].path) return std::string((const char*)hollow::kSkinFiles[i].data, hollow::kSkinFiles[i].size);
    return {};
}

// The factory voice number (the EPROM's order: PrA, PrB native, PrC..PrK DX) of a part's bank and
// program bytes; -1 for off and Int (engine: bank_voice_index).
static int factoryVoice(int bank, int program) {
    program = std::clamp(program, 0, 127);
    if (bank == 2 || bank == 3) return (bank - 2) * 128 + program;
    if (bank >= 4 && bank <= 12) return 256 + (bank - 4) * 128 + program;
    return -1;
}

// ---- the engine's bytes, and where each param lives in them ----------------------------------------

enum Area { System, Performance, Voice };

struct Model {
    uint8_t sys[76] = {}, perf[400] = {}, voice[4][608] = {};
    uint8_t* area(int a, int part) { return a == System ? sys : a == Performance ? perf : voice[part]; }
    // The engine's bulk dumps (Device::getState) into these; false if one was missing.
    bool parse(const std::vector<uint8_t>& s, std::vector<uint8_t>* fseq = nullptr) {
        int seen = 0;
        for (size_t i = 0; i + 11 < s.size();) {
            if (s[i] != 0xF0) { ++i; continue; }
            size_t j = i + 1;
            while (j < s.size() && s[j] != 0xF7) ++j;
            if (j >= s.size()) break;
            const int ah = s[i + 6];
            const uint8_t* d = s.data() + i + 9;
            const size_t n = j - 1 - (i + 9);
            if (ah == 0x00 && n >= 76) { std::memcpy(sys, d, 76); seen |= 1; }
            if (ah == 0x10 && n >= 400) { std::memcpy(perf, d, 400); seen |= 2; }
            if (ah >= 0x40 && ah <= 0x43 && n >= 608) { std::memcpy(voice[ah - 0x40], d, 608); seen |= 4 << (ah - 0x40); }
            if (ah == 0x60 && fseq) fseq->assign(d, d + n);
            i = j + 1;
        }
        return seen == 63;
    }
};

struct Field {
    int h = 0, m = 0, l = 0, shift = 0, width = 0, k = 0;
    bool wide = false;
    double x = 0;                  // "x": the byte is the plain value times x
    std::vector<int> raw;          // "raw": the byte of each label
    int area = -1, part = 0, off = 0;
    bool morph = false;            // a voice field that blends between the morph corners
    double min = 0;                // the param's minimum (labels index from it)

    int word(const uint8_t* b) const {
        int v = wide ? b[off] << 7 | b[off + 1] : b[off];
        return width ? v >> shift & ((1 << width) - 1) : v;
    }
    void set(uint8_t* b, int raw7) const {
        int v = wide ? b[off] << 7 | b[off + 1] : b[off];
        if (width) {
            const int mask = ((1 << width) - 1) << shift;
            v = (v & ~mask) | (raw7 << shift & mask);
        } else {
            v = raw7;
        }
        if (wide) { b[off] = (uint8_t)(v >> 7 & 0x7F); b[off + 1] = (uint8_t)(v & 0x7F); }
        else b[off] = (uint8_t)(v & 0x7F);
    }
    int toRaw(double plain) const {
        if (!raw.empty()) return raw[(size_t)std::clamp((int)std::lround(plain - min), 0, (int)raw.size() - 1)];
        if (x) return (int)std::lround(plain * x);
        return (int)std::lround(plain) + k;
    }
    double toPlain(int r) const {
        if (!raw.empty()) {
            for (size_t i = 0; i < raw.size(); ++i)
                if (raw[i] == r) return min + (double)i;
            return min;
        }
        if (x) return r / x;
        return r - k;
    }
    bool locate() {   // area and offset from the address
        if (h == 0 && m == 0 && l < 76) { area = System; off = l; }
        else if (h == 0x10 && m == 0) { area = Performance; off = l < 0x50 ? l : 80 + l - 0x50; }
        else if (h == 0x10 && m == 1) { area = Performance; off = 128 + l; }
        else if (h >= 0x30 && h <= 0x33 && m == 0) { area = Performance; off = 192 + 52 * (h - 0x30) + l; }
        else if (h >= 0x40 && h <= 0x43 && m == 0) { area = Voice; part = h - 0x40; off = l; }
        else if (h >= 0x60 && h <= 0x63 && m < 8) { area = Voice; part = h - 0x60; off = 112 + 62 * m + l; }
        else return false;
        return off + (wide ? 1 : 0) < (area == System ? 76 : area == Performance ? 400 : 608);
    }
};

static const char* const kCorner[4] = {"tl", "tr", "bl", "br"};   // A to D, the morph_edit values 0..3

// ---- the EG plots' curves ---------------------------------------------------------------------------

// The models behind the envelope plots, see "The envelopes" in docs/editor.md.
// display/egview.h calculates the curves; nothing is rendered.
// Inputs are param values: amp levels 0..99, pitch and filter levels -50..50, part offsets -64..63.
static const bool kEgModels = [] {
    auto model = [](int kind) {
        return [kind](const std::map<std::string, double>& in) {
            auto get = [&](const std::string& k, double zero) { return (in.count(k) ? in.at(k) : 0) + zero; };
            egview::Input e;
            e.egKind = kind;
            for (int i = 0; i < 4; i++) {
                e.L[i] = get("L" + std::to_string(i + 1), kind == egview::AMP ? 0 : 50);
                e.T[i] = get("T" + std::to_string(i + 1), 0);
            }
            e.init = get("init", 50);
            e.hold = get("hold", 0);
            e.timeScale = (int)get("timeScale", 0), e.velSens = (int)get("velocity", 0), e.range = (int)get("range", 0);
            static const char* const kAmp[4] = {"attack", "decay", "release", ""};
            static const char* const kPitch[4] = {"initOffset", "attackOffset", "releaseLevelOffset", "releaseOffset"};
            for (int i = 0; i < 4; i++) e.part[i] = (int)get(kind == egview::PITCH ? kPitch[i] : kAmp[i], 64);
            egview::EgCurve c = egview::curve(e);
            hollow::StageCurve out;
            out.t = std::move(c.t), out.v = std::move(c.v), out.keyOff = c.keyOff, out.corner = std::move(c.corner);
            out.lo = c.lo, out.hi = c.hi, out.db = kind == egview::AMP;
            return out;
        };
    };
    hollow::registerStageModel("fs1r.aeg", model(egview::AMP));
    hollow::registerStageModel("fs1r.peg", model(egview::PITCH));
    hollow::registerStageModel("fs1r.feg", model(egview::FILTER));
    return true;
}();

// ---- the processor ---------------------------------------------------------------------------------

class Fsvr final : public hollow::Processor {
public:
    explicit Fsvr(State& s) : st(s), base(s.size(), 0), sel(s.size(), 0), fields(s.size()) {
        buildFields();
        factory = parseSyx(factoryBytes("fs1r_performances.syx"), "Yamaha FS1R");
        factory.voices = parseSyx(factoryBytes("fs1r_presets.syx"), "").voices;
        factory.fseqs = parseSyx(factoryBytes("fs1r_fseqs.syx"), "").fseqs;
        lib.builtin = builtinBanks();
        dev.setSampleRate(fsvr::ENGINE_RATE);
        // A fresh instance is what its params say (FSVR's init performance and voice): the engine takes
        // them as whole bulks, over its own init images for the bytes no param names.
        std::vector<uint8_t> dump;
        dev.getState(dump);
        model.parse(dump);
        for (size_t i = 0; i < st.size(); ++i) {
            base[i] = sel[i] = st.get(i);
            if (fields[i].area >= 0) fields[i].set(model.area(fields[i].area, fields[i].part), fields[i].toRaw(base[i]));
        }
        for (int p = 0; p < 4; ++p)
            for (auto& c : corners[p]) std::memcpy(c.data(), model.voice[p], 608);
        pushModel();
        loadsSeen = st.loads();
        lib.rescan(true);
        libraryError();
        readOptions();
        // What the LCD and the browser name is what plays: the worker's first look loads the Init Patch (Options), else
        // that factory performance (A001), unless a session arrives first. The standalone's app brings back the state
        // it closed with as a session: Load Last Patch keeps it, and otherwise the Init Patch plays over it (restore).
        if (std::filesystem::exists(std::filesystem::u8path(optionsFile("Init.fsvr")))) startup = optionsFile("Init.fsvr");
        else sel[(size_t)perfProgram] = std::nan("");
        st.setData("library.dir", (std::filesystem::u8path(lib.dir) / "").u8string());
        st.setData("fsvr.version", FSVR_VERSION);   // the About box's header
        st.setData("fsvr.contributors", contributors());
        followParams();
        writeLists();
        names();
        worker = std::thread([this] { run(); });
    }

    ~Fsvr() override {
        {
            std::lock_guard<std::mutex> g(wakeLock);
            quit = true;
        }
        wake.notify_all();
        worker.join();
    }

    void prepare(double rate, int) override {
        dev.setSampleRate(rate);
        hostRate = rate;
    }

    void midi(int frame, const uint8_t* b, int n) override {
        if (n <= 0 || events >= kEvents || used + (size_t)n > arena.size()) return;
        std::memcpy(arena.data() + used, b, (size_t)n);
        ev[events++] = {frame, used, n};
        used += (size_t)n;
    }

    void process(const float* const*, float* const* out, int frames) override {
        const tuning::NoDenormals flush;
        if (const int s = (int)std::lround(get(speed)); s != speedSent) dev.setSpeed(speedSent = s);
        if (const int f = (int)std::lround(get(filter32)); f != filter32Sent) dev.setFilterPoly32((filter32Sent = f) != 0);
        if (const int n = (int)std::lround(get(polyOpt)); n != polySent) dev.setPolyphony(kPoly[polySent = std::clamp(n, 0, 3)]);
        std::unique_lock<std::mutex> lk(ctl, std::try_to_lock);
        const unsigned loads = st.loads();
        const bool live = lk.owns_lock() && !(loads & 1) && loads == loadsSeen;
        if (live) syncParams();
        int at = 0;
        for (int e = 0; e < events; ++e) {
            const int t = std::clamp(ev[e].frame, at, frames);
            if (t > at) dev.process(out[0] + at, out[1] + at, t - at);
            at = t;
            handle(arena.data() + ev[e].at, ev[e].n, live);
        }
        events = 0;
        used = 0;
        if (at < frames) dev.process(out[0] + at, out[1] + at, frames - at);
        lk = {};
        const size_t r = ringAt;
        for (int i = 0; i < frames; ++i) ring[(r + (size_t)i) % ring.size()] = 0.5f * (out[0][i] + out[1][i]);
        ringAt = (r + (size_t)frames) % ring.size();
        st.setVoices(dev.activeVoices());
        while (dev.nextMidiOut(outMsg))
            if (sendMidi) sendMidi(std::max(frames - 1, 0), outMsg.data(), (int)outMsg.size());
    }

    // The engine and the morph corners go into the saved instance as text data.
    void saving() override {
        std::lock_guard<std::mutex> g(ctl);
        step();   // a session or a program change the worker has not reached yet is part of what is saved
        std::vector<uint8_t> dump;
        dev.getState(dump);
        st.setData("fsvr.engine", b64(dump.data(), dump.size()));
        std::vector<uint8_t> c;
        for (auto& part : corners)
            for (auto& v : part) c.insert(c.end(), v.begin(), v.end());
        st.setData("fsvr.morph", b64(c.data(), c.size()));
    }

private:
    State& st;
    fsvr::Device dev;
    std::vector<double> base;          // each param's value as the engine last had it (the audio thread's)
    std::vector<double> sel;           // and as the worker last acted on it (loads, pages, requests)
    std::vector<Field> fields;
    std::vector<int> fieldParams;      // params with a field, in skin order
    std::vector<int> voiceParams[4];   // a part's voice params
    Model model;
    std::array<std::array<uint8_t, 608>, 4> corners[4];
    int lastCorner[4] = {0, 0, 0, 0};  // the corner the pages show with Edit All
    Bank factory;
    Library lib;
    std::mutex ctl;
    unsigned loadsSeen = 0;
    double hostRate = fsvr::ENGINE_RATE;

    // params by id
    int P(const std::string& id) const { return st.indexOf(id); }
    int perfProgram = -1, perfBank = -1, perfUser = -1, fseqBank = -1, fseqNumber = -1, fseqUser = -1, fseqPart = -1, fseqPos = -1, panic = -1;
    int partBank[4], partProgram[4], partUser[4], morphX[4], morphY[4], jitterX[4], jitterY[4], morphSeed[4], morphEdit[4];
    int browseBank = -1, browseCategory = -1, browsePerf = -1, browseFseq = -1, browseVoice[4], knob[4];
    int browseFocus = -1, partOn[4];   // the Parts page's voice field (the part, from 1) and its lamps
    int perfCategory = -1, edited = -1, saveBank = -1, itemCategory = -1, itemReadonly = -1, itemKind = -1, bankReadonly = -1;
    // The user performance, each part's user voice and the user Fseq loaded, by U number: what the browser lights.
    // The params hold the same numbers up to their 16,384, a host's range; the library has no such limit.
    int userPerf = 0, userVoice[4] = {0, 0, 0, 0}, userFseq = 0;
    std::string voiceShown[4];         // each part's voice name as last published (part.voice.pN)
    std::string lcdShown;              // the LCD's performance line as last published (live.lcd.perf)
    // Options (page_options): kept for every instance, not with a session, in <Documents>/FSVR/Settings.json under
    // these keys, then "scale", the GUI scale a new window opens at
    static constexpr std::pair<const char*, const char*> kOptions[] = {
        {"keysTop", "gui.keys_top"}, {"egModern", "gui.eg_modern"}, {"loadLast", "opt.load_last"}, {"speed", "opt.speed"}, {"filter32", "opt.filter32"},
        {"poly", "opt.poly"}};
    static constexpr size_t kScale = sizeof kOptions / sizeof kOptions[0];
    static constexpr int kPoly[4] = {32, 64, 96, 128};   // opt.poly's steps, the voices Device::setPolyphony gets
    double options[kScale + 1] = {};   // as Settings.json has them
    int loadLast = -1, speed = -1, filter32 = -1, polyOpt = -1;
    int speedSent = -1, filter32Sent = -1, polySent = -1;   // the audio thread's: the speed, filter voices and polyphony the engine has
    bool standalone = st.data("live.standalone") == "1";   // the format layer says so before we start
    std::string startup;               // the .fsvr a fresh instance plays first, if not A001
    bool restoredOnce = false;         // a session has come back since we started
    const std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();

    // MIDI waiting for its place in the block
    static constexpr int kEvents = 1024;
    struct Event { int frame; size_t at; int n; };
    Event ev[kEvents];
    int events = 0;
    std::vector<uint8_t> arena = std::vector<uint8_t>(1 << 17);
    size_t used = 0;
    std::vector<uint8_t> outMsg = std::vector<uint8_t>(64);
    std::array<uint8_t, 619> voiceBulk;
    int bankMsb[16], bankLsb[16];
    int perfBankSel = -1;              // the performance bank a bank select chose (0x40 Int .. 0x43 PrC)
    uint32_t noise[4] = {1, 1, 1, 1};  // per part, the morph jitter's generator
    int seedSeen[4] = {-1, -1, -1, -1};
    std::atomic<int> lastNote{-1};
    std::atomic<bool> adoptWanted{false};
    std::atomic<int> hostVoices{0};    // parts whose voice the host sent since the worker last adopted, a bit each

    // the output, for the harmonic display
    std::vector<float> ring = std::vector<float>(8192);
    std::atomic<size_t> ringAt{0};

    std::thread worker;
    std::mutex wakeLock;
    std::condition_variable wake;
    bool quit = false;
    bool notify = false;               // the worker changed params; the host re-reads them
    std::vector<int> browsed[3];       // the browsed bank's rows as U numbers: performances, voices, Fseqs
    std::map<std::pair<int, int>, int> listed;   // factory voices by bank and number: their category in the browser
    std::vector<uint8_t> fseqShown;    // the Fseq the page's display last had
    int messageTicks = 0;              // worker ticks until the status line clears
    std::string libError;              // the library's read error the status line last showed
    uint8_t waveVoice[4][608] = {};    // each part's voice as the operator panel's waves were last drawn from it
    bool waveDrawn[4] = {};
    float scopeShown[32] = {};         // the harmonics the monitor last published
    bool scopeDrawn = false;
    std::atomic<int> userPerfs{0};     // lib.perfs.size() for the audio thread, which never touches the library itself

    // ---- setup -----------------------------------------------------------------------------------

    void buildFields() {
        Json table;
        std::string err;
        if (!hollow::parseJson(skinFile("data/fs1r_sysex.json"), table, &err)) std::fprintf(stderr, "FSVR: fs1r_sysex.json: %s\n", err.c_str());
        for (auto& m : table.members) {
            const int i = st.indexOf(m.first);
            if (i < 0) continue;
            const Json& j = m.second;
            Field f;
            f.h = j["a"][0].integer();
            f.m = j["a"][1].integer();
            f.l = j["a"][2].integer();
            f.shift = j["s"].integer();
            f.width = j["w"].integer();
            f.wide = j["wide"].flag();
            f.k = j["k"].integer();
            f.x = j["x"].num();
            for (auto& r : j["raw"].items) f.raw.push_back(r.integer());
            f.min = st.def((size_t)i).min;
            if (!f.locate()) continue;
            if (f.area == Voice) {
                // Switches and choices come from the nearest corner, amounts blend (docs/editor.md, The morph square).
                const std::string& id = m.first;
                const auto& labels = st.def((size_t)i).labels;
                const bool choice = (!labels.empty() && labels.size() <= 24) || id.find(".alg.") != std::string::npos ||
                                    id.find(".fseq_track.") != std::string::npos || id.find("_ctrl.") != std::string::npos && id.find(".op.") != std::string::npos;
                f.morph = !choice;
                voiceParams[f.part].push_back(i);
            }
            fields[(size_t)i] = f;
            fieldParams.push_back(i);
        }
        perfProgram = P("perf.program"); perfBank = P("perf.bank"); perfUser = P("perf.user");
        fseqBank = P("fseq.bank"); fseqNumber = P("fseq.number"); fseqUser = P("fseq.user"); fseqPart = P("fseq.part"); fseqPos = P("fseq.position");
        panic = P("gui.panic");
        perfCategory = P("perf.category"); edited = P("gui.edited"); saveBank = P("save.bank");
        loadLast = P("opt.load_last"); speed = P("opt.speed"); filter32 = P("opt.filter32"); polyOpt = P("opt.poly");
        itemCategory = P("item.category"); itemReadonly = P("item.readonly"); itemKind = P("item.kind"); bankReadonly = P("bank.readonly");
        browseBank = P("browse.bank"); browseCategory = P("browse.category"); browsePerf = P("browse.perf"); browseFseq = P("browse.fseq");
        browseFocus = P("browse.focus");
        for (int p = 0; p < 4; ++p) {
            const std::string s = "p" + std::to_string(p + 1);
            partBank[p] = P("part.bank." + s); partProgram[p] = P("part.program." + s); partUser[p] = P("part.user." + s);
            morphX[p] = P("part.morph_x." + s); morphY[p] = P("part.morph_y." + s);
            jitterX[p] = P("part.morph_jitter_x." + s); jitterY[p] = P("part.morph_jitter_y." + s);
            morphSeed[p] = P("part.morph_seed." + s); morphEdit[p] = P("part.morph_edit." + s);
            browseVoice[p] = P("browse.voice." + s);
            partOn[p] = P("part.on." + s);
            knob[p] = P("knob." + std::to_string(p + 1));
        }
        for (int c = 0; c < 16; ++c) bankMsb[c] = bankLsb[c] = -1;
    }

    double get(int i) const { return i >= 0 ? st.get((size_t)i) : 0; }
    void putUser(int param, int& loaded, int n) { loaded = n; put(param, n); }   // a U number, past the param's range too
    void put(int i, double v) {   // a value read back from the engine: it has it already, and no action follows
        if (i < 0) return;
        if (i == browseBank && v != get(i)) st.setData("live.browse.filter", "");   // a bank the processor opens (an import, a save) opens unfiltered too
        st.set((size_t)i, v);
        base[(size_t)i] = sel[(size_t)i] = st.get((size_t)i);
        notify = true;
    }
    void choose(int i, double v) {   // a value the processor picks as a user would: the engine hears it like any edit
        if (i < 0) return;
        st.set((size_t)i, v);
        sel[(size_t)i] = st.get((size_t)i);
        notify = true;
    }
    // The morph square is off until it works: every edit reaches all four corners (Edit All), so they stay
    // one voice and the blend is that voice wherever the position params sit.
    // int edit(int p) const { return std::clamp((int)std::lround(get(morphEdit[p])), 0, 4); }
    int edit(int) const { return 4; }

    // ---- to the engine ------------------------------------------------------------------------------

    // dev_ is the unit's device number, its own or 16 for all: the one the engine has, which for a change of
    // the device number itself is the old one, since the engine refuses a message addressed to the new.
    void send(const Field& f, const uint8_t* bytes) { send(f, bytes, model.sys[0x49]); }
    void send(const Field& f, const uint8_t* bytes, int dev_) {
        const int v = f.wide ? bytes[f.off] << 7 | bytes[f.off + 1] : bytes[f.off];
        const uint8_t m[10] = {0xF0, 0x43, (uint8_t)(0x10 | (dev_ < 16 ? dev_ : 0)), 0x5E, (uint8_t)f.h, (uint8_t)f.m, (uint8_t)f.l,
                               (uint8_t)(v >> 7 & 0x7F), (uint8_t)(v & 0x7F), 0xF7};
        dev.sendMidi(m, 10);
    }

    void sendVoice(int p, const uint8_t* v) {   // one bulk to the part's voice: the engine decodes it once
        voiceBulk = {0xF0, 0x43, 0x00, 0x5E, 0x04, 0x60, (uint8_t)(0x40 + p), 0x00, 0x00};
        std::memcpy(voiceBulk.data() + 9, v, 608);
        int sum = 0;
        for (size_t i = 4; i < 617; ++i) sum += voiceBulk[i];
        voiceBulk[617] = (uint8_t)(-sum & 0x7F);
        voiceBulk[618] = 0xF7;
        dev.sendMidi(voiceBulk.data(), voiceBulk.size());
    }

    void pushModel() {   // the whole model as bulks
        for (auto& m : {bulk(0x00, 0, 0, model.sys, 76), bulk(0x10, 0, 0, model.perf, 400)}) dev.sendMidi(m.data(), m.size());
        for (int p = 0; p < 4; ++p) sendVoice(p, model.voice[p]);
    }

    // The morph corners of part p blended at (x, y) in 0..1, y up: amounts weighted by nearness to each
    // corner, everything else from the nearest.
    void blend(int p, double x, double y, uint8_t* out) const {
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        const double w[4] = {(1 - x) * y, x * y, (1 - x) * (1 - y), x * (1 - y)};
        const int near = (int)(std::max_element(w, w + 4) - w);
        std::memcpy(out, corners[p][near].data(), 608);
        for (int i : voiceParams[p]) {
            const Field& f = fields[(size_t)i];
            if (!f.morph) continue;
            double v = 0;
            for (int c = 0; c < 4; ++c) v += w[c] * f.word(corners[p][c].data());
            f.set(out, (int)std::lround(v));
        }
    }
    void position(int p, double& x, double& y) const { x = get(morphX[p]) / 100; y = get(morphY[p]) / 100; }
    bool cornersDiffer(int p) const {
        for (int c = 1; c < 4; ++c)
            if (corners[p][c] != corners[p][0]) return true;
        return false;
    }

    // The part's blended voice to the engine, when it differs from what the engine has.
    void remorph(int p) {
        uint8_t v[608];
        double x, y;
        position(p, x, y);
        blend(p, x, y, v);
        if (std::memcmp(v, model.voice[p], 608) == 0) return;
        std::memcpy(model.voice[p], v, 608);
        sendVoice(p, v);
    }

    // Audio thread: every param that moved since the engine last heard of it.
    void syncParams() {
        bool morphed[4] = {false, false, false, false};
        for (int i : fieldParams) {
            const double v = st.get((size_t)i);
            if (v == base[(size_t)i]) continue;
            base[(size_t)i] = v;
            if (edited >= 0) st.set((size_t)edited, 1);   // the performance differs from what was loaded or saved
            const Field& f = fields[(size_t)i];
            const int raw = f.toRaw(v);
            if (f.area != Voice) {
                uint8_t* b = model.area(f.area, 0);
                const int heard = model.sys[0x49];
                f.set(b, raw);
                send(f, b, heard);
                continue;
            }
            const int p = f.part, e = edit(p);
            for (int c = 0; c < 4; ++c)
                if (e == 4 || e == c) f.set(corners[p][c].data(), raw);
            if (!f.morph) {   // a choice: the nearest corner's, the rest by the blend
                morphed[p] = true;
                continue;
            }
            double x, y;
            position(p, x, y);
            const double w[4] = {(1 - x) * y, x * y, (1 - x) * (1 - y), x * (1 - y)};
            double b = 0;
            for (int c = 0; c < 4; ++c) b += w[c] * f.word(corners[p][c].data());
            if ((int)std::lround(b) == f.word(model.voice[p])) continue;
            f.set(model.voice[p], (int)std::lround(b));
            send(f, model.voice[p]);
        }
        for (int p = 0; p < 4; ++p) {
            // the morph square is off: a position change moves nothing
            // for (int i : {morphX[p], morphY[p]})
            //     if (i >= 0 && st.get((size_t)i) != base[(size_t)i]) {
            //         base[(size_t)i] = st.get((size_t)i);
            //         morphed[p] = true;
            //     }
            if (morphed[p]) remorph(p);
            if (knob[p] >= 0 && st.get((size_t)knob[p]) != base[(size_t)knob[p]]) {   // KN1..KN4 on their control numbers
                base[(size_t)knob[p]] = st.get((size_t)knob[p]);
                const int cc = model.sys[0x16 + p], v = std::clamp((int)std::lround(base[(size_t)knob[p]]), 0, 127);
                for (int ch : listeningChannels()) {
                    const uint8_t m[3] = {(uint8_t)(0xB0 | ch), (uint8_t)cc, (uint8_t)v};
                    dev.sendMidi(m, 3);
                }
            }
        }
    }

    // ---- MIDI --------------------------------------------------------------------------------------

    int perfChannel() const { return model.sys[0x09]; }   // 0..15, 16 all, 127 off
    bool listens(int p, int ch) const {                    // Synth::part_listens
        const int rc = model.perf[192 + 52 * p + 4], mx = model.perf[192 + 52 * p + 3], pc = perfChannel();
        if (rc == 0x7F) return false;
        if (rc == 0x10) return pc == 0x10 || (pc != 0x7F && ch == pc);
        if (p < 2 && rc <= 0x0F && mx <= 0x0F && mx > rc) return ch >= rc && ch <= mx;   // Rcv Max, parts 1 and 2 (Part::rcvMax)
        return rc == ch;
    }
    std::vector<int> listeningChannels() const {
        std::vector<int> out;
        for (int ch = 0; ch < 16; ++ch)
            for (int p = 0; p < 4; ++p)
                if (listens(p, ch)) { out.push_back(ch); break; }
        return out;
    }

    void handle(const uint8_t* b, int n, bool live) {
        const int st8 = b[0] & 0xF0, ch = b[0] & 0x0F;
        if (b[0] == 0xF0) {                       // a patch or parameter change from an editor
            if (n >= 9 && b[1] == 0x43 && b[3] == 0x5E) {   // a part's voice, bulk or parameter change, at address high 40..43
                const int ah = (b[2] & 0xF0) == 0x10 ? b[4] : b[6];
                if (ah >= 0x40 && ah <= 0x43) hostVoices.fetch_or(1 << (ah - 0x40));
            }
            dev.sendMidi(b, (size_t)n);
            adoptWanted = true;
            return;
        }
        if (st8 == 0x90 && n >= 3 && b[2]) {
            lastNote = b[1];
            // for (int p = 0; p < 4 && live; ++p) jitter(p, ch);   // the morph square is off
        }
        if (st8 == 0xB0 && n >= 3) {
            const int cc = b[1], v = b[2];
            if (cc == 0) bankMsb[ch] = v;
            if (cc == 32) {                        // the engine's own rule (midi.cpp control_change)
                bankLsb[ch] = v;
                if (model.sys[0x12] && bankMsb[ch] == 0x3F) {
                    if (v >= 0x40 && v <= 0x43) perfBankSel = v;
                    for (int p = 0; p < 4; ++p)
                        if (v <= 0x0B && listens(p, ch)) st.set((size_t)partBank[p], v + 1);
                }
            }
            if (cc != 64 && cc != 11) adoptWanted = true;   // most write a part byte; the wheel moves the bytes its sets edit
        }
        // A controller source moves the part bytes its sets edit (destinations 17 to 33, Synth::ctrl_write), which
        // the editor then shows: the wheel above, and the bend and aftertouch here.
        if (st8 == 0xE0 || st8 == 0xD0 || st8 == 0xA0) adoptWanted = true;
        if (st8 == 0xC0 && n >= 2) {
            programChange(ch, b[1]);
            return;                               // the engine loads programs only out of an EPROM
        }
        dev.sendMidi(b, (size_t)n);
    }

    // Program change as the unit takes it, out of the factory banks and the user library: on the
    // performance channel in Performance mode a performance of the bank selected (Int = the user's), in
    // Multi mode a voice for each part on the channel, from its bank.
    void programChange(int ch, int prog) {
        if (!model.sys[0x13]) return;              // program change receive off
        const int pc = perfChannel();
        if (model.sys[0x08] == 0) {
            if (!(pc == 0x10 || (pc != 0x7F && ch == pc))) return;
            int bank = perfBankSel;
            if (bank < 0x40) bank = get(perfBank) == 1 ? 0x40 : 0x41 + (int)get(perfProgram) / 128;
            if (bank == 0x40) {
                if (prog + 1 <= userPerfs.load()) { st.set((size_t)perfUser, prog + 1); st.set((size_t)perfBank, 1); }
            } else {
                st.set((size_t)perfProgram, (bank - 0x41) * 128 + prog);
                st.set((size_t)perfBank, 0);
            }
            return;
        }
        for (int p = 0; p < 4; ++p) {
            if (!listens(p, ch) || get(partBank[p]) == 0) continue;
            if (get(partBank[p]) == 1) st.set((size_t)partUser[p], prog + 1);
            else st.set((size_t)partProgram[p], prog + 1);
        }
    }

    // Morph random: each note nudges the part's position by up to the random amounts, from its seed.
    void jitter(int p, int ch) {
        const double jx = get(jitterX[p]) / 100, jy = get(jitterY[p]) / 100;
        if ((jx <= 0 && jy <= 0) || !listens(p, ch) || !cornersDiffer(p)) return;
        const int seed = (int)get(morphSeed[p]);
        if (seed != seedSeen[p]) { seedSeen[p] = seed; noise[p] = 2654435761u * (uint32_t)(seed + 1); }
        auto rnd = [&] { noise[p] = noise[p] * 1664525u + 1013904223u; return (noise[p] >> 8) / double(1 << 24) * 2 - 1; };
        double x, y;
        position(p, x, y);
        const double dx = rnd() * jx, dy = rnd() * jy;
        uint8_t v[608];
        blend(p, x + dx, y + dy, v);
        if (std::memcmp(v, model.voice[p], 608) == 0) return;
        std::memcpy(model.voice[p], v, 608);
        sendVoice(p, v);
    }

    // ---- the worker --------------------------------------------------------------------------------

    void run() {
        auto tick = std::chrono::steady_clock::now();
        int n = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> w(wakeLock);
                if (wake.wait_until(w, tick += std::chrono::milliseconds(33), [this] { return quit; })) return;
            }
            {
                std::lock_guard<std::mutex> g(ctl);
                step();
            }
            requests();
            if (fseqPos >= 0) st.set((size_t)fseqPos, dev.fseqPosition());   // the Fseq page's playback line
            if (++n % 30 == 0) {   // under ctl: a rescan rebuilds the banks a host's save reads
                std::lock_guard<std::mutex> g(ctl);
                if (lib.rescan()) writeLists();
                libraryError();
            }
            if (n % 10 == 0) watchOptions();
            if (n % 2 == 0) harmonics();
            if (n % 3 == 1) waves();
            if (messageTicks && --messageTicks == 0) st.setData("live.message", "");
            if (notify && n % 8 == 0) {   // at most four times a second: a rescan of 3,000 params is not free for a host
                notify = false;
                if (paramsChanged) paramsChanged();
            }
        }
    }

    void step() {   // under ctl: a session that arrived, what the engine did, what the params ask for
        const unsigned loads = st.loads();
        if (!(loads & 1) && loads != loadsSeen) restore(loads);
        if (!startup.empty()) {
            // A performance the host chose before this first tick is what the instance plays; the Init Patch
            // would put the user bank under it, and the selectors below would then look for the host's
            // number among the user's performances.
            bool chosen = false;
            for (int i : {perfProgram, perfBank, perfUser}) chosen |= i >= 0 && st.get((size_t)i) != sel[(size_t)i];
            if (!chosen) playInit(startup);
            startup.clear();
        }
        if (adoptWanted.exchange(false)) adopt(true);
        selectors();
        voiceNames();
        lcdPerf();
    }

    // The LCD's performance line: the preset it was loaded as while the engine still holds that name, else the
    // engine's own name, so a performance bulk from the host or a rename shows what plays.
    void lcdPerf() {
        const std::string n = perfName(), user = st.data("perf.user.name");
        std::string s = n;
        if (get(perfBank) == 1) {
            if (trimmed(user.substr(0, 12)) == n) s = user;   // a .fsvr name can run past the 12 bytes
        } else {
            const std::string label = st.text((size_t)perfProgram, get(perfProgram));   // "A001 Zap !"
            if (label.size() > 5 && trimmed(label.substr(5)) == n) s = label;
        }
        if (s != lcdShown) st.setData("live.lcd.perf", lcdShown = s);
    }

    // The user items a session or a fresh instance names, from its params (which stop at 16,384).
    void followParams() {
        userPerf = (int)get(perfUser);
        userFseq = (int)get(fseqUser);
        for (int p = 0; p < 4; ++p) userVoice[p] = (int)get(partUser[p]);
    }

    // Each part's voice by its name, "off" for a part with no voice bank, its bank and number for one whose voice
    // has no name (an empty internal slot reads "Int 14"): the Parts page's Voice and the LCD
    // show the voice itself, whatever bank or number it came from.
    void voiceNames() {
        for (int p = 0; p < 4; ++p) {
            std::string n = "off";
            if (get(partBank[p]) != 0) {
                n.clear();
                for (int k = 0; k < 10; ++k) n += model.voice[p][k] >= 32 && model.voice[p][k] < 127 ? (char)model.voice[p][k] : ' ';
                while (!n.empty() && n.back() == ' ') n.pop_back();
                if (n.empty()) {   // no voice came back named (an empty internal slot): the bank and number, as the browser writes them
                    const int b = (int)get(partBank[p]), num = (int)get(partProgram[p]);
                    char code[16];
                    if (b == 1) std::snprintf(code, sizeof code, "Int %d", num);
                    else std::snprintf(code, sizeof code, "%c%03d", 'A' + b - 2, num);
                    n = code;
                }
            }
            if (n != voiceShown[p]) st.setData("part.voice.p" + std::to_string(p + 1), voiceShown[p] = n);
        }
    }

    // The About box's credit line for the repository's other contributors (plugin/generated/contributors.txt).
    static std::string contributors() {
        const std::vector<uint8_t> b = factoryBytes("contributors.txt");
        std::string s(b.begin(), b.end());
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        return s;
    }

    // A session came back: the engine as it was saved, the corners too, and every param as the host holds it.
    void restore(unsigned loads) {
        loadsSeen = loads;
        startup.clear();
        // clap-wrapper's standalone loads the state it saved when it last closed, as it starts (entry.cpp); a later
        // restore is a Load State the user chose, and in a host the session is the session.
        // ponytail: "as it starts" is the first restore within 3 s of our construction
        const bool reopened = standalone && !restoredOnce && std::chrono::steady_clock::now() - created < std::chrono::seconds(3);
        restoredOnce = true;
        for (auto& v : voiceShown) v.assign(1, '\0');   // the session brought back the names it saved, maybe stale: publish ours again
        lcdShown.assign(1, '\0');                       // State::load cleared live.lcd.perf: publish it again
        const std::vector<uint8_t> eng = unb64(st.data("fsvr.engine"));
        // const std::vector<uint8_t> mor = unb64(st.data("fsvr.morph"));
        for (size_t i = 0; i < st.size(); ++i) sel[i] = st.get(i);
        if (!eng.empty()) {
            dev.allNotesOff();
            dev.setState(eng.data(), eng.size());
            std::vector<uint8_t> fseq;
            model.parse(eng, &fseq);
            showFseq(fseq);
            for (size_t i = 0; i < st.size(); ++i) base[i] = st.get(i);
        } else {
            for (double& b : base) b = std::nan("");   // an older session: send every param again
        }
        for (int p = 0; p < 4; ++p)
            for (int c = 0; c < 4; ++c)
                // the morph square is off: a session's saved corners give way to the voice it plays
                // if (mor.size() == 4 * 4 * 608) std::memcpy(corners[p][c].data(), mor.data() + (size_t)(p * 4 + c) * 608, 608);
                // else
                std::memcpy(corners[p][c].data(), model.voice[p], 608);
        st.setData("live.message", "");
        st.setData("fsvr.version", FSVR_VERSION);   // a session saved by another version carries its number
        st.setData("fsvr.contributors", contributors());
        followParams();
        writeLists();
        names();
        applyOptions();
        if (reopened && get(loadLast) == 0) playInitPatch();   // Load Last Patch off: the standalone opens on the Init Patch
    }

    // What the engine holds, back into the params: every field whose bytes changed under us (a load, a
    // bulk from the host, a controller that writes a part byte). A changed voice goes into the morph
    // corners the part edits (all four with Edit All) and the part plays the blend again.
    void adopt(bool fromHost = false) {
        std::vector<uint8_t> dump, fseq;
        dev.getState(dump);
        Model now;
        if (!now.parse(dump, &fseq)) return;
        showFseq(fseq);
        // A part whose bank or number the host changed with no voice behind it: a performance bulk or parameter
        // change, or a bank select. The unit loads that voice from its memory, and the engine only from an
        // EPROM, which the plug-in has not got. The plug-in's own loads bring their voices themselves.
        const int sent = fromHost ? hostVoices.exchange(0) : ~0;
        bool bare[4] = {};
        for (int p = 0; p < 4; ++p) {
            const int at = 192 + 52 * p;
            bare[p] = !(sent & 1 << p) && (now.perf[at + 1] != model.perf[at + 1] || now.perf[at + 2] != model.perf[at + 2]) &&
                      std::memcmp(now.voice[p], model.voice[p], 608) == 0;
        }
        for (int i : fieldParams) {
            const Field& f = fields[(size_t)i];
            if (f.area == Voice) continue;
            const int was = f.word(model.area(f.area, 0)), is = f.word(now.area(f.area, 0));
            if (was != is) put(i, f.toPlain(is));
        }
        std::memcpy(model.sys, now.sys, 76);
        std::memcpy(model.perf, now.perf, 400);
        bool loaded = false;
        for (int p = 0; p < 4; ++p) {
            if (!bare[p]) continue;
            const int vb = now.perf[192 + 52 * p + 1], prog = now.perf[192 + 52 * p + 2], fv = factoryVoice(vb, prog);
            if (fv >= 0 && fv < (int)factory.voices.size()) {
                loadVoiceItem(factory.voices[(size_t)fv], p);
                loaded = true;
            } else if (const Item* u = vb == 1 ? lib.voice(prog + 1) : nullptr) {
                loadVoiceItem(*u, p);
                putUser(partUser[p], userVoice[p], prog + 1);
                loaded = true;
            }
        }
        if (loaded) {   // the voices just loaded, read back as any other change is
            adopt();
            return;
        }
        st.setData("perf.name", perfName());
        for (int p = 0; p < 4; ++p) {
            if (std::memcmp(now.voice[p], model.voice[p], 608) == 0) continue;
            const int e = edit(p);
            for (int c = 0; c < 4; ++c)
                if (e == 4 || e == c) std::memcpy(corners[p][c].data(), now.voice[p], 608);
            std::memcpy(model.voice[p], now.voice[p], 608);
            remorph(p);
            show(p);
        }
    }

    // The Fseq page's display draws the loaded Fseq from text data fseq.display, an entry in the form of
    // the skin's data/fseqs.json: per frame the pitch and each track's frequency (the word's high byte) and
    // level, as hex strings of one byte a frame.
    void showFseq(const std::vector<uint8_t>& d) {
        if (d == fseqShown) return;
        fseqShown = d;
        if (d.size() < 32 + 50) { st.setData("fseq.display", ""); return; }
        const int total = (int)(d.size() - 32) / 50, end = d[0x1E] << 7 | d[0x1F];
        const int n = end ? std::min(total, end + 1) : total;
        auto hex = [&](int at) {
            std::string h;
            char b[3];
            for (int k = 0; k < n; ++k) { std::snprintf(b, sizeof b, "%02x", d[32 + (size_t)k * 50 + (size_t)at]); h += b; }
            return "\"" + h + "\"";
        };
        auto tracks = [&](int at) {
            std::string t = "[";
            for (int i = 0; i < 8; ++i) t += (i ? "," : "") + hex(at + i);
            return t + "]";
        };
        st.setData("fseq.display", "{\"frames\":" + std::to_string(n) + ",\"pitch\":" + hex(0) + ",\"vfreq\":" + tracks(2) + ",\"vlevel\":" + tracks(0x12) +
                                        ",\"ufreq\":" + tracks(0x1A) + ",\"ulevel\":" + tracks(0x2A) + "}");
    }

    // The pages show the corner a part edits: the picked one, or with Edit All the last one picked.
    void show(int p) {
        const int e = edit(p), c = e < 4 ? e : lastCorner[p];
        for (int i : voiceParams[p]) put(i, fields[(size_t)i].toPlain(fields[(size_t)i].word(corners[p][c].data())));
        names();
    }

    void names() {
        for (int p = 0; p < 4; ++p)
            for (int c = 0; c < 4; ++c) {
                std::string n;
                for (int k = 0; k < 10; ++k) n += corners[p][c][(size_t)k] >= 32 && corners[p][c][(size_t)k] < 127 ? (char)corners[p][c][(size_t)k] : ' ';
                while (!n.empty() && n.back() == ' ') n.pop_back();
                st.setData("morph.p" + std::to_string(p + 1) + "." + kCorner[c], n);
            }
    }

    bool moved(int i) {   // a param the worker acts on changed since it last looked
        if (i < 0 || st.get((size_t)i) == sel[(size_t)i]) return false;
        sel[(size_t)i] = st.get((size_t)i);
        return true;
    }

    void selectors() {
        bool perf = false, fseq = false;
        for (int i : {perfProgram, perfBank, perfUser}) perf |= moved(i);
        for (int i : {fseqBank, fseqNumber, fseqUser}) fseq |= moved(i);
        if (perf) {
            if (get(perfBank) == 1) loadUserPerf(userPerf = (int)get(perfUser));
            else loadFactoryPerf((int)get(perfProgram));
        }
        for (int p = 0; p < 4; ++p) {
            if (moved(partOn[p])) lamp(p);
            bool voice = false;
            for (int i : {partBank[p], partProgram[p], partUser[p]}) voice |= moved(i);
            if (voice && !perf) {
                if (get(partBank[p]) == 1) userVoice[p] = (int)get(partUser[p]);
                loadPartVoice(p);
            }
            // the lamp follows the bank, whatever set it: a performance, the browser, the host
            if (partOn[p] >= 0 && st.get((size_t)partOn[p]) == sel[(size_t)partOn[p]] && (get(partOn[p]) != 0) != (get(partBank[p]) != 0))
                put(partOn[p], get(partBank[p]) != 0 ? 1 : 0);
            if (moved(morphEdit[p])) {
                if (edit(p) < 4) lastCorner[p] = edit(p);
                show(p);
            }
        }
        if (fseq && !perf) {
            if (get(fseqBank) == 1) loadFactoryFseq((int)get(fseqNumber));
            else loadUserFseq(userFseq = (int)get(fseqUser));
        }
        if (moved(panic) && get(panic) > 0) dev.allNotesOff();
        if (moved(browseFocus) && get(browseFocus) >= 1) {
            focus((int)get(browseFocus) - 1);
            put(browseFocus, 0);
        }
        const bool bank = moved(browseBank);
        if (bank && get(browseCategory) != 0) put(browseCategory, 0);   // another bank opens on All, never on an empty filter
        if (bank && !st.data("live.browse.filter").empty()) st.setData("live.browse.filter", "");   // nor with the last bank's text in the filter box
        bool lists = bank | moved(browseCategory);
        if (lists) writeLists();
        if (moved(browsePerf) && get(browsePerf) >= 1) pick(0, (int)get(browsePerf), 0);
        if (moved(browseFseq) && get(browseFseq) >= 1) pick(2, (int)get(browseFseq), 0);
        for (int p = 0; p < 4; ++p)
            if (moved(browseVoice[p]) && get(browseVoice[p]) >= 1) pick(1, (int)get(browseVoice[p]), p);
    }

    // The Parts page's lamp turns a part off and on: off is voice bank off, as the unit has it, and the bank is
    // kept with the session for on to bring back, with the voice it held. Bank off changes only the part's bank
    // byte, so the engine still holds that voice, edits and all: on puts the byte back and nothing else. A part
    // the lamp did not turn off (a performance or the host did) has no record, and on loads the bank's voice.
    void lamp(int p) {
        const std::string key = "part.bank_was.p" + std::to_string(p + 1);
        const int b = (int)get(partBank[p]);
        if (get(partOn[p]) == 0) {
            if (b == 0) return;
            st.setData(key, std::to_string(b));
            choose(partBank[p], 0);
        } else if (b == 0) {
            const int was = std::atoi(st.data(key).c_str());
            const bool kept = was >= 1 && was <= 12;
            choose(partBank[p], kept ? was : 2);
            if (!kept) loadPartVoice(p);
        }
    }

    // The Parts page's voice field opens the browser on what the part plays: the bank holding its voice, and the
    // category the browser had while the voice is in it, else All. Nothing moves for a part that is off, or whose
    // internal voice no bank holds.
    void focus(int p) {
        const int vb = (int)get(partBank[p]);
        int bank = -1, cat = 0;   // browse.bank, and the voice's browse.category
        if (vb == 1 && userVoice[p] >= 1 && userVoice[p] <= (int)lib.voices.size()) {
            const Library::Ref& r = lib.voices[(size_t)userVoice[p] - 1];
            const Item& v = lib.banks[(size_t)r.bank].voices[(size_t)r.index];
            bank = r.bank + 1;
            cat = (v.address ? v.category : 0) + 2;
        } else if (vb >= 2) {
            bank = 0;
            cat = listedCategory(vb, (int)get(partProgram[p]));
        }
        if (bank < 0) return;
        const int c = (int)get(browseCategory);
        if (bank == (int)get(browseBank) && (c == 0 || c == cat)) return;
        put(browseBank, bank);
        if (c != 0 && c != cat) put(browseCategory, 0);
        writeLists();
    }

    // A factory voice's category as the browser's lists file it (the DX banks' voices carry no category byte, so
    // the skin's are the only ones): its browse.category value, 2 up, or 0 when the lists do not have it.
    int listedCategory(int bank, int program) {
        if (listed.empty()) {
            Json v;
            if (hollow::parseJson(skinFile("views/library_browser.json"), v))
                for (auto& w : v["widgets"].items)
                    if (w["name"].str() == "voices_all")
                        for (size_t i = 0; i < w["rows"].size(); ++i)
                            for (int c = 0; c < 23; ++c)
                                if (w["rows"][i][2].str() == kCategories[c])
                                    listed[{w["set"]["part.bank.{part}"][i].integer(), w["values"][i].integer()}] = c + 2;
        }
        const auto it = listed.find({bank, program});
        return it == listed.end() ? 0 : it->second;
    }

    // The read-only banks the library lists first, under the factory's: Yamaha's Bonus Tool Pack (tools/extract_bonus.py),
    // four internal-memory sets in one bank, and the Templates (tools/make_templates.py). A set's performances play its
    // own Int voices, so each part is tied to its voice here: the four sets' voice numbers overlap.
    static std::vector<Bank> builtinBanks() {
        Bank bonus;
        bonus.name = "Yamaha FS1R (Bonus)";
        for (const char* f : {"1_Best_of_DX.syx", "2_Popular.syx", "3_Cyber.syx", "4_WX5.syx"}) {
            Bank b = parseSyx(factoryBytes(f), "");
            const int base = (int)bonus.voices.size();
            for (Item& perf : b.perfs) {
                for (int p = 0; p < 4; ++p) {
                    const uint8_t* q = perf.data() + 192 + 52 * p;
                    if (const Item* v = q[1] == 1 ? internal(&b, &Bank::voices, q[2]) : nullptr) perf.partVoice[p] = base + (int)(v - b.voices.data());
                }
                bonus.perfs.push_back(perf);
            }
            bonus.voices.insert(bonus.voices.end(), b.voices.begin(), b.voices.end());
        }
        return {bonus, parseSyx(factoryBytes("templates.syx"), "Templates")};
    }

    // ---- loading -----------------------------------------------------------------------------------

    void loadVoiceItem(const Item& it, int p) {
        const std::vector<uint8_t> m = it.address ? readdress(it.syx, 0x40 + p, 0, 0) : it.syx;   // a DX voice takes the part it is given
        dev.loadSyx(m.data(), m.size(), 0, p);
    }

    void loadFactoryPerf(int index) {
        if (index < 0 || index >= (int)factory.perfs.size()) return;
        const Item& it = factory.perfs[(size_t)index];
        dev.allNotesOff();
        dev.loadSyx(it.syx.data(), it.syx.size(), 0, 0);
        followPerf(it, nullptr);
        put(perfBank, 0);
        st.setData("perf.user.name", "");
        adopt();
        put(edited, 0);
        syncBrowse();
    }

    void loadUserPerf(int n) {
        const Item* it = lib.perf(n);
        if (!it) return;
        const Bank& b = lib.banks[(size_t)lib.perfs[(size_t)n - 1].bank];
        dev.allNotesOff();
        dev.loadSyx(it->syx.data(), it->syx.size(), 0, 0);
        followPerf(*it, &b);
        put(perfBank, 1);
        putUser(perfUser, userPerf, n);
        st.setData("perf.user.name", it->name);
        adopt();
        put(edited, 0);
        syncBrowse();
    }

    // A performance names its voices and its Fseq by bank and number: the factory's, or Int for the
    // user's own, which in a bank that holds its own internal voices means those.
    //
    // A part playing a library voice gets that voice's U number, so the part shows the voice it plays and an
    // edit of its bank or number starts from it, rather than from a U number left by an earlier load.
    void followPerf(const Item& perf, const Bank* bank) {
        const uint8_t* d = perf.data();
        const int k = bank && bank >= lib.banks.data() && bank < lib.banks.data() + lib.banks.size() ? (int)(bank - lib.banks.data()) : -1;
        for (int p = 0; p < 4; ++p) {
            st.setData("part.bank_was.p" + std::to_string(p + 1), "");   // the lamp's record is of the voice this load replaces
            const int vb = d[192 + 52 * p + 1], prog = d[192 + 52 * p + 2];
            auto from = [&](const Item* v) {   // a voice of the performance's own bank
                loadVoiceItem(*v, p);
                if (k >= 0) putUser(partUser[p], userVoice[p], lib.number(lib.voices, k, (int)(v - bank->voices.data())));
            };
            const int fv = factoryVoice(vb, prog);   // -1 for bank bytes past PrK, which a hand-made performance may carry
            if (bank && perf.partVoice[p] >= 0) from(&bank->voices[(size_t)perf.partVoice[p]]);
            else if (fv >= 0 && fv < (int)factory.voices.size()) loadVoiceItem(factory.voices[(size_t)fv], p);
            else if (vb == 1) {
                if (const Item* v = internal(bank, &Bank::voices, prog)) from(v);
                else if (const Item* u = lib.voice(prog + 1)) { loadVoiceItem(*u, p); putUser(partUser[p], userVoice[p], prog + 1); }
            }
        }
        if (bank && perf.fseq >= 0) {
            const Item& f = bank->fseqs[(size_t)perf.fseq];
            dev.loadSyx(f.syx.data(), f.syx.size(), 0, 0);
            // The engine's Fseq loader hands a performance with no Fseq part the first part, as a load from
            // the Fseq page wants (chooseFseq). A saved preset's Fseq is the one the unit held when the
            // performance was saved, so the performance's own part byte goes in again over it: with the
            // part off, that Fseq's frame pitch was shifting every note on part 1 once the preset came back.
            if (fseqPart >= 0) send(fields[(size_t)fseqPart], d);
        } else if ((d[0x15] & 7) != 0) {
            const int num = d[0x17];
            if (d[0x16] & 1) {
                if (num < (int)factory.fseqs.size()) dev.loadSyx(factory.fseqs[(size_t)num].syx.data(), factory.fseqs[(size_t)num].syx.size(), 0, 0);
            } else if (const Item* f = internal(bank, &Bank::fseqs, num)) {
                dev.loadSyx(f->syx.data(), f->syx.size(), 0, 0);
            } else if (const Item* u = lib.fseq(num + 1)) {
                dev.loadSyx(u->syx.data(), u->syx.size(), 0, 0);
                putUser(fseqUser, userFseq, num + 1);
            }
        }
    }

    // A bank's internal memory item by its number: the bulk dumped from that number, else the n-th.
    static const Item* internal(const Bank* b, std::vector<Item> Bank::*list, int n) {
        if (!b) return nullptr;
        const std::vector<Item>& items = b->*list;
        for (auto& it : items)
            if (it.number == n) return &it;
        return n >= 0 && n < (int)items.size() ? &items[(size_t)n] : nullptr;
    }

    void loadPartVoice(int p) {
        const int vb = (int)get(partBank[p]);
        const Item* it = vb == 1 ? lib.voice(userVoice[p])
                       : factoryVoice(vb, (int)get(partProgram[p]) - 1) >= 0 ? &factory.voices[(size_t)factoryVoice(vb, (int)get(partProgram[p]) - 1)] : nullptr;
        if (!it) return;
        dev.allNotesOff();
        loadVoiceItem(*it, p);
        adopt();
        syncBrowse();
    }

    // Choosing an Fseq, as the unit's panel does, copies its header's loop points into the performance:
    // the player reads the performance's pair and nothing else (FUN_0000FFFA, docs/ymp706_registers.md),
    // so without the copy a new Fseq plays the loop the performance had, one frame when that was 0 to 0.
    // A performance's own load keeps its pair (followPerf loads its Fseq without this).
    void chooseFseq(const Item& f) {
        dev.loadSyx(f.syx.data(), f.syx.size(), 0, 0);
        const uint8_t* h = f.data();
        choose(P("fseq.loop_start"), h[0x10] << 7 | h[0x11]);
        choose(P("fseq.loop_end"), h[0x12] << 7 | h[0x13]);
        adopt();
        syncBrowse();
    }

    void loadFactoryFseq(int n) {
        if (n >= 0 && n < (int)factory.fseqs.size()) chooseFseq(factory.fseqs[(size_t)n]);
    }

    void loadUserFseq(int n) {
        if (const Item* it = lib.fseq(n)) chooseFseq(*it);
    }

    // A row of the browsed user bank: list 0 performances, 1 voices (into part p), 2 Fseqs.
    void pick(int list, int row, int p) {
        if (row < 1 || row > (int)browsed[list].size()) return;
        const int n = browsed[list][(size_t)row - 1];
        if (list == 0) {
            putUser(perfUser, userPerf, n);
            put(perfBank, 1);
            loadUserPerf(n);
        } else if (list == 1) {
            putUser(partUser[p], userVoice[p], n);
            choose(partBank[p], 1);
            loadPartVoice(p);
        } else {
            putUser(fseqUser, userFseq, n);
            choose(fseqBank, 0);
            loadUserFseq(n);
        }
    }

    // ---- lists -------------------------------------------------------------------------------------

    std::string perfRow(const Item& it) const {
        const uint8_t* d = it.data();
        std::string voices, chans;
        std::vector<std::string> seen;
        for (int p = 0; p < 4; ++p) {
            const uint8_t* q = d + 192 + 52 * p;
            const int bank = q[1], ch = q[4], mx = q[3];
            if (!bank) continue;
            char code[16];
            std::snprintf(code, sizeof code, "%c%03d", bank >= 2 ? "  ABCDEFGHIJK"[bank] : 'U', q[2] + 1);
            voices += (voices.empty() ? "" : " ") + std::string(code);
            std::string c = ch == 16 ? "Perf" : ch == 127 ? "off" : mx < 16 && mx > ch ? std::to_string(ch + 1) + "-" + std::to_string(mx + 1) : std::to_string(ch + 1);
            if (std::find(seen.begin(), seen.end(), c) == seen.end()) seen.push_back(c);
        }
        std::sort(seen.begin(), seen.end());
        for (auto& c : seen) chans += (chans.empty() ? "" : ", ") + c;
        return it.name + "\t" + kCategories[std::clamp(it.category, 0, 22)] + "\t" + voices + "\t" + chans;
    }

    void writeLists() {   // after every change to the library
        userPerfs = (int)lib.perfs.size();
        std::string perfs, voices, fseqs, banks;
        for (auto& r : lib.perfs) perfs += perfRow(lib.banks[(size_t)r.bank].perfs[(size_t)r.index]) + "\n";
        for (auto& r : lib.voices) {
            const Item& v = lib.banks[(size_t)r.bank].voices[(size_t)r.index];
            voices += v.name + "\t" + kCategories[v.address ? std::clamp(v.category, 0, 22) : 0] + "\n";
        }
        for (auto& r : lib.fseqs) {
            const Item& f = lib.banks[(size_t)r.bank].fseqs[(size_t)r.index];
            fseqs += f.name + "\t" + std::to_string(f.frames) + " frames\n";
        }
        std::string writable;
        for (auto& b : lib.banks) (b.readonly() ? banks : writable) += b.name + "\n";
        banks += writable;   // the built-in banks lead, as rescan() puts them
        st.setData("save.bank.list", writable);
        st.setData("perf.user.list", perfs);
        st.setData("voice.user.list", voices);
        st.setData("fseq.user.list", fseqs);
        st.setData("bank.list", banks);
        // The browsed bank's rows, filtered by the category column (0 All, 1 User, then the categories).
        const int b = (int)get(browseBank) - 1, cat = (int)get(browseCategory) - 2;
        std::string rows[3];
        for (auto& v : browsed) v.clear();
        if (b >= 0 && b < (int)lib.banks.size()) {
            const Bank& bank = lib.banks[(size_t)b];
            for (int i = 0; i < (int)bank.perfs.size(); ++i)
                if (cat < 0 || bank.perfs[(size_t)i].category == cat) {
                    browsed[0].push_back(lib.number(lib.perfs, b, i));
                    rows[0] += "U" + std::to_string(browsed[0].back()) + "\t" + perfRow(bank.perfs[(size_t)i]) + "\n";
                }
            for (int i = 0; i < (int)bank.voices.size(); ++i) {
                const Item& v = bank.voices[(size_t)i];
                if (cat >= 0 && (v.address ? v.category : 0) != cat) continue;
                browsed[1].push_back(lib.number(lib.voices, b, i));
                rows[1] += "U" + std::to_string(browsed[1].back()) + "\t" + v.name + "\t" + kCategories[v.address ? std::clamp(v.category, 0, 22) : 0] + "\n";
            }
            for (int i = 0; i < (int)bank.fseqs.size(); ++i) {
                browsed[2].push_back(lib.number(lib.fseqs, b, i));
                rows[2] += "U" + std::to_string(browsed[2].back()) + "\t" + bank.fseqs[(size_t)i].name + "\t" + std::to_string(bank.fseqs[(size_t)i].frames) + " frames\n";
            }
        }
        st.setData("browse.perf.list", rows[0]);
        st.setData("browse.voice.list", rows[1]);
        st.setData("browse.fseq.list", rows[2]);
        syncBrowse();
    }

    // The browsed bank's lists light what is loaded (nothing when it is from elsewhere), so a click on any
    // other row loads it, and a lit row is never a stale one.
    void syncBrowse() {
        auto row = [&](int list, int n) {
            const auto it = std::find(browsed[list].begin(), browsed[list].end(), n);
            return it == browsed[list].end() ? 0 : (int)(it - browsed[list].begin()) + 1;
        };
        auto set = [&](int i, int v) {   // a click the worker has not acted on yet stays
            if (i >= 0 && st.get((size_t)i) == sel[(size_t)i] && get(i) != v) put(i, v);
        };
        set(browsePerf, get(perfBank) == 1 ? row(0, userPerf) : 0);
        for (int p = 0; p < 4; ++p) set(browseVoice[p], get(partBank[p]) == 1 ? row(1, userVoice[p]) : 0);
        set(browseFseq, get(fseqBank) == 0 ? row(2, userFseq) : 0);
    }

    // ---- requests from the GUI ---------------------------------------------------------------------

    std::string take(const std::string& key) {
        std::string v = st.data(key);
        if (!v.empty()) st.setData(key, "");
        return v;
    }

    // A status line for the library page: it clears itself after five seconds, and a session never brings one back.
    void message(const std::string& s) {
        st.setData("live.message", s);
        messageTicks = 150;
    }

    // The library's read error on the status line, once each time it changes.
    void libraryError() {
        if (lib.error == libError) return;
        libError = lib.error;
        if (!libError.empty()) message(libError);
    }

    int selectedPart() const {   // the GUI's {part} var, out of its saved state
        Json ui;
        if (!hollow::parseJson(st.ui(), ui)) return 0;
        const std::string p = ui["vars"]["part"].str("p1");
        return p.size() == 2 && p[1] >= '1' && p[1] <= '4' ? p[1] - '1' : 0;
    }

    void requests() {
        if (std::string path = take("sysex.import"); !path.empty()) importSyx(path, false);
        if (std::string path = take("fseq.import"); !path.empty()) importSyx(path, true);
        if (std::string path = take("sysex.export"); !path.empty()) exportSyx(path);
        if (std::string path = take("fsvr.export"); !path.empty()) exportPreset(path);
        if (std::string path = take("fsvr.import"); !path.empty()) importPreset(path);
        if (take("save.open") == "1") openSave();
        if (take("save.request") == "1") saveToBank();
        if (std::string c = st.data("item.context"); c != itemSeen) {
            std::lock_guard<std::mutex> g(ctl);
            describeItem(itemSeen = c);
        }
        if (std::string c = st.data("bank.context"); c != bankSeen) describeBank(bankSeen = c);
        if (std::string r = take("item.request"); !r.empty()) itemRequest(r);
        if (std::string r = take("bank.request"); !r.empty()) bankRequest(r);
        if (std::string path = take("fseq.export"); !path.empty()) exportFseq(path);
        if (std::string path = take("fseq.import_audio"); !path.empty()) importAudio(path);
        if (std::string r = take("opt.request"); !r.empty()) optionRequest(r);
        if (std::string r = take("clip.request"); !r.empty()) clipRequest(r);
        for (int p = 0; p < 4; ++p)
            if (take("morph.request.p" + std::to_string(p + 1)) == "normalize") {
                std::lock_guard<std::mutex> g(ctl);
                const int e = edit(p), c = e < 4 ? e : lastCorner[p];
                for (auto& corner : corners[p]) corner = corners[p][c];
                remorph(p);
                show(p);
            }
    }

    // Import SysEx: the file becomes a bank of the library, named after it, and what it holds first loads
    // (its first performance, else its first voice into the selected part, else its first Fseq). Several
    // files, one path a line (a drop, or the dialog's several), share one bank, named after their folder.
    void importSyx(const std::string& list, bool fseqFirst) {
        std::vector<std::string> paths;
        for (size_t a = 0; a < list.size();) {
            const size_t e = std::min(list.find('\n', a), list.size());
            if (e > a) paths.push_back(list.substr(a, e - a));
            a = e + 1;
        }
        std::string err;
        std::lock_guard<std::mutex> g(ctl);
        const std::vector<int> made = paths.size() == 1 ? std::vector<int>{lib.import(paths[0], err)} : lib.importAll(paths, err);
        if (made.empty() || made[0] < 0) { message(err); return; }
        const int k = made[0];
        const Bank& b = lib.banks[(size_t)k];
        put(browseBank, k + 1);
        writeLists();
        std::string more;
        if (made.size() > 1) more = ", and " + std::to_string(made.size() - 1) + " more as banks of their own";   // unit dumps
        if (!err.empty()) more += " (" + err + ")";
        message("Imported \"" + b.name + "\": " + std::to_string(b.perfs.size()) + " performances, " + std::to_string(b.voices.size()) +
                " voices, " + std::to_string(b.fseqs.size()) + " Fseqs" + more);
        if (!b.fseqs.empty() && (fseqFirst || (b.perfs.empty() && b.voices.empty()))) {
            const int n = lib.number(lib.fseqs, k, 0);
            putUser(fseqUser, userFseq, n);
            choose(fseqBank, 0);
            loadUserFseq(n);
        } else if (!b.perfs.empty()) {
            const int n = lib.number(lib.perfs, k, 0);
            loadUserPerf(n);
        } else if (!b.voices.empty()) {
            const int p = selectedPart(), n = lib.number(lib.voices, k, 0);
            putUser(partUser[p], userVoice[p], n);
            choose(partBank[p], 1);
            loadPartVoice(p);
        }
    }

    // Export SysEx writes the whole unit: system, performance, its four voices and its Fseq.
    void exportSyx(const std::string& path) {
        std::vector<uint8_t> dump;
        {
            std::lock_guard<std::mutex> g(ctl);
            dev.getState(dump);
        }
        if (!writeFile(path, dump)) message("Cannot write " + path);
        else message("Saved " + std::filesystem::u8path(path).filename().u8string());
    }

    // ---- presets: the performance with its four voices and its Fseq -------------------------------

    std::string perfName() const {
        std::string n;
        for (int k = 0; k < 12; ++k) n += model.perf[k] >= 32 && model.perf[k] < 127 ? (char)model.perf[k] : ' ';
        while (!n.empty() && n.back() == ' ') n.pop_back();
        return n;
    }

    // Under ctl: the engine's performance, voices and Fseq as the unit dumps them, the performance named.
    std::vector<uint8_t> presetBytes(const std::string& name) {
        std::vector<uint8_t> dump, out;
        dev.getState(dump);
        for (size_t i = 0; i + 6 < dump.size();) {
            size_t j = i + 1;
            while (j < dump.size() && dump[j] != 0xF7) ++j;
            std::vector<uint8_t> m(dump.begin() + (long)i, dump.begin() + (long)std::min(j + 1, dump.size()));
            i = j + 1;
            if (m[6] == 0x00) continue;   // the system is the unit's, not the preset's
            if (m[6] == 0x10) {
                Item perf;
                perf.address = 0x10;
                perf.syx = m;
                m = renamed(perf, name);
            }
            out.insert(out.end(), m.begin(), m.end());
        }
        return out;
    }

    // Under ctl: the engine's performance name and category, as the unit's parameter changes.
    void setPerf(const std::string* name, int category) {
        for (int k = 0; name && k < 12; ++k) {
            const char c = k < (int)name->size() ? (*name)[(size_t)k] : ' ';
            model.perf[k] = (uint8_t)(c >= 32 && c < 127 ? c : ' ');
            Field f;
            f.h = 0x10;
            f.l = k;
            f.off = k;
            send(f, model.perf);
        }
        if (name) st.setData("perf.name", perfName());
        if (category >= 0 && perfCategory >= 0) {
            const Field& f = fields[(size_t)perfCategory];
            f.set(model.perf, f.toRaw(category));
            send(f, model.perf);
            put(perfCategory, category);
        }
    }

    static std::string trimmed(std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t a = 0;
        while (a < s.size() && s[a] == ' ') ++a;
        return s.substr(a);
    }

    // The Save modal opens on the performance's name, and on its own bank when it came from one, else the
    // bank the browser shows, else a new bank.
    void openSave() {
        std::lock_guard<std::mutex> g(ctl);
        st.setData("save.name", perfName());
        const int bank = get(perfBank) == 1 && lib.perf(userPerf) ? lib.perfs[(size_t)userPerf - 1].bank + 1 : (int)get(browseBank);
        put(saveBank, saveRow(bank));
        // A new bank's name starts on one no bank has, so the name shown is the name the file gets.
        std::string fresh = "My Presets";
        auto taken = [&](const std::string& n) {
            return std::any_of(lib.banks.begin(), lib.banks.end(), [&](const Bank& b) { return b.name == n; });
        };
        for (int k = 2; taken(fresh); ++k) fresh = "My Presets " + std::to_string(k);
        st.setData("save.bank_name", fresh);
    }

    // The Save modal's row for a browser bank row: its list leaves out the built-in banks, so <New Bank> for one.
    int saveRow(int bank) const {
        const int nb = (int)lib.builtin.size();
        return bank > nb && bank <= (int)lib.banks.size() ? bank - nb : 0;
    }

    // Save to Bank: the performance with its voices and Fseq into the bank the modal chose, or a new one.
    // A performance of the same name there is replaced, with the voices and Fseq that came with it.
    void saveToBank() {
        std::string name = trimmed(st.data("save.name")).substr(0, 12), err;
        if (name.empty()) name = "Untitled";
        std::lock_guard<std::mutex> g(ctl);
        setPerf(&name, -1);
        const std::vector<uint8_t> bytes = presetBytes(name);
        int k = (int)get(saveBank) < 1 ? -1 : (int)get(saveBank) - 1 + (int)lib.builtin.size();
        if (k < 0 || k >= (int)lib.banks.size()) {
            const std::string bankName = trimmed(st.data("save.bank_name"));
            k = lib.add(bankName.empty() ? name : bankName, bytes, err);
        } else {
            const Bank& b = lib.banks[(size_t)k];
            std::vector<Library::Splice> e;
            for (auto& perf : b.perfs)
                if (trimmed(perf.name) == name) {
                    e = presetRanges(b, perf);
                    e[0].bytes = bytes;
                    break;
                }
            if (e.empty()) e.push_back({SIZE_MAX, 0, bytes});
            if (!lib.splice(k, e, err)) k = -1;
        }
        if (k < 0) { message(err); return; }
        writeLists();
        const Bank& b = lib.banks[(size_t)k];
        for (int i = (int)b.perfs.size() - 1; i >= 0; --i)
            if (trimmed(b.perfs[(size_t)i].name) == name) {
                putUser(perfUser, userPerf, lib.number(lib.perfs, k, i));
                break;
            }
        put(perfBank, 1);
        put(browseBank, k + 1);
        put(edited, 0);
        st.setData("perf.user.name", name);
        writeLists();
        message("Saved \"" + name + "\" to " + b.name);
        if (take("save.then_close") == "1") st.setData("hollow.close", "1");   // the standalone was closing
    }

    // A performance's bytes in its bank: its own first, then the voices and Fseq that came with it.
    static std::vector<Library::Splice> presetRanges(const Bank& b, const Item& perf) {
        std::vector<Library::Splice> e{{perf.at, perf.len, {}}};
        for (int v : perf.partVoice)
            if (v >= 0) e.push_back({b.voices[(size_t)v].at, b.voices[(size_t)v].len, {}});
        if (perf.fseq >= 0) e.push_back({b.fseqs[(size_t)perf.fseq].at, b.fseqs[(size_t)perf.fseq].len, {}});
        return e;
    }

    static std::string xmlEscape(const std::string& s) {
        std::string o;
        for (char c : s) o += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
        return o;
    }

    static std::string xmlText(const std::string& doc, const std::string& tag) {   // the first <tag ...>text</tag>
        size_t a = doc.find("<" + tag), e = std::string::npos;
        if (a != std::string::npos) a = doc.find('>', a);
        if (a != std::string::npos) e = doc.find("</" + tag + ">", ++a);
        if (e == std::string::npos) return {};
        std::string s = doc.substr(a, e - a), o;
        static const std::pair<const char*, char> ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
        for (size_t i = 0; i < s.size(); ++i) {
            bool hit = false;
            for (auto& en : ents)
                if (s.compare(i, std::strlen(en.first), en.first) == 0) { o += en.second; i += std::strlen(en.first) - 1; hit = true; break; }
            if (!hit) o += s[i];
        }
        return o;
    }

    // Export Preset: FSVR's own .fsvr, XML so it can carry more than the unit's bytes later on. Version 1
    // holds the name, the category and the sysex a performance and its voices and Fseq load from.
    void exportPreset(const std::string& path) {
        bool ok;
        {
            std::lock_guard<std::mutex> g(ctl);
            ok = writePreset(path);
        }
        if (!ok) message("Cannot write " + path);
        else message("Exported " + std::filesystem::u8path(path).filename().u8string());
    }

    bool writePreset(const std::string& path) {   // under ctl
        const std::string name = perfName();
        const std::vector<uint8_t> bytes = presetBytes(name);
        const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<fsvr version=\"1\" type=\"performance\">\n  <name>" + xmlEscape(name) +
                                "</name>\n  <category>" + xmlEscape(kCategories[std::clamp((int)model.perf[14], 0, 22)]) +
                                "</category>\n  <sysex encoding=\"base64\">" + b64(bytes.data(), bytes.size()) + "</sysex>\n</fsvr>\n";
        return writeFile(path, std::vector<uint8_t>(xml.begin(), xml.end()));
    }

    // ---- options: the Options page, kept for every instance ---------------------------------------

    // <Documents>/FSVR (the library's folder's parent), where Settings.json and Init.fsvr live.
    std::string optionsFile(const char* name) const { return (std::filesystem::u8path(lib.dir).parent_path() / name).u8string(); }

    static const char* optionKey(size_t k) { return k < kScale ? kOptions[k].first : "scale"; }

    void readOptions() {
        for (size_t k = 0; k < kScale; ++k) options[k] = P(kOptions[k].second) >= 0 ? st.def((size_t)P(kOptions[k].second)).def : 0;
        options[kScale] = 1;
        std::vector<uint8_t> raw;
        Json j;
        if (readFile(optionsFile("Settings.json"), raw) && hollow::parseJson(std::string(raw.begin(), raw.end()), j))
            for (size_t k = 0; k <= kScale; ++k) options[k] = j[optionKey(k)].num(options[k]);
        if (options[kScale] != 1 && st.ui().empty())   // a new window opens at the last scale
            st.setUi("{\"scale\":" + hollow::formatNumber("%g", options[kScale]) + "}");
        applyOptions();
    }

    // The options as Settings.json has them: at the start, and over what a session brought back.
    void applyOptions() {
        for (size_t k = 0; k < kScale; ++k) put(P(kOptions[k].second), options[k]);
        st.setData("opt.init_name", initName());
    }

    // Worker, a few times a second: an option or the window's scale changed here, so Settings.json follows.
    void watchOptions() {
        double now[kScale + 1];
        for (size_t k = 0; k < kScale; ++k) now[k] = P(kOptions[k].second) >= 0 ? get(P(kOptions[k].second)) : options[k];
        Json ui;
        now[kScale] = hollow::parseJson(st.ui(), ui) ? ui["scale"].num(options[kScale]) : options[kScale];
        if (std::equal(now, now + kScale + 1, options)) return;
        std::copy(now, now + kScale + 1, options);
        std::string j = "{";
        for (size_t k = 0; k <= kScale; ++k) j += std::string(k ? ", " : "") + "\"" + optionKey(k) + "\": " + hollow::formatNumber("%g", options[k]);
        j += "}\n";
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(optionsFile("")), ec);
        writeFile(optionsFile("Settings.json"), std::vector<uint8_t>(j.begin(), j.end()));
    }

    std::string initName() {
        std::vector<uint8_t> raw;
        if (!readFile(optionsFile("Init.fsvr"), raw)) return "A001 Zap ! (factory)";
        const std::string name = trimmed(xmlText(std::string(raw.begin(), raw.end()), "name"));
        return name.empty() ? "Untitled" : name;
    }

    // A .fsvr as the Init Patch, under ctl: it plays under its own name, in no bank.
    void playInit(const std::string& path) {
        const std::string name = playPreset(path);
        if (name.empty()) return;
        userPerf = 0;
        put(perfBank, 1);
        st.setData("perf.user.name", name);
        put(edited, 0);
        syncBrowse();
    }

    // The Init Patch, or the factory's A001 when none is set: under ctl.
    void playInitPatch() {
        const std::string init = optionsFile("Init.fsvr");
        if (std::filesystem::exists(std::filesystem::u8path(init))) return playInit(init);
        put(perfProgram, 0);
        loadFactoryPerf(0);
    }

    void optionRequest(const std::string& r) {
        std::lock_guard<std::mutex> g(ctl);
        if (r == "fx_off") {   // insertion, variation and reverb to no effect, the EQ flat
            for (const char* id : {"fx.ins.type", "fx.var.type", "fx.rev.type", "fx.eq.low_gain", "fx.eq.mid_gain", "fx.eq.high_gain"}) choose(P(id), 0);
            message("All effects off");
        } else if (r == "init_set") {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::u8path(optionsFile("")), ec);
            if (!writePreset(optionsFile("Init.fsvr"))) { message("Cannot write " + optionsFile("Init.fsvr")); return; }
            st.setData("opt.init_name", initName());
            message("The Init Patch is now \"" + initName() + "\"");
        } else if (r == "init_clear") {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(optionsFile("Init.fsvr")), ec);
            st.setData("opt.init_name", initName());
            message("The Init Patch is the factory's A001 Zap ! again");
        } else if (r == "init_patch") {   // the Init Patch, the system settings left alone
            playInitPatch();
            message("The Init Patch loaded");
        } else if (r == "init_all") {   // the system settings to their defaults, then the Init Patch
            for (int i : fieldParams)
                if (fields[(size_t)i].area == System) choose(i, st.def((size_t)i).def);
            playInitPatch();
            message("Init All: the system settings reset and the Init Patch loaded");
        }
    }

    // A .fsvr file's performance, its voices and its Fseq into the engine, under ctl. Its name, or "" with a
    // message when it is not one.
    std::string playPreset(const std::string& path) {
        std::vector<uint8_t> raw;
        if (!readFile(path, raw)) { message("Cannot read " + path); return {}; }
        const std::string doc(raw.begin(), raw.end());
        if (doc.find("<fsvr") == std::string::npos) { message(std::filesystem::u8path(path).filename().u8string() + " is not an FSVR preset"); return {}; }
        const Bank b = parseSyx(unb64(xmlText(doc, "sysex")), "");
        if (b.perfs.empty()) { message("No performance in " + std::filesystem::u8path(path).filename().u8string()); return {}; }
        std::string name = trimmed(xmlText(doc, "name")).substr(0, 12);
        if (name.empty()) name = b.perfs[0].name;
        dev.allNotesOff();
        dev.loadSyx(b.perfs[0].syx.data(), b.perfs[0].syx.size(), 0, 0);
        followPerf(b.perfs[0], &b);
        adopt();
        return name;
    }

    // Import FSVR Preset: the preset plays at once, and the Save modal opens on its name.
    void importPreset(const std::string& path) {
        std::lock_guard<std::mutex> g(ctl);
        const std::string name = playPreset(path);
        if (name.empty()) return;
        put(edited, 1);   // in no bank yet
        st.setData("save.name", name);
        put(saveBank, saveRow((int)get(browseBank)));
        st.setData("save.then_close", "");
        st.setData("hollow.modal", "dialog_save");
    }

    // ---- copy and paste ------------------------------------------------------------------------------

    // The pages' right-click menus send "copy <clip> <prefix> <suffix>" and "paste <clip> <prefix> <suffix>" as
    // text data clip.request. A copy keeps every param whose id is the prefix, a name and the suffix, by that name
    // ("op.3.v." and ".p1" keep operator 3's voiced params as coarse, eg_l1 and so on); a paste sets the params
    // the names make with its own prefix and suffix, as edits the engine hears. A voiced and an unvoiced
    // operator's envelope params share their names, so an envelope pastes across the two.
    std::map<std::string, std::vector<std::pair<std::string, double>>> clips;
    struct PartClip { bool held = false; uint8_t perf[52] = {}; uint8_t voice[608] = {}; int user = 0; } partClip;

    void clipRequest(const std::string& r) {
        std::vector<std::string> w;
        for (size_t a = 0; a < r.size();) {
            const size_t e = std::min(r.find(' ', a), r.size());
            w.push_back(r.substr(a, e - a));
            a = e + 1;
        }
        w.resize(4);
        std::lock_guard<std::mutex> g(ctl);
        if (w[1] == "part") { partRequest(w[0] == "copy", w[2]); return; }
        const std::string &pre = w[2], &suf = w[3];
        if (w[0] == "copy") {
            auto& c = clips[w[1]];
            c.clear();
            for (size_t i = 0; i < st.size(); ++i) {
                const std::string& id = st.def(i).id;
                if (id.size() > pre.size() + suf.size() && id.compare(0, pre.size(), pre) == 0 && id.compare(id.size() - suf.size(), suf.size(), suf) == 0)
                    c.emplace_back(id.substr(pre.size(), id.size() - pre.size() - suf.size()), st.get(i));
            }
        } else if (w[0] == "paste") {
            for (auto& [name, v] : clips[w[1]]) choose(st.indexOf(pre + name + suf), v);
        }
    }

    // "copy part pN" and "paste part pN": the part's 52 performance bytes and its voice. A paste keeps the channels
    // the part receives on (Rcv Max and Rcv Ch), so it answers where it did, and goes in as a load does: the bytes
    // as parameter changes, the voice as one bulk, and adopt reads what the engine took back into the params.
    void partRequest(bool copy, const std::string& part) {   // under ctl
        const int p = part.size() == 2 && part[0] == 'p' && part[1] >= '1' && part[1] <= '4' ? part[1] - '1' : -1;
        if (p < 0) return;
        const int at = 192 + 52 * p;
        if (copy) {
            std::memcpy(partClip.perf, model.perf + at, 52);
            std::memcpy(partClip.voice, model.voice[p], 608);
            partClip.user = userVoice[p];
            partClip.held = true;
            return;
        }
        if (!partClip.held) return;
        uint8_t perf[400];
        std::memcpy(perf, model.perf, 400);
        for (int k = 0; k < 52; ++k)
            if (k != 3 && k != 4) perf[at + k] = partClip.perf[k];
        dev.allNotesOff();
        for (int i : fieldParams) {
            const Field& f = fields[(size_t)i];
            if (f.area == Performance && f.off >= at && f.off < at + 52) send(f, perf);
        }
        sendVoice(p, partClip.voice);
        adopt();
        putUser(partUser[p], userVoice[p], partClip.user);
        put(edited, 1);
        syncBrowse();
    }

    // ---- right-click on a browser row ---------------------------------------------------------------

    // The row the right-click was on: the browser's tab says what it lists, and its code which one, "U12"
    // a user item, "A001" a factory performance or voice, "01" a factory Fseq.
    struct Target { int kind = -1, user = 0, factory = -1; };
    Target target;
    std::string itemSeen, bankSeen;
    int clip = -1;                     // Copy Attributes: the category, -1 before any copy
    int bankTarget = -2;               // the bank row: -1 the factory's, else a library bank

    std::string browseTab() const {
        Json ui;
        return hollow::parseJson(st.ui(), ui) ? ui["vars"]["browse"].str("perf") : "perf";
    }

    const Item* itemOf(const Target& t) const {
        const std::vector<Item>* f = t.kind == 0 ? &factory.perfs : t.kind == 1 ? &factory.voices : &factory.fseqs;
        if (t.user) return t.kind == 0 ? lib.perf(t.user) : t.kind == 1 ? lib.voice(t.user) : lib.fseq(t.user);
        return t.factory >= 0 && t.factory < (int)f->size() ? &(*f)[(size_t)t.factory] : nullptr;
    }

    const Library::Ref* refOf(const Target& t) const {
        const std::vector<Library::Ref>& l = t.kind == 0 ? lib.perfs : t.kind == 1 ? lib.voices : lib.fseqs;
        return t.user >= 1 && t.user <= (int)l.size() ? &l[(size_t)t.user - 1] : nullptr;
    }

    void describeItem(const std::string& ctx) {   // under ctl
        std::vector<std::string> f;
        for (size_t a = 0; a <= ctx.size();) {
            size_t e = std::min(ctx.find('\t', a), ctx.size());
            f.push_back(ctx.substr(a, e - a));
            a = e + 1;
        }
        const std::string tab = browseTab(), code = f.size() > 1 ? f[1] : "";
        target = {};
        target.kind = tab == "voice" ? 1 : tab == "fseq" ? 2 : 0;
        if (!code.empty() && code[0] == 'U') target.user = std::atoi(code.c_str() + 1);
        else if (target.kind == 2) target.factory = std::atoi(code.c_str()) - 1;
        else if (code.size() >= 2 && code[0] >= 'A' && code[0] <= 'K') {
            const int num = std::atoi(code.c_str() + 1) - 1;
            target.factory = target.kind == 0 ? (code[0] - 'A') * 128 + num : factoryVoice(code[0] - 'A' + 2, num);
        }
        const Item* it = itemOf(target);
        const char* kinds[3] = {"performance", "voice", "Fseq"};
        st.setData("item.name", it ? it->name : "");
        st.setData("item.what", std::string(kinds[target.kind]) + " \"" + (it ? it->name : "") + "\"");
        st.setData("item.new_name", it ? it->name : "");
        st.setData("item.max", it ? std::to_string(nameLength(*it)) + " characters at most" : "");
        const Library::Ref* ref = refOf(target);
        const bool readonly = !ref || lib.banks[(size_t)ref->bank].readonly();
        st.setData("item.bank", ref ? lib.banks[(size_t)ref->bank].name : "Yamaha FS1R");
        put(itemReadonly, readonly ? 1 : 0);
        put(itemKind, target.kind == 2 || (it && !it->address) ? 2 : target.kind);   // 2: no category (an Fseq, a DX voice)
        put(itemCategory, it && it->address ? std::clamp(it->category, 0, 22) : 0);
    }

    // The performance the engine holds, if it is a user one: its bank's file and place, to find it again
    // by after the library changes under it.
    struct Loaded { std::string path, name; int index = -1; };
    Loaded loaded() const {
        const int n = userPerf;
        if (get(perfBank) != 1 || !lib.perf(n)) return {};
        const Library::Ref& r = lib.perfs[(size_t)n - 1];
        return {lib.banks[(size_t)r.bank].path, lib.banks[(size_t)r.bank].perfs[(size_t)r.index].name, r.index};
    }
    void relocate(const Loaded& was, const std::string& path) {
        const int k = was.index < 0 ? -1 : lib.bankOf(path);
        if (k < 0) return;
        const Bank& b = lib.banks[(size_t)k];
        int best = -1;
        for (int i = 0; i < (int)b.perfs.size(); ++i)
            if (b.perfs[(size_t)i].name == was.name && (best < 0 || i == was.index)) best = i;
        if (best >= 0) putUser(perfUser, userPerf, lib.number(lib.perfs, k, best));
    }

    // Delete, Rename, Edit Attributes (OK), Copy Attributes and Paste Attributes on the row; the factory's
    // presets can be read and copied from, not changed.
    void itemRequest(const std::string& r) {
        std::lock_guard<std::mutex> g(ctl);
        const Item* it = itemOf(target);
        if (!it) return;
        const bool categorized = target.kind < 2 && it->address;
        if (r == "copy") {
            if (!categorized) { message("An Fseq or a DX voice has no attributes to copy"); return; }
            clip = it->category;
            message("Copied the attributes of \"" + it->name + "\"");
            return;
        }
        const Library::Ref* ref = refOf(target);
        if (!ref || lib.banks[(size_t)ref->bank].readonly()) { message("The " + (ref ? lib.banks[(size_t)ref->bank].name : "Yamaha FS1R") + " presets are read only"); return; }
        const Bank& b = lib.banks[(size_t)ref->bank];
        const std::string path = b.path, old = it->name;
        const Loaded was = loaded();
        const bool isLoaded = target.kind == 0 && was.index == ref->index && was.path == path;
        std::vector<Library::Splice> e;
        std::string done, name;
        int cat = -1;
        if (r == "delete") {
            e = target.kind == 0 ? presetRanges(b, *it) : std::vector<Library::Splice>{{it->at, it->len, {}}};
            done = "Deleted \"" + old + "\"";
        } else if (r == "rename") {
            name = trimmed(st.data("item.new_name")).substr(0, (size_t)nameLength(*it));
            if (name.empty() || name == old) return;
            e.push_back({it->at, it->len, renamed(*it, name)});
            done = "Renamed \"" + old + "\" to \"" + name + "\"";
        } else if (r == "attributes" || r == "paste") {
            cat = r == "paste" ? clip : (int)get(itemCategory);
            if (!categorized) { message("An Fseq or a DX voice has no attributes"); return; }
            if (cat < 0) { message("Copy the attributes of a preset first"); return; }
            e.push_back({it->at, it->len, recategorized(*it, cat)});
            done = "\"" + old + "\" is now " + kCategories[cat];
        } else {
            return;
        }
        std::string err;
        if (!lib.splice(ref->bank, e, err)) { message(err); return; }
        if (isLoaded && r != "delete") setPerf(name.empty() ? nullptr : &name, cat);   // the engine's copy follows its bank
        if (isLoaded && !name.empty()) st.setData("perf.user.name", name);
        relocate(was, path);
        writeLists();
        describeItem(itemSeen);
        message(done);
    }

    // The bank row: 0 the factory bank, then the library's banks in order.
    void describeBank(const std::string& ctx) {
        const size_t tab = ctx.find('\t');
        const int row = std::atoi(ctx.c_str());
        const std::string name = tab == std::string::npos ? "" : ctx.substr(tab + 1);
        bankTarget = row - 1;
        st.setData("bank.name", name);
        st.setData("bank.new_name", name);
        put(bankReadonly, row == 0 || (row <= (int)lib.banks.size() && lib.banks[(size_t)row - 1].readonly()) ? 1 : 0);
    }

    void bankRequest(const std::string& r) {
        std::lock_guard<std::mutex> g(ctl);
        const int k = bankTarget;
        if (k < 0 || k >= (int)lib.banks.size() || lib.banks[(size_t)k].readonly()) {
            message("The " + (k < 0 || k >= (int)lib.banks.size() ? std::string("Yamaha FS1R") : lib.banks[(size_t)k].name) + " bank is read only");
            return;
        }
        const std::string old = lib.banks[(size_t)k].name, path = lib.banks[(size_t)k].path;
        const bool browsed = (int)get(browseBank) == k + 1;
        Loaded was = loaded();
        std::string err;
        if (r == "delete") {
            if (!lib.remove(k, err)) { message(err); return; }
            if (browsed) put(browseBank, 0);
            message("Deleted the bank \"" + old + "\" (kept in the library's Deleted folder)");
        } else if (r == "rename") {
            const std::string name = trimmed(st.data("bank.new_name"));
            if (name.empty() || name == old) return;
            std::string moved;
            if (!lib.rename(k, name, err, &moved)) { message(err); return; }
            const int now = lib.bankOf(moved);
            if (now >= 0) {
                if (browsed) put(browseBank, now + 1);
                if (was.path == path) was.path = lib.banks[(size_t)now].path;
            }
            message("Renamed the bank \"" + old + "\" to \"" + (now >= 0 ? lib.banks[(size_t)now].name : name) + "\"");
        } else {
            return;
        }
        relocate(was, was.path);
        writeLists();
        describeBank(bankSeen);
    }

    void exportFseq(const std::string& path) {
        std::vector<uint8_t> dump, fseq;
        {
            std::lock_guard<std::mutex> g(ctl);
            dev.getState(dump);
        }
        Model m;
        m.parse(dump, &fseq);
        if (fseq.empty()) { message("No Fseq is loaded"); return; }
        if (!writeFile(path, bulk(0x60, 0, 0, fseq.data(), fseq.size()))) message("Cannot write " + path);
        else message("Saved " + std::filesystem::u8path(path).filename().u8string());
    }

    // Import Audio: the analysis runs here, off the lock; the Fseq it makes is a bank of its own in the
    // library, named after the file, and plays on the performance's Fseq part (part 1 if none has it).
    void importAudio(const std::string& path) {
        message("Analysing " + std::filesystem::u8path(path).filename().u8string() + "...");
        std::vector<float> mono;
        double rate = 0;
        std::string err;
        if (!decodeAudio(path, mono, rate, err)) { message(err); return; }
        const std::string stem = std::filesystem::u8path(path).stem().u8string();
        int frames = 0;
        const std::vector<uint8_t> data = audioToFseq(mono, rate, stem, frames);
        std::lock_guard<std::mutex> g(ctl);
        const int k = lib.add(stem, bulk(0x60, 0, 0, data.data(), data.size()), err);
        if (k < 0) { message(err); return; }
        writeLists();
        const int n = lib.number(lib.fseqs, k, 0);
        putUser(fseqUser, userFseq, n);
        choose(fseqBank, 0);
        if (get(fseqPart) == 0) choose(fseqPart, 1);
        loadUserFseq(n);
        message("Made \"" + lib.banks[(size_t)k].name + "\": " + std::to_string(frames) + " frames");
    }

    // ---- the monitor -------------------------------------------------------------------------------

    // Every part's eight voiced operators as the engine makes them, for the operator panel's display: text
    // data live.wave.p<part>.<op>, 128 points 0..1 with the centre line at 0.5, and live.harmonics.p<part>.<op>,
    // the level of its first 16 harmonics, -48..0 dB as 0..1, which is what tells the spectral forms apart (a
    // sine is one line, the formant a band around its frequency). Live data: never saved.
    void waves() {
        static constexpr int kPeriod = 480, kN = 2 * kPeriod;   // operatorWave's two periods at 100 Hz (48 kHz), every sample
        static const std::vector<std::pair<float, float>> turn = [] {   // cos and sin of 2 pi i / kN
            std::vector<std::pair<float, float>> t(kN);
            for (int i = 0; i < kN; ++i) t[(size_t)i] = {(float)std::cos(2 * 3.14159265358979323846 * i / kN), (float)std::sin(2 * 3.14159265358979323846 * i / kN)};
            return t;
        }();
        std::vector<float> y(kN);
        auto put = [](std::string& s, double v) {   // three decimals, written out so that a host's locale has no say
            const int k = (int)std::lround(std::clamp(v, 0.0, 1.0) * 1000);
            s += k >= 1000 ? "1 " : "0." + std::string(k < 100 ? "0" : "") + std::string(k < 10 ? "0" : "") + std::to_string(k) + " ";
        };
        uint8_t now[4][608];   // the waves follow the part's voice alone (Device::operatorWave), so an unchanged voice is drawn already
        {
            std::lock_guard<std::mutex> g(ctl);
            std::memcpy(now, model.voice, sizeof now);
        }
        for (int p = 0; p < 4; ++p) {
            if (waveDrawn[p] && std::memcmp(now[p], waveVoice[p], 608) == 0) continue;
            std::memcpy(waveVoice[p], now[p], 608);
            waveDrawn[p] = true;
            for (int o = 0; o < 8; ++o) {
                dev.operatorWave(p, o, y.data(), kN);
                std::string wave, harm;
                for (int i = 0; i < 128; ++i) {   // each point the largest sample of its share, as operatorWave draws a trace
                    float m = y[(size_t)i * kN / 128];
                    for (int k = i * kN / 128; k < (i + 1) * kN / 128; ++k)
                        if (std::fabs(y[(size_t)k]) > std::fabs(m)) m = y[(size_t)k];
                    put(wave, 0.5 + 0.45 * m);
                }
                double mag[16], top = 1e-9;
                for (int h = 0; h < 16; ++h) {   // harmonic h + 1 is bin 2 (h + 1) of the two periods
                    double re = 0, im = 0;
                    for (int i = 0; i < kN; ++i) {
                        const auto& t = turn[(size_t)(2 * (h + 1) * i % kN)];
                        re += y[(size_t)i] * t.first, im -= y[(size_t)i] * t.second;
                    }
                    top = std::max(top, mag[h] = std::hypot(re, im));
                }
                for (double m : mag) put(harm, m > top * 1e-3 ? (20 * std::log10(m / top) + 48) / 48 : 0);
                const std::string at = "p" + std::to_string(p + 1) + "." + std::to_string(o + 1);
                st.setData("live.wave." + at, wave);
                st.setData("live.harmonics." + at, harm);
            }
        }
    }

    // The 32 harmonics of the last note played, their level in the output (-60..0 dB as 0..1), for the
    // monitor's harmonic display. Published when they change, so a silent instance repaints nothing.
    void harmonics() {
        static constexpr size_t N = 4096;
        static const std::vector<double> hann = [] {
            std::vector<double> w(N);
            for (size_t i = 0; i < N; ++i) w[i] = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * (double)i / N);
            return w;
        }();
        float out[32] = {};
        const int note = lastNote;
        const size_t at = ringAt;
        auto x = [&](size_t i) { return ring[(at + ring.size() - N + i) % ring.size()]; };
        double peak = 0;
        for (size_t i = 0; note >= 0 && i < N; ++i) peak = std::max(peak, (double)std::fabs(x(i)));
        if (peak > 1e-6) {   // below it every harmonic reads 0 anyway
            const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            for (int h = 0; h < 32; ++h) {
                const double f = f0 * (h + 1);
                if (f >= hostRate / 2) break;
                const double w = 2 * std::cos(2 * 3.14159265358979323846 * f / hostRate);
                double s1 = 0, s2 = 0;
                for (size_t i = 0; i < N; ++i) {   // Goertzel under a Hann window
                    const double s = x(i) * hann[i] + w * s1 - s2;
                    s2 = s1;
                    s1 = s;
                }
                const double mag = std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - w * s1 * s2)) * 4 / N;
                out[h] = mag > 1e-6 ? (float)std::clamp((20 * std::log10(mag) + 60) / 60, 0.0, 1.0) : 0.0f;
            }
        }
        if (scopeDrawn && std::memcmp(out, scopeShown, sizeof out) == 0) return;
        std::memcpy(scopeShown, out, sizeof out);
        scopeDrawn = true;
        st.setScope(out, 32);
    }
};

} // namespace fsvr

namespace hollow {

const Info& pluginInfo() {
    static const Info info{"studio.musica.fsvr", "FSVR", "musica.studio", "https://github.com/musicastudio/FSVR", FSVR_VERSION,
                           "Yamaha FS1R", /*instrument*/ true, /*inputs*/ 0, /*outputs*/ 2, /*midiIn*/ true, /*midiOut*/ true};
    return info;
}

std::unique_ptr<Processor> createProcessor(State& state) { return std::make_unique<fsvr::Fsvr>(state); }

} // namespace hollow
