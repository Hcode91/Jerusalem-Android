# Milestone 18: original area scripts (A03 onward)

A01 and A02 were written by hand from the original handlers. A03 has about 30 KB of
handler code making roughly 1,500 engine calls, and A04–A10 are similar. Translating each
area by hand would take a long time and introduce errors, so from A03 on the port runs the
original area code.

## How it works

- **`X86Machine`** (`src/x86.cpp`) is a small interpreter for the integer x86-32 instructions
  the story code uses. It maps only the PE image of the user's own `Jerusalem.ex_`, a
  private stack and scratch memory. Any other access, unknown instruction or run past the step
  limit stops the story with an error. No original code runs natively.
- **Engine calls are trapped.** `engine_api.hpp` lists the `aApplication` methods that the
  scripts call (object visibility, accesses, puzzles, rotations, variables, bag, timers, speech,
  movies, topic box, `rand`). Each one is implemented natively.
- **Area setup** (`src/script_area.cpp`) runs each area's own setup routine
  (`SetSpaceSoft(N)`: A01 `0047cea0`, A02 `0047c5f0`, A03 `0046c810`, A04 `00464c30`,
  A05 `00460e90`, A06 `0045c030`, A07 `00459e10`, A08 `00455740`, A09 `004513f0`,
  A10 `0044f510`) with the registration functions trapped. This records objects,
  backgrounds, accesses, layers, exits, sounds, ambience, variables and the setup's own
  initial visibility calls. The interpreted A02 model equals the pinned static A02 extractor.
  A03 gives 95 objects, 154 puzzles, 29 rotations, 165 accesses, 699 layers and 345 sounds.
- **ScriptStory** (`src/script_story.cpp`) registers every area at start-up, as the original
  does. It sends each player action through the original dispatchers, which select the area
  handler themselves:

  | Event | Dispatcher | Arguments |
  |---|---|---|
  | Area entry (`GameCall`) | `00430940` | kind, value (A03 = 3, 0) |
  | Use an access | `0042e3c0` | object, access event, scene, in puzzle, x, y |
  | Combine in bag | `0042ee20` | held item, target |
  | Rotation → rotation | `0042f030` | new rotation, previous, 0, event, 0 |
  | Rotation/puzzle moves | `0042f2e0` | new scene, previous, 0, event, kind 1–3 |
  | Timer expired | `0042f530` | timer |
  | Speech finished | `00430450` | sound, 0, 0 |
  | Topic chosen | `00435be0` | 6, 6, topic, 0 |

  Topic lists follow KEYWORDS.INI and the original box (`0048c940`): the chosen topic is
  removed, its `#topic` section adds follow-ups, then the topic's speech plays.
- **Saves** (`JSCRIPT1`) hold the full world state, the changed bytes of the program image,
  queued media and the resume position. They are tied to the program copy's fingerprint.

## Android

*Set up game* now keeps a private copy of the selected program (`original-program.ex_`).
When A02 reaches its hand-off, or when a save from the end of A02 is loaded, the app starts
ScriptStory at `GameCall(3,0)`. It loads `A03.AT3`, the A03 nodes and ambience, and switches
area resources whenever a script changes area. A few A03 layers name images that are not in
A03.AT3; those are skipped and logged.

## Checks

- `script_tests` checks the interpreter against hand-assembled code (calls, traps, jump
  tables, flags, sandbox and step-limit rejection) and the topic book parser. With
  `JERUSALEM_EXE` (and `JERUSALEM_KEYWORDS`) set, it also compares the interpreted A02 setup
  with the static extractor, checks the A03 counts, the opening cue order, topics and a
  save/load round trip.
- `script_probe <exe> fuzz KEYWORDS.INI runs steps` plays random actions. 200 × 3,000 steps
  reached 34 A03 scenes with no interpreter or engine-call failures.
- All host tests pass with ASan/UBSan. **Android validation is pending.**

## Known gaps

- Animation-frame events (`ObjPreSetAniIde*`, handler `004451a0`) are not raised yet. In A03
  they mostly restart idle animations after pauses, plus a few checks.
- Sounds that are not dialogues (footsteps, effects) started by `Speak` are not played, and
  they raise no end event.
- Fades, image transitions, cursors, the encyclopedia and `SouStop` are no-ops.
- The A03 → A04 hand-off runs the original code. A04+ need their own data imports and have
  not been tested.
