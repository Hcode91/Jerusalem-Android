# Ghidra milestone 1: actual headless analysis

Ghidra 12.2 was installed and run in this workspace with Temurin JDK 25.0.5+8. Both release downloads were checked against their published SHA-256 digests. No original program was executed and no engine or Android build changes were made for this milestone.

The three original binaries were imported into one non-shared project and analyzed as `x86:LE:32:default`, Windows compiler specification. Their hashes match the original evidence inventory in `reverse-engineering.md`.

| Program | Image base | Function count at the milestone 1 checkpoint |
| --- | --- | ---: |
| Jerusalem.exe | `00400000` | 2079, including two manually defined audio functions |
| CinemaMovieM.dll | `10000000` | 436 |
| mmxImage.dll | `10000000` | 304 |

These are auto-analysis counts, not a claim that every function boundary or type is correct. The DLLs are separate programs, so their equal preferred image bases are expected. Missing Windows system libraries do not prevent this static analysis.

## Loader entry points established

| Program | Address | Diagnostic-supported role |
| --- | --- | --- |
| Jerusalem.exe | `004a2bb0` | `aSecComSouMono::DecompressHeader` candidate |
| Jerusalem.exe | `004a2440` | `aSecComSouStereo::DecompressHeader` candidate |
| Jerusalem.exe | `004118a0` | `aSecComAqi::DecompressNode` |
| Jerusalem.exe | `00411a80` | `aSecComAqi::DecompressChannel` |
| Jerusalem.exe | `0041cf50` | `aArt::Init` |
| Jerusalem.exe | `004206e0` | `aImageFileCinema::ReadHeader` |
| CinemaMovieM.dll | `10004180` | `aImageFileCinema::ReadHeader` |

Roles derive from code references to the original diagnostic strings. They are descriptive analysis labels, not recovered source symbols. No matching loader diagnostics were found in mmxImage.dll by this script; that does not prove the DLL has no relevant routines.

Ghidra initially disassembled the audio code and identified string references but did not assign containing functions. The audio entries were checked against the x86 instruction listing, including their entry bytes `83 ec 0c 53 55 8b e9 56`, and explicitly defined. Script seeding is limited to the exact executable SHA-256 and checks the entry bytes before changing analysis metadata.

## First verified audio observations

The stereo candidate requests a **60-byte (`0x3c`) buffer** from helper `00411860`; the mono candidate requests **54 bytes (`0x36`)** from the same helper. This establishes the request sizes. The helper's read/transform semantics still require tracing before treating every requested buffer byte as a direct file offset.

Both functions copy 44 bytes from temporary-buffer offset `+8` into object offset `+0x18`. This agrees with the RIFF/WAVE headers observed in the original WAC/WAS files. Further object writes differ:

| Object offset | Mono candidate | Stereo candidate |
| --- | --- | --- |
| `+0x04` | Pointer used as receiver for the read helper | Same |
| `+0x08` | Allocated working-buffer pointer | Same |
| `+0x0c` | Initialized to zero | Same |
| `+0x10` | Initialized to `0x2c` | Same |
| `+0x18 .. +0x43` | 44-byte copied header | Same |
| `+0x44` | u16 from buffer `+52` | u32 from buffer `+52` |
| `+0x46` | u32 from buffer `+0`, then decremented | Not the corresponding stereo field |
| `+0x48` | Within the preceding mono field | u32 from buffer `+56` |
| `+0x4a` | u32 from buffer `+4` | Not the corresponding stereo field |
| `+0x4c` | Within the mono u32 field beginning at `+0x4a` | u32 from buffer `+0` |
| `+0x50` | Not assigned by this mono header routine | u32 from buffer `+4` |

These are object-memory offsets, not asserted disk-format field meanings. In particular, the mono object's unaligned u32 writes prohibit assuming natural four-byte padding. The constructors and accessors must be traced before defining complete class layouts. Ghidra's recovered signatures and output-parameter types remain provisional.

## Reproducible analysis tool and private outputs

The independently written `tools/ghidra/LoaderMilestone.java` maps diagnostic strings to containing functions and invokes Ghidra's decompiler. It does not copy decompiled code into the engine or implement any codec. It also links imported helper DLLs located alongside the executable in the project.

Workspace paths:

- Project: `private/ghidra-analysis/projects/JerusalemRE.gpr` and adjacent `.rep` directory.
- Reports: `private/ghidra-analysis/reports/*.loaders.tsv`.
- Original-derived decompiler output: `private/ghidra-analysis/reports/*.decompiled.c`.
- Import/analysis logs: `private/ghidra-analysis/console.log` and `headless.log`.
- Audio-function follow-up log: `private/ghidra-analysis/audio-console.log`.

All original binaries, project databases, runtime downloads, and decompiler output remain under the Git-ignored `private/` directory.

The initial invocation, with paths shortened for readability, was:

```sh
analyzeHeadless PROJECT_DIRECTORY JerusalemRE \
  -import PRIVATE_INPUT_DIRECTORY -recursive \
  -analysisTimeoutPerFile 180 -max-cpu 2 \
  -scriptPath tools/ghidra \
  -postScript LoaderMilestone.java PRIVATE_REPORT_DIRECTORY
```

After defining the two verified audio entries, the executable was processed without repeating auto-analysis:

```sh
analyzeHeadless PROJECT_DIRECTORY JerusalemRE \
  -process Jerusalem.exe -recursive -noanalysis -max-cpu 2 \
  -scriptPath tools/ghidra \
  -postScript LoaderMilestone.java PRIVATE_REPORT_DIRECTORY
```

## Checkpoint

Milestone 1 has established a usable Ghidra project and concrete loader entry points. It has **not** decoded WAC/WAS, AQC, or Cinema resources. The next small milestone is to trace helper `00411860`, identify the actual audio data-decompression functions and their callers, and turn those verified operations into a format specification before implementing an independent decoder.
