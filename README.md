# Jerusalem native engine research

An independent C++17 engine reimplementation with an SDL2 desktop panorama viewer and SDL3 Android inspector for **Jerusalem: The 3 Roads to the Holy Land**. The current build provides **asset inspection and an A01 traversal preview**, not a playable game. No Windows compatibility layer is used. No original binaries, game art, audio, or proprietary samples are distributed.

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

The latest milestone adds a **main menu, Continue, manual story saves/autosaves, settings and a separate Developer Tools view**. It resumes supported movies/speech and preserves the first-escape puzzle state, inventory and countdown. Original menu art is decoded from user-supplied data, and the newly verified alpha compositor renders the next camp scene. **The camp interactions and full A01 ending remain unfinished.** See [menu/save behavior, evidence and phone tests](docs/menu-story-saves-milestone-14.md). Android build/device validation for this milestone remains pending.

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

Current host validation passes all eight CTest groups with AddressSanitizer and UndefinedBehaviorSanitizer, 4000 deterministic malformed-input rounds, 46 Python regressions, 30 panorama checks, 233 animation checks, 119 navigation/scene-audio checks and 171 interaction checks and 403 checkpoint checks. Fifteen original WAC/WAS outputs match original decoder instructions after documented container normalization. A01 extraction matches original node/exit and 17 ambient-binding captures; volume arithmetic matches 303 original routine cases. Audio, original static images, the 360 camera, animation inspection, A01 traversal and automatic scene audio/background-resume and all three pickups/inventory have been confirmed on the user's Android device. The new checkpoint save/load and expanded emulator checks await Actions/device results; APKs are built only in Actions. Story/dialogue playback, inventory use/combinations, puzzles and original Windows saves remain unimplemented.

## Roadmap

1. Run the workflow in the intended GitHub repository, fix CI failures, and verify launch/import on an ARM64 device.
2. Complete A01 and system AT3 archives have been extracted, and all 30 Disc 1 standard WAVs parse successfully. Verify original sound playback on device and reverse the Cinema image payloads.
3. The camera works on device. AQC patch frames now decode with explicit inspection preview; resolve the remaining channel variant and original activation/clock settings before integrating ambient scene animation.
4. Reverse Cinema/CI2 frame tables. Expand the verified WAC/WAS audio corpus and check native compressed-audio playback on device.
5. Trace scene activation, node transitions, hotspots, object IDs, dialogue triggers, and puzzle logic. Build the corresponding runtime only from established semantics.
6. Expand versioned native checkpoints as more game state is verified. Original `.ars` saves remain unsupported.

## Licence and data

New project code is MIT licensed; SDL3 is separately licensed under zlib and its licence is packaged in the APK by CI. This project is not affiliated with the original creators. Game files remain user supplied and are not covered by this repository's licence. No CD authentication, DRM bypass or licence falsification is implemented. The engine never executes original binaries. Optional private-data research tools use bounded, isolated CPU routines solely to validate decoding, projection, registrations and volume arithmetic; it is not included in the APK.
