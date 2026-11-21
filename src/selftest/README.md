# src/selftest: the engine self check

| File | What |
|---|---|
| `selftest.cpp` | `selftest()`: the engine's invariants, the firmware hosts' pinned register streams, the display tables and EG plots, the effect paths. Run by `engine_selftest` (ctest) and `fsvr_console -selftest` |
| `patchfiles.cpp` | `selftest_patchfiles()`, which `selftest()` runs: `src/patchfiles/` driven with small files built in the test, one per format and container |

Add a check here for any engine behaviour a refactor could silently move.
