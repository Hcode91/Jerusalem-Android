# Milestone 2: WAC/WAS structure and bitstream rules

This is a specification and inspection checkpoint, **not a working audio converter**. No PCM is reconstructed by the new tool and Android WAC/WAS playback remains unsupported.

Subsequent progress: [milestone 3](wac-was-milestone-3.md) implements conversion and connects native playback. This document preserves the earlier inspection checkpoint.

Evidence consists of Ghidra decompilation checked against x86 instructions in executable SHA-256 `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`, plus complete original files from Disc 1. Original-derived code and data remain under ignored `private/`; no decompiled code is copied into an implementation.

## Functions traced

| Entry | Observed role |
| --- | --- |
| `00411860` | Delegates reads to the underlying file object |
| `00415890` | File-read retry wrapper |
| `00415410` | Ordinary direct/cached ReadFile path; also has an alternative backing path not fully traced |
| `00411840` | Delegates buffered-window advancement/refill |
| `004a2bb0` | Mono header handling, established in milestone 1 |
| `004a2440` | Stereo header handling, established in milestone 1 |
| `004a2d10` | Mono streaming/chunk orchestration |
| `004a25e0` | Stereo streaming/chunk orchestration |
| `004a34d0` | Invokes the mono core with a 10-bit magnitude/sign code width |
| `004a3500` | Mono differential sample reconstruction |
| `0041df50` | Stereo literal/cache/repeat sample reconstruction |
| `004a3380` | Mono input-helper initialization, including zero sample predictor |

The independently written `tools/ghidra/AudioTrace.java` exports private decompilation and instruction listings for these addresses. All twelve exports completed successfully. Names and inferred types remain analysis annotations.

## Supported wrapper layout

The following model matches the nine files checked. It is intentionally narrower than a claim to support every version of Arxel audio.

| File offset | Type | Verified interpretation |
| --- | --- | --- |
| `0x00` | u32 LE | Stored block count |
| `0x04` | u32 LE | Wrapper output size: a 44-byte WAVE header plus block output, including mono final-block padding |
| `0x08 .. 0x33` | 44 bytes | Embedded fixed-layout RIFF/WAVE header: PCM format 1, fmt size 16, data chunk immediately after fmt |
| `0x34` onward | Block sequence | Mono and stereo have different block headers |

The wrapper integers and PCM output words are little-endian. Compressed bits are consumed **most-significant-bit first within each byte**, as established by the original byte-swap/shift operations and verified by complete token accounting.

The fixed embedded header describes actual audible PCM length. Its RIFF size is not generally the same as the mono wrapper's padded output size. Optional RIFF chunks, alternate channel counts, sample formats and other wrapper variants have not been established.

## Mono WAC blocks

Each block contains:

| Relative offset | Type | Meaning |
| --- | --- | --- |
| `+0` | u16 LE | Compressed payload length in bytes |
| `+2` | byte array | Exactly that many compressed bytes |

Every stored block in the inspected files represents **256 scalar samples = 512 output bytes**, including the last stored block. The final decoded block may extend beyond the embedded WAVE data length. A future converter must distinguish the wrapper's padded output from the playable PCM and trim to the embedded data length.

For example, `A01_FOOTSTEP_SAND01.WAC` has:

- Stored file: **18716 bytes**.
- Block count: **57**.
- Wrapper output size: **29228 = 44 + 57 × 512**.
- Embedded RIFF total: **29138**.
- Embedded PCM data: **29094 bytes**.
- Decoded padding beyond that PCM: **90 bytes**.

This corrects the earlier assumption that 29228 matched the embedded RIFF size.

The first three bits of each mono payload are skipped by the traced core. They are zero in all inspected blocks; their broader meaning is unknown. After those three bits, produce 256 samples using these rules:

| Bits | Operation established from the core |
| --- | --- |
| `0` followed by 10 bits | Replace the current delta, then add it to the sample predictor |
| `1` | Reuse the current delta and add it to the predictor |

The ten-bit value is sign-magnitude: the low nine bits are magnitude and the high bit selects a negative delta. The magnitude is multiplied by **64**. Predictor addition wraps to 16 bits; it is not a saturating addition. The helper stores the updated predictor and delta across block calls.

Initialization explicitly zeros the sample predictor. Initialization of the previous-delta field has not yet been proved; every first block in the inspected mono corpus begins with a new-delta token, so the token supplies it before use. A converter should handle this state explicitly rather than reproducing uninitialized reads.

The token inspector consumes all 256 tokens in the final block, then reports zero to seven byte-alignment bits. All inspected padding bits are zero. It does not yet reconstruct sample amplitudes.

## Stereo WAS blocks

Each block contains:

| Relative offset | Type | Meaning |
| --- | --- | --- |
| `+0` | u32 LE | Compressed payload length |
| `+4` | u32 LE | Decoded PCM byte count for this block |
| `+8` | byte array | Compressed payload |

The tested `BEES.WAS` contains eight blocks. Seven advertise 176400 decoded bytes each, and the last advertises 94348. Their sum is **1329148 PCM bytes**; adding the 44-byte WAVE header gives **1329192**, matching the wrapper and embedded RIFF totals. All eight block records end exactly at file offset **587347**, the physical EOF.

The core is called with a literal width of 12 bits and a cache-index width of six bits. Each token produces one scalar 16-bit value; stereo channel framing follows the WAVE header's interleaved PCM geometry.

| Bits | Operation established from the core |
| --- | --- |
| `0` followed by 12 bits | Emit that bit pattern shifted left four bits as a 16-bit value; write it to the current replacement-cache slot |
| `10` followed by 6 bits | Emit the referenced cache entry |
| `11` | Repeat the previous emitted scalar sample |

The 64-entry cache and its use-position metadata are reset at each core invocation. Cache references and repeats update the selected entry's use position. When that entry is the current replacement slot, the routine selects the first entry with the smallest recorded use position. The literal path does not itself update that use position. This behaviour must be reproduced deliberately; assuming a conventional FIFO or automatically advancing a replacement index would be incorrect.

The structural inspector checks the number of tokens against the decoded byte count, and requires the remaining payload to be at most seven alignment bits. It rejects a sample-repeat before any sample in the current block because that case would read before the current output buffer in the traced code. Such cross-block/reference behaviour has not been established as a valid supported dialect. The inspected blocks do not require that case.

No standard codec identity is asserted for either format. The observed mono arithmetic and stereo four-bit shift also mean that a converter must not assume these encodings preserve arbitrary 16-bit source values losslessly.

## Inspection tool and validation

Run with standard Python 3; no additional packages are needed:

```sh
python3 tools/wac_layout.py /path/to/A01_FOOTSTEP_SAND01.WAC
python3 tools/wac_layout.py /path/to/BEES.WAS
python3 -m unittest discover -s tests -p 'test_*.py'
```

The tool emits JSON containing wrapper metadata, block offsets and sizes, opcode counts, prefix/alignment bits, and `pcm_generated: false`. It performs checked integer/bit reads and rejects truncated or malformed supported layouts. Current inspection limits are 64 MiB per file and 10000 blocks. Unexpected header variants are rejected rather than guessed.

| Original file | Blocks validated | Extra decoded bytes beyond RIFF PCM |
| --- | ---: | ---: |
| A01_FOOTSTEP_SAND01.WAC | 57 | 90 |
| A01_FOOTSTEP_SAND02.WAC | 57 | 90 |
| A01_FOOTSTEP_SAND03.WAC | 56 | 106 |
| A01_FOOTSTEP_SAND04.WAC | 55 | 16 |
| A01_FOOTSTEP_SAND11.WAC | 58 | 456 |
| A01_FOOTSTEP_SAND12.WAC | 55 | 180 |
| A01_FOOTSTEP_SAND13.WAC | 54 | 112 |
| A01_FOOTSTEP_SAND14.WAC | 54 | 254 |
| BEES.WAS | 8 | 0 |

All block chains reach physical EOF exactly. All token counts consume the payload apart from fewer than eight zero alignment bits. Seven new synthetic regression tests cover mono/stereo token shapes, mono padding, every truncation of fixtures, oversized counts, invalid lengths, trailing data, partial tokens and a repeat without a preceding sample. Together with existing tests, **21 Python tests pass**. No proprietary samples are committed.

## Remaining checkpoint before a converter

- Confirm the original mono wrapper's decremented count and end/status flag behaviour. Physical block count and padded output accounting are established, but the original streaming API's end signalling is not fully traced.
- Implement sample reconstruction independently from this specification, with explicit wrapping and bounded output.
- Validate against more files and an independent/original reference, not merely plausible sound or correct output length.
- Trim mono padding according to RIFF data size and verify output with an independent WAVE reader.

The next milestone is a standalone WAC/WAS-to-WAV converter with these validations. There is no new compressed-audio playback capability in the Android APK from this checkpoint.
