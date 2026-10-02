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
//            r[0b..12] in place by 0x5a82). Register units: r = 8 x full scale. MEASURED s37: the hardware writes
//            the same mono send into both registers of a pair; writing all six as run() does replays every type
//            tested (Hall1/Plate 1.3-1.8 LSB, Delay LCR 2.5 LSB). The dry mix reaches the output bus already summed (s22), not modelled here.
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
// VOP3-1: the same core on docs/vop3/program_N.bin. Its program structure is mapped (see below); what is
// OPEN is which register carries the voice in and which carries the filtered voice out, so Vop3Filter is not
// wired into the voice yet (VFilter in vop3_filter.h stays the voice filter, which is a model).
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

// VOP3-1, the per-voice filter: the same chip, the same instruction set, a different program
// (docs/vop3/program_N.bin, one per channel). Crystallization: the program structure is now mapped (below); what
// is missing is which register carries the voice's audio in and which carries the filtered out.
//
//   program  FW: 512 steps. Class 2 = 416, class 1 = 57, class 0 = 7, class 3 = 32. Repeats a 20-step filter
//            block writing its own register bank: block A at 0x059 writes r[02..0x18], block B at 0x14d writes
//            r[0x31..0x48], block C at 0x1c9 writes r[0x49..0x60], and the pattern continues in the upper half
//            (0x100 up, the channel's own copy). Each block is: an 8-step header writing the parameter file
//            r[0x65], r[0x66], r[0x68], r[0x69], r[0x6b], r[0x6c] by class-2 op 0 (rA = the destination, one
//            running value passed step to step), then the two biquad sections.
//   inputs   FW s38e: r[0] / r[11] are the COEFFICIENT port (slot then value: FUN_0000C36C stages
//            0x01068F78[slot], FUN_0000B6A4 writes the pair) - the boot program's first steps read r[0]. Do not
//            drive them as audio. OPEN: r[0x64], r[0x67], r[0x6a] are read (as f6c operands, so the filter's
//            coefficients) and never written by any step: they and the voice's audio registers are the hardware's. The candidate audio
//            registers are the block heads - r[07]/r[08]/r[09], r[13]/r[14]/r[15], r[31..35], r[49..4d] (read by
//            op 0/op 3/op 5 steps, never written) - i.e. one three-register lane per voice or per filter state.
//            Which lane is live and which register holds the note is a rig measurement: re-point a probe step
//            into d[10] as in s36 and drive r[07], r[13], r[31], r[49] in turn.
//   outputs  OPEN: no step of this program is an op-1 route-0 step, so the DAC path (d[10]/d[11]) is not how the
//            filtered voice leaves. The output stages 0f0/0f8 (op 1, sel, rB = 1c/1d) are the candidates.
//   consts   FW: DAT_0106842C has never been dumped for VOP3-1; the model runs with zero constants. The
//            parameter file r[0x65..] is what the CPU writes there (FUN_0000B5E2, EPROM 0x800200).
struct Vop3Filter {
    Vop3 chip;
    void load(const uint8_t* prog10, const uint16_t coef[512]) {
        for (int i = 0; i < 512; i++)
            for (int j = 0; j < 5; j++) chip.prog[i].w[j] = (uint16_t)(prog10[10 * i + 2 * j] << 8 | prog10[10 * i + 2 * j + 1]);
        std::memcpy(chip.coef.data(), coef, 512 * sizeof *coef);
    }
    // The CPU's parameter write (FUN_0000B5E2): register `reg` of VOP3-1 <- `v`.
    void param(int reg, uint16_t v) { chip.r[reg] = (double)(int16_t)v / 256.0; }
};
