// fsvr/egview.h
//
// Calculates the shape of an EG curve for the editor's envelope plots.
// It uses the engine's constants and tables, but solves each stage directly instead of stepping.
// It is an approximation: selftest.cpp checks it against the engine's own EGs.
//
// FS1R envelope times and levels are small integers, so a handle has few valid positions.
// Snapping to those would make dragging jagged.
// So the inputs here are doubles: the plot draws in-between values during a drag,
// and the editor sends the rounded values to the engine.
//
// Contributed by Wouter van Nifterick (GitHub: WouterVanNifterick).

#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "../fs1r/hardware.h"
#include "../fs1r/chips/cal.h"
#include "../fs1r/firmware/tables.h"

namespace egview {

enum EgKind { AMP, PITCH, FILTER };

struct Input {
    int egKind = AMP;
    double L[4] = {}, T[4] = {};   // levels and times 1-4, 0..99 (pitch and filter levels 0..100)
    double init = 50, hold = 0;    // pitch level 0 (0..100), amplitude hold (0..99)
    int timeScale = 0, velSens = 0, range = 0;
    int part[4] = {64, 64, 64, 64};   // part offsets. Amp: attack, decay, release. Pitch: init, attack, release level, release time
    int note = 60, vel = 64;          // the note we assume: middle C, medium velocity
};

// The curve as vertices: time in seconds, level in the EG's own unit.
// Vertices [0, keyOff) run from key on. The rest is the release, with time starting at 0 again.
// corner[i] is the vertex where point i is reached, -1 if never.
// Amp has 5 points (end of hold, levels 1-4), pitch and filter have 4 (levels 1-4).
struct EgCurve {
    std::vector<float> t, v;
    int keyOff = 0;
    std::vector<int> corner;
    double lo = 0, hi = 1;
};

inline int ci(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// f at a fractional x, interpolated between the two whole values around it.
template <class F> double between(double x, F f) {
    int i = static_cast<int>(std::floor(x));
    double u = x - i;
    return u == 0 ? f(i) : f(i) + u * (f(i + 1) - f(i));
}

// Copies from fs1r/internal.h (egrate, rate_secs, eg_ratescale, EG::lvl_db). Keep them in step.
// The KEYOFF table is measured, see docs/aeg.md.
inline int eg_rate(int t) { return ((99 - ci(t, 0, 99)) * 0xA4) >> 8; }
inline double rate_secs(int q) { q = ci(q, 0, 63); return std::pow(2.0, 26 - (q >> 2)) / (4 + (q & 3)) / SR; }
inline int rate_scale(int tscale, int c0) {
    static constexpr signed char KEYOFF[37] = {-13, -13, -13, -13, -13, -12, -12, -10, -10, -8, -8, -5, -5, -4, -4, -2, -2, -2, -1,
                                           1, 2, 2, 2, 4, 4, 5, 5, 8, 8, 10, 10, 12, 12, 13, 13, 13, 13};
    int x = (tscale & 7) * KEYOFF[ci(c0 - 77, 0, 36)];
    return x < 0 ? -((-x) >> 3) : x >> 3;
}
inline double lvl_db(int a) { return a >= 63 ? -200.0 : -cal::EG_LEVEL_DB * a; }

struct Path {
    EgCurve c;
    double t = 0;
    void put(double v) { c.t.push_back(static_cast<float>(t)), c.v.push_back(static_cast<float>(v)); }
    void corner(int i) { c.corner[(size_t)i] = static_cast<int>(c.t.size()) - 1; }
    void keyOff(double v) { c.keyOff = static_cast<int>(c.t.size()), t = 0, put(v); }
    // Adds n vertices over d seconds, following f(u) for u in (0, 1) and ending on `end`.
    template <class F> void along(double d, int n, F f, double end) {
        for (int k = 1; k < n; ++k) t += d / n, put(f(static_cast<double>(k) / n));
        t += d / n, put(end);
    }
};

// Amplitude EG, after struct EG in fs1r/internal.h. Levels in dB.
// A decay is a straight line in dB, 96 dB per rate_secs.
// A rise starts at the attack floor and approaches EG_OVERSHOOT exponentially, stopping at its target.
// A stage lasts at least one sample. Level 3 is the sustain, and below -120 dB the note is over.
inline EgCurve amp(const Input& in) {
    Path p;
    p.c.lo = -120, p.c.hi = 0, p.c.corner.assign(5, -1);
    const int rs = rate_scale(in.timeScale, (NOTETAB[ci(in.note, 0, 127)] >> 8) + 10);
    auto db = [&](int i) { return between(in.L[i], [](int l) { return lvl_db(LEVTAB[ci(l, 0, 99)] >> 1); }); };
    auto secs = [&](int i) {
        const int off = (i == 0 ? in.part[0] : i == 3 ? in.part[2] : in.part[1]) - 64;   // the part's decay offset applies to T2 and T3
        return between(in.T[i], [&](int t) { return rate_secs(eg_rate(t + off) + rs); });
    };
    const double hold = between(in.hold, [](int h) {   // +4, except at 0x3F which means no hold
        int hr = eg_rate(h);
        if (hr < 0x3F) hr = std::min(hr + 4, 0x3E);
        return hr < 0x3F ? (rate_secs(hr) * cal::EG_HOLD_FRAC + cal::EG_HOLD_LAG) * SR : 0.0;
    });
    double cur = db(3);
    auto segment = [&](double to, double sec, double stop) {
        if (to > cur) {
            if (cur < cal::EG_ATTACK_FLOOR) p.put(cur = std::min(to, cal::EG_ATTACK_FLOOR));
            const double r = 1 - std::exp(-1 / (sec * cal::EG_ATTACK_K * SR + 1)), ov = cal::EG_OVERSHOOT, c0 = cur;
            const double n = std::max(1.0, std::log((ov - to) / (ov - c0)) / std::log(1 - r));
            p.along(n / SR, 16, [&](double u) { return std::min(to, ov - (ov - c0) * std::pow(1 - r, n * u)); }, cur = to);
        } else {
            const double end = std::max(to, stop), c0 = cur, n = std::max(1.0, (c0 - end) * (sec * SR + 1) / 96);
            p.along(n / SR, 8, [&](double u) { return c0 + (end - c0) * u; }, cur = end);
        }
    };
    p.put(cur);
    if (hold >= 1) p.t = hold / SR, p.put(cur);
    p.corner(0);
    for (int s = 0; s < 3; ++s) segment(db(s), secs(s), s == 2 ? -120 : -1e9), p.corner(s + 1);
    p.keyOff(cur);
    segment(db(3), secs(3), -120);
    p.corner(4);
    return p.c;
}

// The pitch and filter EGs run per tick, and the engine ends a stage on a whole tick.
// We add half a tick instead of rounding up, so dragging stays smooth.
inline double ticks(double n) { return std::max(1.0, n + 0.5); }

// Pitch EG, after Synth::setup_peg and peg_tick. Levels in semitones.
// Each stage is a straight ramp and lasts at least one tick.
// Rate 255 (time 0) jumps to the level at once.
inline EgCurve pitch(const Input& in) {
    Path p;
    const int velP = in.velSens ? in.vel + 1 : 128, range = in.range & 3, kf = KEYFACT[ci(in.note, 0, 127)] >> 2;
    const double span = (((128 << 7) >> (range ? range + 1 : 0)) >> 2) * 12.0 / 1024;
    p.c.lo = -span, p.c.hi = span, p.c.corner.assign(4, -1);
    const double Lp[5] = {std::clamp(in.init + in.part[0] - 64, 0.0, 100.0), in.L[0], in.L[1], in.L[2], std::clamp(in.L[3] + in.part[2] - 64, 0.0, 100.0)};
    const double Tp[5] = {0, std::clamp(in.T[0] + in.part[1] - 64, 0.0, 99.0), in.T[1], in.T[2], std::clamp(in.T[3] + in.part[3] - 64, 0.0, 99.0)};
    auto word = [&](int i) {
        return between(Lp[i], [&](int l) {
            int w = (((int)PEGLVL[ci(l, 0, 100)] - 128) << 7) * velP >> 7;
            return double(range ? w >> (range + 1) : w);
        });
    };
    auto rate = [&](int i) {
        return between(Tp[i], [&](int t) {
            int r = PEGTIME[ci(t, 0, 99)];
            return double(r == 255 ? 255 : std::min(254, ((r * (velP + 1)) >> 7) + std::min(255, in.timeScale * kf)));
        });
    };
    auto semis = [](double w) { return w / 4 * 12 / 1024; };   // the register takes word >> 2, at 1024 per octave
    double w = word(rate(1) >= 255 ? 1 : 0);
    auto segment = [&](int i) {
        const double to = word(i), r = rate(i), n = r >= 255 ? 1 : ticks(std::abs(to - w) / r);
        p.along(n / TICK_HZ, 1, [](double) { return 0.0; }, semis(w = to));
    };
    p.put(semis(w));
    int s = 1;
    if (rate(1) >= 255) p.corner(0), s = 2;
    for (; s <= 3; ++s) segment(s), p.corner(s - 1);
    p.keyOff(semis(w));
    segment(4);
    p.corner(3);
    return p.c;
}

// Filter EG, after Synth::start_filter and StepEG. Levels -50..50, starting at level 4.
// A stage aims half its swing past the target and stops when it gets there.
// A stage with no swing holds for FEG_FLAT_S.
inline EgCurve filter(const Input& in) {
    Path p;
    p.c.lo = -50, p.c.hi = 50, p.c.corner.assign(4, -1);
    auto word = [&](int i) {
        return between(in.L[i], [](int l) { return static_cast<double>((l - 50) * 256 / 50); });
    };
    auto coef = [&](int i) {
        return between(in.T[i], [&](int t) {
            t = ci(t, 0, 99);
            int t2 = t - ((in.note - 60) * t * in.timeScale) / 0x57F;
            if (i == 0) t2 -= ((in.vel - 127) * t * in.velSens) / 0x6F2;
            return cal::FEG_RATE_K * std::pow(2.0, -FEGRATE[ci(t2, 0, 99)] / 15.5);
        });
    };
    auto shown = [](double w) { return w * 50 / 256; };
    double w = word(3);
    auto segment = [&](int i) {
        const double to = word(i), c0 = w, k = coef(i);
        if (to == c0) return p.along(int(cal::FEG_FLAT_S * TICK_HZ) / TICK_HZ, 1, [](double) { return 0.0; }, shown(c0));
        const double aim = std::clamp(to + (to - c0) / 2, -511.0, 511.0);
        const double n0 = k >= 1 ? 0 : std::log((aim - to) / (aim - c0)) / std::log(1 - k), n = ticks(n0);
        p.along(n / TICK_HZ, 12, [&](double u) { return shown(aim - (aim - c0) * std::pow(1 - k, std::min(n * u, n0))); }, shown(w = to));
    };
    p.put(shown(w));
    for (int s = 0; s < 3; ++s) segment(s), p.corner(s);
    p.keyOff(shown(w));
    segment(3);
    p.corner(3);
    return p.c;
}

inline EgCurve curve(const Input& in) { return in.egKind == PITCH ? pitch(in) : in.egKind == FILTER ? filter(in) : amp(in); }

}  // namespace egview
