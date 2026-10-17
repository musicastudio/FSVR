// selftest/selftest.cpp - the engine self check. Ours, not the hardware's.
#include "engine/synth.h"
#include "display/display.h"
#include "display/egview.h"
#include <cstring>
#include <memory>

// ------------------------------------------------------------------------------------------ self test
// fsvr_console -selftest: every sysex path the plugin will use. Parameter change in, parameter request out,
// bulk dump out and straight back in, and the whole 400/608 byte state surviving the round trip.
// The effect section's type and parameter paths, as the Data List addresses them: reverb slots 1-8 are
// two bytes and 9-16 one byte, variation and insertion slots two bytes.
namespace fx {
namespace H = fsvr::vop3host;
enum Block { REVERB, VARIATION, INSERTION };
void setType(H::EffectHost& h, Block b, int type) {
    const int byte[3] = {H::REVERB_TYPE, H::VARIATION_TYPE, H::INSERTION_TYPE};
    h.setByte(byte[b], type);
}
void setParam(H::EffectHost& h, Block b, int slot, int word) {   // slot 1..16
    if (b == REVERB && slot > 8) { h.setByte(H::REVERB_PARAM_BYTES_9 + slot - 9, word); return; }
    const int i = (b == REVERB ? H::REVERB_PARAM_BYTES : b == VARIATION ? H::VARIATION_PARAM_BYTES : H::INSERTION_PARAM_BYTES) + 2 * (slot - 1);
    h.store(i + 1, word & 0x7f);
    h.setByte(i, word >> 7);
}
// A chip that hashes every register write, for the two hosts' pins. It reports the channel it is told to
// on EG_STATUS until the next EG_RATE write, as the filter chip does.
struct HashSink : H::WriteSink {
    uint64_t h = 1469598103934665603ull;
    uint32_t status = 0;
    uint32_t sel = 0; bool pending = false;   // a SELECT counts once a write it selects for follows it
    void write(uint32_t reg, uint32_t v) override {
        if (reg == vop3::EG_RATE) status = 0;
        if (reg == vop3::SELECT) { sel = v; pending = true; return; }
        if (pending) { h = (h ^ sel) * 1099511628211ull; pending = false; }
        h = (h ^ (reg << 16 | v)) * 1099511628211ull;
    }
    uint32_t read(uint32_t) override { return status; }
};
}  // namespace fx

static int g_fails = 0;
static void ck(const char* what, bool ok) { if (!ok) { printf("  FAIL %s\n", what); g_fails++; } }
int selftest(Synth& S) {
    ck("fastmath backend within its error budget of libm", fm::selfcheck() == 0);
    // 1. a parameter change reaches the engine at every address class
    struct { int ah, am, al, v; const char* name; } pc[] = {
        {0x00, 0, 0x0E, 3, "system velocity curve"},
        {0x10, 0, 0x11, 100, "performance pan"},
        {0x10, 1, 0x28, 5, "reverb type"},
        {0x10, 1, 0x2F, 9, "insertion type"},
        {0x31, 0, 0x0E, 20, "part 2 pan"},
        {0x42, 0, 0x54, 2, "part 3 voice filter type"},
        {0x63, 5, 0x16, 77, "part 4 op 6 level"},
        {0x60, 1, 0x05, 5 << 3 | 1, "part 1 op 2 skirt"},   // what captures/sweep.py skirt sweeps
    };
    for (auto& t : pc) {
        uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)t.ah, (uint8_t)t.am, (uint8_t)t.al, 0, (uint8_t)t.v, 0xF7};
        ck(t.name, apply_param_change_locked(S, m, 10));
        ck(t.name, S.read_param(t.ah, t.am, t.al) == t.v);
    }
    // A word's change lands in both its bytes: variation P3 (a chorus's FB Level) at 10 00 6C, high byte
    // first, as the plug-in sends it. Written to the high byte alone it read 76 << 7 | 64 and clamped to +1.0.
    for (int al : {0x50, 0x6C, 0x7E}) {
        uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, 0x10, 0x00, (uint8_t)al, 0, 76, 0xF7};
        S.perf.fx[al - 0x50] = 0; S.perf.fx[al - 0x50 + 1] = 64;
        apply_param_change_locked(S, m, 10);
        ck("reverb and variation words take both bytes", S.perf.fx[al - 0x50] == 0 && S.perf.fx[al - 0x50 + 1] == 76);
    }
    ck("the voice image followed the change", S.perf.part[2].voice.raw[0x54] == 2);
    ck("op level decode followed the change", S.perf.part[3].voice.v[5].level == 77);
    // The chip's EG key rate scaling, against the 24 rates 02_envelope_3 measured. Key codes 85, 93
    // and 101 are notes 36, 60 and 84; the rows are the rate offset at time scaling 0 to 7, and the
    // truncation toward zero is what separates the middle row from a flooring divide. docs/aeg.md.
    {
        const int kc[3] = {85, 93, 101};
        const int want[3][8] = {{0, -1, -2, -3, -5, -6, -7, -8},
                                {0, 0, 0, 0, -1, -1, -1, -1},
                                {0, 0, 1, 1, 2, 2, 3, 3}};
        for (int n = 0; n < 3; n++)
            for (int t = 0; t < 8; t++) ck("EG rate scaling", eg_ratescale(t, kc[n]) == want[n][t]);
        // and 10_envelope2's ten key codes, one note per octave from 12 to 120, at time scaling 7 and 3
        const int kc2[10] = {77, 81, 85, 89, 93, 97, 101, 105, 109, 113};
        const int w7[10] = {-11, -11, -8, -4, -1, 1, 3, 7, 10, 11};
        const int w3[10] = {-4, -4, -3, -1, 0, 0, 1, 3, 4, 4};
        for (int n = 0; n < 10; n++) {
            ck("EG rate scaling, tscale 7", eg_ratescale(7, kc2[n]) == w7[n]);
            ck("EG rate scaling, tscale 3", eg_ratescale(3, kc2[n]) == w3[n]);
        }
        ck("EG rate scaling saturates low", eg_ratescale(7, 60) == -11);
        ck("key code 87, the demo drum's note 41, reads -8 (19_drums)", eg_keyoff(87) == -8);
        // 21_keycode, every semitone: saturated below 82 and above 109, and the hold does not scale
        ck("key code table ends (21_keycode)", eg_keyoff(70) == -13 && eg_keyoff(81) == -13 && eg_keyoff(82) == -12 && eg_keyoff(109) == 12 && eg_keyoff(110) == 13 && eg_keyoff(127) == 13);
        { EG lo, hi; int L[4] = {0, 0, 0, 63}, R[4] = {0, 34, 0, 0}; lo.start(L, R, 40, -11); hi.start(L, R, 40, 11);
          ck("hold length ignores the key code (21_keycode)", lo.holdLeft == hi.holdLeft); }
        ck("EG rate scaling saturates high", eg_ratescale(7, 127) == 11);
    }
    // The formant window against the bandwidth byte, 04_formant_1's own staircase: flat to 40, opening
    // from 44, halving every eight steps after that.
    {
        // The window is a time, so its length in periods scales with the fundamental and its width in Hz
        // does not. Both are checked here because getting the units wrong is what cost two fitted knees.
        auto hz = [](double bw) { return cal::FRMT_BW_HZ0 * pow(2.0, bw / cal::FRMT_BW_DB); };
        auto wl = [&](double bw, double f0) { return std::min(cal::FRMT_WL_MAX, f0 / hz(bw)); };
        ck("formant bandwidth doubles every eight", fabs(hz(56) / hz(48) - 2.0) < 1e-9);
        ck("formant bandwidth is the same in Hz at any note", fabs(hz(64) - hz(64)) < 1e-12);
        ck("the window is four times as many periods an octave down",
           fabs(wl(72, 65.41) * 4.0 - wl(72, 261.64)) < 1e-6);
        ck("the window clamps at two periods", wl(0, 261.64) == cal::FRMT_WL_MAX);
    }
    // The skirt multiplies the window exponent, it does not step it. A sin^(2n) window one grain period
    // long puts its lines on the binomial row C(2n, n-k), which is what res2 gives the unit: 6.02 dB down
    // either side at skirt 0, 3.52 and 15.56 at skirt 1. Checking the table against those rows pins the
    // law and the doubling at once, and the sine table's own interpolation with them.
    {
        auto line = [&](int s, int k) {          // the k-th Fourier coefficient of g_win[0][s]
            double re = 0;
            for (int i = 0; i < 1024; i++) re += g_win[0][s][i] * cos(2 * PI * k * i / 1024.0);
            return re;
        };
        auto row_db = [&](int s, int k) { return 20 * log10(fabs(line(s, k) / line(s, 0))); };
        ck("skirt 0 is sin^2: one period either side 6.02 dB down", fabs(row_db(0, 1) + 6.0206) < 0.02);
        ck("skirt 0 is sin^2: nothing two periods out", row_db(0, 2) < -60);
        ck("skirt 1 is sin^4, not sin^4 by a different route", fabs(row_db(1, 1) + 3.5218) < 0.02
                                                            && fabs(row_db(1, 2) + 15.563) < 0.05);
        ck("skirt 2 is sin^8", fabs(row_db(2, 1) + 1.9382) < 0.02 && fabs(row_db(2, 3) + 18.837) < 0.1);
        ck("the exponent doubles rather than stepping", fabs(row_db(3, 1) + 1.0216) < 0.02);
        // The asymmetric family: the same rise, a sin^2 fall whatever the skirt, and no mean in the
        // stored all1/all2 waveform, which is the window with g_winDC taken out.
        ck("the '1' family shares the rise", g_win[1][5][256] == g_win[0][5][256]);
        ck("the '1' family falls as sin^2", fabs(g_win[1][5][768] - g_win[0][0][768]) < 1e-6);
        ck("the '1' family is asymmetric", g_win[1][5][768] > 4 * g_win[0][5][768]);
        double dc = 0; for (int i = 0; i < 1024; i++) dc += g_win[0][0][i] - g_winDC[0][0];
        ck("all2 at skirt 0 carries no DC", fabs(dc) < 1e-3);
    }
    // Register 0xC0 against the note. docs/ymp706_registers.md used to gloss it as "note/3 + 10", which is
    // out by 63 and is what made the old rate scaling look like dead code when it was merely wrong.
    {
        const int note[5] = {0, 36, 60, 84, 127};
        const int want[5] = {73, 85, 93, 101, 116};
        for (int i = 0; i < 5; i++) ck("key code register", (NOTETAB[note[i]] >> 8) + 10 == want[i]);
    }
    // Pan key scaling at its extreme byte, the ten notes 10_envelope2 measured: hard right at 12, centre
    // at 60, hard left from 108 up.
    {
        const int note[10] = {12, 24, 36, 48, 60, 72, 84, 96, 108, 120};
        const int want[10] = {128, 112, 96, 80, 64, 48, 32, 16, 0, -16};
        for (int i = 0; i < 10; i++) ck("pan key scaling", pan_index(64, 0, note[i]) == want[i]);
        ck("pan key scaling centred", pan_index(64, 50, 12) == 64 && pan_index(64, 50, 120) == 64);
    }
    // A rise from silence starts at the attack floor and gets to its target, and the hold is half a
    // traverse plus the lag. Both are measured; an EG that crawls up from -200 dB is the old bug.
    {
        int L[4] = {0, 0, 0, 63}, R[4] = {32, 0, 0, 0};        // L1 full, L4 silent, attack at rate 32
        EG e; e.start(L, R, 0, 0);
        ck("no hold at register 0x3F", e.holdLeft == 0 && e.stage == 1);
        ck("attack starts at the floor", fabs(e.cur - cal::EG_ATTACK_FLOOR) < 1e-9);
        for (int i = 0; i < (int)(rate_secs(32) * SR); i++) e.tick();
        ck("attack reaches its target", e.cur > -0.5);
        // A rise to a target part way up takes the same route: the chip aims at the top of the scale and
        // stops at the target, so it gets there in a fraction of the time an approach aimed at the target
        // itself would need. Level 70 is 21 dB down; the unit is there inside 80 ms at rate 32.
        int M[4] = {LEVTAB[70] >> 1, 0, 0, 63};
        EG m; m.start(M, R, 0, 0);
        int n = 0;
        while (m.stage == 1 && n < (int)(0.5 * SR)) { m.tick(); n++; }
        ck("a rise to a mid target aims at the top", n > (int)(0.04 * SR) && n < (int)(0.10 * SR));
        int H[4] = {63, 63, 63, 63};
        EG h; h.start(L, H, 40, 0);                             // hold 40 -> register 41
        double want = (rate_secs(41) * cal::EG_HOLD_FRAC + cal::EG_HOLD_LAG) * SR;
        ck("hold is half a traverse plus the lag", fabs(h.holdLeft - want) < 1.0);
    }

    // 2. a parameter request comes back as a parameter change with the same value
    { uint8_t m[8] = {0xF0, 0x43, 0x30, 0x5E, 0x10, 0, 0x11, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("parameter request replied", S.outQ.size() == 1);
      if (S.outQ.size() == 1) { auto& r = S.outQ[0];
          ck("reply is a parameter change", r.size() == 10 && r[2] == 0x10 && r[4] == 0x10 && r[6] == 0x11);
          ck("reply carries the value", (r[7] << 7 | r[8]) == 100); } }

    // 3. bulk dumps: request, check the checksum, feed it back, state unchanged
    uint8_t before[400]; S.current_perf_bytes(before);
    { uint8_t m[8] = {0xF0, 0x43, 0x20, 0x5E, 0x10, 0, 0, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("performance dump replied", S.outQ.size() == 1);
      if (S.outQ.size() == 1) {
          auto r = S.outQ[0];
          int n = r[4] << 7 | r[5];
          ck("byte count 400", n == 400);
          int sum = 0; for (size_t i = 4; i + 1 < r.size(); i++) sum += r[i];
          ck("checksum", (sum & 0x7F) == 0);
          for (int i = 0; i < 4; i++) S.perf.part[i].p[0x0E] = 1;          // scribble, then reload
          ck("bulk reloaded", load_sysex(S, nullptr, r.data(), r.size(), 0, 0));
          uint8_t after[400]; S.current_perf_bytes(after);
          ck("performance survived the round trip", memcmp(before, after, 400) == 0);
      } }
    { uint8_t m[8] = {0xF0, 0x43, 0x20, 0x5E, 0x40, 0, 0, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("voice dump replied", S.outQ.size() == 1 && (S.outQ[0][4] << 7 | S.outQ[0][5]) == 608); }
    { uint8_t m[8] = {0xF0, 0x43, 0x20, 0x5E, 0x00, 0, 0, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("system dump replied", S.outQ.size() == 1 && (S.outQ[0][4] << 7 | S.outQ[0][5]) == 76); }

    // 4. RPN / NRPN and bank select
    S.perf.part[0].p[4] = 0; S.forceChannel = 0;
    S.midi_in(0xB0, 101, 0); S.midi_in(0xB0, 100, 0); S.midi_in(0xB0, 6, 12);
    ck("RPN bend range", S.perf.part[0].p[0x26] == 0x4C && S.perf.part[0].p[0x27] == 0x34);
    // Pitch bend (0x15B80): the bend word is (msb - 64) * 16 + (lsb >> 3), the pitch takes it >> 3, 256 steps,
    // and source PB gets the same >> 3 raw, -128..127. Ours: bend14 puts the LSB's low six bits between those steps.
    { Part& pt = S.perf.part[0];
      auto bw = [&](int lsb, int msb) { S.midi_in(0xE0, lsb, msb); return S.bend_word(pt); };
      S.bend14 = false;
      const double w0 = bw(0, 96), w1 = bw(64, 96);
      ck("bend: the unit's pitch keeps the LSB's top bit", w0 == 512 && w1 == 520 && bw(63, 96) == w0);
      bw(0, 0); const int lo = pt.src[S.SRC_PB]; bw(127, 127); const int hi = pt.src[S.SRC_PB]; bw(64, 64);
      ck("bend: source PB spans -128..127, the LSB's top bit included", lo == -128 && hi == 127 && pt.src[S.SRC_PB] == 1);
      S.bend14 = true;
      ck("bend: an LSB whose low six bits are 0 is the unit's word", bw(0, 96) == w0 && bw(64, 96) == w1);
      ck("bend: 32 lands halfway between two of the unit's steps", bw(32, 96) == (w0 + w1) / 2);
      bool up = true; double last = -1e9;
      for (int v = 0; v < 16384; v += 37) { double w = bw(v & 127, v >> 7); up = up && w >= last; last = w; }
      ck("bend: the 14-bit sweep never steps back", up);
      bw(0, 64); }
    S.midi_in(0xB0, 100, 2); S.midi_in(0xB0, 6, 0x28 + 12);
    ck("RPN note shift", S.perf.part[0].p[8] == 12);
    S.midi_in(0xB0, 99, 1); S.midi_in(0xB0, 98, 0x20); S.midi_in(0xB0, 6, 90);
    ck("NRPN filter cutoff", S.perf.part[0].p[0x18] == 90);
    S.midi_in(0xB0, 0, 0x3F); S.midi_in(0xB0, 32, 3);
    ck("bank select", S.perf.part[0].p[1] == 4);
    S.midi_in(0xB0, 126, 0); ck("mono mode", S.perf.part[0].p[5] == 0);
    S.midi_in(0xB0, 127, 0); ck("poly mode", S.perf.part[0].p[5] == 1);

    // 5. the controller matrix (FUN_00016F9C / F58 / 17032 and the source order in FUN_000191C0)
    ck("scaler f9c centre", Synth::scale_f9c(0, 0) == 0);
    ck("scaler f9c full", Synth::scale_f9c(127, 63) == 127);          // (128 * 64 * 2) >> 6 saturates
    ck("scaler f9c half", Synth::scale_f9c(64, 16) == 34);            // (65 * 17 * 2) >> 6
    ck("scaler f9c negative", Synth::scale_f9c(-128, 63) == -128);
    ck("scaler f9c inverted", Synth::scale_f9c(64, -16) == -33);      // a negative depth takes no bias
    ck("scaler f58 adds to the byte", Synth::scale_f58(64, 16, 64) == 64 + 17);
    ck("scaler f58 clamps low", Synth::scale_f58(-128, 63, 10) == 0);
    ck("scaler f58 clamps high", Synth::scale_f58(127, 63, 100) == 127);
    ck("scaler 17032 is f58 doubled less one", Synth::scale_f032(64, 16, 0) == 33);
    {
        Synth& T = S;
        memset(T.perf.c + 0x28, 0, 0x28);
        T.perf.c[0x28] = 0x0F;                                        // set 1 on for every part
        T.perf.c[0x30] = 0; T.perf.c[0x31] = 1 << Synth::SRC_KN1;     // source KN1
        T.perf.c[0x40] = 37;                                          // destination: voiced band width
        T.perf.c[0x48] = 64 + 32;                                     // depth +32
        memset(T.perf.part[0].src, 0, sizeof T.perf.part[0].src);
        ck("no source, no offset", T.ctrl_offset(0, 37) == 0);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 127);
        ck("KN1 is bipolar", T.perf.part[0].src[Synth::SRC_KN1] == 127);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 64);
        ck("KN1 centre is zero", T.perf.part[0].src[Synth::SRC_KN1] == 0);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 0);
        ck("KN1 bottom is -128", T.perf.part[0].src[Synth::SRC_KN1] == -128);
        T.set_source(T.perf.part[0], Synth::SRC_MW, 100);
        ck("the wheel keeps its raw value", T.perf.part[0].src[Synth::SRC_MW] == 100);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 127);
        ck("offset follows the source", T.ctrl_offset(0, 37) == Synth::scale_f9c(127, 32));
        ck("another destination stays clear", T.ctrl_offset(0, 38) == 0);
        T.perf.c[0x28] = 0x0E;                                        // part 1 switched out of the set
        ck("the part switch gates it", T.ctrl_offset(0, 37) == 0);
        T.perf.c[0x40] = 46;                                          // Fseq speed is performance-wide
        ck("a global destination ignores the part switch", T.ctrl_offset(0, 46) != 0);
        T.perf.c[0x28] = 0x0F; T.perf.c[0x40] = 21;                   // filter cutoff edits the part byte
        ck("a part-byte destination lands on the byte",
           T.ctrl_part(0, 21, 64) == Synth::scale_f58(127, 32, 64));
        // Two sets on one destination do not compose: each handler stores a per-part byte, so the
        // highest-numbered one wins (FUN_00014DDC walks 0 to 7, ctrlDest_37 writes DAT_01029758).
        T.perf.c[0x40] = 37; T.perf.c[0x29] = 0x0F;
        T.perf.c[0x32] = 0; T.perf.c[0x33] = 1 << Synth::SRC_KN1;
        T.perf.c[0x41] = 37; T.perf.c[0x49] = 64 + 16;                // set 2, same destination, depth +16
        ck("the higher set wins, it does not sum", T.ctrl_offset(0, 37) == Synth::scale_f9c(127, 16));
        // Frequency bias reaches formant and fixed operators only: the ratio branch of FUN_0001E838
        // adds nothing, which is what detuned the demo's ratio operators under a moving controller.
        T.perf.c[0x41] = 36; T.perf.c[0x49] = 64 + 16;                // set 2 -> frequency bias
        T.perf.c[0x40] = 0;
        Voice& V = T.perf.part[0].voice;
        V.v[0].form = 7; V.v[0].fbias = 7 + 4; V.v[1].form = 0; V.v[1].fixed = 0; V.v[1].fbias = 7 + 4;
        T.refresh_bias(T.ch[0], T.perf.part[0]);
        int want = (int16_t)(Synth::scale_f9c(127, 16) * 4 * 0x10) >> 2;
        ck("the formant operator takes the frequency bias", T.ch[0].fbW[0] == want && want != 0);
        ck("the ratio operator does not", T.ch[0].fbW[1] == 0);
        memset(T.perf.c + 0x28, 0, 0x28);
        init_default_voice(V);
    }
    // An effect destination follows its sources, a controller reset included (CC 121 zeroes them).
    {
        Synth& T = S;
        memset(T.perf.c + 0x28, 0, 0x28);
        T.perf.c[0x30] = 0; T.perf.c[0x31] = 1 << Synth::SRC_KN1;     // set 1: KN1
        T.perf.c[0x40] = 15;                                          // to the insertion's send to the reverb
        T.perf.c[0x48] = 64 + 32;
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 127);
        ck("an effect destination follows its source", T.fx.host.sendReverbOffset == Synth::scale_f9c(127, 32));
        T.control_change(0, 121, 0);
        ck("and a controller reset", T.fx.host.sendReverbOffset == 0);
        memset(T.perf.c + 0x28, 0, 0x28);
    }

    // 5b. the detune law: the ROM's own key scaled table, against what 11_detune measured. The band is
    // (pitchNote >> 8) + 10 - 0x50, which advances four per octave, so note 60 is band 13.
    {
        auto detw = [](int byte, int band) {
            int dd = byte < 15 ? (~byte) : byte - 15, idx = (dd & 0x1F) * 32 + band;
            return (idx & 0x200) ? -(int)FRMDET[idx & 0x1FF] : (int)FRMDET[idx & 0x1FF];
        };
        ck("detune centre is none", detw(15, 13) == 0);
        ck("detune +7 at note 60 is 7 word units", detw(22, 13) == 7);      // 8.20 cents, measured 7.85
        ck("detune -7 at note 60 mirrors it", detw(8, 13) == -7);
        ck("detune +15 at note 60 is 22 units", detw(30, 13) == 22);        // 25.78 cents, measured 25.84
        ck("the same detune is smaller three octaves up", detw(30, 25) == 7 && detw(22, 25) == 2);
        ck("and larger three octaves down", detw(30, 1) == 37 && detw(22, 1) == 13);

        // And that it reaches the operator. Earlier sections leave note shifts scribbled, so the band
        // is read back off the channel rather than assumed: what is under test here is that the word
        // reaches the frequency at all, the table itself being checked above.
        Synth& T = S;
        Voice& V = T.perf.part[0].voice;
        init_default_voice(V);
        std::vector<float> a(64), b(64);
        auto play = [&](int det) {
            V.v[0].detune = det;
            T.midi_in(0x90, 60, 100);
            T.render(a.data(), b.data(), 64);
            Chan* c = nullptr;
            for (auto& ch : T.ch) if (ch.active && ch.note == 60) { c = &ch; break; }
            double f = c ? c->op[0].fop : 0.0;
            int bnd = c ? (c->pitchNote >> 8) + 10 : 0;
            bnd = bnd < 0x50 ? 0 : bnd < 0x70 ? ((bnd ^ 0x10) & 0x1F) : 0x1F;
            T.midi_in(0x80, 60, 0); T.midi_in(0xB0, 120, 0);
            return std::make_pair(f, bnd);
        };
        auto centred = play(15), up = play(22), down = play(8);
        ck("a centred operator has a frequency at all", centred.first > 1.0);
        double want = detw(22, up.second) * 1200.0 / 1024.0;
        ck("detune +7 moves the operator by its table entry",
           fabs(1200.0 * log2(up.first / centred.first) - want) < 0.01 && want > 0.0);
        ck("detune -7 moves it the other way by the same",
           fabs(1200.0 * log2(down.first / centred.first) + want) < 0.01);
        init_default_voice(V);
    }

    // Formant transpose is the pitch half of register 0x230 (FUN_00013bc6): it shifts the grain rate.
    // The formant frequency, coarse and fine plus detune, stays. A frame replaces that frequency and
    // leaves the pitch word, so an Fseq formant is transposed the same way. Sine ignores it.
    {
        Synth& T = S;
        Voice& V = T.perf.part[0].voice;
        init_default_voice(V);
        V.v[0].form = 7; V.v[0].fixed = 1; V.v[0].level = 99; V.v[0].coarse = 16; V.v[0].fine = 110;
        V.v[0].bw = 40; V.v[0].detune = 15; V.pmd = 0;
        std::vector<float> a(64), b(64);
        auto centre = [&](int tr) {
            V.v[0].transpose = tr;
            T.all_off();
            T.midi_in(0x90, 60, 100);
            T.render(a.data(), b.data(), 64);
            double f = 0, w = 0;
            for (auto& ch : T.ch) if (ch.active && ch.note == 60) { f = ch.op[0].fop; w = ch.op[0].fw; }
            return std::make_pair(f, w);
        };
        auto z = centre(0), up = centre(12), down = centre(-12);
        ck("transpose leaves the formant frequency", z.first > 1.0 && fabs(up.first - z.first) < 0.05 && fabs(down.first - z.first) < 0.05);
        ck("formant transpose +12 raises the pitch an octave", z.second > 1.0 && fabs(1200.0 * log2(up.second / z.second) - 1200.0) < 0.05);
        ck("formant transpose -12 lowers the pitch an octave", down.second > 1.0 && fabs(1200.0 * log2(z.second / down.second) - 1200.0) < 0.05);

        uint8_t pc[8];
        memcpy(pc, T.perf.c + 0x15, 1);
        memcpy(pc + 1, T.perf.c + 0x1A, 6);
        pc[7] = T.perf.c[0x21];
        int dly = T.perf.c[0x26];
        T.fseq = Fseq();
        T.fseq.valid = true; T.fseq.nframes = 1; T.fseq.endStep = 0;
        T.fseq.frame[0][0] = 101; T.fseq.frame[0][1] = 118;       // pitch word 26092, the note, so the frame does not drag the channel
        T.fseq.frame[0][2] = 109; T.fseq.frame[0][0x0A] = 68;   // 109 * 256 + 68 * 2 = 28040
        T.fseqPart = 0;
        T.perf.c[0x15] = 1; T.perf.c[0x21] = 2; T.perf.c[0x26] = 0;
        memset(T.perf.c + 0x1A, 0, 6);
        V.fseqV = 1; V.v[0].fseqtrk = 0;
        auto fz = centre(0), fu = centre(12);
        // 28040 is the frame word, about 977 Hz, and transpose does not move it. The pitch does.
        ck("an Fseq formant keeps its frequency and transposes the pitch",
           fabs(fz.first - 977.35) < 2.0 && fabs(fu.first - fz.first) < 0.05 &&
           fabs(1200.0 * log2(fu.second / fz.second) - 1200.0) < 0.05);
        T.fseq.valid = false; T.fseqPart = -1;
        T.perf.c[0x15] = pc[0]; memcpy(T.perf.c + 0x1A, pc + 1, 6); T.perf.c[0x21] = pc[7]; T.perf.c[0x26] = (uint8_t)dly;
        T.all_off();
        init_default_voice(V);

        OpV w{};
        w.form = 7; w.bw = 40; w.skirt = 2;
        float wa[32], wb[32];
        op_wave(w, wa, 32);
        w.transpose = 12;
        op_wave(w, wb, 32);
        double d = 0; for (int i = 0; i < 32; i++) d += fabs(wa[i] - wb[i]);
        ck("the formant waveform follows transpose", d > 0.5);
        w.form = 0; w.transpose = 0;
        op_wave(w, wa, 32);
        w.transpose = 12;
        op_wave(w, wb, 32);
        d = 0; for (int i = 0; i < 32; i++) d += fabs(wa[i] - wb[i]);
        ck("a sine waveform ignores transpose", d < 1e-6);
    }

    // 6. notes still sound and stop
    S.midi_in(0x90, 60, 100);
    int live = 0; for (auto& c : S.ch) if (c.active) live++;
    ck("note on allocated a channel", live > 0);
    std::vector<float> bl(4096), br(4096);
    S.render(bl.data(), br.data(), 4096);
    double pk = 0; for (auto v : bl) pk = std::max(pk, (double)fabs(v));
    ck("note produced audio", pk > 0.0001);
    S.midi_in(0x80, 60, 0); S.midi_in(0xB0, 120, 0);
    live = 0; for (auto& c : S.ch) if (c.active) live++;
    ck("all sound off", live == 0);

    // The summary belongs after the last check, not here: it used to print at this point, so every
    // check below it (the filter EG, the formant window, the DX7 conversion, the voice edit) printed its
    // failure and then reported "selftest: ok" above it, and the caller's exit code was the only place
    // the truth showed.
    // The voice filters, on their chip. A slot's passband, that no slot leaks into another, the corner's
    // octave per twelve cutoff bytes and the resonance's lift.
    { using namespace fsvr::vop3host::filter;
      auto f = std::make_unique<Filter::Bank>();
      uint8_t v[fsvr::vop3host::FilterHost::VOICE_BYTES] = {}, r[fsvr::vop3host::FilterHost::PART_BYTES] = {};
      for (int i = 0x18; i <= 0x23; i++) r[i] = 64;                  // the part's offsets at their centres
      r[PART_LFO2_SPEED] = r[PART_LFO2_DEPTH] = 64;
      v[RESONANCE_VEL] = v[EG_DEPTH_VEL] = 7; v[CUTOFF] = 127; v[KEY_SCALE_DEPTH] = 64; v[KEY_SCALE_POINT] = 60;
      v[INPUT_GAIN] = 12; v[EG_DEPTH] = 64;                          // LPF24 wide open, 0 dB in, no EG, no LFO
      for (int i = EG_L4; i <= EG_L3; i++) v[i] = 50;
      auto set = [&](int byte, int val) { v[byte] = (uint8_t)val; f->sync(0, v, r, 0, 0, 0); };
      // the gain of a sine through slot s, and the most that reaches any other slot
      auto gain = [&](int s, double hz, double* leak = nullptr) {
          double in = 0, out = 0, other = 0;
          for (int i = 0; i < 2400; i++) {
              double x[16] = {}; const double a = 0.3 * sin(2 * PI * hz * i / SR);
              x[s] = a; f->run(x);
              if (i >= 960) { in += a * a; out += x[s] * x[s]; for (int k = 0; k < 16; k++) if (k != s) other = std::max(other, fabs(x[k])); }
          }
          if (leak) *leak = other;
          return sqrt(out / in);
      };
      set(CUTOFF, 127);
      bool pass = true, tight = true;
      for (int s : {0, 5, 15}) {
          f->host.noteOn(s, 0, 60, 100);
          double leak, g = gain(s, 100, &leak);
          pass = pass && fabs(20 * log10(g / cal::FLT_LOSS)) < 0.1; tight = tight && leak < 1e-9;
      }
      ck("filter chip: a slot's passband comes back at FLT_LOSS", pass);
      ck("filter chip: nothing of a slot reaches another", tight);
      auto corner = [&](int cutoff) {                      // where the lowpass is 3 dB under its 50 Hz level
          set(CUTOFF, cutoff); f->host.noteOn(0, 0, 60, 100);
          double ref = gain(0, 50), lo = 60, hi = 4000;
          for (int k = 0; k < 9; k++) { double mid = sqrt(lo * hi); (gain(0, mid) > ref * 0.7071 ? lo : hi) = mid; }
          return sqrt(lo * hi);
      };
      double c52 = corner(52), c64 = corner(64);
      ck("filter chip: twelve cutoff bytes are an octave of corner", fabs(c64 / c52 / 2 - 1) < 0.08);
      auto peak = [&](int reso) {
          set(RESONANCE, reso); f->host.noteOn(0, 0, 60, 100);
          double best = 0;
          for (double hz = c64; hz < 2.4 * c64; hz *= 1.12) best = std::max(best, gain(0, hz));
          return best / gain(0, 50);
      };
      ck("filter chip: resonance lifts the corner", 20 * log10(peak(76) / peak(16)) > 10); }

    // The filter through the engine. A part with its filter on plays on the chip's sixteen channels and the
    // others on the rest; a filtered note loses its top; and a part without a filter sounds exactly as it
    // does in a performance that has none.
    { auto note = [](Synth& T, int part, int n, std::vector<float>& l, std::vector<float>& r) {
          srand(7); T.note_on(part, n, 100); T.render(l.data(), r.data(), (int)l.size()); };
      auto energy = [](const std::vector<float>& x) { double e = 0; for (float s : x) e += (double)s * s; return e; };
      std::vector<float> l0(4800), r0(4800), l1(4800), r1(4800), l2(4800), r2(4800);
      auto open = [](Synth& T) {
          using namespace fsvr::vop3host::filter;
          uint8_t* b = T.perf.part[0].voice.raw;
          b[FILTER_TYPE] = LPF24; b[RESONANCE] = 0; b[CUTOFF] = 40; b[CUTOFF_LFO1] = b[CUTOFF_LFO2] = 0; b[INPUT_GAIN] = 12; b[EG_DEPTH] = 64; };
      auto A = std::make_unique<Synth>(), B = std::make_unique<Synth>(), C = std::make_unique<Synth>();
      open(*A); open(*B); open(*C);
      B->perf.part[0].p[7] = 1; C->perf.part[0].p[7] = 1;
      C->filter32 = false;                                 // the unit's split, which the next checks are about
      note(*A, 0, 96, l0, r0);                             // no filter anywhere
      note(*B, 0, 96, l1, r1);                             // the same note with part 1's filter on, cutoff 40
      int at = -1; for (int i = 0; i < NCHAN; i++) if (B->ch[i].active) at = i;
      ck("engine: a part with its filter on plays on the filter's channels", at >= 0 && at < 16 && B->flt.bank[0].host.slotPart[at] == 0);
      ck("engine: a filtered note loses its top", energy(l1) < 0.1 * energy(l0) && energy(l1) > 0);
      auto D = std::make_unique<Synth>();
      note(*D, 1, 60, l0, r0);                             // part 2 alone, no filter anywhere
      note(*C, 1, 60, l2, r2);                             // part 2 alone, beside a part with its filter on
      at = -1; for (int i = 0; i < NCHAN; i++) if (C->ch[i].active) at = i;
      ck("engine: a part without a filter plays on the other channels", at >= 16);
      ck("engine: and sounds exactly as it does with no filter anywhere", l2 == l0 && r2 == r0 && energy(l0) > 0);
      // a switch that changes stops every channel, as on the unit
      C->perf.part[1].p[7] = 1; C->render(l2.data(), r2.data(), 16);
      ck("engine: changing a filter switch stops every note", C->active_chans() == 0);
      // Ours: two filter chips, so a filtered part plays 32 voices, filtered on both; the unit's 16 with filter32 off.
      for (int wide = 0; wide < 2; wide++) {
          auto E = std::make_unique<Synth>(); open(*E); E->perf.part[0].p[7] = 1; E->perf.part[0].p[0] = 32; E->filter32 = wide;
          for (int n = 0; n < 32; n++) E->note_on(0, 40 + n, 100);
          std::vector<float> l(4800), r(4800); E->render(l.data(), r.data(), (int)l.size());
          int busy = 0, onChip1 = 0; for (int s = 0; s < 16; s++) onChip1 += E->flt.bank[1].host.slotPart[s] == 0;
          for (auto& c : E->ch) busy += c.active && c.filtered;
          if (wide) ck("engine: 32 filtered voices, the second sixteen on bank 1", busy == 32 && onChip1 == 16 && energy(l) > 0);
          else ck("engine: 16 filtered voices, the unit's", busy == 16 && onChip1 == 0);
      } }

    // Note and velocity limits: low above high plays everything but the gap between them, as Drum Kit 9o9's
    // part 3 has its notes at 67/55.
    { enum { NOTE_LIMIT = 0x0F, VEL_LIMIT = 0x2A };   // low, then high in the byte after
      auto plays = [](int at, int lo, int hi, int note, int vel) {
          auto T = std::make_unique<Synth>();
          T->perf.part[0].p[at] = (uint8_t)lo; T->perf.part[0].p[at + 1] = (uint8_t)hi;
          T->note_on(0, note, vel);
          return T->active_chans() > 0; };
      ck("limits: a note in an inverted range's gap is silent", !plays(NOTE_LIMIT, 67, 55, 60, 100));
      ck("limits: notes either side of the gap sound, its ends too",
         plays(NOTE_LIMIT, 67, 55, 50, 100) && plays(NOTE_LIMIT, 67, 55, 55, 100) && plays(NOTE_LIMIT, 67, 55, 67, 100) && plays(NOTE_LIMIT, 67, 55, 80, 100));
      ck("limits: a velocity in an inverted range's gap is silent", !plays(VEL_LIMIT, 100, 40, 60, 70));
      ck("limits: velocities either side of the gap sound, its ends too",
         plays(VEL_LIMIT, 100, 40, 60, 30) && plays(VEL_LIMIT, 100, 40, 60, 40) && plays(VEL_LIMIT, 100, 40, 60, 100) && plays(VEL_LIMIT, 100, 40, 60, 120));
      ck("limits: a range the right way round keeps out what is outside it",
         !plays(NOTE_LIMIT, 40, 80, 30, 100) && plays(NOTE_LIMIT, 40, 80, 60, 100)); }

    // A part whose voice bank is off receives nothing even with a receive channel (A011 Sho, parts 3 and 4).
    { S.perf.part[1].p[1] = 0; S.perf.part[1].p[4] = 0x10; ck("bank off silences the part", !S.part_listens(1, 0)); S.perf.part[1].p[1] = 2; ck("bank on hears it again", S.part_listens(1, 0)); }

    // DX7 conversion against the firmware's own: demo song 10 sends the unit's conversion of PrC voice 371
    // (DX-Acrd 4), edited afterwards on a few musical bytes. Every byte the demo left alone must match:
    // the common block outside LFO/PEG/category, the two unused operator slots, and on each used slot the
    // placement (byte 5), fixed/coarse/fine where unedited, detune, hold, time scaling and the sense bytes.
    { static const uint8_t demo[608] = {
    0x44, 0x58, 0x2D, 0x41, 0x63, 0x72, 0x64, 0x20, 0x34, 0x20, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x04, 0x13, 0x26, 0x00, 0x00, 0x03, 0x00, 0x00, 0x04, 0x13, 0x00, 0x00, 0x00, 0x00, 0x24, 0x32,
    0x2F, 0x32, 0x32, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x32, 0x00,
    0x16, 0x00, 0x00, 0x00, 0x00, 0x4A, 0x40, 0x40, 0x40, 0x40, 0x13, 0x00, 0x00, 0x00, 0x00, 0x47, 0x40, 0x40, 0x40, 0x40, 0x00, 0x24, 0x0A, 0x55, 0x07, 0x00, 0x00, 0x45, 0x3C, 0x0C, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x32, 0x64, 0x4B, 0x4B, 0x1E, 0x1E, 0x1E, 0x63, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x38, 0x06, 0x14, 0x0F, 0x32, 0x32, 0x14, 0x14, 0x63, 0x63, 0x63, 0x00,
    0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x38, 0x07, 0x07, 0x07, 0x18, 0x0E, 0x00, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07,
    0x63, 0x63, 0x63, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x07, 0x07, 0x07, 0x07, 0x18, 0x01, 0x00, 0x00, 0x38, 0x07, 0x14, 0x0F, 0x32, 0x32, 0x14, 0x14, 0x63, 0x63, 0x63, 0x00, 0x00, 0x14,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x38, 0x07, 0x07, 0x07, 0x18, 0x0F, 0x00, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63,
    0x63, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x07, 0x07, 0x07, 0x07, 0x58, 0x04, 0x00, 0x00, 0x38, 0x00, 0x00, 0x08, 0x32, 0x32, 0x00, 0x00, 0x5F, 0x5C, 0x5C, 0x00, 0x07, 0x4A, 0x54, 0x2A,
    0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0x07, 0x37, 0x0A, 0x18, 0x0F, 0x4B, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63, 0x63, 0x00,
    0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x07, 0x07, 0x07, 0x07, 0x58, 0x00, 0x00, 0x00, 0x38, 0x01, 0x00, 0x0B, 0x32, 0x32, 0x00, 0x00, 0x63, 0x60, 0x54, 0x00, 0x0E, 0x5C, 0x54, 0x34, 0x00, 0x01,
    0x4C, 0x27, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x39, 0x07, 0x07, 0x07, 0x18, 0x10, 0x00, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63, 0x63, 0x00, 0x00, 0x14,
    0x14, 0x00, 0x00, 0x00, 0x07, 0x07, 0x07, 0x07, 0x58, 0x01, 0x00, 0x00, 0x38, 0x02, 0x00, 0x08, 0x32, 0x32, 0x00, 0x00, 0x63, 0x63, 0x63, 0x00, 0x1F, 0x4C, 0x56, 0x24, 0x00, 0x01, 0x63, 0x38,
    0x06, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0x07, 0x3D, 0x0A, 0x18, 0x10, 0x2A, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63, 0x63, 0x00, 0x00, 0x14, 0x14, 0x00,
    0x00, 0x00, 0x07, 0x07, 0x07, 0x07, 0x58, 0x03, 0x00, 0x00, 0x38, 0x03, 0x00, 0x13, 0x32, 0x32, 0x00, 0x00, 0x63, 0x5C, 0x47, 0x00, 0x00, 0x4D, 0x52, 0x17, 0x00, 0x00, 0x44, 0x4B, 0x00, 0x32,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0x07, 0x37, 0x0A, 0x18, 0x10, 0x4B, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63, 0x63, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00,
    0x07, 0x07, 0x07, 0x07, 0x58, 0x00, 0x01, 0x00, 0x38, 0x04, 0x00, 0x12, 0x32, 0x32, 0x00, 0x00, 0x63, 0x5C, 0x47, 0x00, 0x00, 0x50, 0x55, 0x3B, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x39, 0x07, 0x37, 0x0A, 0x18, 0x10, 0x68, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63, 0x63, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x07, 0x07,
    0x07, 0x07, 0x58, 0x01, 0x00, 0x00, 0x38, 0x05, 0x00, 0x13, 0x32, 0x32, 0x00, 0x00, 0x63, 0x5C, 0x52, 0x00, 0x20, 0x51, 0x56, 0x1F, 0x00, 0x01, 0x63, 0x37, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x39, 0x07, 0x3D, 0x0A, 0x18, 0x11, 0x00, 0x00, 0x14, 0x07, 0x00, 0x32, 0x32, 0x14, 0x14, 0x00, 0x07, 0x63, 0x63, 0x63, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x07, 0x07, 0x07, 0x07,
      };
      static const uint8_t vced[155] = {   // PrC 371 DX-Acrd 4, VCED order with the name last
    0x46, 0x0F, 0x05, 0x2F, 0x5F, 0x5C, 0x5C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x3E, 0x00, 0x04, 0x00, 0x00, 0x44, 0x00, 0x08, 0x27, 0x63, 0x60, 0x54, 0x00, 0x27, 0x00,
    0x00, 0x01, 0x03, 0x01, 0x00, 0x00, 0x4A, 0x00, 0x00, 0x00, 0x03, 0x3C, 0x14, 0x0A, 0x3C, 0x63, 0x63, 0x63, 0x00, 0x38, 0x06, 0x0A, 0x00, 0x00, 0x01, 0x03, 0x06, 0x63, 0x00, 0x01, 0x00,
    0x00, 0x57, 0x0F, 0x0A, 0x46, 0x63, 0x5C, 0x47, 0x00, 0x4B, 0x00, 0x32, 0x00, 0x00, 0x00, 0x03, 0x00, 0x42, 0x00, 0x03, 0x00, 0x0B, 0x5B, 0x0F, 0x0A, 0x23, 0x63, 0x5C, 0x47, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x4E, 0x00, 0x00, 0x01, 0x0A, 0x3B, 0x0F, 0x0A, 0x41, 0x63, 0x5C, 0x52, 0x00, 0x37, 0x00, 0x0A, 0x00, 0x00, 0x01, 0x03, 0x06, 0x63, 0x00, 0x01,
    0x00, 0x0B, 0x63, 0x62, 0x62, 0x62, 0x2F, 0x32, 0x32, 0x32, 0x03, 0x07, 0x01, 0x13, 0x26, 0x07, 0x00, 0x00, 0x04, 0x01, 0x24, 0x44, 0x58, 0x2D, 0x41, 0x63, 0x72, 0x64, 0x20, 0x34, 0x20,
      };
      uint8_t out[608]; convert_dx7(vced, out); int bad = 0;
      for (int i = 0; i < 608; i++) if (i != 0x0E && out[i] != demo[i]) bad++;   // 0x0E is the category, the demo's one edit
      ck("DX7 conversion matches the unit's own DX-Acrd 4 bulk on every byte but the category", bad == 0); }

    // DX7 conversion: the operator slot is DX7MAP word 25 + j, and every DX carrier lands on an FS1R carrier
    { uint8_t vced[155] = {}; vced[134] = 4;                        // DX algorithm 5: carriers 1, 3, 5
      for (int j = 0; j < 6; j++) vced[j * 21 + 16] = (uint8_t)(10 + j);   // output level tags op6..op1 as 10..15
      uint8_t out[608]; convert_dx7(vced, out); bool ok = true;
      const unsigned char* alg = FS1R_ALG[out[0x2C]];
      for (int j = 0; j < 6; j++) for (int o = 0; o < 8; o++) if (out[112 + o * 62 + 22] == 10 + j) {
          bool carrier = alg[2 * o + 1] & 1, want = (6 - j) == 1 || (6 - j) == 3 || (6 - j) == 5; if (carrier != want) ok = false; }
      ck("DX7 alg 5 carriers land on FS1R carriers (B025 Tremolo)", ok); }

    // A read leaves the sum bus as it is and t1 bit 1 starts it over (docs/ymp706_registers.md, the algorithm word).
    // The table bears that out on all 88: the bit never falls while an add waits for its reader, and every entry
    // that adds to the bus sets it at or before its first add.
    { bool drops = false, late = false;
      for (int a = 0; a < 88; a++) {
          bool waiting = false, cleared = false;
          for (int o = 0; o < 8; o++) {
              const int t0 = FS1R_ALG[a][2 * o], t1 = FS1R_ALG[a][2 * o + 1];
              if (((t0 >> 3) & 7) == 6) waiting = false;
              if (t1 & 2) { if (waiting) drops = true; cleared = true; }
              if (t1 & 4) { if (!cleared) late = true; waiting = true; }
          }
      }
      ck("algorithm table: t1 bit 1 never clears a sum no operator has read", !drops);
      ck("algorithm table: every entry that adds to the sum bus clears it first", !late); }

    // So 65 and 80 fan out as the Algorithm Sheet draws them: 3 and 4 into each of 5..8, 1 and 2 into each of 3..8.
    // One modulator at full level and one carrier past the first reader, everything else silent: the carrier is a
    // plain sine without the modulator and not one with it. When a read cleared the bus it stayed a plain sine.
    { struct { int alg, mod, car; const char* what; } fan[] = {
          {64, 2, 6, "algorithm 65: operator 3 reaches operator 7 over the sum bus"},
          {79, 0, 3, "algorithm 80: operator 1 reaches operator 4 over the sum bus"}};
      for (auto& f : fan) {
          double purity[2];
          for (int with = 0; with < 2; with++) {
              init_perf(S.perf); Part& pt = S.perf.part[0]; Voice& V = pt.voice; pt.p[1] = 2; pt.p[4] = 0x10;
              init_blank_voice(V.raw); V.raw[0x2C] = (uint8_t)f.alg;
              V.raw[112 + f.car * 62 + 22] = 99; V.raw[112 + f.mod * 62 + 22] = with ? 99 : 0;
              decode_voice(V);
              S.all_off(); S.midi_in(0x90, 69, 100);
              std::vector<float> l(24000), r(24000);
              for (int at = 0; at < 24000; at += 480) S.render(l.data() + at, r.data() + at, 480);
              double fop = 0; for (auto& c : S.ch) if (c.active && c.part == 0) fop = c.op[f.car].fop;
              // The share of the last 0.1 s that is a sine at the carrier's own frequency.
              const int from = 19200; double re = 0, im = 0, all = 0;
              for (int n = from; n < 24000; n++) {
                  const double x = l[n] + r[n], ph = 2 * PI * fop * (n - from) / SR;
                  re += x * cos(ph); im += x * sin(ph); all += x * x;
              }
              purity[with] = all > 0 ? 2 * (re * re + im * im) / (24000 - from) / all : 0;
          }
          ck(f.what, purity[0] > 0.99 && purity[1] < 0.5);
      }
      S.all_off(); init_perf(S.perf); }

    // The formant window follows the operator's own pitch mod sense, not the channel word at pms 7
    { S.perf.part[0].p[1] = 2; S.perf.part[0].p[4] = 0x10; Voice& V = S.perf.part[0].voice;
      V.v[0].form = 7; V.v[0].fixed = 1; V.v[0].pms = 1; V.pmd = 99; V.lfo1delay = 0;
      S.midi_in(0x90, 60, 100); float L[480], R[480]; double lo = 1e9, hi = 0;
      for (int k = 0; k < 400; k++) { S.render(L, R, 480); if (k < 200) continue; for (auto& c : S.ch) if (c.active && c.part == 0) { lo = std::min(lo, c.op[0].fw); hi = std::max(hi, c.op[0].fw); } }
      ck("formant window pitch mod goes through pms (B011 Human Eh)", hi > lo && 1200 * log2(hi / lo) < 200); S.all_off(); }

    // A voice edit reaches a sounding note: op 1 coarse 1 -> 2 moves its frequency word an octave.
    { S.perf.part[0].p[1] = 2; S.perf.part[0].p[4] = 0x10; S.perf.part[0].voice.v[0].fixed = 0; S.perf.part[0].voice.v[0].form = 0;
      S.midi_in(0x90, 60, 100); int w0 = -1; for (auto& c : S.ch) if (c.active && c.part == 0) w0 = c.freqWord[0];
      uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, 0x60, 0x00, 0x01, 0x00, 2, 0xF7}; apply_param_change_locked(S, m, 10);
      int w1 = -1; for (auto& c : S.ch) if (c.active && c.part == 0) w1 = c.freqWord[0];
      ck("voice edit reaches the sounding note", w0 >= 0 && w1 == w0 + 1024); S.all_off(); }

    // The operator frequency EG is a linear ramp at a rate the amplitude EG's own ladder sets, and the
    // slope does not depend on the swing: that is what 07_modulation_2 measured and what separates it
    // from the exponential approach the engine used to have. Two swings at one attack time, the larger
    // taking proportionally longer, is the smallest thing that fails if the shape goes back.
    { init_perf(S.perf); Voice& V = S.perf.part[0].voice;
      S.perf.part[0].p[1] = 2; S.perf.part[0].p[4] = 0x10;
      auto ramp_ticks = [&](int init) {
          // attack level 0, so the ramp runs from feg_semis(init) to zero at the attack time
          V.v[0].fegInit = init; V.v[0].fegAtt = 0; V.v[0].fegAttT = 40; V.v[0].fegDecT = 40;
          S.all_off(); S.midi_in(0x90, 60, 100);
          Chan* C = nullptr; for (auto& c : S.ch) if (c.active && c.part == 0) C = &c;
          if (!C) return -1;
          double prev = C->op[0].feg.cur; int n = 0; double step = 0, worst = 0;
          // tick the EG directly: it runs per sample, and what is under test is its own shape. The last
          // step is short, since it lands exactly on the target rather than overshooting it, so the
          // constant-rate check covers every step but that one.
          for (int k = 0; k < 400000 && C->op[0].feg.stage == 0; k++) {
              double cur = C->op[0].feg.tick();
              double d = fabs(cur - prev);
              if (C->op[0].feg.stage != 0) break;            // the truncated final step
              if (n == 0) step = d; else worst = std::max(worst, fabs(d - step));
              prev = cur; n++;
          }
          ck("frequency EG ramps at a constant rate", n > 100 && worst < step * 1e-6);
          return n;
      };
      int n25 = ramp_ticks(25), n50 = ramp_ticks(50);
      double s25 = FreqEG::feg_semis(25), s50 = FreqEG::feg_semis(50);
      // the two swings are 10.5 and 47.6 semitones, so the sample counts must be in that ratio
      ck("frequency EG time is proportional to the swing",
         n25 > 0 && n50 > 0 && fabs((double)n50 / n25 - s50 / s25) < 0.02 * (s50 / s25));
      // and the rate is FEG_TRAVERSE semitones per traverse of the amplitude EG's rate word
      double want = cal::FEG_TRAVERSE / (rate_secs(egrate(40)) * SR);
      ck("frequency EG slope is FEG_TRAVERSE per traverse",
         n25 > 0 && fabs(s25 / n25 - want) < want * 0.01);
      S.all_off(); init_perf(S.perf); }

    // A note-on that takes a channel from a sounding note damps it rather than zeroing it
    // (FUN_00023000: EG stage word to 4 and register 0xFC/FD, the mask out of the one-hot table at
    // 0x35B260). Fill every channel with a sounding note, strike one more, and the displaced channel's
    // own output must carry on into the next sample instead of dropping to zero.
    { init_perf(S.perf); Voice& V = S.perf.part[0].voice;
      S.perf.part[0].p[1] = 2; S.perf.part[0].p[4] = 0x10;
      V.v[0].form = 0; V.v[0].level = 99; V.v[0].fixed = 0; V.v[0].coarse = 1; V.v[0].keysync = 1;
      for (int i = 0; i < 4; i++) { V.v[0].L[i] = i == 3 ? 0 : 99; V.v[0].T[i] = i == 3 ? 99 : 0; }
      S.all_off();
      float l[64], r[64];
      for (int k = 0; k < NCHAN; k++) { S.midi_in(0x90, 36 + k, 100); S.render(l, r, 8); }
      int live = 0; for (auto& c : S.ch) if (c.active) live++;
      ck("every channel is sounding before the steal", live == NCHAN);
      // the channel the allocator will actually take, found the same way the allocator picks it:
      // strike the note, then see which channel now holds it (age no longer decides this)
      double before[NCHAN];
      for (int i = 0; i < NCHAN; i++) before[i] = fabs(S.ch[i].last);
      S.midi_in(0x90, 100, 100);                       // one more note: the allocator has to steal
      Chan* victim = nullptr;
      for (auto& c : S.ch) if (c.active && c.note == 100) victim = &c;
      ck("the steal produced a channel holding the new note", victim != nullptr);
      if (victim) {
          int vi = int(victim - S.ch);
          ck("the victim channel was making sound", before[vi] > 1e-4);
          double damp = fabs(victim->damp);
          ck("note-on damps the stolen channel rather than zeroing it", damp > 0.5 * before[vi]);
          double first = damp;
          for (int k = 0; k < 400; k++) S.render(l, r, 1);
          double later = fabs(victim->damp);
          ck("the damp decays", later < 0.5 * first);
      }
      S.all_off(); init_perf(S.perf); }

    // Ours: op byte 0x1C is a key-synced oscillator's initial phase, value / 128 of a turn. 64 is half a turn,
    // which negates a lone sine carrier at the channel's output; with key sync off the byte does nothing.
    { auto run = [](int sync, int ph, std::vector<double>& y) {
          auto T = std::make_unique<Synth>(); init_perf(T->perf); Voice& V = T->perf.part[0].voice;
          T->perf.part[0].p[1] = 2; T->perf.part[0].p[4] = 0x10;
          for (int o = 1; o < 8; o++) V.v[o].level = 0, V.u[o].level = 0;
          V.v[0].form = 0; V.v[0].level = 99; V.v[0].fixed = 0; V.v[0].coarse = 1; V.v[0].keysync = sync; V.u[0].level = 0;
          for (int i = 0; i < 4; i++) { V.v[0].L[i] = i == 3 ? 0 : 99; V.v[0].T[i] = i == 3 ? 99 : 0; }
          V.raw[112 + 0x1C] = (uint8_t)ph;
          srand(3); T->midi_in(0x90, 60, 100);
          Chan* c = nullptr; for (auto& x : T->ch) if (x.active) c = &x;
          float l, r; for (auto& v : y) { T->render(&l, &r, 1); v = c ? c->last : 0; }
          if (c && sync) { V.raw[112 + 0x1C] = 32; for (int k = 0; k < 512; k++) T->render(&l, &r, 1); y[0] = c->phOff[0]; } };   // a sounding note follows an edit
      std::vector<double> a(2400), b(2400), c(2400), d(2400);
      run(1, 0, a); run(1, 64, b); run(0, 0, c); run(0, 64, d);
      double ab = 0, aa = 0; for (size_t i = 1; i < a.size(); i++) ab += a[i] * b[i], aa += a[i] * a[i];
      ck("phase: 64 is half a turn on a key-synced operator", aa > 0 && ab / aa < -0.99);
      ck("phase: and nothing without key sync", c == d);
      ck("phase: refresh_regs carries an edit to a sounding note", a[0] == 0.25 && b[0] == 0.25); }

    // The allocator, against the unit itself (FS1R.unlock captures/2026-09-25-7, session6 alloc).
    // Filling an empty machine with notes 36..67 on part 1 put them on channels 1..31 then 0, and a
    // 33rd note of 100 arriving on the full part took channel 2 (which held note 37). Both are
    // round-robin behaviour the old "lowest free, else oldest" got wrong.
    { init_perf(S.perf); Voice& V = S.perf.part[0].voice;
      V.v[0].form = 0; V.v[0].level = 99; V.v[0].fixed = 0; V.v[0].coarse = 1; V.v[0].keysync = 1;
      for (int i = 0; i < 4; i++) { V.v[0].L[i] = i == 3 ? 0 : 99; V.v[0].T[i] = i == 3 ? 60 : 0; }
      S.all_off(); S.nextChan[0] = 0; for (int i = 0; i < 4; i++) S.partChan[i] = 0;
      float l[8], r[8];
      int order[NCHAN], n = 0;
      for (int note = 36; note < 36 + NCHAN; note++) {
          S.midi_in(0x90, note, 100); S.render(l, r, 4);
          int got = -1;                                    // the channel this note landed on
          for (int i = 0; i < NCHAN; i++) if (S.ch[i].active && S.ch[i].note == note) got = i;
          order[n++] = got;
      }
      bool rr = true;
      for (int i = 0; i < NCHAN; i++) rr = rr && order[i] == (i + 1) % NCHAN;
      ck("allocation is round robin: 32 notes land on channels 1..31 then 0", rr);
      // the 33rd, above every held note: the unit took channel 2, which held note 37
      int before = -1;
      for (int i = 0; i < NCHAN; i++) if (S.ch[i].note == 37) before = i;
      S.midi_in(0x90, 100, 100); S.render(l, r, 4);
      int took = -1;
      for (int i = 0; i < NCHAN; i++) if (S.ch[i].active && S.ch[i].note == 100) took = i;
      ck("the 33rd note steals the channel the unit stole (ch 2)", took == 2 && before == 2);
      S.all_off(); init_perf(S.perf); }

    // The polyphony readout counts channels, not keys. It used to count notes held or sustained, so a
    // voice with a long release left the display the moment the key came up while the channel was still
    // sounding and still unavailable to the allocator.
    { init_perf(S.perf); Voice& V = S.perf.part[0].voice;
      init_default_voice(V);
      S.perf.part[0].p[1] = 2; S.perf.part[0].p[4] = 0x10;
      V.v[0].form = 0; V.v[0].level = 99; V.v[0].fixed = 0; V.v[0].coarse = 1; V.v[0].keysync = 1;
      for (int i = 0; i < 4; i++) { V.v[0].L[i] = i == 3 ? 0 : 99; V.v[0].T[i] = i == 3 ? 36 : 0; }   // release runs about half a second
      S.all_off();
      std::vector<float> l(1024), r(1024);
      S.midi_in(0x90, 60, 100); S.render(l.data(), r.data(), 1024);
      ck("a sounding note counts as one voice", S.active_chans() == 1);
      S.midi_in(0x80, 60, 0); S.render(l.data(), r.data(), 1024);
      ck("a released note goes on counting while its release runs", S.active_chans() == 1);
      int blocks = 0; while (S.active_chans() && ++blocks < 200) S.render(l.data(), r.data(), 1024);
      ck("and stops counting once the release is over", S.active_chans() == 0);
      // A release that ends on a floor (L4 > 0, Nightmare's -91 dB) keeps the channel owned for good, and once
      // it is below hearing it stops counting: that pinned the readout at 32.
      V.v[0].L[3] = 10; S.midi_in(0x90, 60, 100); S.render(l.data(), r.data(), 1024); S.midi_in(0x80, 60, 0);
      blocks = 0; while (S.active_chans() && ++blocks < 200) S.render(l.data(), r.data(), 1024);
      ck("a release parked on an inaudible floor stops counting", S.active_chans() == 0 && std::any_of(std::begin(S.ch), std::end(S.ch), [](const Chan& c) { return c.active; }));
      S.all_off(); init_perf(S.perf); }

    // display/egview.h calculates the editor's EG curves without stepping the engine, so check it against the engine.
    // Step copies of a note's EGs and compare when each stage ends:
    // within half a tick per stage for pitch and filter, within 1 ms or 1% for amplitude.
    { init_perf(S.perf); S.init_system(); Voice& V = S.perf.part[0].voice; Part& pt = S.perf.part[0];
      init_default_voice(V);
      pt.p[1] = 2; pt.p[4] = 0x10; pt.p[0x1A] = 70; pt.p[0x1B] = 58; pt.p[0x1C] = 64; pt.p[0x20] = 60; pt.p[0x21] = 70; pt.p[0x22] = 64; pt.p[0x23] = 64;
      const int L[4] = {99, 70, 40, 0}, T[4] = {20, 45, 60, 50}, pL[5] = {30, 80, 40, 40, 50}, pT[4] = {30, 40, 50, 60}, fL[4] = {100, 20, 70, 50}, fT[4] = {30, 40, 50, 60};
      for (int i = 0; i < 4; i++) V.v[0].L[i] = L[i], V.v[0].T[i] = T[i], V.pegT[i] = pT[i];
      for (int i = 0; i < 5; i++) V.pegL[i] = pL[i];
      V.v[0].hold = 30; V.v[0].tscale = 7; V.pegVel = 1; V.pegRange = 1; V.pegTscale = 3;
      S.all_off();
      S.midi_in(0x90, 72, 100);
      Chan* c = nullptr; for (auto& x : S.ch) if (x.active && x.note == 72) c = &x;
      ck("egview: a note to hold it to", c != nullptr);
      if (c) {
          egview::Input in; in.note = c->noteP; in.vel = c->vel;
          // The times (in seconds) of the first n stage changes of a stepped EG.
          auto ends = [](auto step, auto stage, double rate, size_t n) {
              std::vector<double> at;
              int s = stage();
              for (long long k = 1; k < 30 * rate && at.size() < n; ++k) { step(); if (stage() != s) s = stage(), at.push_back(k / rate); }
              return at;
          };
          auto agree = [&](const char* what, const egview::EgCurve& g, const std::vector<double>& at, double tol) {
              bool ok = at.size() >= 3;
              for (size_t i = 0; ok && i < at.size(); i++) ok = std::fabs(g.t[(size_t)g.corner[i]] - at[i]) <= std::max(tol * (i + 1), at[i] * 0.01);
              ck(what, ok);
          };
          in.egKind = egview::AMP; in.hold = 30; in.timeScale = 7; in.part[0] = 70; in.part[1] = 58; in.part[2] = 64;
          for (int i = 0; i < 4; i++) in.L[i] = L[i], in.T[i] = T[i];
          EG e = c->op[0].eg;
          agree("egview: the amplitude EG ends its hold and stages where the engine does", egview::curve(in),
                ends([&] { e.tick(); }, [&] { return e.stage; }, SR, 3), 0.001);
          in = egview::Input(); in.note = c->noteP; in.vel = c->vel;
          in.egKind = egview::PITCH; in.init = pL[0]; in.velSens = 1; in.range = 1; in.timeScale = 3; in.part[0] = 60; in.part[1] = 70;
          for (int i = 0; i < 4; i++) in.L[i] = pL[i + 1], in.T[i] = pT[i];
          Chan pc = *c;
          agree("egview: the pitch EG ends its stages where the engine does", egview::curve(in),
                ends([&] { S.peg_tick(pc); }, [&] { return pc.pegStage; }, TICK_HZ, 3), 0.51 / TICK_HZ);
          // The filter EG runs in the chip: its first two segments end where the EG view puts its corners,
          // and one segment's time after the key comes up the level has covered two thirds of the way back.
          Part& fp = S.perf.part[0]; uint8_t* b = fp.voice.raw;
          S.all_off(); fp.p[7] = 1;
          using namespace fsvr::vop3host::filter;
          for (int i = 0; i < 3; i++) b[EG_L1 + i] = (uint8_t)fL[i], b[EG_T1 + i] = (uint8_t)(fT[i] - 22);
          b[EG_L4] = (uint8_t)fL[3]; b[EG_T4] = (uint8_t)(fT[3] - 22); b[EG_SCALING] = 3 << 3 | 2; b[EG_DEPTH] = 64;
          S.midi_in(0x90, 72, 100);
          int slot = -1; for (int i = 0; i < 16; i++) if (S.ch[i].active) slot = i;
          ck("egview: a filtered note to hold it to", slot >= 0);
          if (slot >= 0) {
              in = egview::Input(); in.note = S.ch[slot].noteP; in.vel = S.ch[slot].vel;
              in.egKind = egview::FILTER; in.velSens = 3; in.timeScale = 2; in.part[0] = 70; in.part[1] = 58; in.part[2] = 64;
              for (int i = 0; i < 4; i++) in.L[i] = fL[i], in.T[i] = fT[i] - 22;
              const egview::EgCurve g = egview::curve(in);
              const Modulators::Eg& eg = S.flt.bank[0].mod.eg[(slot + 2) & 15];
              float l[1], r[1]; std::vector<double> at;
              for (int k = 1, st = 1; k < SR * 2 && at.size() < 2; k++) {
                  S.render(l, r, 1);
                  if (S.flt.bank[0].host.stage[slot] != st) st = S.flt.bank[0].host.stage[slot], at.push_back(k / (double)SR);
              }
              bool ok = at.size() == 2;
              for (size_t i = 0; ok && i < 2; i++) ok = fabs(g.t[(size_t)g.corner[i]] / at[i] - 1) < 0.05;
              ck("egview: the filter EG ends its first two segments where the chip does", ok);
              ck("filter EG: the host's stage follows the chip's reports", S.flt.bank[0].host.stage[slot] == 3);
              // the view draws the release from level 3 to within a unit of level 4, which gives its segment's time
              const double from = eg.level, to = (fL[3] - 50) * 256 / 50, T4 = g.t.back() * log(3.0) / log(fabs((fL[2] - fL[3]) * 256 / 50.0));
              S.midi_in(0x80, 72, 0);
              for (int k = 0; k < (int)(T4 * SR); k++) S.render(l, r, 1);
              ck("filter EG: the release moves, two thirds of the way in its segment's time", fabs((eg.level - from) / (to - from) - 2.0 / 3) < 0.05);
          }
          S.all_off(); fp.p[7] = 0;
      }
      S.all_off(); init_perf(S.perf); }

    // display/display.h, the transcribed display tables, against values the Data List and the manual give.
    { using namespace display;
      ck("fixed frequency: 440.2 at coarse 16, fine 0", !strcmp(FIXED_FREQUENCY[0][15], "440.2"));
      ck("fixed frequency tops out at 28024 Hz", !strcmp(FIXED_FREQUENCY[127][20], "28024"));
      ck("ratio spans 0.500 to 61.69", ratio(0, 0) == 0.5 && fabs(ratio(31, 99) - 61.69) < 1e-9);
      const EffectType& hall = REVERB[1];
      const ValueTable& rt = VALUE_TABLES[hall.slot[0].table];
      ck("Hall1's Reverb Time at 0x50 starts at 2.0 s", hall.slot[0].lsb == 0x50 && !strcmp(rt.text[hall.slot[0].init - rt.sysexMin], "2.0"));
      const EffectType& echo = REVERB[15];
      const ValueTable& fb = VALUE_TABLES[echo.slot[1].table];
      ck("Echo's Lch FB Level starts at +22", !strcmp(fb.text[echo.slot[1].init - fb.sysexMin], "+22"));
      ck("Echo's Lch Delay1 starts at 220.0 ms (14-bit, 0.1 ms)", echo.slot[0].init == 2200 && VALUE_TABLES[echo.slot[0].table].count == 0); }

    // The effect chip: its dry path against the tap, a reverb, the type and parameter paths, the LFO rate and the wiring.
    { init_perf(S.perf);
      const uint8_t* init = (const uint8_t*)&S.perf;          // the Init performance's common bytes: every effect off, the EQ flat
      auto fresh = [&](std::initializer_list<std::pair<int, int>> set) {   // an effect section loaded with them and these bytes
          uint8_t p[192]; memcpy(p, init, 192);
          for (auto& b : set) p[b.first] = (uint8_t)b.second;
          auto e = std::make_unique<Effects>(); e->load(p); return e; };
      auto tone = [](double hz, size_t len, size_t on) {      // a sine at 0.1 for `on` samples, then silence up to `len`
          std::vector<double> x(len, 0.0); for (size_t i = 0; i < on; i++) x[i] = 0.1 * sin(2 * PI * hz * i / SR); return x; };
      auto drive = [](Effects& e, int bus, const std::vector<double>& x, std::vector<double>& l, std::vector<double>& r) {
          l.assign(x.size(), 0.0); r.assign(x.size(), 0.0);
          for (size_t i = 0; i < x.size(); i++) { double b[8] = {}; b[2 * bus] = b[2 * bus + 1] = x[i]; e.run(b, l[i], r[i]); } };
      auto energy = [](const std::vector<double>& l, const std::vector<double>& r, size_t from) {
          double s = 0; for (size_t i = from; i < l.size(); i++) s += l[i] * l[i] + r[i] * r[i]; return s; };
      const size_t n = SR / 2;
      const std::vector<double> sine = tone(1000, n, n), burst = tone(1000, n, SR / 10);
      std::vector<double> l, r;
      // 1. a dry bus at volume 127 comes out at cal::OUT_GAIN
      { auto e = fresh({});
        drive(*e, 0, sine, l, r);
        double in = 0, out = 0; for (size_t i = n / 2; i < n; i++) { in += sine[i] * sine[i]; out += l[i] * l[i]; }
        ck("effect chip: the dry path at volume 127 is OUT_GAIN to 0.05 dB", fabs(10 * log10(out / in) - 20 * log10(cal::OUT_GAIN)) < 0.05); }
      // 2. Hall 1 on the reverb send rings on after a burst; with the reverb off nothing comes back from that bus
      double hallTail;
      { auto h = fresh({{169, 64}, {170, 100}}), o = fresh({});   // reverb pan centre, return 100
        fx::setType(h->host, fx::REVERB, 1);
        drive(*h, 3, burst, l, r); hallTail = energy(l, r, n / 2);
        drive(*o, 3, burst, l, r);
        ck("effect chip: Hall 1 rings on after the burst", hallTail > 1e-6 && hallTail > 100 * energy(l, r, n / 2)); }
      // 3. the same reverb as parameter changes: the sync sends the type, the host loads its defaults and hands them back
      { auto y = fresh({});
        uint8_t c[192]; memcpy(c, init, 192); c[168] = 1; c[169] = 64; c[170] = 100;
        y->sync(c);
        const display::EffectType& hall = display::REVERB[1];   // Reverb Time at 0x50, Diffusion at 0x52
        ck("effect chip: a type change writes the type's defaults back into the performance", c[0x51] == hall.slot[0].init && c[0x53] == hall.slot[1].init);
        drive(*y, 3, burst, l, r);
        ck("effect chip: a synced reverb type reaches the chip", energy(l, r, n / 2) > 0.5 * hallTail);
        fx::setParam(y->host, fx::REVERB, 1, 30); fx::setParam(y->host, fx::REVERB, 11, 3);   // Reverb Time, a word; Rev Delay, a byte
        ck("effect chip: a parameter reaches the host by its slot", y->host.readParameter(0x50) == 30 && y->host.readParameter(0x62) == 3); }
      // 4. the insertion Tremolo's LFO runs at its default, 1.831 Hz: the strongest 0.3..40 Hz component of the left level
      { auto t = fresh({{176, 64}, {179, 127}});               // insertion pan centre, level 127
        fx::setType(t->host, fx::INSERTION, 11);
        drive(*t, 1, tone(1000, 2 * SR, 2 * SR), l, r);
        std::vector<double> env; const size_t w = SR / 100;
        for (size_t at = SR / 4; at + w <= l.size(); at += w) { double s = 0; for (size_t i = 0; i < w; i++) s += fabs(l[at + i]); env.push_back(s / w); }
        double mean = 0; for (double e : env) mean += e; mean /= env.size();
        double best = 0, hz = 0;
        for (double f = 0.3; f < 40; f += 0.02) {
            double re = 0, im = 0;
            for (size_t k = 0; k < env.size(); k++) { double ph = 2 * PI * f * k / 100.0; re += (env[k] - mean) * cos(ph); im -= (env[k] - mean) * sin(ph); }
            double m = sqrt(re * re + im * im); if (m > best) { best = m; hz = f; } }
        ck("effect chip: the insertion Tremolo runs at its default LFO Freq, 1.831 Hz", fabs(hz - 1.831) < 0.06); }
      // 4b. s41: the insertion Chorus sweeps its delay at its default LFO Freq, 0.275 Hz, not twice it: the strongest
      // 0.1..4 Hz component of the left output's instantaneous frequency, from its zero crossings
      { auto t = fresh({{176, 64}, {179, 127}});
        fx::setType(t->host, fx::INSERTION, 1);
        drive(*t, 1, tone(1000, 8 * SR, 8 * SR), l, r);
        std::vector<double> per; double last = -1;          // zero-crossing periods, one per cycle of the 1 kHz tone
        for (size_t i = SR; i + 1 < l.size(); i++) if (l[i] <= 0 && l[i + 1] > 0) {
            const double z = i + l[i] / (l[i] - l[i + 1]); if (last >= 0) per.push_back(z - last); last = z; }
        double mean = 0; for (double v : per) mean += v; mean /= per.size();
        double best = 0, hz = 0;
        for (double f = 0.1; f < 4; f += 0.005) {
            double re = 0, im = 0;
            for (size_t k = 0; k < per.size(); k++) { double ph = 2 * PI * f * k / 1000.0; re += (per[k] - mean) * cos(ph); im -= (per[k] - mean) * sin(ph); }
            double m = sqrt(re * re + im * im); if (m > best) { best = m; hz = f; } }
        ck("effect chip: the insertion Chorus sweeps at its default LFO Freq, 0.275 Hz (s41)", fabs(hz - 0.275) < 0.03); }
      // 5. the variation Rotary SP on a 220 Hz sine does not hiss: under -70 dB of its energy above 4 kHz, from 1 s on
      { auto t = fresh({{172, 64}, {173, 100}});               // variation pan centre, return 100
        fx::setType(t->host, fx::VARIATION, 8);
        const int len = 4096;
        drive(*t, 2, tone(220, SR + len, SR + len), l, r);
        std::vector<double> x(len), cs(len), sn(len);
        for (int i = 0; i < len; i++) { cs[i] = cos(2 * PI * i / len); sn[i] = sin(2 * PI * i / len); x[i] = 0.5 * (l[SR + i] + r[SR + i]) * (0.5 - 0.5 * cs[i]); }
        double high = 0, all = 0;
        for (int k = 1; k < len / 2; k++) {
            double re = 0, im = 0;
            for (int i = 0; i < len; i++) { re += x[i] * cs[(k * i) & (len - 1)]; im -= x[i] * sn[(k * i) & (len - 1)]; }
            double e = re * re + im * im; all += e; if (k * (double)SR / len > 4000) high += e; }
        ck("effect chip: the variation Rotary SP has no noise above 4 kHz", 10 * log10(high / all + 1e-30) < -70); }
      // 5b. a type change leaves the window it reloads with no state of its own. The host silences the window with
      // its clear program (EffectHost::clearWindow), and that program's whole job is to zero the window's registers,
      // so the type that follows starts from zero rather than inheriting what the old one held. Gate Reverb into
      // Distortion is the pair a performance change hit hardest: the distortion took the old registers for its
      // own, and a tone the window had been playing at 0.02 came back out of it at full scale.
      { auto t = fresh({{176, 64}, {179, 127}});               // insertion pan centre, level 127
        fx::setType(t->host, fx::INSERTION, 26);               // Gate Reverb
        drive(*t, 1, tone(1000, SR / 2, SR / 2), l, r);
        double driven = 0, held = 0, after = 0;
        for (size_t i = 0; i < l.size(); i++) driven = std::max(driven, std::max(fabs(l[i]), fabs(r[i])));
        for (int i = 67; i < 87; i++) held = std::max(held, fabs(t->state.r[i]));   // the insertion window's registers
        fx::setType(t->host, fx::INSERTION, 21);               // Distortion
        for (int i = 67; i < 87; i++) after = std::max(after, fabs(t->state.r[i]));
        ck("effect chip: an insertion type change zeroes the window's registers", held > 1 && after == 0);
        drive(*t, 1, std::vector<double>(SR / 10, 0.0), l, r);
        double burst = 0;
        for (size_t i = 0; i < l.size(); i++) burst = std::max(burst, std::max(fabs(l[i]), fabs(r[i])));
        ck("effect chip: a type change does not step the window above the level it was playing", burst < driven); }
      // 5c. a performance load starts the whole section from silence (Effects::load). Ours: a chip program
      // reaches a window's registers and nothing else, so the cells and the delay memory are cleared here
      // instead; and because the new performance's mixer words all land in one sample, the section fades in
      // rather than stepping, but only when a load finds something sounding - a render that opens by loading
      // its performance must not open with a fade.
      { auto t = fresh({{168, 1}, {169, 64}, {170, 100}});     // Hall 1, pan centre, return 100
        drive(*t, 3, tone(1000, SR / 2, SR / 2), l, r);
        double ram = 0, cells = 0;
        for (double v : t->state.ram) ram = std::max(ram, fabs(v));
        for (double v : t->state.m) cells = std::max(cells, fabs(v));
        ck("effect chip: a reverb leaves state behind it", ram > 0.01 && cells > 0.01);
        uint8_t c[192]; memcpy(c, init, 192);
        t->load(c);
        double left = 0;                                       // nothing a program computed survives the load
        for (double v : t->state.ram) left = std::max(left, fabs(v));
        for (double v : t->state.r) left = std::max(left, fabs(v));
        for (double v : t->state.t) left = std::max(left, fabs(v));
        for (int i = 42; i <= 55; i++) left = std::max(left, fabs(t->state.m[i]));
        // the cells the clear programs refill from this sample's inputs and LFOs are not state and are not checked
        ck("effect chip: a performance load clears the state the programs carry", left == 0);
        ck("effect chip: a load under something sounding fades the section in", t->fade > 0);
        auto q = fresh({});                                    // nothing has ever played through this one
        q->load(c);
        ck("effect chip: a load into silence does not fade", q->fade == 0); }
      // 6. through the engine: a part with its insertion switch on is heard through the insertion and has no dry path
      { auto peak = [&] { std::vector<float> a(256), b(256); float m = 0;
            for (int k = 0; k < 10; k++) S.render(a.data(), b.data(), 256);   // the chip rings down what the last run left, some 15 ms
            S.midi_in(0x90, 60, 100);
            for (int k = 0; k < 40; k++) { S.render(a.data(), b.data(), 256); for (int i = 0; i < 256; i++) m = std::max(m, std::max(fabsf(a[i]), fabsf(b[i]))); }
            S.all_off(); return m; };
        float direct = peak();
        S.perf.part[0].p[0x14] = 1; S.perf.fx[176 - 80] = 64; S.perf.fx[179 - 80] = 127;   // part 1 on the insertion, pan centre, level 127
        float inserted = peak();
        S.perf.fx[179 - 80] = 0;
        float silent = peak();
        ck("engine: a note sounds through the effects", direct > 0.001f);
        ck("engine: a part on the insertion is heard through it", inserted > direct * 0.1f);
        ck("engine: a part on the insertion has no dry path", silent < direct * 0.001f);
        init_perf(S.perf); }
      // 6b. taking a performance cuts the sound first, as the unit does: all sound off, so the channels go
      // where they are rather than releasing. Before this only the plug-in's preset browser did it and a
      // performance bulk from a host or the MIDI in left notes playing on through the new voices.
      { std::vector<float> a(256), b(256);
        uint8_t bulk[400];                                       // the performance as its own bulk
        memcpy(bulk, (const uint8_t*)&S.perf, 192);
        for (int p = 0; p < 4; p++) memcpy(bulk + 192 + 52 * p, S.perf.part[p].p, 52);
        S.midi_in(0x90, 60, 100);
        for (int k = 0; k < 8; k++) S.render(a.data(), b.data(), 256);
        const int sounding = S.active_chans();
        perf_from_bytes(S, nullptr, bulk);
        ck("engine: a performance cuts the sound before it loads", sounding > 0 && S.active_chans() == 0);
        init_perf(S.perf); }
      // 7. the host's register writes, pinned: its start and a load, every type with each parameter at both ends,
      //    the mixer and EQ bytes, the controller offsets, and a load of every byte.
      { namespace H = fsvr::vop3host;
        fx::HashSink sink;
        auto h = std::make_unique<H::EffectHost>(); h->sink = &sink;
        uint8_t perf[H::COMMON_BYTES] = {};
        perf[H::VOLUME] = 127; perf[H::INSERTION_LEVEL] = 127;
        perf[H::REVERB_PAN] = perf[H::REVERB_RETURN] = perf[H::VARIATION_PAN] = perf[H::VARIATION_RETURN] = perf[H::INSERTION_PAN] = 64;
        perf[H::EQ_LOW_GAIN] = perf[H::EQ_MID_GAIN] = perf[H::EQ_HIGH_GAIN] = 64;
        perf[H::EQ_LOW_FREQ] = 20; perf[H::EQ_MID_FREQ] = 40; perf[H::EQ_HIGH_FREQ] = 50; perf[H::EQ_MID_Q] = 7;
        h->init(); h->load(perf);
        const int types[3] = {17, 29, 41};
        for (int b = 0; b < 3; b++) for (int t = 0; t < types[b]; t++) {
            fx::setType(*h, fx::Block(b), t);
            for (int slot = 1; slot <= 16; slot++) {
                if (b == fx::VARIATION && t == 24 && slot == 11) continue;   // Karaoke's hidden parameter
                for (int w : {0, b == fx::REVERB && slot > 8 ? 127 : 16383}) fx::setParam(*h, fx::Block(b), slot, w); } }
        for (int i : {16, 20, 169, 170, 172, 173, 174, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190})
            for (int v : {0, 127}) h->setByte(i, v);
        h->setByte(H::INSERTION_TYPE, 11);
        for (int i = 0; i < 16; i++) for (int v : {-128, 127, 0}) { h->setControlOffset(i, v); h->runPosted(); }
        for (int v : {-128, 127, 0}) { h->setSendReverbOffset(v); h->setSendVariationOffset(v); h->runPosted(); }
        for (int i = 0; i < (int)H::COMMON_BYTES; i++) perf[i] = (uint8_t)((i * 29 + 7) & 127);
        perf[H::REVERB_TYPE] %= 17; perf[H::VARIATION_TYPE] %= 29; perf[H::INSERTION_TYPE] %= 41;   // types the unit has
        h->load(perf);
        ck("effect host: its register writes are the firmware's", sink.h == 0x573bae0212dcff1eull); }
      // 8. the filter's EG generators: a segment's time, its report, the acknowledgement, and a segment with no swing
      { auto c = std::make_unique<Modulators>();
        auto w = [&](int ch, int reg, int v) { c->write(reg, ch, (uint16_t)v); };
        using namespace vop3;
        auto wait = [&] { int n = 0; while (!c->read(EG_STATUS) && n < SR * 4) { c->step(); n++; } return n / (double)SR; };
        c->write(EG_STATUS, 0, 0xffff);
        w(3, EG_RATE, 0); w(3, EG_TARGET, -200); w(3, EG_RATE, 120); w(3, EG_TARGET, 200); w(3, EG_AIM, 400);   // -200 to 200, the aim half the swing past
        double took = wait(), law = cal::FEG_SEG_S * pow(2.0, 120 / cal::FEG_RATE_OCT);
        ck("filter EG: an EG segment takes the law's time and reports its channel", fabs(took / law - 1) < 0.01 && c->read(EG_STATUS) == (0x10 | 3));
        w(3, EG_RATE, 255);
        ck("filter EG: a rate write acknowledges the segment", c->read(EG_STATUS) == 0 && c->eg[3].level == 200);
        w(5, EG_RATE, 0); w(5, EG_TARGET, 100); w(5, EG_RATE, 40); w(5, EG_TARGET, 102); w(5, EG_AIM, 103);
        ck("filter EG: a segment with no swing takes FEG_FLAT_S", fabs(wait() - cal::FEG_FLAT_S) < 0.001); }
      // 9. the filter host's register writes, pinned: sixteen notes through their EG segments, over every filter type
      //    and LFO2 wave, the key sync and the firmware's slip, the sensitivities either side of 7 and segments with no
      //    swing; then the part offsets and every byte marked alone.
      { using namespace fsvr::vop3host::filter;
        fx::HashSink sink;
        auto f = std::make_unique<fsvr::vop3host::FilterHost>(); f->sink = &sink;
        auto report = [&](int s) { sink.status = 0x10 | ((s + 2) & 15); f->egInterrupt(); };
        f->boot(); f->freeSlots(); f->clearStates();
        for (int p = 0; p < 4; p++) {
            for (int i = 0; i < f->VOICE_BYTES; i++) f->voice[p][i] = (uint8_t)((i * 37 + p * 11) & 127);
            for (int i = 0; i < f->PART_BYTES; i++) f->part[p][i] = (uint8_t)((i * 53 + p * 7) & 127); }
        for (int s = 0; s < 16; s++) {
            uint8_t* V = f->voice[s & 3];
            V[FILTER_TYPE] = (uint8_t)(s & 7);                         // the six types and two past them
            V[LFO2_WAVE] = (uint8_t)((s * 3) & 7);
            V[LFO2_KEY_SYNC] = s % 3 == 0; V[5] = s % 3 == 1;          // the key sync, and the firmware's slip on a name byte
            V[RESONANCE_VEL] = V[EG_DEPTH_VEL] = (uint8_t)s;
            if (s % 4 == 0) { V[EG_L1] = V[EG_L4]; V[EG_L3] = V[EG_L2]; }
            f->noteOn(s, s & 3, 30 + 5 * s, 20 + 7 * s);
            for (int k = 0; k < 5; k++) report(s);
            f->setSlotCutoffMod(s, 9 * s - 60); f->flush();
            f->noteOff(s); report(s); }
        for (int p = 0; p < 4; p++) {
            f->setPartCutoffMod(p, 40 - 30 * p); f->setPartLfo2Depth(p, 25 * p - 30); f->setPartLfo2Speed(p, 10 * p - 20); f->flush();
            for (int i = 0; i < f->VOICE_BYTES; i++) { f->mark(f->VOICE + i, p); f->flush(); }
            for (int i = 0; i < f->PART_BYTES; i++) { f->mark(f->PART + i, p); f->flush(); } }
        for (int s = 0; s < 16; s++) f->kill(s);
        f->freeSlots();
        ck("filter host: its register writes are the firmware's", sink.h == 0xf188051d637d4182ull); }
    }

    printf(g_fails ? "selftest: %d FAILURES\n" : "selftest: ok\n", g_fails);
    return g_fails ? 1 : 0;
}

