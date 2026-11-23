# Complete A01 progression

The native controller now continues from the opening through the camp and the final A01 dialogue. The verified area boundary is the original handler's `GameCall(2,3)` request into A02. The port reports A01 completion there; it does not claim to implement A02. The implementation and host checks are ready for review. **The new APK, emulator assertions and ARM64 phone playthrough are pending Actions/publication.**

This milestone includes the uncommitted [original main-menu restoration](original-menu-milestone-15.md). Android still uses native inventory, topic, settings and save/load panels. Completing A01 progression does not mean every original UI effect has been recreated.

## Playable progression

- Existing opening, six panoramas, pickups, sling combination, matting/character interactions, timed escape, alternate projectile and failure movies remain supported.
- The camp's character hotspot and a Talk control offer the original Hegira, then Allah topics. Labels come from the imported `KEYWORDS.INI`; completed choices are removed. Each selection follows the original camera/puzzle, speech and movie callbacks.
- The bowl and jug can be collected and combined in either order. Filling removes the empty bowl and adds the full bowl while retaining the jug. The original world-bowl handler's inventory IDs are preserved separately from this combination.
- The seven cards enter inventory after the Allah sequence. The table opens from its original hotspot, with the original back access. Matching cards advance the registered hotspot sequence. Wrong cards play one of the original responses without consuming inventory or advancing the puzzle.
- Completing the cards returns to camp and enables giving the full bowl. The final scene, AN013 and speeches lead to the A02 handoff. The app opens its menu with an A01-complete notice. Tapping a failure screen also opens the menu for New Game or Load Game.
- Puzzle scenes now use the original sound registrations, including simultaneous camp wind, music and fire. Their sound category and scene attenuation values use the existing native looping backend.
- Supported DAN mouth presentations follow the speech playback clock, including restored speech offsets. An unsupported optional DAN logs its reason and leaves audio/subtitles usable.

## Verified original facts

Executable SHA-256: `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`.

| Source | Confirmed behavior |
| --- | --- |
| `0044a620` | Camp object use, card acceptance/rejection, hotspot availability and water gift. |
| `0044b680` | Bowl/jug combination removes 10018, adds 10019 and retains 10020; both ingredient orders work. |
| `0044c8c0` | Topic speech starts and their view/presentation changes. |
| `0044c1c0` | Dialogue-completion edges, card grant, AN010/AN013/AN014 and the final area-2 entry-3 request after speech 11039. |
| `0047cea0` | 25 backgrounds, 33 accesses, 83 visual layers, 32 object declarations, 42 speech bindings and 56 puzzle sound bindings. |
| `0048bb60`, `0048c4a0`, `0048c940` | Keyword sections, removal of the selected topic and insertion of its successors. The optional trailing `1` changes text color; it is not a one-use flag. |
| `00406aa0`, `00406410`, `004072e0` | Three-integer DAN levels map level tag to object/presentation; intervals use integer milliseconds. Silence hides targets; the first matching nonzero level is shown. |

The mandatory card chain is Abraham (10023), Moises (10025), Jesus (10024), Muhammad (10026), Torah (10028), Gospels (10029), Coran (10027). Only the next target is active. Original card reactions choose AN011 or AN011B; wrong matches keep the card and can select speech 11040, 11041 or 11042, followed by AN014. The native random stream is deterministic and saved; a new Android session supplies its seed.

All decompilation, original executables, disc contents and validation captures remain under ignored `private/`. The portable controller, parsers and rendering code are independently authored; no original executable runs in the APK.

## Formats and saves

Stage TSV revision 2 adds bounded `object` declarations and `sound` bindings to the existing background/access/layer/speech records. The executable must be selected through Game data → Set up game once to generate this richer model. Revision-1 models remain inspectable but do not satisfy full-area startup checks.

Native story save version 2 preserves camp entry, completed topics, bowl/jug states, card grant/consumption, inventory order, completion and random state, in addition to the existing view, time, active/queued media and original escape state. Loading checks inventory conservation and valid card-progress prefixes before replacing the session. Full-area fingerprints cover required movies, speech/subtitles, present DAN files, keyword text and ambient audio, with a 512 MiB aggregate bound. Paths/casing are excluded from the content calculation.

Version-1 saves use the previous asset/rule fingerprints for migration. The old camp stop is removed and becomes playable. New saves use version 2. Rule/data mismatch, malformed state and corruption remain errors rather than silently starting a different game. A completed A01 session can also be autosaved.

## Validation and limits

- All ten host CTest groups pass with ASan/UBSan, including complete synthetic routes using sling and bare stone, timed/wrong-target failure routes, wrong cards, both bowl-combination orders, stale media callbacks and saves throughout new sequences. Local leak checking is disabled because this workspace is traced; Actions retains its sanitizer configuration.
- A frozen original-free version-1 camp save from the previous native controller migrates and round-trips through version 2.
- An isolated, bounded original-handler harness checks 183 camp/card/combination/dialogue-start/completion cases. A separate original setup capture matches all 173 visual/object records and all 56 puzzle audio bindings. These are research-only calls with engine APIs stubbed, not execution of the original game.
- Private native validation drives both complete routes while decoding their original backgrounds and layers, and validates all 42 registered speech resources. All 16 complete A01 CI2 files decode to EOF; this is decoder coverage, **not a new byte-for-byte comparison against the DLL for every movie**. All 60 loose A01 DIA files parse.
- Of 34 A01 DAN files, 30 match the supported complete triple dialect. OZDE_DIA029, OZDE_DIA032, OZDE_DIA033 and OZDE_DIA034 have two-token level rows that fail that parser. They are diagnosed and skipped for animation. Their audio, subtitles and gameplay callbacks still run.
- Exact original per-presentation restart/delay effects, idle incidental barks/footsteps, menu sound/fades and the original inventory/topic/save panels are fidelity work still outstanding. This deliverable covers progression through the A01 ending; it is not a claim of pixel-perfect reproduction.
- C++ Android translation-unit and Java source checks pass. The Java source check uses Android API 35 with a minimal SDLActivity declaration; only Actions compiles against the full pinned SDL glue, builds the APK and runs the emulator.
- Actions now packages an original-free camp save reached by the actual host controller. Its generated asset fingerprint is filled by the shared native validator. The emulator loads it and exercises topics, bowl/jug, wrong/correct cards, card-progress save/restart/load and A01 completion. This uses no skip/cheat command in the app and ships no fixture inside the APK. **The corrected full-camp emulator check is pending.**

The first published Actions runs on `0b80977` passed host tests, APK compilation/lint and ARM64 packaging, then reached the camp fixture and failed its first jug tap. Captured evidence showed that ADB taps used renderer-local coordinates without the Android status-bar offset, and could use the inset from before the menu closed. The corrected test maps through the accessible game viewport's screen bounds and output pixel scale, and waits until native and Android control heights match. Regression checks reproduce the captured 24-pixel offset, the stale inset, fullscreen and scaled outputs, and reject movie geometry. All gameplay assertions remain required; the corrected emulator run is pending.


The next runs on `b8ca35f` passed item pickup/combination and both camp conversations. Native logs confirm entry to card-table puzzle 10037 and acceptance of object 10021; the check nevertheless failed because its UI status had changed from the scene description to the interaction acknowledgement. Camp progression checks now require a new final puzzle cue after each action and reject intermediate speech/movie cues and earlier visits. Regression tests cover status replacement and stale/intermediate cues. The corrected complete emulator route remains pending; these observations do not establish that the later card/save/ending steps passed on Android.


Runs on `3f36d80` then passed wrong-card feedback, the first four correct cards and card-progress save/restart/load. They stopped at Torah (10028): its verified handler consumes the card and plays AN011/AN011B without a new puzzle cue, leaving the table active. Requiring another puzzle cue was therefore incorrect. The native adapter now logs a bounded ready-for-input snapshot once after each cue sequence becomes idle, with the scene, puzzle flag and inventory IDs. The smoke test requires a fresh ready snapshot and checks the entire remaining inventory after the wrong card and every correct card. Same-scene movie completion and intermediate media are covered by regression tests. This adds diagnostics only; it does not alter game progression. The complete Android route is still pending Actions.

## Phone test

1. Install the Actions APK over the existing app. Keep imported data and saves.
2. In the menu footer, open Game data → Set up game and choose the original `Jerusalem.ex_`/`.exe`. This refreshes `a01-stage.tsv` to revision 2. Ensure `KEYWORDS.INI`, the complete A01 archive, node, movie, sound and English DIA/DAN directories are imported.
3. Test an existing save. An old camp save should now offer the character conversation rather than a boundary message. A first-escape save should resume normally.
4. For a full run, choose New Game and follow the [opening/escape walkthrough](a01-story-milestone-13.md#phone-test-from-an-upgraded-or-fresh-installation). Continue after its old stopping point.
5. At the camp, collect the water bowl and jug. Inventory → Combine items: combine them. Confirm the full bowl appears and the jug remains.
6. Tap the character or use Talk. Choose Hegira and let the sequence finish. Talk again and choose Allah. Wait for AN010 and the card grant.
7. Tap the card table. Select a card through Inventory → Use item, then tap its matching table image. Start with an intentionally wrong card at Abraham: confirm the response returns you to the table and keeps the card.
8. Solve Abraham → Moises → Jesus → Muhammad → Torah → Gospels → Coran. Save after one or more cards, close/restart, then Load Game → Manual save. Confirm consumed cards remain absent, the bowl/jug remain owned and the next target still works.
9. Use the table's bottom back access if you want to return to camp before finishing. After the final card sequence, select the full bowl and give it at the camp's newly enabled hotspot. Let the final speech/cinematic chain finish.
10. Confirm the A01-complete notice and menu appear. Save the completed session if desired. A02 remains the next implementation area. Also test Menu/Continue and background/resume during a camp speech.
