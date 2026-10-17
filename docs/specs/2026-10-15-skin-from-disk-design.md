# The skin on disk, not in the binary

Design, 2026-10-15.

## Why

`hollow_embed_skin()` turns all 35 MB of `plugin/skin` into a C array compiled into `fsvr_lib`, so every
format carries its own copy. CLAP, VST3, VST2, the 32-bit VST2 with its DXi, the AU and the standalone are
each about 38 MB, and almost all of each is the same skin — around 190 MB of duplicate bytes in a release.
27 MB of the skin is `scales/`, the same art rendered at five sizes.

Putting the skin on disk and resolving it at run time removes that duplication, and makes the skin
something a user (or the installer's updater) can replace without a new binary. It also makes live skin
editing work from any format rather than only where `HOLLOW_SKIN_DIR` is set by hand.

## What already exists

Two of the four pieces are in Hollow today:

- `loadSkin()` (`hollow/src/core/skin.cpp:1196`) already reads `HOLLOW_SKIN_DIR` and loads that folder
  through `loadSkinDir()` before falling back to the embedded table.
- `Gui::tick` (`hollow/src/core/editor.cpp:2856`) already re-stats the skin folder every 30 ticks whenever
  `Skin::dir` is set and reloads when the stamp changes, keeping the current skin if the new one fails.

What is missing is the installed default path, the packaging that puts a skin there, a key to force a
reload, and actually dropping the 35 MB embed.

## Scope

In scope: Hollow's path resolution, the embedded fallback, the F5 key, the CMake staging option, the FSVR
format zips, and the docs. Out of scope: shrinking the skin itself (the five pre-rendered scale sets are
the bulk of it and are a separate question), and the installer repository, which needs a matching change
recorded here but made elsewhere.

## 1. Resolving the skin folder

A new platform function returns the directory holding the binary that contains Hollow's own code:

```cpp
std::string platformModuleDir();   // "" when it cannot be determined
```

- **Windows** (`platform/win32.cpp`): `GetModuleFileNameW` on the handle `thisModule()` already obtains
  (`win32.cpp:38`), then strip the file name. This gives the DLL for a plugin and the EXE for the
  standalone, which is what we want in both cases.
- **macOS** (`platform/mac.mm`): `dladdr` on the address of a function in Hollow. When the result is inside
  a bundle (`.../Contents/MacOS/FSVR`), walk up to the bundle directory's parent so the skin sits beside
  the `.vst3`/`.app` rather than buried in it.
- **Linux** (`platform/linux.cpp`): `dladdr` the same way, no bundle case.

`loadSkin()` then searches, first readable skin wins:

1. `$HOLLOW_SKIN_DIR`, as today. Set but unusable still logs to stderr and falls through.
2. `<platformModuleDir()>/skin` — the standalone, and any format a user unzipped by hand.
3. The shared folder:
   - Windows: `%ProgramW6432%\<NAME>\skin`
   - macOS: `/Library/Application Support/<NAME>/skin`
   - Linux: `$XDG_DATA_HOME/<NAME>/skin` (default `~/.local/share/<NAME>/skin`), then
     `/usr/share/<NAME>/skin`
4. `HOLLOW_SOURCE_SKIN_DIR`, when compiled in — see section 5.
5. The embedded fallback — see section 2.

`<NAME>` is `hollow_add_plugin`'s `NAME`, passed to the core as a compile definition
(`HOLLOW_PRODUCT_NAME`), so Hollow carries no FSVR string.

The search splits into three functions rather than one, so each is testable on its own and the fallback
does not need the environment manipulated to reach it:

```cpp
std::vector<std::string> skinSearchPaths();           // steps 1-4, in order, whether or not they exist
std::shared_ptr<Skin> loadEmbeddedSkin();             // step 5, the compiled-in fallback
std::shared_ptr<Skin> loadSkin();                     // the two composed, as callers use today
```

Windows uses the `ProgramW6432` environment variable rather than `SHGetKnownFolderPath(FOLDERID_ProgramFiles)`
deliberately: in the 32-bit DXi process the latter resolves to `Program Files (x86)` and would never find
the 64-bit install. `ProgramW6432` names the 64-bit folder from either bitness. When it is unset (32-bit
Windows) fall back to `ProgramFiles`.

Linux searches a user location before the system one because its plugin folders (`~/.clap`, `~/.vst3`,
`~/.vst`) are per-user and a user install cannot write `/usr/share`.

## 2. The built-in fallback

`hollow_embed_skin()`, `kSkinFiles` and `embedded_skin.h` stay exactly as they are. They stop pointing at
the 35 MB folder, which is the whole change on that side.

`hollow_add_plugin` gains `FALLBACK_SKIN <dir>`, which is what it embeds. `SKIN <dir>` stays but changes
meaning: it is now the folder to stage beside the binaries (section 4), not the folder to compile in.
`loadSkin()` uses the embedded table only when every path in section 1 misses.

FSVR's fallback is a new `plugin/fallback_skin/`:

- `skin.json`, `views/main.json`: one view, no scales, that paints the product name, "skin not found", and
  the paths that were searched, so the window explains itself.
- `fonts/SairaSemiCondensed-Regular.ttf` and `fonts/OFL.txt` — the font FSVR already ships, under the
  licence it already ships.

About 100 KB against 35 MB. The searched paths reach the view as skin vars, which `Gui::subst` already
resolves in any widget's text: `loadEmbeddedSkin()` sets `skin_search` and `product`, so the view stays a
static file and describes itself wherever it is created, not only through `loadSkin()`.

**The processor needs one skin file.** `data/fs1r_sysex.json`, 229 KB, is the parameter-to-sysex map every
param change goes through — it lives in the skin folder, but it is processor data, not artwork. Without it
a plugin that found no skin would be silent rather than merely blank, which is not the trade we are making.
It joins the factory banks in `hollow_embed_files` (embedded from the skin itself, so there is no second
copy to drift), and `plugin.cpp`'s `skinFile()` searches the same paths as `loadSkin()` before falling back
to the embedded skin and then that table. The other skin file the processor reads,
`views/library_browser.json`, stays out: it only draws the browser, which needs a skin anyway.

So the embedded total is about 330 KB, not 100 KB. Still under 1% of the 35 MB it replaces, and audio then
really is unaffected by a missing skin.

## 3. Reload

`enum Key` (`hollow/include/hollow/hollow.h:177`) gains `KeyF5`, mapped in `platform/win32.cpp`,
`platform/mac.mm`, `platform/linux.cpp` and `formats/vst_keys.h`.

`Gui::keyDown` handles `KeyF5` before the skin's own chord table, so it still works when the loaded skin is
the fallback. It re-runs `loadSkin()` and calls `setSkin()`, keeping the current skin when the new one fails
to load, exactly as the polling path does today (`editor.cpp:2861`).

`Gui::reloadsOnF5()` decides whether the key is ours: the standalone's window always, and a plug-in only
while `HOLLOW_SKIN_DIR` is set. A shipped plug-in must not take F5 from its host, which may well want the
key; a skin being edited still reloads inside a host, which is often the only place a problem shows. Where
it is not ours, `keyDown` falls through unhandled and the host gets the key. The test is not
`Gui::standalone()`, which also asks whether the platform has an audio settings window and so is false in
the Linux standalone.

`Skin` gains `bool watch = false`. `loadSkin()` sets it only for the `HOLLOW_SKIN_DIR` case, and the poll in
`Gui::tick` tests it instead of testing `dir` being non-empty. Without this the change would turn 678 stat
calls twice a second into something every shipped plugin instance pays, since every skin would now have a
`dir`. With it, an installed skin is never stat-walked and skin editing behaves as it does now.

## 4. Build and packaging

`hollow_add_plugin` gains an option `HOLLOW_STAGE_SKIN`, default `OFF`. When on, a post-build step copies
the skin folder next to each format in `BIN`, giving `bin/Standalone/skin`, `bin/VST3/skin` and so on. The
copy is a plain `cmake -E copy_directory`; CMake here is 3.24 and `copy_directory_if_different` arrived in
3.26.

Default off because 35 MB into five format folders on every local build is 175 MB of copying for nothing:
local development uses `HOLLOW_SKIN_DIR`, which is also the only way to get the live-reload polling.

`.github/workflows/build.yml`'s Configure step adds `-DHOLLOW_STAGE_SKIN=ON`. Nothing else in CI changes:
the Zip step already takes whole `bin/<format>` folders, and the release job's per-format split already
copies each format's directory wholesale, so both pick the skin up on their own.

The result for a user is unchanged in shape — `FSVR-Windows-VST3.zip` opens onto `FSVR.vst3` and a `skin`
folder, and both go into the VST3 folder.

### The installer (separate repository)

`musicastudio/Installer` needs a matching change, made there, not here:

- Write one copy of the skin to the shared folder for the platform: `C:\Program Files\FSVR\skin`,
  `/Library/Application Support/FSVR/skin`, `/usr/share/FSVR/skin`.
- Stop shipping the per-format `skin` folders it would otherwise pick up from the build artifacts, so an
  installed machine holds one copy rather than six.
- Remove the shared folder on uninstall.
- The updater replaces the shared skin along with the binaries; a skin and a binary from different versions
  are not supported.

Until that change lands, the installer's output still works — it would just place six copies, resolved at
step 2 rather than step 3.

## 5. Developer convenience

When `HOLLOW_STAGE_SKIN` is `OFF`, `hollow_add_plugin` compiles the skin's absolute source path in as
`HOLLOW_SOURCE_SKIN_DIR`, searched at step 4, after the shared folder and before the embedded fallback. A
locally built standalone then runs with no environment variable set. A staged build — which is what CI and
every release produce — does not define it, so no absolute source path reaches a shipped binary.

This is a convenience, not a requirement. Cutting it costs one `HOLLOW_SKIN_DIR=...` in front of local runs.

## 6. Testing

`check_gui` and `check_plugin` link `fsvr_lib` and call `loadSkin()`. Their `add_test` calls set
`HOLLOW_SKIN_DIR` to `${CMAKE_CURRENT_SOURCE_DIR}/skin`, so the existing suite runs against the directory
path rather than an embed — the new code path gets the whole of today's coverage for free.

New checks in `check_gui`, which already has its own skin folder in hand:

- `platformModuleDir()` returns the folder holding the test binary.
- F5 reloads: copy the skin to the test's temporary directory, point `HOLLOW_SKIN_DIR` at the copy, change a
  caption in it, press F5, and the new caption shows.
- A skin folder that fails to parse leaves the previous skin in place rather than blanking the window.

- `skinSearchPaths()` returns the four paths in the documented order, with `HOLLOW_SKIN_DIR` first when it
  is set and absent when it is not.
- `loadEmbeddedSkin()` gives a skin that opens a window naming the search paths. Calling it directly is why
  the search is split into three functions: the fallback is reachable in a test without a machine that has
  no FSVR installed.

The F5 check needs a skin folder to copy and edit. It takes `HOLLOW_SKIN_DIR` (which `add_test` sets) and
falls back to `HOLLOW_SOURCE_SKIN_DIR`, rather than naming the latter directly: a staged build does not
define it, and CI's build is staged.

Manual, once, per platform: build with `-DHOLLOW_STAGE_SKIN=ON`, unzip one format into a host's plugin
folder with its `skin` beside it, and confirm the editor opens.

## Measured

On Windows, after the change: `check_gui.exe` 4.2 MB where it was 38 MB, and a staged `bin/Standalone`
holding a 4.6 MB `FSVR.exe` beside a 32.8 MB `skin` of 678 files. One hand-installed format is about what
it was; an installed machine goes from roughly 230 MB across six formats to one 33 MB skin plus about
4.5 MB a format.

## Consequences

- A format unzipped without its `skin` folder, on a machine where the installer never ran, shows the
  fallback rather than the editor. This is the trade for removing ~190 MB of duplicated binary, and the
  fallback names the three paths it looked in.
- The skin becomes user-replaceable. That is mostly a gain, and means a corrupt or partial skin folder has
  to degrade sensibly — hence the "keep the previous skin on a failed load" check above.
- `hollow_add_plugin`'s `SKIN` argument changes meaning. FSVR is the only product using Hollow, so no other
  caller needs updating.
