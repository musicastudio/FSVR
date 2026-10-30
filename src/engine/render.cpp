// engine/render.cpp - the sample loop: channels, filter voices, part buses, effects.
#include "engine/synth.h"

void Synth::render(float* outL, float* outR, int frames) {
    std::lock_guard<std::mutex> lk(mtx);
    const tuning::NoDenormals flush;
    ctlSerial++;                               // MIDI and edits land between render calls
    fx.sync((uint8_t*)&perf);
    filter_latch();
    for (int p = 0; p < 4; p++) if (fltSw >> p & 1) filter_sync(p);
    // Part routing: a part with its insertion switch on feeds the insertion bus only, any other the dry bus and both sends.
    Sends s;
    for (int p = 0; p < 4; p++) {
        const uint8_t* q = perf.part[p].p;
        s.ins[p] = (q[0x14] & 1) != 0;
        s.dry[p] = sendlvl(q[0x11]);
        s.var[p] = sendlvl(q[0x12]); s.rev[p] = sendlvl(q[0x13]);
    }
    sense_tick(frames / (double)SR);
    // Ours: the loop runs in segments, a tick to the next, every channel over the whole segment and then the mix a
    // sample at a time (render_segment). Nothing a channel's sample reads moves between ticks but what its own
    // samples before it wrote, so a segment's samples are the serial loop's to the bit; the self check holds the two
    // together. The serial loop takes over while the engine sleeps, when sleep could begin inside this call, and
    // under FSVR_RENDER=serial.
    if (serialOnly || asleep || quiet + frames >= SR) { render_serial(outL, outR, frames, s); return; }
    for (int i = 0; i < frames;) {
        tickAcc += TICK_HZ / SR; while (tickAcc >= 1) { tickAcc -= 1; tick(); }
        int n = 1;                             // to the next tick, the accumulator stepped as the serial loop steps it
        while (i + n < frames && n < SEG_MAX && tickAcc + TICK_HZ / SR < 1) { tickAcc += TICK_HZ / SR; n++; }
        render_segment(outL + i, outR + i, n, s);
        i += n;
    }
}

// A segment: every active channel over its n samples into chanOut, then the samples one by one. The channels that
// sound go through the lane kernel W at a time, those of one part together so a group's lanes share a voice; a
// lane that goes silent or inactive before the end is put back and rendered the scalar way, as is every channel
// the kernel does not take (a silent one counting down, and all of them on the Fast steps).
void Synth::render_segment(float* outL, float* outR, int n, const Sends& s) {
    const int nf = flt_banks() * FILTER_CHANNELS;
    bool busy[SEG_MAX] = {};
    int until[NCHAN] = {};                     // the sample a channel rendered up to; 0 for one that was not active
    bool done[NCHAN] = {};
    const fsvr::TgKernels& K = fsvr::tg_kernels();
    if (egDecimation == 1) {
        int order[NCHAN], count = 0;
        for (int p = 0; p < 4; p++) for (int c = 0; c < NCHAN; c++) if (ch[c].active && !ch[c].silent && ch[c].part == p) order[count++] = c;
        for (int i = 0; i + 1 < count; i += K.lanes) {   // a group of one is the scalar loop's
            fsvr::tonegen::Group& G = group;
            G.lanes = std::min(K.lanes, count - i);
            for (int l = 0; l < G.lanes; l++) {
                const int c = order[i + l];
                G.chan[l] = &ch[c]; G.voice[l] = &perf.part[ch[c].part].voice; G.fseq[l] = fseq_on(ch[c].part); G.out[l] = chanOut[c];
            }
            G.gateDb = gateDb; G.ctlDecimation = ctlDecimation; G.ctlSerial = ctlSerial; G.now = now;
            K.render_group(G, n);
            for (int l = 0; l < G.lanes; l++) if (G.flipAt[l] < 0) { until[order[i + l]] = n; done[order[i + l]] = true; }
        }
        for (int c = 0; c < NCHAN; c++) if (done[c]) { for (int t = 0; t < n; t++) busy[t] = true; break; }   // a lane the kernel kept sounded throughout
    }
    for (int c = 0; c < NCHAN; c++) {
        if (done[c]) continue;
        Chan& C = ch[c];
        until[c] = C.active ? render_chan_block(C, perf.part[C.part].voice, fseq_on(C.part), gateDb, ctlDecimation, egDecimation, ctlSerial, now, chanOut[c], busy, n) : 0;
    }
    for (int t = 0; t < n; t++) {
        double pl[4] = {}, pr[4] = {}, v[NCHAN] = {};
        for (int c = 0; c < NCHAN; c++) {
            if (t >= until[c]) continue;
            const Chan& C = ch[c];
            const double m = chanOut[c][t];
            if (c < nf && (C.filtered || !fltWide)) v[c] = m; else { pl[C.part] += m * C.panL; pr[C.part] += m * C.panR; }
        }
        mix_sample(outL, outR, t, v, pl, pr, busy[t], s);
    }
    now += n;
}

// The reference: a sample at a time throughout.
void Synth::render_serial(float* outL, float* outR, int frames, const Sends& s) {
    // Ours: an engine with nothing to play sleeps. Once no channel has been active and the output has held one
    // value for a second, which is every reverb tail and filter ring-down gone under the DAC's last step, each
    // block computes its first sample in full and repeats it for the rest, and wakes the moment that sample
    // moves or a channel starts. An idle instance was 12 % of a desktop core (A001 Zap!, A020 Vox Morph), the
    // effect section and the filter chip running on silence; asleep it is 1/256 of that. The unit runs its
    // chips regardless. What sleeping changes is invisible at the output: the effect LFOs and the filter's
    // modulators advance one sample a block rather than a block, so they come back at another phase.
    bool skipping = false;
    for (int i = 0; i < frames; i++, now++) {
        if (asleep && i > 0) {
            // The rest of the block repeats the held sample, from wherever in it the engine fell asleep. A silent
            // channel's last envelopes still count down over those samples to the one the allocator frees it on.
            if (!skipping) { skipping = true; for (auto& c : ch) if (c.active && c.silent && (c.freezeLeft -= frames - i) <= 0) c.active = false; }
            outL[i] = lastL; outR[i] = lastR; continue;
        }
        bool any = false;
        tickAcc += TICK_HZ / SR; while (tickAcc >= 1) { tickAcc -= 1; tick(); }
        // The channels, each panned onto its part's pair. While a part has its filter on the first sixteen
        // pass through the filter chip first, sounding or not, so that a filter rings out after its note.
        // With fltWide the other sixteen have filter voices too, and a channel of an unfiltered part goes straight on.
        const int nf = flt_banks() * FILTER_CHANNELS;
        double pl[4] = {}, pr[4] = {}, v[NCHAN] = {};
        for (int n = 0; n < NCHAN; n++) {
            Chan& c = ch[n];
            if (!c.active) continue;
            if (!c.silent || c.freezeLeft < 0) any = true;   // a silent channel counting down plays nothing more
            const bool steps = !c.silent;
            const double m = render_chan(c, perf.part[c.part].voice, fseq_on(c.part), gateDb, ctlDecimation, egDecimation, ctlSerial);
            if (steps) c.ran = now + 1;
            if (n < nf && (c.filtered || !fltWide)) v[n] = m; else { pl[c.part] += m * c.panL; pr[c.part] += m * c.panR; }
        }
        mix_sample(outL, outR, i, v, pl, pr, any, s);
    }
}

// One sample from the channels on: the filter chip over v, the filtered channels panned, the four buses, the
// effects, the output, and whether the engine may sleep.
void Synth::mix_sample(float* outL, float* outR, int i, double v[NCHAN], double pl[4], double pr[4], bool any, const Sends& s) {
    const int nf = flt_banks() * FILTER_CHANNELS;
    flt.run(flt_banks(), v);
    for (int n = 0; n < nf; n++) { pl[ch[n].part] += v[n] * ch[n].panL; pr[ch[n].part] += v[n] * ch[n].panR; }
    for (int p = 0; p < 4; p++) partPeak[p] = std::max(partPeak[p], (float)std::max(std::fabs(pl[p]), std::fabs(pr[p])));
    // Four stereo buses into the effects: dry, insertion, variation send, reverb send.
    double bus[8] = {}, l, r;
    for (int p = 0; p < 4; p++) {
        if (s.ins[p]) { bus[2] += pl[p]; bus[3] += pr[p]; continue; }
        bus[0] += pl[p] * s.dry[p]; bus[1] += pr[p] * s.dry[p];
        bus[4] += pl[p] * s.var[p]; bus[5] += pr[p] * s.var[p];
        bus[6] += pl[p] * s.rev[p]; bus[7] += pr[p] * s.rev[p];
    }
    fx.run(bus, l, r);
    // Ours: whatever happens upstream, the host never gets a NaN. gain stands in for the analogue volume
    // pot, after the DAC.
    if (!std::isfinite(l) || !std::isfinite(r)) l = r = 0.0;
    outL[i] = (float)(l * gain); outR[i] = (float)(r * gain);
    const bool held = outL[i] == lastL && outR[i] == lastR;
    lastL = outL[i]; lastR = outR[i];
    if (any || !held) { quiet = 0; asleep = false; } else if (++quiet >= SR) asleep = true;
}
