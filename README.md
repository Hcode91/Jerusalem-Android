# Jerusalem native engine research

An independent C++17 engine reimplementation with an SDL2 desktop panorama viewer and SDL3 Android inspector for **Jerusalem: The 3 Roads to the Holy Land**. The current runtime implements **A01 progression from the opening through the camp puzzles and the area ending**, alongside asset inspection. New Android validation is pending. The complete game remains unfinished. No Windows compatibility layer is used. No original binaries, game art, audio, or proprietary samples are distributed.

## Implemented

- Bounded AT3 directory parser, prefix inspection, and extraction of stored payloads from complete archives. Stored objects are not decompressed or renamed as decoded images.
- Original aMes/aObj localization table parsing, preserving order, duplicate IDs, and original bytes.
- DAN level and interval parsing for the observed text dialect, with explicit `END` termination.
- PE32 static inspection: sections, named/ordinal imports, relevant strings, SHA-256 identity. It never executes Windows code.
- Ghidra loader tracing, WAC/WAS block/token inspection and native/Python PCM conversion for the verified mono/stereo dialect. Outputs match the original decoder routines on fifteen complete assets. See [audio decoding and validation](docs/wac-was-milestone-3.md). Audio playback has been confirmed by the user on Android.
- Native AQC static-image decoding and Python PNG/native PPM export, verified on all 19 Disc 1 nodes. See [AQC findings](docs/aqc-milestone-4.md). A verified spherical 360-degree camera now supports touch look-around, FOV zoom and a flat/360 toggle; see [camera evidence and SDL2 viewer](docs/panorama-milestone-5.md). Original patch-frame decoding and explicit animation inspection now cover 18 nodes; see [channel evidence and preview controls](docs/aqc-animation-milestone-6.md). One A09 channel variant and original activation/timing remain unsupported.
- Bounded extraction of six A01 nodes and 17 navigation/puzzle actions from the supported user-supplied executable, with original angular hit tests and arrival cameras. Android/SDL2 traversal previews preserve disabled routes and stop at unimplemented puzzles. Android also reads original node-specific ambient sound bindings and loops concurrent music/wind/bees streams; see [scene audio](docs/a01-audio-milestone-8.md). See [A01 evidence and usage](docs/a01-navigation-milestone-7.md).
- Original A01 harness, matting and stone pickup effects, static presentation composition and a text inventory in Android/SDL2. See [pickup evidence and limits](docs/a01-pickups-milestone-9.md). Inventory can combine harness + stone into a sling in either order; see [combination evidence and controls](docs/a01-combinations-milestone-11.md). Limited original item use is now supported; full conversation and puzzle chains remain unavailable. Native preview checkpoints preserve node, view, pickups and combinations; see [checkpoint scope and format](docs/native-checkpoints-milestone-10.md).
- Standard uncompressed 24/32-bit BMP decoding and integer PCM WAV inspection/playback. The WAV parser has processed all 30 standard WAV files on Disc 1, including an original menu sound.
- Streaming ISO9660 extraction from the supplied MODE1/2352 disc image, with checked extents and dual-endian fields.
- CUE validation for the supplied single MODE1/2352 data-track layout; conventional key/value INI parsing as a separate, explicitly selected dialect.
- SDL2 desktop panorama viewer sharing the C++ decoder/camera core.
- Native SDL3 Android entry point, rendering and audio backends, inspector scene, touch image panning, zoom controls, lifecycle audio pause/resume, diagnostics, and Android Storage Access Framework folder import.
- Parser regression tests, truncation checks, deterministic malformed-input tests, and an ARM64 debug APK GitHub Actions workflow.

The latest implementation combines the [original main menu](docs/original-menu-milestone-15.md) with [complete A01 progression, save migration and phone tests](docs/a01-complete-milestone-16.md). It adds camp topics, bowl/jug use, seven-card matching, wrong responses, original puzzle ambience, supported speech animation and the final A02 handoff. Existing version-1 camp saves migrate into the playable camp. Full-area models require one refresh through Game data → Set up game. **APK/emulator/ARM64 phone validation of these changes is pending.** Four optional DAN tracks and several presentation details remain fidelity work; A02 gameplay is not implemented. No original assets are bundled.

**A02 now follows A01** when its files are imported: the opening conversation, the Walls/Continue/Guards topics and all four A02 movies run up to the original hand-off to A03. Run Game data → Set up game once to refresh the model; see [A02 milestone](docs/a02-milestone-17.md). Android validation of A02 is pending.

The progressive C++ Cinema decoder still matches all 367 frames of three complete original movies byte-for-byte. SDL2 has a desktop movie player, and host tools export PNG frames and stereo WAV tracks. See [Cinema format evidence](docs/a01-story-cinema-milestone-12.md).

Both complete European disc images are now available privately. The [Disc 2 survey](docs/disc2-corpus-survey.md) adds 119 successfully decoded full-node images and seven validated AT3 directories; new-area gameplay remains unimplemented. Progressive Cinema decoding is now implemented for the verified subset; interlaced variants remain unsupported.

See [reverse-engineering notes](docs/reverse-engineering.md) for byte layouts, code references, sample identities, uncertainties, and next priorities. See [Android usage and validation](docs/android.md) for import and installation.

## Host tools and tests

Only host parsers/tools are built locally. **All APK builds must run in GitHub Actions.** Gradle enforces the Actions environment guard.

```sh
cmake -S . -B build-host -DJERUSALEM_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel 2
ctest --test-dir build-host --output-on-failure
build-host/jinspect at3 /path/to/a01.at3
build-host/jinspect menu /path/to/SY.AT3
build-host/jinspect localized /path/to/aObj.ini
build-host/jinspect describe /path/to/aObj.ini
build-host/jinspect dan /path/to/clip.dan
build-host/jinspect extract-a01-navigation /path/to/Jerusalem.ex_ /path/to/new-output-directory
build-host/jinspect extract-at3 /path/to/a01.at3 /path/to/new-output-directory
build-host/jinspect extract-media /path/to/resource /path/to/new-output-directory
python3 tools/pe_probe.py /path/to/Jerusalem.ex_
python3 tools/aqc_decode.py /path/to/node.aqc /path/to/new-image.png
build-host/jinspect decode-aqc /path/to/node.aqc /path/to/new-output-directory
python3 tools/aqc_channels.py list /path/to/node.aqc
build-host/jinspect extract-aqc-channels /path/to/node.aqc /path/to/new-frame-directory
python3 tools/wac_layout.py /path/to/sound.wac
python3 tools/wac_decode.py /path/to/sound.wac /path/to/new-sound.wav
build-host/jinspect decode-audio /path/to/sound.was /path/to/new-output-directory
python3 tools/disc_extract.py list /path/to/disc1.bin
python3 tools/disc_extract.py extract /path/to/disc1.bin DATA/A01.AT3 /path/to/new-a01.at3
```

`extract-at3` writes numerically named `.stored` files and a manifest; archive filenames are never used as output paths. `extract-media` scans for RIFF/WAVE and BMP signatures and writes only fully validated, supported media. A filename extension alone does not identify a codec. Both operations require a new output directory and refuse incomplete payloads.

`jinspect` currently caps an input at 64 MiB; complete AT3 archives matching the observed layout within that limit are supported. This cap is a research limitation, not a statement about the original engine. Large-resource streaming is future work. RIFF trailing data outside the declared container is not part of its extracted media.

Keep legally obtained files outside the repository, or under the ignored `private/` directory. Public tests generate synthetic data from the documented layouts. To additionally check the supplied private AT3/DAN prefixes without copying them into the tree, set `JERUSALEM_PRIVATE_AT3` and `JERUSALEM_PRIVATE_DAN` to their paths when running `resource_tests`. These optional checks apply to the specific documented samples.

## Build status

All ten current host CTest groups pass with AddressSanitizer and UndefinedBehaviorSanitizer. Original asset decoding, 360 camera, animations, A01 traversal, inventory, save/load and the previous native menu have been confirmed on the user's Android device. The original menu and full A01 implementation have passed host tests and source checks; their APK/emulator/phone validation is pending. APKs and emulator tests run only in Actions. The camp and A01 ending now pass host route tests; later-area gameplay remains unfinished.

## Roadmap

See the [milestone completion roadmap](docs/port-roadmap.md). Next: validate the original menu and full A01 build on Android, resolve remaining fidelity issues, then expand shared systems and complete the remaining areas. At each milestone, report evidence and limits and check with the user before committing/publishing or continuing.

## Licence and data

New project code is MIT licensed; SDL3 is separately licensed under zlib and its licence is packaged in the APK by CI. This project is not affiliated with the original creators. Game files remain user supplied and are not covered by this repository's licence. No CD authentication, DRM bypass or licence falsification is implemented. The engine never executes original binaries. Optional private-data research tools use bounded, isolated CPU routines solely to validate decoding, projection, registrations and volume arithmetic; it is not included in the APK.
