# A01 opening and first escape

This milestone connects the original A01 opening to the first escape and its following conversation. It does **not** complete area A01. The native controller stops before the next topic-selection puzzle instead of bypassing it or claiming an area ending.

## Implemented

- A separate **Start A01 story** button, with a fresh inventory. Existing asset inspection, navigation previews and preview checkpoints remain available.
- Original opening: AN001 → black puzzle 10045 / ISHT_DIA001 → AN002 → node 1010101. The disc stores this speech as `ISHT_DIA001.WAS` although its executable registration names `.WAC`. Dialogue lookup now accepts a unique WAC, WAS or WAV sibling, case-insensitively; bytes select the audio decoder. Ambiguous or unsafe alternatives are rejected.
- First arrival at node 1010106: AN003 → puzzles 10016, 10018 and 10017 with speech 10000, 10001 and 10002 → node 6 and the original 60-second countdown.
- Puzzle exit 10039: two speakers, the original connection-enable changes, and return to node 5 at yaw 24°.
- The matting offer, selecting world object 10010, character changes 10006 → 10008 ↔ 10009, and using the palm on object 10011 after the encounter. The palm can be collected before or after the offer; it is retained when used, as in the original handler.
- AN004 leads to puzzle 10000. Dismissing its upper character presentation exposes the intended target. Using either the stone or the combined sling on object 10005 plays AN007C, then conversation 11011–11017, including AN009 and the registered background changes. Completing speech 11011 clears the bag through the original `BagRemAll` callback (`0042c4f0`); this is a scripted change, not an accidental scene-load reset.
- Wrong targets 10012/10013 play AN007A/AN007B and show the original failure screen. The countdown failure plays the verified AN006 variant. Without a projectile, the puzzle has the original 8–12-second failure path; this milestone deterministically uses its 8-second lower bound. Original random variation is not reproduced yet.
- Original AT3 BMP-named Cinema backgrounds and sprite frames, automatic AQC character animations, fitted 640×480 puzzle coordinates, and touch markers carrying object IDs.
- Movie completion waits for the display duration, and speech completion waits for the PCM output buffer to drain. A cancelled or failed playback never counts as successful completion. Serial numbers reject stale completion events.
- The foreground story clock pauses during synchronous movies and excludes Android background time. Returning to node 3 in phase 2 applies the original countdown change to 50 seconds. Inventory dialogs do not explicitly pause this clock.

## Evidence and limits

User-provided gameplay reference: [Jerusalem: The Three Roads to the Holy Land — English Longplay — No Commentary](https://www.youtube.com/watch?v=8f98wqVi2gs). The video page was accessible, but playable footage was not exposed by the available browsing tools. No scene observations or timing measurements have been verified from this video yet. Future comparisons should cover cursor/hotspot feedback, camera movement, inventory use, puzzle entry/exit, and movie/dialogue transitions; the video does not establish binary format structures.

The supported executable is still fingerprinted as FNV-64 `64b12e05ac088220`, SHA-256 `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`. No original instructions execute in the app.

`extract_a01_stage` reads constant registration packets into a separate, bounded `JERUSALEM_A01_STAGE_V1` model: **25 backgrounds, 33 pixel/angular accesses, 83 static/animated layer bindings and 42 voice bindings**. Existing navigation TSV/checkpoint fingerprints are unchanged. This does not extract a general scripting language: the first-escape controller is independently authored C++ from the documented handlers.

| Original routine | Evidence used |
| --- | --- |
| `0047cea0` | Puzzle backgrounds, object access order and bounds, image/channel/animation bindings, voice names |
| `0044efd0` case 0 | New game opening and initial presentations |
| `0044b780` | First encounter, puzzle 10039 callback, connection changes and countdown adjustment |
| `0044c1c0` | Speech-finished chains, movie requests and scene changes |
| `0044a620` | Matting, local world selection use, character phases, palm, projectile and wrong-target actions |
| `0042c4f0` | Explicit bag clearing after speech 11011 |
| `004284d0` | Screen fade-out used before AN009; this visual fade is not reproduced yet |
| `0044b9e0` | Countdown and no-projectile failure timers |
| `004256a0` | Object flag 8 selects a world use source without adding it to the bag |
| `0042bf70` / `0042bf90` | Enable / disable inclusive ranges of connection indices, respectively |
| `00429d90` / `00429db0` | Enable / disable inclusive ranges of object access indices |
| `004110f0` | Forward/reverse and ping-pong animation modes; repeated endpoint frames |

Private isolated reference checks match **296 captured setup calls / 141 visual-access records** and **eight selected original handler cases** for the palm exit, both successful projectiles and both wrong targets. All engine calls are capture stubs. This validates these records/requests, not every branch or the entire original execution order.

The dialogue lookup regression covers all three audio extensions, flattened/data/DATA layouts, mixed casing, missing files, duplicate extensions/casing and symlinks. An additional private check resolves and decodes all 16 speeches required by the first-escape startup, including the real opening WAS. Missing-resource messages direct the user to import data; refreshing the executable is only suggested for a missing or stale scene model.

The host route tests also use the real extracted graph/setup, decode the original backgrounds and all active BMP animation frames encountered on both success routes, and apply the registered AQC patches. Phone rendering and interaction still require device testing.

Cinema sprite alpha comes from coverage: index 0 preserves the destination, and a written black pixel is opaque. The existing raw decoder output is unchanged. **TGA-named entries contain meaningful fourth-byte values**; their original alpha composition remains unverified and is explicitly rejected by the new stage-art loader. The next puzzle needs these entries, along with the general conversation/topic UI (`0048a5a0` / `0048a610`). The story stops before that screen. Animation frame selection uses the registered rates and forward/ping-pong modes, but per-object delays, lip-sync and all animation-finished callbacks are incomplete. Node ambience reuses the verified navigation bindings; a full reproduction of puzzle/music mixing is pending.

**Story saves are not implemented.** Save/load buttons refuse to serialize this new story state as an old preview checkpoint. The old preview slot is neither overwritten nor silently treated as a story save. Use A01 navigation to access that slot. Stop audio halts the story and requires restarting; it does not skip a conversation. The original menu, later A01 puzzles and area transition are unfinished.

## Phone test, from an upgraded or fresh installation

1. Install the ARM64 APK from the new GitHub Actions artifact. An upgrade normally retains imports. On a fresh installation, choose **Import data** and import the extracted original game data, including both discs, `A01.AT3` and `SY.AT3`. Do not select a BIN, ISO or split ZIP part as the game data directory.
2. **Long-press A01 navigation** and select the original `Jerusalem.ex_` / `.exe` again. This now writes both the navigation model and `a01-stage.tsv`. A normal navigation preview may start afterward.
3. Tap **Start A01 story**. The opening should play without browsing individual movie or sound files. Speech subtitles should advance, then the app should enter node 1 automatically. If a needed file is missing, the status identifies it.
4. Before first entering node 6, visit node 3 and collect harness 10002, matting 10003 and stone 10004. In **Inventory → Combine items**, combine harness and stone into sling 10017. Keeping the stone uncombined is also a supported success route.
5. At node 5, collect palm 10001. Use the matting on USE 10007. Tap ITEM 10010 to select the original world-use source, then USE 10006. Tap USE 10008 to change the character again. Taking the palm after these interactions should also update the presentation correctly.
6. Travel to node 6. AN003 and three speeches should run automatically. After returning to the panorama, tap its PUZZLE exit. Two speakers should finish, then the app should return to node 5. A countdown is now active.
7. Select the palm through **Inventory → Use item**, then tap USE 10011. AN004 should open the original flat puzzle screen. This is not a free-travel bypass.
8. With **no item selected**, tap the upper USE 10008 marker first. Then select the sling (or stone) in Inventory and tap ACT 10005. AN007C should run, followed by the conversation, AN009 and further speech/background changes. The status should eventually say the first escape is complete and the next topic puzzle is unfinished.
9. Restart with **Start A01 story** for failure checks: wait out the countdown; or, after reaching puzzle 10000, use the projectile on USE 10012 or USE 10013. Those should end the attempt with their respective original movie and failure screen.
10. During a movie or speech, put the app in the background and return. Playback should resume without jumping through callbacks or spending background time from the countdown. **Stop audio** intentionally halts this attempt; restart to continue testing.

Please report the last movie/speech or node/puzzle number if the sequence stops unexpectedly. A screenshot of the status is useful; a new asset upload is not necessary.

## Host verification

```sh
cmake -S . -B build-host -DJERUSALEM_SANITIZE=ON -DJERUSALEM_DESKTOP_VIEWER=ON
cmake --build build-host --parallel 2
ctest --test-dir build-host --output-on-failure
# Optional private original evidence; never commit these inputs/outputs:
build-host/jinspect extract-a01-stage /path/to/Jerusalem.exe /new/a01-stage.tsv
python3 tools/validate_stage_reference.py /path/to/Jerusalem.exe /new/a01-stage.tsv
build-host/stage_tests /path/to/Jerusalem.exe /path/to/game-root
build-host/story_tests --dialogue /new/a01-stage.tsv /path/to/game-root
build-host/jinspect stage-art /path/to/A01.AT3 A01S01N05P02.BMP /new/background.pam
```

The optional reference command requires Unicorn. PAM exports retain RGBA coverage. The Android workflow additionally exercises a fully synthetic opening, six-node traversal, first encounter, puzzle screen and speech-completion return. These generated fixtures test the application plumbing and are never presented as original game playback. APK compilation and emulator checks run only in GitHub Actions.

## Next priorities

1. Verify TGA-named Cinema alpha against the original image/render path and implement the next conversation/topic puzzle.
2. Extend the event/state model through the remaining A01 object and dialogue branches to the actual area transition, with original comparison captures.
3. Add a versioned story checkpoint covering scene/puzzle, presentation/access flags, variables, bag, timers and pending media state; then test a full A01 route on device.
