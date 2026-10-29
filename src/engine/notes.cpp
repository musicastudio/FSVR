// engine/notes.cpp - note on and off, operator setup, and the 192 Hz tick. KNOWN: rewritten from the disassembly.
#include "engine/synth.h"

void Synth::init_system() {
    memset(sys, 0, sizeof sys);
    sys[0] = 64; sys[6] = 64; sys[8] = 0; sys[9] = 0; sys[0x0E] = 0;
    sys[0x10] = sys[0x12] = sys[0x13] = sys[0x14] = sys[0x15] = 1;
    static const uint8_t cc[12] = {16, 17, 18, 19, 20, 21, 22, 13, 4, 2, 80, 81};   // KN1-4 MC1-4 FC BC Formant FM
    memcpy(sys + 0x16, cc, 12);
    sys[0x47] = 0; sys[0x48] = 4; sys[0x49] = 0; sys[0x4A] = 0;
}

// FUN_0002087e: the four filter switches are latched, and a change stops every channel. From then on the
// parts with a filter play on the sixteen channels the filter chip is wired to. Ours: the wide filter and the
// polyphony (docs/Differences.md) latch here too, so a change of either stops every note the same way.
void Synth::filter_latch() {
    int sw = 0;
    for (int p = 0; p < 4; p++) sw |= (perf.part[p].p[7] & 1) << p;
    if (sw == fltSw && filter32 == fltWide && poly == nchan) return;
    all_off();
    fltSw = sw; fltWide = filter32; nchan = poly;
    flt.reset();
    nextChan[0] = nextChan[1] = 0; nextChan[2] = FILTER_CHANNELS;
    for (int p = 0; p < 4; p++) partChan[p] = sw && !fltWide && !(sw >> p & 1) ? FILTER_CHANNELS : 0;
}

// The filter host reads the voice and the part record. Destinations 21 to 26 edit the record, as the firmware
// has them do (FUN_00017192 and the five after it), by writing the part's bytes (ctrl_write); 42, 44 and 45
// are offsets the host keeps itself.
void Synth::filter_sync(int part) {
    const Part& pt = perf.part[part];
    uint8_t rec[fsvr::vop3host::FilterHost::PART_BYTES]; memcpy(rec, pt.p, sizeof rec);
    flt.sync(flt_banks(), part, pt.voice.raw, rec, ctrl_offset(part, 42), ctrl_offset(part, 44), ctrl_offset(part, 45));
}

void Synth::note_on(int part, int note, int vel) {
    filter_latch();
    Part& pt = perf.part[part]; const Voice& V = pt.voice;
    int lo = pt.p[0x0F], hi = pt.p[0x10];   // FUN_00010B48: high below low plays all but the gap
    if (hi < lo ? (note > hi && note < lo) : (note > hi || note < lo)) return;
    lo = pt.p[0x2A]; hi = pt.p[0x2B];
    if (hi < lo ? (vel > hi && vel < lo) : (vel > hi || vel < lo)) return;
    vel = clampi(((pt.p[0x0C] * VELCURVES[clampi(velCurve(), 0, 4) * 128 + vel]) >> 6) + (pt.p[0x0D] - 0x40) * 2, 1, 127);
    bool mono = pt.p[5] == 0;
    // mono: remember held notes, choose the sounding note by priority
    if (mono) {
        if (pt.nheld < 32) pt.held[pt.nheld++] = note;
        int prio = pt.p[6] & 3, target = note;
        if (prio == 1) { target = -1; for (int i = 0; i < pt.nheld; i++) target = std::max(target, pt.held[i]); }
        else if (prio == 2) { target = 128; for (int i = 0; i < pt.nheld; i++) target = std::min(target, pt.held[i]); }
        else if (prio == 3) target = pt.held[0];
        Chan* cur = nullptr; for (auto& c : chans()) if (c.active && c.part == part && c.held) cur = &c;
        if (cur && pt.nheld > 1) {          // legato: retune the sounding channel (FUN_000119d0), no retrigger
            if (target != cur->note) retune(*cur, target);
            return;
        }
        note = target;
    }
    // FUN_0000f7dc, measured against the unit (FS1R.unlock 2026-09-25-7). Three things, none of which
    // the old "first free, else oldest" did:
    //  1. allocation is ROUND ROBIN from the last channel allocated, not the lowest free one. Filling an
    //     empty machine with 32 notes puts them on channels 1..31 then 0, which is what the unit did. Free
    //     is a channel no key holds (owned, synth.h): a release still sounding does not keep it.
    //  2. when nothing is free, the part to take from is the one furthest over its own note reserve.
    //     The reserves cannot oversubscribe the 32 channels: writing 32 to part 2 with parts 3 and 4
    //     holding 8 each read back as 16, i.e. 32 - (0 + 8 + 8), so a part's effective reserve is what
    //     is left after the others. (FUN_00030796 clamps parameter writes against a per-parameter
    //     table; the sum rule is the reading that fits the one measured point, 2026-09-25-7.)
    //  3. within that part the walk starts after the part's own cursor and takes the first channel whose
    //     stored note is ABOVE the arriving one; failing that, the lowest stored note it saw.
    // While a part has its filter on, FUN_000112c0 walks half the channels instead: 0 to 15 for a part with
    // a filter (FUN_0000fa5c), 16 to 31 for one without (FUN_0000fcfa), each with a cursor of its own.
    // OPEN: the two halves' walks are taken to follow the whole walk's three rules; only that one was measured.
    // With fltWide (ours) every channel has a filter slot, so there are no halves: the whole walk, as with no filter.
    // Ours too: the walk is over the nchan channels in use rather than the unit's 32 (docs/Differences.md), the
    // halves being the chip's sixteen and the rest.
    const bool filtered = fltSw >> part & 1, split = fltSw && !fltWide;
    const int first = split && !filtered ? FILTER_CHANNELS : 0, count = !split ? nchan : filtered ? FILTER_CHANNELS : nchan - FILTER_CHANNELS;
    int& cursor = nextChan[!split ? 0 : filtered ? 1 : 2];
    auto at = [&](int from, int k) { return first + ((from - first + k) % count + count) % count; };
    Chan* c = nullptr;
    // FUN_000112c0: a mono part with no key down takes back the first channel it last owned, without moving a
    // cursor; the walk below is only for a part that owns none. MEASURED 2026-10-26 (FS1R.unlock 2026-10-26-open,
    // takes 3 and 4): a key-sync-off sine on a mono part starts each strike where one generator ran on to.
    if (mono) for (int k = 0; k < count && !c; k++) { Chan& x = ch[first + k]; if (x.owner == part && !owned(x)) c = &x; }
    const bool reused = c != nullptr;
    for (int k = 1; k <= count && !c; k++) { Chan& x = ch[at(cursor, k)]; if (!owned(x)) { c = &x; break; } }
    if (!c) {
        int best = part, excess = -1;
        for (int k = 0; k < 4; k++) {
            int p = (part + k) & 3, others = 0, n = 0;
            if (split && (fltSw >> p & 1) != (int)filtered) continue;   // its notes are in the other half
            for (int q = 0; q < 4; q++) if (q != p) others += perf.part[q].p[0x00];
            // Ours: past the unit's 32 a reserve is the part's share of those 32, scaled to the channels in use,
            // so a performance's reserves mean the same at every polyphony (docs/Differences.md).
            int res = clampi(perf.part[p].p[0x00], 0, std::max(0, NCHAN - others)) * nchan / NCHAN;
            for (auto& x : chans()) if (owned(x) && x.part == p) n++;
            if (n > res && n - res > excess) { excess = n - res; best = p; }
        }
        int thr = note, fb = -1;
        for (int k = 1; k <= count; k++) {
            int i = at(partChan[best], k);
            if (!owned(ch[i]) || ch[i].part != best) continue;
            if (thr < ch[i].note) { c = &ch[i]; break; }
            if (fb >= 0) { c = &ch[fb]; break; }
            fb = i; thr = ch[i].note;
        }
        if (!c) c = &ch[fb >= 0 ? fb : first];
    }
    const int slot = int(c - ch);
    if (!reused) { cursor = slot; partChan[part] = slot; }
    // FUN_000112c0: the filter slot is silenced before the note takes it. With fltWide an unfiltered note can
    // take a channel a filtered one rang out on, so its slot is silenced too.
    if (filtered || (fltSw && fltWide)) flt.host(slot).kill(slot & 15);
    Chan& C = *c; bool sync = V.lfo1sync != 0; uint32_t keepPhase = C.lfoPhase;
    // FUN_00023000: the firmware damps the channel it is about to take, it does not silence it. The mask
    // it is called with comes out of the one-hot table at 0x35B260 indexed by the allocated channel, and
    // the call sets that channel's EG stage word to 4 (release) and writes register 0xFC/FD. So a note
    // landing on a channel that is still sounding fades the old waveform out over cal::DAMP_MS. Zeroing
    // the struct instead put a step the size of the old note's own output into the output between two
    // samples, which is a click the unit does not make.
    double damp = C.active ? C.last + C.damp : 0.0;
    // The channel's phase generators never stop: an operator with key sync off starts the note where its
    // generator has run to since the channel's last note, at that note's rate. MEASURED 2026-10-26 (FS1R.unlock
    // 2026-10-26-open, take 3): a 1 Hz sine, key sync off, struck every 1.5 s on a mono part starts each strike
    // half a turn on from the last (0.544, 0.038, 0.531, 0.025 ...); on a poly part, a channel each strike, scattered.
    // A channel no note has used yet starts at a drawn phase: what it ran since power-on is not known.
    uint32_t run[8];
    for (int o = 0; o < 8; o++) run[o] = C.age ? C.op[o].phase + op_inc32(C.op[o].inc) * (uint32_t)(now - C.ran) : V.v[o].keysync ? 0u : draw();
    C = Chan(); C.active = true; C.part = part; C.owner = part; C.note = note; C.vel = vel; C.held = true; C.age = ++clock; C.chn = noteChn;
    C.damp = damp; C.filtered = filtered;
    C.lfoPhase = sync ? 0 : keepPhase;
    C.panBase = pt.p[0x0E] ? pt.p[0x0E] : (int)(draw() % 128);       // part pan 0 = random per note; the rest is the table index
    compute_pitch(C, pt, note);
    // portamento start (FUN_000124fe)
    int porta = pt.p[0x24]; C.portaTarget = C.pitchNote; C.portaCur = C.pitchNote;
    if ((porta & 1) && pt.p[0x25] && pt.lastPitch >= 0 && ((porta & 2) || (mono && pt.nheld > 1))) C.portaCur = pt.lastPitch;
    C.portaOn = (porta & 1) && pt.p[0x25]; C.portaRate = PEGTIME[clampi((pt.p[0x25] * 100) >> 7, 0, 99)];
    pt.lastPitch = C.pitchNote;
    setup_ops(C, pt, vel);
    setup_peg(C, pt, vel);
    // Fseq trigger (FUN_0000fffa): first note only, or every note
    if (fseq.valid && fseqPart == part && ((perf.c[0x24] & 1) || !anyOther(C))) fseq_start(vel);
    // The filter slot starts before the tone generator's registers are written: its modulation word
    // (FUN_0002a47c), then the note (FUN_0000b0e8), which takes the note after its shifts and the curved velocity.
    if (filtered) { filter_sync(part); auto& h = flt.host(slot); h.setSlotCutoffMod(slot & 15, filter_mod(C)); h.noteOn(slot & 15, part, C.noteP, vel);
        auto& L = flt.bank[slot >> 4].lanes; L.cutoff_s[slot & 15] = NAN; L.glide[slot & 15] = cutoffGlide ? CUTOFF_GLIDE_K : 0; }
    refresh_regs(C, pt);
    if (getenv("FS1R_DEBUG")) {
        printf("note %d vel %d -> note' %d pitch %d keyfact %d C0 %d\n", note, vel, C.noteP, C.pitchNote, C.keyfact, C.regC0);
        for (int o = 0; o < 8; o++) printf("  op%d lvl %3d ulvl %3d egb %3d freq %5d ufreq %5d frmt %5d bw %2d egL %2d %2d %2d %2d egR %2d %2d %2d %2d hold %d\n", o + 1, C.levelOff[o], C.ulevelOff[o], C.egbias[o], C.freqWord[o], C.ufreqWord[o], C.frmtWord[o], C.bwReg[o], C.egL[o][0], C.egL[o][1], C.egL[o][2], C.egL[o][3], C.egR[o][0], C.egR[o][1], C.egR[o][2], C.egR[o][3], C.egHold[o]);
        printf("  peg lvl %d %d %d %d %d rate %d %d %d %d %d velP %d\n", C.pegLvl[0], C.pegLvl[1], C.pegLvl[2], C.pegLvl[3], C.pegLvl[4], C.pegRt[0], C.pegRt[1], C.pegRt[2], C.pegRt[3], C.pegRt[4], C.velP);
        int sp = eb86(clampi(V.lfo1speed + pt.p[0x15] - 64, 0, 99)); uint32_t inc = (sp == 0 ? 0xB : sp * (sp < 0xA0 ? 0xB : 0xB + ((sp - 0xA0) >> 2))) << 1;
        int q = 99 - clampi(V.lfo1delay + pt.p[0x17] - 64, 0, 99); uint32_t dinc = ((16 + (q & 15)) << 10) >> (7 - (q >> 4));
        printf("  lfo wave %d speed %d -> inc %u (%.2f Hz) delay %d -> inc %u (%.2f s) pmd %d amd %d fmd %d porta %d rate %d\n", V.lfo1wave, V.lfo1speed, inc, inc * TICK_HZ / 65536.0, V.lfo1delay, dinc, 65536.0 / (dinc ? dinc : 1) / TICK_HZ, V.pmd, V.amd, V.fmd, C.portaOn, C.portaRate);
    }
    for (int o = 0; o < 8; o++) {
        OpState& s = C.op[o]; const OpV& v = V.v[o]; const OpU& u = V.u[o];
        s.eg.start(C.egL[o], C.egR[o], C.egHold[o], eg_ratescale(v.tscale, C.regC0));
        s.feg.start(v.fegInit, v.fegAtt, v.fegAttT, v.fegDecT);
        s.phase = v.keysync ? 0u : run[o];   // 2^32 to a cycle
        s.ueg.start(C.uegL[o], C.uegR[o], C.uegHold[o], eg_ratescale(u.tscale, C.regC0));
        s.ufeg.start(u.fegInit, u.fegAtt, u.fegAttT, u.fegDecT);
        s.rng = 0x9E3779B9u * (o + 1) ^ clock; if (!s.rng) s.rng = 1;
        s.attS = -1e9;                    // the level glide starts at the new note's own level
    }
}

void Synth::compute_pitch(Chan& C, const Part& pt, int note) {
    int n = clampi(note + (sysNoteShift() & 0x7F) - 0x40, 0, 127);
    n = clampi(n + (perf.c[0x12] & 0x3F) - 24, 0, 127);
    n = clampi(n + (pt.p[8] & 0x3F) - 24, 0, 127);
    n = clampi(n + (pt.voice.raw[0x1E] & 0x3F) - 24, 0, 127);
    C.noteP = n; C.keyfact = KEYFACT[n] >> 2;
    int pitch = NOTETAB[n];
    // The part's detune scales by the band of the key played (0x0102A402, before any note shift), not of the
    // shifted note: Teck Hook's parts an octave and two down beat as the unit's do only so.
    int d = pt.p[9] - 0x40;
    if (d) { int band = (note >> 3) & 0xF; int prod = std::abs(d) * (15 - band); if (!prod) prod = 1; pitch += (d < 0 ? -prod : prod) / 15; }
    pitch += (sysTune() & 0x7F) - 0x40;
    C.pitchNote = pitch;
}

// A voice parameter change reaches the notes already sounding, as the unit's own edit buffer does:
// the per-channel words are rebuilt from the voice at the note's own pitch and velocity. The EGs in
// flight keep the segments they started with, and the filter EG likewise; everything the tick
// reads through refresh_regs (frequency, level, form, bandwidth, detune, algorithm, LFOs, filter)
// moves at once.
void Synth::voice_changed(int part) {
    const Part& pt = perf.part[part];
    for (auto& C : ch) if (C.active && C.part == part) { setup_ops(C, pt, C.vel); refresh_regs(C, pt); }
}

void Synth::retune(Chan& C, int note) {
    const Part& pt = perf.part[C.part];
    C.note = note; compute_pitch(C, pt, note);
    C.portaTarget = C.pitchNote; if (!C.portaOn) C.portaCur = C.pitchNote;
    perf.part[C.part].lastPitch = C.pitchNote;
    setup_ops(C, pt, C.vel); refresh_regs(C, pt);
}

void Synth::setup_ops(Chan& C, const Part& pt, int vel) {
    const Voice& V = pt.voice; int n = C.noteP; int pm = C.pitchNote - 0x53AA; int part = C.part;
    int band = (C.pitchNote >> 8) + 10; band = band < 0x50 ? 0 : band < 0x70 ? ((band ^ 0x10) & 0x1F) : 0x1F;
    int fv = (fseq.valid && fseqPart == part) ? V.fseqV : 0, fu = (fseq.valid && fseqPart == part) ? V.fseqU : 0;
    for (int o = 0; o < 8; o++) {
        const OpV& v = V.v[o]; const OpU& u = V.u[o];
        C.fseqOp[o] = (fv >> o) & 1; C.fseqUOp[o] = (fu >> o) & 1;
        // voiced level offset: velocity + level + key scaling (0.375 dB units)
        int ks;
        {
            int base = LEVTAB[std::min(99, v.level)];
            int dist = keygroup(n) - keygroup(v.bp + 21) + 1;
            int att;
            if (dist <= 0) { const unsigned char* t = (v.lc == 1 || v.lc == 2) ? KSEXP : KSLIN; int x = std::min(127, (t[std::min(39, -dist)] * eb86(v.ld)) >> 8); att = (v.lc == 2 || v.lc == 3) ? base - x : base + x; }
            else { const unsigned char* t = (v.rc == 1 || v.rc == 2) ? KSEXP : KSLIN; int x = std::min(127, (t[std::min(39, dist)] * eb86(v.rd)) >> 8); att = (v.rc == 2 || v.rc == 3) ? base - x : base + x; }
            ks = clampi(att, 0, 127) << 1;
        }
        C.levelOff[o] = std::min(255, vel_att(v.amsb, vel) + ks);
        // unvoiced: 2*LEVTAB + velocity, then level key scaling
        {
            int a = std::min(255, LEVTAB[std::min(99, u.level)] * 2 + vel_att(u.amsb, vel));
            a += -(u.lks * 64 * (n - 60)) >> 8;
            C.ulevelOff[o] = clampi(a, 0, 255);
        }
        // frequency words. The bias the controller matrix adds to them is not here: it moves while
        // the note sounds, and refresh_bias rebuilds it every tick.
        if (v.form == 7 || v.fixed) C.freqWord[o] = 8 * (v.coarse * 128 + v.fine) + 0x28ED + keytrack(v.notescale, pm) + fvs_term(v.fmsb, vel);
        // MEASURED 2026-10-23: a fine of 100 or more plays as the next coarse step at fine 0 (coarse 0 at 100,
        // 110 and 127 is 1.0, coarse 1 at 100 is 2.0, coarse 2 at 100 is 3.0). The factory Hook under Teck Hook,
        // Tech Lead and 25 other operators store fine 100; held at 99 they sat 8 cents under and beat.
        else if (v.fine >= 100) C.freqWord[o] = 0x1243 + COARSE[std::min(31, v.coarse + 1)];
        else C.freqWord[o] = 0x1243 + COARSE[v.coarse] + FINE[v.fine];
        C.ufreqWord[o] = std::min(0x7F00, ((u.coarse & 0x1F) * 256 + u.fine * 2) * 4 + 0x28ED + keytrack(u.notescale, pm) + fvs_term(u.fmsb, vel));
        // Detune, in pitch word units, from the ROM's own key scaled table. MEASURED: the chip does
        // the same thing to a ratio operator's register 0x80 that the CPU does to a formant
        // operator's transpose word, so both read FRMDET and the amount shrinks as the note rises.
        {
            int d = v.detune; int dd = d < 15 ? (~d) : d - 15; int idx = (dd & 0x1F) * 32 + band;
            C.detW[o] = (idx & 0x200) ? -FRMDET[idx & 0x1FF] : FRMDET[idx & 0x1FF];
        }
        // formant transpose word (register 0x230): 0x1243 + TRANS + that detune for frmt ops, else the raw byte 6
        if (v.form == 7) C.frmtWord[o] = 0x1243 + TRANS[clampi(v.transpose + 24, 0, 48)] + C.detW[o];
        else C.frmtWord[o] = v.bw;
        // EG registers: levels LEVTAB >> 1, rates ((99-T)*0xA4)>>8, part EG offsets on attack/decay/release (assumed T1/T2/T4)
        // FUN_00019414 walks the part's EG bytes 0x1A, 0x1B, 0x1B, 0x1C against voice times T1-T4,
        // so the decay offset reaches both T2 and T3, not T2 alone.
        int atk = pt.p[0x1A], dec = pt.p[0x1B], rel = pt.p[0x1C];
        int offs[4] = {atk - 64, dec - 64, dec - 64, rel - 64};
        for (int i = 0; i < 4; i++) {
            C.egL[o][i] = LEVTAB[std::min(99, v.L[i])] >> 1; C.egR[o][i] = egrate(v.T[i] + offs[i]);
            C.uegL[o][i] = LEVTAB[std::min(99, u.L[i])] >> 1; C.uegR[o][i] = egrate(u.T[i] + offs[i]);
        }
        C.egHold[o] = v.hold; C.uegHold[o] = u.hold;
    }
}

void Synth::setup_peg(Chan& C, const Part& pt, int vel) {
    const Voice& V = pt.voice;
    C.velP = V.pegVel ? vel + 1 : 128;        // firmware overrides the graded PEGVEL table with this (see docs)
    int Lp[5], Tp[5];
    for (int i = 0; i < 5; i++) Lp[i] = V.pegL[i];
    Lp[0] = clampi(Lp[0] + pt.p[0x20] - 64, 0, 100);
    Lp[4] = clampi(Lp[4] + pt.p[0x22] - 64, 0, 100);
    Tp[0] = 255; for (int i = 0; i < 4; i++) Tp[i + 1] = V.pegT[i];
    Tp[1] = clampi(Tp[1] + pt.p[0x21] - 64, 0, 99);
    Tp[4] = clampi(Tp[4] + pt.p[0x23] - 64, 0, 99);
    for (int i = 0; i < 5; i++) {
        int w = ((int)PEGLVL[clampi(Lp[i], 0, 100)] - 128) << 7;
        w = (w * C.velP) >> 7; if (V.pegRange) w >>= V.pegRange + 1;
        C.pegLvl[i] = w;
        int r = i ? PEGTIME[clampi(Tp[i], 0, 99)] : 255;
        C.pegRt[i] = r == 255 ? 255 : std::min(254, ((r * (C.velP + 1)) >> 7) + std::min(255, V.pegTscale * C.keyfact));
    }
    if (C.pegRt[1] == 255) { C.pegStage = 1; C.pegCur = C.pegLvl[1]; C.pegTarget = C.pegLvl[2]; C.pegRate = C.pegRt[2]; }
    else { C.pegStage = 0; C.pegCur = C.pegLvl[0]; C.pegTarget = C.pegLvl[1]; C.pegRate = C.pegRt[1]; }
}

void Synth::note_off(int part, int note) {
    Part& pt = perf.part[part];
    if (pt.p[5] == 0) {                       // mono
        for (int i = 0; i < pt.nheld; i++) if (pt.held[i] == note) { memmove(pt.held + i, pt.held + i + 1, (pt.nheld - i - 1) * sizeof(int)); pt.nheld--; break; }
        Chan* cur = nullptr; for (auto& c : chans()) if (c.active && c.part == part && c.held) cur = &c;
        if (!cur) return;
        if (pt.nheld > 0) {                   // another key still down: go back to it (legato)
            int prio = pt.p[6] & 3, target = pt.held[pt.nheld - 1];
            if (prio == 1) { target = -1; for (int i = 0; i < pt.nheld; i++) target = std::max(target, pt.held[i]); }
            else if (prio == 2) { target = 128; for (int i = 0; i < pt.nheld; i++) target = std::min(target, pt.held[i]); }
            else if (prio == 3) target = pt.held[0];
            if (target != cur->note) retune(*cur, target);
            return;
        }
        cur->held = false; if (pt.sustain && (pt.p[0x2D] & 1)) { cur->sustained = true; return; }
        release(*cur); return;
    }
    for (auto& c : chans()) if (c.active && c.part == part && c.note == note && c.held) {
        c.held = false;
        if (pt.sustain && (pt.p[0x2D] & 1)) { c.sustained = true; continue; }
        release(c);
    }
}

void Synth::release(Chan& C) {
    for (auto& s : C.op) { s.eg.release(); s.ueg.release(); }
    const int slot = int(&C - ch);
    if (C.filtered) flt.host(slot).noteOff(slot & 15);   // FUN_00010a52
    C.pegStage = 4; C.pegTarget = C.pegLvl[4]; C.pegRate = C.pegRt[4];
    fseq_keys();
}

// FUN_00010384(0): every channel the part holds is freed and damped (FUN_00023000), its filter slot killed,
// and the part's held keys forgotten, so no key up is owed to a part that may no longer hear it.
void Synth::silence_part(int part) {
    for (auto& C : chans()) {
        if (!owned(C) || C.part != part) continue;
        const int slot = int(&C - ch);
        if (C.filtered) flt.host(slot).kill(slot & 15);
        const double damp = C.last + C.damp;
        C = Chan(); C.part = part; C.damp = damp; C.active = damp != 0.0;
    }
    Part& pt = perf.part[part];
    pt.nheld = 0; pt.lastPitch = 0x53AA;
    fseq_keys();
}

// The handlers the firmware runs after a part byte is written, from the part block of its table at flash 0x3E460.
// Each one that changes who hears what, or how notes are held, silences first.
void Synth::part_written(int part, int al, int was) {
    const Part& pt = perf.part[part];
    switch (al) {
    case 0x00: for (int p = 0; p < 4; p++) silence_part(p); break;   // Note Reserve, FUN_00020878
    // Voice Bank: FUN_00021928 only records it, and the mask is rebuilt by the voice select that follows on
    // the panel (setupPartFromMode, FUN_00020644). rcv() reads the bank live, so off silences here.
    case 0x01: if (was && !pt.p[1]) silence_part(part); break;
    case 0x02:                                                        // Program, FUN_0001FE62
    case 0x03: case 0x04:                                             // Rcv Max, Rcv Ch, FUN_00020644
    case 0x05: case 0x06: silence_part(part); break;                  // Mono/Poly FUN_000204FE, Priority FUN_00020872
    }
}

void Synth::lfo_tick(Chan& C, const Part& pt) {
    const Voice& V = pt.voice; int part = C.part;
    int dly = clampi(V.lfo1delay + pt.p[0x17] - 64, 0, 99), q = 99 - dly;
    uint32_t dinc = ((16 + (q & 15)) << 10) >> (7 - (q >> 4));
    C.lfoDelay = std::min<uint32_t>(0xFFFF, C.lfoDelay + dinc);
    if (C.lfoDelay == 0xFFFF) C.lfoFade = std::min<uint32_t>(0xFFFF, C.lfoFade + dinc);
    int sp = eb86(clampi(V.lfo1speed + pt.p[0x15] - 64, 0, 99)) + clampi(ctrl_offset(part, 43) * 2, -255, 255);
    sp = clampi(sp, 0, 255);
    uint32_t inc = sp == 0 ? 0xB : sp * (sp < 0xA0 ? 0xB : 0xB + ((sp - 0xA0) >> 2)); inc = (inc & 0xFFFF) << 1;
    uint32_t ph = C.lfoPhase + inc; bool wrapped = ph > 0xFFFF; C.lfoPhase = ph & 0xFFFF;
    int hi = C.lfoPhase >> 8, val;
    switch (V.lfo1wave) {
    case 1: val = (int8_t)(~hi); break;
    case 2: val = (int8_t)hi; break;
    case 3: val = (hi & 0x80) ? -128 : 127; break;
    case 4: { int idx = hi & 0x3F; if (hi & 0x40) idx ^= 0x3F; int s = SINE64[idx]; val = (hi & 0x80) ? (int8_t)(~s) : s; break; }
    case 5: if (wrapped) { C.lfoSH = SHTAB[((C.lfoSH & 0xFF) * 0xB3 + (int)(draw() & 0xFF)) & 0x7FF]; } val = C.lfoSH; break;
    default: { uint32_t t = (C.lfoPhase << 1); if (t > 0xFFFF) t = ~t & 0xFFFF; val = (int)((t >> 8) & 0xFF) - 0x80; }
    }
    C.lfoVal = val;
}

// The unit applies a note-on at a control tick, so the pitch EG's first step comes one to two ticks after the
// note sounds, never within the first. MEASURED 2026-10-26 (FS1R.unlock 2026-10-26-open, take 20): sixteen strikes
// of a stepping pitch EG at note 84 put the first step 5.4 to 11.2 ms after the onset, where the engine's tick,
// free against the note, put it 0 to 5.2. Teck Hook's voice (PrA Hook, a 1.5 semitone pitch EG drop into a 0 Hz
// formant) played 1 to 5 dB under the unit depending on where the tick fell, and its level follows the unit's
// within 0.6 dB with this (takes 6 and 10).
void Synth::peg_tick(Chan& C) {
    if (C.pegWait) { C.pegWait = false; return; }
    int s = C.pegStage; if (s == 3 || s == 5) return;
    if (s > 5) { C.pegStage = 3; return; }
    if (C.pegRate < 255) {
        if (C.pegCur < C.pegTarget) C.pegCur = std::min(C.pegTarget, C.pegCur + C.pegRate);
        else if (C.pegCur > C.pegTarget) C.pegCur = std::max(C.pegTarget, C.pegCur - C.pegRate);
        if (C.pegCur != C.pegTarget) return;
    } else C.pegCur = C.pegTarget;
    C.pegStage = s + 1;                       // next segment: rate T[s+2], target L[s+2] (stage 3 holds L3 until key off)
    if (s + 1 < 5) { C.pegRate = C.pegRt[std::min(4, s + 2)]; C.pegTarget = C.pegLvl[std::min(4, s + 2)]; }
}

void Synth::porta_tick(Chan& C) {
    if (!C.portaOn || !C.portaRate) { C.portaCur = C.portaTarget; return; }
    int d = C.portaTarget - C.portaCur; if (!d) return;
    if (d < 0) { int n = C.portaCur - (((-d) >> 10) + 1) * C.portaRate; C.portaCur = std::max(C.portaTarget, n); }
    else { int n = C.portaCur + ((d >> 10) + 1) * C.portaRate; C.portaCur = std::min(C.portaTarget, n); }
}

void Synth::refresh_regs(Chan& C, const Part& pt) {
    const Voice& V = pt.voice; int part = C.part;
    voice_ctrl(C, pt); refresh_bias(C, pt);
    int fseqPitch = 0;
    if (fseq_on(part) && !(perf.c[0x23] & 1)) {
        const uint8_t* f = fseq.frame[fseqStep];
        fseqPitch = (f[0] * 256 + f[1] * 2) - (NOTETAB[fseq.noteAssign & 0x7F] + 0x1243) + (fseq.tuning - 63);
    }
    C.regPitch = C.portaCur + (C.pegCur >> 2) + bend_word(pt, C.chn) + fseqPitch;
    C.regC0 = (C.pitchNote >> 8) + 10;
    int fade = C.lfoFade >> 8;
    int pmd = clampi(eb86(clampi(V.pmd + pt.p[0x16] - 64, 0, 99)) + ctrl_offset(part, 39) * 2, 0, 255);   // ctrlDest_39 doubles it
    C.regPM = clampi((C.lfoVal * ((pmd * fade) >> 8 & 0xFF)) >> 4, -0x7FF, 0x7FF);
    int fmd = clampi(eb86(V.fmd) + ctrl_offset(part, 41), 0, 255);
    C.regFM = clampi((C.lfoVal * ((fmd * fade) >> 8 & 0xFF)) >> 4, -0x7FF, 0x7FF);
    int amc = ctrl_offset(part, 40);
    int amd = clampi(eb86(V.amd) + amc, 0, 255), fadeA = clampi(fade + amc, 0, 255);
    C.regAM = EGBIAS[(((amd * fadeA) >> 8) * ((127 - C.lfoVal) & 0xFF)) >> 8];
    int lvVel = 0x80;                                     // Fseq level velocity multiplier (FUN_000125f8)
    if (fseqPart == part && perf.c[0x27] != 0x40) {
        int s = perf.c[0x27]; int mul;
        if (s & 0x40) mul = ((s & 0x3F) + 1) * VELW[VELCURVE[fseqVel] & 0x7F]; else mul = ((~s) & 0x3F) * VELW[127 - (VELCURVE[fseqVel] & 0x7F)];
        lvVel = (~(mul >> 6)) & 0xFF;
    }
    // No formant tracking term: FUN_000127FC applies the whole key-to-Fseq difference to the channel
    // pitch (the fseqPitch above, whose constants it confirms exactly) and nothing shifts the frame's
    // formant frequencies. Formants staying put while the fundamental moves is the point of the format.
    for (int o = 0; o < 8; o++) {
        int l = C.levelOff[o], ul = C.ulevelOff[o];
        if (fseq_on(part)) {
            const uint8_t* f = fseq.frame[fseqStep]; int t = V.v[o].fseqtrk;
            if (C.fseqOp[o]) { l = f[0x12 + t] << 1; if (lvVel != 0x80) l = (~(((~l) & 0xFF) * lvVel >> 8)) & 0xFF; C.fqWord[o] = f[2 + t] * 256 + f[0xA + t] * 2 + C.fbW[o]; }
            if (C.fseqUOp[o]) { ul = f[0x2A + t] << 1; if (lvVel != 0x80) ul = (~(((~ul) & 0xFF) * lvVel >> 8)) & 0xFF; C.fquWord[o] = f[0x1A + t] * 256 + f[0x22 + t] * 2 + C.ufbW[o]; }
        }
        C.regLevel[o] = clampi(l + C.egbias[o] + C.vcLvl[o][0], 0, 255);
        // Ours (docs/Differences.md): op byte 0x1C, reserved on the unit, is a key-synced oscillator's initial
        // phase, 0 to 127 for 0 to 360 * 127 / 128 degrees. Read here, so an edit moves a sounding note too.
        C.phOff[o] = V.v[o].keysync ? V.raw[112 + o * 62 + 0x1C] / 128.0 : 0.0;
        C.regULevel[o] = clampi(ul + C.uegbias[o] + C.vcLvl[o][1], 0, 255);
    }
    int vAtt, uAtt; part_levels(part, vAtt, uAtt);
    C.partV = db2lin(-LEVEL_DB * vAtt); C.partU = db2lin(-LEVEL_DB * uAtt);
    refresh_pan(C, pt);
}

void Synth::voice_ctrl(Chan& C, const Part& pt) {
    // FUN_00017454 walks the ten control slots with amt = clamp(src' * dep' * 2 >> 7) (FUN_00016FF0, the
    // same bias-and-scale the controller sets use), src being the part's FORMANT or FM byte as
    // (v - 64) * 2 clamped to a signed byte. The handlers at flash 0x3DE04: "out" stores -2 * amt as a
    // level offset (FUN_00017A4A), "freq" stores amt << 5 as a frequency word offset (FUN_00017B02), and
    // "width" stores amt into the per-op bandwidth offset that FUN_0001F6BC then scales by the op's own
    // BWBIAS, exactly as destination 37 does (FUN_00017B6E). The engine had roughly a quarter, an eighth
    // and a quarter of those, and a cal constant for a scale that is a shift in the flash.
    memset(C.vcLvl, 0, sizeof C.vcLvl); memset(C.vcFreq, 0, sizeof C.vcFreq); memset(C.vcBw, 0, sizeof C.vcBw);
    const uint8_t* b = pt.voice.raw;
    int src[2] = {clampi((pt.p[0x1D] - 64) * 2, -128, 127), clampi((pt.p[0x1E] - 64) * 2, -128, 127)};
    for (int k = 0; k < 10; k++) {
        int d = b[k < 5 ? 0x40 + k : 0x4A + (k - 5)];
        int dep = (int)b[k < 5 ? 0x45 + k : 0x4F + (k - 5)] - 64;
        int dd = (d >> 4) & 3, v = (d >> 3) & 1, o = d & 7;
        if (dd < 1 || dd > 3) continue;
        int amt = clampi((ctrl_bias(src[k < 5 ? 0 : 1]) * ctrl_bias(dep) * 2) >> 7, -128, 127);
        if (dd == 1) C.vcLvl[o][v] += clampi(-2 * amt, -255, 255);
        else if (dd == 2) C.vcFreq[o][v] += amt << 5;
        else { const uint8_t* op = pt.voice.raw + 112 + o * 62; int bias = v ? (op[35 + 5] & 0xF) : ((op[4] >> 3) & 0xF); C.vcBw[o][v] += clampi((amt * BWBIAS[bias]) >> 3, -128, 127); }
    }
}

void Synth::refresh_pan(Chan& C, const Part& pt) {
    int base = pt.p[0x0E] ? pt.p[0x0E] : C.panBase;             // Panpot edits the part byte
    int idx = pan_index(base, pt.p[0x28], C.noteP);
    idx += ((C.lfoVal * std::max(1, eb86(clampi(pt.p[0x29], 0, 99))) * (int)(C.lfoFade >> 8)) >> 16) / 2; // pan LFO depth: image +0x1F is eb86(byte), 1 at 0 (FUN_00019362 case 0x29)
    if (perf.c[0x11]) idx += (perf.c[0x11] - 64) / 2;                              // performance pan
    idx = clampi(idx, 0, 127);
    C.panL = db2lin(-LEVEL_DB * PANL[idx]); C.panR = db2lin(-LEVEL_DB * PANR[idx]);
}

void Synth::tick() {
    ctlSerial++;
    double dt = 1.0 / TICK_HZ;
    fseq_tick(dt);
    for (int n = 0; n < nchan; n++) {
        Chan& C = ch[n];
        if (!C.active) continue;
        const Part& pt = perf.part[C.part];
        lfo_tick(C, pt); peg_tick(C); porta_tick(C); refresh_regs(C, pt);
        if (C.filtered) flt.host(n).setSlotCutoffMod(n & 15, filter_mod(C));   // FUN_0002c998
    }
    flt.flush(flt_banks());
}
