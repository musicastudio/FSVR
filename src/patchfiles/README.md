# src/patchfiles: patch files read into banks

Patch files of any kind FSVR takes, read into a `Bank` of performances, voices and Fseqs, with a `Report` of what was skipped and why. **Ours**: reading a file is not synthesis and the unit has no counterpart. It sits outside the plug-in so the console and the tools can read the same files.

One entry point, `readPatches(bytes, name, report, pending)`. A file is sniffed by its content, never its extension: a container turns it into a sysex stream, then each message goes to the format that takes it, in file order. An item is the sysex the FS1R takes for it, so a DX voice is its ACED (when it has a supplement) and its VCED, and the engine's own conversion (`convert_dx7`) runs each time it loads.

| File | What |
|---|---|
| `patchfiles.h` | `Item`, `Bank`, `Report`, `Pending`; `readPatches`; `vmemRenamed` and `vmemBlanked`, a VMEM voice edited in its slot |
| `reading.h` | what the dispatcher, containers and formats share: `Message`, `Reading`, each container's and format's entry |
| `read.cpp` | `readPatches`: the containers in order (SMF, `.fsvr`, MiniDexed, headerless, sysex), then the formats on each message; why a message nobody took was skipped |
| `dx7.h`, `dx7.cpp` | DX7 voice data: VCED and ACED messages, the VMEM record packed and unpacked, clamping to the DX7's ranges, INIT VOICE, an AMEM slot as an ACED |
| `container_sysex.cpp` | a sysex stream: a `.syx`, and an `.fs1`, which is the same messages; a stream whose bulks lost an F7 or a checksum, mended |
| `container_fsvr.cpp` | FSVR's own `.fsvr` preset: XML whose `<sysex>` is the stream in base64 |
| `container_minidexed.cpp` | a MiniDexed `performance.ini`: its first four tone generators with voice data as one FS1R performance and their VCEDs; the rest reported |
| `container_smf.cpp` | a Standard MIDI File's sysex events, in order |
| `container_headerless.cpp` | a 4096-byte VMEM with no frame (or one within 256 bytes of header or tail, or several), and bare 155- or 156-byte VCEDs |
| `format_fs1r.cpp` | the FS1R's bulks: performances, voices, Fseqs; voices and an Fseq right after a performance are its own |
| `format_vced.cpp` | a DX7 single voice, with the ACED before it; right after a performance, the voice of its next Int part |
| `format_aced.cpp` | a DX7II, DX7s or TX802 voice's supplement, held for the next VCED |
| `format_vmem.cpp` | a DX7 or DX5 32-voice bank, unpacked to 32 VCEDs, each with its AMEM slot's ACED |
| `format_amem.cpp` | a DX7II, DX7s or TX802 bank's supplements, held for the next VMEM, and the DX7II bank select |
| `format_dx7ii.cpp` | a DX7II performance, PCED or the 32 of PMEM: single, dual or split onto parts 1 and 2 |
| `format_tx802.cpp` | a TX802's performances, as hex digits: its first four leading tone generators onto the four parts, the rest named in the report |
| `performance.cpp` | a DX performance as an FS1R performance bulk built on the engine's init performance, its voices found by number at the end of the read and its parts arranged; a voice-only DX file's one performance per voice |

## Rules
- **Detect by content.** A container says whether a file is its own from the bytes.
- **Never refuse for damage.** A bad checksum or a DX5's `10 00` byte count is a warning, and a checksum with its top bit set is the right one. A bulk that lost its F7 (at the end of the file, or before the next F0) or a DX bulk that lost its checksum is mended in the stream first (`repairStream`), and the library keeps the mended one. DX voice bytes are clamped to the DX7's ranges, a bare VCED's as a bank's, and a name to ASCII 32-126; bits outside a packed field are dropped as the DX7 drops them. A headerless bank's top bits are cleared.
- **Report what is skipped**, with the reason, so the status line can name it.
- **Pairing follows the stream:** ACED then VCED, AMEM then VMEM. Files read together share a `Pending`, so a supplement in one file pairs with the bank in the next.
- **A DX performance names its voices by number**: a VMEM's 32 take the numbers of the bank select before it, else follow the last bank read; a file whose banks are all 1-64 but whose performances name 65-128 is a cartridge dump, and its banks are 65-128. A TX802's presets (128-255) are looked up, less 128, only in banks sent with no bank select, as its factory set's are. They are looked up once the file is read, in it first and then in the files read before it from the same folder (`Pending::folder`; a folder's banks number from 1 again), and the report names a file whose voices were taken; a part whose voice is in neither is left off, and a performance with none is skipped. A file of performances with no bank of its own says so (`Bank::namesVoices`), and the library reads it after the other files of its folder, whatever order they came in.
- **A TX802's presets can come from the library.** A bank whose TX802 performances took its banks as the presets records them (`Bank::tx802Presets`). A TX802 performance naming a preset its files lack plays the one the library passes in `Pending::presets`, and the bank gives those banks' messages (`Bank::libraryPresets`) to keep before the file, so it plays them again when read alone. A TX802 performance with a blank name is an empty slot and is not read.
- **Several dumps in one file read as they do apart:** a bank landing on numbers an earlier bank of the file holds starts another dump (`newDump`), so the library can keep a folder of DX files one after the other.
- **Mapping a DX performance onto FS1R parts is ours**, not the hardware's: the firmware has no such path. Each choice is marked where it is made, and none goes in `cal.h`.
- **The parts that play are arranged once their voices are known** (`arrangeParts`): a TG or voice at level 0 takes no part, and a performance with nothing left is skipped with that reason; parts that all receive on one channel receive on the performance's (Pfm); a part alone is centred, and parts all panned alike (all centred, or a TX802's TGs all on one output) are spread ±16 around the centre, ±32 when they are one voice detuned against itself, the flattest left. A DX7II's pan modes 1-3 put voice A left and B right, and its dual detune applies in dual only. No part is panned past 16 or 112, so a side still bleeds into the other.
- **A DX performance gets effects** (`dxEffects`), after Yamaha's Best of DX bonus set: Hall1 at return 64, and a Chorus insertion a little drier than D=W on every part that plays, sending 16 to the reverb. Each type's other parameters are the unit's defaults for it, as choosing the type on the unit sets them.
- **A file of DX voices and no performance gives each voice one** (`voicePerformances`), playing it alone on part 1 on Pfm, named as the voice, so the bank plays from the performance list. Like every DX performance it is rebuilt on each read and never stored; a file that held DX performances, even ones all skipped, gets none.
- **An AMEM slot becomes an ACED only when it would change the conversion** (`acedMatters`): a DX7 voice's default slot does not, so a DX7II bank and a DX7 bank of the same voices load the same.
- **A file that was not sysex is rewrapped** (`Bank::rewrapped`) into the messages the instrument would have sent; the library keeps those, and the items' byte ranges count in them.
- The engine's `load_sysex` keeps taking only what the unit takes (VCED, ACED, FS1R bulks). VMEM, AMEM and DX performances stop here.
- One source file per container and per format. A new one gets a line in this table and its test in `src/selftest/patchfiles.cpp`.
