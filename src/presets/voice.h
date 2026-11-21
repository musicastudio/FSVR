// presets/voice.h - a voice as the firmware decodes it, and the DX7 conversion. KNOWN.
#pragma once
#include <cstdint>

struct OpV {
    int keysync, transpose, coarse, fine, notescale, bwbias, form, fixed, skirt, fseqtrk, bw, detune;
    int fegInit, fegAtt, fegAttT, fegDecT;
    int L[4], T[4], hold, tscale, level, bp, ld, rd, lc, rc;
    int fbias, pms, fmsb, amsb, ams, fms, egbias;   // fmsb/amsb = raw sense nibbles (sens+7)
};
struct OpU {
    int transpose, mode, coarse, fine, notescale, bw, bwbias, res, skirt;
    int fegInit, fegAtt, fegAttT, fegDecT, level, lks;
    int L[4], T[4], hold, tscale;
    int fbias, fmsb, amsb, ams, fms, egbias;
};
struct Voice {
    uint8_t raw[608];
    char name[11]; int cat;
    int lfo1wave, lfo1speed, lfo1delay, lfo1sync, pmd, amd, fmd;
    int noteshift, pegL[5], pegT[4], pegVel, pegRange, pegTscale;
    int fseqV, fseqU, alg, corr[8], fb;
    // Operators whose correction came in as a parameter change since the last voice load or algorithm change:
    // FUN_00032624 writes the nibble without the carrier test FUN_0003251C applies, so a modulator plays it too.
    int corrEdit;
    OpV v[8]; OpU u[8];
};
// DX7 ACED (49 bytes, DX7II/DX7S/TX802 additions), or null for a plain DX7 voice. Only nine of the 49 bytes
// carry anything: six per-operator amplitude mod senses, the pitch EG range, its velocity switch and its rate
// scaling. One that comes over MIDI waits in its engine for the next VCED (Synth::aced).

// Defined in presets/patch.cpp.
void decode_voice(Voice& V);
void init_blank_voice(uint8_t* b);
void init_default_voice(Voice& V);
void apply_aced(const uint8_t* aced, uint8_t* out);
void convert_dx7(const uint8_t* vced, const uint8_t* aced, uint8_t* out);
