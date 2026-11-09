# Work toward a complete native port

The current implementation covers the A01 opening, camp puzzles and area ending; Actions/phone validation is pending. Decoding and previewing other areas does not establish their game logic is implemented. Completion means running every area and required puzzle/dialogue path from original user-supplied data, with Android controls and reliable saves.

Work proceeds in reviewable milestones. At the end of each, report the behavior, evidence, passing/failing checks and remaining limits, then ask the user whether to commit/publish or continue. Keep source changes uncommitted until that check-in. All APK builds and emulator runs remain in GitHub Actions; do not wait for build results after publishing, as the user reports them.

| Milestone | Deliverable and acceptance evidence |
| --- | --- |
| 15: original main menu | Restore verified background, ten hit regions and highlight sprites. Preserve current game/saves, provide import/recovery and accessible controls. Host implementation checks pass; Actions and phone checks pending. Original submenus/music remain separate work. |
| 16: full A01 (combined scope) | Implemented camp topics, speech/animation callbacks, water/container/card effects, final A02 handoff and save migration. Host/original-data checks pass; Android validation is pending. See milestone 16. |
| 17: A01 compatibility follow-up | Play the complete route on ARM64 and compare against the original, fix new issues and investigate remaining DAN/presentation/idle-sound differences. Full progression was implemented with milestone 16 at the user's request. |
| 17b: A02 | Implemented from the area-2 handlers; host and private-data checks pass, Android validation pending. See [A02 milestone](a02-milestone-17.md). |
| 18: shared game systems | Extract reusable node/puzzle/dialogue/action records from the verified executable rather than extending a single area controller indefinitely. Include inventory presentation, topic selection and required animation callbacks. Keep effects data-driven where original evidence permits. |
| 19 onward: remaining areas | Implement and verify areas a02–a10 in evidence-backed groups. Each milestone covers complete playable sequences with required inventory, dialogue and transitions; merely opening the area's panoramas/movies is not acceptance. Identify cross-area dependencies before choosing a group. |
| Full front end and supporting content | Recreate original save/load/options panels and menu sound behavior; implement encyclopedia/visit/intro/credits and original inventory presentation as their formats and handlers are verified. Preserve a usable touch/accessibility alternative. |
| Compatibility and release | Complete movie/animation coverage including outstanding variants, compare required paths with the original under Wine, validate saves across supported transitions, test multiple ARM64 devices and lifecycle interruptions, and publish installation/import documentation with explicit remaining issues. |

The sequence can change when investigation reveals a dependency. Unknown behavior remains documented as unknown; a milestone cannot silently replace it with a guessed puzzle or a demonstration. Neither an APK build nor a single successful playthrough alone establishes full-game compatibility. No original assets, executable code, CD authentication bypass or proprietary samples belong in public commits or the APK.
