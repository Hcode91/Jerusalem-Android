# Reverse engineering ledger

## Scope and evidence rules

This pass uses static analysis only: bounded file reads, PE import/section parsing, SHA-256, `objdump -p/-h/-d -Mintel`, printable-string offsets, and x86 call-site tracing. Original Windows code is neither executed nor shipped. Addresses below are preferred virtual addresses in the supplied executable; use the hash to identify the build. A PE timestamp is an embedded header value, not proof of a release date.

**Verified** means directly observed bytes or a described instruction sequence. **Inference** means an interpretation supported by those observations. **Unknown** means no decoder or runtime semantics have been established. A 4096-byte prefix is never passed off as a complete original asset.

The named `jerusalem_analysis.zip` was not accessible as an archive in this pass. Individual files supplied afterward are the actual evidence. The manifest describes additional files that have not arrived; its inventory is user-provided context, not a substitute for examining their contents.

## Evidence inventory

| Received file | Bytes | SHA-256 |
| --- | ---: | --- |
| Jerusalem.ex_ (upload materialized as `.ex`) | 811008 | `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687` |
| CinemaMovieM.dll | 94208 | `0d9759b42d0e7f47fe4ecc8d65f0388195dcf66c557d4619f306243ebe88f43e` |
| AT3 prefix | 4096 | `55dbe352ce8d874e6b9abfb54e93b9065027206be60fe250ea7b4c2653969282` |
| AQC prefix | 4096 | `88d9804242bd4e97b4e90c51ddc66564fdc9dc4f04fdac6439a4aacc42d810d2` |
| CI2 prefix | 4096 | `40421eb07e0df810d32d99a1564f26953a61fe13921969e8a97a6f79c6e0d666` |
| DAN | 107 | `68e8cee74e5bbd051ea0baab403ea16b1d198148591a224f2bcc0c6d15c53d95` |
| aMes.ini | 7280 | `4ae00c40720e3fa806ed784378c48ec0dfb1d885023e149917cad814ce3ddae5` |
| aObj.ini | 7370 | `d9f8a66813f00f3de5b7f9443279c02c3b68d68ede3c56dbc61b5776e73d7e75` |
| aPre.ini | 12 | `b368a8eb6161e61148cccbb360ad7a4b81f722b4a6056468e2317955b137161c` |
| fl.ini | 399 | `2cef6f0fd3ccd79c24704a9b4773566cb81461ddb3d7610387fb55f2fbe76778` |
| data/cd.ini | 3 | `f1b2f662800122bed0ff255693df89c4487fbdcf453d3524a42d4ec20c3d9c04` |

The manifest reports original lengths of AT3 **4439244**, AQC **661841**, CI2 **10185862**, WAC **18716**, WAS **587347**, DIA **79**, DAN **107**, and OUT **1494** bytes. Initially only the AT3/AQC/CI2 prefixes and complete DAN were received from that asset list. The original CUE describes one MODE1/2352 track at index zero. The connected Drive download tool rejected the complete 763565040-byte BIN; subsequent direct public download using the workspace's network-enabled execution succeeded. Its ISO9660 directory now supplies complete private counterparts, including the formerly missing files. No installer was executed.

`disc_extract.py` verified the primary ISO9660 descriptor at logical block 16, the 2048-byte logical block size, MODE1 sector headers, dual-endian directory fields, and bounded extents. The disc lists 2831 entries. The streaming reader does not implement EDC/ECC repair, Joliet/Rock Ridge names, interleaved, multi-volume, or multi-extent files. Its independently generated regression disc has no original content.

Additional complete assets extracted privately from the original disc:

| ISO path | Bytes | SHA-256 |
| --- | ---: | --- |
| DATA/A01.AT3 | 4439244 | `03bece26df818c05a9c0c6ea5745a9a1adfd357fb519f1519fcb9a992b16c31a` |
| DATA/SY.AT3 | 14603762 | `383a40c4ec13b1e5552cc83c9cd4b93bf7d875bfff588bd12b20d4c87951b1b3` |
| DATA/A01/NODE/A01S01N01R01.AQC | 661841 | `c4057f4e7f33e320ed21120f4566ee233cf2a149d9a2740e890d3d4cb6537770` |
| DATA/A01/PLA/ENG/A01S01_AN001.CI2 | 10185862 | `6e6aa2c6f72b251bf9fe73d0f3b9a0bd49cce4d35beb09aa2dde4a6eeebdbec3` |
| DATA/A01/SOUND/A01_FOOTSTEP_SAND01.WAC | 18716 | `5dea93ec8868a1208c81b5bfe5ab7afdc8fee5f368b65cba75ceee903d01a7ec` |
| DATA/A01/SOUND/BEES.WAS | 587347 | `dd5296672903551f11300cc20ee14592348f24a63635ce2636a87ee49559687f` |
| DATA/SY/SOUND/MENU1.WAV | 18910 | `41aa97d5f05e9b56f904cb1250ba5dffb440321f3cf91e292d8bbeb5dab9e2b7` |
| DATA/A01/DIA/ENG/ADRI_DIA002.DIA | 79 | `1be7e26ce0921041e97fe4d4ddde90ef4466208382932f3591404472f5300113` |
| DATA/ENC/C_CHRI.OUT | 1494 | `cd57185fe3d13522c485af94cef1ba1bb7e008a07c43e102d01711c37bbc97c0` |
| MMXIMAGE.DLL | 61440 | `7defec2ee31f54e814e8277d6ba72ab1c0141bfd7ea47a822b790dc4c397fdd1` |
| KEYWORDS.INI | 4469 | `c9f02fa45409ff07ce2aa0e5cace65f805274fdb4f34bcf20ba94e2a8f5d2110` |
| SETUP.INI | 62685 | `71a5cf8dfccb6a84695de320c093632641441febf349187f766277da2f7575f1` |

Private samples and disassembly outputs are excluded from Git. Only independently written code, synthetic fixtures, hashes, layouts, and findings are committed.

## 1. PE executable and platform dependencies

Verified executable characteristics:

- DOS `MZ` and PE signatures are present. The `.ex_` name does not indicate compression for this uploaded file: it is already a PE32 executable.
- Machine `0x014c` (x86), GUI subsystem 2, preferred image base `0x00400000`, entry RVA `0x0009099d` (VA `0x0049099d`). Linker version 6.0; embedded timestamp 2001-12-21 11:26:48 UTC.
- Four sections. `.text`: RVA/file offset `0x1000`, virtual size `0xa4f55`. `.rdata`: RVA/file offset `0xa6000`, virtual size `0x3d3a`. `.data`: RVA/file offset `0xaa000`, raw size `0x1b000`, with a larger virtual reservation. `.rsrc`: RVA `0xd3000`, file offset `0xc5000`, virtual size `0xd60`.
- Imports: KERNEL32 (94 symbols), USER32 (27), GDI32 (15), WINMM (10), DDRAW (1), SHELL32 (1), mmxImage (3), CinemaMovieM (5), DSOUND (1 by ordinal).
- File/thread/event APIs include CreateFileA/ReadFile/SetFilePointer, CreateThread, CreateEventA, and WaitForMultipleObjects. LoadLibraryA/GetProcAddress are present, so the static import table is not guaranteed to enumerate every runtime dependency.

`tools/pe_probe.py` independently reproduces the PE section/import results and relevant string addresses with checked RVA mapping. It supports PE32, named and ordinal imports, and rejects missing file-backed RVAs and truncated headers/tables. It does not disassemble instructions, emulate Windows, or resolve ordinal names from guesswork.

## 2. Graphics

**Verified:** DDRAW.dll imports `DirectDrawCreate`, IAT RVA `0xa6018`. GDI32 imports include StretchDIBits and text/font operations. The executable imports mmxImage32Copy, mmxImage32CopyAlpha, and mmxImage32PreAlpha (decorated C++ names) from mmxImage.dll. Diagnostics identify BMP, TGA, Tgc, Bma, Cin, and Cinema image loader classes.

**Inference:** the original renderer combines DirectDraw surfaces with software image operations and a GDI text path. Individual DirectDraw interface methods are COM vtable calls and cannot be recovered from import names alone. Surface formats, backbuffer behaviour, actual alpha conventions, and panorama projection remain unverified.

The new engine uses SDL3 textures and independently decodes only uncompressed BI_RGB 24/32-bit BMP so far. The fourth byte of a 32-bit BI_RGB pixel is treated as reserved and displayed opaque; no assumption about original alpha is made. Row padding and top-down/bottom-up orientation are handled. No original BMP has been decoded yet. All 325 A01 and 765 system stored image objects begin with the Cinema `CNM UNR` signature despite embedded `.bmp` resource names; there are no loose `.BMP` files in the disc's primary directory tree. The privately recovered MMX DLL's static imports are GDI32 (2) and KERNEL32 (60).

## 3. Audio

**Verified:** DSOUND.dll is imported by ordinal **1** at IAT RVA `0xa6020`; no function name is encoded in that import. WINMM imports mmioOpenA/mmioRead/mmioDescend/mmioAscend and related functions, as well as timeGetTime. This is evidence of DirectSound linkage and RIFF-aware multimedia I/O, not proof of a particular WAC/WAS format.

Executable strings identify `.wav`, `.wac`, `.was` extensions and both `aSecComSouMono` and `aSecComSouStereo` header/decompression paths. Concrete call sites and complete samples are needed to assign codecs and channel layouts. No claim of PCM, ADPCM, zlib, or another codec is made for WAC/WAS.

The new backend validates standard RIFF/WAVE chunks, odd-byte padding, PCM geometry/byte rate/block alignment, and data alignment. Supported playback is integer PCM mono/stereo, 8/16-bit, 1000..192000 Hz, through SDL3 audio streams. Truncated and unsupported codecs fail explicitly. All **30** loose original Disc 1 WAVs pass this parser. MENU1.WAV is mono 22050 Hz, 16-bit PCM with 18866 sample bytes. Actual device sound playback remains unobserved.

Full WAC and WAS examples begin with two u32 words, followed by RIFF at offset 8. The WAC's second word is 29228, matching the embedded RIFF's claimed decoded total length, but its stored file is only 18716 bytes. The WAS counterpart likewise claims a larger RIFF length than stored bytes. This is **not** a valid embedded complete WAV to carve: compressed payload reconstruction remains necessary and unsupported. The scanner correctly refuses an incomplete RIFF candidate. The first-word role and compression algorithm are still unknown.

## 4. Cinema/CI2 video and animation

**Verified:** Jerusalem imports CinemaMovieGetNumberOfFrames, CinemaMovieClose, CinemaMoviePlayNextFrame, CinemaMovieInit, and CinemaMovieSetSynchroOff. The supplied CinemaMovieM.dll is PE32 x86, image base `0x10000000`, entry RVA `0x7273`, embedded timestamp 1999-07-13 14:38:48 UTC. Its static imports are KERNEL32 (76), USER32 (5), and GDI32 (1, StretchDIBits).

Its 27 named exports include width/height/frame-rate/duration queries, exact/next/previous frame playback, sound channel controls, and synchronization modes. Diagnostics identify SControl/TControl, frame headers, sound reads/skips, and decompression sequences. These provide concrete starting points for tracing the codec. They do not establish a standard movie container.

The supplied CI2 prefix begins with the eight bytes `43 4e 4d 20 55 4e 52 00` (`CNM UNR` plus NUL). Unsigned little-endian words at offsets `0x08`, `0x0c`, `0x1c`, `0x20`, `0x24` are respectively 310, 1500, 310, 310, 2067. **Unknown:** versioning, timebase, packed dimensions, frame-table layout, frame compression, and sound interleaving. The prefix is only 4096 of 10185862 bytes; the full counterpart has now been extracted for follow-up codec tracing. No CI2 decoder is implemented.

## 5. Confirmed AT3 directory structure

This is the first proprietary binary structure implemented. It is supported by both sample bytes and original loader instructions.

At VA `0x41cffb..0x41d004`, the loader reads a **32-byte header** into the object at `+4`. At `0x41d090` it loads the loop count from object `+0x10`, corresponding to file header `+0x0c`. At `0x41d0a6..0x41d0ac`, each loop reads exactly **255 bytes**. Record construction at `0x41cee0` copies a NUL-terminated name and stores fields at record `+0xf3`, `+0xf7`, `+0xfb`. GetRec at `0x41d396..0x41d3a1` seeks to the field at `+0xf3`; `0x41d3f3..0x41d3fd` reads the byte count at `+0xf7`.

| Header offset | Type | Observation / meaning |
| --- | --- | --- |
| `0x00` | 8 bytes | `ATIII\0\0\0` in this build/sample |
| `0x08` | u32 LE | 82875, equals count × record width in this sample |
| `0x0c` | u32 LE | 325 records; confirmed loader loop count |
| `0x10` | u32 LE | 255; agrees with loader's fixed read size |
| `0x14..0x1f` | 12 bytes | zero in sample; semantics unknown |

| Record offset | Type | Meaning |
| --- | --- | --- |
| `0x00..0xf2` | 243 bytes | NUL-terminated stored resource name, padded in sample |
| `0xf3` | u32 LE | absolute file offset used by GetRec |
| `0xf7` | u32 LE | stored byte count used by GetRec |
| `0xfb` | u32 LE | auxiliary size-like field; exact semantics not established |

The sample's first record has offset **82907** (`32 + 82875`), stored length **7619**, auxiliary field **10422**. The next record starts at **90526**, exactly the previous offset plus stored size. Fifteen complete directory records fit in the prefix; the sixteenth record is partial. All referenced payloads are beyond the prefix. Strict parsing and raw extraction later succeeded for **all 325 A01 records** and **all 765 system records** using the complete original archives. Their Cinema headers establish that archive extraction and image decoding are separate operations.

**Inference:** the auxiliary field may describe an uncompressed size. It is deliberately named `auxiliary_size`, not asserted to be a decoded length. Resource names ending `.bmp` do not prove stored bytes contain a BMP header. Payload compression is **unknown**.

`parse_at3` supports strict complete-file parsing and explicitly opted-in prefix inspection. It validates signature, directory geometry, bounded names, payload ranges, and arithmetic before any extraction. `extract-at3` requires complete directory and payload ranges; writes original stored bytes unchanged as numeric `.stored` files; and never constructs filesystem paths from embedded names. No proprietary decompression is implied.

## 6. AQC and scene/navigation data

**Verified sample observations:** AQC has no standard image signature at offset zero. u32 LE values at offsets 0, 4, 8, 12 are **593055**, **2048**, **856**, **2**. IEEE754 interpretations at `0x14`, `0x18`, `0x1c` yield **360**, **-75**, **75**. Words at `0x24`, `0x2c`, `0x30`, `0x34` are **2048**, **856**, **3506176**, **4096**. The arithmetic `2048 × 856 × 2 = 3506176` holds.

**Inference:** these may encode image dimensions, a two-byte pixel representation, and angular bounds for a panorama. A single numerical coincidence is insufficient to assign field semantics or implement a decoder. No AQC pixels have been recovered.

Executable strings at VA `0x4ab940` onward identify `aSecComAqi::DecompressNode` and `DecompressChannel`. Format strings reference `%s.aqc` and numbered AQC filenames. Diagnostics also identify rotations, puzzles, animation images, hotspots, and accessibility. These suggest relationships between views, transitions, and interactions. Actual node graph layout, coordinates, hotspot hit testing, projection, and script effects have not been traced. The Android scene manager currently has only an inspection scene; there is no invented game navigation.

## 7. Dialogue and DAN

The complete 107-byte DAN is text with CRLF line endings. It has a count of one, then one three-integer record, followed by eight three-integer records and a two-token `718 END` terminator. The eight latter records form contiguous ranges beginning at zero and ending at 717; their third values alternate between zero and one.

**Verified code:** routine VA `0x406aa0` opens the dialogue animation file. At `0x406adc..0x406ae3` it reads a single `%d`; the associated diagnostic calls this the number of levels. At `0x406b12..0x406b3e` it reads that many `%d %d %d` records. At `0x406c7f..0x406ca8` and `0x406dc5..0x406de9` it reads additional triples until scanning returns something other than three fields. Format strings are at `0x4aa814` and `0x4aa7cc`. The original scan does not prove it semantically interprets the `END` word; it stops because that final line is not a triple.

The new parser intentionally accepts the observed explicit-END dialect, exposes levels and intervals as raw integers, checks count/range/order/bounds, and rejects missing termination or trailing tokens. A broader check of **695** original DAN files accepts **610** and rejects **85**. Of these files, 82 have a two-token line after the level count; other rejected cases also require investigation. The parser does not silently reinterpret those dialects. Rejection is a documented coverage limitation rather than evidence that the original files are corrupt.

**Unknown:** time units, interpretation of the three level values, mapping from third interval values to animation layers, and synchronization with audio. It does not animate mouths or play dialogue. The newly extracted DIA is line-oriented numeric cue text; executable diagnostics identify a lyric parser with minute/second fields, but a complete subtitle grammar has not yet been implemented.

## 8. Configuration, localization, inventory, scripts, and saves

`aMes.ini` and `aObj.ini` are **not conventional key/value INI**. A key line starts a block; language rows use a three-letter code followed by `#field1#field2`. aObj usually has a label in field1 and an empty field2; aMes commonly uses title/body fields. Order and duplicate keys are significant data and are preserved by the parser. The supplied aMes and aObj parse into **343** and **518** language records respectively (49 and 74 seven-language blocks). Several aObj numeric IDs repeat; a dictionary keyed only by number would lose information.

The parser preserves bytes without assuming an encoding. The Android display uses Windows-1252 as a practical display interpretation; this is not a proven universal encoding for every language or custom icon byte.

`aPre.ini` is a four-integer line, observed as `100 100 -1 1`; meanings are unverified. `fl.ini` begins with count 17 then colon-separated settings, including language, load-source and audio chunk settings. `data/cd.ini` contains one integer and CRLF. The supplied CHECKCD field is documented as evidence only; it is not modified or used to implement an authentication bypass. The generic INI parser is explicitly not used to pretend these dialects are key/value INI. Newly extracted KEYWORDS.INI and SETUP.INI are available for follow-up; their semantics have not been assigned to gameplay.

Executable diagnostics link object presentation, bag animation, accessibility, rotation, puzzle, timer, and variable serialization paths to application LoadSave. Save path strings reference `.ars` files and BMP thumbnails; other diagnostics mention code/mtime/size compatibility checks. **Unknown:** complete save layout, whether these checks validate data versions, and the state machine/script instruction set. No original saves, puzzle scripts, or inventory mechanics are implemented. The aObj table supplies labels and identifiers; it does not by itself define interaction logic. The recovered OUT example is binary data and remains undecoded.

## 9. Validation performed and remaining gates

- C++ host parser suite: **434 explicit checks** including the private AT3/DAN sample expectations, every-byte truncation cases for valid synthetic WAV/BMP/AT3 inputs, malformed offsets/dimensions/chunks, and **4000** deterministic malformed-input rounds. ASan/UBSan passed with local LeakSanitizer disabled because the sandbox cannot run its tracing step. CI retains default leak checking.
- Python regression suite: ten tests passed across PE32 and raw MODE1/ISO9660 parsing, including truncation, invalid file-backed offsets/RVAs, named/ordinal imports, sector modes, dual-endian mismatch, and unsafe directory identifiers. PE results were cross-checked against `objdump` for the supplied Windows files.
- Real AT3 prefix: 15/325 records listed, all payloads unavailable, incomplete status explicit. Strict extraction refuses the prefix.
- Real DAN: one level, eight intervals, terminal value 718. aMes/aObj: 343/518 parsed records; duplicate keys retained.
- Complete archives: all 325 A01 and 765 system records pass strict parsing and raw extraction. All 30 loose Disc 1 WAVs pass PCM parsing. No original graphics decoded and no functional CI2/AQC/WAC/WAS codec is claimed.
- Android source and Actions workflow are implemented but not yet run in GitHub Actions; device launch, storage provider behaviour, audio lifecycle, and rendering remain validation gates. No APK was built locally.

## Next investigation steps

Use the complete AT3/Cinema payloads now recovered from the disc to reverse image decompression. Trace AQC decompression xrefs and Cinema header/control readers, recording exact field offsets, termination rules, and allocation sizes. Investigate the DAN variants before relaxing validation. Use multiple files to distinguish constants from counts. Compare every decompressed result against a trusted independent result or a fully traced algorithm; add synthetic edge cases and private golden hashes. Only then connect decoded views to researched scene IDs/hotspots and implement dialogue/inventory/puzzles and versioned native saves.

Architecture references used for the independent app: [SDL Android integration](https://wiki.libsdl.org/SDL3/README-android), [SDL audio stream API](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream), [pinned SDL3 source](https://github.com/libsdl-org/SDL/tree/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540). Original format findings above derive from the supplied bytes and instruction references, not third-party format guesses.
