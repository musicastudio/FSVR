# FSVR plugin guide

The plugin is a controller for the engine, not a second synthesiser. Everything you move becomes a sysex parameter change into `fs1r::Device`, and what the engine holds comes back into the editor out of its own bulk dumps. That is how a hardware editor works, and it means the plugin and the console always agree. [editor.md](editor.md) says why the editor is laid out the way it is; this page is how to use it.

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

Copy what your host loads into its plug-in folder: on Windows `C:\Program Files\Common Files\CLAP` and `...\VST3`; on macOS `~/Library/Audio/Plug-Ins/CLAP`, `VST3`, `VST` and `Components` (Logic Pro loads only the AU, scans it at launch and validates a new one once); on Linux `~/.clap`, `~/.vst3` and `~/.vst`. The macOS builds are universal, Apple silicon and Intel in one binary. The DXi, for Cakewalk's older hosts, registers with `regsvr32 "FSVR.dll"` from an elevated prompt, the 32-bit DLL from `FSVR-Windows-VST2-32-DXi.zip` (the installer does this for you).

Nothing here is signed by an Apple developer account, so the first launch of the macOS standalone needs **Open** from its right-click menu, or one command to drop the quarantine flag the download put on it:

```bash
xattr -dr com.apple.quarantine ~/Downloads/FSVR.app
```

On Linux the editor is an X11 window (a Wayland desktop runs it under XWayland). The file dialogs are zenity's, or kdialog's where zenity is missing, and Import Audio reads MP4 and AAC through an `ffmpeg` on the PATH; WAV, AIFF, MP3 and Ogg need nothing.

## The standalone

The standalone's **MIDI/Aud** button, above Volume, opens its audio and MIDI settings: the driver, output, input, sample rate and buffer size, and the MIDI inputs, where a click opens or closes one. On Windows it is a dialog in the synth's own style that works the same settings as **Audio/MIDI Settings** in the window's system menu (right-click the title bar), with Save State, Load State and Reset State beside it there; on macOS it opens the app's settings window. Closing the standalone on Windows with the performance changed since it was loaded or saved asks whether to save it to a bank first. It remembers the devices and the last state in `clap-wrapper-standalone/studio.musica.fsvr` in your local application data.

## The window

The header is the unit's front panel: the performance's volume, the LCD, a pod of Save, Import, Editor and Keys buttons over the four part buttons, the Tone and KN knob modes and their four knobs, and the output monitor.

- **The LCD** shows the performance (click it for the factory performance menu; the arrows step through them), the selected part's voice and receive channel, the voices sounding (POLY) and the CPU. A user item shows as U and its number.
- **Part 1 to 4** pick the part the edit pages show. Every part's params exist on their own, so a host automates a known part whichever is selected.
- **Tone and KN.** With Tone lit the four knobs are the part's Attack, Release, Formant and FM; with KN lit they are KN1 to KN4, the control sources the performance's controller sets route.
- **The monitor** shows the last note's first 32 harmonics and the output level. The top bezel button is panic, all notes off while held; the lower one arms MIDI learn: click a control, move a controller, and that CC drives it from then on. The assignments are saved with the session.
- **Editor** and **Keys** hide the editor pages and the keyboard, and the window shrinks to what is left.
- **The keyboard** along the bottom has 88 keys, A0 to C8. The pitch and mod wheels lie on their sides above it: drag right to raise them, so the pitch wheel bends up to the right and springs back when you let go.

The tabs across the top open the pages: Browser, Parts, Operators, Performance, Effects, Fseq and Quick Control. The Operators tab holds the expert pages as sub-tabs along its top: Operator (one operator's every setting), All Operators and Envelopes (all eight at once), Modulation (controller sets, LFOs, the Formant and FM routes), Key Scaling, Filter and Pitch. The operator column on the right is the operator panel: 1 to 8 pick an operator, V and N its voiced or unvoiced (noise generator) half, and it shows the operator's waveform as the engine makes it with its first 16 harmonics under it, under a header bar that names the spectral form and opens the forms, then its most used settings, its frequency (a ratio, or hertz in Fixed: drag it or step it) and its amplitude EG; Edit Operator opens its page on the Operators tab, and the buttons under it open the other expert pages there. Every number box has a down button at its left end and an up button at its right. Values read as the unit's display reads them: -24..+24, L63..R63, C-2..G8, the effect parameters in hertz, milliseconds or their named choices, an operator's frequency as a ratio or in hertz.

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

Only these chords are FSVR's; every other key goes to your host, so the transport bar and your own shortcuts keep working. While a dialog is open or you are typing a name, FSVR takes every key until you are done, so nothing you type goes astray. On macOS, Alt is Option.

They work in every format, in the standalone and in a host, with nothing to configure and no need to click the editor first.

While FSVR's editor has the keyboard, a chord listed above is FSVR's and your host does not see it, even if the host has that key bound to something of its own. Click away from the editor and the key is the host's again. Every key FSVR does not bind stays the host's throughout, so the transport bar and your own shortcuts keep working while you edit.

## The browser and your library

The Bank column holds the factory bank, **Yamaha FS1R** (the 384 performances, 1408 voices and 90 Fseqs of the unit's ROM, built into the plug-in), then your own banks. The Performances, Voices and Fseqs tabs switch what the lists show, and the Category column narrows them. A performance loads all four parts with the voices and Fseq it names; a voice loads into the selected part; an Fseq plays on the performance's Fseq part.

Your library is a folder of .syx files, one bank each: `Documents/FSVR/Library` (set `FSVR_LIBRARY` to use another). Every FSVR in every host shares it and notices when a file in it comes, goes or changes, so you can also manage it with a file manager.

- **Save > Save Current Preset** saves the performance with its voices and its Fseq into one of your banks, or a new one, under the name you give it (12 characters). Saving under a name the bank already has replaces that preset. The factory bank is read only, so it is not offered.
- **Save > Export Current Preset** writes the performance as an `.fsvr` file, FSVR's own preset format, to share or keep outside the library. **Import > Import FSVR Preset** loads one and asks which bank to save it to.
- **Import > Import FS1R SysEx to New Bank** copies a .syx into the library as a new bank named after the file (the name with a number after it if that is taken), shows it and loads the first thing in it. It takes FS1R performance, voice and Fseq dumps, including a whole unit's internal memory, DX7 single voices and DX7 32-voice banks.
- **Save > Export to FS1R SysEx** writes the whole unit, system settings included, as bulk dumps a real FS1R takes.
- **Right-click a preset** to delete or rename it, or to edit, copy or paste its attributes (its category). The factory's presets are read only, but their attributes open to read and copy.
- **Right-click a bank** to delete or rename it. A deleted bank's file moves to the library's `Deleted` folder, so it can come back.
- **Yamaha FS1R** lists the factory's presets only; yours are in the banks you saved them to.

## Fseqs

The Fseq page is a bank manager of its own: the 90 presets with a lock, then yours. Beside it are the performance's Fseq settings (the part it plays on, play mode, trigger, pitch, speed, loop), its tracks drawn with a gold line where playback is, and which operators of the selected part follow it. Choosing an Fseq sets the loop to the one its header gives, as the unit does; a performance keeps its own. **Import** adds a .syx of Fseqs to the library, **Export** saves the loaded Fseq as a bulk dump the unit takes.

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

The CPU side is the firmware: velocity curves, level key scaling, pitch and portamento, the pitch EG, LFO1 and LFO2, part levels, mono handling, performances, Fseq playback, pan. The tone generator and the effect DSP are models, because no register documentation or instruction set exists for either chip; every constant that models them is gathered in `src/fs1r/chips/cal.h`, so calibrating against a recording of real hardware is one table edit. `docs/ymp706_registers.md` marks which is which line by line.
