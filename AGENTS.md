# Working on FSVR

FSVR is a software reconstruction of the Yamaha FS1R. The firmware's control logic is rewritten in C++ from the decompiled v1.20 ROM, and the two custom chips, for which no register documentation exists anywhere, are modelled and calibrated against recordings of a real unit.

That split is the whole project, and it decides how you should answer almost any question here.

## Read this first

- **[STATUS.md](STATUS.md)** what is known, what is modelled, what nobody knows, and the open work
- **[docs/findings.md](docs/findings.md)** the research log: every conclusion and the evidence behind it
- **[docs/fidelity_plan.md](docs/fidelity_plan.md)** where the engine stands against each recording, ranked

## Names in the repo

Two collaborators, and the repo uses their GitHub handles rather than their chat display names: **rgwan** owns the unit, the dumps and the capture sessions, and **jameshansen** owns the project. A chat client may show either under some other display name; write `rgwan` and `jameshansen` in commits, release notes, findings and docs anyway, so one person is one name everywhere and the history stays greppable.

## Where a claim comes from

The path tells you, and it tells you what a disagreement with real hardware means.

| Path | Status | A disagreement with the unit means |
|---|---|---|
| `src/fs1r/` | the hardware's values: `hardware.h` read off the board, the EPROM's tables (generated), the DSP register map, and `cal.h` | read wrong, or for `cal.h` not yet calibrated. Nothing else lives here |
| `src/fs1r/cal.h` | **INFERRED**, the calibration surface | this is the file you edit to calibrate |
| `src/engine/`, `src/presets/`, `src/effects/host/`, `src/filters/filter_host.*` | **KNOWN**, rewritten from the disassembly | you have a bug. Go read the firmware |
| `src/tonegen/`, `src/modulators/` | **INFERRED**, modelled; constants in `cal.h` | it needs calibrating against a recording |
| `src/filters/`, `src/effects/`, `src/dsp/` (the DSP) | **KNOWN**, bit for bit what the firmware's DSP programs compute | you changed the sound; `dsp_check` says so |
| `src/device/`, `src/math/`, `src/display/`, `src/io/`, `src/selftest/`, `src/console/`, `src/patchfiles/` | **ours**, no hardware counterpart | nothing. The hardware has no opinion |
| `src/kernel/` | **ours**: the scalar code of `filters/` (and in time `tonegen/`) a few lanes at a time, compiled once per instruction set | nothing; a hash that moves means a lane is not the scalar code, and `dsp_check` says so |
| `src/engine/tuning.h` | our cost knobs | nothing, and changing one must not alter the output |
| `plugin/` | the plug-in: processor, bank manager, Import Audio | it never models synthesis, see below |
| `plugin/skin/` | the editor, edited in place with Hollow's web editor | nothing; the hardware has no opinion (`docs/editor.md`) |
| `hollow/` | the plug-in framework, Hollow's `framework/` | nothing. Keep it in step with Hollow's copy |

## The rules

**Every `src/` folder has a `README.md`: read it first, keep it true.** It says what the folder is, its status (KNOWN, INFERRED, ours), each file's job and the folder's rules. Before changing a folder, read its README; it is faster than reading the code to find where something lives. In the same commit as any change that adds, removes, renames or moves a file, or changes what a file does, update that folder's README (and the path table above if a folder's status changes). A new folder under `src/` gets a README in the commit that creates it. A README that disagrees with the code is a bug.

**Never invent a constant.** If a number is not read from the firmware or measured off hardware, it does not go in. Every modelled constant lives in `src/fs1r/cal.h` and names the document that measured it.

**A number fitted against one recording is not measured.** This is the expensive lesson of this project. Three conclusions in two days were written up with a residual quoted and all three were wrong, because a model was fitted against a single recording until the error stopped moving. The modulation index was published twice at the wrong value. Read the 2026-09-19 entry in `docs/findings.md` before you quote a residual. Two datasets, or a firmware trace, or it is not settled.

**Cite the firmware by address.** Anything read from the disassembly names its `FUN_xxxxxxxx` so the next person can re-check it in Ghidra. `tools/ghidra_ctrl_dests.py` shows how to reach code Ghidra never turns into functions.

**The plugin never models synthesis.** It moves parameter values in and out of the engine as sysex, exactly as a hardware editor would, and learns state back from the engine's own bulk dumps. If you find yourself computing audio in `plugin/`, you are in the wrong layer. The morph square blends voice bytes into a voice bulk and Import Audio writes Fseq bytes on the scales `fsvr::Device` gives it; neither renders anything.

**Generated files are generated.** Do not hand-edit anything in `plugin/generated/`, nor `src/fs1r/tables.h` or `algorithms.h`. Change the generator or its source and re-run it. `plugin/generated/README.md` names each generator. `plugin/skin/` is not generated: it is the source, edited in place with the web editor of the Hollow project beside this repository (`python hollow.py editor` there opens it by default). Its params and `data/fs1r_sysex.json` were made from `plugin/generated/` once, so a change to the parameter descriptions reaches the skin only by editing it there too.

**Scale is not fidelity.** `src/engine/tuning.h` holds knobs that trade CPU for nothing else. If changing one moves a measurement, the value is wrong, not the hardware's.

## The editor's layout

The skin is placed by the pixel, so check it by the pixel. Render the page with `hollow-render` and look at it enlarged before a layout change is done; `build/x64/hollow/Release/hollow-render.exe plugin/skin page_operator out.png` draws one view.

- **Nothing is cut off.** A caption never runs under its widget's lamp, arrow or steppers, nor past the widget's edge. Key Sync was 66 px and its caption ran under the lamp, so it is 94. Widen the widget or shorten the caption rather than shrinking the font. `python ../Hollow/tools/overflow.py plugin/skin` measures every caption, every choice a dropdown can show and the widest value of every number field with the skin's own fonts, and it must report none.
- **What a widget draws stays inside its rect.** The widget clips, so a ring, glow or shadow drawn past the rect loses that side. The selected matrix box's ring lost its right side, and a column of its left and top, until it was drawn inside the box.
- **A value is centred on its control by its pixels.** A glyph's advance carries spacing after it, so text centred in a rect lands about a pixel left of a knob or fader centred on the same rect. Level's 99 sat 1 px left of the fader until its box moved 1 px right. Measure the render.
- **A section's controls share rows and columns.** Captions stand on one line (Amp Vel's with Level's), values on one line (Level's with OP Att's). A column under a button or tab is centred on it, its caption taking the button's width and its knob or fader centred on the same line: Level and its fader stand under Noise, and Amp Vel, EG Bias and OP Att under Voiced. They sat 6 and 7 px off until they were measured.
- **A number edited by step carries the steppers, as its neighbours do.** The `panels/label_plate` fill brings them (`"steppers"` in `skin.json`).
- **A parameter sits with the controls it acts on, under its panel name.** OP Att trims an operator's output, so it is in Amplitude beside Level, not in the Freq EG row. It reads OP Att, the owner's manual's name, rather than the Data List's "carrier level correction", and shows the unit's dB through the param's `labels`.

## Releases

Every release tag carries release notes: an annotated tag (`git tag -a vX.Y.Z -F notes.md`) whose body says what was measured, what changed and the numbers, and which collaborator reports it answers. The Actions job's `generate_release_notes` alone leaves only a compare link, which is not release notes. Skip them only when told to for that release.

The release job reads that body off the tag *object* through the API, not from a checkout: `actions/checkout` peels a tag ref to its commit, so `git tag -l --format='%(contents)'` in the job returned the commit message and v0.4.7 shipped with it as the body (patched by hand afterwards). A lightweight tag has no object and fails the job on purpose. Check the release page's body after the run, not just the zips.

**The release page is the Download table, a rule, then the tag's notes.** The release job writes the table of the three installers (Windows, macOS, Linux, each linked on the tag, with how to run it) and the paragraph on per-format zips above the tag's message, as v0.5.0's page first had them by hand; so the tag holds only the notes. The notes follow v0.5.0's and v0.5.1's shape: a first line `FSVR vX.Y.Z: <what the round is>.`, a paragraph naming whose reports it answers, then one paragraph per change led by its point in bold, with the measured numbers, and a closing **Checks.** paragraph saying what passed and what was not run.

**Both collaborators are credited on every release.** The commit a release tag points at (the version bump, or the last commit before the tag) ends with a `Co-authored-by` line for whichever of the two is not its author:

- authored by jameshansen: `Co-authored-by: rgwan <h@iloli.bid>`
- authored by rgwan: `Co-authored-by: jameshansen <1550976+jameshansen@users.noreply.github.com>`

The line goes in that commit, never in the tag's message or the release notes, which the release page shows as they are. Any other commit credits the people who contributed to it, one `Co-authored-by` line each: the author of a pull request or commit it builds on or fixes, and anyone else who took part in the work. Never one for Claude or any other tool, on any commit. Put it in the commit before pushing, since adding it afterwards means rewriting a pushed commit.

**One release per request.** A release is cut when asked for, then the next one waits for the next ask: more changes pushed later in the same conversation go to `main` and stay there until the user says to release them. Do not chain point releases on your own because the previous one has already built.

**Increment the last digit only, unless the user names the version.** `0.4.7` goes to `0.4.8`, never to `0.5.0`, however much the round changed. The minor and major digits are the user's call and not an inference from the size of the diff: v0.5.0 was cut for four measured engine fixes and had to be retagged as v0.4.8. Ask, or bump the patch digit.

## Before you say it works

```bash
build.bat test
```

That builds the engine and the console, then runs the engine self check, which covers the effect chip, and `check_formant` and `check_presets`. All of them must pass.

If you touched the plug-in (`plugin/`, `hollow/` or the skin):

```bash
build.bat plugin
```

That builds every format, 64-bit and 32-bit, into `bin/<format>` and runs `check_plugin`, which drives the processor the way a host does, then `check_gui`, which drives the editor over it the way a mouse and a keyboard do: every menu, dialog, page and control. Anywhere else, `cmake -B build/plugin -S . -DFSVR_BUILD_PLUGIN=ON`, build, and `ctest -R "plugin|gui"`.

If you touched anything the audio path reaches, also run the render regression:

```bash
python tools/regress.py
```

Nineteen fixed cases against `tools/regress_ref.json`. It compares pitch, harmonic peaks, envelope, stereo width and centroid. **If it fails, that is a finding, not an inconvenience.** Only run `--update` when you meant to change the output and can say why in the commit message.

`regress.py` and `check_formant.py` drive `bin/fsvr_console.exe` on Windows and `bin/render_capture` anywhere else, picked by `tools/fs1r_render.py` (the console is in `bin/` on every checkout but fails before `main()` under WSL, so the choice is made by running it, not by looking for the file). The stored reference holds on every platform: the random note phase, part pan 0 and the S&H LFO draw from each instance's own generator (`Synth::draw`), not from the C library's `rand()`, whose sequence differed between Windows and Linux and made every case fail on a fresh Linux checkout until 2026-10-26.

For a change that should not alter the output at all, prove it rather than assume it. Render a fixed case before and after and compare the file hashes:

```bash
bin/fsvr_console.exe -w before.wav -n 60 -d 3
```

For a change meant to make the engine cheaper, that proof is the gate and the number comes with it: `python tools/abcheck.py --build-base upstream/main` builds the baseline apart and renders it, `python tools/abcheck.py --ref build/ab/<sha> --tier A` says whether the nineteen cases and the Fat Line demo still render the same 16-bit audio (`--tier B` allows one step), and `bin\perfbench.exe --set standard --ref build/perf-<sha> --save build/perf-head` measures and gives the same verdict on its scenarios. A timing only means something against the other binary in the same session, alternated; `docs/performance.md` (Measuring) has the protocol and the record.

### Tracing a waveform

A change to how operators run (phase, key sync, ratios, the EGs, feedback, a form) is checked by tracing them, not by listening. `tools/wavetrace.cpp` plays one voice on a fresh engine at several notes, tone generator only, and prints every sounding operator's frequency, ratio to the fundamental, phase and **lead**: the phase less frequency times time since key-on, what the operator gained or lost against an oscillator left alone. A key-synced operator at a steady pitch leads by 0 at every note. A lead that moves with the note is something sweeping the pitch, which is how Bassline 2's frequency EG was told apart from a key sync bug (`docs/findings.md` 2026-10-17). `-png` draws a few periods of the fundamental per note, the channel and then each operator, aligned on the fundamental's cycle, so notes that agree draw the same wave. Look at it enlarged.

```bash
build.bat trace
```

```bash
bin/wavetrace.exe -v presets/native/036_Bassline_2.syx -set 7:8=50 -n 36,48,60,72 -png build/trace.png
```

`-set 7:8=50` is operator 7's byte 8 (Freq EG Init back to 0); `-set 0x2C=16` a common byte; `-r` and `-p` take a voice from the EPROM. Elsewhere it is the CMake target `wavetrace`. When a trace settles something, pin it in `src/selftest/selftest.cpp` as the key sync checks are, so a later change cannot move it silently.

## Finding work

[STATUS.md](STATUS.md)'s "Open work" section is the list, and it is grouped by what actually blocks each item rather than by subsystem: what anyone can start today, what is waiting on a hardware session, what is open research, and what is parked. Start there.

The maintainers mirror the same list on a private project board. If you are outside the org you cannot see it, and you are not missing anything: STATUS.md is the copy that is kept current, and the board follows it.

Reported bugs are ordinary issues and are separate from the fidelity work:

```bash
gh issue list -R musicastudio/FSVR
```

## What you cannot see

The register-level captures that settled the CPU side of the engine were taken with a patched firmware over sysex, and they live in a **private** companion repo. If you do not have access, you cannot read them, and you should not go looking. Their conclusions are public and are indexed in [docs/captures/sessions.md](docs/captures/sessions.md), with the working in `docs/findings.md`.

The EPROM image itself is not in this repo either. It is rgwan's dump, kept outside the tree.

## Things that will trip you up

- `captures/` is measurement data with two pipelines and a rule, stated in `captures/README.md`. Recordings and analysis are gitignored; only inputs are tracked
- The engine always runs at 48 kHz whatever the host rate. `fsvr::Device` resamples on the way out, so patch timing is identical everywhere
- `TICK_HZ` is 192.3 Hz. Only `render`, `render_chan` and `refresh_ctl` run per sample; everything `tick()` reaches is control rate
- The tables `init_tables()` fills once (the grain windows, their means and the phase generator's mantissas) have external linkage on purpose. Making them file-static again would give every translation unit its own zeroed copy and the engine would render silence
- The plug-in's processor runs on two threads: the audio thread sends param changes and MIDI and renders, a worker loads patches and files and reads the engine back into the params. `ctl` guards what both touch, and the audio thread only ever try-locks it. `saving()` finishes whatever the worker has not reached before the host saves
- Commits here never carry a `Co-Authored-By` line for Claude or any tool; the one co-author line is the other collaborator's, on the commit a release tag points at (Releases, above)
