# FSVR plugin guide

The plugin is a controller for the engine, not a second synthesiser. Everything you move becomes a sysex parameter change into `fsvr::Device`, and what the engine holds comes back into the editor out of its own bulk dumps. That is how a hardware editor works, and it means the plugin and the console always agree. [editor.md](editor.md) says why the editor is laid out the way it is; this page is how to use it.

## Formats and installing

| | Windows | macOS | Linux |
|---|---|---|---|
| CLAP | `FSVR.clap` | `FSVR.clap` | `FSVR.clap` |
| VST3 | `FSVR.vst3` | `FSVR.vst3` | `FSVR.vst3` |
| VST2 | `FSVR.dll`, and a 32-bit one | `FSVR.vst` | `FSVR.so` |
| AU | | `FSVR.component` | |
| DXi | inside the 32-bit `FSVR.dll` | | |
| Standalone | `FSVR.exe` | `FSVR.app` | `FSVR` |

The installer (`FSVR-Windows-Installer.exe`, `FSVR-MacOS-Installer.zip`, `FSVR-Linux-Installer` on [the release page](https://github.com/musicastudio/FSVR/releases/latest) or [musica.studio](https://musica.studio/code/fsvr)) puts every format in the folders hosts scan, with all of them ticked by default, and can keep FSVR up to date through musica.studio. To install by hand, each format is also its own zip, `FSVR-<OS>-<format>.zip`.

Copy what your host loads into its plug-in folder: on Windows `C:\Program Files\Common Files\CLAP` and `...\VST3`; on macOS `~/Library/Audio/Plug-Ins/CLAP`, `VST3`, `VST` and `Components` (Logic Pro loads only the AU, scans it at launch and validates a new one once); on Linux `~/.clap`, `~/.vst3` and `~/.vst`. The macOS builds are universal, Apple silicon and Intel in one binary. On Windows, every format and the standalone run on Windows 7 SP1 and later; CI checks each build against what Windows 7's system DLLs export, though nobody has tried one on Windows 7 itself yet. The installer needs Windows 10 or later, so on Windows 7 install from the zips. The DXi, for Cakewalk's older hosts, registers with `regsvr32 "FSVR.dll"` from an elevated prompt, the 32-bit DLL from `FSVR-Windows-VST2-32-DXi.zip` (the installer does this for you).

Nothing here is signed by an Apple developer account, so macOS stops the first launch of the installer or the standalone. On macOS 15 and later, try to open it once, then click **Open Anyway** under System Settings > Privacy & Security; on earlier versions choose **Open** from its right-click menu. A plug-in copied by hand from a downloaded zip carries the same quarantine flag, and a host then refuses to load it. One command drops the flag from the app, and one from the plug-in folders:

```bash
xattr -dr com.apple.quarantine ~/Downloads/FSVR.app
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins
```

On Linux the editor is an X11 window (a Wayland desktop runs it under XWayland). The file dialogs are zenity's, or kdialog's where zenity is missing, and Import Audio reads MP4 and AAC through an `ffmpeg` on the PATH; WAV, AIFF, MP3 and Ogg need nothing.

## The standalone

The standalone's audio and MIDI settings are the last section of the **Options** page: the driver, output, input, sample rate and buffer size, and the MIDI inputs, where a click opens or closes one. They work the same settings as the app's own **Audio/MIDI Settings** window, which **Device Window** opens; on Windows it is also in the window's system menu (right-click the title bar), with Save state, Load state, Reset state and Restore last session beside it, and its **ASIO Control Panel...** button opens the driver's own panel (its buffer size and clock) while the driver is ASIO. Closing the standalone on Windows with the performance changed since it was loaded or saved asks whether to save it to a bank first. It remembers the devices and the last state in `clap-wrapper-standalone/studio.musica.fsvr` in your local application data, and opens on that state when Options > Load Last Patch in Standalone is on; off, the default, it opens on the Init Patch.

## The window

The header is the unit's front panel: the performance's volume, the LCD, a pod of Save, Import, Editor and Keys buttons over the four part buttons, the Tone and KN knob modes and their four knobs, and the output monitor.

- **The LCD** shows the performance (click it for the factory performance menu; the arrows step through them), the selected part's voice and receive channel, the voices sounding (POLY) and the CPU. A user item shows as U and its number.
- **Part 1 to 4** pick the part the edit pages show. Every part's params exist on their own, so a host automates a known part whichever is selected.
- **Tone and KN.** With Tone lit the four knobs are the part's Attack, Release, Formant and FM; with KN lit they are KN1 to KN4, the control sources the performance's controller sets route.
- **The monitor** shows the last note's first 32 harmonics and the output level. The top bezel button is panic, all notes off while held; the lower one arms MIDI learn: click a control, move a controller, and that CC drives it from then on. The assignments are saved with the session.
- **Editor** and **Keys** hide the editor pages and the keyboard, and the window shrinks to what is left.
- **The keyboard** along the bottom has 85 keys, C1 to C8, with the pitch and mod wheels standing at its left: drag up to raise them, and the pitch wheel springs back when you let go. Options > More Keys, move Mod/Pitch to top makes it 88 keys, A0 to C8, with the wheels lying on their sides above it, dragged right.

The tabs across the top open the pages: Browser, Performance, Parts, Quick Control, Operators, Fseq, Effects and Options. The Operators tab holds the expert pages as sub-tabs along its top: Operators (all eight operators at once), Operator (one operator's every setting), Filter, Pitch, Envelopes (all eight at once), Key Scaling and Modulation (controller sets, LFOs, the Formant and FM routes). The operator column on the right is the operator panel: 1 to 8 pick an operator, V and N its voiced or unvoiced (noise generator) half, and it shows the operator's waveform as the engine makes it with its first 16 harmonics under it, under a header bar that names the spectral form and opens the forms, then its most used settings, its frequency (a ratio, or hertz in Fixed: drag it or step it) and its amplitude EG; Edit Operator opens its page on the Operators tab, and the buttons under it open the other expert pages there. Every number box has a down button at its left end and an up button at its right. Values read as the unit's display reads them: -24..+24, L63..R63, C-2..G8, the effect parameters in hertz, milliseconds or their named choices, an operator's frequency as a ratio or in hertz.

Each of the eight operators has a colour, the same everywhere it appears: its button, its box in the algorithm, its envelope and its row on the expert pages (1 blue, 2 green, 3 olive, 4 gold, 5 orange, 6 red, 7 magenta, 8 violet).

On the Parts page the lamp beside each part's name turns the part on and off, and a click on its Voice opens the Browser on that voice.

**Copy and paste** are on the right-click menus. Right-click an operator's button, 1 to 8 in the operator column, to copy that operator or paste over it; it pastes onto the same half, voiced onto voiced and unvoiced onto unvoiced. Right-click an envelope (the operator panel's, the Operator page's, any of the eight on Envelopes, the Pitch page's or the Filter page's) to copy it or paste over it; an operator's envelope pastes between voiced and unvoiced operators. Right-click a part's button in the header, or its lamp on the Parts page, to copy the part or paste over it: the part takes the copied part's voice and every setting but the MIDI channels it receives on. Each instance keeps its own clipboard.

**Options** is one list of settings, kept for every instance rather than with a project: the window's size (GUI Scale, 50% to 200%), the keyboard's layout and Modern EG Labels; the Init Patch, what a new instance plays and Init All loads (Use Current makes the performance playing it, Use Factory goes back to A001 Zap !), and Load Last Patch in Standalone; Turn off all Effects, Init Current Patch (the Init Patch loaded, after asking), Init All (the system settings to their defaults as well), and Quality / Speed, which trades accuracy for CPU: Full is the engine as measured, and each step towards Fastest is cheaper and less exact, Fastest by a quarter to a third of the CPU on a heavy patch. The output reaches the host's sample rate the same clean way at every step, and an instance with nothing to play costs next to nothing. Filter on Every Voice lets a part with its filter on play every voice rather than the unit's 16, and Polyphony sets how many voices there are: the unit's 32, or 64, 96 or 128, with a filter voice for each and the parts' note reserves scaled to it; a change of either stops every note. In the standalone it ends with the audio and MIDI settings.

The expert pages share the algorithm matrix on the right. Its header holds the algorithm's number, with ▼ and ▲ to step it (or drag it, scroll it or double-click to type one), and the Feedback pot. **Select Algorithm** swaps the matrix for a browser of all 88, drawn as small matrices, 2x2, 3x3 or 4x4 at a time, scrolled with the wheel or the bar, and opened on the current one; click one to take it, or Cancel to keep the one you had.

## Keyboard shortcuts

Every page has a chord, so you can get around without the mouse. Hover any button and its tooltip names its own shortcut.

| | |
|---|---|
| **Alt+1** to **Alt+8** | Operator 1 to 8 |
| **Alt+O** / **Alt+E** | Ops / Env, all eight operators or envelopes at once |
| **Alt+M** / **Alt+K** / **Alt+F** / **Alt+P** | Mod / KeySc / Filter / Pitch |
| **Alt+X** / **Alt+S** | Effects / Fseq |
| **Alt+B** / **Alt+Z** / **Alt+T** / **Alt+R** | Browser / Quick Control / Parts / Performance |
| **Alt+N** | Voiced and unvoiced, back and forth (the V and N switches) |
| **Shift+1** to **Shift+4** | The part the edit pages show |
| **Ctrl+Minus** / **Ctrl+Plus** | The window smaller / larger, 50% to 200% |

Only these chords are FSVR's; every other key goes to your host, so the transport bar and your own shortcuts keep working. While a dialog is open or you are typing a name, FSVR takes every key until you are done, so nothing you type goes astray. On macOS, Alt is Option.

They work in every format, in the standalone and in a host, with nothing to configure and no need to click the editor first.

While FSVR's editor has the keyboard, a chord listed above is FSVR's and your host does not see it, even if the host has that key bound to something of its own. Click away from the editor and the key is the host's again. Every key FSVR does not bind stays the host's throughout, so the transport bar and your own shortcuts keep working while you edit.

## The browser and your library

The Bank column holds the factory bank, **Yamaha FS1R** (the 384 performances, 1408 voices and 90 Fseqs of the unit's ROM, built into the plug-in), then two more built-in banks, then your own. **Yamaha FS1R (Bonus)** is Yamaha's FS1R Bonus Tool Pack, once a download on yamahasynth.com and now only on the web archive: four sets of 128 performances, each with the 128 voices it plays (Best of DX Voice, Popular, Cyber, and Jan Beiling's WX5 soundbank). **Templates** are starting points: **Init Patch**, one sine operator; **Silence**, every operator at output level 0; **Silence EG 0**, the same with every envelope level at 0 too; an FM pair, three additive sines, a formant operator and a noise operator. Both are read only like the factory bank; their presets are numbered after yours, so the U numbers your own presets had before are unchanged. The Performances, Voices and Fseqs tabs switch what the lists show, and the Category column narrows them. A performance loads all four parts with the voices and Fseq it names; a voice loads into the selected part; an Fseq plays on the performance's Fseq part.

Your library is a folder of .syx files, one bank each: `Documents/FSVR/Library` (set `FSVR_LIBRARY` to use another). Every FSVR in every host shares it and notices when a file in it comes, goes or changes, so you can also manage it with a file manager.

- **Save > Save Current Preset** saves the performance with its voices and its Fseq into one of your banks, or a new one, under the name you give it (12 characters). Saving under a name the bank already has replaces that preset. The built-in banks are read only, so they are not offered.
- **Save > Export Current Preset** writes the performance as an `.fsvr` file, FSVR's own preset format, to share or keep outside the library. **Import > Import FSVR Preset** loads one and asks which bank to save it to.
- **Import > Import FS1R SysEx to New Bank** copies a .syx into the library as a new bank named after the file (the name with a number after it if that is taken), shows it and loads the first thing in it. It takes FS1R performance, voice and Fseq dumps, including a whole unit's internal memory, DX7 single voices and DX7 32-voice banks. An `.fs1` file, which other FS1R editors save a performance as (its bulk and its four voices'), imports the same way, and one in the library folder is a bank like a .syx.
- **Several files at once** (picked together in the Import dialog, or dropped together) become one bank, named after the folder they came from, and each performance in it keeps the voices and Fseq that came with it. A whole unit's dump, whose performances find their voices by internal number, still gets a bank of its own, since its numbers would clash with another file's.
- **Drop .syx or .fs1 files on the editor** to import them, as the Import menu does.
- **Save > Export to FS1R SysEx** writes the whole unit, system settings included, as bulk dumps a real FS1R takes.
- **Right-click a preset** to delete or rename it, or to edit, copy or paste its attributes (its category). The built-in banks' presets are read only, but their attributes open to read and copy.
- **Right-click a bank** to delete or rename it. A deleted bank's file moves to the library's `Deleted` folder, so it can come back.
- **Yamaha FS1R** lists the factory's presets only; yours are in the banks you saved them to.

## Fseqs

The Fseq page is a bank manager of its own: the 90 presets with a lock, then yours. Beside it are the performance's Fseq settings (the part it plays on, play mode, trigger, pitch, speed, loop), its tracks drawn with a gold line where playback is, and which operators of the selected part follow it. Choosing an Fseq sets the loop to the one its header gives, as the unit does; a performance keeps its own. Speed is 10.0 to 500.0 percent of the Fseq's own pace, or MIDI 1/4, MIDI 1/2, MIDI, MIDI 2/1 and MIDI 4/1 to follow MIDI clock. The unit has nothing between, so the down stepper goes from 10.0 straight to MIDI 4/1. **Import** adds a .syx of Fseqs to the library, **Export** saves the loaded Fseq as a bulk dump the unit takes.

**Import Audio** makes an Fseq of your own out of a WAV, AIFF, MP3, Ogg Vorbis or MP4/M4A file: a pitch and eight formants a frame, voiced and unvoiced, as many frames as fit the Fseq's 512, playing at the sound's own pace at 100 % speed. The key that plays it at its own pitch is the header's note. It lands in the library as a bank named after the file and loads at once, on the performance's Fseq part (part 1 when none has one). You hear it through a voice whose operators follow the Fseq: the factory's FseqBase voices, B115 to B128, are made for that, or switch on V and N for an operator on the Fseq page.

## The morph square

The Quick Control page holds the part's quick edits and the morph square: four corner voices, A to D, blended into the voice the part plays.

- With **Edit All** lit (a fresh part), a voice you load goes into all four corners and the edit pages edit all four, so the part plays as it always did.
- Click a **corner** to edit it alone: a voice you load then goes into that corner only, and the edit pages show and edit it.
- Drag the square to blend. Amounts (levels, rates, frequencies, depths) blend smoothly; choices (the algorithm, forms, waves, switches) come from the nearest corner.
- **Rnd Amt** (the slider above and the fader beside) moves each note's position by up to that much, from the **Seed**; **Normalize** copies the edited corner into all four.

The square's position and random amounts are host params, so they automate. The corners are saved with the session.

## MIDI

Notes, bend, aftertouch, controllers, RPN and NRPN, clock and sysex reach the engine as the unit takes them, and the performance's controller sets route the sources to the destinations. Program change follows the unit's system settings: in Performance mode, on the performance channel, it picks a performance of the bank last selected (bank select MSB 63, then LSB 64 for your library's performances, 65 to 67 for Preset A to C); in Multi mode it picks a voice, from its bank, for each part on that channel (LSB 0 to 11 selects Int or PrA to PrK). The receive switches on the Performance page gate both. A sysex dump or parameter change sent to the plug-in is taken as the unit takes it, which is how an external editor drives it, and dump requests are answered on the plug-in's MIDI output.

## Sessions

The session holds every param, the engine's own bulk dumps, the morph corners and your MIDI learn assignments, so a project reopens exactly as it was saved. The params hosts see are every FS1R param for all four parts, the morph square, the knobs and the performance, voice and Fseq numbers; the editor's own state (pages, the browser's position, the selected part, the scale) is saved with it but not listed. The window's size, 0.5x to 2x, is the scale in the LCD's top right corner: click it. Each size is drawn at its own resolution, not magnified.

## What is modelled and what is read

The CPU side is the firmware: velocity curves, level key scaling, pitch and portamento, the pitch EG, LFO1 and LFO2, part levels, mono handling, performances, Fseq playback, pan. The tone generator and the effect DSP are models, because no register documentation or instruction set exists for either chip; every constant that models them is gathered in `src/fs1r/cal.h`, so calibrating against a recording of real hardware is one table edit. `docs/ymp706_registers.md` marks which is which line by line.
