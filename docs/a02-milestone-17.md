# A02 progression (milestone 17)

A02 now continues straight from the end of A01. It is a short area with no panoramas: two listening scenes, a conversation with two topics, and four movies. After A01 has been completed, the game plays through A02 to its own boundary, which is the original `GameCall(3,0)` request into A03. A03 is not implemented. **APK, emulator and phone validation of this milestone are pending.**

## Verified original behaviour

Executable SHA-256: `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`.

Each area has its own case in the per-area dispatchers, which select on `00428170` (current space). These are the area-2 cases:

| Source | Behaviour |
| --- | --- |
| `0047c5f0` | A02 setup, `SetSpaceSoft(2)`. It registers 7 puzzle backgrounds, 6 objects, one access (character 20001 in puzzle 22004), 13 animated layers and 1 static layer, 12 speeches (category 5), and two ambient sounds (`a02_crowd`, volume 90, master 100; `track07`, volume 80) for the six puzzles. |
| `0044efd0`, area-2 entry case (`0044f286`) | `SetSpace(2)`, timer stop and `BagRemAll`. Shows objects 20000, 20001 and 20003, plays AN016, then goes to puzzle 21002 and speech 21000. |
| `0044a3f0` | Speech-finished edges: 21000→21001; 21001→puzzle 21000 + 21002; 21002→show 20004/0 + 21003; 21003→hide 20004/0, puzzle 21002 + 21004; 21004→21005; 21005→puzzle 21000 + 21006; 21006→AN017, puzzle 22004; 22000→22001; 22001→puzzle 22004; 22002→22003; 22003→AN018, puzzle 22003 + 22004; 22004→AN018B, `GameCall(3,0)`. |
| `0044a5d0` | Topic chosen: closes the topic window; 22000 goes to puzzle 22001 and 22002 goes to puzzle 22002. The engine then plays the topic speech. |
| `0044a180` | Object use: only character 20001 with no item reacts. It skips playing speech, or else opens the topic window. |
| `0044a1e0` | Animation-finished events for the crowd and listeners restart them after random delays (between 0.3 and 22 s, depending on the object). |
| `0043df80` | Area 2's combine handler is the shared no-op. |
| `00484042` | A sound without `SouSetVol` keeps master volume 100. |

`KEYWORDS.INI` section `#2000` offers *Walls* (22000) and *Continue* (100001). Choosing *Continue* inserts its `#100001` successor, *Guards* (22002). Choosing *Guards* ends the area, so it can be chosen without first asking about *Walls*, as in the original.

## Implementation

- `extract_a02_stage` reads only constant registration packets from `0047c5f0` and fails on any unexpected call or count. `extract_story_stage` appends them to the A01 records as stage text `JERUSALEM_STORY_STAGE_V3`. **Game data → Set up game must be run once** to write the combined model.
- The registered access for 20001 ends at y=3926. It is clipped to the 480-line puzzle screen.
- The story controller continues past speech 11039 only when the model has A02 records **and** A02's files pass `validate_a02_assets`: A02.AT3, the four movies, all 12 speeches, the ambient sounds and the A02 topics in KEYWORDS.INI. Otherwise A01 ends at its previous boundary. This keeps older models and A01-only imports unchanged; CI's synthetic fixture uses this path.
- Movies resolve under their own area folder (`A02/PLA/ENG`), and puzzle ambience resolves inside `A02`. Images are looked up in A01.AT3, then A02.AT3, then SY.AT3.
- Native story saves are now **version 3**. They add the current area and the A02 topic state. Versions 1–2 still load: their rules hash is computed over the A01 subset of a combined model. A02 files are checked for presence, but they are **not** added to the save's asset fingerprint, so existing A01 saves stay valid.
- At the A02 boundary, the app shows an "A02 complete" notice (flag 128). The A01 notice is unchanged when A02 is unavailable.

## Validation

- All nine host CTest groups pass with ASan/UBSan. A new route continues from the complete A01 route through A02. It checks the AN016/AN017 order, the speech chain 21000–21006, empty inventory, topic order (Walls, Continue → Guards), stale or invalid topics, the no-op combine, AN018B and boundary 3. Every intermediate state is saved and reloaded.
- A private check of the real executable and user-supplied A02 files (not part of the repository):
  - extraction gives the expected 7/6/1/14/12/12 records, and the A01 subset of the combined model is byte-identical to the previous A01 model;
  - all 183 A02 background and layer images decode;
  - AN016 (134), AN017 (82), AN018 (60) and AN018B (200 frames) decode to EOF;
  - all 12 speeches and TRACK07.WAS decode, and all DIA files parse.
- Three DAN tracks (OZDE_DIA050, ADRI_DIA051, OZDE_DIA054) animate registered presentations. ADRI_DIA055 and ADRI_DIA057 name presentation 20002/0, which the original creates (`ObjAddPre` at `0047cab9`) without an image in any puzzle, so the original draws no lip movement for them either. The app now treats such a level as a valid no-op: no warning, nothing drawn, and it is never recorded in saves.

## Performance (follow-up)

- Panorama views use `render_panorama_fast`. It evaluates the exact projection every 8 pixels and steps linearly between them in fixed point. It is about 10× faster than the exact renderer on the host (0.6 ms vs 6.3 ms for a 640×300 view), and tests keep it within one source texel of the exact mapping, including across the seam.
- The story picture is recomposed only when an animation frame, visibility or scene actually changes. Before, it was rebuilt every 80 ms, which also capped animations at 12.5 fps.
- The main loop paces itself to ~60 Hz instead of sleeping 16 ms on top of each frame's work.
- Cinema movies remain at their original rate (7.5 fps for the samples checked).

## Known limits

- Idle crowd and listener animations loop continuously. The original restarts each one after a random delay through `0044a1e0`.
- The encyclopedia visit entry recorded by `00432440(7)` after the topics is not modelled, because the encyclopedia is not implemented.
- `0042cdd0(1024)` sound stops before AN018B, and speech skipping by tapping 20001, are not reproduced.
- A03 and later areas are not implemented.
