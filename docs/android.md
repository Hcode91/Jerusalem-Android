# Android app and CI

## Build and install

Push this source tree to the intended GitHub repository on a working branch. The **Native tests and Android ARM64 APK** workflow runs on pushes and pull requests; `workflow_dispatch` also supports manual runs from a branch containing the workflow.

The host job configures CMake, compiles the parsers, and runs CTest with ASan/UBSan. The Android job runs only after tests pass. It installs JDK 17, Android API 35, NDK `27.2.12479018`, CMake `3.22.1`, and Gradle `8.11.1`; Android Gradle Plugin is `8.9.1`. It fetches SDL3 `3.2.28` at immutable commit `7f3ae3d57459e59943a4ecfefc8f6277ec6bf540`. Matching SDL Java glue and C code are built together.

The workflow builds `:app:assembleDebug` and runs `:app:lintDebug`, verifies ARM64 ELF libraries inside the APK, and uploads **jerusalem-native-arm64-debug**. Download and unzip that artifact, transfer `app-debug.apk` to an ARM64 Android 8.0+ device, and install it. It uses the standard Android debug signing key. No release publication or signing credentials are needed.

Build failures are fatal. Test reports and available Android logs/lint reports are uploaded even on failure. An APK is uploaded only after a successful build and packaging check. CI also compiles an x86_64 APK exclusively for an Android API 29 emulator smoke test. That test verifies native startup, JNI inspection, and display of an independently generated synthetic BMP, and uploads screenshot/log evidence. The downloadable APK artifact remains ARM64. Emulator success does not establish ARM64 hardware compatibility or original gameplay.

No APK or Android native builds have been run locally. The workflow target is `Hcode91/Jerusalem-Android`; see the validation status in the README and pull request for observed CI results.

The pinned SDL Java glue receives integration fixes from `patches/sdl-android-lint.patch`. Bluetooth operations now catch permission denial at their method/callback boundaries and return an unavailable result; failed GATT operations are marked failed. The inspector does not request Bluetooth access at startup. These defensive paths are not a claim of tested Steam Controller support.

Two narrow lint annotations document existing SDK guards: the API31 haptic implementation is instantiated only under SDK>=31 (SDLControllerManager lines 52–59), and USB receiver registration already selects explicit flags at SDK>=33 (HIDDeviceManager lines 195–199). No permission warning is suppressed, no checks are disabled for application code, and Android lint still aborts on errors.

## Data import and use

1. Extract your original game data into an Android-accessible folder, preserving its directory tree. This app currently imports extracted resources, not disc images or Windows installers.
2. Launch **Jerusalem Asset Inspector**, select **Import data**, and choose that folder in Android's folder picker.
3. The app copies files into its private `files/game-data` directory. No broad storage permission or network access is requested. The chosen folder is read, not modified. Uninstalling clears the private copy.
4. Select **Browse assets**. Directories can be entered and left through the parent entry. Selecting an asset opens a selectable inspection report. A supported BMP is displayed on the native surface; a supported PCM WAV is played. Text localization and DAN files are parsed for inspection. AT3 reports available directory records; prefixes are explicitly identified as incomplete.
5. Drag an image to pan, use **Zoom +**, **Zoom −**, or **Fit image**, and use **Stop audio** to stop playback. **Status** shows native diagnostics; Android logcat tag is `Jerusalem`.

File import is asynchronous. Each file is copied into a temporary file, checked against a known source size, synced, and renamed only when complete. A failed import can leave earlier completed files available, but the partially copied file is removed. Existing imported paths are replaced only after the replacement has been copied successfully. Re-importing does not remove old files absent from the newly chosen folder.

Windows executables/DLLs are skipped. Individual files above 64 MiB, directory nesting above 32 levels, more than 10,000 entries, and cumulative import above 2 GiB are unsupported. Known oversized files are skipped; unknown sizes that cross the limit stop the import with an explicit error. Names are treated as basenames and checked to stay under the private directory. The folder picker supports removable storage and document providers; source access must remain valid during the copy.

No original data is bundled. No scene, dialogue playback, inventory, puzzle solving, original save/load, or proprietary animation rendering is available yet. A file bearing a `.bmp` or `.wav` name inside an AT3 archive may still have a proprietary payload encoding.

## Required device smoke check

This remains pending until an Actions-built APK is available:

- Install the ARM64 APK; confirm the native surface and accessible import controls appear and native status changes to Ready.
- Import the supplied text files and prefixes. Confirm aMes/aObj labels, AT3 partial-directory status, and DAN interval metadata are shown. Rejected files must not crash the app.
- Import a legally obtained, complete uncompressed BMP and PCM WAV. Confirm the image matches an independent viewer; verify pitch/duration, stop, and background/resume behaviour for sound.
- Rotate the device, open/close the folder picker, background/foreground, deny picker selection, and import an invalid/truncated file.
- Confirm no Windows binary or proprietary test data appears in the APK.

An ARM64 hardware launch has not been observed in this pass. The emulator test and its evidence cover x86_64 startup and synthetic image inspection only. A CI green check must not be reported as gameplay success.
