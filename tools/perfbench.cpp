// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// perfbench - the engine's cost on fixed scenarios, and its output hashed, so builds compare.
//
//   build.bat perf                        bin\perfbench.exe (CMake: the perfbench target)
//   perfbench --list                      the scenarios and the performances they play
//   perfbench [--set quick|standard|full|name,name,...] [--runs K] [--core N | --no-pin] [--rate 48000|44100|both]
//             [--speed 0..3|all] [--block N] [--seconds S] [--label TEXT] [--json FILE] [--save DIR] [--ref DIR]
//   perfbench --check-blocks              a held chord in 512-frame blocks and in one block hash alike
//   perfbench --check-factory --rom PATH  every performance loaded from the blobs and from the EPROM hashes alike
//
// A scenario is a factory performance (plugin/generated, no EPROM needed) and a phrase: a chord held, a chord
// struck twice a second, the demo song, or nothing. It plays through fsvr::Device in host-sized blocks with
// the MIDI delivered at block starts, and the thread's CPU time inside process() over the rendered seconds
// is the cost, as a share of one core: the minimum and the median of K runs, pinned to one core. The 16-bit
// output is hashed as a WAV holds it (FNV-1a over the interleaved samples); --save keeps it and --ref
// compares a later build against it: A when every sample is the same, B when none moved more than one
// step, else FAIL (exit 2). Runs of one scenario that hash unalike fail too: the engine is deterministic.
//
// The protocol is docs/performance.md's: a P-core, K=3, the median, the builds alternated in one session.
#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#else
#include <time.h>
#ifdef __linux__
#include <sched.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#endif
#endif
#include "fs1r.h"
#include "io/factory.h"
#include "io/smf.h"
#include "engine/tuning.h"
#include "math/fastmath.h"

namespace {

// ---------------------------------------------------------------------------------------- the scenarios
enum Drive { IDLE, HELD, STRUCK, SMF };
enum Set { QUICK = 1, STANDARD = 2, FULL = 3 };   // quick is in standard is in full
struct Scenario { const char* name; int perf; Drive drive; int notes; double secs; const char* smf; int set;
                  int release = 0;    // EG time 4 written to every operator before the phrase, 0 for the performance's own
                  int voice = -1;     // a factory voice (0..1407) loaded into every part, -1 for the performance's own
                  int filter = -1;    // every part's filter switch written 0 or 1, -1 for the performance's own
                  int poly = -1;      // every part's Mono/Poly byte written 0 or 1, -1 for the performance's own
                  int chans = -1; };  // the polyphony (Device::setPolyphony: 32, 64, 96 or 128), -1 for the unit's 32

// Performances by their EPROM index: A001 Zap! 0, A014 Homy 13, A020 Vox Morph 19, A088 Fat Line 87,
// B014 Full Tines 141, B053 Teck Hook 180. A chord is the first `notes` of CHORD; more than eight notes run up from 36.
// Teck Hook is four parts of one voice, the one four-part sustained sound in the factory set; its parts are mono, so
// superpad4 sets them poly and puts A109 SuperPad, eight voiced and eight unvoiced operators with an eight-second
// release, in every part, as an editor would: eight notes take all 32 channels with every operator live. With the
// parts' filters off the release is the amplitude EG's (a filter EG closes at key-off), and a struck chord's releases
// ring under the next one, which steals every channel. superpad4f keeps Teck Hook's filters, on in every part: 32
// channels and 32 filter slots.
// The -pN rows set the polyphony (Options > Polyphony, docs/Differences.md) and hold enough keys to fill it: 64 keys of
// Vox Morph's two layered parts are 128 channels, 32 keys of the four-part pad likewise, with or without its 128
// filter slots; held1-zap-p128 is one filtered note with eight filter banks to run, what the setting costs before a
// chord is played. More than eight notes run up from 36, so 64 keys end at 99.
// For a song, secs is the tail after its last event. A scenario's definition never changes: one that
// must gets a new name, so a number stays comparable over the record.
const int SCHEMA = 1;
const Scenario SCENARIOS[] = {
    {"idle-zap",          0,   IDLE,   0,  10, nullptr,                         QUICK},
    {"held1-zap",         0,   HELD,   1,  10, nullptr,                         STANDARD},
    {"held8-zap",         0,   HELD,   8,  10, nullptr,                         STANDARD},
    {"held8-homy",        13,  HELD,   8,  10, nullptr,                         STANDARD},
    {"held8-voxmorph",    19,  HELD,   8,  10, nullptr,                         QUICK},
    {"held8-fatline",     87,  HELD,   8,  10, nullptr,                         STANDARD},
    {"held8-fulltines",   141, HELD,   8,  10, nullptr,                         STANDARD},
    {"held32-voxmorph",   19,  HELD,   32, 10, nullptr,                         STANDARD},
    {"struck8-zap",       0,   STRUCK, 8,  10, nullptr,                         STANDARD},
    {"struck8-voxmorph",  19,  STRUCK, 8,  10, nullptr,                         STANDARD},
    {"struck8-fatline",   87,  STRUCK, 8,  10, nullptr,                         QUICK},
    {"struck8-fulltines", 141, STRUCK, 8,  10, nullptr,                         STANDARD},
    {"held8-superpad4",   180, HELD,   8,  10, nullptr,                         STANDARD, 0, 108, 0, 1},
    {"struck8-superpad4", 180, STRUCK, 8,  10, nullptr,                         STANDARD, 0, 108, 0, 1},
    {"held8-superpad4f",  180, HELD,   8,  10, nullptr,                         STANDARD, 0, 108, -1, 1},
    {"held1-zap-p128",    0,   HELD,   1,  10, nullptr,                         STANDARD, 0, -1,  -1, -1, 128},
    {"held32-voxmorph-p64",    19,  HELD, 32, 10, nullptr,                      FULL,     0, -1,  -1, -1, 64},
    {"held64-voxmorph-p128",   19,  HELD, 64, 10, nullptr,                      STANDARD, 0, -1,  -1, -1, 128},
    {"held32-superpad4-p128",  180, HELD, 32, 10, nullptr,                      FULL,     0, 108, 0,  1,  128},
    {"held32-superpad4f-p128", 180, HELD, 32, 10, nullptr,                      STANDARD, 0, 108, -1, 1,  128},
    {"demo-fatline",      -1,  SMF,    0,  2,  "captures/demo/04_Fat Line.mid", STANDARD},
    {"demo-all",          -1,  SMF,    0,  2,  "captures/demo/all.mid",         FULL},
};
const int CHORD[8] = {48, 52, 55, 59, 62, 66, 69, 72};

struct Event { double t; std::vector<uint8_t> bytes; };

// The phrase, and how long it plays.
bool phrase(const Scenario& s, const std::string& root, double override, std::vector<Event>& ev, double& secs) {
    secs = override > 0 ? override : s.secs;
    auto note = [&](double t, int n, bool on) { ev.push_back({t, {(uint8_t)(on ? 0x90 : 0x80), (uint8_t)n, (uint8_t)(on ? 100 : 0)}}); };
    if (s.drive == HELD) for (int i = 0; i < s.notes; i++) note(0, s.notes > 8 ? 36 + i : CHORD[i], true);
    if (s.drive == STRUCK)
        for (double t = 0; t + 0.25 < secs; t += 0.5)
            for (int i = 0; i < s.notes; i++) { note(t, CHORD[i], true); note(t + 0.25, CHORD[i], false); }
    if (s.drive == SMF) {
        std::vector<fs1r_smf::SmfEvent> smf;
        if (!fs1r_smf::read_smf((root + "/" + s.smf).c_str(), smf)) return false;
        for (auto& e : smf) ev.push_back({e.t, e.bytes});
        secs = smf.back().t + s.secs;
    }
    return true;
}

// ---------------------------------------------------------------------------------------- the clock
double cpu_seconds() {
#ifdef _WIN32
    FILETIME c, e, k, u; GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
    auto t = [](FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32 | f.dwLowDateTime) * 1e-7; };
    return t(k) + t(u);
#else
    timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}
uint64_t cycles() {
#ifdef _WIN32
    ULONG64 c = 0; QueryThreadCycleTime(GetCurrentThread(), &c); return c;
#elif defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    return 0;
#endif
}
bool pin(int core) {
#ifdef _WIN32
    return SetThreadAffinityMask(GetCurrentThread(), 1ull << core) != 0 && SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#elif defined(__linux__)
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(core, &s); return sched_setaffinity(0, sizeof s, &s) == 0;
#else
    (void)core; return false;   // macOS has no hard affinity
#endif
}
std::string cpu_name() {
    char name[49] = {};
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    for (int i = 0; i < 3; i++) {
        int r[4];
#ifdef _WIN32
        __cpuid(r, 0x80000002 + i);
#else
        __get_cpuid(0x80000002 + i, (unsigned*)&r[0], (unsigned*)&r[1], (unsigned*)&r[2], (unsigned*)&r[3]);
#endif
        memcpy(name + 16 * i, r, 16);
    }
#else
    strcpy(name, "unknown");
#endif
    std::string s = name;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}
const char* os_name() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
}
std::string compiler_name() {
#if defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#else
    return "unknown";
#endif
}

// ---------------------------------------------------------------------------------------- one run
struct Run { double cpu = 0; uint64_t cycles = 0, hash = 0; int64_t frames = 0; int chans = 0; std::vector<int16_t> pcm; };

const uint64_t FNV_BASIS = 1469598103934665603ull, FNV_PRIME = 1099511628211ull;

// The scenario through a fresh device: the phrase in blocks, the CPU time inside process() summed, the
// output hashed and, when asked, kept.
Run render(const Scenario& s, const std::vector<Event>& ev, double secs, const fs1r_factory::Blobs& b, const char* rom,
           double rate, int speed, int block, bool keep) {
    fsvr::Device dev;
        if (rom && !dev.loadRom(rom)) { printf("cannot read %s\n", rom); exit(1); }
    dev.setSampleRate(rate);
    dev.setSpeed(speed);
    if (s.chans > 0) dev.setPolyphony(s.chans);
    if (s.drive != SMF) dev.forceChannel(0);     // every part with a receive channel plays the chord
    if (s.perf >= 0 && !(rom ? dev.loadRomPerformance(s.perf) : fs1r_factory::performance(dev, b, s.perf))) {
        printf("cannot load performance %d\n", s.perf); exit(1);
    }
    if (s.voice >= 0)
        for (int p = 0; p < 4; p++)
            if (!(rom ? dev.loadRomVoice(p, s.voice) : fs1r_factory::voice(dev, b, p, s.voice))) { printf("cannot load voice %d\n", s.voice); exit(1); }
    if (s.poly >= 0)                             // part byte 5, Mono/Poly
        for (int p = 0; p < 4; p++) {
            const uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)(0x30 + p), 0, 5, 0, (uint8_t)s.poly, 0xF7};
            dev.sendMidi(m, 10);
        }
    if (s.filter >= 0)                           // part byte 7, the filter switch
        for (int p = 0; p < 4; p++) {
            const uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)(0x30 + p), 0, 7, 0, (uint8_t)s.filter, 0xF7};
            dev.sendMidi(m, 10);
        }
    if (s.release)                               // EG time 4 of every voiced (byte 19) and unvoiced (35 + 20) operator
        for (int p = 0; p < 4; p++)
            for (int o = 0; o < 8; o++)
                for (int al : {19, 55}) {
                    const uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)(0x60 + p), (uint8_t)o, (uint8_t)al, 0, (uint8_t)s.release, 0xF7};
                    dev.sendMidi(m, 10);
                }
    Run run; run.frames = (int64_t)(secs * rate);
    if (keep) run.pcm.reserve((size_t)run.frames * 2);
    std::vector<float> l(block), r(block);
    uint64_t h = FNV_BASIS;
    size_t next = 0;
    const tuning::NoDenormals flush;             // as the plug-in runs it
    for (int64_t at = 0; at < run.frames; at += block) {
        const int n = (int)std::min<int64_t>(block, run.frames - at);
        while (next < ev.size() && (int64_t)(ev[next].t * rate) <= at) { dev.sendMidi(ev[next].bytes.data(), ev[next].bytes.size()); next++; }
        const double c0 = cpu_seconds(); const uint64_t y0 = cycles();
        dev.process(l.data(), r.data(), n);
        run.cycles += cycles() - y0; run.cpu += cpu_seconds() - c0;
        for (int i = 0; i < n; i++) {
            const int16_t a = fs1r_smf::to_s16(l[i]), c = fs1r_smf::to_s16(r[i]);
            const uint8_t bytes[4] = {(uint8_t)a, (uint8_t)(a >> 8), (uint8_t)c, (uint8_t)(c >> 8)};
            for (uint8_t x : bytes) h = (h ^ x) * FNV_PRIME;
            if (keep) { run.pcm.push_back(a); run.pcm.push_back(c); }
        }
    }
    run.hash = h; run.chans = dev.renderingChannels();   // the channels the engine still computes at the end
    return run;
}

// ---------------------------------------------------------------------------------------- references
struct Diff { bool found = false, length = true; int64_t moved = 0, first = -1; int maxStep = 0; };
const char* verdict(const Diff& d) { return !d.found ? "-" : !d.length ? "FAIL" : d.maxStep == 0 ? "A" : d.maxStep <= 1 ? "B" : "FAIL"; }

std::string raw_name(const Scenario& s, double rate, int speed) {
    std::string n = s.name;
    if (rate != 48000) n += ".r" + std::to_string((int)rate);
    if (speed) n += ".s" + std::to_string(speed);
    return n + ".raw";
}
Diff compare(const std::vector<int16_t>& pcm, const std::string& path) {
    Diff d;
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return d;
    d.found = true;
    std::vector<int16_t> ref; int16_t tmp[8192]; size_t n;
    while ((n = fread(tmp, 2, 8192, f)) > 0) ref.insert(ref.end(), tmp, tmp + n);
    fclose(f);
    if (ref.size() != pcm.size()) { d.length = false; return d; }
    for (size_t i = 0; i < pcm.size(); i++) {
        const int step = std::abs((int)pcm[i] - (int)ref[i]);
        if (!step) continue;
        d.moved++; d.maxStep = std::max(d.maxStep, step);
        if (d.first < 0) d.first = (int64_t)(i / 2);
    }
    return d;
}
bool save(const std::vector<int16_t>& pcm, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    const bool ok = fwrite(pcm.data(), 2, pcm.size(), f) == pcm.size();
    fclose(f);
    return ok;
}

struct Row {
    std::string name, file; double rate; int speed, block; double secs; int runs;
    std::vector<double> pct; double cyclesPerSample; int chans; uint64_t hash; Diff diff;
};

void usage() {
    printf("usage: perfbench [--list] [--set quick|standard|full|name,...] [--runs K] [--core N | --no-pin]\n"
           "                 [--rate 48000|44100|both] [--speed 0..3|all] [--block N] [--seconds S] [--label TEXT]\n"
           "                 [--json FILE] [--save DIR] [--ref DIR] [--rom PATH] [--check-blocks] [--check-factory]\n"
           "                 [--release T]   EG time 4 of every operator written to T, to try a release length; a scenario's own value is the record\n"
           "                 [--perf N]      the performance played instead of the scenario's, to try a shell; the scenario's own is the record\n");
}

}   // namespace

int main(int argc, char** argv) {
    std::string set = "standard", label, json, saveDir, refDir, rom;
    int runs = 3, core = 2, speedArg = 0, block = 512, releaseArg = 0, perfArg = -1;
    bool noPin = false, list = false, checkBlocks = false, checkFactory = false, allSpeeds = false, bothRates = false;
    double rate = 48000, seconds = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { if (i + 1 >= argc) { usage(); exit(1); } return argv[++i]; };
        if (a == "--list") list = true;
        else if (a == "--set") set = next();
        else if (a == "--runs") runs = std::max(1, atoi(next()));
        else if (a == "--core") core = atoi(next());
        else if (a == "--no-pin") noPin = true;
        else if (a == "--rate") { std::string v = next(); if (v == "both") bothRates = true; else rate = atof(v.c_str()); }
        else if (a == "--speed") { std::string v = next(); if (v == "all") allSpeeds = true; else speedArg = atoi(v.c_str()); }
        else if (a == "--block") block = std::max(1, atoi(next()));
        else if (a == "--seconds") seconds = atof(next());
        else if (a == "--release") releaseArg = atoi(next());
        else if (a == "--perf") perfArg = atoi(next());
        else if (a == "--label") label = next();
        else if (a == "--json") json = next();
        else if (a == "--save") saveDir = next();
        else if (a == "--ref") refDir = next();
        else if (a == "--rom") rom = next();
        else if (a == "--check-blocks") checkBlocks = true;
        else if (a == "--check-factory") checkFactory = true;
        else { usage(); return 1; }
    }
    fs1r_factory::Blobs blobs;
    if (!blobs.load(argv[0])) { printf("cannot find plugin/generated: run from the repository, or set FSVR_ROOT\n"); return 1; }

    // The scenarios asked for: a set, or names.
    std::vector<const Scenario*> picked;
    for (const Scenario& s : SCENARIOS) {
        if (set == "quick" || set == "standard" || set == "full") { if (s.set <= (set == "quick" ? QUICK : set == "standard" ? STANDARD : FULL)) picked.push_back(&s); }
        else if ((("," + set + ",").find("," + std::string(s.name) + ",")) != std::string::npos) picked.push_back(&s);
    }
    if (picked.empty()) { printf("no such scenario: %s\n", set.c_str()); return 1; }
    if (set == "quick" && seconds == 0) seconds = 4;

    if (list) {
        for (const Scenario& s : SCENARIOS) {
            fsvr::Device dev;
            if (s.perf >= 0) fs1r_factory::performance(dev, blobs, s.perf);
            printf("%-18s %s%-3d %-14s", s.name, s.perf >= 0 ? "perf " : "", s.perf, s.perf >= 0 ? dev.performanceName() : s.smf);
            if (s.drive == HELD) printf("  %d note%s held %g s", s.notes, s.notes > 1 ? "s" : "", s.secs);
            if (s.drive == STRUCK) printf("  %d notes every 0.5 s, %g s", s.notes, s.secs);
            if (s.drive == IDLE) printf("  nothing played, %g s", s.secs);
            if (s.drive == SMF) printf("  the song and a %g s tail", s.secs);
            printf("  [%s]\n", s.set == QUICK ? "quick" : s.set == STANDARD ? "standard" : "full");
        }
        return 0;
    }

    const bool pinned = !noPin && pin(core);
    if (!noPin && !pinned) printf("note: could not pin to core %d\n", core);

    if (checkBlocks) {
        // The same chord in host blocks and in one block: a block boundary must not change a sample.
        const Scenario& s = SCENARIOS[2];
        std::vector<Event> ev; double secs; phrase(s, blobs.root, 0, ev, secs);
        const Run a = render(s, ev, secs, blobs, nullptr, 48000, 0, 512, false);
        const Run b = render(s, ev, secs, blobs, nullptr, 48000, 0, (int)(secs * 48000), false);
        printf("check-blocks: %s in 512-frame blocks %016llx, in one block %016llx: %s\n", s.name,
               (unsigned long long)a.hash, (unsigned long long)b.hash, a.hash == b.hash ? "ok" : "FAIL");
        return a.hash == b.hash ? 0 : 2;
    }
    if (checkFactory) {
        // Every performance the scenarios play, loaded from the blobs and from the EPROM, one second of the chord.
        if (rom.empty()) { printf("--check-factory needs --rom\n"); return 1; }
        int fails = 0;
        for (const Scenario& s : SCENARIOS) {
            if (s.perf < 0 || s.drive == IDLE) continue;
            std::vector<Event> ev; double secs; phrase(s, blobs.root, 1, ev, secs);
            const Run a = render(s, ev, secs, blobs, nullptr, 48000, 0, 512, false);
            const Run b = render(s, ev, secs, blobs, rom.c_str(), 48000, 0, 512, false);
            printf("check-factory: %-18s blobs %016llx  eprom %016llx  %s\n", s.name, (unsigned long long)a.hash,
                   (unsigned long long)b.hash, a.hash == b.hash ? "ok" : "FAIL");
            fails += a.hash != b.hash;
        }
        return fails ? 2 : 0;
    }

    std::vector<double> rates{rate}; if (bothRates) rates = {48000, 44100};
    std::vector<int> speeds{speedArg}; if (allSpeeds) speeds = {0, 1, 2, 3};
    std::vector<Row> rows;
    int fails = 0;
    printf("kernels: %s\n", fsvr::Device::simd());
    printf("%-18s %5s %2s %5s %5s  %7s %7s %9s %2s  %-16s %s\n", "scenario", "rate", "sp", "block", "secs", "min%", "med%", "cyc/smp", "ch", "hash", refDir.empty() ? "" : "ref");
    for (const Scenario* s : picked)
        for (double rt : rates)
            for (int sp : speeds) {
                std::vector<Event> ev; double secs;
                if (!phrase(*s, blobs.root, seconds, ev, secs)) { printf("%-18s cannot read %s\n", s->name, s->smf); fails++; continue; }
                Row row{s->name, raw_name(*s, rt, sp), rt, sp, block, secs, runs, {}, 0, 0, 0, {}};
                Scenario sc = *s; if (releaseArg) sc.release = releaseArg; if (perfArg >= 0) sc.perf = perfArg;
                std::vector<int16_t> pcm; uint64_t hash = 0; bool same = true;
                for (int k = 0; k < runs; k++) {
                    const bool keep = k == runs - 1 && (!saveDir.empty() || !refDir.empty());
                    Run run = render(sc, ev, secs, blobs, nullptr, rt, sp, block, keep);
                    row.pct.push_back(100.0 * run.cpu / (run.frames / rt));
                    row.cyclesPerSample = (double)run.cycles / run.frames; row.chans = run.chans;
                    if (k && run.hash != hash) same = false;
                    hash = run.hash;
                    if (keep) pcm.swap(run.pcm);
                }
                row.hash = hash;
                std::sort(row.pct.begin(), row.pct.end());
                const double median = row.pct[row.pct.size() / 2];
                if (!refDir.empty()) row.diff = compare(pcm, refDir + "/" + row.file);
                if (!saveDir.empty() && !save(pcm, saveDir + "/" + row.file)) { printf("cannot write into %s\n", saveDir.c_str()); return 1; }
                printf("%-18s %5d %2d %5d %5.1f  %7.2f %7.2f %9.0f %2d  %016llx", s->name, (int)rt, sp, block, secs, row.pct.front(), median,
                       row.cyclesPerSample, row.chans, (unsigned long long)hash);
                if (!refDir.empty()) {
                    printf("  %s", verdict(row.diff));
                    if (row.diff.found && row.diff.length && row.diff.moved) printf(" (max %d step%s, %lld moved, first at %lld)", row.diff.maxStep, row.diff.maxStep > 1 ? "s" : "", (long long)row.diff.moved, (long long)row.diff.first);
                    if (row.diff.found && !row.diff.length) printf(" (length differs)");
                    if (!strcmp(verdict(row.diff), "FAIL")) fails++;
                }
                if (!same) { printf("  NONDETERMINISTIC"); fails++; }
                printf("\n");
                rows.push_back(row);
            }

    if (!saveDir.empty()) {
        FILE* f = fopen((saveDir + "/manifest.json").c_str(), "w");
        if (f) {
            fprintf(f, "{\"schema\": %d, \"label\": \"%s\", \"files\": {", SCHEMA, label.c_str());
            for (size_t i = 0; i < rows.size(); i++)
                fprintf(f, "%s\"%s\": \"%016llx\"", i ? ", " : "", rows[i].file.c_str(), (unsigned long long)rows[i].hash);
            fprintf(f, "}}\n");
            fclose(f);
        }
    }
    if (!json.empty()) {
        FILE* f = fopen(json.c_str(), "w");
        if (!f) { printf("cannot write %s\n", json.c_str()); return 1; }
        char date[32]; const time_t now = time(nullptr); strftime(date, sizeof date, "%Y-%m-%d", localtime(&now));
        fprintf(f, "{\n \"schema\": %d,\n \"label\": \"%s\",\n \"date\": \"%s\",\n", SCHEMA, label.c_str(), date);
        fprintf(f, " \"host\": {\"cpu\": \"%s\", \"os\": \"%s\", \"core\": %d, \"pinned\": %s},\n", cpu_name().c_str(), os_name(), core, pinned ? "true" : "false");
        fprintf(f, " \"build\": {\"compiler\": \"%s\", \"math\": \"%s\", \"simd\": \"%s\"},\n \"scenarios\": [\n", compiler_name().c_str(), fm::backend_name(), fsvr::Device::simd());
        for (size_t i = 0; i < rows.size(); i++) {
            const Row& r = rows[i];
            fprintf(f, "  {\"name\": \"%s\", \"rate\": %d, \"speed\": %d, \"block\": %d, \"seconds\": %g, \"runs\": %d, \"cpu_pct\": {\"min\": %.3f, \"median\": %.3f, \"all\": [",
                    r.name.c_str(), (int)r.rate, r.speed, r.block, r.secs, r.runs, r.pct.front(), r.pct[r.pct.size() / 2]);
            for (size_t k = 0; k < r.pct.size(); k++) fprintf(f, "%s%.3f", k ? ", " : "", r.pct[k]);
            fprintf(f, "]}, \"cycles_per_sample\": %.1f, \"chans\": %d, \"hash\": \"%016llx\"", r.cyclesPerSample, r.chans, (unsigned long long)r.hash);
            if (r.diff.found) fprintf(f, ", \"ref\": {\"max_step\": %d, \"moved\": %lld, \"tier\": \"%s\"}", r.diff.maxStep, (long long)r.diff.moved, verdict(r.diff));
            fprintf(f, "}%s\n", i + 1 < rows.size() ? "," : "");
        }
        fprintf(f, " ]\n}\n");
        fclose(f);
    }
    return fails ? 2 : 0;
}
