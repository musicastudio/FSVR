// device/device.cpp - fsvr::Device: host sample rate, state, the public surface. Ours.
#include "engine/synth.h"
#include "kernel/dispatch.h"

// ------------------------------------------------------------------------------------------ fsvr::Device
namespace fsvr {

// The engine runs at 48 kHz whatever the host's rate, and reaches any other rate through a Kaiser-windowed sinc,
// TAPS engine samples wide, cut at half the lower of the two rates: about 90 dB down by 24.2 kHz at a 44.1 kHz
// host, so nothing folds into the audible band, and flat to 19.9 kHz. It is tabulated at PHASES fractional
// positions and read between two of them. Its delay is TAPS / 2 engine samples, 0.67 ms.
//
// It replaced a four-point cubic whose end slopes were twice the central difference (Paul Bourke's form), which
// is worse than a straight line on anything smooth: at 44.1 kHz a lone sine came out with a line at 3900 Hz - f
// (48000 - 44100) at -63 dBc on C2, -51 on C4 and -25 on C8, and the linear interpolator the Fast steps used
// sat 22 to 35 dB under it. Either way the artefact belonged to the host rate rather than to the slider, which
// read it as Full being the worst setting (docs/performance.md, 2026-10-17). This costs under 1 % of a core.
struct Device::Impl {
    static constexpr int TAPS = 64, PHASES = 512;
    Synth s;
    Rom rom;
    double hostRate = ENGINE_RATE;
    double pos = 0;                          // fractional read position between engine samples
    float eng[2][256]; int engFill = 0, engRead = 0;
    float hist[2][2 * TAPS] = {}; int at = 0;   // the last TAPS engine samples, each written twice so a window is contiguous
    std::vector<float> kern = std::vector<float>((PHASES + 1) * TAPS);   // phase-major, built by setSampleRate
    bool echo = false;
    inline void pull() {
        if (engRead >= engFill) {
            engFill = 256; engRead = 0;
            s.render(eng[0], eng[1], engFill);
        }
        for (int c = 0; c < 2; c++) hist[c][at] = hist[c][at + TAPS] = eng[c][engRead];
        at = (at + 1) % TAPS;
        engRead++;
    }
    // The output between the window's two middle samples, frac of the way from the earlier.
    inline void read(double frac, float& l, float& r) const {
        const double x = frac * PHASES; const int ph = std::min((int)x, PHASES - 1); const float w = (float)(x - ph);
        const float* k0 = &kern[ph * TAPS]; const float* k1 = k0 + TAPS;
        const float* a = hist[0] + at; const float* b = hist[1] + at;
        float yl = 0, yr = 0;
        for (int k = 0; k < TAPS; k++) { const float c = k0[k] + w * (k1[k] - k0[k]); yl += c * a[k]; yr += c * b[k]; }
        l = yl; r = yr;
    }
    void design() {
        const double cut = std::min(1.0, hostRate / ENGINE_RATE), beta = 9.0;   // cut against the engine's Nyquist
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; k++) { t *= x * x / (4.0 * k * k); s += t; } return s; };
        for (int p = 0; p <= PHASES; p++) {
            double sum = 0; float* k = &kern[p * TAPS];
            for (int j = 0; j < TAPS; j++) {
                const double t = j - (TAPS / 2 - 1) - (double)p / PHASES, u = t / (TAPS / 2);   // t: engine samples from the output
                const double sinc = t == 0 ? 1.0 : sin(PI * cut * t) / (PI * cut * t);
                const double win = std::fabs(u) >= 1 ? 0.0 : i0(beta * sqrt(1 - u * u)) / i0(beta);
                k[j] = (float)(cut * sinc * win); sum += k[j];
            }
            for (int j = 0; j < TAPS; j++) k[j] = (float)(k[j] / sum);                  // unity at DC at every phase
        }
    }
};

Device::Device() : p(new Impl) { init_tables(); }
Device::~Device() = default;

void Device::setSampleRate(double hostRate) { if (hostRate > 1000 && hostRate != p->hostRate) { p->hostRate = hostRate; p->design(); } }
double Device::sampleRate() const { return p->hostRate; }
void Device::setGain(double g) { p->s.gain = g; }
void Device::setEchoParameters(bool on) { p->echo = on; }

// Ours, a user's choice, so unlike engine/tuning.h's knobs every step but Full changes the output. Full gates at
// -100 dB, under the 16-bit floor after OUT_GAIN, and runs the control maths every 16 samples and the EGs every
// sample. Good gates at -90 dB, which takes out the operators parked at output level 0 (-96 dB), and halves the
// control rate. Fast and Fastest gate at -80 and -72 dB, which also lets a release tail under the gate end its
// channel sooner, and advance the amplitude EGs 4 and 8 samples at a time (render_chan). Measured over 8-note
// chords struck twice a second, Fastest is 32 % under Full on A020 Vox Morph, 30 % on B014 Full Tines and 8 % on
// A001 Zap!, whose cost is the filter and the effects, which no step touches (docs/performance.md, 2026-10-17).
void Device::setSpeed(int s) {
    static const struct { double gateDb; int ctl, eg; } kS[4] = {{-100, tuning::CTL_DECIMATION, 1}, {-90, 32, 1}, {-80, 48, 4}, {-72, 64, 8}};
    const auto& k = kS[s < 0 ? 0 : s > 3 ? 3 : s];
    p->s.gateDb = k.gateDb, p->s.ctlDecimation = k.ctl, p->s.egDecimation = k.eg;
}
void Device::setFilterPoly32(bool on) { p->s.filter32 = on; }   // render latches it
void Device::setPolyphony(int voices) { p->s.poly = std::clamp(voices / FILTER_CHANNELS * FILTER_CHANNELS, (int)NCHAN, Synth::MAX_CHAN); }   // a bank of filter voices a sixteen; render latches it
int Device::polyphony() const { return p->s.poly; }
void Device::setBend14(bool on) { p->s.bend14 = on; }
void Device::setCutoffGlide(bool on) { p->s.cutoffGlide = on; }
void Device::forceChannel(int channel) { p->s.forceChannel = channel; }

void Device::process(float* outL, float* outR, int n) {
    if (p->hostRate == ENGINE_RATE) {        // the common case: no resampling at all
        p->s.render(outL, outR, n);
        return;
    }
    const double step = ENGINE_RATE / p->hostRate;
    for (int i = 0; i < n; i++) {
        p->pos += step;
        while (p->pos >= 1.0) { p->pos -= 1.0; p->pull(); }
        p->read(p->pos, outL[i], outR[i]);
    }
}

void Device::sendMidi(const uint8_t* b, size_t len) {
    if (!len) return;
    if (b[0] == 0xF0) {
        std::lock_guard<std::mutex> lk(p->s.mtx);
        if (load_sysex(p->s, p->rom.ok ? &p->rom : nullptr, b, len, 0, 0)) return;
        if (apply_param_change_locked(p->s, b, len) && p->echo && len >= 10 && (b[2] & 0xF0) == 0x10)
            p->s.push_param(b[4], b[5], b[6], b[7] << 7 | b[8]);
        return;
    }
    std::lock_guard<std::mutex> lk(p->s.mtx);
    p->s.midi_in(b[0], len > 1 ? b[1] & 0x7F : 0, len > 2 ? b[2] & 0x7F : 0);
}

bool Device::nextMidiOut(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    if (p->s.outQ.empty()) return false;
    out.swap(p->s.outQ.front());
    p->s.outQ.erase(p->s.outQ.begin());
    return true;
}

void Device::allNotesOff() { std::lock_guard<std::mutex> lk(p->s.mtx); p->s.all_off(); }

void Device::getState(std::vector<uint8_t>& sysex) const {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    Synth& S = p->s;
    S.outQ.clear();
    S.push_bulk(0x00, 0, 0, S.sys, 76);
    uint8_t perfBytes[400]; S.current_perf_bytes(perfBytes);
    S.push_bulk(0x10, 0, 0, perfBytes, 400);
    for (int i = 0; i < 4; i++) S.push_bulk(0x40 + i, 0, 0, S.perf.part[i].voice.raw, 608);
    if (S.fseq.valid) { std::vector<uint8_t> f; S.fseq_bytes(f); S.push_bulk(0x60, 0, 0, f.data(), (int)f.size()); }   // the current Fseq, Data List 3.2.1
    sysex.clear();
    for (auto& m : S.outQ) sysex.insert(sysex.end(), m.begin(), m.end());
    S.outQ.clear();
}

bool Device::setState(const uint8_t* d, size_t len) {
    bool any = false;
    for (size_t i = 0; i < len; ) {
        if (d[i] != 0xF0) { i++; continue; }
        size_t j = i + 1;
        while (j < len && d[j] != 0xF7) j++;
        if (j >= len) break;
        size_t n = j - i + 1;
        {
            std::lock_guard<std::mutex> lk(p->s.mtx);
            // The system block getState wrote: the firmware's loader takes no system bulk, and this is the
            // device's own state rather than a dump off the MIDI input, so it goes back byte for byte.
            if (n == 76 + 11 && d[i + 3] == 0x5E && d[i + 6] == 0x00) { memcpy(p->s.sys, d + i + 9, 76); any = true; }
            else if (load_sysex(p->s, p->rom.ok ? &p->rom : nullptr, d + i, n, 0, 0)) any = true;
            else if (apply_param_change_locked(p->s, d + i, n)) any = true;
        }
        i = j + 1;
    }
    // The Fseq bulk's loader gives a performance with no Fseq part the first one, as a load from the Fseq
    // page wants; restoring a saved state is not that, and the performance's own Fseq part stands.
    std::lock_guard<std::mutex> lk(p->s.mtx);
    p->s.fseqPart = (p->s.perf.c[0x15] & 7) ? (p->s.perf.c[0x15] & 7) - 1 : -1;
    return any;
}

bool Device::loadRom(const char* path) {
    if (!load_rom(p->rom, path)) return false;
    p->s.rom = &p->rom;
    return true;
}
bool Device::romLoaded() const { return p->rom.ok; }
bool Device::loadRomPerformance(int idx) { if (!p->rom.ok) return false; return rom_perf(p->s, p->rom, idx); }
bool Device::loadRomVoice(int part, int idx) {
    if (!p->rom.ok || part < 0 || part > 3) return false;
    std::lock_guard<std::mutex> lk(p->s.mtx);
    rom_voice(p->rom, idx, p->s.perf.part[part].voice);
    return true;
}
bool Device::loadRomFseq(int n) { if (!p->rom.ok) return false; return rom_fseq(p->s, p->rom, n); }
bool Device::loadSyx(const uint8_t* d, size_t len, int pick, int part) {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    return load_sysex(p->s, p->rom.ok ? &p->rom : nullptr, d, len, pick, part);
}

const char* Device::performanceName() const { return p->s.perf.name; }
const char* Device::voiceName(int part) const { auto& pt = p->s.perf.part[clampi(part, 0, 3)]; return pt.p[1] == 0 ? "off" : pt.voice.name; }   // the panel shows "off" for a part with no bank
int Device::algorithm(int part) const { return p->s.perf.part[clampi(part, 0, 3)].voice.alg; }
bool Device::partActive(int part) const { return p->s.perf.part[clampi(part, 0, 3)].rcv() != 0x7F; }
const char* Device::fseqName() const { return p->s.fseq.valid ? p->s.fseq.name : ""; }
int Device::fseqFrames() const { return p->s.fseq.valid ? p->s.fseq.nframes : 0; }
bool Device::fseqFrame(int step, uint8_t out[50]) const {
    if (!p->s.fseq.valid || step < 0 || step >= p->s.fseq.nframes) return false;
    memcpy(out, p->s.fseq.frame[step], 50);
    return true;
}
int Device::fseqPosition() const { return p->s.fseq.valid ? p->s.fseqStep : 0; }
void Device::operatorWave(int part, int op, float* out, int n) const {
    OpV v;
    {
        std::lock_guard<std::mutex> lk(p->s.mtx);
        v = p->s.perf.part[clampi(part, 0, 3)].voice.v[clampi(op, 0, 7)];
    }
    op_wave(v, out, n);
}
int Device::fseqPart() const { return p->s.fseqPart; }
double Device::fseqFrameSeconds(int s) { return (VELW[clampi(s, 0, 127)] * 84.0 + 2884.0) * 32.0 / CPU_HZ; }   // fseq_start at ratio 1000
// A frequency word, 1024 to the octave with 26861 at 440 Hz, and a frame level: notes.cpp refresh_regs
// doubles the byte into the level register, and the chip attenuates LEVEL_DB per register step (tonegen/channel.cpp).
int Device::fseqWord(double hz) { return hz > 0 ? clampi((int)lround(26861 + 1024 * log2(hz / 440.0)), 0, 0x7FFE) : 0; }
int Device::fseqLevel(double gain) { return gain > 0 ? clampi((int)lround(-20 * log10(gain) / (2 * LEVEL_DB)), 0, 127) : 127; }
int Device::activeVoices() const {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    return p->s.active_chans();
}
int Device::renderingChannels() const {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    int n = 0; for (auto& c : p->s.ch) n += c.active && !c.silent;
    return n;
}

const char* Device::simd() { return kernels().name; }

int Device::selfTest() {
    init_tables();
    auto sp = std::make_unique<Synth>();   // too big for the stack
    int fails = selftest(*sp);
    // A saved state with an Fseq loaded but no Fseq part comes back with none: the plug-in's session
    // restore once played the last Fseq on part 1 under a performance that has no Fseq.
    std::vector<uint8_t> f(9 + 32 + 128 * 50 + 2, 0), state;
    f[0] = 0xF0; f[1] = 0x43; f[3] = 0x5E; f[6] = 0x60; f.back() = 0xF7;
    Device a, b;
    a.sendMidi(f.data(), f.size());
    const uint8_t off[10] = {0xF0, 0x43, 0x10, 0x5E, 0x10, 0x00, 0x15, 0x00, 0x00, 0xF7};
    a.sendMidi(off, sizeof off);
    a.getState(state);
    b.setState(state.data(), state.size());
    if (a.fseqPart() != -1 || b.fseqPart() != -1) { printf("  FAIL state restore enables the Fseq (part %d)\n", b.fseqPart()); fails++; }
    return fails;
}

}  // namespace fsvr

