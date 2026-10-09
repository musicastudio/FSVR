# The FSVR editor

The plug-in's editor covers everything the Yamaha FS1R has, in hammered aqua chrome: a [Hollow](../hollow/docs/framework.md) skin in `plugin/skin`, drawn by the framework's software renderer, and a processor (`plugin/plugin.cpp`) that answers it out of the engine. This file records the choices where software and the unit part ways, what the processor does for each control the unit never had, and the usability fixes from a sweep of every page. The skin is edited in place with the Hollow project's web editor, and its chrome is burnt into the artwork; [plugin_guide.md](plugin_guide.md) is how to use it.

## Rules

- **Look.** Hammered aqua chrome over slate panels, with light FS1R touches: the green LCD, the dot font, the graphite pod and dark buttons. The pots, dropdowns and tags share one style across every page, on light translucent plates inside each group, and each group's title is a tab on top of its box, left-aligned with 45 degree sides. Whatever is lit (a toggle, the chosen page, part or operator) lights in the LCD's green, the green of the browser's chosen row. The pots' caps are graphite with a light pointer, and the faders' handle is that cap without its pointer at three quarters, on a disc of the panel's colour, over the track at half its width.
- **The chrome is in the artwork.** Hollow's runtime can lay a surface texture over a skin's artwork; FSVR's skin has its chrome burnt into the images instead (`hollow-bake`), so every place renders the same pixels with no surface at run time, checked by rendering the window both ways.
- **Limits from the unit's memory or panel go.** Software has no battery-backed RAM to share out and no physical knobs, so a limit that exists only for those reasons is dropped from the GUI.
- **Limits from the unit's data formats stay.** FSVR reads and writes the FS1R's own sysex, so four parts, eight voiced and eight unvoiced operators, one Fseq per performance, the insertion, variation, reverb and EQ chain, and the name lengths all keep the format's shape.
- **Limits in the engine stay in the GUI until the engine lifts them.** The engine runs the FS1R's own firmware, rewritten from the decompiled ROM, so its 32-note allocation is real. The GUI keeps the unit's note reserve controls rather than promising voices the engine won't play.
- **Unit-only settings keep their params.** A setting with no meaning in software loses its control but keeps its host param and sysex mapping, so a dump round-trips unchanged.
- **The processor never models synthesis.** Every param is a sysex parameter change into the engine at the address `plugin/skin/data/fs1r_sysex.json` gives it, and what the engine holds comes back into the params out of its own bulk dumps.

## The bank manager

The unit holds 128 internal performances, 128 voices (or 64 and six Fseqs). FSVR has a user library instead: a folder of .syx files, `Documents/FSVR/Library` (`FSVR_LIBRARY` names another), each file a bank, shared by every instance and read again whenever a file comes, goes or changes.

- **Banks.** The browser's Bank column holds the factory bank, Yamaha FS1R, then the library's banks by name. Import > Import FS1R SysEx to New Bank copies the file into the library as a new bank named after it, "Name", then "Name 2" if that is taken, and opens it; its first performance loads, else its first voice into the selected part, else its first Fseq. A bank is any mix of FS1R performance, voice and Fseq bulks at any of their addresses, DX7 single voices (VCED, with an ACED before it) and DX7 32-voice banks, which are unpacked to single voices since the unit takes only those.
- **Save > Save Current Preset** opens a modal: the library's banks with `<New Bank>` first, the preset's name (the performance's 12 characters) and, for a new bank, the bank's name. The factory bank is not listed, since it is read only. The performance goes into the bank's .syx with its four voices and its Fseq after it, where it is a user performance; one of the same name in that bank is replaced, with the voices and Fseq that came with it.
- **Save > Export Current Preset** writes the performance as a `.fsvr` file, FSVR's own format: XML, so it can carry more than the unit's bytes later on. Version 1 holds `<name>`, `<category>` and `<sysex encoding="base64">`, the performance, voice and Fseq bulks it loads from. **Import > Import FSVR Preset** loads one at once and opens the Save modal on the name the file gives.
- **Save > Export to FS1R SysEx** writes the whole unit, system included, as bulk dumps a real FS1R takes.
- **Right-click on a preset** (a performance, voice or Fseq row, in any bank): a heading naming it ("Performance: China Pop"), then Rename, Delete, Edit Attributes, Copy Attributes, Paste Attributes. The row lights while its menu and the modal it opens are up. A preset's attributes are its category; an Fseq and a DX voice have none. Delete and Rename edit the bank's file in place, and deleting a performance takes the voices and Fseq that came with it. The factory's presets open the same modals read only, and their attributes still copy. The row's first cell, its code, names the item: U and a number for a user item, A001 for a factory performance or voice, 01 for a factory Fseq.
- **Right-click on a bank**: a heading ("Bank: CYBER"), then Rename and Delete. A deleted bank's file moves to the library's `Deleted` folder rather than going for good; renaming renames the file. Yamaha FS1R is read only.
- **Edited.** `gui.edited` is on once a param reaches the engine after a performance loaded or saved, and the LCD's edit mark shows it. Closing the standalone while it is on asks whether to save first (Windows; macOS and Linux close without asking).
- **A user bank in the browser** lists its performances, voices and Fseqs, each with its U number, filtered by the Category column (the factory lists are static in the skin; a user bank's rows are the processor's). A row loads what it names; a voice goes into the selected part.
- **Numbers.** Every user item has a U number, the banks' items one after another: U1, U2 and so on, as many as there are (up to 16,384, a host param's fixed range, exact through a host's 32-bit normalized value). A user performance is Performance Bank User with `perf.user`; a user voice is a part's bank Int with `part.user.pN`; a user Fseq is Fseq Bank Int with `fseq.user`. The FS1R's own `fseq.number` byte stops at 89 and keeps meaning a preset. The LCD shows a user item as U002.
- **Int inside a bank.** A performance names its voices and Fseq by bank and number. When it comes from a bank that holds its own internal voices (a dump of a whole unit, voices at 51 00 nn), Int voice N is that bank's voice N; when voice bulks for its parts follow it in the file (what Save to Bank writes), those are its voices; otherwise Int N is user voice N. Fseqs follow the same rule.
- **Banks hold their own.** Yamaha FS1R lists the factory's presets only; a user preset is found in its own bank, since the same name can be in two banks. The Fseq page's bank still lists the 90 presets with a lock and then every user Fseq.
- **Another bank opens on All.** Choosing a bank sets the Category column back to All, so a category picked in one bank never hides another bank's presets.
- **A part shows the voice it plays.** A user performance's parts playing its bank's voices (the ones saved after it, or its bank's Int voices) take those voices' U numbers, so the Parts page and the LCD name them, and editing a part's voice number starts from the voice it plays.
- **The lit row is what plays.** A user bank's lists light the loaded performance, each part's voice and the Fseq when they come from that bank, and nothing when they come from elsewhere.

## Unit-only settings without controls

Their params stay, with their sysex addresses, so a system dump reads and writes the same bytes.

| Setting | On the unit | Why it has no control |
|---|---|---|
| Knob Mode (abs, rel) | How a turned knob picks up a stored value | The GUI's pots and host automation are always absolute |
| Play 1 to 4 (note, velocity) | The notes the front panel's PLAY button sounds | The on-screen keyboard and the host play notes |
| Memory (Int Voice 128, 64) | Shares the internal RAM between voices and Fseqs | No shared memory; the library is unbounded |
| Dump Interval | Pacing between bulk dump blocks sent over MIDI to other hardware | FSVR exports .syx files, which need no pacing |
| LCD Contrast | The display's contrast | Never had a control |

Kept on purpose: Device Number (sysex addressing with editors and other units), the receive switches, Bulk Dump Protect (a DAW can still send a dump into the plug-in; the Data List calls this byte "bulk dump protect" while the manual's menu calls it the receive switch), and Transmit Knob Control. A fresh instance's system settings are the engine's own (`Synth::init_system`): every receive switch on.

## Controls added

### The knob mode switch

The unit has two knob mode buttons. With the upper one lit, its four knobs edit the part (attack, release, formant, FM). With the lower one lit, they are KN1 to KN4, control sources that the performance's controller sets route anywhere.

- Two stacked dark buttons, Tone and KN, end the header's pod. Tone shows the part's Attack, Release, Formant and FM pots; KN shows KN1 to KN4 on the same pots.
- KN1 to KN4 are the params `knob.1` to `knob.4` (0 to 127). The processor sends each on its control number from the system settings, on every channel a part listens on, so the controller sets see them as the unit's knobs.
- The unit's third state (both buttons dark, the knobs navigate the display) is not carried over, since the pages do that job.

### The morph square

**Off for now.** The square is gone from the Easy page and its code is commented out in `plugin/plugin.cpp` until it works: every edit reaches all four corners, so a part plays its one voice wherever the morph params sit, and a session's saved corners are ignored. What follows is how it worked.

The morph square, on the Easy page: four corner voices per part, blended into the one voice the part plays. [Differences.md](Differences.md) has it as a difference from the unit.

- **What the square does.** A part's morph holds four corner voices, one per corner of the square. The square's position blends them, a random amount on each axis nudges every note's position (from a seed), Normalize copies one corner into all four, and the edit pages edit one corner or all of them.
- **Why the corners are whole voices.** Keeping every corner's params would multiply them four times: an FS1R part's voice is 672 params, so four corners for four parts would add about 10,700 host params. The processor holds the corners as whole voices instead, saved with the instance.
- **The panel.** The square, the random X slider above it and the random Y fader beside it (Rnd Amt), the seed, and Normalize. The four corner fields (A top left, B top right, C bottom left, D bottom right) show each corner's voice name.
- **Editing a corner.** Clicking a corner field picks it; Edit All (lit on a fresh part) edits all four. The edit pages show the picked corner, or with Edit All the last one picked, and an edit sets that param in the corners edited.
- **Loading.** A voice loaded any way (browser, performance, program change, a dump from the host) goes into the corners the part edits: all four with Edit All, so a part plays as before until its corners differ, or the picked one only, which is how a voice gets into a corner.
- **Playing.** The part plays the corners blended at the square's position: amounts bilinearly, and choices (the algorithm, oscillator modes and forms, waves, key syncs, Fseq tracks and switches, the formant and FM control routing, categories, the name) from the nearest corner. Moving the square sends the part one voice bulk; an edit where the corners agree is one parameter change, as without a morph.
- **Random.** Each note moves the position by up to the random amounts, from a generator seeded by the seed. The engine holds one voice per part, as the unit does, so the notes already sounding follow the newest note's position, rather than each voice moving on its own.
- **Params per part.** `part.morph_x`, `part.morph_y` (0 to 100, host-automatable), `part.morph_jitter_x`, `part.morph_jitter_y`, `part.morph_seed`, and `part.morph_edit` (A, B, C, D or All), editor state the host doesn't list.

### The algorithm browser

The unit picks an algorithm by number from a list; FSVR draws them.

- **The header.** The matrix's header (`fm_matrix`, embedded by the six expert pages) holds the algorithm's number with ▼ and ▲ and Select Algorithm on one plate, and the Feedback pot, once for every page. The matrix's artwork came with FM8's pan row under the output row, which FSVR never drew into; it is gone, and the matrix sits one row lower under a taller header.
- **Browsing.** Select Algorithm swaps the matrix for `alg_browser`: the 88 algorithms as small matrices, Hollow's `matrix_wires` with a fixed `number` at a `scale`, 2x2, 3x3 or 4x4 at a time (the skin var `alg_grid`, kept with the GUI state). The scroll bar is the morph square's Rnd Amt fader, its track and handle cut from `faders/morph_y`.
- **Choosing.** The browser opens scrolled to the current algorithm, lit in green. A click sets `voice.alg.pN` and goes back to the matrix; Cancel goes back without a change.

### Import Audio

The Fseq page's Import Audio makes an Fseq of your own out of a WAV, AIFF, MP3, Ogg Vorbis or MP4/M4A file (`plugin/audio_fseq.cpp`, after [fseq-flash](https://github.com/zkarcher/fseq-flash)): per frame a pitch from the autocorrelation, and eight formants picked from the smoothed spectrum as its strongest peaks at least a bandwidth apart, their level split between the voiced and the unvoiced operators by how periodic the frame is. The frame bytes are written on the engine's own scales (`fs1r::Device::fseqWord`, `fseqLevel`), the header's speed makes the Fseq play at the sound's own pace, as many frames as fit in 512, and its note is the key that plays the sound at its own pitch. The Fseq is a bank of its own in the library, named after the file, loads at once, and plays on the performance's Fseq part (part 1 when none has it). A voice whose operators follow the Fseq is what voices it: the factory's FseqBase voices, B115 to B128, are made for that.

### The envelopes

The unit draws an EG as its points with each stage's time laid end to end, and so did FSVR's first plots. That is not what plays. The FS1R's times are rates: a stage lasts its distance over its rate, so a level equal to the one before it is over in a sample whatever its time says, and a level moved changes how long the stages on both sides of it take. The amplitude EG falls in a straight line in dB and rises on a curve, the part's attack, decay and release offsets and the key's rate scaling change its times, the filter EG holds a flat segment for a fixed 0.78 s instead of skipping it, and the pitch EG's velocity scales its rate and its levels.

- **The curve is the engine's arithmetic, solved rather than stepped.** The operator, Env, Pitch and Filter pages' plots are Hollow's `stage_env` with a `model`: the processor registers `fs1r.aeg`, `fs1r.peg` and `fs1r.feg`, which draw through `src/fsvr/egview.h`. That is the engine's EG arithmetic in closed form, each segment's length and shape computed from the engine's own constants and tables (struct EG; `setup_peg` and `peg_tick`, FUN_00026C5A and FUN_00028C8E; `start_filter` and StepEG, FUN_0000D050), so a curve costs microseconds however long the envelope. It is a copy for a different purpose, so the engine self check holds it to a note's own EGs: the amplitude EG's stages end within a millisecond or a percent of the engine's, the pitch and filter EGs' within half a tick a stage (the engine ends a stage on a whole tick; the drawing takes half a tick for that, which keeps a drag continuous). Nothing is rendered.
- **The note it assumes.** Middle C at velocity 64 (`egview::Input`), at the part's and the system's default tuning. The amplitude EG takes no velocity; the key's rate scaling and the pitch and filter EGs' velocity and time scaling are evaluated there. Following the last note played is open.
- **Time is linear, in seconds.** The width holds the longest curve's settling and its release with a sixth to spare; the key off, the dotted line, comes where the curve has reached its sustain, so the sustain shelf is as long as the span leaves over. Each handle sits where the engine reaches its point, and a point it never reaches has none. The amplitude EG's floor is -120 dB, where the engine calls a release over.
- **Dragging.** The point you hold goes where the pointer is, at every position, as if every value were possible: the level and the time become the values between the params' whole steps that put it there, found by bisection through the model, and the curve is drawn from them, so a point dragged in a straight line moves in one. `egview` draws such a value between its whole neighbours, each table it reads taken linearly between them. The engine plays whole bytes only, so the params get the values rounded on every move and the sound follows the drag; on release the curve comes back to the rounded values, which are what plays. A whole drag is one host gesture per param. Typing a level instead (in the grid below the plot) shows what the engine does to a level alone: the point slides along the slope into it, and the stage after it lengthens or shortens.
- **Zoom.** A plot opens fitted: every point in view, however long the envelope, and again whenever another operator, part or layer is shown. The wheel zooms about the pointer, no further out than that fit; Shift+wheel and a drag on empty plot pan, and a double-click fits the plot again. Left alone the plot keeps its width while the curves still fit it and fill more than a quarter of it, so an edit does not rescale it under the pointer.
- **The grid.** Under the plot each stage is a column, Hold, Attack, Decay 1, Decay 2 and Release (Init first on the Pitch page, no Hold on the Filter page), with its time and level in two rows. The Release level is also where the amplitude and filter EGs start.
- **dB, All and Sync.** dB (`gui.eg_db`, on by default) shows the amplitude EGs in dB, where their decays are straight; off shows linear amplitude. All (`gui.eg_overlay`, operator page) draws the other seven operators' amplitude EGs faintly behind this one on the same time axis, so their timing can be read against each other. Sync (`gui.eg_sync`, Env page, on by default) puts the eight plots on one time axis, fitted to the longest envelope, and a zoom or a pan on one moves them all. All three are host-hidden params kept with the instance.

### The monitor, panic and the LCD

- **The monitor** shows the last note's first 32 harmonics in the output, at the numbers printed under them, and the output's peak level per channel, -60 to 0 dB.
- **Panic** (the bezel's top button) is all notes off while held.
- **The LCD's POLY** counts the channels the allocator still owns, out of the unit's 32: one per part a key plays, and a released note keeps counting until its release is over, which is the same test note-on reads when it looks for a free channel. CPU is the audio thread's load.
- **MIDI/Aud**, above Volume in the standalone only, opens its audio and MIDI settings. On Windows they are a modal of the skin's (`dialog_audio`) that reads and drives clap-wrapper's own settings window, which stays hidden: the driver, output, input, sample rate and buffer size as dropdowns and the MIDI inputs as a list of lamps, each change applied and saved by the app as if its window had been used. On macOS the button opens clap-wrapper's window through its app menu's action; Linux's standalone has neither, so the button does nothing there.

### Keybindings

The unit has no keyboard, so these are ours (issue #11): a way to reach any page without the mouse. They are `skin.json`'s `"keys"`, a Hollow facility ([skin-format.md](../hollow/docs/skin-format.md)), so each binding carries the same action as its Navigator button and the two cannot drift apart.

| Chord | Where it goes |
|---|---|
| Alt+1 to Alt+8 | Operator 1 to 8 |
| Alt+O, Alt+E | Ops, Env (all eight at once) |
| Alt+M, Alt+K, Alt+F, Alt+P | Mod, KeySc, Filter, Pitch |
| Alt+X, Alt+S | Effects, Fseq |
| Alt+B, Alt+Z, Alt+T, Alt+R | Browser, Easy, Parts, Performance |
| Alt+N | Voiced and unvoiced, back and forth (the V and N switches) |
| Shift+1 to Shift+4 | The part the edit pages show |

- **The editor holds the keyboard while its window is up** and consumes only these chords; every other key goes on to the host, so a DAW keeps its own. A click in the editor takes the keyboard back after the host has had it.
- **A dialog, or a field being typed into, takes every key**, space included, and no chord fires until it is done. That is what lets a preset be named "Hall 1 Pad" without a letter or a space going astray.
- **Alt+1 to Alt+8 open the Operator page**, since `{op}` is read by that page alone; the other chords are their Navigator buttons. Alt+N steps the shared `layer` var, which the Operator, Ops and Env pages show, so pressing it elsewhere decides what those pages show when you next reach them. The issue asked for Alt+U; N is the mark the unit itself puts on an unvoiced operator, and the buttons say N, so the chord does too.
- **Each chord names itself in the tooltip** of the control that does the same thing, which is where the Navigator's twelve page buttons got the tooltips they had been missing. Help > Keyboard Shortcuts, a page listing them all, is still to come: there is no Help menu yet to hang it on.
- **Hosts take the keyboard off a plug-in's window, and the fix is a message hook.** A host may handle keys in its own message loop, where its accelerators are, so the editor's window never sees them however well it holds focus, and a plug-in that seems to need a click first is usually meeting this rather than a focus bug. So on Windows a skin with bindings hooks the host's message queue (`WH_GETMESSAGE`, `hollow/src/platform/win32.cpp`), takes the chords it uses and passes everything else on, which is what JUCE does and for the same reason. It needs nothing of the host or of the format, so every format works in every host tested, with no setting to turn on and no click first.
- **The plug-in interface is the second route, and the cooperative one.** A host may also offer keys through the plug-in's own API rather than to the window, and Hollow answers VST2's `effEditKeyDown` through `Editor::key`. VST3 has such a call too, but the wrapper FSVR's VST3 is built with stubs it out, and CLAP has no key event and never will, deliberately: keyboard focus is the window system's job in its view, and VST2's and VST3's key APIs are the mistake, hosts having taken the keyboard away in the first place. So the hook above is what serves those two, and on Windows it makes this route redundant for all three. macOS and Linux need neither: the responder chain and X11 input focus deliver keys without a host in front of them, which is why JUCE hooks on Windows alone.

### Modals

Every dialog but the About box is a Hollow modal: a view named `dialog_*`, opened by a `modal` action or by the processor through the text data `hollow.modal`, drawn centred over the window behind a dimming veil, with the keyboard (Escape closes it). They share one frame: a dark title bar with the title and a close X, the page's hammered aqua around the browser's dark stripes, a dark rim, dark buttons along the bottom right, text fields dark on a thin light rim, and lists in the browser's colours. The window grows to hold a modal when the editor is hidden.

| Modal | Opened by |
|---|---|
| `dialog_save` | Save > Save Current Preset; Import FSVR Preset; Save... when closing |
| `dialog_delete`, `dialog_rename`, `dialog_attributes` | A preset's right-click menu |
| `dialog_bank_delete`, `dialog_bank_rename` | A bank's right-click menu |
| `dialog_quit` | Closing the standalone with the performance edited (skin.json `close`) |
| `dialog_audio` | MIDI/Aud (skin.json `standalone`) |

## Usability sweep

### Text overflow

Hollow's `tools/overflow.py` measures text against its widget with the skin's own font strips, the way the runtime measures it: every static caption, every choice a dropdown can show, every label or the widest number a number field can show, and every list cell. It found 25 overflows in the pages as they stood, and a few more in the new controls as they went in; the skin has none. The fixes:

- **Dropdowns.** The text pad cleared 14 px on the right while the arrow reaches 19 px in, so long choices ("Performance", "Int Voice 128") ran under the arrow. The pad is 21 px.
- **Centred tags.** The label plates' text pad is 1 px each side (was 3 and 2), giving captions like "Transpose", "Bandwidth" and "Resonance" the room they need on the operator page.
- **Pitch page.** The Pitch box is wider and Portamento narrower, so "Note Shift" and "Bend Down" fit.
- **Filter page.** Key and Velocity fields are 70 px, so "Key Depth" fits; the Part row's fields are 64 px for "EG Depth".
- **Key Scaling page.** The curve dropdowns are 70 px, so "+Exp" and "-Exp" fit, with the row's fields spread evenly.
- **Fseq page.** The length column is wider for "512 frames", and the Loop box is one column of four rows, since two columns couldn't fit both the labels and "One Way".
- **Parts page.** The user voice number field fits "U16384".

### Elements that didn't make sense

- **Mod page.** Row VC8 of the controller sets ran past its box; the rows are 26 px apart. LFO2's phase dropdown had no caption and read as a stray "0"; it is captioned Phase. LFO1's filter knob had its caption beside it, unlike every other knob; it has one above and sits under the wave and Key Sync controls. The Formant and FM routes were five unlabelled rows of dropdowns and numbers; each column is captioned Dest, V/U, Op and Depth.
- **Pitch page.** The Pitch EG's range dropdown ("8 oct") had no caption; it is captioned Range.
- **Effects page.** The page heading read "Insertion" while Variation and Reverb were titled inside their boxes. The heading is "Effects" and all three blocks are titled the same way. The Data List's run-together parameter names are spaced ("OutputLevel" reads "Output Level", "LPFCutoff" reads "LPF Cutoff").
- **Easy page.** The knob captioned "LFO2" is LFO2's depth; it reads "LFO2 Dep" beside "LFO2 Spd".
- **Browser.** The Channels column read "ch pfm"; it reads "Perf", or the channel numbers. The FS1R's "--" category reads "No category" in the category list. A fresh instance plays the performance its LCD and browser name (A001), rather than FSVR's init performance under A001's name.
- **Fseq lengths.** The Fseq page said "steps" while the browser said "frames"; both say frames, the owner's manual's word.
- **Performance page.** With the unit-only settings gone it regroups into Performance, Master, MIDI (channel, program mode, notes, device, knob transmit), MIDI Receive (bank select, program change, sysex, bulk dump protect, knobs) and Controller Numbers.

## Operators and the noise generator

The FS1R's noise generator is its eight unvoiced operators. Each is noise shaped into a formant: a band with a centre frequency, a bandwidth, a skirt and a resonance, its own amplitude and frequency envelopes and a level. The owner's manual says they give speech its fricatives and serve as noise generators for percussion and effects; with a narrow enough band they act as extra oscillators. The unit's display marks them N (N:OP1 to N:OP8) against V for voiced, so FSVR's buttons and labels say N too, while the param ids keep `u` (`op.3.u.bandwidth.p1`).

- **Operator page (1 to 8).** V and N switch the page between voiced operator n and unvoiced operator n. For N the oscillator box shows the frequency mode (Normal, Link FO following the fundamental, Link FF following voiced operator n's formant) and Skirt, Transpose, Bandwidth and Resonance; the voiced-only Form, Band ratio, Detune and Key Sync hide. Coarse and fine, the sensitivities, the frequency EG, the amplitude EG and the level are shared.
- **Ops and Env.** Their V and N switches show all eight voiced or all eight unvoiced operators at once.
- **KeySc.** Level scaling curves are voiced-only on the unit; an unvoiced operator has one level key scaling amount, the Noise column.
- **Fseq.** Each unvoiced operator has its own switch (the N row) to follow the Fseq's unvoiced tracks.
- **Mod.** The Formant and FM routes pick V or N and an operator number.
- **Algorithm matrix.** Voiced operators only, as on the unit: the 88 algorithms route voiced operators. Each unvoiced operator goes straight to the part's output at its level, and the part's V/N Balance (Parts and Easy pages) sets voiced against unvoiced.

## Everything fitted

Of the FS1R's params, every one is on a page except these, all on purpose: the unit-only settings (above); the reverb's and variation's tenth parameter slot, which no effect type in the Data List uses; and a receive channel range for parts 3 and 4, since the manual gives Rcv Max to parts 1 and 2 only, though the sysex has the byte for all four.

## Readouts

Values read the way the unit shows them, from `src/fsvr/display.h`, which the skin's data tables `fixed_freq`, `op_ratio` and `fx_values` were made from.

- **Effects.** One slot param means something different in each effect type (Reverb Param 1 is Hall1's Reverb Time and Delay LCR's Lch Delay), so each slot has a readout per type, shown with its type, through that type's value table ("2.0" seconds, "4.0k", "thru", "D=W"), or for the 14-bit delay times the value as milliseconds, and a drag stays within what that type takes.
- **Operator frequency.** The coarse display reads as the unit reads F.Coarse: a voiced operator's ratio (0.500 to 61.69, from coarse and fine), or in fixed mode, and for an unvoiced operator, the frequency in hertz, with "Hz" beside it.

## What the processor answers

The skin asks through params and the instance's text data; the processor (`plugin/plugin.cpp`) answers on a thread of its own, never the audio one.

| Key or param | Kind | Written by | Meaning |
|---|---|---|---|
| `sysex.import`, `fseq.import` | text data | GUI | A .syx to import as a bank (the Import menu; the Fseq bank's Import) |
| `sysex.export`, `fseq.export` | text data | GUI | Where to write the unit, the Fseq |
| `fsvr.export`, `fsvr.import` | text data | GUI | A .fsvr preset to write, or to load and save |
| `save.open`, `save.request`, `save.then_close` | text data | GUI | Save to Bank opened (the processor fills it in), confirmed, and whether the standalone closes after it |
| `save.name`, `save.bank_name`, `save.bank` | text data, param (not the host's) | both | The preset's name, a new bank's name, the bank (0 a new one, else 1 + its row) |
| `item.context`, `bank.context` | text data | GUI | The row a right-click was on: its index and cells, tab-separated (a list's `contextRow`) |
| `item.name`, `item.what`, `item.new_name`, `item.max`, `item.category`, `item.readonly`, `item.kind` | text data, params (not the host's) | processor | That preset, for its modals |
| `item.request` | text data | GUI | delete, rename, attributes, copy or paste on it |
| `bank.name`, `bank.new_name`, `bank.readonly`, `bank.request` | text data, param (not the host's) | both | The same for a bank: delete or rename |
| `perf.name` | text data | processor | The engine's performance name, Export Current Preset's file name |
| `gui.edited` | param (not the host's) | processor | The performance changed since it was loaded or saved |
| `hollow.modal`, `hollow.close` | text data | either | The modal shown; "1" closes the standalone without asking (Hollow's) |
| `fseq.import_audio` | text data | GUI | A sound file to make an Fseq of |
| `library.dir` | text data | processor | The library folder |
| `bank.list` | text data | processor | The library's banks, a name a line |
| `perf.user.list`, `voice.user.list`, `fseq.user.list` | text data | processor | Every user item, a line each, tab-separated fields |
| `browse.perf.list`, `browse.voice.list`, `browse.fseq.list` | text data | processor | The browsed user bank's rows, filtered by category |
| `browse.bank`, `browse.category` | params (not the host's) | GUI | What the browser shows |
| `browse.perf`, `browse.voice.pN`, `browse.fseq` | params (not the host's) | GUI | A row picked in a user bank's list |
| `perf.user.name` | text data | processor | The loaded user performance's name, for the LCD |
| `fsvr.message` | text data | processor | What the last import, export or analysis did, in the browser's header |
| `fseq.display` | text data | processor | The loaded Fseq's tracks, for the Fseq page's display of a user Fseq |
| `fseq.position` | param (not the host's) | processor | The frame playback is at, the display's gold line |
| `perf.bank`, `perf.user`, `perf.program` | params | GUI or host | The performance to load |
| `part.bank.pN`, `part.program.pN`, `part.user.pN` | params | GUI or host | A part's voice |
| `fseq.bank`, `fseq.number`, `fseq.user` | params | GUI or host | The Fseq |
| `knob.1` to `knob.4` | params | GUI or host | KN1 to KN4 |
| `part.morph_*.pN` | params | GUI or host | Part N's morph |
| `morph.pN.tl`, `.tr`, `.bl`, `.br` | text data | processor | The names of part N's corner voices |
| `morph.request.pN` | text data | GUI | `normalize`: copy the edited corner into all four |
| `gui.panic` | param (not the host's) | GUI | All notes off while held |
| `fsvr.engine`, `fsvr.morph` | text data | processor | The engine's bulk dumps and the morph corners, saved with the instance |

Choosing an Fseq (the browser, the Fseq page, Import, Import Audio) copies its header's loop start and end into the performance, as the unit's panel does: the player reads the performance's pair and nothing else (FUN_0000FFFA, `docs/ymp706_registers.md`), so without the copy a new Fseq would play the loop the performance had, and hold one frame when that was 0 to 0. Loading a performance keeps its own pair.

Program change reaches the same params the unit's way: on the performance channel in Performance mode a performance of the bank selected (bank select 3F then 40 for the user's, 41 to 43 for Preset A to C), in Multi mode a voice for each part on the channel.

## Validation against K_Take's FS1R Editor

K_Take's FS1R Editor (freeware; Windows 1.62 from 2020, Mac 1.1.0 from 2015) is an independent editor for the unit, built from Yamaha's documentation and the hardware. Both builds were disassembled with Ghidra outside the repositories, and the tables its Mac build creates at launch were compared with the skin's tables, made from this repository's Data List and ROM data.

| Area | Compared | Result |
|---|---|---|
| Effect types and their parameter slots | 825 slot params, by type number, address and name | Two parser bugs found and fixed: Echo's Lch and Rch FB Level and Auto Wah's LFO Depth had no field on the Effects page. Name differences left are abbreviations (ours are the Data List's) or the editor's 11-character labels |
| Controller destinations | The 33 after the 14 insertion slots | Same list, same order |
| Algorithms | Each algorithm's carriers | All 88 match, confirming the graphs worked out from the EPROM |
| Voice categories | 1,408 preset voices | 63 fixed: Pre E 65 to 127 read Cp, not Or. Of the 11 left, the Data List or the ROM backs ours in all |
| Voice names | 1,408 | About 90 differ, all the editor's spelling (GuiterBell, Caffein, Table for Tabla); ours are the ROM's |
| Preset Fseqs | Pitch mode, start delay, loop mode, loop start and end, end step | All 90 match the ROM headers |
| Fixed frequencies | The unit's frequency display, 128 fine by 21 coarse | Within 0.4% of `440.13 * 2^(coarse - 16 + fine / 128)` Hz above the lowest octaves; the table is the unit's own readout, with one typo |

The tables that are the unit's own readouts are `src/fsvr/display.h`, transcribed from the editor with credit to it (K_Take, https://synth-voice.sakura.ne.jp/fs1r_editor_english.html) and checked by the engine self test.

## Open

- **Names aren't editable.** The unit's formats hold 12-character performance names, 10-character voice names and 8-character Fseq names; the editor edits none of them yet.
- **Factory items don't list under User banks and the other way round.** The factory bank's lists are static in the skin, a user bank's are the processor's; the Category column filters both.
- **The LCD's performance menu lists the factory banks only.** User performances are picked in the browser.
- **Polyphony and note reserve** stay the unit's until the engine allocates more than 32 notes.
- **Insertion destination names.** The Mod page's "Ins Param 1" to "14" stay numbered: the editor's names are misaligned, and the engine doesn't yet implement destinations 1 to 14, so which insertion parameter each drives is still to be read from the firmware.
- **The engine ignores Rcv Max.** Its `part_listens` matches the receive channel only, so a part set to a range (Preset C's guitar performances use 1 to 6) hears just its first channel. The editor shows and saves the range.
