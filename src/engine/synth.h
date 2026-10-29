// engine/synth.h - struct Synth, the whole machine. Not a public header; src/fs1r.h is.
//
// The firmware's side of the FS1R, rewritten from the disassembly: notes, controllers, Fseq, MIDI,
// the tick and the sample loop. The parts it drives live in their own folders: presets/ (the data it
// decodes), tonegen/ (the tone generator), filters/ and effects/.
#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include "fs1r.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "fs1r/algorithms.h"
#include "fs1r/tables.h"
#include "fs1r/hardware.h"
#include "fs1r/cal.h"
#include "engine/tuning.h"
#include "math/fastmath.h"
#include "engine/scales.h"
#include "presets/voice.h"
#include "presets/performance.h"
#include "presets/rom.h"
#include "tonegen/channel.h"
#include "tonegen/lanes.h"
#include "kernel/tonegen.h"
#include "effects/effects.h"
#include "filters/filter.h"

struct Synth {
    // Ours (docs/Differences.md, more than 32 voices): the engine holds MAX_CHAN channels and allocates nchan of
    // them, the unit's NCHAN by default. Device::setPolyphony asks for poly and the next render latches it
    // (filter_latch), which stops every note. Whatever walks the channels walks chans(), the nchan in use.
    static constexpr int MAX_CHAN = 128;
    static_assert(MAX_CHAN % FILTER_CHANNELS == 0 && Filter::VOICES >= MAX_CHAN, "a filter voice for every channel");
    Perf perf; Chan ch[MAX_CHAN]; int poly = NCHAN, nchan = NCHAN;
    template <class C> struct Span { C* b; C* e; C* begin() const { return b; } C* end() const { return e; } };
    Span<Chan> chans() { return {ch, ch + nchan}; }
    Span<const Chan> chans() const { return {ch, ch + nchan}; }
    uint32_t clock = 0; double gain = 1.0;     // the analogue volume pot, after the tap
    long long now = 0;   // samples rendered: when a channel last ran (Chan::ran, note_on)
    // Ours: the random note phase, part pan 0 and the sample-and-hold LFO draw from this, not from rand(). rand() is
    // one generator shared by every instance in the process and a different sequence on each C library, so two
    // instances took each other's draws and a render on Windows differed from the same render on Linux.
    uint32_t rnd = 0x9E3779B9u;
    uint32_t draw() { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }   // xorshift32, as the noise
    // FUN_0000f7dc's cursors: the last channel allocated, and the last one each part took. Allocation
    // walks from these rather than from channel 0, which is what fixes the channel a note lands on.
    // While a part has its filter on there are two more walks, each over half the channels: the parts
    // with a filter take the sixteen the filter chip is wired to, the others the rest (FUN_0000fa5c,
    // FUN_0000fcfa). nextChan is the cursor of the whole, the low and the high walk.
    int nextChan[3] = {0, 0, FILTER_CHANNELS}, partChan[4] = {0, 0, 0, 0};
    Effects fx;
    // The voice filters. fltSw is the parts' filter switches as last latched, a bit a part.
    // Ours (docs/Differences.md): filter32 gives the channels past the sixteenth filter voices too, a bank per
    // sixteen, so a part with its filter on plays every voice instead of the unit's 16. fltWide is it as last latched.
    Filter flt; int fltSw = 0; bool filter32 = true, fltWide = false;
    bool bend14 = true;                        // ours: keep the pitch bend LSB the unit drops (bend_word)
    // Ours (docs/Differences.md): the unit writes a new cutoff word straight into the filter's coefficient memory
    // (FUN_0000DB3C -> FUN_0000C36C), a semitone a byte, so a breath controller on cutoff steps. This glides it.
    bool cutoffGlide = true;
    // ponytail: one fixed time, ours and not measured; a host-facing knob if someone needs a different feel.
    static constexpr double CUTOFF_GLIDE_MS = 10.0;
    static inline const double CUTOFF_GLIDE_K = 1.0 - std::exp(-1.0 / (CUTOFF_GLIDE_MS * 0.001 * SR));
    int noteChn = 0;                           // the MIDI channel of the note on midi_in is handing note_on
    int flt_banks() const { return fltSw ? (fltWide ? nchan / FILTER_CHANNELS : 1) : 0; }   // banks of sixteen filter voices in use
    void filter_latch();                       // FUN_0002087e: a switch that changed stops every channel
    void filter_sync(int part);                // the part's voice, record and controllers, into the filter host
    // FUN_0002c36c: LFO1 into a slot's cutoff. The fade takes twice the controller's offset, destination 42.
    int filter_mod(const Chan& C) const { return (C.lfoVal * clampi((int)(C.lfoFade >> 8) + filter_fade(C.part), 0, 255)) >> 8; }
    int filter_fade(int part) const { int o = ctrl_offset(part, 42) * 2; return o == 254 ? 255 : o; }
    const Rom* rom = nullptr;
    uint8_t sys[76] = {};                      // system parameters, sysex table 4
    int sysTune() const { return sys[0]; }
    int sysNoteShift() const { return sys[6]; }
    int velCurve() const { return sys[0x0E]; }
    int perfChannel() const { return sys[9]; }             // 0-15, 0x10 = all, 0x7F = off
    int devNumber() const { return sys[0x49]; }
    std::vector<std::vector<uint8_t>> outQ;                // sysex the engine sends back (dump / parameter replies)
    mutable std::mutex mtx;
    // Fseq playback
    Fseq fseq; bool fseqRun = false; double fseqAcc = 0, fseqPeriod = 0.01; int fseqStep = 0, fseqDir = 1; int fseqVel = 100; int fseqPart = -1;
    bool fseqHeld = false, fseqClock = false; double fseqDelay = 0, fseqClockAcc = 0;
    double tickAcc = 0;
    // Ours: counts the ticks and the render calls, the two moments a control refresh's inputs can move, so a
    // refresh whose inputs have not moved is skipped (tonegen/channel.cpp, refresh_chan). Bit-identical.
    uint64_t ctlSerial = 1;
    // Ours: the sample loop runs in segments between ticks (render.cpp), each channel over a whole segment into
    // chanOut and the mix a sample at a time after; the serial loop is its reference, and FSVR_RENDER=serial runs
    // that alone.
    static const int SEG_MAX = 256;            // a segment is at most a tick's 249.6 samples
    bool serialOnly = false;
    double chanOut[MAX_CHAN][SEG_MAX];
    struct Sends { double dry[4], var[4], rev[4]; bool ins[4]; };   // the parts onto the buses, once a call
    fsvr::tonegen::Group group;                // the lane kernel's working set (render_segment)
    void render_serial(float* outL, float* outR, int frames, const Sends& s);
    void render_segment(float* outL, float* outR, int n, const Sends& s);
    void mix_sample(float* outL, float* outR, int i, double v[MAX_CHAN], double pl[4], double pr[4], bool any, const Sends& s);
    // Ours: the engine sleeps while nothing sounds (render.cpp). The output's last sample, and how long it has held.
    float lastL = 0, lastR = 0; int quiet = 0; bool asleep = false;

    Synth() { init_perf(perf); init_system(); fx.load((uint8_t*)&perf); if (const char* r = getenv("FSVR_RENDER")) serialOnly = !strcmp(r, "serial"); }
    void init_system();
    static inline double sendlvl(int v) { return db2lin(-LEVEL_DB * SENDTAB[clampi(v, 0, 127)]); }

    // ---------------------------------------------------------------- per-part derived values
    // ---------------------------------------------------------------- controller sets (FUN_00014DDC)
    // Each of the eight sets sums the sources its 14-bit bitmap enables, then hands the sum and the
    // set's depth to a per-destination handler out of the table at flash 0x3DC04. There are three
    // scalers, and which one a destination uses is exactly the split the owner's manual describes:
    // destinations that "overwrite the edit buffer" edit a part byte, destinations that "directly
    // control the tone generator" return a signed offset.
    //   FUN_00016F9C  clamp((v' * d' * 2) >> 6, -128, 127)             destinations 1-16 and 34-47
    //   FUN_00016F58  clamp(base + ((v' * d' * 2) >> 7), 0, 127)       destinations 18 and 21-33
    //   FUN_00017032  clamp(base + adj((v' * d' * 2) >> 6), 0, 127)    destinations 17, 19, 20
    // with v' = v + (v > 0), d' = d + (d > 0), and adj taking one off a positive result.
    // The part switch at performance common 0x28 + n gates destinations 17-45 only; 1-16 (the insertion
    // parameters and its sends) and 46-47 (the Fseq) are performance-wide and ignore it.
    static int ctrl_bias(int v) { return v + (v > 0 ? 1 : 0); }
    static int scale_f9c(int v, int d) { return clampi((ctrl_bias(v) * ctrl_bias(d) * 2) >> 6, -128, 127); }
    static int scale_f58(int v, int d, int base) { return clampi(base + ((ctrl_bias(v) * ctrl_bias(d) * 2) >> 7), 0, 127); }
    static int scale_f032(int v, int d, int base);
    // The sum for one set: walk the bitmap from bit 0 up, clamping to a signed byte after each source.
    int ctrl_set_sum(int part, int set) const;
    bool ctrl_set_active(int part, int set, int dest) const;
    // A "direct" destination. Every one of these handlers stores a per-part byte rather than adding to
    // one (ctrlDest_36 writes DAT_01029748[part], ctrlDest_37 DAT_01029758, ctrlDest_39 DAT_0102979a),
    // and FUN_00014DDC walks the sets 0 to 7, so two sets on one destination do not compose: the last
    // one evaluated wins, exactly as for the destinations that edit a part byte.
    int ctrl_offset(int part, int dest) const;
    void fx_controls(int part);                // destinations 1-16, into the effect host (controllers.cpp)
    // A destination that edits a part byte: the firmware writes the byte, so the last set that selects
    // it wins rather than the offsets adding up. ctrl_part is the value one would write, stored being the
    // base copy (Part::ctlBase); ctrl_write writes them all into the part's bytes, which everything reads.
    int ctrl_part(int part, int dest, int stored) const;
    void ctrl_write(int part);
    // Destination 34 is its own shape: the depth is the bend range in semitones, not a scaler
    // (ctrlDest_34, and the manual: "if Vcn depth is set to +2 the maximum control value is +2
    // semitones"). The summed source drives it like a bend wheel.
    int ctrl_pitch_bias(int part) const;
    // Destination 35 writes one per-part value that event 0x205 then scales by each operator's own EG
    // bias sense: ~((|scaled| + table[|depth|]) * 2), the table being flash 0x3DDC4 (ctrlDest_35).
    int ctrl_eg_bias(int part) const;
    double bend_word(const Part& pt, int chn) const;   // chn: the note's MIDI channel, which a ranged part bends by
    void part_levels(int part, int& vAtt, int& uAtt) const;

    // ---------------------------------------------------------------- note on (FUN_00010b48 / FUN_000112c0 / FUN_000125f8)
    void note_on(int part, int note, int vel);
    // The Fseq retrigger test is the part's own held-note count, not the machine's: FUN_0001228c
    // increments DAT_010288dc[part] and calls the trigger only when that count reaches 1.
    bool anyOther(const Chan& C) const { for (auto& x : chans()) if (&x != &C && x.active && x.held && x.part == C.part) return true; return false; }
    // The frame writer (FUN_000197b4) runs whenever the mode byte is 1 or 2 and the part matches; it is
    // not gated on the sequence still advancing, so a sequence that has played out holds its last frame
    // on the registers. The start delay is the one thing that does gate it: FUN_000125f8 clears the
    // track masks while DAT_010291ca is counting, so until the delay expires the operators are the
    // voice's own.
    bool fseq_on(int part) const;

    // Everything the controller matrix feeds an operator, rebuilt on the tick rather than at note-on:
    // FUN_00014DDC re-runs the destination handler every time one of its sources moves, and
    // FUN_0001B334 (voiced) and FUN_0001D056 (unvoiced) then rebuild the whole per-part array, so a
    // held note follows the wheel. The frequency bias reaches the formant and fixed operators only,
    // since the ratio branch of FUN_0001E838 adds nothing, and the unvoiced one only in normal mode.
    void refresh_bias(Chan& C, const Part& pt);

    // FUN_00010dc4: note shifts, note table, part detune, master tune
    void compute_pitch(Chan& C, const Part& pt, int note);
    void retune(Chan& C, int note);
    void voice_changed(int part);              // rebuild the sounding channels' words after a voice edit
    // per-operator note-dependent values: levels (FUN_00012a32/FUN_00012fcc/FUN_00012e2c), frequency words (FUN_0001e838/FUN_000135d8),
    // formant transpose words (FUN_00013bc6), bandwidth registers (FUN_0001ba06), EG rates with part offsets
    void setup_ops(Chan& C, const Part& pt, int vel);
    // FUN_00026c5a: pitch EG per note. Levels PEGLVL (v-128)<<7 * velocity >> range, rates PEGTIME scaled by velocity and key
    void setup_peg(Chan& C, const Part& pt, int vel);
    void note_off(int part, int note);
    void fseq_keys();
    void release(Chan& C);
    void silence_part(int part);                     // FUN_00010384(0) on one part
    void part_written(int part, int al, int was);    // the firmware's handler after a part byte is written
    void set_sustain(int part, bool on) { Part& pt = perf.part[part]; pt.sustain = on; if (!on) for (auto& c : chans()) if (c.active && c.part == part && c.sustained) { c.sustained = false; release(c); } }
    // What the allocator counts as taken: a key down, or one the sustain pedal holds. FUN_00011E40, the poly
    // note off, clears a channel's in-use word (0x010287DE) and its part (0x010289BC) the moment its key comes
    // up with no sustain, so a channel whose release is still sounding is free to the next note, which damps
    // it (note_on). A channel used to stay taken until all sixteen of its EGs passed -120 dB, which is up to
    // 73 s after the key on A020 Vox Morph, and a full machine stole held notes in its place (#54).
    static bool owned(const Chan& c) { return c.active && (c.held || c.sustained); }
    // What the POLY readout counts: a key down or sustained, or a released note still audible. The active flag
    // is not it: a release can sit at a -91 dB L4 floor, or crawl through -70 dB for tens of seconds, and that
    // pinned the readout at 32 on Zap ! and Nightmare.
    // ponytail: the loudest carrier EG against a fixed floor; the channel's output level if this misreads a patch.
    static constexpr double POLY_AUDIBLE_DB = -60;
    bool audible(const Chan& c) const {
        const unsigned char* alg = FS1R_ALG[perf.part[c.part].voice.alg];
        for (int o = 0; o < 8; o++) {
            const OpState& s = c.op[o];
            if ((alg[2 * o + 1] & 1) && s.eg.cur - s.attS > POLY_AUDIBLE_DB) return true;
            if (s.ueg.cur - s.uatt > POLY_AUDIBLE_DB) return true;
        }
        return false;
    }
    int active_chans() const { int n = 0; for (auto& c : chans()) n += c.active && (c.held || c.sustained || audible(c)); return n; }
    void all_off() {
        for (auto& c : ch) c.active = false;   // every channel held, not just those in use: a polyphony change shrinks nchan next
        for (auto& p : perf.part) { p.nheld = 0; }
        fseqRun = false; fseqHeld = false;
        for (int n = 0; n < flt_banks() * FILTER_CHANNELS; n++) flt.host(n).kill(n & 15);   // FUN_00010384
    }
    void all_release() { for (auto& c : chans()) if (c.active && (c.held || c.sustained)) { c.held = c.sustained = false; release(c); } for (auto& p : perf.part) p.nheld = 0; }

    // ---------------------------------------------------------------- Fseq (FUN_0000fffa / FUN_0001a59e)
    // The loop points are the performance's own bytes and nothing else. FUN_0000fffa reads 0x1C-0x1F
    // straight into DAT_010291CE and DAT_010291D0 at every trigger; the Fseq header's own pair is the
    // default the panel copies into a performance when you select the Fseq, not a fallback the player
    // reaches for. This used to fall back when the two were equal, which turned a performance that the
    // unit freezes on its first frame into one that plays. Eight factory performances set them equal on
    // purpose, "Zap !" at 127 and "Replicant" at 94, and so did the first draft of 12_fseqlevel, which
    // is how it was found: the unit recorded digital silence where the engine played.
    void fseq_loop(int& lo, int& hi, int& dir) const;
    void fseq_start(int vel);
    // One frame forward. oneway loops the section between the loop points while a key is held and then
    // plays out the rest; round ping-pongs between them (owner's manual page 34).
    void fseq_step();
    void fseq_tick(double dt);
    // 24 ppqn in; the speed word 0..4 selects 1/4, 1/2, 1/1, 2/1, 4/1 frames per clock. KNOWN: FUN_0001ACB6 takes the
    // clocks since the last frame (0x01029206, bumped per 0xF8 in FUN_0002F760) times 100, shifts >>2, >>1, x1, x2, x4
    // by the word, and divides by 100 for the frames to step.
    void midi_clock();

    // ---------------------------------------------------------------- tick: LFO, PEG, portamento, register refresh
    void lfo_tick(Chan& C, const Part& pt);
    void peg_tick(Chan& C);
    void porta_tick(Chan& C);
    void refresh_regs(Chan& C, const Part& pt);
    // Voice common 0x40-0x53: five Formant control destinations and five FM control destinations, each
    // [--ddvooo] where dd = off/out/freq/width, v picks the voiced or unvoiced operator and ooo the
    // operator. The paired depth byte scales the part's FORMANT (0x1D) or FM (0x1E) value.
    // INFERRED scaling: a full-depth knob moves the centre frequency about six semitones.
    void voice_ctrl(Chan& C, const Part& pt);
    // registers 0x22A-0x22F: the pan index (part pan, pan scaling, pan LFO, performance pan, the Panpot
    // controller) read through the firmware's own pan tables as a 0.375 dB attenuation per side.
    // FUN_00025bc8 and FUN_0002c36c keep the pan as 0..255 and index the tables at pan >> 1, and the voice
    // image carries 2 * the part byte at +0x2E, so the table index is the part byte itself, not one below it.
    // The firmware keeps the whole sum in the 0..255 domain and halves it once with pan >> 1 (docs/
    // ymp706_registers.md, Pan). pan_index returns the part-pan-plus-scaling in the index domain, so the two
    // terms that follow — the pan LFO and the performance pan — are each summed at 0..255 and land halved.
    void refresh_pan(Chan& C, const Part& pt);
    void tick();

    // ---------------------------------------------------------------- MIDI in (Data List sections 2 to 4)
    // One entry point for channel messages so the plugin layer can feed the engine the same way the
    // console does. Sysex goes through midi_sysex(); replies land in outQ.
    int forceChannel = -1;                       // console -c: every part listens on this channel
    // Ours: the Options page's Quality / Speed (Device::setSpeed). At Full these are the engine as measured: an operator
    // under gateDb is not computed, the control maths runs every ctlDecimation samples and the amplitude EGs every
    // egDecimation (render_chan).
    double gateDb = -100; int ctlDecimation = tuning::CTL_DECIMATION, egDecimation = 1;
    double senseTimer = 0;                       // active sensing: mute if 0xFE stops arriving
    int bankMsb = 0x3F, perfBank = -1;
    // FUN_000191C0 fills the source table from the system control numbers, in this order. The four knob
    // slots are disabled when the system's knob receive switch is off.
    enum { SRC_KN1, SRC_KN2, SRC_KN3, SRC_KN4, SRC_MC1, SRC_MC2, SRC_PB, SRC_CAT,
           SRC_PAT, SRC_FC, SRC_BC, SRC_MC3, SRC_MW, SRC_MC4 };
    static const int SRC_RAW = 0x17C0;           // PB, CAT, PAT, FC, BC, MW keep the value as it comes: PB -128..127, the rest 0..127
    int ccSource(int cc) const;
    // FUN_00014AD0: (v - 64) * 2 for the knobs and MIDI controls, raised so full scale reaches 127;
    // the physical controllers are stored as they come.
    void set_source(Part& pt, int slot, int v);
    bool part_listens(int p, int chn) const;
    void load_perf_bank(int lsb, int prog);      // defined with the ROM loaders
    void midi_in(int st, int d1, int d2);
    void control_change(int p, int cc, int v);
    void data_entry(int p, int v);
    void sense_tick(double dt) { if (senseTimer > 0 && (senseTimer -= dt) <= 0) { senseTimer = 0; all_off(); } }

    // ---------------------------------------------------------------- sysex out (dump and parameter replies)
    void push_sysex(std::vector<uint8_t>& m) { if ((int)outQ.size() < tuning::MIDI_OUT_QUEUE) outQ.push_back(m); }
    void push_bulk(int ah, int am, int al, const uint8_t* d, int n);
    void push_param(int ah, int am, int al, int val);
    int read_param(int ah, int am, int al) const;
    void current_perf_bytes(uint8_t* out) const;
    void fseq_bytes(std::vector<uint8_t>& d) const;
    void dump_request(int ah, int am, int al);

    // The first sixteen channels go through the filter chip while a part has its filter on. The part buses
    // go through the effect chip (fx). Its individual out pair (common 0x14) is not rendered.
    void render(float* outL, float* outR, int frames);
};

int selftest(Synth& S);   // selftest/selftest.cpp
