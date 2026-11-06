# Milestone 4: original AQC static images

The native core and standalone Python tool now decode the static image in all **19 complete AQC files on Disc 1**: six A01 nodes and thirteen A09 nodes. All are 2048 × 856 pixels. The first A01 output is the original oasis/palm-tree scene, visually inspected locally. No game images, exported screenshots, original binaries or decompiler output are committed.

The Android inspector displays these images through its existing touch pan/zoom renderer. This is a flat decoded image, not a researched 360° projection. Embedded animation channels, hotspots, scene transitions and gameplay remain unimplemented.

## Original code evidence

The verified executable identity is recorded in the earlier milestones. The following addresses were traced in Ghidra, with decompilation checked against instruction listings:

| Entry | Observed role |
| --- | --- |
| `004118a0` | Static node header copying and two compressed-stream dispatches |
| `0041dad0` | 13-bit literal / 6-bit cache-index / repeat word decoder |
| `0041dc40` | 16-bit literal / 6-bit cache-index / repeat word decoder |
| `00411820` | Compressed buffer access |
| `00403b10` | CAquatorStream initialization and four-pixel vector expansion |
| `00403960` | Colour conversion: RGB565 components shifted to BGR byte positions |
| `00405400` | Image header/indices/codebook loading into expanded buffers |
| `004052c0` | Expanded-buffer allocation, including separate codebook space |
| `00405330`, `00405340` | Rectangle extents used to calculate pixel count |

`tools/ghidra/ResourceTrace.java` exports private decompilation, instruction listings and callers for explicitly selected function entries or addresses within existing functions. `LoaderMilestone.java` also finds rotation allocation, activation and save/load diagnostics. Private exports remain under `private/ghidra-analysis/`.

## Supported static-node layout

All integers are little-endian; compressed bits are MSB first within each byte. There is no magic string. Recognition requires the following complete header model, followed by bounded streams; a filename alone does not prove the format.

| Offset | Type | Established layout / supported constraint |
| --- | --- | --- |
| `0x00` | u32 | First compressed-stream length |
| `0x04` | u32 | Image width |
| `0x08` | u32 | Image height |
| `0x0c` | u32 | Source bytes per pixel, supported value 2 |
| `0x10 .. 0x1f` | four f32 | Finite fields observed as 0, 360, -75, 75; angular interpretation is likely, but projection semantics remain unverified |
| `0x20 .. 0x2f` | four u32 | Full image rectangle, supported values 0, width, 0, height |
| `0x30` | u32 | Source pixel bytes = width × height × 2 |
| `0x34` | u32 | Observed header word `0x1000`; bit semantics are not established |
| `0x38` | byte array | First compressed stream |
| `0x38 + first_length` | u32 | Second compressed-stream length |
| immediately after | byte array | Second compressed stream |
| after both streams | bytes | Additional channel data; reported but not decoded or validated |

The decoder supports widths divisible by four, dimensions 1..8192, a maximum 64 MiB source and a maximum 64 MiB RGBA image. Unknown header variants are rejected. A complete static section can be decoded even when its following channels are absent; this does not establish that the entire AQC container is complete.

## Compression and pixels

Both streams use the same 64-entry cache/repeat mechanism established for stereo audio, including the first-minimum use-position replacement rule and the absence of a use-position update on the literal path. Each stream starts with zeroed cache state.

| Token | First stream | Second stream |
| --- | --- | --- |
| `0` + literal | 13-bit vector index | 16-bit RGB565 pixel |
| `10` + six bits | Reference cache slot | Reference cache slot |
| `11` | Repeat last word | Repeat last word |

The first stream produces width × height / 4 indices. The observed codebook contains **8100 vectors × four pixels = 32400 words**; the implementation deliberately supports this verified fixed size, rather than generalizing to other codebook variants. An index must be below 8100. All observed maxima satisfy that limit.

Each index selects four adjacent RGB565 pixels, copied to the flat output in stream order. Their standard RGB byte components are `(word >> 8) & 248`, `(word >> 3) & 252`, `(word << 3) & 248`. This reproduces the original component left shifts; white is (248,252,248), not bit-replicated (255,255,255). The native renderer adds opaque alpha.

After the exact expected word count, only zero byte-alignment bits (fewer than eight) are accepted. Undefined first-word repeats, out-of-range vector indices, partial tokens and unexplained stream bytes are errors. The static parser does not scan for replacement signatures or silently synthesize missing pixels.

For `A01S01N01R01.AQC`, the first stream is 593055 bytes, the second is 56917 bytes, and the static section ends at byte 650032. The physical file is 661841 bytes; **11809 trailing bytes remain undecoded**. Other nodes have considerably larger tails. Treating those bytes as navigation or animation records without tracing them would be unsupported.

## Independent validation

`tools/validate_aqc_reference.py` uses the optional, bounded CPU reference harness in `tools/reference_cpu.py`. Only hash-identified original decoder instruction ranges and a newly written buffer-access return stub execute. There are no Windows APIs and no game startup. The reference is research-only and never used by the engine or APK.

For all 19 original nodes:

- The independently implemented Python index stream matches the original routine for all 876544 meaningful bytes.
- The Python codebook matches the original routine for all 64800 meaningful bytes.
- C++ PPM pixels match Python RGB pixels byte for byte.

The original loops can emit an extra word from alignment bits, as in the stereo audio routine. Comparison uses meaningful word counts. The new decoders stop at the established counts and validate alignment instead of reading beyond a payload.

First A01 output identities:

| Data | SHA-256 |
| --- | --- |
| Indices (876544 bytes) | `1215c0c980048cf212f59827c8e3f3582203c29dabab22511bcb9b1fc087c518` |
| Codebook (64800 bytes) | `1adbf92514dd1a09e830380dff167ea277ff806af815846b299e2b72fa60189c` |
| RGB pixels (5259264 bytes) | `54d2a2a1a7919480502fcee81ee57f48d2ba244fe1074262060c9fc8d442f72d` |

Synthetic regressions cover exact four-pixel expansion and RGB565 component values, tail reporting, every truncation before the static section completes, header/range/NaN errors, undefined repeats, cache references, alignment, and PNG CRC/chunk/pixel validity. CTest also checks native PPM output and native rejection of malformed fixtures. The shared C++ cache decoder retains the audio regressions. AddressSanitizer/UndefinedBehaviorSanitizer host tests pass, with the documented local LeakSanitizer environment limitation. There are now **36 Python tests**, alongside native tests and deterministic malformed-input rounds.

The Actions emulator test additionally generates an independent AQC colour-bar fixture, opens it through the Android UI, checks successful native decode/display, and captures a screenshot. Android builds run exclusively in Actions; this new path still requires Actions and physical-device results.

## Commands

```sh
python3 tools/aqc_decode.py /path/to/A01S01N01R01.AQC /path/to/new-image.png
build-host/jinspect decode-aqc /path/to/A01S01N01R01.AQC /path/to/new-output-directory
# Native output is new-output-directory/decoded.ppm.

# Optional private original-routine reference, using the research environment:
private/validation-venv/bin/python tools/validate_aqc_reference.py \
  /path/to/Jerusalem.ex_ /path/to/node.aqc
```

No third-party packages are required by the Python exporter itself. Output paths must be new and proprietary outputs should stay outside public commits.

## Next evidence needed

Rotation allocation is at `004097d0`, activation at `004293e0`, and rotation save/load at `00409250`. Activation recentres the original cursor at (320,240), suggesting a 640×480 interaction surface; this does not establish the rendered viewport or FOV. Continue from rotation/background initialization to the actual sampling/mapping routine before implementing a projection. The requested SDL2 desktop viewer should follow that evidence rather than adopt an assumed spherical/cylindrical projection. Channel/frame timing and Cinema/CI2 image decoding remain separate work.
