# Milestone 3: native WAC/WAS audio conversion

`src/audio_codec.cpp` and `tools/wac_decode.py` independently implement the verified dialect described in [milestone 2](wac-was-milestone-2.md). Both reconstruct actual game audio. The Android inspector routes supported WAC/WAS through the C++ decoder and its existing SDL PCM output path, including Replay, Stop and background pause/resume. This does not implement dialogue scheduling or scene playback.

## Implemented behaviour

- Fixed 44-byte embedded PCM headers, mono/stereo 16-bit samples.
- Mono sign-magnitude deltas, wrapping 16-bit accumulation, predictor/delta persistence across blocks, and final trimming to the embedded RIFF data length.
- Stereo literal/cache/repeat tokens, first-minimum replacement selection, use-position updates and cache reset per block.
- Bounded reads and output allocation, exact block-chain accounting and token termination checks.
- Rejection of unknown/nonzero mono prefixes and alignment bits, undefined initial mono delta, first-token stereo repeat, unsupported headers, truncation, oversized output and trailing bytes.
- A 64 MiB input/output limit and 10000-block limit. Large-resource streaming remains future work.

The converter consumes all 256 mono samples per stored block before trimming. It does not reproduce the original streaming wrapper's API status flags; those are unnecessary for offline decoding and remain a separate investigation.

## Independent reference validation

`tools/validate_audio_reference.py` is an optional research tool requiring Unicorn 2.1.4 and the user's original executable with SHA-256 `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`. It executes only the two hash-identified decoder instruction ranges in an isolated x86 CPU emulator, replacing the buffer-pointer accessor with a trivial return stub. It provides no Windows APIs, executable entry point, installation, CD authentication or host filesystem access to the emulated routines. An execution-range guard and instruction/time budgets bound the run. No executable bytes are distributed.

This reference tool is separate from the new Python/C++ implementations. It is never packaged in the APK or used by the runtime. The APK performs all sample reconstruction in native C++.

The Python decoder's complete emitted WAVE files match the original routines **byte for byte** for all eight complete A01 footstep WACs, three English dialogue WACs from A01/A09/A10, `BEES.WAS`, and three A03 footstep WAS files (15 originals total). The C++ output in turn matches those Python files byte for byte. Python's standard `wave` reader independently accepts the resulting files and their declared frame counts. No output recordings or original game data are committed.

The stereo reference emits two more bytes than each advertised block output because its bit loop also consumes byte-alignment padding as a token. The new implementation decodes the advertised scalar sample count, validates the remaining alignment bits, and emits exactly the declared PCM length. Reference comparison uses those declared block lengths. Mono initialization of an unused previous delta remains uncertain; the new implementation requires a new-delta token before reuse rather than depending on undefined state.

Example reference identities:

| File | Output WAVE bytes | SHA-256 |
| --- | ---: | --- |
| A01_FOOTSTEP_SAND01.WAC | 29138 | `cdccd8e08abadbb8bca1ae0bfc8b0beb98800ece4f4f46ccc726e4e791dd2248` |
| A01_FOOTSTEP_SAND14.WAC | 27438 | `960ec77644fabfb15746bb78defbdc95a73d5ffc6386f992abb475d2be0383b3` |
| BEES.WAS | 1329192 | `ec86a5f98d17a8c3d7aad23efe42c23639cf52538777b3ca1086719bae7b04b8` |

An additional original-file case, `A03_FOOTSTEP_RUG11.WAS`, advertises a padded RIFF total of 85892 bytes but only 85760 PCM data bytes. Its stored block decodes to 85848 bytes before original bit-padding overrun. Both new converters preserve the declared PCM, omit the 88 padded output bytes, and normalize RIFF size to 85796 (total WAVE size 85804). The original-reference tool applies this explicit container normalization before comparing sample bytes. This is a measured original header inconsistency, not a new codec guess.

## Commands

```sh
# Standalone Python, no third-party packages required. Output must be new.
python3 tools/wac_decode.py /path/to/sound.wac /path/to/new-sound.wav

# Native host tool. Output directory must be new.
build-host/jinspect decode-audio /path/to/sound.was /path/to/new-directory
# The result is new-directory/decoded.wav.

# Optional original-routine comparison, in a separate research environment.
python3 -m venv private/validation-venv
private/validation-venv/bin/pip install unicorn==2.1.4
private/validation-venv/bin/python tools/validate_audio_reference.py \
  /path/to/Jerusalem.ex_ /path/to/sound.wac /path/to/sound.was
```

## Regression and Android verification

Twenty-nine Python regressions pass, including native cross-checks through CTest. Synthetic tests verify explicit PCM values, signed-delta wrapping and trimming, cross-block state, all 64 stereo cache entries, replacement tie behaviour, cache resets, undefined-state rejection, every fixture truncation and oversized lengths. CTest runs those cases through both Python and the native `jinspect`, comparing emitted WAVE bytes. Existing deterministic malformed-input testing also invokes the new native decoder.

Host AddressSanitizer and UndefinedBehaviorSanitizer validation passes. This workspace runs processes under tracing, which prevents LeakSanitizer from working; local checks use `ASAN_OPTIONS=detect_leaks=0`. That adjustment is local only: the Actions workflow retains its normal sanitizer configuration. A first local run failed specifically at LeakSanitizer initialization, and was rerun with address/undefined checks still enabled.

The Actions emulator test now generates independent synthetic mono WAC and stereo WAS signals and verifies that both reach the native output-buffer-drained state. All APK builds remain exclusive to Actions. These new Android paths await the Actions result and physical-device playback; neither emulator checks nor byte equality establish speaker audibility.

Next research focus: AQC image decompression, followed by an independently validated image exporter before attempting a panorama renderer. The working Android inspector remains SDL3; the requested SDL2 desktop engine will be introduced separately once original images can be rendered.
