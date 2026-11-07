// tonegen/channel.h - one tone generator channel: eight operator pairs on an algorithm. INFERRED beyond the register values.
#pragma once
#include <cstdint>
#include "presets/voice.h"
#include "tonegen/operator.h"

// The note-on damp, same shape: what is left of the previous note in a reused channel decays at this
// rate instead of being cut to zero. cal::DAMP_MS.
const double DAMP_K = exp(-1.0 / (cal::DAMP_MS * 0.001 * SR));

struct Chan {
    bool active = false; int part = 0, note = 0, vel = 0; bool held = false, sustained = false; uint32_t age = 0;
    int chn = 0;               // the MIDI channel that keyed it: a part on a range of channels bends each note by its own

    // Ours: a channel whose carriers and noise are over stops rendering (render_chan). quiet counts the
    // zero samples that decide it, and freezeLeft is the samples until its last envelope is done, once
    // that is arithmetic, so the allocator sees it freed on the same sample it would have been.
    bool silent = false; int quiet = 0; long long freezeLeft = -1;
    int owner = -1;      // the part whose note last took the channel, until a silence frees it (DAT_010289fc)
    long long ran = 0;   // Synth::now after the last sample the channel's operators stepped (render), for note_on
    // The channel's last output, and the damp left over from the note this channel was playing before.
    // FUN_00023000: note-on damps the channel it is about to take (the mask table at 0x35B260 is one hot
    // per channel) rather than silencing it, so the previous note's waveform fades instead of being cut
    // between two samples. cal::DAMP_MS is how fast. Both are the channel's own word, ahead of the filter
    // and the pan, which is where the tone generator's fade is.
    double last = 0, damp = 0;
    // CPU side, computed at note-on
    int noteP = 60;            // note after all shifts (DAT_0103937a)
    int pitchNote = 0;         // NOTETAB + detune + tune (DAT_01028a84)
    int keyfact = 0;           // KEYFACT >> 2
    int levelOff[8], ulevelOff[8], freqWord[8], ufreqWord[8], frmtWord[8], bwReg[8], ubwReg[8];
    int detW[8] = {};                // detune, key scaled, in pitch word units (register 0x80)
    int egbias[8], uegbias[8]; // image 0x1C0/0x1C8
    int fbW[8] = {}, ufbW[8] = {};   // frequency bias words, DAT_010282bc / DAT_010283bc
    int egL[8][4], egR[8][4], egHold[8], uegL[8][4], uegR[8][4], uegHold[8];
    // pitch EG (software, FUN_00028c8e)
    bool pegWait = true;   // the note-on lands on the next tick, so the first tick is not the pitch EG's (peg_tick)
    int pegStage = 5, pegCur = 0, pegTarget = 0, pegRate = 255, velP = 128;
    int pegLvl[5], pegRt[5];
    // portamento (FUN_00029048)
    int portaCur = 0, portaTarget = 0, portaRate = 0; bool portaOn = false;
    // LFO1 (FUN_00028828)
    uint32_t lfoPhase = 0, lfoDelay = 0, lfoFade = 0; int lfoVal = 0; int lfoSH = 0;
    // the pan registers (0x22A-0x22F)
    int panBase = 63; double panL = 1, panR = 1;
    int vcLvl[8][2] = {}, vcFreq[8][2] = {}, vcBw[8][2] = {};   // voice Formant/FM control offsets, [op][voiced=0]
    // registers refreshed each tick
    double regPitch = 0; int regPM = 0, regFM = 0, regAM = 0, regLevel[8], regULevel[8], regC0 = 73;
    int fqWord[8] = {}, fquWord[8] = {}; double partV = 1, partU = 1;
    OpState op[8]; double fbBus = 0, fbPrev = 0;
    double f0 = 0, fbGain = 0; int ctlLeft = 0, egLeft = 0;        // control-rate cache, see refresh_ctl; egLeft, render_chan
    uint64_t ctlSerialC = 0, ctlSerialV[8] = {}, ctlSerialU[8] = {};   // the Synth::ctlSerial each refresh last computed under
    bool fseqOp[8] = {}, fseqUOp[8] = {};
    bool filtered = false;     // its note goes through its filter voice, flt channel n
    double phOff[8] = {};      // ours: each voiced operator's initial phase in turns, op byte 0x1C (refresh_regs)
};

// The per-operator frequency and level maths runs every tuning::CTL_DECIMATION samples rather than
// every sample. Its inputs only move on the 192 Hz register tick and the voice parameters, and doing
// it per sample (a dozen pow() calls per operator) was the whole CPU bill. The one input that moves
// every sample, an operator's own pitch EG, stays in the sample loop while it runs: its integral is
// the phase. The rate itself is ours, not the hardware's, so it lives in engine/tuning.h.
// fseq: the part's Fseq is driving the operators this tick (Synth::fseq_on). serial is Synth::ctlSerial, which
// moves whenever an input can have: a refresh under a serial it already computed under is skipped.
void refresh_chan(Chan& C, const Voice& V, uint64_t serial);                // the channel's fundamental and feedback gain
void refresh_op(Chan& C, const Voice& V, bool fseq, int o, uint64_t serial);   // operator o, voiced and unvoiced
void refresh_ctl(Chan& C, const Voice& V, bool fseq, uint64_t serial);        // both, every operator
// The channel's own word, before the filter and the pan. gateDb, ctlDecimation and egDecimation are Synth's cost
// knobs: at an egDecimation of n the amplitude EGs advance n samples at a time and the gains ramp across the block.
double render_chan(Chan& C, const Voice& V, bool fseq, double gateDb, int ctlDecimation, int egDecimation, uint64_t ctlSerial);
// Ours: the channel over a segment of n samples into out, as render_chan renders them one by one; returns the sample it
// went inactive at (n when it did not), and marks busy[t] where it counted as sounding at t (Synth::render's any). now
// is Synth::now at the segment's first sample, for Chan::ran.
int render_chan_block(Chan& C, const Voice& V, bool fseq, double gateDb, int ctlDecimation, int egDecimation, uint64_t ctlSerial,
                      long long now, double* out, bool* busy, int n);
// The unvoiced carriers and their walks run free, per channel and operator: a note never restarts them, and while a
// channel plays nothing its carriers run on at the centres they last played (docs/noise.md, 2026-11-02).
// seed_unvoiced starts channel c's once, each from a seed of its own; keep_unvoiced carries them from was into C, which
// note_on or silence_part has just rebuilt, the carriers run on to now (Synth::now).
void seed_unvoiced(Chan& C, int c);
void keep_unvoiced(Chan& C, const Chan& was, long long now);
