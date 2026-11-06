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
4. Select **Browse assets**. Directories can be entered and left through the parent entry. Most assets open a selectable inspection report. A supported BMP or AQC static node image is displayed on the native surface after closing its report. Supported AQC nodes open in a spherical 360-degree camera: drag to look around and use Zoom to change the field of view. **Flat / 360** toggles the full flat-image inspector, and **Fit image** resets the current view. Supported PCM WAV, WAC and WAS files close the browser and start playing only after the main window regains focus; they do not open a modal report that would pause SDL. Text localization and DAN files are parsed for inspection. AT3 reports available directory records; prefixes are explicitly identified as incomplete.
5. Use **Play audio** to start/replay the selected supported sound and **Stop audio** to stop it. Swipe the button row horizontally if a control is off-screen. Phone volume keys control the media stream. Native playback errors and buffer status appear in the status bar; **Status** also shows native diagnostics. Android logcat tag is `Jerusalem`. Drag an image to pan, or use **Zoom +**, **Zoom −**, and **Fit image**.

File import is asynchronous. Each file is copied into a temporary file, checked against a known source size, synced, and renamed only when complete. A failed import can leave earlier completed files available, but the partially copied file is removed. Existing imported paths are replaced only after the replacement has been copied successfully. Re-importing does not remove old files absent from the newly chosen folder.

Windows executables/DLLs are skipped. Individual files above 64 MiB, directory nesting above 32 levels, more than 10,000 entries, and cumulative import above 2 GiB are unsupported. Known oversized files are skipped; unknown sizes that cross the limit stop the import with an explicit error. Names are treated as basenames and checked to stay under the private directory. The folder picker supports removable storage and document providers; source access must remain valid during the copy.

No original data is bundled. A01 traversal and manual AQC animation inspection are available in the latest source. Dialogue playback, inventory, puzzle solving and original save/load remain unavailable. A file bearing a `.bmp` or `.wav` name inside an AT3 archive may still have a proprietary payload encoding.

The main native view and selectable Android report now use the same format-aware description. Localization files show English labels (or available languages if there are no English rows), DAN files show interval records, AT3 files list stored resource names, and other single-byte text files show their original contents with an explicit unparsed-format label. The native view shows the beginning of the report; use the scrollable Android report for the full contents. Binary resources without a decoder explain that limitation rather than showing a generic hex header. AQC static images now have a native decoder; supported animation channels have manual preview; Cinema images remain unsupported. `jinspect describe FILE` produces the same report on the host without playing media.

Audio diagnostics distinguish successful queueing, stream errors, and a drained output buffer. A drained buffer proves that SDL's output backend consumed the data; it does not prove audible sound at a physical speaker. The Actions audio smoke test generates its own two-second stereo/22050 Hz/16-bit WAV, checks output-buffer consumption and Replay, and rejects a dummy SDL audio backend. The emulator is headless and does not verify audible output. The verified mono WAC/stereo WAS dialect now has native decoding; Actions also generates synthetic compressed signals to check output-buffer consumption. These new playback paths await Actions/device verification.

## Required device smoke check

This remains pending until an Actions-built APK is available:

- Install the ARM64 APK; confirm the native surface and accessible import controls appear and native status changes to Ready.
- Import the supplied text files and prefixes. Confirm aMes/aObj labels, AT3 partial-directory status, and DAN interval metadata are shown. Rejected files must not crash the app.
- Import a legally obtained, complete uncompressed BMP or AQC node, and supported WAV/WAC/WAS. Confirm the image matches an independent viewer; verify pitch/duration, stop, and background/resume behaviour for sound.
- Rotate the device, open/close the folder picker, background/foreground, deny picker selection, and import an invalid/truncated file.
- Confirm no Windows binary or proprietary test data appears in the APK.

An ARM64 hardware launch has not been observed in this pass. The emulator test and its evidence cover x86_64 startup and synthetic image inspection only. A CI green check must not be reported as gameplay success.

For the current original-image check, browse to `data/a01/node/A01S01N01R01.AQC` (filename case may differ), open it and close its report. The native surface should display the oasis/palm-tree scene at 2048×856 source resolution. In the latest build, drag to look around the panorama; use Flat / 360 to see the full decoded image. The earlier flat-image build was confirmed working by the user. A truncated 4096-byte sample cannot display the image. No game assets are bundled.

The [camera milestone](panorama-milestone-5.md) documents verified projection, source limits and the SDL2 desktop viewer. Camera defaults come from the original constructor; the new A01 traversal mode separately applies verified exit arrival cameras and hotspots. Camera rendering is capped at a 640-pixel longer dimension and updates when dragged/zoomed or resized. Its new Actions smoke check verifies that a swipe changes yaw; physical-device performance is still to be checked.

Original-image and 360-camera operation have now been confirmed by the user. The next build adds **Preview animation**: select a complete A01 AQC, close its report and tap that button (scroll the control row if needed). The camera focuses on a real multi-frame patch. Preview runs at an explicitly chosen 12 fps inspection speed. Further presses cycle through multi-frame channels and then stop, restoring the original image and previous camera. This does not run game activation/timing rules. Unknown channel variants report their limitation while keeping the static panorama usable. See [milestone 6](aqc-animation-milestone-6.md).

## A01 traversal preview (milestone 7)

Animation inspection has now been confirmed on the user's phone. The next APK adds **A01 navigation**:

1. Import the original DATA folder with all six A01 node AQCs, preserving filenames.
2. Tap **A01 navigation** (scroll the button row horizontally). On first use, choose **Choose file**, then select the original `Jerusalem.ex_` or `Jerusalem.exe`. The document picker reads it once; the app saves only extracted connections. Only the verified European executable version is supported.
3. Drag to look around and tap the green **EXIT** markers to change nodes. Orange **PUZZLE** markers report unavailable puzzle interactions. Node 6's return is originally disabled and remains disabled.
4. Tap **A01 navigation** again to leave traversal, or browse another asset. Long-press the button to replace the source executable.

This is a traversal preview, not the original scripted opening or complete A01 gameplay. It does not automatically play ambient audio, dialogue or animations, apply story events, complete puzzles or save progression. All assets stay user-supplied. Missing/duplicate node files and unsupported executables produce explicit diagnostics. See [evidence, limits and tests](a01-navigation-milestone-7.md).

The new Android build/emulator checks and real-phone traversal remain pending. Existing device confirmations establish earlier functionality only.

## Original A01 scene audio (milestone 8)

The user confirmed A01 traversal on Android. The latest source adds automatic music/wind/bees playback using original per-node bindings. After installing the next Actions APK, **long-press A01 navigation and select the original executable again** to refresh the older V1 model. Import A01's original SOUND directory as well as NODE. Music may be stored as TRACK02.WAS although the executable names it track02.wav; the resolver follows the original extension order.

Shared tracks retain their playback position between nodes, volumes change with the original registrations, and node 6 removes the bees track. Stop audio stops scene streams; backgrounding pauses them; leaving traversal or browsing stops them. Missing sound data produces an explicit diagnostic while traversal stays available. Original fades, story/animation activation and preference master volumes remain unimplemented. See [milestone 8](a01-audio-milestone-8.md) for evidence, tests and limits. This new scene-audio build and phone result remain pending.
