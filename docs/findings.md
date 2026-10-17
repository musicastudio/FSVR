# Findings

The research log. Newest first. One entry per thing learned, dated, with the evidence that settled it.

This file is history. It records what was found and how. It does not track open work, which lives on the board, and it does not hold the current state of what is known, which lives in [STATUS.md](../STATUS.md). When an entry here settles a constant, the constant itself belongs in `src/fs1r/chips/cal.h` with the entry named beside it.

Entries that have a dedicated working document (`aeg.md`, `skirt.md`, `noise.md`, `detune.md`, `formant.md`, `filter.md`) are summarized here and linked, not duplicated.

---

## 2026-10-17, the unit cuts the sound before a performance, and that is not what keeps it from popping

mkruselj reported loud pops switching performances in the plug-in, A032 against A033 and A071 against A072
by ear, and then A073 R.P.M. into A072 Mouth Pop. Reproduced offline through `Synth` with a note held: A071
Relaxxx into A072 clipped at full scale and rang on at 0.30 for a second against a signal of 0.055, A073
into A072 clipped from a signal of 0.037. Silence either side of the switch pops not at all, so it needs
signal in the section.

**Three mechanisms, each isolated by clearing one thing at the switch.**

- The **registers**. The host silences a window it is about to reprogram by loading the window's clear
  program (`EffectHost::clearWindow`), whose whole body is a run of writes zeroing that window's registers:
  `ins_m14.cpp` does `r[67]` to `r[86]`, `dly_m11.cpp` `r[87]` to `r[102]`, `var_m11.cpp` `r[103]` to
  `r[118]`. On the unit the chip runs it for as long as the upload takes; here `clearWindow` and
  `uploadWindow` are one synchronous call, so it never got a sample to run in and the next type took the old
  one's registers. Gate Reverb into Distortion returned a tone the window had been playing at 0.02 at full
  scale. Running it once, as the chip does, is the firmware's own behaviour and fixes that.
- The **cells**. `ins_m04`'s `m[44]` is the touch wah's envelope follower, and `ins_m03` (Early Reflections)
  leaves a delay tap there, which opens the wah into the overdrive behind it. The clear program refreshes
  only the window's input and LFO cells, so `m[42]` to `m[51]` came across. Zeroing exactly `m[42..44]` took
  A073 into A072 from 1.00 to 0.088.
- The **delay memory**. A new echo in the variation window read a second of the previous performance out of
  it. No chip program can write delay memory at all, because the chip addresses it by slot.

**All sound off before the load is the unit's behaviour, and it is now the engine's.** mkruselj, on his own
FS1R: a performance change stops the sound first, and it is all sound off rather than all notes off, the
notes cut where they are rather than released. `perf_from_bytes` now calls `all_off()` before any byte
changes. Until this, only the plug-in's preset browser did it (`loadFactoryPerf`), so a performance bulk
from a host, an editor or the MIDI in left notes sounding and they carried on through the new performance's
voices. No `FUN_` for it yet: reported off the unit, not read off the disassembly.

**It is not what keeps the unit from popping, which was the obvious next hypothesis.** If the unit silences
and then takes its time - and it is slow at performance changes - the state might decay or flush before the
new programs run. Measured by cutting the sound and then running the old programs on zero input for a while
before the load (`build/probe_time.cpp`, against the code as it was):

| wait | A073 into A072 | A071 into A072 | A020 Vox Morph into A021 |
|---|---|---|---|
| 0 ms | 1.00000 | 1.00000 | 0.26849 |
| 20 ms | 1.00000 | 1.00000 | 0.02708 |
| 100 ms | 1.00000 | 1.00000 | 0.01842 |
| 800 ms | 1.00000 | 1.00000 | 0.00599 |

The wait does nothing for either carried-state case, at any length. It does fix the third column, which is a
different thing: a load writes every mixer gain, return, pan and EQ word between one sample and the next,
and a gain step on audio still sounding is a click of its own. The wait fixes it only by removing the audio
to step. And the delay memory does not flush while waiting because the reverb tail keeps writing its own
output into it: `|ram|` stayed between 9 and 19 across all ten waits on A071, and the tail itself is loud,
peaking 0.15 to 0.63 during the silence.

**So either the unit pops on these pairs too, or a cell is not what we model it as.** A chip program reaches
its window's registers and no further, and nothing reaches delay memory, so the firmware has no way to
prevent either carryover and time does not do it. The one thing that would change the answer is the cell
model: if a cell on the chip is a latch the hardware reloads rather than a store that holds,
`m[44] = cell(sat((v29 * v32) + m[44]))` is not an integrator and this entry's second mechanism does not
exist. **The test on hardware:** hold a note under A073 R.P.M., switch to A072 Mouth Pop, and listen for a
clipped burst. Clean means the cell model is wrong and `ins_m04` is where to look.

**Answered the same day: mkruselj ran it on his own FS1R and it is clean.** So the cells do not survive a
window's reload on the unit and the model is wrong, by one of two routes: the chip clears its cell file when a
window is uploaded, or the real clear program zeroes more than the one here does. `m[44]` is in any case not
an envelope follower but the second integrator of the wah's state-variable filter, whose first is `r[74]` -
`v22` is the difference node, `r[74] += v29 * v22`, `m[44] += v29 * v31`, both integrators sharing the
frequency multiplier `v29` that `t[9]` also carries. The window's clear program zeroes `r[74]` and not
`m[44]`, which is half of each integrator pair and not a thing hardware would do, so this is a bug in
`src/effects/` rather than a divergence. It cannot be settled without the EPROM or the VOP3 interpreter,
neither of which is on this machine. The delay memory and the mixer step have not been tried on the unit.

FSVR clears all of it on a performance load and fades the section in over 8 ms for the mixer step, which is
recorded as ours in [Differences.md](Differences.md). Across all 766 switches between neighbouring factory
performances with a note held, sixty stepped more than twice above the level either side of them and nine
more than four times; with the state cleared, twenty-one and six; with the fade as well, none. The fade is
not made redundant by all sound off: with the engine cutting the sound and the fade cancelled it is
twenty-one and six again, because the effect tails are still live when the mixer words land.

---

## 2026-10-16, pitch bend keeps the LSB's top bits, and source PB is -128..127

Found by WouterVanNifterick in PR #43 (merged): source PB is a raw signed byte, -128..127, where the engine fed
it `msb - 64`, and the bend word drops the LSB. Re-read here off the Pitch Bend handler at 0x15B80 (no Ghidra
function; `tools/ghidra_disasm_sh2.py 0x15b80-0x15c48`), which agrees on both:

- the bend word at DAT_010293F4 is `((msb - 64) << 8 | lsb << 1) >> 4`, `(msb - 64) * 16 + (lsb >> 3)`. The
  engine's "the firmware keeps the MSB only" was wrong; `FUN_0002022C`'s `bend >> 3` gives the pitch the
  LSB's top bit, 256 steps.
- that `>> 3` goes back into the data byte with 0xE0 as the controller number and the handler jumps to
  `FUN_00014AD0`, where PB is in the raw mask 0x17C0: source PB is `(msb - 64) * 2 + (lsb >> 6)`, -128..127.
  At `msb - 64` a set on PB reached half its depth.
- with the LSB dropped `bend >> 3` topped out at 126, so `bend_word` never reached the firmware's 0x7F -> 0x80
  and an upward bend stopped at 126/128 of its range.

Both are fixed in `midi.cpp`; the engine self check covers them.

---
## 2026-10-16, the sum bus is cleared by t1 bit 1 of the algorithm word, not by a read: 65 and 80 fan out

`render_chan` cleared the sum bus whenever an operator read it, a guess from the first commit. On two algorithms that cut the routing short. 65 has operators 3 and 4 adding to the bus and 5, 6, 7 and 8 all reading it with nothing in between, and 80 has 1 and 2 adding and 3 to 8 reading; with a clearing read only the first reader was modulated, so 65 played 3 and 4 into 5 alone and 80 played 1 and 2 into 3 alone. The FS1R Algorithm Sheet (p.2), the MONTAGE Data List's Algorithm Chart (pp.172-173) and the Montage plugin's own table all draw 3 and 4 into each of 5 to 8 and 1 and 2 into each of 3 to 8.

The table says where the bus does clear. t1 bit 1, which `algorithms.h` listed as unknown and which reaches the chip as bit 2 of the low algorithm byte, sits exactly where a bus that a read leaves alone needs clearing. Every entry that adds to the bus has it at or before its first add; thirteen set it again on the operator that starts a second sum after the first was read (45, 47, 48, 50, 51, 53, 62, 64, 70, 71, 72, 77, 83); none sets it while an add waits for its reader; the six without it never touch the bus. With a clearing read every one of those thirteen bits did nothing. In six of them one operator reads the first sum, sets the bit and adds, so the clear falls between its read and its add, which is where `render_chan` now does it. Read this way the 88 route as the sheet draws them, and 65 and 80 are the only two that change.

It reaches two ROM voices: native 104 Saws2 (65), where operators 6, 7 and 8 played as plain sines, and native 132 Mukkuri (80), where five of the six formant carriers played unmodulated; and through them ROM performances 118 Mukkuri and 228 OB Pad. The self check now asserts the table's pattern and renders a later reader on 65 and on 80 with one modulator at full level.

**Not measured.** It is the table's reading, INFERRED like the rest of the chip. A capture would settle it: algorithm 80, operator 1 the only modulator at level 99 and operator 4 the only sounding carrier. A clearing read leaves operator 4 a plain sine. `docs/ymp706_registers.md` has the working.

## 2026-10-15, session 41 (cont.): Vokodrone's flanger depth matches; the 4.3x was the probe, the DRAM word is wider than +-8

rgwan's Vokodrone report ("feeds back a little more, sounds like a flanger") was the doubled LFO, fixed by the s41 LFO shapes: rendered through the whole device from the same performance and sine-voice bulks the rig sends, the insertion flanger's FM is 0.0028 rad/sample against the unit's 0.0030, and 0.0051 before s41. The 4.3x excess reported earlier came from the harness, which drove the insertion bus with a 0.25 sine, hotter than the rig's voice, into the program's +-8 clip.

The unit agreed step by step (s41h-l, FS1R.unlock `2026-10-15-201341/-202658/-203846/-231352-s25`): Vokodrone's delay-index steps 1b4 / 1d3, and the variation Flanger's and Chorus's 16c / 182, match the engine to 2% of their swing. One rule moved: the flanger's feedback node reaches +-11 on the unit where the engine pinned it at +-8, so the delay RAM word clips at +-32 now (the s14 +-8 was the DAC, not the word). It changes one of the fourteen spot-checked ROM performances (P78), by 5% of peak.

Hikari pointed out that VOP3-1 and VOP3-2 are the same chip, so `Vop3::lfo` no longer branches on which board it is: `LFO_WAVE` bit 12, which only the filter host sets, selects the bipolar range. The filter renders bit-identical and the effect renders are unchanged; that bit 12 is the selector is inferred, not probed.

The variation Flanger, Chorus and Phaser1 "deeper notches" were the measurement: every s25 take has a one-sample phase glitch every 1.451 s, and the 4 s holds were too short for the 0.137 Hz chorus; on a 40 s hold the chorus's FM step is 11.10 Hz on the unit and 11.08 in the engine. All eight effects now agree on FM band and envelope. Left: Vokodrone's notch peaks 10% low at |FB| 60.

## 2026-10-15, session 41 (cont.): the effect LFO is unipolar, and LFO_DIRECTION centres it

The first s41 fix (`b89d62b`) halved the -1..1 waves to -0.5..0.5 and so put every modulation back on its set rate, but it was a fit to the audio, not a reading of the LFO. Session 41c-g read the LFO itself: a free step moves the LFO into an m cell and the DAC step plays it (FS1R.unlock `2026-10-15-173459-s25`, `-181203-s25`, `-193346-s25`; `docs/vop3_ship/41c`..`41g`). On the unit the saws read 0 .. +128 rising and Phaser1's triangle 0 .. +44.3 (its depth word), so the waves are unipolar; 2WayRotary's, whose `LFO_DIRECTION` is 0xC000, reads -64 .. +64 rising, so bit 15 of the direction register centres the wave rather than negating it. `Vop3::lfo` now runs exactly that on the effect chip (saw down `1 - x`, saw up `x`, triangle 0..1..0, direction 0x8000 subtracts 0.5) and all eight s41b effects keep the unit's three strongest envelope and FM lines: the unit-measured shapes and the audio fit agree. The two LFO readers of the variation Rotary SP tapped on the unit (s41d) match the engine to 2% of their swing.

**The depth is not settled.** Against the unit, envelope / FM depth on the 1 kHz tone: Chorus 0.6 / 0.4x, Flanger 0.6 / 0.1x, Rotary SP 0.9 / 0.6x, 2WayRotary 1.1 / 0.7x, the two Phaser1s 0.4-0.8 / 0.04-0.1x, insertion Chorus 0.8 / 0.8x, Vokodrone's flanger 1.0 / 4.3x with an 11.7 Hz FM line the unit lacks. Vokodrone's flanger at five feedback words (s41f, `2026-10-15-183244-s25`) has the unit's notch depth at every setting and 1.3x its level, so rgwan's "feeds back more" is not the feedback loop; it is the swept delay, downstream of the LFO, which now reaches the program at the unit's amplitude. Scaling the delay index (`idxw`, x2/x4/x8) changes nothing there: the next measurement is the delay-index step's value on the unit (1b4/1d3 of the insertion window), as s41d did for the rotary's LFO readers.

## 2026-10-15, session 41: the effect LFOs ran at twice their rate; each wave spans 1, not 2

WouterVanNifterick reported the effect LFOs, Chorus first, running twice as fast as the unit, and rgwan that Vokodrone's insertion (type 3, Flanger) sounds as if it feeds back more and leans towards a flanger. Session 41 put a 1 kHz sine through eight modulation effects one at a time, each with the block of the first ROM performance using it (Full B3's Rotary SP and 2WayRotary, Vokodrone's own flanger from the demo), on rgwan's FS1R over SSH (`FS1R.unlock/captures/2026-10-15-155937-s25`, `docs/vop3_ship/41b.probes.json`, `tools/vop3_lfo_rate.py`), and read the three strongest lines of the tone's envelope and frequency modulation over 212 s each.

**The chip's LFO registers were right; its wave was twice too tall.** The engine set every LFO at the Data List's frequency (Full B3's rotary 2.335 Hz, Chorus 0.137 Hz) and the unit modulates at exactly that and its odd harmonics. The engine modulated at twice it in all eight, because a -1..1 wave swept the delay or allpass through zero and folded. `Vop3::lfo` now spans 1 on the effect chip: the saws -0.5..0.5, the triangle 0..1 (a centred triangle doubles both phasers, a 0..1 saw doubles both rotaries, so each was fitted on its own). Superseded the same day by the measured shapes (entry above). Afterwards all eight land on the unit's lines, frequencies to the bin and relative heights within a few percent: Chorus 0.137 / 0.410 / 0.688 Hz FM, Flanger 0.642 / 1.921 / 1.284, Rotary SP 2.335 Hz FM and 7.006 Hz AM, 2WayRotary 7.324, Phaser1 0.779 and 0.273, insertion Chorus 0.547. The effect programs only use the triangle and the saws; square, sine and S&H keep -1..1 (Rotary SP reads a speed-0 sine as a constant), and the filter chip is untouched.

**Open: the depth.** The rates are settled, the amounts are not. Vokodrone's flanger frequency modulation is 0.0117 in the engine against the unit's 0.0027, four times, with an 11.7 Hz line the unit does not have. That is rgwan's "feeds back more"; the variation Flanger's and both phasers' FM is the other way, smaller than the unit's. Next is the depth path, LFO_DEPTH against the program's scaling of the LFO cell.

## 2026-09-29, the PLG100-SG's phoneme table is in the FS1R's own formant word encoding

The SG keeps its formants as log pitch words and converts to the chip's linear frequency number only at the last step, and both the word and the conversion line up with the FS1R. `tools/sg_phonemes.py` decodes the table.

**The layout.** `FUN_0000E992(n)` returns `0x1A068 + n * 0xFB8` for singers 0 to 7: a 24-byte header and 125 records of 32 bytes. Every byte is seven bits. Singers 0, 2, 4 and 6 share one header and differ from each other by 5 to 103 bytes; 1, 3, 5 and 7 share the other and differ from the first set in about 2240 of 4000 bytes. So it is two voices in four close variants each.

**The word.** `FUN_0000C0F6` builds each formant word as `hi * 256 + lo * 2` from a record's byte pairs, four of them at bytes 3 to 10 and a fifth at bytes 1 and 2, adds a per-singer and a per-part offset, and clamps to 0..0x7CAC. That is the FS1R's Fseq frame encoding exactly (`docs/ymp706_registers.md`, Fseq playback). `FUN_0000B702` interpolates between the previous frame and this one and hands the word to `FUN_0000F1DC`, which computes `(4/9) * 2^((x - 0x5000) / 1024)` through a mantissa table at 0x22A28 that is `65536 * (2^(i/1024) - 1)` to the unit, and a table of octave powers at 0x22A08. That is 1024 units per octave, the FS1R pitch word's own resolution, and 0x7CAC is where the result reaches 1023, the top of the extension bank's ten-bit frequency field.

**The vowels.** The ROM gives the ratios exactly and the absolute scale not at all, since that depends on the YMF293's clock. Read on the FS1R's origin instead, word 0x7FFF at 23982 Hz as measured off `12_fseqlevel` on 2026-09-21, and nothing fitted, singer 0's first five records are the Japanese vowels in dictionary order with textbook male formants: 900/1236 Hz (a), 327 with its second formant at 2383 (i), 413/1573 (u), 475/2090 (e), 538/809 (o). The records after them fall into runs that share a first byte, of five, six and seven, several of them repeating the vowels' F1 and F2 under a different consonant, which is the shape of a consonant-vowel syllabary rather than a strict table of fives. Singer 1 does not read as cleanly: its record 0 is 983/1793, a plausible higher voice's a, but its record 1 at 507/1660 is no i, so the second voice either orders its records differently or leans on the offsets `FUN_0000C0F6` adds. That the vowels come out where they should on an origin measured for a different reason argues that the SG's word is the FS1R's word, origin included, but this is one reading and it is not settled. The test is to play a frame built from these bytes on the unit and hear the vowel, or record it and read the formants back.

**Against the FS1R's own preset Fseqs**, which needs no Hz origin at all, since both sides are words. The FS1R's factory vocal Fseqs lay their frames out the way the SG lays out a record: op1 holds a near-constant low formant around 0x66xx, the SG's fifth word, which sits at 0x66FC in most records, and op2 to op5 carry F1 to F4. "Aeiou" (preset 84) opens at 0x6D3E / 0x7090 / 0x7414 / 0x7548 against the SG's a at 0x6D0E / 0x6EE2 / 0x740A / 0x7536. Over the 1792 voiced frames of fourteen vocal presets (11, 14, 20, 21, 22, 23, 50, 61, 63, 75, 79, 84, 85, 88), sliding every SG word by a common offset and taking the median distance from each record to its nearest FS1R frame across F1 to F4 gives a single minimum near zero: an octave either way is five to ten times worse. Singer 0 fits best 96 to 192 units low and singer 1 96 units high, so the FS1R's recorded voices sit between the SG's two, which is where a lower and a higher speaker would put them. So the SG's word means the FS1R's frequency to within about 0.2 octave, on data neither product was fitted to. The exact origin is still open, since speakers differ by about that much.

**Nothing else is shared.** A search for byte runs common to the SG image and the FS1R's flash and EPROM finds the Hitachi C runtime (EPROM 0x203000 against SG 0x3A6C onwards) and the startup code, and past those, at 40 bytes or more and excluding arithmetic ramps, only `SHTAB`. No envelope, level, velocity or pitch table carries over.

**The levels.** `FUN_0000C0F6` reads two sets of seven levels per record, byte 0x10 with 0x11 to 0x16 and byte 0x17 with 0x18 to 0x1D, adds the singer and part offsets, clamps each to 0..0x7F and looks it up in a 128-entry table at 0x22808 that is `0x7F00 * 10^(-0.375 n / 20)` exactly. So a level is an attenuation, 0x7F is off, and the step is 0.375 dB, the YMP706's own register step. The two sets split as voiced and unvoiced, as an FS1R frame's do: the vowels carry voiced levels of 0 to 0x1A on the formants and unvoiced ones near 0x58, and the runs with first byte 0x17 have every voiced level at 0x7F and only unvoiced ones set, which is a whispered or fricative syllable. An FS1R frame level reaches register 0x48 shifted left one (`docs/ymp706_registers.md`, Tick), 0.75 dB a step, so an SG level carries across at half its value. The levels themselves do not line up with "Aeiou" beyond F1 and F2: the FS1R's recorded voice falls off much faster above F2 than the SG's drawn one.

**The lyric codes.** There is no lookup. `FUN_0000ABF8` takes each lyric byte from the part's 16 KB buffer and uses it as the record number plus one, so codes 1 to 0x7D are records 0 to 124, 0 clears the current syllable and 0x7E and 0x7F are control codes. The record's first byte is not a phoneme name either: `FUN_0000C0F6` reads its bit 6 and bits 3 to 5 to decide whether the levels glide in from the previous frame or jump. The names of the 125 codes are in the PLG100-SG's phoneme list, not the ROM.

**Why it matters here.** If it holds, a record's byte pairs go into an FS1R Fseq frame's formant frequency bytes unchanged and its levels at half value, which is 250 phoneme frames in two voices of Yamaha's own formant data for an Import Audio style "sing this text" path in the plug-in. What each extra oscillator the SG drives past the fifth word is for, and so which of its levels map onto FS1R operators, is not read.

## 2026-09-29, the PLG100-SG's YMF293 is an OPL3 with two extra banks, and shares no register interface with the YMP706

rgwan dumped the PLG100-SG's firmware today (`dmps/plg100-sg/XT687D0_M27C802.bin` in `rgwan/fs1r_firmware_RE`), and the question was whether its tone generator, the YMF293-F, is a relative of the YMP706 that the SG firmware could let us read from a second angle. It is not, at the register level.

**The image.** 1 MB, the two bytes of every word swapped as dumped; swapped back it is big-endian SH code loaded at 0 (the SH7014 is ROM-less), with the Hitachi 1993 SH C runtime banner. It is in `FS1R_DISASM/roms/PLG100-SG.BIN` and `tools/build_fs1r_ghidra.py --only plg100sg` builds the project.

**Shared tables.** Every table in `src/fs1r/firmware/tables.h` was searched for in the image. `SHTAB`, the 2048-byte sample and hold walk, is there whole at 0x36B56, and the PLG150-DX, which carries the YMP706 itself, does not have it, so the FS1R's LFO code shares a lineage with the SG's rather than with the DX board's. None of the pitch tables are there (`NOTETAB`, `TRANS`, `FRMDET`, `COARSE`, `FINE`), and neither are the constants 0x1243, 0x28ED or 0x3FAA the FS1R builds its frequency words from. The sine and LFO speed tables that look like near matches only share their linear stretches.

**The bus.** The YMF293 sits on CS1 at 0x400000. Four address ports at 0x400000, 0x400002, 0x400004 and 0x400006 select a register in one of four 256-register banks, the data port is 0x400001, and a read of 0x400000 with bits 0 or 2 set is busy. `FUN_00011CD2(bank, reg, value)` is the write and keeps a shadow of all four banks at 0x01053B7C; `FUN_00011D20` writes a pair. The YMP706 has one 1024-register space behind a channel select at 0x3FF, a busy line on a port pin, and 16-bit values written high byte at reg, low at reg+8. Nothing of that carries over.

**Banks 0 and 1 are an OPL3.** `FUN_00011B60`, `FUN_0001201A`, `FUN_00012094` and `FUN_0001220C` initialize operators at 0x20, 0x40 (TL 0x3F), 0x60 (AR/DR 0xAF), 0x80 and 0xE0 and channels at 0xA0, 0xB0 and 0xC0 (0x30, both outputs on), plus 0xBD, 0x08, 0x104, and `FUN_00011B16` writes 0x85 to 0x105, the OPL3 mode register. The channel to operator table at 0x36A70 is OPL's own, `0 1 2 8 9 10 16 17 18`.

**Banks 2 and 3 are the extension**, each tied to the OPL bank two below it (`bank + 2` in every caller). They take nine per-channel registers at 0xA8, 0xB4, 0xC0, 0xD8, 0xE4 and 0xF0, and `FUN_0000EB8C` writes pairs at 0x6C+n and 0x78+n with the high byte as `v & 0x1F | 0x20`. That has the same layout as OPL's B0 register: block and F-number high bits under a key-on bit. So the SG's extra oscillators appear to take linear F-number and block, where the YMP706 takes a log pitch word at 1024 units per octave.

**What this settles and what it does not.** The SG firmware is not a second trace of YMP706 registers, and no value from it goes into `cal.h`. Two things are still worth reading: the phoneme data (a run of 32-byte records from 0x1A080 with rising per-formant values and 0x7F-padded level fields, not yet decoded), and `FUN_000097B2` and `FUN_0000939A`, which feed the extension banks and would show whether the SG shapes a formant with anything like the FS1R's bandwidth and skirt.

## 2026-09-28, A071 Relaxxx was silent because Cmp+DS+Dly was read with the plain distortion's word layout

James: A071 Relaxxx renders silent. It is not the voices and not the Fseq. Part 1 is `FseqBase07`, every voiced operator at level 0 and the sound in the five Fseq-driven formant operators, and with the ROM loaded those come up at 7.5 to 16.6 dB of attenuation on carriers at 292, 431 and 2825 Hz, so the channel is producing signal. Parts 2 and 3 are `SweepyVce` and produce signal too. All three parts have the insertion switch on, and the performance's insertion type is **26, Cmp+DS+Dly**, which `ins_to_common` maps to `C_COMPDIST` and then reads with **Comp+Dist's word indexes**: drive at word 3, the compressor at words 11 to 14, an Edge at word 10, and `configure_dist` setting a low-pass from word 3 when it is below 60. In the Data List (p.30) Cmp+DS+Dly puts the delay at 0x108, 0x10A and 0x10C (words 0 to 2), the drive at 0x10E (word 3), the output level at 0x110, the two distortion gains at 0x112 and 0x114 and the compressor at 0x11C to 0x122 (words 10 to 13), with no EQ frequencies, no Q, no LPF and no Edge. A071's word 3 is 0, so the block was given drive 0 and a low-pass at the bottom of the frequency table, and everything after the part summing went through it: the insertion input measured 0.03 peak against 0.0005 out.

**Fix.** `vop3_effects.h`: types 26 and 27 read the compressor at words 10 to 13, the two gains at words 5 and 6 on Comp+Dist's default frequencies (250 Hz and 4 kHz, Q 1.0), no LPF; the attack and release words now reach the follower for all three compressor-plus-drive types where they were a fixed 5 and 170 ms; and the Edge, which 26 and 27 do not have, takes Distortion's and Overdrive's own defaults (80 and 104). Type 25 keeps its layout, which was right. A071 goes from a peak of **0.0014 to 0.0498**, the same level as B016's single note. Every effect constant here remains INFERRED; the layout is the Data List's and is KNOWN.

**Regression.** None of the nineteen cases changes. The tracked `regress_ref.json` is stale against HEAD itself (fifteen of nineteen fail on a clean checkout of `main`, last written 2026-09-21), so the check was made the way AGENTS.md says to on Linux: fingerprint HEAD with `--update`, apply the fix, compare. All nineteen match; the tracked file was restored and still wants regenerating by whoever meant to change those outputs.

**B016 Dyno Rose, the disruption while playing a passage: Auto Pan's L/R direction flipped hard.** James hears a subtle disruption of the tone on B016 while playing a passage, not a loud click. It is not the note-ons and not the allocator: a 64-note line at eight notes a second with the notes held 1.5 s makes two channel steals in the engine and both take a channel that is already under -70 dB, and the jumps in the render that stand out from their own envelope (4x the local 90th percentile of the first difference, over 0.002) fall at **multiples of 124.16 ms** from each other, unrelated to the notes. That is one half cycle of the performance's variation block: **Auto Pan at word 76, 4.03 Hz, direction 5**, which the Data List calls the L/R square wave, and `run_var_ins` turned the square into the two channel gains directly, so the whole variation return (fed by the insertion at 0.439, returned at 0.386) went from one side to the other in one sample twice a cycle. Switching the variation to Thru, or cutting its feed with the insertion switch, takes every off-note jump out; the reverb, the insertion type and the part filter do not. The waveform at one flip reads +96 +98 +100 **+68** +83 +90, a step and a recovery.

**Fix.** The pan position now glides to the LFO's value with the one-pole the noise gate already uses for its gain (0.002 per sample, about 10 ms). Every passage rendered then has **no** jump away from a note-on where it had eleven, and the two other directions, being continuous at this rate, are not changed by it. INFERRED: the unit plainly does not step there, but how fast it swings is unmeasured and the constant is the gate's precedent, not a reading. None of the nineteen regression cases carries an Auto Pan and none changes.

**Also ruled out for B016 on the way.** Ten notes at velocity 127 peak at 0.574 with no sample at the clamp. The documented tick-locked step (`fit_onset.py`, 8.20x on this build against the unit's 1.68x) moves only from 5.5x to 4.5x with the variation block out and is otherwise unchanged with the insertion, the reverb and the part filter off; dumping every register `refresh_regs` writes at note-on and at the first tick for part 2 shows nothing changing but the LFO value, whose depth words are 0. What the analyzer reads there is the aliasing of the DX7 voice's own modulators (op 3 at 24x and op 4 at 43.75x the fundamental, 12.5 and 22.9 kHz at note 72, which is what BrightEP 1's coarse 24 and coarse 25 fine 75 mean on a DX7) under their fast EGs, and that stays where the 2026-09-25 entry left it.

---

## 2026-09-27, the engine sent Fseq bulks at the Data List's address and took them only at its own

The Data List's bulk address table (3.2.1, "Address (H) (M) (L)") gives the Fseq bulk as `6b 00 nn`, b the bank: 60 00 00 for the current Fseq and 61 00 nn for internal Fseq nn. `Synth::dump_request` (`midi.cpp`) answers a request with exactly that. `load_sysex` (`rom.cpp`), though, took an Fseq bulk only at address high 70, the address `tools/extract_presets.py` gave the preset files before anyone checked the table, so a dump off a real unit, or one this engine had just sent, was dropped without a word. `load_sysex` now takes 60, 61 and 70 (70 stays so the bundled presets still load), `Device::getState` writes the current Fseq at 60 00 00, and the plug-in's exports carry it there, so what FSVR writes is what a unit takes.

Two more in the same place, both in `fsvr/device.cpp`, ours: `Device::setState` dropped the system bulk that `getState` writes, since the firmware's loader takes none, so a restored session kept the fresh system block; it now puts those 76 bytes back as they were. And the plug-in's `saving()` snapshotted the engine before its worker had applied a session that had just arrived, which the plug-in's own check caught as a session saved straight after it was opened losing its sound.

None of these moves a render. `bin/fsvr_console.exe -w` at `8618c17` and after is the same file, byte for byte, on the init voice, performances 2 and 56 (the second with Fseq 0) and 300, and ROM voice 300.

---

## 2026-09-25, a fixed-frequency operator gets no frequency EG

`24_onset`'s `onset-feg` segment was the one isolated subsystem that separated from the unit: **2.54x** its own body at the tick window against the unit's **0.98x**. It turned out not to be a step at all, and the flagged ratio was the analyzer reading a frequency sweep rather than a discontinuity — a carrier sliding down from well above its target has its largest first differences at the start, which is exactly where the window sits. Measuring the thing itself instead settles it.

**The measurement.** Tracking the carrier's instantaneous frequency over the first 120 ms of each strike, median over the eight strikes, with the segment's own voice (a **fixed**-frequency sine at 1002 Hz, `feg_init` +25, attack time 20):

| ms after note-on | unit | engine (before) | error |
| --- | --- | --- | --- |
| 0.00 | 1007.6 Hz | 1733.4 Hz | **+939 cents** |
| 5.33 | 1007.6 Hz | 1575.0 Hz | +773 cents |
| 10.67 | 1007.6 Hz | 1384.1 Hz | +550 cents |
| 21.33 | 1007.6 Hz | 1058.9 Hz | +86 cents |
| 26.67 and after | 1007.6 Hz | 1007.4 Hz | −0.3 cents |

**The unit does not sweep at all.** It holds 1007.6 Hz flat from the first frame while the engine started 939 cents sharp and took 25 ms to ramp down, the two agreeing to 1 cent only once our ramp had finished. So a fixed-frequency operator gets no frequency EG.

**Why this does not contradict the earlier FEG work.** The 2026-09-25 entry that made the FEG a linear ramp measured `07_modulation_2`'s six `feg-*` segments to 1 to 5 cents, and those segments are built with `sine(...)` — **ratio**-mode carriers. This one is `fixed=1`. The two results are consistent and together they say where the envelope acts: the FEG rides the note's pitch word, and a fixed operator never takes that word, it takes its own coarse and fine bytes (`notes.cpp` computes `freqWord` from `8 * (coarse * 128 + fine) + 0x28ED` for fixed and formant operators, and from the ratio tables plus `C.regPitch` otherwise). One guard in `render_chan`, `if (!v.fixed && s.feg.stage < 2)`.

Formant operators (`form == 7`) also take an absolute word rather than the pitch, so the same argument would put them with the fixed ones — but **no capture drives a formant operator with a live FEG**, so they were left applying it rather than changed on a guess. That is the next thing a capture could settle cheaply.

**Result.** Worst error over the sweep **939 → 11.3 cents**, mean **+111.7 → −1.2 cents**, and the residual 11.3 is the first analysis frame alone, where the window straddles note-on. `onset-feg` now reads **1.41x**, the same as the control, against the unit's 0.98x.

**Scope.** Three of the nineteen render regression cases change: `perf-towarp`, `ctrl-freqbias` and `ctrl-freqbias-off`. Checking every case for a fixed operator with a non-flat FEG finds **exactly those two performances** (towarp has 2 such operators, the freqbias voice 183 has 3) and none in the other sixteen, so the change is confined to what it should touch.

**The check.** `tools/fit_onset.py --check` is the gate: it fails if `onset-feg` reads more than 1.4x the control. Verified both ways — 1.41x passes with the fix, 2.54x fails without it. It lives there rather than in the engine selftest because the selftest could not be made to produce sound for this voice: `init_perf` leaves the part with no receive channel and algorithm 12 (where operator 1 is a modulator), and after fixing both the operator still rendered silent. Three attempts at a selftest version all passed identically with the fix in and out, which is worse than no test, so they were deleted. The lesson is in the skill: a new check has to be seen failing before it is worth keeping.

---

## 2026-09-25, the unit does not step at the control tick: the B016 click is ours, and it is the insertion path

`24_onset` was recorded (`FS1R.unlock/captures/2026-09-25-8`, gate clean, EPROM and stub matching, 63 s played) and it answers the question the previous entry asked.

**The control is honest and the unit is flat.** `onset-plain`, the segment with every tick-driven update switched off, reads **1.08x** at one tick period after note-on, so the tick period, the alignment and the analyzer are all sound. Against that, per segment, hardware then engine:

| segment | unit | ours |
| --- | --- | --- |
| onset-b016 | **1.68x** | **9.80x** |
| onset-plain (control) | 1.08x | 1.41x |
| onset-lfo | 1.17x | 1.79x |
| onset-filter | 2.02x | 0.42x |
| onset-peg | 1.31x | 2.40x |
| onset-feg | **0.98x** | **2.54x** |

So **the unit has no step at the control tick at all**, on B016 or on any of the isolated subsystems. The discontinuity is entirely ours. Two segments separate by more than the control's own spread: B016 itself, and `onset-feg`.

**Which operator, and which block.** Muting B016's part-1 operators one at a time through the real parameter-change address (`0x60 + part`, op, 22) moves the ratio from 4.41x to **1.77x when operator 2 is muted** and nowhere else, so op2 carries it. Op2's own envelopes are not the cause though: its frequency EG is flat (init +0, attack +0, so `FreqEG::start` puts it straight in stage 2 and it never ticks), and tracing its amplitude EG, `att`, `attS` and `regLevel` sample by sample across 244..255 shows a smooth ramp with **nothing changing at 249**.

The step is downstream of the channel. Switching the parts' insertion switch off (part byte 0x14) drops the ratio to **2.93x and moves the peak from 5.188 ms to 1.831 ms**, i.e. removes it; zeroing the reverb send instead changes nothing (4.41x, still 5.188 ms). So it is the **insertion path**, and B016 has `insSw 1` on both parts. What makes that odd, and unresolved: B016's reverb, variation and insertion types all read **0**, which is `C_THRU` in `run_var_ins` and an early pass-through in `run_reverb`, so no block should be doing anything. Either the type decode is wrong for this performance or something in the per-block level and pan words is being applied at the tick boundary rather than per sample.

**Where this stopped.** Probing the chain further needs `Device`, not `Synth`: `Synth::render` on a `rom_perf`-loaded performance renders silence, and three separate probes wasted on that path all read zeros and briefly made the step look absent from the channel sum when it was really absent from the whole probe. `onset-feg` (2.54x against the unit's 0.98x) is a second, independent bug with a measurement already in hand, and it is smaller and better isolated than the insertion one.

**Also ruled out here.** The operator frequency EG is not B016's cause even though `onset-feg` is a real defect, because B016's FEGs are all flat. And LFO1's value, the previous entry's candidate, was already reverted for changing nothing.

---

## 2026-09-25, the B016 click is locked to the control tick, not to the waveform

B016 Dyno Rose clicks on note-on in the engine and not on James's unit, and it has now survived three fixes that were each a genuine fault against the firmware and none of them this one: the frequency EG (a linear ramp, not an exponential approach), the note-on damp (`FUN_00023000` releases the channel it takes rather than zeroing it), and the channel allocator (round robin with a note-priority steal). Measured after all three, the transient ratio is **4.14x its own body, exactly what it was before any of them**.

**Where the step is.** 5.188 ms after note-on. That is 249 samples at 48 kHz and one period of the **192.3 Hz MTU2 tick**, which is what drives every per-note update: LFO1, the pitch EG, portamento, the operator frequency EG, the filter EG, and the register refresh as a whole. Two things make this a control-rate correction rather than waveform content. Across twelve different random seeds the step's **time is identical to three decimals while its size moves from 3.72x to 5.36x**, and the seed only changes the note's start phase and pan — waveform detail moves with the phase, a tick-locked correction does not. And it is present with either part muted, and both parts show it, so it is **not** the converted DX7 voice in part 2. That was the standing suspicion since the first pass and it is wrong.

So some register the engine writes at note-on differs from what the first tick then computes, and the jump to the correct value is audible. Reading our own code cannot say which update, and cannot say anything at all about whether the unit does the same.

**What was tried and reverted.** LFO1's value looked like the answer: `refresh_regs` turns `C.lfoVal` into the pitch, AM and FM words, but `lfoVal` is only ever computed by `lfo_tick`, so a fresh channel carries 0 through its whole first tick period. On B016 that is a 0 → −120 step in `lfoVal` at exactly sample 249. Staging it at note-on (without advancing the phase or the delay) changed **zero of the nineteen regression cases** and did not move the ratio, because the LFO fade is 0 at note-on so the PM and AM words are 0 whatever `lfoVal` holds. It was dead code fixing nothing and it was reverted rather than committed. The `lfoVal` jump is real and it is not the click.

**The capture.** `captures/requests/24_onset.mid` (`tools/make_capture_onset.py`), six segments, a minute of playing. Each is one note struck eight times a second apart, so the onset averages over the strikes: a tick-locked step sits at the same offset every strike and survives averaging, waveform detail and noise do not. `onset-b016` is the patch itself, by its own performance bulk. `onset-plain` is the **control** and the most important segment in the file: one fixed sine carrier with a flat amplitude EG, flat PEG, flat frequency EG, LFO depths 0 and the filter off, so nothing updates on the tick and there must be no step — if that one steps, the tick period or the analysis is wrong and nothing else in the file can be read. Then four variants, each differing from the control in exactly one respect: LFO1 at full pitch depth with key sync off, the filter on with a wide fast filter EG, a fast pitch EG, a fast operator frequency EG. Whichever reproduces the step names the update responsible; `onset-b016` alone would mean the cause is that patch's own parameter combination rather than any single subsystem.

`tools/fit_onset.py` aligns every strike on its own note-on, averages the first differences across the eight, and reports the averaged step in a 0.4 ms window at one tick period against the note's own body. Against our own render it puts **B016 at 9.80x with its peak at 5.542 ms and the control flat at 1.41x**, and none of the four isolated subsystems reproduces it alone — so the pipeline discriminates, and it already says the cause is a combination rather than one update. `FS1R.unlock/captures/fs1r_capture_session7.py` plays it.

**Four bugs in the stimulus, all caught by rendering it rather than by trusting the offsets.** This is the part worth keeping. `Smf.note` takes `(ms, note, vel, dur)` and the first version had velocity and duration swapped, which threw `bytes must be in range(0, 256)`. A bank select plus program change did not load B016 at all — the segment was playing something at 93.8 Hz — so it now sends the performance bulk straight out of `presets/`. The LFO depth byte alone does nothing: the depth only reaches the pitch through the operator's own pitch mod **sensitivity** (`set_op`'s `pms`), which is 0 in `init_voice`. And a **fixed-frequency operator ignores the channel pitch entirely**, so the LFO and PEG cases could never have shown anything with the fixed carrier the control uses, and now use ratio carriers. The check that found all four was rendering the file and asserting every segment is audible and spectrally distinct from the others; three segments had identical RMS to six decimals before it.

---

## 2026-09-25, the channel allocator is round robin with a note-priority steal, measured against the unit

`23_damp` (the previous entry) came back with no damp in it, which said the reading behind the stimulus was wrong without saying where. `FS1R.unlock/captures/fs1r_capture_session6.py` settles it by reading the allocator instead of inferring it from audio: `fs.peek` on 0x010287DC (the per-channel priority word), 0x010287DE (in-use), 0x010289BC (channel to part) and 0x010288DC (notes per part), dumped after every note-on. The take is `FS1R.unlock/captures/2026-09-25-7`, gate clean, EPROM and stub both matching their images.

**The priority word is the note number, exactly.** Filling an empty machine with notes 36 to 67 wrote 36, 37, 38 ... into the word of each channel as it was taken, one for one, which confirms what `FUN_00010dc4`'s `DAT_0103937a = uVar3 & 0x7f` only suggested.

**Allocation is round robin, not lowest-free.** Those 32 notes landed on channels **1, 2, 3 ... 31, then 0**, not 0..31. `FUN_0000f7dc` phase 1 walks from `DAT_010290ee`, the last channel allocated, and takes the first free one it meets; the wrap onto channel 0 at the end is the signature. The engine took the lowest free channel, so every note after the first was on the wrong channel.

**The steal ranks on the note, from the part's own cursor.** With all 32 channels held, a 33rd note of 100 took **channel 2**, which held note 37 — not the oldest (channel 1, note 36) and not the lowest-numbered. Phase 3 walks from `DAT_010288bc[part]`, that part's own cursor, and takes the first channel whose stored note is **above** the arriving one, falling back to the lowest stored note it passed. A note of 24 arriving below everything held took the same channel 2 by the fallback path. Reproducing `FUN_0000f7dc` in Python against the measured state gives channel 2 for the arriving note and the exact 1..31,0 fill order, so the reading is checked before any C++ was written; the engine's replacement is checked the same way in the selftest, against the measured numbers rather than against my reasoning.

The engine did "first free channel, else the oldest by age". Both halves were wrong. On the demo songs this is not cosmetic: steals of a still-sounding channel go from 13 to 47 in Vokodrone, 44 to 188 in AN Superarp, and 0 to 32 in Kalimba. Age is no longer consulted anywhere in allocation, and `Synth` carries the two cursors the firmware has (`nextChan`, `partChan[4]`).

**A part's reserve cannot oversubscribe the machine.** Writing note reserve 32 to part 2 while parts 3 and 4 held 8 each read back as **16**, which is 32 - (0 + 8 + 8). `FUN_00030796` clamps every parameter write against a per-parameter table, and the note reserve's own max at EPROM 0x37F25A is 32, so the 16 is not that clamp — it is the reserves being held to the 32 channels between them. That is one measured point and the engine now implements the sum rule, which fits it; a second point with a different split would confirm it properly.

**What is still open, and why the steal stage of that session cannot close it.** The stage wrote part 1 on MIDI channel 1 and part 2 on MIDI channel 2, then sent one note on channel 1 — and the unit sounded it on **two** channels, charged to parts 1 and 2. One note took two channels, so nothing after that point in the stage is a single steal and its two changed channels cannot be read as one. Two readings survive: 0x010289BC is not the channel-to-part map, or a part hears more than the one channel written to byte 0x04. The DataList table (line 3241) does give `03 = Rcv CHANNEL MAX A1~A16/off` against `04 = Rcv CHANNEL`, and section 2.2.3.2 says a voice program change is received "using Part Receive Channel (Part Receive Channel to Part Receive Channel Max)", so a part does listen to a *range* and the engine's `part_listens` compares one channel. But both parts in that stage had Max written as off, which a range does not explain either, so the range fix was written and then **reverted** rather than shipped on a reading that does not fit the measurement. Session 6 now reads bytes 0x00, 0x01, 0x03 and 0x04 back per part and asserts one note takes one channel, so the next run says which of the two it is.

---

## 2026-09-25, note-on took a sounding channel by zeroing it, which is the click (B016 Dyno Rose, A020 Vox Morph)

James: B016 Dyno Rose and A020 Vox Morph click when a note starts and his unit does not. The previous entry's frequency EG fix did not move either of them, and the "single-sample dropout" the first pass found in B016 was an artifact of the detector rather than of the engine: the test flagged a sample far off the line between its neighbours, and a clean sine at 4.19 samples per cycle trips that on 10 % of its samples. B016's part 2 runs a modulator at 43.75 times the fundamental, which is exactly there. The detector was measuring Nyquist, not a fault.

**The measurement, off the demo recording.** The unit plays the fifteen demo songs out of its own EPROM, so the same byte stream drives the recording and our render and no capture rig is involved. Per note-on, take the energy above 6 kHz in a 3 ms window at the onset and reference it to the same band over the 200 ms that follow. That ratio is within one signal, so it survives the two not being sample aligned, and it separates the two candidate faults: a click adds high frequencies without adding level, while an attack that is early or too steep moves both together. Both signals get the same +-25 ms window search, because the song placement is one 10 ms envelope frame coarse and measuring the unit at a fixed index while measuring ourselves at our best would manufacture the difference being looked for. That bias is not hypothetical: without the search the same script read +6.1 dB on Vokodrone and -29.2 on Full Tines, both of which are the alignment.

Four songs come out click-like, high frequencies without level: AN Superarp +9.6 dB HF against +1.7 level, Fat Line +7.1 / +3.5, Kalimba +5.5 / -1.1, Piko Voice +4.1 / -0.2. The other eleven sit inside a couple of decibels on both. Splitting note-on from note-off puts it on note-on (Fat Line +7.1 at note-on against -25.5 at note-off).

**The cause, read out of the flash.** `FUN_00023000` is what note-on calls once the allocator has chosen a channel, and both note-on paths call it (`FUN_000112c0` polyphonic, `FUN_0001228c` mono). Its argument is a word out of the table at EPROM 0x35B260, which is one hot per channel, and it does two things with it: it sets that channel's EG stage word at 0x0103B398 (stride 0x42) to **4**, the release stage, and it writes YMP706 register 0xFC/FD, the mask the register map already had as "damp / restart". So the hardware *releases* the note it is about to displace. It never cuts one off.

The engine did `C = Chan()`, which zeroes the struct, so a note landing on a channel that was still sounding dropped that channel's output to zero between two samples. On a 32-voice machine that happens whenever the allocator runs out: 13 times in Vokodrone, 44 in AN Superarp, and the step is the size of whatever the old note was putting out, up to 0.15 of full scale.

`note_on` now carries the displaced channel's last output into the new one as a damp and `render_chan` adds it while it decays at `cal::DAMP_MS`. In a forced steal the largest step across the note-on goes from 1.61x the note's own body to 0.83x, i.e. from a discontinuity to no discontinuity.

**What is measured and what is not.** That the damp exists, that it is a release rather than a mute, and that it is applied to the channel being taken are all read from the flash and are not in question. The rate is not: `cal::DAMP_MS = 1.0` is the order of the level register's own glide and nothing more, and it is now the only constant in `namespace cal` with no measurement behind it. Nothing already recorded can settle it, since a damp needs the allocator to run out and every file in the capture set plays one note at a time. `captures/requests/23_damp.mid` (`tools/make_capture_damp.py`) is built for it: fill every channel, keep one audible, strike one more note so the allocator has to take the audible one, and read the fade. Six segments, a minute of playing. Three of them are the same steal at 0, -8 and -14 dB, which is what separates a one-pole time constant (same fade time from any level) from a constant dB per second (quicker from a lower level) — not a formality, since the frequency EG turned out to be the ramp against expectation the day before. Two more put the same steal at 500 Hz and 8 kHz, and one holds the note with no steal at all as the reference. `tools/fit_damp.py` reads it back, and against our own render it recovers the 1.0 ms the engine uses to within 7 %, so the pipeline is known good before any hardware time goes into it. `FS1R.unlock/captures/fs1r_capture_session5.py` plays it.

Which channel the steal lands on is a reading of `FUN_0000f7dc` (the part furthest over its note reserve, then the channel whose stored note is above the arriving one), so the file is built to fail visibly: if the steal takes a filler instead, the audible tone keeps sounding and the segment reads "no damp". Our own allocator takes the audible channel in all five steals at three distinct levels, which is as far as that check goes without the unit.

**The take came back "no damp", which is that failure firing.** rgwan recorded `23_damp` (`FS1R.unlock/captures/2026-09-25-6`, gate clean, EPROM and stub both matching their images, 41.5 s played). The reference segment is flat as intended, and the three steal segments drop 1 to 5 dB at the steal and then *hold* — no fade at any of the three levels, and the 500 Hz and 8 kHz segments disagree with each other. Fitting a decay to that produced slopes of 0 and time constants of infinity and 1.5e7 ms, which is the fitter correctly reporting that there is nothing to fit. The unit never took the audible channel, so the recording does not contain a damp and `DAMP_MS` is still unmeasured. The small step that *is* there is consistent with the 33rd note simply being added to the mix.

So one of the assumptions behind the stimulus is wrong, and the audio cannot say which: whether the note reserve landed where the file wrote it, whether the priority word is the note number at all, or whether the part-selection phase ever picks the audible part. `FS1R.unlock/captures/fs1r_capture_session6.py` reads the allocator directly instead of inferring it — `fs.peek` on 0x010287DC (priority word, stride 4), 0x010287DE (in-use flag), 0x010289BC (channel-to-part map) and 0x010288DC (notes per part) after every note-on, plus the reserves at 0x010000C0 stride 0x34. It fills all 32 channels ascending and then overflows twice from the same held state, once with a note above everything held and once below, which is exactly the distinction the priority rule is supposed to make; then it repeats `23_damp`'s own two-part split and reports in one line whether the audible channel's word changed. Reads only, no recording, and it answers which assumption broke rather than producing another number to disbelieve.

The nineteen render regression cases are **byte-identical** with the damp and without it, which is the expected result and worth stating: every one of them plays a single note, so none of them ever exhausts 32 channels.

**This fixes a real discontinuity and it is not what the demo measures.** Both halves of that matter. The zeroing was a genuine fault against the firmware and it is gone. But the four click-like demo songs do not move by a tenth of a decibel with the damp in, and two of them, Fat Line and Kalimba, never steal a channel at all across the whole song; AN Superarp steals 44 times and its jumps are 0.0002 of full scale, three orders under the 0.15 Vokodrone reaches. So the onset HF excess those four songs show has a second cause, still open, which the damp does not touch. What has been ruled out for it: the frequency EG (the previous entry, no movement), channel stealing (this entry), and the amplitude EG's attack floor, which is measured off hardware at -54.5 dB and is the same jump on the unit. A per-performance sweep of the note-on step in isolation puts two ROM performances well above the rest, 120 Drum Kit 1 at 14x its own body step and 143 Dyno Rose at 3.9x, so whatever it is concentrates in percussive patches with fast attacks rather than being uniform. Sorting it out wants the same kind of isolation the damp got: a request file that plays one voice with one operator at a known attack and reads the first two milliseconds, rather than another pass over songs where fifteen things move at once.

---

## 2026-09-25, the operator frequency EG is a linear ramp, not an exponential approach (B056 Tech Lead, B049 Hollow, B115 Obie Strings, B120 Hit)

James: B056 Tech Lead sounds like two tones a few hundred milliseconds apart settling onto one pitch, B049 Hollow and B115 Obie Strings do the same and B120 Hit is out of tune altogether; adjusting the PEG does nothing. It does nothing because none of those patches uses it: B056's voice (PrA 081 `Tech Lead`) has a flat pitch EG, every level 50 and the release times 20. What every one of the four has is an operator **frequency** EG, which is a different block: per operator, sysex `0x08`-`0x0B` voiced and `0x2A`-`0x2D` unvoiced, init and attack levels through `FEGLVL` into registers 0x60-0x78 and both times through the amplitude EG's own rate conversion. The CPU side of that was already checked against the unit's voice image, so the question was only what the chip does with the two words, and the engine's answer was a guess: an exponential approach with `cal::FEG_TIME_K = 0.3` as a fraction of `rate_secs`, carried since the block was first written.

`07_modulation_2` has six frequency EG segments and has had them since the first capture set. They are marked `measure: envelope`, so the analyzer had only ever read their amplitude; nobody had tracked the carrier. Tracking it (`zero crossings of the segment, 4 ms hops`) reads the shape straight off the recording:

| segment | init | attack | attack time | what the unit does |
|---|---|---|---|---|
| `feg-i25-a0-t40` | +25 | 0 | 40 | 946 cents down to 0 in 220 ms, straight |
| `feg-i-25-a0-t40` | -25 | 0 | 40 | 946 cents up to 0 in 220 ms, straight |
| `feg-i0-a50-t40` | 0 | +50 | 40 | 4762 cents up over 1.07 s, straight |
| `feg-i0-a-50-t40` | 0 | -50 | 40 | 4800 cents down over 0.71 s, straight |
| `feg-i50-a0-t0` | +50 | 0 | 0 | over inside one analysis window |
| `feg-i-50-a0-t0` | -50 | 0 | 0 | the same |

**Linear, and the slope does not depend on the swing.** A straight line fits the four moving segments to 0.3, 1.3, 3.8 and 3.8 cents rms; the best exponential over the same points, free to pick its own time constant, leaves 39 to 90. Four and a half times the swing takes four and a half times as long, at one attack time, which is what a ramp does and what an approach cannot: an exponential aimed past a target proportional to the swing takes the *same* time whatever the swing, which is exactly the argument that settled the filter EG's shape the other way on 2026-09-23 (`flteg`, 128 and 512 both 0.23 s). The two EGs are on different chips and they do not behave alike.

**The rate is the amplitude EG's own ladder, read as pitch.** Attack time 40 is rate word 37, a 546 ms traverse of `rate_secs`; the two long segments ramp at 4393 and 4391 cents per second, which is 23.99 and 23.98 semitones per traverse. So one stepper runs both EGs and only the unit of the step differs: 0.375 dB on the amplitude side, a quarter tone here, a 96-unit scale quartered either way. `cal::FEG_TRAVERSE = 24.0` replaces `FEG_TIME_K`, and it is measured rather than fitted: two segments at one rate word agree to 0.07 %, and the number they land on is the one the amplitude side's scale predicts.

Against the unit, the worst frequency EG envelope in `07_modulation_2` goes from 2.16 to 1.40 dB rms and the file's mean from 0.81 to 0.76; the four moving segments now track the recording's pitch inside 1 to 5 cents where they were 200 to 2900 cents out. On B056 the fundamental is steady from the first analysis window at note 60, where it used to slide 308 Hz down to 260 over 150 ms, which is the "two tones settling" exactly: op7's `FEG init +14 att +6` was crawling toward its target on the old curve instead of ramping to it.

Seven of the nineteen render regression cases move, and they are precisely the seven whose voices have a non-flat frequency EG (`rom-voice0` and the three Fseq cases on PrA 000 `Ballad EP`, `perf-towarp` on `Warp1`/`Warp2`, and the two `ctrl-freqbias` cases on `Tech BD`); the other twelve are byte-identical. The selftest gained the shape check: two swings at one attack time, the constant step, and the slope against `FEG_TRAVERSE`.

Two things this does not settle. `FEG_SEMIS` is still 48 semitones for the full 128-step register swing, since the four segments here all read their own endpoints consistently with it but none of them pins the scale independently of `FEGLVL`. And B016 Dyno Rose's note-on click is **not** this: it is unchanged by the fix, it sits in a converted DX7 voice (PrC 050 `BrightEP 1`) whose operator ratios convert correctly, and no recording in the set covers it. It stays open.

---

## 2026-09-24, the engine read every DX-bank voice one record off the EPROM, and so did the bundled bank (B014 Full Tines)

James: B014 Full Tines does not sound like demo song 2, and the demo's `FullTine 1` bulk is the unit's own conversion of that voice, so the two should be one patch. They were not, for a reason upstream of the converter. The DX7 voice table in the EPROM starts at `0x230000` and holds 1152 plain 155-byte VCEDs with the name last; `5c2a48b` (rgwan) fixed `tools/extract_presets.py`, which had been reading records from `0x230091` with the name moved to the front, i.e. each record's first 145 bytes were the *next* voice's operator and common data under this voice's name. Two consumers kept the old read: `rom_voice` in `src/fs1r/firmware/rom.cpp`, which the console's `-r`/`-P` path, the plugin's ROM loader and a performance's bank/program bytes all go through, and `plugin/generated/fs1r_presets.syx`, which was last packed before the extractor fix (`tools/check_presets.py` only compared names, and every name was right). The selftest's DX-Acrd 4 VCED fixture was typed off the same shifted read, which is why the byte-for-byte diff of `0c7c10f` needed a skip list for "the demo's edits": the "edits" were the neighbouring voice.

With the record read at `0x230000`: engine conversion of PrC 68 against the demo's `FullTine 1` bulk differs on one byte, the category (`0x0E`, the demo's one edit), down from 27; DX-Acrd 4 against the demo's bulk likewise differs on the category alone, and the selftest fixture now carries the real record and diffs all 607 other bytes with no skip list. Across the fifteen demo songs, every voice bulk whose name is in the preset banks is now byte-identical to the engine's load of that bank slot except for bytes the demo genuinely edits (Full Tines and Accordion: category; BeatBox `Beat SD`: two filter bytes; AN Superarp: two operator bytes; Human: four controller bytes; FS Moby II: its control sets). `captures/demo_preset/02_Full Tines.mid` is the song with its three sysex blocks replaced by bank select `3F/42` + program 13 (`tools/make_demo_preset.py`), so the engine loads PrB 014 from the bundled ROM and the demo's own bulks never reach it; rendered against the original the two files differ by one flat gain of -1.38 dB (ratio 0.85321 on every sample, std 3e-8) with a residual under 2e-8 after scaling, and that gain is the one byte the demo edits in the performance block, volume `0x10` 109 against the ROM's 93. `check_presets.py` now diffs the blob's bytes against `presets/`, not just the names.

---

## 2026-09-24, the DX7 conversion byte for byte against the unit's own

rgwan: the demo's `FullTine 1` bulk sounds right and B014 Full Tines, the same voice through the converter, does not. The demo sends two voices the firmware itself converted from the DX banks, `FullTine 1` (PrC 68) and `DX-Acrd 4` (PrC 371), and then edited on a handful of musical bytes (EG times, a coarse ratio, key scaling, LFO speed, category), so every byte the edits left alone is an oracle for `convert_dx7`. Read against both with `FUN_00035918`/`FUN_00035E96` open: the common block's LFO2 takes LFO1's wave and speed, PMD is halved, PEG levels of 99 become 100, the formant and FM control sets are filled from a per-algorithm table at `0x38A7CC` (destination output of one operator, depths +10 and +7), the filter block gets its defaults (cutoff 36, resonance 10, 85, 7, 69, 60, 12; EG 64, 50, 100, 75, 75, 30, 30, 30, 99), the two unused operator slots take the template at `0x38A914` with break point 60 and call index 6 and 7, every slot's unvoiced half is the 27-byte template per slot at `0x38A937`, and on a used operator the time scaling is the DX rate scaling, the EG bias sense is the DX amp mod sense, the frequency EG init and attack are 50, and the Fseq track is the DX index. The engine had the blank-voice defaults for all of that: unvoiced levels 0 instead of the template's 20, no control sets, filter EG levels 50, PMD unhalved, and PEG velocity through 99. The selftest carries the DX-Acrd 4 VCED and the unit's bulk and diffs every unedited byte; the B025 carrier-slot check stays. B014's note-60 envelope now lies on the demo bulk's to a decibel over the four seconds the two share; what differs after that is the demo's own edits.

---

## 2026-09-24, the formant window's pitch mod goes through the operator's own sense, not the channel word (B011 Human Eh)

rgwan: B011 Human Eh grows a strong vibrato a few seconds in that the unit does not have. The voice is the demo's `Human` (LFO1 sine, speed 35, delay 55, pmd 12; pms 1..4 on the six formant operators, 2 and 3 on the two sine carriers), and the demo take of song 15 carries the unit's answer: on the held note 55 its harmonics at 4, 8 and 12 times the fundamental wobble 33, 39 and 48 cents peak to peak, the same as the sine carriers at pms 2. The engine wobbled them 50 to 78 in the mix and, operator by operator, 365 cents on the pms 1 formant and 184 on the pms 4 one. The window was being clocked off `C.f0`, the channel pitch plus the whole LFO pitch word, which is pms 7 whatever the operator's own sense says; only the carrier `fop` went through `PMS_FRAC`. The window now runs at the channel pitch plus the operator's own `pmw`, and the pms 1 formant's harmonics come out at 9 to 10 cents, the pms 4 one's at 14 to 33, and the mix at 21 to 35 against the unit's 33 to 48. The same look at `07_modulation_1`'s eight pms segments (pitch depth 99, a sine operator) read the per-operator sense curve itself: the fundamental swings 0.038, 0.076, 0.125, 0.212, 0.365 and 0.620 of pms 7's at pms 1..6, where the DX7 curve the engine carried gave 0.026, 0.053, 0.089, 0.161, 0.277 and 0.497, a third shy at every step. `PMS_FRAC` is the measured curve now and the seven segments land within 1 to 4 %.

---

## 2026-09-24, the DX7 conversion placed every operator one slot off (B025 Tremolo's tone after note-off, Full Tines)

`1532608` rewrote `convert_dx7` from `FUN_00035918`/`FUN_00035E96` and read the operator slot out of DX7MAP word `26 + j`. The firmware reads word `25 + j` (`0x389FBE + 2j` off the row base `0x389F8C + 66 * alg`; `26 + j` is the same list shifted by one, which is why it looked plausible). The effect: DX operator 6 landed on the slot meant for DX operator 5 and so on round, so in DX algorithm 5 (FS1R 12) the carrier DX op 2, with its L4 of 99 and rates of 99, went into FS1R op 8, a carrier slot, and sustained at full level after note-off until the channel died; every other DX voice had its carriers and modulators swapped by one. A check against all 32 DX algorithms' carrier sets: word `25 + j` puts every DX carrier on an FS1R carrier slot, word `26 + j` fails 31 of them. Two more bytes read against the demo's own `FullTine 1` bulk (the demo song sends the converted voice, so the firmware's conversion is on disk): time scaling is the DX rate scaling, not 7, and detune is `dx + 8`. What remains different between the engine's conversion and the unit's for that voice is the LFO block, the ACED-only bytes (the demo's bulk carries values the VCED cannot) and the ±1 rate rounding, none of which the VCED alone can produce. Selftest carries the carrier-set check.

**Voice edits reach sounding notes** and **the plugin's voice Note Shift** (a −24 default) are in the same set of commits.

---

## 2026-09-24, session 4: the key code table at every semitone, the hold does not scale, the unvoiced resonance carrier and the noise it takes with it

Two recordings from `FS1R.unlock/captures/2026-09-24-5` (`fs1r_capture_session4.py record`); the `perchan3` register dump stopped on a script bug (the bulk-echo query put an operator byte address of 134 in a MIDI data byte) and is fixed for the next run.

**`21_keycode`.** One decay at every semitone 12..120, time scaling 7. Every slope lands on the chip's rate ladder, so the key offset is read at all 37 key codes 77..113, not ten of them: the ten-point interpolation had twelve notes a rate step off, and the hold had been scaled with the key code where the unit holds 135 ms at every note. `docs/aeg.md`. `21_keycode` 7.99 / 2.43 → 4.44 / 0.57; `19_drums` 0.81 / 1.92 → 1.24 / 2.32, which is the price of the note-60 hits' hold no longer scaling; the drum's own note 41 is unchanged at −8.

**`22_ures`.** The unvoiced resonance carrier at five bandwidth registers by the four settings that carry a tone, 8 kHz. The carrier's amplitude against the band depends on the register as much as on the setting (1.50 at register 10, 2.37 at 87, setting 7), and the noise under it is cut: 23 dB at register 10 and setting 7, nothing at 67 and up. `NOISE_RES_DC` is now `URES_DC` and `URES_NOISE_DB`, two five-by-four tables interpolated over the register. Read off the left channel alone: the recording's right channel lags the left by one sample, which costs a mono sum 1.25 dB at 8 kHz and had put a phantom decibel into every unvoiced take at that centre. `22_ures` 0.82 / 2.11 → 1.08 / 0.62; the level that remains is the 1.1 dB every 8 kHz unvoiced take sits over the engine (`13_unvoiced3` too), the noise level against the centre, not this table.

**Voice edits reach sounding notes**, and the plugin's voice Note Shift defaulted to −24 (`gen_parameters.py` read the one Data List row written with hyphens instead of tildes as unipolar). Both in the same release.

---

## 2026-09-24, the "1" forms are the same window with a sin^2 fall, all1/all2 are the window alone, and the drum's key code is 87

Nothing recorded; three readings of data already on disk, and the last three open items outside the effects.

**The skirt has two window families, and the formant is one of them.** FS1R.unlock's 2026-09-19 skirt
sweep carries sixty partials per step, not the fourteen `skirt.md` tabulated, and period-averaging each
step's take gives the grain waveform itself. On all1 the rise sharpens with the skirt and the fall never
moves; on all2 both do. So the "2" forms are `sin^p` with `p = 2 * 2^skirt` (the binomial rows, as before),
and the "1" forms are the **same exponent on the rising half with the fall held at `sin^2`**. That one
asymmetric window puts all1, odd1 and res1 inside 0.4 to 1.8 dB rms over the lines the take resolves at
every skirt, where no symmetric `sin^p` got under 6. `04_formant_2` says the formant is the same
asymmetric window stretched over its bandwidth: its sixteen skirt segments land at 0.4 to 2.4 dB per
line, level to 0.4 dB, where the `sqrt(2)`-per-step symmetric fit had 2.5 dB of band shape and 4 dB of
level at skirt 7. `WIN_SKIRT_FRMT` is gone; one skirt law, two window shapes. `tools/check_skirt.py`
renders all six harmonic forms at every skirt against the sweep and is the check.

**all1 and all2 are the window alone, read by phase like the sine.** The engine had them as a two-period
grain under a carrier at the fundamental, which is one partial at skirt 0 and 36 to 66 dB wrong above it.
all2's sixty lines are `C(p, p/2 - k)` on every harmonic, which is a one-period `sin^p` pulse train with
no carrier at all; the unit's output carries none of its mean (mean / rms under 0.03 on every take where a
raw pulse train sits at 0.7). So the two are a stored one-period waveform, the window minus its mean,
indexed by the operator's phase, and a modulator reaches them as phase like any other form. all1 and all2
now score 0.5 to 1.8 dB per skirt against 48 to 81 before; `04_formant_3`'s four form segments do not
move, since they sit at skirt 0.

**The drum's note 41 is key code 87, not 86** (`(NOTETAB[41] >> 8) + 10`; the 2026-09-23 entry miscounted).
Between the measured -10 at 85 and -5 at 89, the rounded interpolation gave -7 and the six note-41 hits of
`19_drums` want -8 or -9: envelope shape 8.1 to 1.5 dB rms and level -0.9 to -0.3 dB over the six, against
5.8 to 8.9 and +0.4 to +1.0 at -7, and 13 to 28 at -5. Truncating toward zero instead of rounding gives -8.
`19_drums` 1.25 / 3.03 to **0.81 / 1.92** in level and envelope. The other two of every four key codes are
still interpolated; register 0xC0 driven directly is what reads all 128 (`fs1r_capture_session4.py`).

**The unvoiced "pedestal above 4 kHz" is already closed** and had been since 2026-09-22's two-pole reading:
octave bands of hardware minus engine over eleven wideband unvoiced segments across three files sit at
-0.5 to +0.5 dB from 125 Hz to 16 kHz, including 4 to 8 and 8 to 16 kHz. The 3.1 to 3.7 dB
`analyze_capture.py` still reports as "shape" on the 1 kHz files is its sixth-octave bands below 100 Hz, one
or two FFT bins each at the two floors, which `noise.md` already said; a gate on those was tried and
dropped because it also empties the sparse-spectrum files. The number is the metric, not the engine.

Capture bench, before to after: `04_formant_2` shape 2.52 to **0.44**, `19_drums` envelope 3.03 to **1.92**
and level 1.25 to **0.81**, `06_filter_2` envelope 2.52 to **1.78**, `03_fm_1` shape 0.19 to 0.01, `14_sens`
level 1.66 to 1.17; `04_formant_3` reads 0.73 to 2.14 on one segment (`f0-note72`, six bands, a 1 kHz
formant three partials wide at a 523 Hz fundamental) while its other 24 hold. Nothing else moved. Demo,
effects in: mean|err| 5.23 to **4.92**, tilt 2.06 to **1.96**, worst envelope correlation 0.902 to **0.908**.

## 2026-09-23, the plugin's voice Note Shift parameter defaulted to -24

rgwan: some performances play an octave off in FSVR. The engine's pitch path is the firmware's
(`FUN_00010DC4`: system, performance, part and voice shifts each clamped in turn), and every ROM
performance renders at the pitch its bytes ask for. The plugin was the culprit: `gen_parameters.py`
reads each parameter's centre out of the Data List's range text, and the voice common Note Shift is
the one row written `(-24-0-+24)` with hyphens where every other bipolar row uses `~`. It parsed as
unipolar with a default of 0, which is -24 semitones, and a host or preset that reset the parameter
to its default, or a GUI that showed it, dropped the voice an octave (two octaves against a part at
-12). The range parser takes either separator now; the JSON changed on that one parameter only.

## 2026-09-23, the DX7 conversion was a guess, and B024's hiss was its algorithm map

rgwan: B024 Velvet Dyno hisses at high velocity. Rendering its two parts alone puts the hiss on part 2,
BrightEP 1, a PrC voice, so a DX7 conversion, whose spectrum at velocity 127 in the engine ran flat to
16 kHz. The engine's `convert_dx7` mapped DX algorithm N to FS1R algorithm N + 8, placed DX operator
6 - j on FS1R operator j + 2, and took every EG time as `99 - rate`. The firmware's converter is
`FUN_00035918` (the common block) and `FUN_00035E96` (one operator), driven by a 33-word row per DX
algorithm at EPROM `0x389F8C` (`DX7MAP`):

* words 0..15 are the FS1R connection words, and word 16 the FS1R algorithm. N + 8 holds for DX 1 to
  23 and for 25 to 28 (+7); DX 24 is FS1R 7, 29 is 6, 30 is 36, 31 is 8, 32 is 1. BrightEP 1 is DX 30:
  the engine had it on FS1R 38, which puts its feedback on the operator DX left silent and its chain
  in the wrong order;
* words 26..31 say which FS1R operator each DX operator 6..1 lands on, and it is not j + 2 for eleven
  of the 32 algorithms (DX 10, 11, 12, 13, 23 to 32);
* words 17..22 add 2 to the output level of the operators the row marks, and the row's t1 words carry
  the carrier level corrections (bits 3..6), which the engine had left at zero;
* the EG times are `99 - TAB[rate] - rate - term`, with `DX7RATE_A` (`0x38A84C`) on the attack and
  `DX7RATE_B` (`0x38A8B0`) on the rest, and the term `rs * 1386 / 504 + (99 - OL) * 4 / 10 + 6` on
  the attack, `rs * 1386 / 504 + (99 - OL) * 2 / 10` on the others, rs the DX rate scaling and OL the
  output level after the +2. The rate scaling also lands on `tscale` as it is;
* the DX AMS lands on both the AM sensitivity and the EG bias sensitivity nibble unscaled (the engine
  doubled it into AM only), the detune is `dx + 8`, and the two FS1R operators no DX operator reaches
  get the firmware's template (bandwidth 20, times 20, level 0).

BrightEP 1 at velocity 127 now rolls off 86 dB by 4 kHz where it was flat. The fixed-frequency
conversion (`FUN_00035658`) is software float and the engine's `log2(hz / 440) + 16` is kept.

## 2026-09-23, a part whose voice bank is off receives nothing

rgwan: A011 Sho plays parts 1 and 2 on the unit and all four in FSVR, with parts 3 and 4 sounding
their InitEP voices. The performance's parts 3 and 4 have Voice Bank = off (part byte 1 = 0) and
part 4 a receive channel of 16 (byte 4 = 0x10, "pfm"). The engine took the receive channel as the
whole of whether a part listens, and loaded whatever voice was in the part's buffer. The owner's
manual, page 63: "When Voice Bank = off no MIDI data will be received for the corresponding part."
`Part::rcv()` now reads as off when the bank is off, which is the one place `part_listens` and
`partActive` both go through; the panel shows "off" for the voice. Seven of the 384 ROM performances
have a part like this: Sho, Replicant, SuperPad, Strat 7II, Ice Score, Angel Bells, Ensemble.

## 2026-09-23, the filter EG's rate law off the CPU's own stage word: exponential, doubling every 15.5 rate words

`fs1r_capture_session3.py flteg` rerun after the gate fix, twelve notes, the CPU's stage word at
0x0106ADEC sampled every 26 ms. The attack segment, L4 = 0 to L1 = 100 (a swing of 512 level units),
at times 10 to 80:

| time | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 |
|---|---|---|---|---|---|---|---|---|
| rate word | 119 | 134 | 149 | 164 | 179 | 194 | 209 | 224 |
| attack, s | 0.052 | 0.078 | 0.129 | 0.233 | 0.418 | 0.806 | 1.565 | 2.994 |

`t = 1.33e-4 * 2^(word / 15.5) + 0.024 s` to 1.2% rms, the 24 ms being the sampler's own offset. Time
0 (word 11) and 99 (word 253) fall outside the sampler: the first ends inside the first sample and the
second had not ended at six seconds, 11 s by the law. With the asymptote half a swing past the end the
segment covers two thirds of the way to it, ln 3 time constants, so the chip's per-tick approach is
`42.9 * 2^(-word / 15.5)` at the 192.3 Hz tick (`cal::FEG_RATE_K`, was a 0.67 guess on a `/ 32` law).

**Exponential, not a ramp.** Time 40 at L1 = 25 (a swing of 128) took 0.232 s against 0.233 at L1 =
100 (a swing of 512): the time does not depend on the swing, which is what an exponential aimed
past a target proportional to the swing gives and a linear ramp cannot.

**A flat segment is a fixed 0.78 s.** Stage 2 in every note is L1 to L2 = L1, no swing, and it held
0.71 to 0.86 s at every rate word from 11 to 224, so the CPU's +2/+3 words are not what the chip
times; it takes 0.78 s over a zero swing regardless (`cal::FEG_FLAT_S`). Time 40 at L1 = 50 (end word
0, the same as L4) showed no attack because there was none to show: L1 = L4 is a zero swing. An
earlier reading of that note as "an end word of 0 ends at once" went into the engine and snapped Fat
Line's decay to L2 = 50 shut, 5.20 to 7.40 dB on the demo set; reverted.

**LFO2 never touches the staged cutoff word.** The `lfo2` run sampled the part's slot at 0x01068F78
under LFO1 filter depth 99, under LFO2 depth 99, and under neither, 115 samples each: no slot moved
in any of the three. LFO1's filter term is the CPU's and goes out by a different path (the `+0x28`
offset word FUN_0002E6CC refreshes, not the staged coefficient), and LFO2 is the chip's own, which
`20_fltmod` measured the rate of. The engine had both right in structure.

`06_filter_2` envelopes 2.38 to **2.52**, `14_sens` 3.21 to **3.14**; what is left in both is the
patch's amplitude onset (the unit's first 100 ms sit 10 to 30 dB above the engine's on every filter
EG segment, EG depth or not), not the filter EG, which now opens at the same millisecond. No GUESS
constants remain in `cal.h`.

## 2026-09-23, session 3's take: the filter EG depth and LFO2's rate measured, three files already inside 0.1 dB

rgwan ran `fs1r_capture_session3.py` and recorded the six outstanding request files as one take
(`FS1R.unlock/captures/2026-09-23/16_vel_to_20_flt.flac`). `tools/split_take.py` cuts a take like that
into one WAV per file by each file's own marker bursts; all six landed with their spans to the hop.

**Three of the six needed nothing.** `16_velocity_1/2` (63 segments, the velocity law at seven amplitude
sensitivities) sit at 0.06 dB median level error, so `vel_att` as read from the firmware is the chip's.
`17_egdecay` (a decay to a level that is not silence) 0.09 dB level, 0.43 dB envelope. `18_fmchain`
(the second and third links of a modulator chain) 0.05 dB level, 0.15 dB shape: every link carries the
same index. The drum's tonal half was not any of those.

**The filter EG depth scale is 110 cutoff bytes** at full depth and full level (`cal::FEG_DEPTH_BYTES`,
was a 96 guess). Six held-level segments at cutoff 96: the corner reads 13.1, 25.5 and 52 bytes down
for depth words -8, -16 and -32 at level 250, and 52 down for level -128 at depth 63, so the shift is
linear in both and 1.15x what 96 gave. Per octave band the closed corners now sit within 2 dB of the
unit's down to its own -72 dB leakage floor. `06_filter_2` envelopes 2.47 to **2.38**.

**LFO2 is a 16-bit phase stepped at 3000 Hz.** Six speeds; the modulation fundamental off the envelope
spectrum is 0.95, 1.91, 2.70, 4.76, 10.64 Hz for `LFO2SPD` words 20, 40, 60, 104, 232, which is
`word * 3000 / 65536` within 4% at every one, 3000 being 48 kHz / 16, one step per 16 frames.
`cal::LFO2_INC_K` carries it; the engine's guess had been 192.3 Hz ticks, six times slow. Speed 127
(word 556) reads 43.2 Hz where the law gives 25.4, and is left unexplained: it is one point, past the
table's last measured step, and a sweep over 100 to 127 would say whether the word or the chip bends
there. `20_fltmod`'s LFO envelopes 19.5 to 9.2 dB; what is left in them is the waveform's shape against
the analyzer's alignment, not the rate.

**The drum, `19_drums`.** Note 60 hits match to 1 dB at every velocity and the noise-off / voiced-off
halves separately. Note 41 is 2.6 to 3.0 dB quiet in the engine at every velocity, and the envelope
says why: its voiced half decays about 10 dB further than the unit's by 300 ms while note 60's tracks
the unit to the decibel. Note 41 is key code 86, between the measured 85 and 89 of the key-code table
in `aeg.md`, so the interpolation there is the suspect, and `10_envelope2`'s `tkey` grid at one note
per octave never played it. Open.

## 2026-09-23, AM sensitivity: the chip's AM word is seven bits, and the sensitivity weights are eighths

Off `14_sens` as recorded, no new take. The CPU's AM word is `EGBIAS[amd * (127 - lfo) >> 8]`, 0 to 255,
and the engine applied it as `regAM * LEVEL_DB * ams / 7`, which is why it matched the unit at LFO
depths 33 and 66 (swings of 18 and 49 steps) and ran 20% deep at 99 (a swing of 224). Per-cycle
peak-to-trough on both sides through the same 2 ms estimator, in dB, eight sensitivities:

| depth | | ams 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|---|
| 33 | unit | 0.4 | 1.2 | 2.0 | 3.5 | 4.2 | 5.1 | 5.9 | 6.7 |
| 66 | unit | 0.4 | 2.6 | 4.6 | 9.1 | 11.3 | 13.5 | 15.8 | 18.1 |
| 99 | unit | 0.4 | 7.8 | 13.8 | 23.6 | 29.6 | 35.5 | 40.9 | 46.1 |
| 99 | engine, `ams / 7` | 0.4 | 6.9 | 13.7 | 20.6 | 27.5 | 34.3 | 41.2 | 48.0 |
| 99 | engine, now | 0.4 | 6.0 | 11.9 | 23.8 | 29.7 | 35.6 | 41.6 | 47.6 |

Two things fall out. The 66 row divided by its 49-step swing is 1, 2, 4, 5, 6, 7, 8 eighths to the
step, not sevenths: the sensitivity is a 3-bit shift-and-add, {0, 1, 2, 4, 5, 6, 7, 8} / 8, and the
jump from 2 to 3 (4.6 to 9.1 dB) is the unit's, not noise. And the 99 row tops out at 46 dB where
224 steps would be 85: the chip's AM word is seven bits, 127 steps, 47.8 dB, and the CPU's 8-bit
table runs off the end of it. `am_att()` in `internal.h`; the linear `ams / 7` is gone. What is left
is 1.8 dB at sensitivities 1 and 2 at depth 99, where the unit modulates a little deeper than
`127 / 8` and `127 / 4` allow, inside the estimator's smear on a trough that steep. `07_modulation_1`
envelopes 2.27 to **2.21**, `14_sens` 3.28 to **3.24** (its number is the filter EG's).

**Three small ones off the same pass.** The feedback gain: `03_fm_2`'s six clean feedback levels put
the second harmonic a steady 0.5 dB under the unit, so `cal::FEEDBACK` is 0.53, and levels 1 to 6 now
land to 0.1 dB. The Fseq MIDI-clock rate table was marked inferred and is the firmware's:
`FUN_0001ACB6` scales the clocks since the last frame by 1/4, 1/2, 1, 2, 4 for speed words 0 to 4.
And the analyzer's silent-segment fault: a float render's residue after an EG has run to its floor sits
at -105 to -115 dB through the output stage, above the -120 gate, so `hold-50`/`hold-70` in
`10_envelope2` and `hold-60`/`hold-80` in `02_envelope_3` scored 30 to 38 dB of disagreement on
silence. `SILENT_DB` is -100, 70 dB under anything that plays; those four rows are gone.

## 2026-09-23, the firmware audit: the filter EG is the CPU's, LFO2 and the Fseq delay were not what the engine had

A pass over the engine against the flash with Ghidra's decompiler, readonly-folding the EPROM so the
tables resolve, and with `FUN_00203000` given its real signature (the signed divide, `r1 / r0`, which the
cached decompilations had lost so every `/ 889` and `/ 507` in the filter path read as a call). Nothing
here is from a recording; each item names the function that settles it. The engine edits are in
`vop3_filter.h`, `notes.cpp`, `fseq.cpp` and `cal.h`; the scores that moved are
`06_filter_2` envelopes 2.68 to **2.47**, `14_sens` 3.34 to **3.28**, `08_panlevel_1` shape 1.71 to
**1.60**, nothing else changed.

**The filter EG is stepped by the CPU, one segment at a time.** `FUN_0000D050` (note on) writes the
segment to VOP3-1 through the register primitive `FUN_00039648(idx, word)` = `*(u16*)(0x800000 + 2 * idx)`:
register 0x2B is the rate, `0x374F4E[time]` (`FEGRATE`, 100 words, 0x0FFF down to 0x0016), 0x2C is the
end level `(L - 50) * 256 / 50`, 0x2D is the asymptote, the end level plus **half the swing again**, so
the chip aims past the target and the CPU cuts the segment when the chip's status word at 0x800242
reports the crossing; 0x21 starts it. `FUN_0000CB6C` runs on that interrupt, advances the channel's stage
word at 0x0106ADEC (1 attack, 2, 3, 4 hold, 5 release from `FUN_0000D7B0`, whose release aims at L4 with
the same law) and loads the next segment. When the swing is zero the asymptote is the target plus 3 (plus
2 on the end word), so a flat segment still ends. The engine's `StepEG` had four fixed time constants
off the amplitude EG's rate table and a `FEG_STEP_K` fraction; it now has the firmware's structure, and
the one thing left inferred is the chip's reading of the rate word (`cal::FEG_RATE_K`, a two-point
guess, marked). FS1R.unlock's `capture3.py flteg` reads that off the stage word directly, and whether
the chip is exponential or a ramp with it.

**Everything else on the filter path was a different formula.** All in `FUN_0000D050` / `FUN_0000E170`:

* cutoff key scaling is `(ks - 64) * (note - breakpoint) * 16 / 507`, the engine had `/ 64`;
* resonance velocity is `sens * (vel - 127) * 116 / 889` for a positive sensitivity and `vel * 116 / 889`
  scaled by the sensitivity for a negative one, the engine had `(sens * (vel - 64)) >> 4`;
* the EG depth velocity is multiplicative, `depth * sens * (vel - 127) / 889` added to the depth, the
  engine added a velocity term to the level;
* the EG depth word is `0x120 * depth` (register 0x28); what the chip does with it against the level is
  inside VOP3-1 (`cal::FEG_DEPTH_BYTES`, a guess, see `20_fltmod`).

**LFO2 is not the CPU's.** `FUN_0000D050` and `FUN_0000E2A0` hand VOP3-1 a waveform (`FUN_0000C1AC`, the
0x20-per-step field `ymp706_registers.md` had as the filter type) and a speed word from `0x374A04`
(`FUN_0000C130`, `LFO2SPD`, 128 words), and nothing on the CPU side adds an LFO2 term to the cutoff
coefficient. The engine had run LFO2 as a copy of LFO1 with the same speed table; it now uses
`LFO2SPD` and the chip's rate per word is `cal::LFO2_INC_K`, a guess. The filter type reaches the chip
another way: `FUN_0000C280` / `FUN_0000C604` patch the per-channel microcode jump. `capture3.py lfo2`
confirms the cutoff word holds still under LFO2, and `20_fltmod` records the rate.

**The voice's Formant and FM control** (`FUN_00017454`, handlers at 0x3DE04): the amount is
`clamp(bias(src) * bias(dep) * 2 >> 7)` like every other controller, and the destinations store
`-2 * amt` as a level offset, `amt << 5` as a frequency word offset, and `amt` into the per-op bandwidth
offset that `FUN_0001F6BC` scales by the op's own `BWBIAS` as destination 37 does. The engine had a quarter,
an eighth and a quarter of those, and `cal::VCTRL_FREQ` for a scale that is a shift in the flash.
Removed.

**The Fseq start delay** is `0x35BD3E[byte]` (`FSEQDLY`, 0 to 346) frames of a fixed 7000-count CMT1
period, 8 ms, counted down by `FUN_0001A47A`, and the real frame rate starts when it runs out: 2.77 s
at 99. `cal::FSEQ_DELAY_S = 1.0` was wrong in unit and value. Removed.

**The pan LFO depth** goes through `eb86()` (`FUN_00019362` case 0x29 stores it at image +0x1F, 1 at
byte 0); the engine used the raw byte, 22% shallow at the top.

**The reverb's dimensions are not a tap pattern.** `FUN_0039B09C` loads the block: coefficient rows at
`0x363600 + 0x364F10[type] * 0x128`, 0x5E coefficient words to the slots at `0x35E02C` and 0x36 delay
words, in samples times `0x7AE1 / 65536`, to the slots at `0x35E0E8`, and `0x36FEB4` is the eight-row
width/height/depth table the room types index. The engine's early reflections are an invented six-tap
spread (`run_reverb`, marked INFERRED). Not changed here: it is a rebuild of the block from the
microcode, `vop3_2_microcode.md` territory, and the 09_effects files are the ones to score it with.

**Still inferred, and what settles each**, in `FS1R.unlock/captures/capture3.py` (reads only) and
`captures/requests/20_fltmod.mid` (one recording, 2.3 min):

| constant | what | how |
|---|---|---|
| `FEG_RATE_K` | chip time per filter EG rate word, and exponential vs ramp | `fs1r_capture_session3 flteg`: the stage word through 14 notes |
| `FEG_DEPTH_BYTES` | cutoff bytes per EG level per depth word | `20_fltmod` held-level segments, cutoff parked at 96 |
| `LFO2_INC_K` | LFO2 phase per tick per speed word | `20_fltmod` lfo2 segments; `capture3 lfo2` proves it is the chip's |

## 2026-09-22, the noise band is two unequal poles, the drum is not the noise, and the effects-off demo

rgwan recorded the fifteen demo songs with the effects and the filter stripped (`captures/demo_nofilterfx`), plus Vokodrone's bass and drum parts on their own. Against the whole take the engine sits 2.8 dB under the unit in the median band, flat across the octaves, with four songs at zero and Ana-Unison at -8; the top octave, +3.3 dB bright on Vokodrone, is the one band the day's work moved, to -0.6.

**The snare is the two sine carriers, not the noise.** `tools/compare_isolated.py` lines the drum take up against a render of channel 3 alone and scores each note by band. The drum part reads 1.5 to 3.3 dB hot on every note and the snare 4 dB hot in the 100 to 200 Hz band, which is operators 6 and 8, sines at 143 Hz under a three-deep modulator chain and a feedback operator. Muting every unvoiced operator costs the engine's snare half a decibel. Time-frequency slices say where: for the first 6 ms the unit's carrier line is 12 dB down where the engine's is already clear, so the unit's modulation is deeper early and decays through the first 10 ms, and the engine's modulator, whose EG drops 99 to 80 at rate 51, is already down. Nothing in the capture set measures a decay to a level that is not silence, the velocity law at any sensitivity but 0, or a modulator on a modulator, and `tools/make_capture_tonal.py` writes the four files that do, the fourth being the drum voice itself one hit at a time. FS1R.unlock `fs1r_capture_session3.py record` plays them.

**The noise band is two digital one-poles with different coefficients**, and the "pedestal" of 2026-09-21 is the second pole's own floor: a digital one-pole passes `(a / (2 - a))^2` at Nyquist, which is white, sits at the same level at any centre and rises with the register twelve decibels an octave. Fitting both poles free reproduces every one of forty-one segments to 0.6 to 0.9 dB rms over 200 Hz to 23 kHz, and the 1 kHz take gives the same coefficients as the 8 kHz one at every register they share. The peak falls one `LEVEL_DB` per register from 25 up. The skirt is two controls rather than a register shift. The unvoiced output is also averaged two samples at a time, which the fixed sines of `01_reference` do not show and every wideband noise segment does. Band shape over 100 Hz to 22 kHz: `13_unvoiced3` 2.23 to **0.61**, `05_unvoiced_1` 2.65 to **1.19**, `15_filter`'s source 2.16 to **0.52**. `docs/noise.md`, `tools/fit_noise_band.py`.

**The bass part** is 2.2 dB quiet at every velocity and its formants sit in the wrong octaves, -5 to -8 dB at 160 to 320 Hz and +5 to +10 at 640 to 2560 Hz on notes 40 to 57, where the window is shorter than the period. That is the formant window family, item 8 of `docs/fidelity_plan.md`, seen at a fundamental the capture set never played.

**Two scoring faults, still open.** `tools/analyze_capture.py`'s shape number on the noise files is carried by its sixth-octave bands below 100 Hz, one or two FFT bins each at the recording's floor, which is why it moved 3.3 to 3.1 where the same files moved 2.65 to 1.19 on bands within 40 dB of the peak. And `tools/regress.py` needs `bin/fsvr_console.exe`, so it cannot run outside Windows.

## 2026-09-21, the filter and the noise formant both come off the inferred list

`15_filter` is the first recording with a source that can see a filter. `06_filter` never could: its source is a single 32.7 Hz partial, so its eleven readings had nothing above the corner to attenuate. The corner is `17.4 * 2^(byte / 12)`, 0.098 octaves rms over thirteen points, where the one-pole reading of the coefficient had put byte 64 at 3751 Hz against the unit's 713. The resonance runs to a 19.9 dB peak where the engine topped out at 8.5, and the feedback goes as the cube of resonance table A. `RESO_COMP` is measured at zero rather than guessed. `docs/filter.md`.

`13_unvoiced3` settles the noise formant chain: the band sits at an 8 kHz centre where it does not fold at DC, and the corner, its clamp, the skirt, the level and the resonance are all measured. `docs/noise.md`.

## 2026-09-20, an Fseq frame does not cut the grain it lands on

rgwan reported clicks before 00:06 in Vokodrone that his unit does not make. The intro from 2.0 to 5.5 s is part 4 alone, eight formant operators under a preset Fseq. A grain carries the level it was fired with, so a frame that lands mid-grain takes effect on the next one rather than chopping the one in flight.

What the demo cannot separate is whether the chip latches per grain or slews the level register on a 3 to 5 ms time constant, which fits the intro nearly as well. The lever is the grain rate, one period of the fundamental, which is what `12_fseqlevel` was built to drive.

The first take of that file came back silent, which found an invented fallback: eleven of fourteen segments recorded at the digital floor and three as steady noise, because the performance left the Fseq unassigned and the engine had been substituting something the hardware does not.

Take 2 came back on 2026-09-21 and the grain latch was too slow. The same take's three sine segments also caught a separate fault: a ratio operator handed an Fseq frequency runs off the end of the register, one line at 23982 Hz at notes 36, 60 and 84 alike, which is pitch word 32767 to a tenth.

## 2026-09-19, the spectral skirt multiplies the window exponent

The grain window is `sin^p`, and the skirt multiplies p rather than stepping it: `p = 2 * 2^skirt` on all/odd/res and `2 * sqrt(2)^skirt` on the formant, where the engine had `2 * (skirt + 1)` for everything.

The proof needs no fit. `res2` puts its grain in exactly one period under a carrier on partial 9, high enough that no line folds back, so the lines it produces are the window's own Fourier coefficients, and a `sin^(2n)` window's coefficients are the binomial row. The unit gives 1, 2, 1 at skirt 0, then 1, 4, 6, 4, 1 at skirt 1, then the sin^8 and sin^16 rows, 0.00 to 0.35 dB rms against the exact coefficients over skirts 0 to 5. The engine lands inside 0.9 dB over all eight settings of both.

This also closes what separates all1 from all2, odd1 from odd2 and res1 from res2, which had stood as "identical at every setting measured". They are identical at skirt 0 and four to five times apart by skirt 7, and every segment of the capture set sat at skirt 0. `docs/skirt.md`.

## 2026-09-19, the amplitude EG's shape, and four seconds of Full Tines

The 0918 pass measured the rate table and stopped there, so everything the rates are used for was still a DX7 guess. Three were wrong.

Full Tines lost four seconds of its tail. The song holds the sustain pedal down and the hardware rings for four seconds after the last note off where ours rang for one. It was never the pedal and never the rate curve: the engine's key rate scaling was wrong by 11 to 16 chip steps at every note, always too fast, which is 7 to 16 times the decay time. With the law measured off `02_envelope_3`, the engine's last audible frame moves from 14.02 s to 17.57 s against the unit's 18.28, and the song's envelope correlation from 0.838 to 0.976, from the worst of the fifteen to above the median. 0.7 s of tail is left.

The key code law is a saturating table rather than a line: ten key codes four apart bound it to one value each, and it stops at plus or minus 13 at both ends. `docs/aeg.md`.

## 2026-09-19, the modulation index, and the alignment fault that got it wrong twice

One cycle of phase deviation at full modulator level is the DX7-lineage guess. Three is what the fifteen songs ask for. Mean absolute band error: 8.3 dB at one cycle, 6.9 at two, 6.3 at three, 6.4 at four. Raising the feedback alone to the same depth recovers only a third of it, so it is the operator-to-operator index and not the feedback path.

The sideband sweep that was meant to settle it gave 4.0, and that was wrong too. The analyzer had been lining each sweep up on where its tone begins, and `sweep.py` brings its note up 1.55 s before the first step writes anything, so every spectrum was scored against its neighbour's register value. A ladder is shift invariant in slope, so `LEVEL_DB` came out right and nothing looked wrong. Anchored properly the answer is **3.369**, and a second take agrees. `03_fm` settles it independently and was never being read: its 25 spectra sit 4.46 dB out in band shape at 4.0 and 0.25 dB at 3.369.

This is the lesson behind the rule that a number fitted against one recording is not measured. Three conclusions in two days were written up with a residual quoted and then turned out to be wrong, all failing the same way.

## 2026-09-19, the filter is a ladder, and the demo cannot tell

`FUN_0000C3D0` sends two resonance coefficients per channel, not one. With `r = 2^(-raw/16)`, table 0x374B24 is `A = 1 - r` and 0x374C24 is `B = max(0, 0.5 - 2r^2)`, quadratic in the same damping and zeroed for HPF and BEF, and `FUN_0000CA44` gives only LPF18 and LPF12 a tap mix and a doubled input. One shared cutoff with a feedback term and an input-side term is a ladder, so the three lowpasses became taps off one 4-pole cascade instead of three separate topologies, and the invented "second pole pair runs flat at 0.7" is gone.

Against the recording it is a wash, 5.55 dB where the old model scored 5.50. Two settings inside it did get settled: spending table B on the passband reads as a flat +4 dB across every band, so `RESO_COMP` is 0, and the textbook self-oscillation point `LADDER_K` 4 oversharpens the peak. The structure is kept because it is what the firmware's coefficients describe, not because the demo prefers it.

## 2026-09-19, two scoring faults that were hiding the real numbers

`tools/analyze_capture.py --compare` scores spectrum shape now, the octave bands with each segment's own level divided out. That is what caught the modulation index, and nothing had been reading it. It put numbers on three open items at once: `04_formant` at 6.3, 7.5 and 17.2 dB, `05_unvoiced` at 3.8 and 6.6, and `06_filter` at 14.7 dB on both files, where those same two files matched to 0.00 dB in level.

`captures/demo/render` was stale and `tools/demo_scores.txt` with it. Fourteen of the fifteen files predated the engine and came out 3.9 to 14.4 dB quieter when re-rendered, so the 5.50 dB line described a build that no longer existed. Re-rendered and the log restarted from a measured before and after.

## 2026-09-18, the first capture set, and two tables read out of the EPROM

rgwan recorded the whole capture set, 21 files in one 46.7 minute take off the digital output board, and ran the debug monitor over it reading the voice image back for all 492 segments.

**The frequency EG depth was over twice too strong at mid settings.** The sysex byte reaches register 0x60/0x68 through a table at EPROM 0x35B2E0, now extracted as `FEGLVL`, and it is far from linear.

**The pan table index was one step left of the hardware's.** The voice image carries twice the part's pan byte at +0x2E and `FUN_00025BC8` indexes at `pan >> 1`, so the index is the part byte itself.

Measured and now in the engine: the output path's fixed gain, `cal::OUT_GAIN` 0.14992, agreed to 0.005 dB by nine segments, and the channel accumulator's hard clip, recovered identically from four and eight stacked carriers.

Confirmed as they stood: the EG rate model to 0.6% over rates 16 to 33 across 38 measured rates, `EG_LEVEL_DB` 1.5 to 1.5039 over forty steps, `CARRIER_DB` 1.5 straight over sixteen steps.

## 2026-09-18, the extracted demo MIDIs ran 4 % fast

Most of what was holding the envelope correlations down. The demo player's timer ISR sets its next compare from the running count rather than from a fixed period (flash 0x0002F28C), so every period carries the interrupt's own entry cost. The recording measures that at 4 %: song to song the distance in rgwan's take is 1.040 times the nominal length, on all fifteen, with no reload gap left over. `tools/extract_demo.py` now writes a 520000 us quarter, 10.4 ms per delay unit.

## 2026-09-16, five CPU-side spots that were read but not certain

All five were marked "assumed" because the firmware had been read but not far enough. Tracing them settled each one.

**The controller matrix was wrong in five ways at once.** The source bit order (PB, CAT and PAT sit at bits 6 to 8, with FC, BC, MC3, MW and MC4 after them, not at the end), the source values (the knobs and MIDI controls are bipolar `(v - 64) * 2`, not raw), and three more.

**Frequency bias** (destination 36) was routed but never applied: `(int16)(scaled * sense * 0x10) >> 2` on the operator's frequency word.

**The part EG offsets** reach T1, T2, **T3** and T4, not T1, T2 and T4. `FUN_00019414` walks the part's bytes 0x1A, 0x1B, 0x1B, 0x1C. The hold rate takes its +4 only when it is below 0x3F.

**The Fseq pitch offset** is exact, constant for constant, against `FUN_000127FC`. The formant tracking term that used to sit alongside it was an invention and is gone: nothing in the firmware shifts a frame's formant frequencies, which is the point of an Fseq.

**MIDI clock** weights each tick by 25, 50, 100, 200 or 400 for speed words 0 to 4 (`FUN_0001ACB6`), so the 1/4 to 4/1 ratios were right, and the free-running `VELW[adjust] * 84 * 1000 / ratio + 2884` is confirmed instruction for instruction.

## 2026-09-16, four engine faults found by driving Vokodrone against the unit

Written as the v0.3.2 release notes, which were never published in full. The shipped release body carries only the title and the SHA-256 lines, so this is the only copy of the working.

The FS1R plays fifteen demo songs out of its own EPROM, and `tools/extract_demo.py` pulls that byte stream out as standard MIDI files, so the hardware and the engine are driven by one sequence and can be lined up sample for sample. Song 1, "Vokodrone", is the demo's hardest case on purpose: four parts, a formant sequence on part 4, and a foot controller and a mod wheel driving six controller sets for fifty seconds, 1123 and 901 messages of them. It was the worst of the fifteen against the recording, bright by 8 to 15 dB above 320 Hz and out by 19 dB over its closing phrase. Isolating its parts put the fault in the engine rather than in a calibration constant, and the firmware names each one.

**An Fseq that had played out blasted its operators at full level.** The operators a formant sequence drives had their own level forced to zero attenuation on the assumption that the frame would overwrite it, and when the sequence stopped advancing nothing did. The frame writer is gated on the mode byte and the part, not on the sequence still running, so the registers hold the last frame; only the start delay gates it, by clearing the track masks while the delay counts down. Any patch whose Fseq reaches its end and stops was jumping to full volume on its sequenced operators.

**The Fseq retrigger test looked at every part.** "First note" means the Fseq part's own held-note count, which the firmware keeps per part and tests for 1. The engine scanned all 32 channels instead, so any other part holding a note stopped the sequence restarting and it ran on out of step with the keys.

**The frequency bias controller detuned operators the hardware leaves alone, and then froze.** The firmware adds the frequency bias word only to formant and fixed operators; the ratio branch adds nothing, and the unvoiced one is zeroed outside normal mode. The engine added it everywhere, so a mod wheel swept Vokodrone's bass operators by up to an octave and a half that the hardware never moves. Worse, the whole controller matrix was read once at note-on, so the frequency bias, both bandwidth biases and the amplitude EG bias were frozen for the life of a note, while the firmware re-runs the destination handler whenever one of its sources moves and rebuilds the per-part arrays. Vokodrone holds a thirty-second chord under a moving foot controller. All of that now runs on the 192 Hz tick, and the Fseq frame's own frequency words carry the bias as the firmware adds it.

**Controller sets do not compose.** Every "direct" destination handler stores a per-part byte rather than adding to one, and the eight sets are walked in order, so two sets pointed at one destination overwrite each other rather than summing, exactly as for the destinations that edit a part byte.

**What it measured.** Across all fifteen demo songs the mean absolute per-octave-band error against the unit went from 6.28 to 5.50 dB and the median envelope correlation from 0.956 to 0.965. In the median over the songs, every band from 640 Hz up sat within a decibel of the hardware. Vokodrone itself went from +8 to +15 dB bright to inside 5 dB in every band, from 0.956 to 0.978 correlation, with its per-second level tracking the unit to 1.5 dB across the song. What was left concentrated in four songs and in the top octave, where the engine is still dark because it models neither the phase truncation nor the level quantization of a fixed-point chip.

**What it did not settle.** Calibration against a measurement rig. The demo recording is real hardware and moved the modulation index, the resonance curve, the filter's cutoff reading and the four faults above, but it never holds a note still, so the chip-side models (EG shape, formant window, filter, effect algorithms) stayed inferred. The capture kit added in v0.3.0 exists to settle them.

The v0.3.2 downloads and their SHA-256 lines are on the [release itself](https://github.com/musicastudio/FSVR/releases/tag/v0.3.2). The release predates the FSVR rename, so its artifacts carry the `FS1R.emu` name.
