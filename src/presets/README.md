# src/presets: voices, performances, the EPROM and sysex

The FS1R's data formats as the firmware decodes them, and how they get in: EPROM banks, sysex bulk dumps and parameter changes. **KNOWN**: rewritten from the disassembly and the Data List.

| File | What |
|---|---|
| `voice.h` | `Voice`, `OpV` (voiced operator), `OpU` (unvoiced), the decoders and the DX7 VCED/ACED conversion |
| `performance.h` | `Perf`, `Part` (with its controller state), `Fseq`; the 192 common bytes the effect host reads in place |
| `rom.h` | `Rom` and the loaders: EPROM banks, sysex, parameter change |
| `patch.cpp` | `decode_voice`, blank/default voices, `convert_dx7`, `init_perf`, `Part::reset_ctl` |
| `rom.cpp` | EPROM and sysex loading, bank selection, single-parameter writes. Taking a performance cuts the sound first, as the unit does |

## Rules
- The structs hold the raw bytes (`raw`, `p`, `c`, `fx`) next to the decoded fields: the engine and the effect host read the bytes as the firmware does.
- A new parameter: decode it in `patch.cpp`, name its field in `voice.h` or `performance.h`, and cite the Data List or `FUN_`.
