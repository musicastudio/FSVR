# Generated

Every file in this folder is written by a tool. **Do not hand-edit any of them.** An edit here survives until the next time someone runs the generator, then disappears without warning, and the diff that removes it will look like the generator is broken.

They are committed rather than built, so the plugin needs no Python at build time. `plugin/CMakeLists.txt` embeds the three factory banks and the contributors in the binary (`hollow_embed_files`); the parameter descriptions and the voice index are what the editor's params, their sysex table and the browser's lists in `plugin/skin` were made from.

| File | Generator | Source |
|---|---|---|
| `parameterDescriptions_fs1r.json` | `tools/gen_parameters.py` | the Data List MIDI tables. 893 parameters with sysex addresses and bit layouts |
| `fs1r_presets.syx` and `fs1r_presets.csv` | `tools/make_presets_blob.py` | `presets/`, the 1408 factory voices |
| `fs1r_performances.syx` | `tools/make_presets_blob.py` | `presets/performances`, the 384 factory performances |
| `fs1r_fseqs.syx` | `tools/make_presets_blob.py` | `presets/fseq`, the 90 preset formant sequences |
| `contributors.txt` | `tools/contributors.py` | GitHub's contributor list for the repository, less jameshansen, rgwan and bots: the About box's credit line. The build workflow runs it before every build |

To change what is in one of these, change the generator or its source and re-run it. `tools/check_presets.py` reads the banks back and runs under `build.bat test`, so a stale or hand-edited file fails the build.

The same rule applies to two generated files that are not here, because they are text and carry the warning in their own header: `src/fs1r/firmware/tables.h` and `src/fs1r/firmware/algorithms.h`, both by `tools/extract_tables.py`. The editor's skin in `plugin/skin` is not generated: it is edited in place with Hollow's web editor (see `docs/editor.md`), so a change to the parameter descriptions reaches its `params.json` and `data/fs1r_sysex.json` only by editing them too.
