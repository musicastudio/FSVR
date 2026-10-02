// fs1r/chips/vop3_modules.h - VOP3-2 (effects) and VOP3-1 (filter) as programs on the measured core.
//
// Crystallization: both modules run the firmware's own microcode on Vop3 (vop3_core.h), so every rule the rig
// settles improves them with no change here. What this file owns is the board around the chip, and each piece
// says whether it is MEASURED, FW (read from the firmware) or OPEN.
//
// VOP3-2, per sample:
//   inputs   MEASURED s35h: the hardware writes the reverb send pair into r[0d]/r[0e] and the variation pair
//            into r[0f]/r[10]; no step writes them, steps 002-005 read them in place (s22's r[03..08] were
//            values the program computes). s36: the insertion pair lands in r[11]/r[12] (steps 006/007 scale
//            r[0b..12] in place by 0x5a82). Register units: r = 8 x full scale. OPEN: the mid-pass arrival (r[0e] carries the mono reverb send one pass ahead of r[0d]); written at pass
//            start here. The dry mix reaches the output bus already summed (s22), not modelled here.
//   NOTE s37: the hardware writes the same mono send into BOTH registers of an algorithm's pair; which one the
//            microcode reads is the algorithm's business (Hall1/Plate read r[0d], Delay LCR reads r[0e] /
//            r[10]). Writing both, as run() does, is measured correct: Delay LCR replays to 2.5 LSB.
//   program  FW: base image + one type per window at the upload function's addresses (tools/vop3_e2e.py WIN),
//            selectors from the performance's effect types (docs/vop3_2_params.md, Dispatch).
//   consts   FW: DAT_0106842C as the effect handlers leave it; offsets DAT_0106882C + area base (FUN_000397F4).
//            docs/vop3_2/params.json holds each type's preset-block defaults; a performance's own parameters
//            change them (the rig's Hall1 load, a factory performance's block, differs on 50 of 94 steps).
//            OPEN: computing these words from the parameters in C++ (the handlers' soft-float designer); feed
//            a dump (FS1R.unlock session.json) when exact output matters.
//   output   MEASURED s27/s28: DAC = d[10] (L, one pass late) / d[11] (R), readout d / 4, 18-bit floor.
// VOP3-1: the same core on docs/vop3/program_N.bin; OPEN: per-channel register banks, the voice input
// registers and the output cells are not measured, so Vop3Filter is not wired into the voice yet (VFilter in
// vop3_filter.h stays the voice filter).
#pragma once
#include <cstring>
#include "vop3_core.h"

struct Vop3Effects {
    struct Window { uint32_t rom; int stride, n, first; };   // FW: tools/vop3_e2e.py WIN
    static constexpr Window WIN[5] = {{0x3658D4, 0, 0xE8, 0}, {0x3663B4, 400, 0x28, 0xE8},
                                      {0x366A94, 0x3C0, 0x60, 0x1A0}, {0x36A874, 0x2D0, 0x48, 0x110},
                                      {0x36CA34, 0x2D0, 0x48, 0x158}};
    Vop3 chip;

    // rom = the EPROM in CPU view from 0x200000; sel = the four window selectors (the u16's high byte).
    void load(const uint8_t* rom, const int sel[4], const uint16_t coef[512], const int offs[128]) {
        for (auto& s : chip.prog) s = {};
        for (int w = 0; w < 5; w++) {
            const Window& W = WIN[w];
            const uint8_t* p = rom + (W.rom - 0x200000) + W.stride * (w ? sel[w - 1] : 0);
            for (int k = 0; k < W.n; k++)
                for (int j = 0; j < 5; j++)
                    chip.prog[W.first + k].w[j] = (uint16_t)(p[10 * k + 2 * j] << 8 | p[10 * k + 2 * j + 1]);
        }
        std::memcpy(chip.coef.data(), coef, 512 * sizeof *coef);
        std::memcpy(chip.offs, offs, sizeof chip.offs);
    }
    // Sends in DAC units (full scale 1); out in DAC units.
    void run(const double rev[2], const double var[2], const double ins[2], double& L, double& R) {
        chip.r[0x0D] = 8 * rev[0]; chip.r[0x0E] = 8 * rev[1];
        chip.r[0x0F] = 8 * var[0]; chip.r[0x10] = 8 * var[1];
        chip.r[0x11] = 8 * ins[0]; chip.r[0x12] = 8 * ins[1];   // s36
        chip.pass();
        chip.dac(L, R);
        L /= 8; R /= 8;                          // readout units -> full scale 1
    }
};

struct Vop3Filter {                              // OPEN: outline only, see the header
    Vop3 chip;
    void load(const uint8_t* prog10, const uint16_t coef[512]) {
        for (int i = 0; i < 512; i++)
            for (int j = 0; j < 5; j++) chip.prog[i].w[j] = (uint16_t)(prog10[10 * i + 2 * j] << 8 | prog10[10 * i + 2 * j + 1]);
        std::memcpy(chip.coef.data(), coef, 512 * sizeof *coef);
    }
};
