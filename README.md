# Jerusalem-Android

An Android app for playing **Jerusalem: The Three Roads to the Holy Land** (Arxel Tribe / Cryo, 2002) from your own copy of the game.

It is a single-game wrapper around [Winlator](https://github.com/brunodev85/winlator) (Wine + Box64). It opens straight into its own screen: import your disc, press **Play**, and the game starts full screen. No containers or shortcuts to set up by hand.

> No game files are included. You need your own disc, as an `.iso`, a raw `.bin` image, or a folder of extracted files.

## Using it

1. Install the APK. On first launch it installs its Windows environment, which takes a minute or two.
2. Tap **Import disc image** and pick your `.iso` or `.bin`, or tap **Import game folder** and pick the extracted folder. The files are copied into the app (about 700 MB per disc). After that, the original can be deleted.
3. Tap **Play**.

Controls (Winlator touchpad mode):
- Drag one finger to move the cursor.
- Tap to left-click.
- Two-finger tap to right-click.
- Press Back to open the menu, which includes Exit.

**Settings** has:
- the renderer: CNC DDraw (default) or WineD3D
- the game desktop size
- a link to Winlator's full settings (logs, Box64 presets, and so on)
- a button to remove the imported game files

### How the disc works

The game expects to run from its CD. The installer only copies about 8 MB, and the game finds the disc by looking for a CD-ROM drive that has `data/cd.ini`. The app copies each disc to `files/jerusalem/discN`, maps disc 1 to `D:` (and disc 2, if present, to `E:`) and marks those drives as CD-ROM drives in Wine. The game then finds its disc without any patching. `Jerusalem.ex_` on the disc is the plain game program; it's copied as `Jerusalem.exe`, which is all the installer does with it too.

## Building

The repository holds only this project's own code (`overlay/`). Everything upstream is fetched at build time, pinned in [`winlator.lock`](winlator.lock):

| Input | Source |
|---|---|
| Winlator app source | `brunodev85/winlator` @ last source commit. Public source stops at Winlator **7.1**; later releases are binary-only. |
| Wine/Box64 system image (`imagefs.txz`) | extracted from the official `Winlator_7.1.apk` release |
| OpenXR SDK (linked by Winlator's native code) | `KhronosGroup/OpenXR-SDK` `release-1.0.34` |

GitHub Actions builds a debug APK on every push and uploads it as the `Jerusalem-Android` artifact. To build locally, you need JDK 17, the Android SDK with NDK `22.1.7171670` and CMake `3.22.1`, and Gradle 7.6:

```sh
scripts/test.sh          # ISO reader unit tests (plain JVM)
scripts/prepare.sh       # fetch upstream + apply overlay into third_party/winlator
cd third_party/winlator && gradle assembleDebug
```

Notes:
- The app id is `io.github.hcode91.jerusalem`, so it installs alongside a normal Winlator.
- The build is signed with the committed `overlay/app/debug.keystore`, so new builds install over old ones.
- `targetSdkVersion` must stay at 28. Newer targets block running binaries from app storage, which is how Winlator works.

## Layout

```
overlay/                     copied over the Winlator tree before building
  app/build.gradle           app id, signing, NDK/CMake versions
  app/src/main/AndroidManifest.xml   Jerusalem screen is the launcher
  app/src/main/java/com/winlator/jerusalem/
    JerusalemActivity.java   first-run setup, import, settings, launch
    GameImporter.java        copies a disc from an image or folder
    IsoImage.java            ISO 9660 + Joliet reader (2048/2352/2336-byte sectors)
scripts/prepare.sh           fetch upstream inputs and apply the overlay
scripts/test.sh              JVM unit tests
tests/                       test ISO generator + IsoImage tests
```

## Licence

This project's own code is under the [LICENSE](LICENSE) in this repo. Winlator is LGPL-2.1, and its code stays under that licence. Jerusalem is © its respective owners; this project contains none of the game's files.
