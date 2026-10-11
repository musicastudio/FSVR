// check_plugin: the FSVR processor end to end, as a host drives it but without one: its State (the skin's
// params and text data), its worker and its audio, a block at a time. The factory banks, a sysex param
// round trip through a saved session, Import SysEx into the bank manager, a pick from a user bank, the
// morph square, Import Audio, program change, panic and the monitor, Save to Bank, .fsvr presets and the
// browser's right-click menus. Exits non-zero on any failure.
//   build/<dir>/Release/check_plugin      (ctest runs it as "plugin")
#include <hollow/hollow.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace hollow;
namespace fs = std::filesystem;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("check_plugin: FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

struct Rig {   // one instance, run a block at a time
    std::shared_ptr<Skin> skin = loadSkin();
    std::unique_ptr<State> st = std::make_unique<State>(skinParams(*skin));
    std::unique_ptr<Processor> proc = createProcessor(*st);
    std::vector<float> l = std::vector<float>(512), r = std::vector<float>(512);
    float peak = 0;
    Rig() { proc->prepare(48000, 512); }
    void run(int blocks = 1) {
        float* out[2] = {l.data(), r.data()};
        for (int b = 0; b < blocks; ++b) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            proc->process(nullptr, out, 512);
            for (float v : l) peak = std::max(peak, std::fabs(v));
        }
    }
    template <class F> bool until(F done, int ms = 8000) {   // the worker acts about 30 times a second
        for (auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms); std::chrono::steady_clock::now() < end;) {
            run();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    int at(const char* id) const { return st->indexOf(id); }
    double get(const char* id) const { return at(id) >= 0 ? st->get((size_t)at(id)) : -1e9; }
    void set(const char* id, double v) { if (at(id) >= 0) st->set((size_t)at(id), v); }
    bool is(const char* id, double v) const { return std::fabs(get(id) - v) < 1e-6; }   // values are snapped to their steps
    void midi(std::initializer_list<uint8_t> b) { proc->midi(0, b.begin(), (int)b.size()); }
    std::string data(const char* key) const { return st->data(key); }
    int lines(const char* key) const { std::string d = st->data(key); return (int)std::count(d.begin(), d.end(), '\n'); }
};

static void writeBytes(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}

// One native bulk at ah 00 al onto the end of a unit-shaped image.
static void addBulk(std::vector<uint8_t>& unit, int ah, int al, const std::vector<uint8_t>& d) {
    std::vector<uint8_t> m = {0xF0, 0x43, 0x00, 0x5E, (uint8_t)(d.size() >> 7), (uint8_t)(d.size() & 0x7F), (uint8_t)ah, 0x00, (uint8_t)al};
    m.insert(m.end(), d.begin(), d.end());
    int sum = 0;
    for (size_t i = 4; i < m.size(); ++i) sum += m[i];
    m.push_back((uint8_t)(-sum & 0x7F));
    m.push_back(0xF7);
    unit.insert(unit.end(), m.begin(), m.end());
}

// 1 + the line of a list whose tab-separated field is text, 0 if none.
static int rowOf(const Rig& a, const char* key, const std::string& text, int field = 0) {
    std::string d = a.data(key);
    int n = 1;
    for (size_t p = 0; p < d.size(); ++n) {
        const size_t e = std::min(d.find('\n', p), d.size());
        std::string line = d.substr(p, e - p);
        for (int k = 0; k < field; ++k) line = line.find('\t') == std::string::npos ? std::string() : line.substr(line.find('\t') + 1);
        if (line.substr(0, line.find('\t')) == text) return n;
        p = e + 1;
    }
    return 0;
}

// The fundamental of a held C4 in Hz: the left channel's last 4096 samples after a third of a second,
// from silence, by autocorrelation over 40 Hz to 1.2 kHz.
static double pitchOf(Rig& a) {
    a.set("gui.panic", 1);
    a.run(8);
    a.set("gui.panic", 0);
    a.run(20);
    a.midi({0x90, 60, 100});
    std::vector<float> x;
    for (int b = 0; b < 40; ++b) {
        a.run();
        if (b >= 32) x.insert(x.end(), a.l.begin(), a.l.end());
    }
    a.midi({0x80, 60, 0});
    a.run(4);
    int best = 40;
    double top = -1e30;
    for (int lag = 40; lag < 1200; ++lag) {
        double s = 0;
        for (size_t i = 0; i + (size_t)lag < x.size(); ++i) s += x[i] * x[i + (size_t)lag];
        if (s > top) { top = s; best = lag; }
    }
    return 48000.0 / best;
}

// A DX7 32-voice bank (VMEM): every voice an init voice (op 1 loud), named TEST01..TEST32.
static std::vector<uint8_t> dx7Bank() {
    std::vector<uint8_t> b = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    for (int v = 0; v < 32; ++v) {
        uint8_t d[128] = {};
        for (int op = 0; op < 6; ++op) {
            uint8_t* p = d + op * 17;
            for (int k = 0; k < 4; ++k) { p[k] = 99; p[4 + k] = k < 3 ? 99 : 0; }
            p[14] = op == 5 ? 99 : 0;   // OP1 (stored last) carries the sound
            p[15] = 1 << 1;             // coarse 1
            p[12] = 7 << 3;             // detune centred
        }
        for (int k = 0; k < 4; ++k) { d[102 + k] = 99; d[106 + k] = 50; }
        d[117] = 24;
        char name[11];
        std::snprintf(name, sizeof name, "TEST%02d    ", v + 1);
        std::memcpy(d + 118, name, 10);
        b.insert(b.end(), d, d + 128);
    }
    int sum = 0;
    for (size_t i = 6; i < b.size(); ++i) sum += b[i];
    b.push_back((uint8_t)(-sum & 0x7F));
    b.push_back(0xF7);
    return b;
}

// One second of a 150 Hz voice-like tone: harmonics shaped by formant bumps at 700 and 1200 Hz, then a
// noise burst, 16-bit mono WAV at 44.1 kHz.
static std::vector<uint8_t> vowelWav() {
    const int sr = 44100, n = sr;
    std::vector<int16_t> s((size_t)n);
    uint32_t noise = 1;
    for (int i = 0; i < n; ++i) {
        double t = (double)i / sr, v = 0;
        if (i < n * 3 / 4) {
            for (int h = 1; h * 150 < 5000; ++h) {
                const double f = h * 150.0;
                const double a = std::exp(-std::pow((f - 700) / 150, 2)) + 0.6 * std::exp(-std::pow((f - 1200) / 200, 2)) + 0.02;
                v += a * std::sin(2 * 3.14159265358979 * f * t);
            }
            v *= 0.25;
        } else {
            noise = noise * 1664525u + 1013904223u;
            v = ((noise >> 9) / double(1 << 23) - 1) * 0.3;
        }
        s[(size_t)i] = (int16_t)std::clamp(v * 32767, -32767.0, 32767.0);
    }
    std::vector<uint8_t> w;
    auto u32 = [&](uint32_t x) { for (int k = 0; k < 4; ++k) w.push_back((uint8_t)(x >> (8 * k))); };
    auto u16 = [&](uint16_t x) { w.push_back((uint8_t)x); w.push_back((uint8_t)(x >> 8)); };
    w.insert(w.end(), {'R', 'I', 'F', 'F'});
    u32(36 + (uint32_t)n * 2);
    w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    w.insert(w.end(), {'d', 'a', 't', 'a'});
    u32((uint32_t)n * 2);
    const uint8_t* p = (const uint8_t*)s.data();
    w.insert(w.end(), p, p + n * 2);
    return w;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / ("fsvr_check_plugin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));   // runs side by side
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "Library");
#ifdef _WIN32
    _putenv_s("FSVR_LIBRARY", (tmp / "Library").u8string().c_str());
#else
    setenv("FSVR_LIBRARY", (tmp / "Library").u8string().c_str(), 1);
#endif

    Rig a;
    CHECK(a.st->size() > 3000, "the skin's params did not load (%zu)", a.st->size());
    CHECK(!a.data("library.dir").empty(), "no library.dir");
    CHECK(!a.data("fsvr.version").empty(), "no fsvr.version for the About box");

    // A fresh instance plays what its LCD names: A001 "Zap !", B021 on part 1, a Sound FX.
    CHECK(a.until([&] { return a.is("part.bank.p1", 3) && a.is("part.program.p1", 21); }), "A001 did not load its part 1 voice B021 (bank %g, program %g)",
          a.get("part.bank.p1"), a.get("part.program.p1"));
    CHECK(a.is("perf.category", 16), "A001's category is %g, not Sound FX (16)", a.get("perf.category"));
    const std::string zapVoice = a.data("morph.p1.bl");
    CHECK(!zapVoice.empty() && zapVoice != "InitEP", "part 1's voice name is \"%s\"", zapVoice.c_str());

    // A note sounds, the monitor shows its harmonics, and panic silences it: B014 "Full Tines", a piano.
    a.set("perf.program", 141);
    CHECK(a.until([&] { return a.is("perf.category", 1); }), "B014 did not load");
    a.midi({0x90, 60, 100});
    a.run(20);
    CHECK(a.peak > 1e-4f, "the init voice made no sound (peak %g)", a.peak);
    CHECK(a.st->voices() >= 1, "no voice is playing");
    CHECK(a.until([&] { float v[64]; int n = a.st->scope(v, 64); return n == 32 && *std::max_element(v, v + n) > 0; }),
          "the monitor's harmonics never showed");
    a.set("gui.panic", 1);
    CHECK(a.until([&] { return a.st->voices() == 0; }), "panic left %d notes", a.st->voices());
    a.set("gui.panic", 0);

    // Another factory performance: A002 "Shaman" plays B095 on part 1, a Vocal.
    a.set("perf.program", 1);
    CHECK(a.until([&] { return a.is("part.bank.p1", 3) && a.is("part.program.p1", 95) && a.is("perf.category", 19); }),
          "A002 did not load (bank %g, program %g, category %g)", a.get("part.bank.p1"), a.get("part.program.p1"), a.get("perf.category"));
    CHECK(a.data("morph.p1.bl") != zapVoice, "part 1 still holds %s", zapVoice.c_str());

    // Program change: bank select Preset B (LSB 0x42) then program 6 is B006.
    a.midi({0xB0, 0, 0x3F});
    a.midi({0xB0, 32, 0x42});
    a.midi({0xC0, 5});
    CHECK(a.until([&] { return a.is("perf.program", 133) && a.is("perf.bank", 0); }), "program change reached performance %g", a.get("perf.program"));
    const std::string shaman = a.data("morph.p1.bl");
    CHECK(a.until([&] { return a.data("morph.p1.bl") != shaman; }), "B006 did not load its voices");

    // A param edit reaches the engine and comes back with the session, even saved and reopened at once
    // (saving() finishes what the worker has not reached yet).
    a.set("op.1.v.level.p1", 57);
    a.set("perf.volume", 101);
    a.run(4);
    a.proc->saving();
    const std::string named = a.data("part.voice.p4");
    a.st->setData("part.voice.p4", "");   // as a session saved before the names were published has it
    const std::string blob = a.st->save();
    a.st->setData("part.voice.p4", named);
    {
        Rig b;
        CHECK(b.until([&] { return b.data("part.voice.p4") == "off"; }), "a fresh instance's part 4 reads \"%s\"", b.data("part.voice.p4").c_str());
        b.st->load(blob);   // part 4 is off in both: the name it published already is the one the session wants
        CHECK(b.is("op.1.v.level.p1", 57) && b.is("perf.volume", 101), "the session's params did not come back (level %g, volume %g)",
              b.get("op.1.v.level.p1"), b.get("perf.volume"));
        b.proc->saving();
        CHECK(b.data("fsvr.engine") == a.data("fsvr.engine"), "the engine did not come back the same");
        CHECK(b.is("perf.program", 133), "the performance number did not come back");
        b.run(20);
        b.proc->saving();
        CHECK(b.data("fsvr.engine") == a.data("fsvr.engine"), "the engine changed after the session came back");
        CHECK(b.until([&] { return b.data("part.voice.p4") == "off"; }), "a session that saved part 4's voice name empty kept it \"%s\"",
              b.data("part.voice.p4").c_str());
    }

    // Import SysEx: a DX7 bank becomes a bank of its own, named after the file, and its first voice loads.
    const fs::path syx = tmp / "Test Bank.syx";
    writeBytes(syx, dx7Bank());
    a.st->setData("sysex.import", syx.u8string());
    CHECK(a.until([&] { return a.data("bank.list") == "Test Bank\n"; }), "the bank list is \"%s\"", a.data("bank.list").c_str());
    CHECK(a.lines("voice.user.list") == 32, "%d user voices, not 32", a.lines("voice.user.list"));
    CHECK(a.is("browse.bank", 1), "the browser is not on the new bank");
    CHECK(a.until([&] { return a.is("part.bank.p1", 1) && a.is("part.user.p1", 1) && a.data("morph.p1.bl") == "TEST01"; }),
          "the first voice did not load (bank %g, user %g, \"%s\")", a.get("part.bank.p1"), a.get("part.user.p1"), a.data("morph.p1.bl").c_str());
    a.st->setData("sysex.import", syx.u8string());
    CHECK(a.until([&] { return a.data("bank.list") == "Test Bank\nTest Bank 2\n"; }), "a second import did not make \"Test Bank 2\"");
    CHECK(a.lines("voice.user.list") == 64, "%d user voices, not 64", a.lines("voice.user.list"));
    CHECK(a.data("fsvr.message").rfind("Imported", 0) == 0, "no import message (\"%s\")", a.data("fsvr.message").c_str());
    CHECK(a.until([&] { return a.data("fsvr.message").empty(); }), "the import message stayed up");

    // A pick from the browsed user bank: its third voice into part 2.
    a.set("browse.bank", 1);
    CHECK(a.until([&] { return a.lines("browse.voice.list") == 32; }), "the browsed bank lists %d voices", a.lines("browse.voice.list"));
    a.set("browse.voice.p2", 3);
    CHECK(a.until([&] { return a.is("part.bank.p2", 1) && a.is("part.user.p2", 3) && a.data("morph.p2.bl") == "TEST03"; }), "the pick did not load TEST03");

    // Morph is off until it works: a voice picked with corner A chosen reaches every corner, and moving the
    // square changes nothing.
    // a.set("part.morph_edit.p1", 0);
    // a.run(2);
    // a.set("part.bank.p1", 1);
    // a.set("part.user.p1", 5);
    // CHECK(a.until([&] { return a.data("morph.p1.tl") == "TEST05" && a.data("morph.p1.bl") == "TEST01"; }), "corner A is \"%s\", C \"%s\"",
    //       a.data("morph.p1.tl").c_str(), a.data("morph.p1.bl").c_str());
    // a.proc->saving();
    // const std::string atC = a.data("fsvr.engine");
    // a.set("part.morph_y.p1", 100);   // the top left corner: A
    // a.run(4);
    // a.proc->saving();
    // CHECK(a.data("fsvr.engine") != atC, "moving the morph square did not change the voice");
    // a.st->setData("morph.request.p1", "normalize");
    // CHECK(a.until([&] { return a.data("morph.p1.bl") == "TEST05" && a.data("morph.p1.br") == "TEST05"; }), "Normalize did not copy corner A");
    a.set("part.morph_edit.p1", 0);
    a.run(2);
    a.set("part.bank.p1", 1);
    a.set("part.user.p1", 5);
    CHECK(a.until([&] { return a.data("morph.p1.tl") == "TEST05" && a.data("morph.p1.bl") == "TEST05"; }), "corner A is \"%s\", C \"%s\"",
          a.data("morph.p1.tl").c_str(), a.data("morph.p1.bl").c_str());
    a.proc->saving();
    const std::string still = a.data("fsvr.engine");
    a.set("part.morph_y.p1", 100);
    a.run(4);
    a.proc->saving();
    CHECK(a.data("fsvr.engine") == still, "moving the morph square changed the voice");

    // Import Audio: a vowel at 150 Hz becomes a user Fseq, loaded, its pitch the word for 150 Hz.
    const fs::path wav = tmp / "Vowel.wav";
    writeBytes(wav, vowelWav());
    const std::string shown = a.data("fseq.display");   // the list line comes before the load, the display after it
    a.st->setData("fseq.import_audio", wav.u8string());
    CHECK(a.until([&] { return a.lines("fseq.user.list") == 1 && a.data("fseq.display") != shown && !a.data("fseq.display").empty(); }, 20000),
          "Import Audio made no Fseq (%s)", a.data("fsvr.message").c_str());
    CHECK(a.is("fseq.bank", 0) && a.is("fseq.user", 1), "the new Fseq is not selected");
    const std::string disp = a.data("fseq.display");
    const size_t p = disp.find("\"pitch\":\"");
    if (p != std::string::npos) {
        const int hi = std::stoi(disp.substr(p + 9 + 20, 2), nullptr, 16);   // frame 10, well inside the tone
        CHECK(std::abs(hi - 0x62) <= 1, "the pitch word's high byte is %02x, 150 Hz is 62", hi);
    }

    // The Fseq plays from a note on its part, and the page's playback line follows it.
    a.set("fseq.part", 1);
    a.set("fseq.play_mode", 1);   // Fseq, not Scratch
    a.run(2);
    a.midi({0x90, 60, 100});
    CHECK(a.until([&] { return a.get("fseq.position") > 5; }), "the Fseq playback position stayed at %g (speed %g, delay %g, part %g, mode %g, voices %d)", a.get("fseq.position"), a.get("fseq.speed"), a.get("fseq.delay"), a.get("fseq.part"), a.get("fseq.play_mode"), a.st->voices());
    a.midi({0x80, 60, 0});

    // Export writes a hardware-shaped dump.
    a.st->setData("sysex.export", (tmp / "out.syx").u8string());
    CHECK(a.until([&] { return fs::exists(tmp / "out.syx") && fs::file_size(tmp / "out.syx") > 3000; }), "Export SysEx wrote nothing");

    // Save to Bank, as the Save modal asks for it: an edit marks the performance edited, the modal opens
    // on its name, and the save makes a new bank holding it as a user performance.
    auto lineOf = [&](const char* key, const std::string& first) { return rowOf(a, key, first); };   // 1 + the line whose first field is first, 0 if none
    auto request = [&](const char* key, const std::string& v) { a.st->setData("fsvr.message", ""); a.st->setData(key, v); };
    auto settle = [&] { a.until([] { return false; }, 400); };   // a right-click's row reaches the worker before its modal opens
    a.set("perf.program", 1);
    CHECK(a.until([&] { return a.is("perf.category", 19) && a.is("gui.edited", 0); }), "A002 did not load clean");
    a.set("perf.volume", 90);
    CHECK(a.until([&] { return a.is("gui.edited", 1); }), "an edit did not mark the performance edited");
    a.st->setData("save.open", "1");
    CHECK(a.until([&] { return a.data("save.name") == "Shaman"; }), "the Save modal opened on \"%s\"", a.data("save.name").c_str());
    a.set("save.bank", 0);
    a.st->setData("save.bank_name", "Saved");
    a.st->setData("save.name", "My Shaman");
    request("save.request", "1");
    CHECK(a.until([&] { return lineOf("bank.list", "Saved") && a.is("perf.bank", 1) && a.data("perf.user.name") == "My Shaman"; }),
          "Save to a new bank did not make \"Saved\" (%s)", a.data("fsvr.message").c_str());
    CHECK(a.is("gui.edited", 0), "the saved performance still reads as edited");
    const int saved = lineOf("bank.list", "Saved");
    a.set("browse.bank", saved);
    CHECK(a.until([&] { return a.lines("browse.perf.list") == 1; }), "the new bank lists %d performances", a.lines("browse.perf.list"));
    a.set("save.bank", saved);
    request("save.request", "1");   // the same name again replaces it
    CHECK(a.until([&] { return !a.data("fsvr.message").empty(); }) && a.lines("browse.perf.list") == 1, "saving \"My Shaman\" again left %d performances (%s)",
          a.lines("browse.perf.list"), a.data("fsvr.message").c_str());
    a.st->setData("save.name", "Second");
    request("save.request", "1");
    CHECK(a.until([&] { return a.lines("browse.perf.list") == 2; }), "a second name did not add a performance (%s)", a.data("fsvr.message").c_str());

    // A saved preset loads back as it was saved: its performance, its four voices and its Fseq, byte for
    // byte but the name, after another performance in between.
    auto unb64 = [](const std::string& s) {
        static const std::string k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<uint8_t> o;
        uint32_t v = 0;
        int bits = 0;
        for (char c : s) {
            size_t p = k.find(c);
            if (p == std::string::npos) continue;
            v = v << 6 | (uint32_t)p;
            if ((bits += 6) >= 8) o.push_back((uint8_t)(v >> (bits -= 8)));
        }
        return o;
    };
    auto bulks = [](const std::vector<uint8_t>& d) {   // address high and data of each bulk but the system's
        std::vector<std::pair<int, std::vector<uint8_t>>> out;
        for (size_t i = 0; i + 11 < d.size();) {
            size_t j = i + 1;
            while (j < d.size() && d[j] != 0xF7) ++j;
            if (d[i + 6] != 0x00) out.push_back({d[i + 6], std::vector<uint8_t>(d.begin() + (long)i + 9, d.begin() + (long)j - 1)});
            i = j + 1;
        }
        return out;
    };
    auto engine = [&] { a.run(4); a.proc->saving(); return bulks(unb64(a.data("fsvr.engine"))); };
    auto saveAs = [&](int bank, const std::string& name) {
        a.st->setData("save.name", "");
        a.st->setData("save.open", "1");
        CHECK(a.until([&] { return a.data("save.name") == a.data("perf.name"); }), "the Save modal did not open on \"%s\"", a.data("perf.name").c_str());
        a.set("save.bank", bank);
        a.st->setData("save.name", name);
        request("save.request", "1");
        CHECK(a.until([&] { return a.data("perf.user.name") == name && a.is("gui.edited", 0); }), "\"%s\" did not save (%s)", name.c_str(), a.data("fsvr.message").c_str());
    };
    auto sound = [&] {   // a note's RMS in dB over half a second, from silence
        a.set("gui.panic", 1);
        a.run(8);
        a.set("gui.panic", 0);
        a.run(40);
        a.midi({0x90, 60, 100});
        double sum = 0;
        int n = 0;
        for (int b = 0; b < 48; ++b) {
            a.run();
            for (float v : a.l) { sum += (double)v * v; ++n; }
        }
        a.midi({0x80, 60, 0});
        return 10 * std::log10(sum / n + 1e-20);
    };
    auto same = [&](const char* what, const std::vector<std::pair<int, std::vector<uint8_t>>>& was, std::vector<std::pair<int, std::vector<uint8_t>>> now) {
        CHECK(was.size() == now.size(), "%s: %zu bulks, then %zu", what, was.size(), now.size());
        for (size_t k = 0; k < std::min(was.size(), now.size()); ++k) {
            if (now[k].first == 0x10 && now[k].second.size() >= 12 && was[k].second.size() >= 12)   // but the name
                std::copy(was[k].second.begin(), was[k].second.begin() + 12, now[k].second.begin());
            size_t diff = 0, first = 0;
            for (size_t b = 0; b < std::min(was[k].second.size(), now[k].second.size()); ++b)
                if (was[k].second[b] != now[k].second[b] && !diff++) first = b;
            CHECK(was[k].first == now[k].first && !diff, "%s: bulk %02x has %zu bytes changed, the first at %zu (%d, was %d)", what, was[k].first, diff, first,
                  diff ? now[k].second[first] : 0, diff ? was[k].second[first] : 0);
        }
    };
    auto sameAfterReload = [&](const char* what, double loud, const std::vector<std::pair<int, std::vector<uint8_t>>>& edited) {
        CHECK(std::fabs(sound() - loud) < 1.0, "%s: saving changed the sound", what);
        const auto was = engine();
        same((std::string(what) + ", saving").c_str(), edited, was);
        const int n = (int)a.get("perf.user");
        const std::string name = a.data("perf.user.name");
        a.set("perf.bank", 0);
        a.set("perf.program", 1);
        CHECK(a.until([&] { return a.data("perf.name") == "Shaman"; }), "%s: A002 did not load in between", what);
        a.set("perf.user", n);
        a.set("perf.bank", 1);
        CHECK(a.until([&] { return a.data("perf.name") == name; }), "%s: it did not load back (\"%s\")", what, a.data("perf.name").c_str());
        const auto now = engine();
        const double again = sound();
        CHECK(std::fabs(again - loud) < 1.0, "%s: a note is %.1f dB after the reload, %.1f dB when saved", what, again, loud);
        CHECK(was.size() == now.size(), "%s: %zu bulks saved, %zu loaded", what, was.size(), now.size());
        for (size_t k = 0; k < std::min(was.size(), now.size()); ++k) {
            size_t diff = 0, first = 0;
            for (size_t b = 0; b < std::min(was[k].second.size(), now[k].second.size()); ++b)
                if (was[k].second[b] != now[k].second[b] && !diff++) first = b;
            CHECK(was[k].first == now[k].first && !diff, "%s: bulk %02x came back with %zu bytes changed, the first at %zu (%d, was %d)", what, was[k].first, diff, first,
                  diff ? now[k].second[first] : 0, diff ? was[k].second[first] : 0);
        }
    };

    // A factory performance, edited in a voice and in the performance, saved into a user bank.
    a.set("perf.bank", 0);
    a.set("perf.program", 12);   // A013 Dirt Vocoder
    CHECK(a.until([&] { return a.data("perf.name") == "Dirt Vocoder" && a.is("gui.edited", 0); }), "A013 did not load (\"%s\")", a.data("perf.name").c_str());
    a.set("op.1.v.level.p1", 42);
    a.set("perf.volume", 77);
    a.set("part.note_shift.p2", 30);
    a.run(4);
    const double edited = sound();
    const auto editedBytes = engine();
    saveAs(saved, "Round Trip");
    CHECK(a.until([&] { return a.is("browse.bank", saved) && a.get("browse.perf") == lineOf("browse.perf.list", "U" + std::to_string((int)a.get("perf.user"))); }),
          "the saved preset's row is not the lit one (row %g)", a.get("browse.perf"));
    sameAfterReload("a saved factory performance", edited, editedBytes);

    // A bank shaped like a whole unit's dump: a performance at 11 00 05 whose parts play the bank's own
    // internal voices (Int, 51 00 nn), loaded, edited and saved back over itself.
    {
        a.set("perf.bank", 0);
        a.set("perf.program", 12);
        CHECK(a.until([&] { return a.data("perf.name") == "Dirt Vocoder" && a.is("gui.edited", 0); }), "A013 did not load again");
        const auto parts = engine();
        std::vector<uint8_t> unit;
        for (auto& b : parts) {
            if (b.first != 0x10) continue;
            std::vector<uint8_t> d = b.second;
            std::memcpy(d.data(), "Int Perf    ", 12);
            for (int p = 0; p < 4; ++p) { d[192 + 52 * p + 1] = 1; d[192 + 52 * p + 2] = (uint8_t)(10 + p); }
            addBulk(unit, 0x11, 5, d);
        }
        for (auto& b : parts)
            if (b.first >= 0x40 && b.first <= 0x43) addBulk(unit, 0x51, 10 + b.first - 0x40, b.second);
        const fs::path unitSyx = tmp / "Unit.syx";
        writeBytes(unitSyx, unit);
        a.st->setData("sysex.import", unitSyx.u8string());
        CHECK(a.until([&] { return a.data("perf.name") == "Int Perf" && a.data("perf.user.name") == "Int Perf"; }), "the unit bank's performance did not load (\"%s\")",
              a.data("perf.name").c_str());
        const auto loaded = engine();
        for (size_t k = 0; k < loaded.size() && k < parts.size(); ++k)
            if (loaded[k].first >= 0x40 && loaded[k].first <= 0x43)
                CHECK(loaded[k].second == parts[k].second, "part %d did not load the bank's Int voice", loaded[k].first - 0x40 + 1);
        auto voiceName = [&](int n) {   // line n of the user voice list, its name
            std::string d = a.data("voice.user.list");
            size_t p = 0;
            for (int k = 1; k < n && p < d.size(); ++k) p = d.find('\n', p) + 1;
            return p >= d.size() ? std::string() : d.substr(p, d.find('\t', p) - p);
        };
        CHECK(a.is("part.bank.p1", 1) && voiceName((int)a.get("part.user.p1")) == a.data("morph.p1.bl"), "part 1 plays \"%s\" but shows U%g, \"%s\"",
              a.data("morph.p1.bl").c_str(), a.get("part.user.p1"), voiceName((int)a.get("part.user.p1")).c_str());
        a.set("op.2.v.level.p1", 11);
        a.set("perf.volume", 66);
        a.run(4);
        const double intEdited = sound();
        const auto intBytes = engine();
        saveAs(lineOf("bank.list", "Unit"), "Int Perf");
        a.set("browse.bank", lineOf("bank.list", "Unit"));
        CHECK(a.until([&] { return a.lines("browse.perf.list") == 1; }), "saving over \"Int Perf\" left %d performances in its bank", a.lines("browse.perf.list"));
        sameAfterReload("a unit bank's performance saved over itself", intEdited, intBytes);

        // Another bank opens on All: a category left from the factory bank never hides a bank's presets.
        a.set("browse.bank", 0);
        CHECK(a.until([&] { return a.lines("browse.perf.list") == 0; }), "the factory bank did not open");   // the worker has seen the change
        a.set("browse.category", 7);   // Guitar
        a.set("browse.bank", lineOf("bank.list", "Unit"));
        CHECK(a.until([&] { return a.is("browse.category", 0) && a.lines("browse.perf.list") == 1; }), "the bank opened on category %g with %d rows",
              a.get("browse.category"), a.lines("browse.perf.list"));
    }

    // Export Preset writes .fsvr XML; importing it loads it and opens the Save modal on its name.
    const fs::path preset = tmp / "Int Perf.fsvr";
    request("fsvr.export", preset.u8string());
    CHECK(a.until([&] { std::vector<uint8_t> b; std::ifstream f(preset, std::ios::binary); std::string x((std::istreambuf_iterator<char>(f)), {});
                        return x.find("<name>Int Perf</name>") != std::string::npos && x.find("<sysex") != std::string::npos; }), "Export Preset wrote no .fsvr");
    a.st->setData("hollow.modal", "");
    request("fsvr.import", preset.u8string());
    CHECK(a.until([&] { return a.data("hollow.modal") == "dialog_save" && a.data("save.name") == "Int Perf"; }), "Import FSVR Preset did not open the Save modal (%s)",
          a.data("fsvr.message").c_str());

    // The right-click menu on a user performance: Rename, Edit Attributes, Paste Attributes, Delete.
    const std::string second = "0\tU" + std::to_string(lineOf("perf.user.list", "Second")) + "\tSecond";
    a.st->setData("item.context", second);
    CHECK(a.until([&] { return a.data("item.name") == "Second" && a.is("item.readonly", 0); }), "the row did not describe itself (\"%s\")", a.data("item.name").c_str());
    a.st->setData("item.new_name", "Renamed");
    request("item.request", "rename");
    CHECK(a.until([&] { return lineOf("perf.user.list", "Renamed") > 0; }), "Rename did not rename (%s)", a.data("fsvr.message").c_str());
    const std::string renamed = "0\tU" + std::to_string(lineOf("perf.user.list", "Renamed")) + "\tRenamed";
    a.st->setData("item.context", renamed);
    settle();
    a.set("item.category", 5);
    request("item.request", "attributes");
    CHECK(a.until([&] { return a.data("perf.user.list").find("Renamed\tBass") != std::string::npos; }), "Edit Attributes did not set Bass (%s)", a.data("fsvr.message").c_str());
    a.st->setData("item.context", "0\tA001\tZap !");   // a factory row: read only, but its attributes copy
    CHECK(a.until([&] { return a.data("item.name") == "Zap !" && a.is("item.readonly", 1) && a.is("item.category", 16); }), "A001 did not describe itself read only");
    request("item.request", "delete");
    CHECK(a.until([&] { return a.data("fsvr.message").find("read only") != std::string::npos; }), "deleting a factory preset said \"%s\"", a.data("fsvr.message").c_str());
    request("item.request", "copy");
    CHECK(a.until([&] { return a.data("fsvr.message").rfind("Copied", 0) == 0; }), "Copy Attributes said \"%s\"", a.data("fsvr.message").c_str());
    a.st->setData("item.context", renamed);
    CHECK(a.until([&] { return a.data("item.name") == "Renamed"; }), "back on the user row");
    request("item.request", "paste");
    CHECK(a.until([&] { return a.data("perf.user.list").find("Renamed\tSound FX") != std::string::npos; }), "Paste Attributes did not paste Sound FX");
    request("item.request", "delete");
    CHECK(a.until([&] { return !lineOf("perf.user.list", "Renamed") && lineOf("perf.user.list", "My Shaman"); }), "Delete did not delete only \"Renamed\"");

    // The right-click menu on a bank: Rename Bank, then Delete Bank, which keeps its file in Deleted.
    a.st->setData("bank.context", std::to_string(lineOf("bank.list", "Saved")) + "\tSaved");
    settle();
    a.st->setData("bank.new_name", "Kept");
    request("bank.request", "rename");
    CHECK(a.until([&] { return lineOf("bank.list", "Kept") && !lineOf("bank.list", "Saved"); }), "Rename Bank did not rename (%s)", a.data("fsvr.message").c_str());
    a.st->setData("bank.context", "0\tYamaha FS1R");
    settle();
    request("bank.request", "delete");
    CHECK(a.until([&] { return a.data("fsvr.message").find("read only") != std::string::npos; }), "deleting the factory bank said \"%s\"", a.data("fsvr.message").c_str());
    a.st->setData("bank.context", std::to_string(lineOf("bank.list", "Kept")) + "\tKept");
    settle();
    request("bank.request", "delete");
    CHECK(a.until([&] { return !lineOf("bank.list", "Kept"); }) && fs::exists(tmp / "Library" / "Deleted" / "Kept.syx"), "Delete Bank did not move \"Kept\" to Deleted");

    // A performance with no Fseq part, saved while the unit held an Fseq, plays at its own pitch when its
    // copy is picked after a close and reopen. The preset carries that Fseq, and the engine's Fseq loader
    // gives a performance without a part the first one, so the copy once played every part 1 note at the
    // frame's pitch, its sequence running. The bank is shaped like a unit's dump again, from B014 "Full
    // Tines" (a piano) in Fseq play mode with the Fseq pitch on part 1 alone, as the performance reported
    // was; the Fseq the unit holds is the factory's 43 "ChuckRtm".
    {
        a.set("perf.bank", 0);
        a.set("perf.program", 141);
        CHECK(a.until([&] { return a.data("perf.name") == "Full Tines" && a.is("gui.edited", 0); }), "B014 did not load (\"%s\")", a.data("perf.name").c_str());
        const std::string vowel = a.data("fseq.display");
        a.set("fseq.bank", 1);
        a.set("fseq.number", 42);
        CHECK(a.until([&] { return a.data("fseq.display") != vowel; }), "the factory Fseq did not load");
        std::vector<uint8_t> unit;
        for (auto& b : engine()) {
            if (b.first >= 0x40 && b.first <= 0x43) addBulk(unit, 0x51, 20 + b.first - 0x40, b.second);
            if (b.first != 0x10) continue;
            std::vector<uint8_t> d = b.second;
            std::memcpy(d.data(), "Tines Int   ", 12);
            d[0x15] = 0;   // Fseq part off
            d[0x21] = 2;   // Fseq play mode
            d[0x23] = 0;   // Fseq pitch
            for (int p = 0; p < 4; ++p) { d[192 + 52 * p + 1] = 1; d[192 + 52 * p + 2] = (uint8_t)(20 + p); d[192 + 52 * p + 4] = p ? 127 : 16; }   // part 1 alone, on the Perf channel
            addBulk(unit, 0x11, 7, d);
        }
        const fs::path unitSyx = tmp / "Unit Two.syx";
        writeBytes(unitSyx, unit);
        a.st->setData("sysex.import", unitSyx.u8string());
        CHECK(a.until([&] { return a.data("perf.user.name") == "Tines Int" && a.is("gui.edited", 0); }), "Unit Two's performance did not load (\"%s\")", a.data("perf.name").c_str());
        CHECK(a.is("fseq.part", 0), "Tines Int loaded with Fseq part %g", a.get("fseq.part"));
        const double hz = pitchOf(a);
        CHECK(hz > 200 && hz < 330, "Tines Int plays C4 at %.1f Hz", hz);
        a.set("part.attack.p1", 63);
        a.run(4);
        saveAs(lineOf("bank.list", "Unit Two"), "Tines Copy");
        a.proc->saving();
        const std::string session = a.st->save();
        Rig b;
        b.st->load(session);
        b.proc->saving();
        CHECK(b.data("fsvr.engine") == a.data("fsvr.engine"), "the engine did not come back with the session");
        b.set("browse.category", 0);
        b.set("browse.bank", rowOf(b, "bank.list", "Unit Two"));
        CHECK(b.until([&] { return b.lines("browse.perf.list") == 2; }), "the reopened instance lists %d performances in Unit Two", b.lines("browse.perf.list"));
        // The session came back with the saved row lit, so the original first: the copy is then a pick, not
        // the engine the session restored.
        b.set("browse.perf", rowOf(b, "browse.perf.list", "Tines Int", 1));
        CHECK(b.until([&] { return b.data("perf.user.name") == "Tines Int"; }), "the reopened instance did not load \"Tines Int\" (\"%s\")", b.data("perf.name").c_str());
        b.set("browse.perf", rowOf(b, "browse.perf.list", "Tines Copy", 1));
        CHECK(b.until([&] { return b.data("perf.user.name") == "Tines Copy" && b.is("part.attack.p1", 63); }), "the pick did not load \"Tines Copy\" (\"%s\")", b.data("perf.name").c_str());
        CHECK(b.is("fseq.part", 0), "Tines Copy came back with Fseq part %g", b.get("fseq.part"));
        const double back = pitchOf(b);
        CHECK(std::fabs(back - hz) < 0.02 * hz, "Tines Copy plays C4 at %.1f Hz after the reopen, %.1f Hz when saved", back, hz);
        const double step = b.get("fseq.position");   // the worker publishes the Fseq's step every tick
        b.midi({0x90, 60, 100});
        b.until([&] { return b.get("fseq.position") != step; }, 400);
        b.midi({0x80, 60, 0});
        CHECK(b.get("fseq.position") == step, "the Fseq ran on Tines Copy, which has no Fseq part (step %g, was %g)", b.get("fseq.position"), step);
    }

    a.proc.reset();
    fs::remove_all(tmp, ec);
    if (!fails) std::printf("check_plugin: factory banks, sessions, Import SysEx, the bank browser, morph (off), Import Audio, program change, panic, the monitor, Save to Bank, .fsvr presets, the right-click menus and a preset's Fseq after a reopen all pass\n");
    return fails ? 1 : 0;
}
