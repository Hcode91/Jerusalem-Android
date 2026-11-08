# Original main menu restoration

This milestone replaces the Android button column over the original bitmap with the complete original 640x480 main menu and its selection artwork. It covers the **main menu**, not the original save/load/options submenus or all ten menu systems. Implementation is ready for review; APK/emulator/device verification is pending publication through Actions.

## Evidence

Supported executable SHA-256: `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`.
System archive SHA-256: `383a40c4ec13b1e5552cc83c9cd4b93bf7d875bfff588bd12b20d4c87951b1b3`.

Ghidra references from the highlight filenames converge on system setup `0047ffc0`. Background puzzle 90000 registers `Menu.bmp` at (0,0). The access registrations at `00429ac0` and image registrations at `0042a080` establish these coordinates. The independent C++ menu description records the observed facts; no decompiled source or original assets are included.

| Object | Original label | Hit rectangle (left, top, right, bottom) | Highlight | Image size |
| --- | --- | --- | --- | --- |
| 99001 | New Game | 269,28,433,64 | MEN1_H.BMP | 164x40 |
| 99002 | Resume Game | 269,64,469,100 | MEN2_H.BMP | 200x40 |
| 99004 | Load Game | 269,100,449,136 | MENULOAD.BMP | 180x40 |
| 99003 | Save Game | 269,136,437,172 | MENUSAVE.BMP | 168x40 |
| 99005 | Options | 269,242,405,278 | MEN5_H.BMP | 136x40 |
| 90200 | Visit | 269,278,377,314 | MENUVISIT.BMP | 108x40 |
| 90210 | Encyclopedia | 269,314,449,350 | MENUENC.BMP | 180x40 |
| 99009 | Intro | 269,350,377,386 | MEN_INTRO.BMP | 112x40 |
| 99010 | Credits | 269,386,377,422 | MEN_CRED.BMP | 136x40 |
| 99007 | Quit | 269,422,373,458 | MEN7_H.BMP | 104x40 |

All highlights start at the access rectangle's left/top. Their 40-pixel image heights differ from the 36-pixel access heights; Intro and Credits also have wider images than access rectangles. Rendering and input preserve this distinction rather than resizing the artwork to the button. All eleven complete images decode successfully from the original archive through the existing native BMP/Cinema readers.

Original input handler `00432500` maps these object IDs to new/resume, save/load/options puzzles, intro, credits and quit. Hover handler `00434a60` shows presentation 0 and cycles six system sounds. System setup binds `TRACK02` (sound 95607, category 2, scene volume 80) to the menu and related puzzles. **Menu music, hover sounds and original fade timing are documented evidence, not implemented behavior in this milestone.**

## Behavior

- `OriginalMenuView` draws the decoded background with its aspect ratio intact, letterboxed to the available menu viewport. The original labels remain visible; no button column covers the artwork.
- Ten real Android buttons sit over the verified hit rectangles. Touch, focus and pointer hover use the original highlight images. Transparent Cinema pixels retain their native coverage. Disabled Resume/Save entries are dimmed; enabled state changes only when necessary to preserve accessibility idle.
- Original Resume Game invokes the existing Continue behavior; original Options opens the current native settings panel. Their accessibility names remain Continue and Settings for compatibility.
- New Game asks before restarting an active session. Cancel leaves it running in its paused state. Restart keeps the manual save and starts a new autosave. This confirmation is a native-port addition.
- Visit, Encyclopedia, Intro and Credits explicitly report that they are not implemented. Selecting them does not discard or unpause the current game. Their artwork is visible because it is part of the original menu, not because their systems are complete.
- A footer outside the artwork provides Menu actions, Game data and Developer Tools. Menu actions offers the same entries as a scrollable list with 48 dp minimum button heights. This is useful when the original hit rectangles are small on a phone. Game data holds import/setup. The app retains its landscape orientation.
- Missing/invalid background keeps the native fallback menu usable for import and recovery. A missing or malformed individual highlight leaves the original background usable, records a warning, and uses a selection outline for that entry. The artwork loader refuses wrong dimensions and never indexes unvalidated pixel buffers.
- Existing menu pause, save/load, settings and Developer Tools behavior remains in use. Save/load/options still use native Android panels. The combined milestone-16 implementation continues through the full A01 camp and ending.

## Verification

Local checks: all ten host CTest groups pass with ASan/UBSan (leak detection disabled for the traced workspace); the Android C++ translation unit passes a warnings-as-errors syntax check. Java 17 source checking passes against Android API 35 with a minimal SDLActivity declaration; it does not substitute for compiling against the real SDL glue in Actions. No APK or emulator was built/run locally.

Regression coverage includes complete generated menu archives, missing and wrong-sized highlights, missing/wrong-sized backgrounds, truncated archives, menu hit-box alignment in three aspect ratios, large-action-sheet navigation, rejection of displaced/duplicate accessibility controls, and the existing original-free parser/story suite. Optional private stage validation decodes every original menu image and still passes both first-escape routes (4,725 stage checks).

Actions smoke coverage now imports original-free menu fixtures with genuine dimensions, checks the Android accessibility rectangles against the original registrations, captures `synthetic-original-menu.png`, exercises an unsupported entry and the large action list, cancels a new-game request, and runs the existing story save/load/resume checks. These new emulator assertions are **pending an Actions run**.

```sh
build-host/jinspect menu /path/to/SY.AT3
build-host/stage_tests /path/to/Jerusalem.exe /path/to/game-root
```

## Phone test after the Actions build

1. Install over the existing app; keep the existing data and saves.
2. Confirm the complete lamp/menu picture is visible, with the original New Game through Quit labels. If it falls back to native buttons, import `DATA/SY.AT3` using Game data.
3. Press and hold Options: its original highlight should appear. Release to open settings; Cancel should return to the menu. Also test a mouse or keyboard if available.
4. Use Resume Game to return to the active game or a saved session. Open Menu and check the countdown/audio remain paused.
5. Save, close/restart, then Load Game. Check inventory, view and speech position as in milestone 14.
6. Choose New Game during an active session, then Cancel. Resume should preserve that session. A deliberate Restart should replay the opening.
7. Open Menu actions and check its larger controls. Test Game data and Developer Tools. Select Visit and confirm the explicit limitation message, then return to the same menu.

The combined [full A01 milestone](a01-complete-milestone-16.md) now implements the camp conversation/topic and item interactions, with original behavior comparisons and resumable state. See the [completion roadmap](port-roadmap.md).
