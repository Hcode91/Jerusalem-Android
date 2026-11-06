# Milestone 12: palm, a character offer, spoken feedback and native Cinema

This is a combined implementation pass following the user's request for larger milestones. It adds four original-data capabilities: collecting the palm branch, offering the braided matting to a character, playing one verified spoken response with DIA subtitles, and decoding/previewing progressive CNM/CI2 movies. The existing A01 preview remains separate from the original scripted opening and full puzzle/story progression.

## Palm and first character action

The supported executable remains SHA-256 `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`. The extractor reads the following registrations/effects from the original user-selected executable and writes a V5 native model. It never runs or retains that program.

| Operation | Original evidence | Native result |
| --- | --- | --- |
| Palm registration | 0047e99f..0047ea18 | Item 10001, node 1010105, rect [1005,1170) × [290,394), static channel 0 on nodes 1010105/1010106 |
| Palm pickup | 0044ab55..0044ac15 | Assign variable 10002 = 1, add palm, hide presentation 0, disable its hotspots; no shared pickup-counter increment or milestone |
| Conditional palm appearance | Same handler | When variable 10003 is 1/2/3, hide character 10006/10008/10009 presentation 1 and show presentation 0 |
| Matting offer | 0044a865..0044a8d5 | Selected item 10003, target 10007, event 0: set variable 10003 = 1, remove matting, show 10006 presentation 1 if variable 10002 is zero, otherwise presentation 0; enable target 10006 |
| Character presentations | 0047e6d2..0047e99f | Original static AQC channel bindings for presentation pairs of 10006/10008/10009 |
| Spoken feedback | 0044a661..0044a6a9 | Using harness 10002, stone 10004 or sling 10017 on target 10000/event 0 starts dialogue/sound 11000; item is retained |
| Feedback registration | 0047fab3..0047fac6 | Original `ADRI_DIA002.WAC` basename, area A01; paired English `.DIA` text |

Fresh variables and hidden character presentations are checked against 0044f0db..0044f145; palm visibility is checked against 0044f095..0044f0a3. The independent core initializes only the supported state. V5 adds assignment pickups, bounded static presentations, conditional pickup switches, interaction targets, consuming item-use rules and response-only rules. It is data driven; the runtime contains no hard-coded Jerusalem item IDs. Native rule IDs are extraction identifiers, not original script addresses.

`validate_story_reference.py` executes only the bounded original dispatch with capture stubs replacing the relevant APIs. It matches **256 palm state cases, 256 matting-offer palm-variable cases and six response/event cases**. Existing navigation/audio bindings, 768 ordinary-pickup effects and the sling combination remain verified against their original captures. Three character phases have proven palm presentation switches; only the first phase can currently be reached through implemented gameplay. Later phase transitions remain unavailable.

The Android inventory adds **Use item**. Select matting and tap the **USE** marker at A01S01N05R01. Its offer consumes the matting, selects the correct original static character presentation and enables the successor target. Collecting the palm before or after offering matting produces the corresponding appearance. Ordinary conversation/puzzle entry and further interactions at the successor target explicitly remain unsupported. Pickup hotspots take precedence over item-use attempts in overlapping native-preview regions; this does not claim full original object-dispatch ordering.

For the independent spoken response, select harness, stone or sling and use the marker at **A01S01N04R01**. Original audio is resolved under `data/a01/sound/eng`, with subtitles under `data/a01/dia/eng`. Missing/unsafe/ambiguous assets or unsupported subtitles fail visibly without consuming the selected item. There is no dialogue tree or mouth-animation scheduler yet.

## Native checkpoint revision 3

Native saves now retain palm assignment, consumed matting, character phase, enabled targets and presentation changes by replaying the action log. Bit 31 marks combination-result actions; bit 30 marks consuming item-use rule IDs in revision 3. Response-only playback does not alter persistent state and is not recorded. Story-enabled models reserve pickup, combination-result and use-rule IDs below `0x40000000` to keep the tags unambiguous.

The envelope is still format version 1, now with state revision 3 for V5 features. It accepts at most 64 actions, **68..324 bytes**. This original preview can perform four pickups, one combination and one consuming offer; its maximum is 92 bytes. Unknown/repeated actions, missing ingredients or disabled targets fail replay. Model fingerprints include new rules and exclude display translations; node-data hashes still cover the complete six AQCs.

Revision-1 and revision-2 files migrate only if their full legacy model projection and node-data hashes match. The projection removes newly introduced assignment pickups and story rules, and removes combinations for revision 1. Existing pickup/sling state is preserved; new palm/character state starts fresh. A previously generated original-data checkpoint was loaded against the new extracted V5 model. Original Windows `.ARS` saves and playback positions remain unsupported.

## Verified DIA dialect

Ghidra traces at 00406980/00406e40 establish a line-based text loader and numeric timestamp parser. 00407250 compares `GetTickCount() - start` with cue times; therefore the observed integer timestamps are **milliseconds**. 00407150 chooses the first inclusive interval, so the earlier caption remains active at an exact shared boundary. The native timeline preserves that boundary behavior.

The bounded parser supports the observed integer-timestamp dialect, optional `#` secondary text, Windows-1252 text conversion and NBSP normalization. It requires strictly increasing times and a final timed `END`, rejects NUL/control bytes and caps input/line/cue counts. The separate comma/timecode grammar seen in the original parser is explicitly rejected until its semantics are fully established. The complete original ADRI_DIA002 sample has two captions at 0/1402 ms and a terminal boundary at 4045 ms.

Android renders UTF-8 subtitles in a normal accessible TextView. Its monotonic playback clock pauses with the audio lifecycle transition. It is a playback clock, not a measurement of speaker latency. Navigation, Stop audio and opening another resource clear the subtitle session. Seeking, DAN-driven mouth animation and save/resume inside speech are unavailable.

## Native Cinema decoder and corrections to the supplied notes

The user supplied preliminary CNM findings from another investigation. Those were used as leads, then checked against our original files and the DLL with SHA-256 **`0d9759b42d0e7f47fe4ecc8d65f0388195dcf66c557d4619f306243ebe88f43e`**. Newly written C++ code implements bounded progressive codebook/index decoding; it does not embed or translate decompiler output. Original instructions are used only by the private optional comparison harness.

Three material corrections were found:

- **Sound byte 0 is channels minus one.** 10002670 and 10002b50 pass that byte plus one into the audio initializer. Thus `01 10 22 56` describes **stereo, 16-bit, 22050 Hz**, not mono. This explains the apparent doubled duration.
- **Global FPS = header u32 at 0x0c × 0.01.** Exports 10004ad0/10004ba0 use this scale and 1000 ms per second. A value of 1500 means 15 FPS. GetDuration at 10004bf0 returns the u32 at 0x24 multiplied by **10 milliseconds**, not 20.
- The frame-header fields at +0x14/+0x18 are **1500 in these movies**, not universally zero. The verified progressive image decoder does not read them; they are retained/ignored by that decoder. The preview uses fixed global FPS. Other per-frame synchronization modes are not reproduced.

### Stream structure and implemented tokens

All integers are little endian. The 192-byte header starts `CNM UNR\0`; width/height are unaligned signed i32 at 0x11/0x15. Bpp at 0x19 must be 32. The nonzero 0x1a interlaced flag is rejected. Byte 0x1b counts up to three sound configurations, each 16 bytes; u32 0x1c counts frames and 8-byte control records. The decoder skips that bounded table and processes the stream sequentially; it does not implement seeking.

`Z/A/B` carry u32-sized raw PCM for configurations 0/1/2. `T` carries an 8-byte codebook header and its sized payload. `S/U` carry a 47-byte frame header and sized image data. `X` skips its sized frame payload and preserves the prior destination. Frame fields +0/+4 are payload size/embedded-codebook offset, +8/+0a are N/D, and +0c/+10 are row width/row count. Supported D values are 4 or 2, with fixed 16-byte codebook stride and D*4 output bytes per index.

The C++ decoder implements LSB-first codebook deltas, literal/up/neighbor/offset/MRU index tokens, per-frame MRU/seen reset, and persistent neighbor candidates. Index 0 preserves the destination. Full groups can overshoot the final output count: the decoder performs the bounded group-state operations but only validates/copies actually rendered indices. This matters for five small A01 stills; rejecting unused final indices initially failed those assets. Regression coverage now exercises an invalid unused tail index without allowing an invalid rendered index.

The original buffer allocator supplies a 64-byte zero-filled tail. Native bit reads emulate at most those 64 bytes and cannot read arbitrary memory. Physical chunk lengths, codebook dimensions, geometry and frame counts remain checked. Zero padding follows original decoder behavior; a media parser cannot detect every possible in-place corruption without a source checksum.

Other limits include 64 MiB input, dimensions up to 4096², at most 100000 declared frames, codebook N up to 4095, and a cumulative 512 Mi-pixel decode budget. Raw/PPM export has a 1 GiB limit; per-track PCM export has a 64 MiB limit. Unsupported headers/tags or exhausted limits stop with diagnostics. Output is BGRX bottom-up internally, converted to top-down opaque RGBA for display. Transparent sprite blending is not established by this preview.

### Independent comparisons

The isolated DLL harness maps only hash-identified decoder instructions and private buffers. It executes no Windows APIs, file loading or game authentication. Comparisons cover:

| Input | Result |
| --- | --- |
| 1090 A01.AT3/SY.AT3 stills | All full-frame FNV64 checksums match; one was also compared as complete raw bytes |
| A02S02_AN016.CI2, 134 frames | Every BGRX byte of every frame matches |
| A01S01_AN004.CI2, 114 frames | Every BGRX byte of every frame matches |
| A01S01_AN007C.CI2, 119 frames | Every BGRX byte of every frame matches |
| Independent D=2 generated stream, 3 frames | All raw bytes match the original routine |
| Original AN004 stereo audio export | WAV PCM matches all 670280 source PCM bytes; channels/rate/bit depth checked |

Original movie identities:

```text
A02S02_AN016.CI2   5369879 bytes  SHA256 20f7d50ae69e5cb1847d12b62692ac2eb19ac6b63d246816d3da732427c042d5
A01S01_AN004.CI2   6587630 bytes  SHA256 d709c0db8489940bc0b0790d57ecc6d81e0d03ea9c2769988a706999b9a53756
A01S01_AN007C.CI2  6175045 bytes  SHA256 5f16ab02eba06dee9e9a6f84f97bc952139e404b946b113297bdbc288881240b
```

Original movie comparisons cover progressive D=4 and zero/one configured audio tracks. D=2 and A/B track routing have synthetic tests; original multi-track and interlaced files remain unverified. This is not a claim that every movie on both discs is supported.

## Playback and tools

Android **Browse assets** now recognizes complete CNM resources. Multi-frame movies start after the browser dismisses and focus returns, avoiding the modal-dialog audio problem. **Replay movie** restarts the selected clip; **Stop audio** also stops its preview. Frame decoding is incremental, textures are reused, PCM is queued to SDL, and clocks/audio pause on background transitions. The preview plays the first configured audio track. It does not execute cinematic completion callbacks or advance the story.

SDL2 has a separate native movie player sharing the same decoder. It queues PCM, preserves aspect ratio and supports pause/resume/background handling. Original AN004 completed all 114 frames through the SDL2 dummy video/audio backend. Physical output and Android performance/synchronization remain device checks.

```sh
# Inspect/decode all frames and show per-frame checksums:
build-host/jinspect cinema /path/to/movie.CI2
# New directories only; no original resource is changed:
build-host/jinspect extract-cinema /path/to/movie.CI2 /path/to/new-ppm-and-wav-directory
build-host/jinspect extract-cinema-audio /path/to/movie.CI2 /path/to/new-wav-directory
python3 tools/cinema_extract.py /path/to/movie.CI2 /path/to/new-png-and-wav-directory
build-host/jinspect dia /path/to/ADRI_DIA002.DIA
build-host/jerusalem_cinema /path/to/movie.CI2
# Space pauses, Escape quits; --exit-at-end is available for automation.
# Optional private comparisons; requires Unicorn and the user's original binaries:
python3 tools/validate_cinema_reference.py --exact /path/to/CinemaMovieM.dll build-host/jinspect /path/to/movie.CI2
python3 tools/validate_story_reference.py /path/to/Jerusalem.exe /path/to/a01-navigation.tsv
```

The existing SDL2 panorama viewer can collect the palm and restore V5 checkpoints, but the new interactive Use-item/dialogue interface currently lives in Android. The independent core and host tests exercise its state rules; a unified desktop gameplay UI remains future work.

## Regression and next device check

All **nine host CTest groups** pass under ASan/UBSan with the documented local LeakSanitizer restriction. The new story/media group has **1009 assertions** covering both palm/offer orders, retained counters/milestones, enabled targets, response-only rules, revision-1/2 migration, malformed action sequences, subtitle boundaries/encoding, CNM literal/neighbor/MRU/offset/up tokens, group-copy/tail behavior, destination preservation, D=2/4 output, Z/A/B routing, dropped frames and truncation. Private original-node state/pixel tests exercise both action orders on all six nodes. Existing parser/malformed-input regressions remain enabled. SDL2 smoke coverage includes timed CNM playback and native-backed PNG/WAV export with independently checked stereo sample counts.

The Actions Android smoke test now exercises V5 palm/character use, restart/restoration, generated speech/DIA advancement, and a generated 100-frame stereo movie. No original data enters CI. **The new Android build/emulator and phone results are pending. All APK builds remain exclusive to GitHub Actions.**

After installing the new APK:

1. Long-press **A01 navigation** and select the original executable again for V5 connections. Existing compatible checkpoints can be loaded.
2. At **A01S01N05R01**, collect the palm and use **Inventory → Use item → Braided matting** on the nearby USE marker. Try the opposite order in a fresh session; the original character appearance should change accordingly. Save/restart/load to check persistence.
3. At **A01S01N04R01**, use harness, stone or sling on its USE marker to hear the original response and see subtitles. English sound and DIA folders must both be imported.
4. Browse/open **A01S01_AN004.CI2** or **A01S01_AN007C.CI2** under `a01/pla/eng`; check motion, stereo sound, Replay movie, Stop audio and background/resume.

The next substantial engine work is connecting puzzle-screen backgrounds/hotspots, movie completion handlers and dialogue chains into an original sequence. A working decoder alone does not establish those gameplay transitions, and the native preview does not bypass them.

## First combined Android run and browser-test correction

Actions run [42276947585](https://github.com/Hcode91/Jerusalem-Android/actions/runs/42276947585) passed host tests, APK compilation/lint and ARM64 packaging. Its emulator test completed the new story pickup/use, checkpoint restoration and subtitle advancement before failing to select the movie fixture. The captured UI shows a scrollable Imported assets ListView whose visible rows end at `synthetic-labels.ini`; `synthetic-movie.CI2` is below the fold. The harness now searches the vertical list in both directions using swipes inside its viewport. Regression tests cover offscreen selection, reverse search and bounded failure for a missing asset. Movie playback assertions remain enabled; the corrected emulator result and physical-phone checks are pending.
