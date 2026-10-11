# Where FSVR differs from the FS1R on purpose

Everything else in this repository is an attempt to match rgwan's unit, and a disagreement with it is a
bug (`AGENTS.md`, "Where a claim comes from"). This file is the short list of places where FSVR knowingly
does something the hardware does not, because the hardware's behaviour is an artifact of how its CPU talks
to its chips rather than a musical decision. Each entry says what the unit does, what FSVR does, and where
the firmware says so.

## The part's filter switch is live

**The unit.** Part byte 0x07, `FilterSw`, is latched per channel at note-on. `FUN_00010B48` reads the part
byte into `DAT_01029160[part]` on the note-on path and gates the filter call on it:

```
if (((DAT_01039382 != 0) && ((&DAT_01029160)[DAT_010290f6] != 0)) && (DAT_0103937e == 0)) {
    FUN_0000B0E8(channel, part, note, velocity);       // claims a VOP3-1 filter channel for this note
}
```

`FUN_0000B0E8` is what writes the part into the channel map at `0x0106AD8C` and stages the filter EG
(`FUN_0000D050`). The note-off path is gated on the same byte (`FUN_00011F00` -> `FUN_0000B188` ->
`FUN_0000D7B0`). The per-tick update, `FUN_0000DB3C`, then only ever walks the sixteen filter channels that
are already mapped to the part, and register 0x270, the switch that puts VOP3-1 in the channel loop at all,
is recomputed only when the panel or a sysex byte moves (`FUN_00019362` case 7, event 0x183). So on the unit
a note that started with the filter off stays unfiltered until it is released and played again, and a note
that started with it on stays filtered even after the switch goes off. Cutoff and resonance, by contrast, are
recomputed for every sounding note on the 192.3 Hz tick and do follow the panel live.

**FSVR.** `refresh_filter` reads the switch, the filter type and the input gain on the tick alongside the
cutoff and the resonance, so all five follow a held note. Switching the filter in mid-note clears the filter
state first, so it opens from rest rather than from whatever its delay line held when it was last in circuit.

**Why.** The switch is the only part of the filter block that behaves differently from the rest of it, and
the difference is a consequence of the channel-allocation scheme rather than anything anyone chose: the
filter is a pool of sixteen VOP3-1 channels that a note either owns or does not. Tweaking the cutoff while a
pad sustains works, tweaking the switch does nothing until the next note, and nothing in the panel tells you
which is which. FSVR makes the whole block consistent.

**What it costs.** A performance that toggles `FilterSw` from a controller or from automation under a
sustaining note sounds different here than on the unit. Nothing in the capture set does that: every
`filtoff` reference in `15_filter` sets the switch before the note, which is also how the analysis compares
them, so no measurement in `docs/fidelity_plan.md` moves.

Where the two disagree and it matters, `FilterSw` is a part byte like any other: set it before the note-on
and the unit's behaviour and FSVR's are the same.

# What the plug-in adds

The entries above are the engine's. The plug-in adds features the unit never had; none of them changes how the engine renders a voice, and each is something you choose to use. [editor.md](editor.md) has the detail.

## The morph square (new)

**The unit.** A part plays one voice. Moving between two sounds means a controller set routed to a few destinations (the 48 in the Data List's list), or loading another voice.

**FSVR.** Each part holds four corner voices, and the Quick Control page's morph square (off for now) blends them into the one voice the part plays: amounts (levels, rates, frequencies, depths, key scaling) bilinearly by the square's position, choices (the algorithm, forms, waves, switches, Fseq tracks, the formant and FM control routing) from the nearest corner. A random amount on each axis moves each note's position from a seed. The processor does it outside the engine, as an editor would: moving the square sends the part one voice bulk, and the engine plays that voice exactly as the unit would play it. With all four corners the same, which is where every part starts, nothing differs from the unit.

**What it costs.** The engine holds one voice per part, as the unit does, so a morph position reaches the notes already sounding along with the next one, rather than each voice moving on its own. A voice exported or stored from a morphed part is the corner the part edits, not the blend.

## User libraries without the unit's memory limits

**The unit.** 128 internal performances, and 128 internal voices with no Fseqs or 64 voices with six, in battery-backed RAM.

**FSVR.** A folder of .syx files, each a bank, as many as you import or save to (Import > Import FS1R SysEx to New Bank names a new bank after the file). A performance's Int voices and Fseq mean its own bank's internal ones when the bank holds them, and your library's numbered ones otherwise. The factory banks are the unit's.

## Import Audio

**The unit.** It plays the 90 preset Fseqs and the six its internal memory can hold, which arrive as bulk dumps; it cannot make one from a sound itself.

**FSVR.** The Fseq page's Import Audio analyses a WAV, AIFF, MP3, Ogg Vorbis or MP4 file into an Fseq of your own, after fseq-flash's method, with the frame bytes on the engine's own scales. The result is an ordinary Fseq: it exports as a bulk dump the unit takes.
