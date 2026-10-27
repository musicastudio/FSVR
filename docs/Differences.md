# Where FSVR differs from the FS1R on purpose

Everything else in this repository is an attempt to match rgwan's unit, and a disagreement with it is a
bug (`AGENTS.md`, "Where a claim comes from"). This file is the short list of places where FSVR knowingly
does something the hardware does not. Each entry says what the unit does, what FSVR does, where the firmware
says so, and how to get the unit's behaviour back.

## Toggle Filter Effects on Playing Notes
On a real FS1R if the filter or effects is turned off while notes are being played, they remain enabled on those held notes. In FSVR, this toggle is immediately applied to held notes.

## 32 filtered voices

**The unit.** The board has one VOP3-1 for the voice filters, and it is wired to the tone generator's first
sixteen channels only. So while any part has its filter on, the note allocator (`FUN_000112c0`) stops walking
all 32 channels and walks two halves instead, each with its own cursor: parts with their filter on get
channels 0 to 15 (`FUN_0000fa5c`), parts with it off get 16 to 31 (`FUN_0000fcfa`). The filtered parts
share 16 voices between them, and a 17th filtered note steals one even when the other half is idle; the
unfiltered parts are capped at 16 the same way.

**FSVR.** FSVR does not emulate the filter chip: it runs what the chip's program computes, one resonant ladder
per voice (`fsvr::filters::VoiceFilter`, bit for bit, pinned by `dsp_check`). A voice is as cheap to add as a struct, so every one of the 32 channels has one.
The firmware's filter host drives sixteen slots, so the 32 voices are two banks of sixteen, each with its own
host, EGs and LFO2s (`Filter::Bank`); channel n is slot `n & 15` of bank `n >> 4`. Both banks run the same
host and calibration; nothing about the second needed a new constant.

- **Allocation.** With both chips there is nothing to split: the allocator walks all 32 channels with the one
  cursor and the stealing rules it uses when no filter is on.
- **Routing.** A note is filtered when its part's filter switch is on, whichever channel it lands on
  (`Chan::filtered`). A channel of a part with its filter off goes straight to its part's bus, bypassing the
  filter. Note on, note off, the cutoff modulation every tick and all-notes-off go to the slot in the note's own
  bank.
- **Ring-out.** Once its note ends, a filtered channel keeps passing through its slot, so the filter rings out
  as on the unit. When a note from a part with its filter off takes that channel, the slot is silenced first,
  the same way the unit silences a slot before a filtered note takes it.
- **Cost.** The second bank only runs while a part has its filter on, and only in this mode.
- **A type byte past BEF.** On the unit it points a slot's output step at a register the byte happens to
  name, which is another voice's state rather than a filter; FSVR plays silence there. No voice holds one.

It is the default. Options > Special > 32 Filtered Voices switches it off (`Device::setFilterPoly32(false)`,
`-flt16` on the console); the setting is kept in `Settings.json` for every instance. Off is the unit's 16 and
renders bit-identically to the single-chip engine: all fifteen demo songs were checked. Changing the setting
stops every note, as a filter switch does. Nothing about the second bank can be measured, since the unit has
no second chip.

## A performance change starts the effect section over

**The unit.** The effect chip's state is its own, and a performance change does not reset it. The firmware
silences each window it is about to reprogram (`EffectHost::clearWindow`) by zeroing the window's constants,
re-spreading its delay slots and loading the window's clear program, and that program's whole body is a run of
writes that zero the window's registers: `effects/ins_m14.cpp` zeroes `r[67]` to `r[86]`, `dly_m11.cpp`
`r[87]` to `r[102]`, `var_m11.cpp` `r[103]` to `r[118]`. It can do no more than that. The window's remaining
cells it does not name, and delay memory no chip program can write at all, because the chip addresses that
memory by slot. So as the chip is modelled here, the next effect type inherits both.

**For the cells that is wrong, and the unit says so.** mkruselj switched A073 R.P.M. to A072 Mouth Pop on his
own FS1R with a note held and it is clean, where this model clipped at full scale and the burst was carried by
cells `m[42]` to `m[44]`. So the cells do not survive a window's reload on the unit, by some route the model
does not have: the chip clearing its cell file when a window is uploaded would do it, and so would a clear
program that zeroes more than the one here. Which of those it is is open. The clearing below is therefore
standing in for chip behaviour not yet pinned rather than diverging from it, and `src/effects/` is where the
bug is. The delay memory and the mixer step have not been tried on the unit.

**FSVR.** `Effects::load` zeroes the registers, the cells, the multiplier registers and all 256K words of
delay memory, and then lets the host program the windows for the new performance. A single type change
(`Effects::sync`, the editor) still runs the window's clear program as the chip does, and takes that window's
own leftovers with it.

Carried over, that state is what the new programs take for their own, and it was measured loud. `m[44]` is the
second integrator of the wah's state-variable filter, whose first is `r[74]`, so a window reload zeroes half
of each integrator pair; coming from the Compressor (R.P.M.'s insertion) `m[44]` started life holding the compressor's own cell, which opened
the filter into the overdrive behind it: A073 R.P.M. into A072 Mouth Pop clipped at full scale from a
signal of 0.037. A new echo in the variation window read a second of the previous performance's audio out of
the delay memory: A071 Relaxxx into A072 rang on at 0.30 for a second. Over all 766 switches between
neighbouring factory performances with a note held, sixty stepped more than twice above the level either side
of them and nine more than four times.

Clearing it costs the wet paths the time they take to refill, measured at about 30 ms to 90 % on a
reverb-heavy performance and nothing on the dry path, which reads as the effects fading in rather than as a
click.

**The unit's own sequence does not stand in for this.** The unit cuts the sound before it takes a
performance, all sound off rather than all notes off (mkruselj, on his own FS1R), and the engine does that
too now, in `perf_from_bytes`. Cutting the sound and then waiting - the unit is slow at performance changes,
so the state had a chance to decay before the new programs ran - was measured out to 800 ms and changes
neither carried-state case by a digit: A073 into A072 and A071 into A072 both still clip at full scale. The
delay memory does not flush while waiting either, because the reverb tail goes on writing its own output
into it. The wait fixes only the mixer step, and only by removing the audio there was to step. See
[findings.md](findings.md), 2026-10-17.

**And a fade, for the step the clearing cannot reach.** A load also writes every mixer gain, return, pan and
EQ word of the new performance between one sample and the next, and a gain step on audio that is still
sounding is a click of its own whatever state sits behind it. So the section fades in over 8 ms
(`Effects::FADE_SAMPLES`, a raised cosine), and only when the load finds something sounding: `Effects::level`
tracks the output's peak over the last fifth of a second, so a load into silence does not fade, and a render
that opens by loading its performance does not open with one. With the fade in, none of the 766 switches steps
more than twice above its neighbours and the loudest first 20 ms sits below the level already playing.

## 14-bit pitch bend

**The unit.** The Pitch Bend handler (0x15B80) keeps `(msb - 64) * 16 + (lsb >> 3)`, and the pitch takes that
>> 3 (`FUN_0002022C`), so the wheel moves in 256 steps: at a range of 12 semitones each is 9.4 cents, which a
slow bend makes audible as a staircase.

**FSVR.** The LSB's low six bits, which the unit drops, land between its steps as a fraction of one, so the
wheel has 16,384 positions and the unit's pitch word comes out exactly wherever those bits are 0. The bend as a
controller source is the unit's own -128..127, unchanged. `Device::setBend14(false)` gives the unit's 256
steps.

## Filter cutoff glide

**The unit.** A controller on Filter Cutoff (destination 21) rewrites part byte 0x18 through `FUN_00030796` with
its last argument 1, which sets the filter's dirty flag (`FUN_0000B2E4`, 0x01068F18); the filter task then
recomputes the slot's cutoff word in `FUN_0000DB3C` and `FUN_0000C36C` stores it straight into the filter
chip's coefficient memory (0x01068F78), `0xC0D + 0xA9 * byte`. Nothing on the CPU ramps it and the filter
program reads it as a constant, so the corner jumps a semitone (169 words) at every step of the byte. A
breath controller at depth +63 (23 of the WX5 bank's performances) moves the byte one step a CC, and a slow swell
is a staircase of semitones (FSVR#56, reported by TheBreathalyzer). rgwan's unit confirms it
(FS1R.unlock `captures/2026-10-26-cutoff`): a jump of the wheel on cutoff opens the filter in 1.1 to 1.9 ms, as the
engine's step does, where the glide takes 2.8 to 3.0 (`docs/findings.md`, 2026-10-26).

**FSVR.** The cutoff word glides to each new value on a one-pole of 10 ms (`Synth::CUTOFF_GLIDE_MS`, ours),
starting each note at its own word, so nothing changes at a note-on and a held note's corner moves smoothly.
The EG and LFO2 terms are added after it, unchanged. On a held sine under LPF24 walked by a CC every 16 ms,
the level's ripple at the CC rate drops from 0.161 to 0.018 dB/ms. `Device::setCutoffGlide(false)` gives the
unit's step, and `render_capture` keeps it off unless `-fsvr` is given, so no fidelity number moves; with
`-fsvr` all nineteen regression cases render bit-identical, since none moves the cutoff during a note.

## Oscillator initial phase

**The unit.** Voiced operator byte 0x1C is reserved (Data List, Voice Voiced Parameter) and every factory voice
holds 0 there. A key-synced oscillator starts each note at phase 0.

**FSVR.** Byte 0x1C, 0 to 127, is a key-synced operator's initial phase, value / 128 of a turn, so 64 is 180
degrees and 127 is 357.2 (0 and 360 are the same phase). It is added to the operator's phase input on every
form, so the sine and all1/all2 rotate and the grain forms shift their carrier inside each grain. `refresh_regs`
reads it each tick, so an edit moves a sounding note too. With key sync off it does nothing. A voice that leaves
it at 0 sounds as on the unit; one that sets it plays at 0 on the unit.

It sets where the operator starts at key-on, not a relation it keeps. An operator with a frequency EG of its own
sweeps away from its partners by a share of a turn that doubles with each octave, on the unit as here: B042
Bassline 2's operator 7 (Freq EG Init 30) ends its sweep 0.22 of a turn ahead of operator 8 at C2 and 0.43 at C3
(`docs/findings.md`, 2026-10-17). Freq EG Init and Attack Level at 0 keep the phase where this byte puts it.

## Quality / Speed, the host rate and an idle engine

**The unit.** Every slot of every channel, both effect DSPs and the filter chip run at 48 kHz all the time,
sounding or not, into a 48 kHz DAC.

**FSVR.** The engine runs at 48 kHz too and reaches any other host rate through a 64-tap Kaiser-windowed sinc,
flat to 19.9 kHz and about 90 dB down by 24.2 kHz at 44.1 kHz, with 0.67 ms of delay (`src/device/device.cpp`).
The Options page's Quality / Speed trades accuracy for CPU from its second step on: Good gates operators under
-90 dB rather than -100 and runs the control maths half as often, Fast and Fastest gate at -80 and -72 dB and
advance the amplitude EGs 4 and 8 samples at a time with the gain ramped across the block. At every step a
channel whose carriers are falling in their release under the gate counts as finished, and once nothing has
sounded for a second the engine sleeps: each block computes one sample and repeats it, until that sample moves
or a note starts (`src/engine/render.cpp`). What the sleep changes is the phase the effect and filter LFOs come
back at. Full at a 48 kHz host is the unit's arithmetic; nothing restores the chips running through silence,
and there is nothing to hear in it.

## User libraries without the unit's memory limits

**The unit.** 128 internal performances, and 128 internal voices with no Fseqs or 64 voices with six, in battery-backed RAM.

**FSVR.** A folder of .syx files, each a bank, as many as you import or save to (Import > Import FS1R SysEx to New Bank names a new bank after the file). A performance's Int voices and Fseq mean its own bank's internal ones when the bank holds them, and your library's numbered ones otherwise. The factory banks are the unit's.

## Import Audio

**The unit.** It plays the 90 preset Fseqs and the six its internal memory can hold, which arrive as bulk dumps; it cannot make one from a sound itself.

**FSVR.** The Fseq page's Import Audio analyses a WAV, AIFF, MP3, Ogg Vorbis or MP4 file into an Fseq of your own, after fseq-flash's method, with the frame bytes on the engine's own scales. The result is an ordinary Fseq: it exports as a bulk dump the unit takes.

## Custom algorithm: a full FM matrix (#92)

**The unit.** The algorithm byte (voice common 0x2C) is 0 to 87, a row of the fixed connection table
(`fs1r/algorithms.h`, EPROM 0x37C0DC), with one shared feedback path set by common 0x3D.

**FSVR.** 0x2C = 0x7F selects a custom matrix instead; 0 to 87 play exactly as before. Operator bytes 0x40 to
0x47 (`F0 43 1n 5E 6p mm 40+k vv F7`) are operator m's depth into operator k, so its row reads op1..op8 with its
own feedback where k = m (op1: fb, op2..op8; op2: op1, fb, op3..op8). A depth into another operator is on the
output level's curve (0 off, 99 a full-level modulator), so a stock algorithm converts by copying each
modulator's level. Feedback is 0 off, else the feedback gain at fb = 7d / 99 (0.53 * 2^(7d/99 - 7); 99 is the unit's 7), the same
0 to 99 as the level. Operator level (0x16) is then only the operator's send to the mix, and any operator above 0 is
a carrier, which it can be while it also modulates others through its depths; 0x3D does nothing. 0x48 to 0x4F are held for ring modulation and do nothing yet. An operator
modulated by one at or after it in the order takes that operator's previous sample, as the unit's feedback bus does.

The bytes sit past the unit's 62 per operator, so a voice bulk does not carry them, and a real FS1R ignores
both their parameter changes and 0x2C = 0x7F's out-of-range value. The off path is the unit: all 256 native
presets render bit-identically to main. The engine selftest pins that algorithm 2 rebuilt as a matrix plays
the same channel word. Nothing here can be measured, since the unit has no such mode.
