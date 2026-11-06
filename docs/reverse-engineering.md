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

Executable strings identify `.wav`, `.wac`, `.was` extensions and both `aSecComSouMono` and `aSecComSouStereo` header/decompression paths. Subsequent tracing established custom mono delta and stereo cache/repeat codecs; see milestone 3 below. These are not identified as a standard codec.

The new backend validates standard RIFF/WAVE chunks, odd-byte padding, PCM geometry/byte rate/block alignment, and data alignment. Supported playback is integer PCM mono/stereo, 8/16-bit, 1000..192000 Hz, through SDL3 audio streams. Truncated and unsupported codecs fail explicitly. All **30** loose original Disc 1 WAVs pass this parser. MENU1.WAV is mono 22050 Hz, 16-bit PCM with 18866 sample bytes. Actual device sound playback remains unobserved.

A subsequent uploaded `a02_crowd.wav` is 649156 bytes, SHA-256 `ae1ebf5a785a7e2832b0653bc5a16ec5ae31d679d834cf57592ad0eb5853e4bf`. It contains 649112 data bytes: 162278 stereo frames at 22050 Hz, 16-bit PCM, duration 7.35955 seconds. Independent Python wave parsing agrees with the C++ parser. Its nonzero peak is 7064 and RMS approximately 556 in signed 16-bit units. The sample is kept outside Git; optional `JERUSALEM_PRIVATE_CROWD_WAV` checks validate its geometry.

The Android UI previously queued playback and immediately opened a report dialog. SDLActivity's `onWindowFocusChanged(false)` can switch its native state to PAUSED, so starting a sound behind that dialog is unreliable. PCM WAV selection now avoids a report modal and waits for main-window focus before queueing playback. Play/Replay, media-volume routing, live native status/error polling, and output-buffer diagnostics were added. The exact-file host parser regression passes; the new Android playback/replay smoke result and audible hardware playback remain pending. No codec change or audio amplification is applied to the original file.

Full WAC and WAS examples begin with two u32 words, followed by RIFF at offset 8. Subsequent Ghidra tracing established a stored block count and wrapper output length. **Correction:** in the footstep WAC, wrapper output length 29228 includes mono block padding; the embedded RIFF total is 29138, while the stored compressed file is 18716 bytes. Simply carving the embedded RIFF cannot produce valid PCM. The block layouts and bit-token rules now have an independent structural inspector validated on nine complete files; see [milestone 2](wac-was-milestone-2.md). [Milestone 3](wac-was-milestone-3.md) subsequently implemented sample reconstruction and connected Android playback.

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

**Updated verification:** Ghidra tracing established image dimensions, source bytes per pixel, the two compressed streams, codebook expansion and RGB565 colours. All 19 Disc 1 static images now decode and match original index/codebook routines. The spherical projection and angular fields are now verified in milestone 5. See [milestone 4](aqc-milestone-4.md) for exact offsets, bounds and validation.

Executable strings at VA `0x4ab940` onward identify `aSecComAqi::DecompressNode` and `DecompressChannel`. Format strings reference `%s.aqc` and numbered AQC filenames. Diagnostics also identify rotations, puzzles, animation images, hotspots, and accessibility. These suggest relationships between views, transitions, and interactions. A01 registrations, angular hotspot hit tests and arrival cameras are now established in milestone 7; broader script effects remain unresolved. The projection is now established in milestone 5. Android now offers an explicitly limited A01 traversal preview derived from those original registrations.

## 7. Dialogue and DAN

The complete 107-byte DAN is text with CRLF line endings. It has a count of one, then one three-integer record, followed by eight three-integer records and a two-token `718 END` terminator. The eight latter records form contiguous ranges beginning at zero and ending at 717; their third values alternate between zero and one.

**Verified code:** routine VA `0x406aa0` opens the dialogue animation file. At `0x406adc..0x406ae3` it reads a single `%d`; the associated diagnostic calls this the number of levels. At `0x406b12..0x406b3e` it reads that many `%d %d %d` records. At `0x406c7f..0x406ca8` and `0x406dc5..0x406de9` it reads additional triples until scanning returns something other than three fields. Format strings are at `0x4aa814` and `0x4aa7cc`. The original scan does not prove it semantically interprets the `END` word; it stops because that final line is not a triple.

The new parser intentionally accepts the observed explicit-END dialect, exposes levels and intervals as raw integers, checks count/range/order/bounds, and rejects missing termination or trailing tokens. A broader check of **695** original DAN files accepts **610** and rejects **85**. Of these files, 82 have a two-token line after the level count. Two others end without the supported complete terminator, and one violates the supported interval ordering. These cases require investigation. The parser does not silently reinterpret those dialects. Rejection is a documented coverage limitation rather than evidence that the original files are corrupt.

**Unknown:** time units, interpretation of the three level values, mapping from third interval values to animation layers, and synchronization with audio. It does not animate mouths or play dialogue. The newly extracted DIA is line-oriented numeric cue text; executable diagnostics identify a lyric parser with minute/second fields, but a complete subtitle grammar has not yet been implemented.

## 8. Configuration, localization, inventory, scripts, and saves

`aMes.ini` and `aObj.ini` are **not conventional key/value INI**. A key line starts a block; language rows use a three-letter code followed by `#field1#field2`. aObj usually has a label in field1 and an empty field2; aMes commonly uses title/body fields. Order and duplicate keys are significant data and are preserved by the parser. The supplied aMes and aObj parse into **343** and **518** language records respectively (49 and 74 seven-language blocks). Several aObj numeric IDs repeat; a dictionary keyed only by number would lose information.

The parser preserves bytes without assuming an encoding. The Android display uses Windows-1252 as a practical display interpretation; this is not a proven universal encoding for every language or custom icon byte.

`aPre.ini` is a four-integer line, observed as `100 100 -1 1`; meanings are unverified. `fl.ini` begins with count 17 then colon-separated settings, including language, load-source and audio chunk settings. `data/cd.ini` contains one integer and CRLF. The supplied CHECKCD field is documented as evidence only; it is not modified or used to implement an authentication bypass. The generic INI parser is explicitly not used to pretend these dialects are key/value INI. Newly extracted KEYWORDS.INI and SETUP.INI are available for follow-up; their semantics have not been assigned to gameplay.

Executable diagnostics link object presentation, bag animation, accessibility, rotation, puzzle, timer, and variable serialization paths to application LoadSave. Save path strings reference `.ars` files and BMP thumbnails; other diagnostics mention code/mtime/size compatibility checks. **Unknown:** complete save layout, whether these checks validate data versions, and the state machine/script instruction set. No original saves, puzzle scripts, or inventory mechanics are implemented. The aObj table supplies labels and identifiers; it does not by itself define interaction logic. The recovered OUT example is binary data and remains undecoded.

## 9. Validation performed and remaining gates

- C++ host parser suite: **761 explicit checks** including the private AT3/DAN prefixes, complete A01 archive, and original menu WAV expectations, every-byte truncation cases for valid synthetic WAV/BMP/AT3 inputs, malformed offsets/dimensions/chunks, and **4000** deterministic malformed-input rounds. ASan/UBSan passed with local LeakSanitizer disabled because the sandbox cannot run its tracing step. CI retains default leak checking.
- Python regression suite: fourteen tests passed across PE32, raw MODE1/ISO9660 parsing, and the emulator test harness, including truncation, invalid file-backed offsets/RVAs, named/ordinal imports, sector modes, dual-endian mismatch, unsafe directory identifiers, uppercase Android control labels, and diagnostic collection after device disconnection. PE results were cross-checked against `objdump` for the supplied Windows files.
- Real AT3 prefix: 15/325 records listed, all payloads unavailable, incomplete status explicit. Strict extraction refuses the prefix.
- Real DAN: one level, eight intervals, terminal value 718. aMes/aObj: 343/518 parsed records; duplicate keys retained.
- Complete archives: all 325 A01 and 765 system records pass strict parsing and raw extraction. All 30 loose Disc 1 WAVs pass PCM parsing. Subsequent milestones decode AQC static images and WAC/WAS audio; Cinema/CI2 graphics remain unsupported.
- Actions run 42149071429 successfully compiled the ARM64 APK, passed Android lint and packaging checks, and launched the x86_64 native engine to Ready. Its later UI smoke step failed on exact-case matching of an Android-uppercase button label. Accessibility descriptions and case-insensitive fallback now address that failure, with four host regression tests; the new emulator result remains pending. ARM64 hardware launch, storage provider behaviour, audio lifecycle, and image display remain validation gates. No APK was built locally.

## Next investigation steps

Use the complete AT3/Cinema payloads now recovered from the disc to reverse image decompression. Trace AQC decompression xrefs and Cinema header/control readers, recording exact field offsets, termination rules, and allocation sizes. Investigate the DAN variants before relaxing validation. Use multiple files to distinguish constants from counts. Compare every decompressed result against a trusted independent result or a fully traced algorithm; add synthetic edge cases and private golden hashes. Only then connect decoded views to researched scene IDs/hotspots and implement dialogue/inventory/puzzles and versioned native saves.

Architecture references used for the independent app: [SDL Android integration](https://wiki.libsdl.org/SDL3/README-android), [SDL audio stream API](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream), [pinned SDL3 source](https://github.com/libsdl-org/SDL/tree/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540). Original format findings above derive from the supplied bytes and instruction references, not third-party format guesses.

## Native compressed audio implementation

[Milestone 3](wac-was-milestone-3.md) implements bounded Python/C++ WAC/WAS conversion, validates fifteen complete original assets against isolated original decoder instructions, and connects the C++ decoder to Android playback. It also documents stereo RIFF padding normalization. The earlier audio inspection findings above describe the initial checkpoint.

## Native original-image implementation

[Milestone 4](aqc-milestone-4.md) implements bounded Python/C++ AQC static decoding and Android image display. All 19 Disc 1 static nodes have original-routine-verified indices/codebooks and matching native/Python pixels. Animation tails are reported as undecoded. Projection, hotspots, navigation and Cinema decoding remain future work.

## Native panorama camera

[Milestone 5](panorama-milestone-5.md) establishes spherical/equirectangular mapping, a 640×448 logical camera viewport, 85° constructor FOV with 30°..87° limits, and row-stride semantics at AQC offset 0x34. It implements the native camera and SDL2 viewer, and compares 2701 source-coordinate samples against original code. AQC channel framing remains unresolved in one of 19 nodes; no animation playback is claimed.

## Original AQC channel frames and inspection preview

[Milestone 6](aqc-animation-milestone-6.md) implements bounded channel/frame parsing and native/Python patch decoding. All 639 frames across 79 channels in 18 nodes match original decoder indices and have identical native/Python pixels. A09S02N06R01 remains unsupported for channels. Android and SDL2 provide explicit single-channel inspection preview; game-driven activation, original timing, navigation and interactions remain future work. The user has confirmed the preceding 360 camera on Android.

## Original A01 graph and angular interactions

[Milestone 7](a01-navigation-milestone-7.md) documents the initializer, node/exit registration APIs, angular coordinate conversion, hit bounds, enabled states and arrival cameras. A bounded static extractor matches 91 isolated original call captures, producing six nodes, 16 rotation links and one puzzle action. Android and SDL2 consume a user-derived native model; the disabled node-6 return stays disabled and the puzzle reports its implementation boundary. Story callbacks, original activation/timing and full gameplay remain unimplemented. The user has now confirmed animation inspection as well as audio/static-image/360-camera operation on Android.

## A01 node entry, ambient sounds and presentation state

The user confirmed A01 traversal on Android. [Milestone 8](a01-audio-milestone-8.md) traces actual node-entry sound activation, stream looping, original suffix fallback and volume conversion. It adds 17 extracted ambient bindings and native concurrent looping playback on Android, retaining shared tracks during transitions. The optional reference validates 303 original gain/pan arithmetic cases. New scene audio remains pending Android testing. Object/channel presentation APIs and A01 interaction handlers are partially mapped; their initial state, game clocks and story callbacks remain blockers for authentic automatic animation and pickup progression.

## Complete second-disc corpus

Both uploaded split archives passed full ZIP CRC checks; the new Disc 1 matches the existing image. [Disc 2 survey](disc2-corpus-survey.md) records identities, the 208-file ISO index, successful native static decoding for 119 full nodes, structural counts for 498 channels/5880 frames, seven validated AT3 directories (3712 entries), and 80 CNM/CI2 headers. The separate 8617-byte numbered A07 resource contains two patch-framed records and remains outside full-node support. Disc 1 contains the complementary sound/dialogue resources for these areas. These are host coverage results, not original-routine verification for every new image or additional gameplay support.

## Original A01 pickup effects and presentations

The user confirmed scene audio/lifecycle operation. [Milestone 9](a01-pickups-milestone-9.md) establishes fresh visibility, hotspots, channel bindings and normal pickup effects for three A01 objects. Native Android/SDL2 now compose their real single-frame patches, remove them on collection, track the original byte counter and unique milestone, and show a text inventory. Registrations/initialization and 768 isolated original effect traces match extraction. This remains a limited pickup preview: other initial presentations, item use, intro/story callbacks, dialogue, puzzles and saves are not implemented. New device testing is pending.

## Independent native preview persistence

The user confirmed the three supported A01 pickups and inventory. [Milestone 10](native-checkpoints-milestone-10.md) adds a new native checkpoint format for current node, camera and ordered pickups. It rebuilds verified state by replaying independent pickup effects and checks model/data compatibility, exact lengths, CRC32 and field bounds. This introduces no claim about original `.ARS` layout or unimplemented story state. Android and Linux SDL2 expose save/load; eight host groups pass, including 403 new checkpoint checks and private six-node state/pixel restoration. The user subsequently confirmed native save/load works on Android.


## A01 inventory handler and native combination

[Milestone 11](a01-combinations-milestone-11.md) traces 0044b680 and extracts the verified symmetric harness/stone combination. Calls at 0044b6b8..0044b6dc remove 10002 and 10004 and add 10017, whose declaration/label comes from 0047e5af..0047e5c7. An isolated original-handler comparison matches 36 supported-domain argument pairs. Independent native code implements ownership checks, consumption and result insertion with V4 data, Android ingredient dialogs and checkpoint action replay. Host tests pass; new Android results are pending. Bowl/jug effects and palm/character state remain distinct unsupported cases rather than guessed generalizations.
