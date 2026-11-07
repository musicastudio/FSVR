// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// tonegen/lanes.h - a group of channels side by side, a number a row, for the lane kernel. Ours.
//
// What render_chan (channel.cpp) keeps per operator and per channel, laid out so that one register holds the
// same number of W channels (kernel/simd.h), plus what the kernel decodes once a segment from the voice and the
// algorithm: the forms as masks and table pages, the algorithm's buses as masks. The kernel gathers a group from
// its channels at a segment's start, renders the segment, and scatters it back, or, for a lane that went silent
// or inactive before the end, puts the channel back as it was for the scalar loop to render it (render.cpp).
// Plain data only: the kernel files include this and inline nothing of the engine's (kernel/README.md).
#pragma once
#include <cstdint>
#include "tonegen/channel.h"

namespace fsvr::tonegen {

struct Group {
    static constexpr int WMAX = 4;             // the widest kernel's lanes (avx2); a narrower one fills the first W
    static constexpr int OPS = 8;

    int lanes = 0;                             // channels in the group
    Chan* chan[WMAX] = {};
    const Voice* voice[WMAX] = {};
    bool fseq[WMAX] = {};                      // fseq_on(part) this segment
    double* out[WMAX] = {};                    // each lane's output row for the segment
    double gateDb = -100; int ctlDecimation = 16; uint64_t ctlSerial = 0; long long now = 0;
    int flipAt[WMAX] = {};                     // the first frame a lane went silent or inactive before the end, else -1

    struct Op {
        // the phase generator, and the frequency EG that moves it
        int32_t phase[WMAX], inc[WMAX];        // uint32 bits
        double word[WMAX], fop[WMAX], fw[WMAX], wl7[WMAX];
        int32_t ratio[WMAX];
        int32_t fegStage[WMAX], fegHold[WMAX]; double fegCur[WMAX], fegTarget[WMAX], fegKatt[WMAX], fegKdec[WMAX];
        // the amplitude EG and the level
        int32_t egStage[WMAX]; double egCur[WMAX], egTarget[WMAX], egRate[WMAX], egHold[WMAX]; uint64_t egRising[WMAX];
        double att[WMAX], attS[WMAX], uattS[WMAX], ph0[WMAX];
        // the formant's grains
        double gW[2][WMAX], gC[2][WMAX], gFc[2][WMAX]; uint64_t gOn[2][WMAX]; int32_t nextGen[WMAX]; double fphase[WMAX];
        // the form on each lane: masks, the window page, the all form's mean, and the phase scales of the odd and res forms
        uint64_t sine[WMAX], all[WMAX], odd[WMAX], res[WMAX], frmt[WMAX];
        int32_t winAt[WMAX], skirt[WMAX];      // g_win[fam][skirt] as an offset from g_win[0][0]
        float dc[WMAX];
        double formA[WMAX], formB[WMAX];       // 2 on odd (the window twice a period), 1 else; ratio + 1 on res, 1 else
        // the unvoiced operator
        int32_t uegStage[WMAX]; double uegCur[WMAX], uegTarget[WMAX], uegRate[WMAX], uegHold[WMAX]; uint64_t uegRising[WMAX];
        int32_t ufegStage[WMAX], ufegHold[WMAX]; double ufegCur[WMAX], ufegTarget[WMAX], ufegKatt[WMAX], ufegKdec[WMAX];
        double uatt[WMAX], ninc[WMAX], ucoef[WMAX], udrift[WMAX], uwidth[WMAX];
        int32_t rng[WMAX]; double lp0[WMAX], lp1[WMAX], walk[WMAX], nphase[WMAX];
        // the algorithm's buses, as masks: where the input comes from, and what the output feeds
        uint64_t inFb[WMAX], inCb[WMAX], inH[WMAX], inS[WMAX], toH[WMAX], sClr[WMAX], sAdd[WMAX], toFb[WMAX], carrier[WMAX];
        double out[WMAX];                      // the last sample's output, for OpState::out
        // what every lane of the group has in common this segment, so a path no lane takes is skipped
        bool anySine, anyWin, anyAll, anyFrmt, anyRes, anyOdd;
        int fegOn, ufegOn;                     // the lanes whose frequency EGs are still moving, a bit each
    } op[OPS];

    // the channel
    double fbBus[WMAX], fbPrev[WMAX], fbGain[WMAX], partV[WMAX], partU[WMAX], last[WMAX], damp[WMAX];
    int32_t quiet[WMAX], ctlLeft[WMAX]; uint64_t silent[WMAX], active[WMAX];

    // the segment-start copy of what the kernel changes in a channel, for a lane that flips
    OpState snapOp[WMAX][OPS];
    struct Snap { double fbBus, fbPrev, last, damp, f0, fbGain; int quiet, ctlLeft; long long freezeLeft, ran; bool silent, active;
                  uint64_t ctlSerialC, ctlSerialV[OPS], ctlSerialU[OPS]; } snap[WMAX];
};

}  // namespace fsvr::tonegen
