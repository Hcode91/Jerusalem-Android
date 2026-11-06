# Jerusalem native engine research

An independent C++17 / SDL3 Android engine reimplementation for **Jerusalem: The 3 Roads to the Holy Land**. The current build is an **asset inspector**, not a playable game. No Windows compatibility layer is used. No original binaries, game art, audio, or proprietary samples are distributed.

## Implemented

- Bounded AT3 directory parser, prefix inspection, and extraction of stored payloads from complete archives. Stored objects are not decompressed or renamed as decoded images.
- Original aMes/aObj localization table parsing, preserving order, duplicate IDs, and original bytes.
- DAN level and interval parsing for the observed text dialect, with explicit `END` termination.
- PE32 static inspection: sections, named/ordinal imports, relevant strings, SHA-256 identity. It never executes Windows code.
- Standard uncompressed 24/32-bit BMP decoding and integer PCM WAV inspection/playback. The WAV parser has processed all 30 standard WAV files on Disc 1, including an original menu sound. No original image has yet been decoded.
- Streaming ISO9660 extraction from the supplied MODE1/2352 disc image, with checked extents and dual-endian fields.
- CUE validation for the supplied single MODE1/2352 data-track layout; conventional key/value INI parsing as a separate, explicitly selected dialect.
- Native SDL3 Android entry point, rendering and audio backends, inspector scene, touch image panning, zoom controls, lifecycle audio pause/resume, diagnostics, and Android Storage Access Framework folder import.
- Parser regression tests, truncation checks, deterministic malformed-input tests, and an ARM64 debug APK GitHub Actions workflow.

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
build-host/jinspect extract-at3 /path/to/a01.at3 /path/to/new-output-directory
build-host/jinspect extract-media /path/to/resource /path/to/new-output-directory
python3 tools/pe_probe.py /path/to/Jerusalem.ex_
python3 tools/disc_extract.py list /path/to/disc1.bin
python3 tools/disc_extract.py extract /path/to/disc1.bin DATA/A01.AT3 /path/to/new-a01.at3
```

`extract-at3` writes numerically named `.stored` files and a manifest; archive filenames are never used as output paths. `extract-media` scans for RIFF/WAVE and BMP signatures and writes only fully validated, supported media. A filename extension alone does not identify a codec. Both operations require a new output directory and refuse incomplete payloads.

`jinspect` currently caps an input at 64 MiB; complete AT3 archives matching the observed layout within that limit are supported. This cap is a research limitation, not a statement about the original engine. Large-resource streaming is future work. RIFF trailing data outside the declared container is not part of its extracted media.

Keep legally obtained files outside the repository, or under the ignored `private/` directory. Public tests generate synthetic data from the documented layouts. To additionally check the supplied private AT3/DAN prefixes without copying them into the tree, set `JERUSALEM_PRIVATE_AT3` and `JERUSALEM_PRIVATE_DAN` to their paths when running `resource_tests`. These optional checks apply to the specific documented samples.

## Build status

Host validation passed CMake/CTest, 761 C++ checks with private original-data checks enabled, 4000 deterministic malformed-input rounds, and fourteen Python regression tests. Complete original archives were extracted and 30 original WAVs parsed. [Actions run 42149071429](https://github.com/Hcode91/Jerusalem-Android/actions/runs/42149071429) successfully built the ARM64 APK, passed Android lint and packaging checks, and launched the x86_64 native engine to Ready. Its subsequent UI test failed because Android uppercased a button label. Control matching now uses stable accessibility descriptions and case-insensitive fallback, with regression coverage; that fix awaits the next Actions result. ARM64 hardware launch and original scene playback remain unverified.

## Roadmap

1. Run the workflow in the intended GitHub repository, fix CI failures, and verify launch/import on an ARM64 device.
2. Complete A01 and system AT3 archives have been extracted, and all 30 Disc 1 standard WAVs parse successfully. Verify original sound playback on device and reverse the Cinema image payloads.
3. Reverse the AT3 payload compression and AQC image codec using loader call sites and complete examples; add byte-exact decoder regression checks before rendering scenes.
4. Reverse Cinema/CI2 frame tables and WAC/WAS sound codecs; trace complete samples now extracted privately from Disc 1.
5. Trace scene activation, node transitions, hotspots, object IDs, dialogue triggers, and puzzle logic. Build the corresponding runtime only from established semantics.
6. Design versioned native saves after the game state model is understood. Original `.ars` saves are currently unsupported.

## Licence and data

New project code is MIT licensed; SDL3 is separately licensed under zlib and its licence is packaged in the APK by CI. This project is not affiliated with the original creators. Game files remain user supplied and are not covered by this repository's licence. No CD authentication, DRM bypass, licence falsification, or original executable execution is implemented.
