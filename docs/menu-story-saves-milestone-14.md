# Main menu, story saves and alpha sprites

The application now opens a game menu instead of the inspector toolbar. New Game launches the existing native A01 opening/escape route. Continue resumes the active session, or loads the newer manual/autosave slot after a process restart. The complete A01 area is still unfinished.

## Menu and controls

- New Game, Continue, Load Game, Save Game, Settings and Quit are native Android controls with 48 dp minimum heights. The menu scrolls on smaller/landscape displays.
- `MENU.BMP` is decoded from the user's `SY.AT3` at runtime. Its decorative artwork is used behind the menu; the side containing baked original labels is covered with accessible controls. Unimplemented original options (Visit, Encyclopedia, Credits, etc.) are not offered as working buttons. Before import, the menu uses a plain background.
- Import game data opens the existing folder importer. Set up game reads the original executable to prepare the connections/layouts. No original binaries or art are packaged or committed.
- Developer Tools exposes the existing browser, asset playback, animation preview, navigation and old preview checkpoints. Normal gameplay shows Menu and Inventory controls.
- Opening the menu pauses movie/speech playback, scene audio, animation updates and the countdown. Android background time is excluded independently. The renderer's bottom inset now follows the Android control panel's measured height, so fitting and hit testing share the same viewport.
- Settings persist app volume, hotspot markers and subtitles. Game audio still respects the phone's media volume. The developer view retains diagnostic marker IDs.

## Native story saves

Manual slot: app-private `native-saves/a01-story.jrs`.

Autosave slot: app-private `native-saves/a01-auto.jrs`.

The old `a01-preview.jrs` navigation slot remains separate. These are independent native formats, not original Windows saves. A story save includes:

- Location/puzzle and camera; selected item; pickup/combination/consuming-use history; the scripted bag-clear flag.
- Character/story phases, visible presentations, direct channel visibility, enabled object accesses and modified connections.
- Story clock and deadline, including remaining countdown time and failure-timer kind.
- Active and queued movie/speech callbacks, serial numbers, media playback position and visual clock.

Load validates everything before replacing the active state. Inventory variables/collection/use sets are reconstructed from the checked action history; original shared-channel writes are preserved separately. An active movie or speech is requeued with its original serial and resumed at its saved position, rather than marked complete. Subtitle lookup follows the restored audio position.

Movie seeking replays a fresh decoder to the saved frame and trims PCM at whole sample frames. This handles both streamed chunks and movies that preload the soundtrack before their first image. Speech seeking decodes the supported WAV/WAC/WAS source and submits its remaining PCM. Malformed offsets are rejected. A tiny end-of-speech remainder can be replayed when saving immediately before its completion callback.

Autosaves occur when a new session starts, when the menu opens and approximately every 15 seconds while playing. Paused/background activity requests a save where the Android event loop can service it; an abrupt process kill can therefore lose progress since the last completed autosave. Failed/finished-at-the-current-boundary attempts do not overwrite the autosave automatically. Save errors are reported. Manual saves use a synced temporary file followed by atomic rename.

Continue prefers an active healthy session. Otherwise it chooses the newer existing slot. Load Game allows explicitly selecting Manual save or Autosave. An invalid chosen slot reports its error and leaves the current session intact; it does not silently substitute another slot.

### Format version 1

Magic `JRNSTORY`, little-endian fields, version 1, maximum 64 KiB, CRC32 over all preceding bytes. The header contains a fingerprint of normalized navigation/stage rules and a fingerprint of the six nodes, both AT3s, nine required movies and sixteen required audio/subtitle pairs. File paths/casing are excluded from the content fingerprint. Changing those original assets or the rules rejects incompatible saves.

Following the header are camera/media/visual/selection fields, runtime phase/clock flags, bounded action history, presentation/access/channel/connection flags, active cue and pending cue queue. All lengths, flags, IDs, floats and serials are checked. This version covers the implemented first-escape controller; future story semantics must explicitly revise/migrate the format rather than pretend compatibility.

## Verified alpha path and remaining A01 work

The executable's Cinema image path (`00420840`) invokes `mmxImage32PreAlpha` for alpha surfaces. The supplied `mmxImage.dll` has SHA-256 `7defec2ee31f54e814e8277d6ba72ab1c0141bfd7ea47a822b790dc4c397fdd1`:

- `10001270`: premultiply each source RGB component with `floor(component * alpha / 256)`, then invert the alpha byte.
- `10001070` / `10001d50`: add the premultiplied source and `floor(destination * inverted_alpha / 256)`.

The independent scalar implementation matches **2,313 original pixel cases**, including all alpha values, odd/even widths, multiple rows and padded strides, in an isolated bounded harness. No Windows initialization or APIs execute. This preserves the original integer rounding, including its small darkening at the extremes; it is not conventional `/255` blending.

TGA-named Cinema entries now expose their genuine fourth-byte alpha and use this compositor. BMP-named sprites retain their index-0 coverage behavior. The next camp scene (puzzle 10023), including its alpha animation, can now be rendered after the escape dialogue. **Camp conversation/topic, water/container/card interactions and the remaining A01 ending are still unavailable**; the controller stops with an explicit boundary message.

Further inspection confirms `0048a5a0` / `0048a610` are visual BoxShow/BoxHide calls, not sufficient evidence of a general dialogue interpreter. `KEYWORDS.INI` contains the A01 Hegira/Allah topic chain (`#1018` → 11018; `#11018` → 11022). Their UI, availability changes and object/puzzle effects still need implementation from the original handlers. No clickable placeholder topics have been added.

## Verification

Host coverage includes save round trips during original/synthetic cue sequences, inventory and phase restoration, exact remaining deadline, a restored selected-palm interaction, corruption/truncation/rule/data mismatch rejection, legacy preview regressions, and byte-exact PCM tail seeking for streamed/preloaded movies. Optional private routes decode all active original backgrounds/frames including the next alpha scene. All normal CTest fixtures remain original-free.

```sh
cmake --build build-host --parallel 2
ctest --test-dir build-host --output-on-failure
# Optional private evidence (Unicorn needed for the reference harness):
python3 tools/validate_alpha_reference.py /path/to/mmxImage.dll
build-host/stage_tests /path/to/Jerusalem.exe /path/to/game-root
```

The Actions emulator test opens the menu, accesses Developer Tools, runs the synthetic story, saves/restarts/loads, rejects a bad slot without losing the active session, and resumes an opening speech without duplicating its completion callback. All APK builds and emulator tests remain exclusively in GitHub Actions. Host tests and a C++ Android translation-unit syntax check do not establish that this APK has passed device validation.

## Phone test

1. Install the updated ARM64 APK over the current app. Existing imported data and old preview saves should remain. If data/connections are already prepared, no executable refresh is needed for the menu or saves.
2. Confirm the menu appears on launch. Original lamp artwork should appear when `SY.AT3` is imported. Scroll the menu if necessary.
3. Choose New Game. During the opening speech, open Menu; wait a few seconds and use Continue. Playback should resume from that point.
4. Open Menu → Save Game during a speech or movie. Then close/restart the app, choose Load Game → Manual save, and confirm playback resumes and advances once into the following scene.
5. Collect items, combine the sling and change the character state as in the [escape walkthrough](a01-story-milestone-13.md#phone-test-from-an-upgraded-or-fresh-installation). Save, restart and load. Check the bag, removed world items and character presentation.
6. During the timed section, open Menu and wait. Continue should retain the remaining time. Save/load should preserve it too.
7. Test Settings: adjust volume, toggle subtitles/markers, restart and check they persist. The phone's media-volume setting still applies.
8. Finish the supported escape. The next camp image/alpha animation should render; its interactions are not implemented yet.
9. Developer Tools should still offer the old asset browser and navigation checkpoint controls. Use Menu to return to the game front end.
