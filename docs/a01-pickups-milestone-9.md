# Milestone 9: three original A01 pickups and native inventory

The user confirmed scene audio, traversal and background/resume after the lifecycle fix. This milestone adds a **limited pickup preview** for three original A01 objects, shared C++ state and image composition, Android touch interaction/text inventory, and SDL2 mouse interaction/console inventory. It does not complete A01 story progression or item-use puzzles.

## Original evidence

The supported executable identity remains the European build documented in milestone 7. All addresses below are its original virtual addresses.

| Address | Established role |
| --- | --- |
| `0044efd0`, case 0 | Fresh-game setup explicitly shows the supported presentations and zeroes their counter |
| `00429700` | Object registration, executable fallback label, optional localization override |
| `00429c20` | Angular object hotspot registration |
| `0042a270` | Object/presentation-to-rotation static channel binding |
| `0042a920`, `0041c4f0`, `00418f10` | Show presentation, enable its image/animation channels |
| `0042a9a0`, `0041c550`, `004190c0` | Hide presentation and disable its channels |
| `00429d70`, `00429dd0` | Disable object's accessibility/hotspots |
| `0042c3a0` | BagAdd: resolves object and adds it to original inventory |
| `0044a620` | A01 object interaction dispatcher |
| `00432440` | Append a unique milestone value to the game's variable-backed history |

The initializer's bounded constant region `0047e493..0047e62b` contains the supported object declarations, angular hotspots and static-channel bindings. It also contains an unrelated loaded-sling declaration, which is not promoted into a supported pickup. Startup calls at `0044f06b..0044f095` show the three supported presentation-0 objects in this order. Calls at `0044f0ff..0044f145` establish fresh variables, including counter 10001 = 0.

| Item ID | Executable fallback label | English AOBJ.INI label | Pickup node | Channel bindings |
| --- | --- | --- | --- | --- |
| 10002 | Fragment of harness | Fragment of harness | 1010103 | N03 channel 3; N04 channel 0 |
| 10003 | mattress | Braided matting | 1010103 | N01/N02/N03/N04 channel 0 |
| 10004 | Stone | Stone | 1010103 | N03 channel 2 |

Channels are zero-based. All three hotspots are initially enabled, use cursor ID 60 and event 0, and use the same angular tenths/hit bounds as earlier navigation. Raw bounds (left, top, right, bottom) are harness `(1072,548,1682,706)`, matting `(1891,-143,2290,285)`, stone `(1733,459,1924,557)`. Object hit testing precedes navigation exit testing in `004256a0`.

The normal-pickup case bodies at `0044a74c`, `0044a7b1`, `0044a816` each perform:

1. Read byte variable 10001, increment its low byte, and write it back.
2. Record unique milestone value 1 through `00432440`.
3. Hide that object's presentation 0.
4. Disable its hotspots.
5. Add the object to inventory.

The surrounding dispatcher first distinguishes ordinary clicks from using an already selected inventory object. This milestone supports the ordinary pickup path only; it never selects inventory objects for use. The history helper deduplicates entries in variable slots beginning at 95000 and updates history indices at 98016/98017. The new runtime stores unique milestone membership in its own representation; it does not claim original save compatibility or implement the history UI.

Shared channel ownership is material: harness and matting both reference N04 channel 0. Original presentation hide writes a channel's visibility flag directly; it does not count remaining presentation owners. The new state does the same. Recomputing visibility by unioning uncollected items would behave differently and is deliberately avoided.

## Extraction and data model

The existing executable fingerprint/PE checks remain required. The native static extractor recognizes a narrowly validated 79-byte instruction template for each supported effect, resolves call targets and checks that the read/write variable IDs and hide/disable/inventory object IDs agree. It reads labels/hotspots/bindings from the actual registrations and verifies initial visibility and zero counter from the startup packets. It does not execute game code or serialize guessed behavior.

The native interchange header becomes `JERUSALEM_A01_NAV_V3`; V1/V2 remain readable. Existing node/exit/ambient rows are unchanged. New rows are:

```text
item ID HOTSPOT_NODE LEFT TOP RIGHT BOTTOM CURSOR EVENT COUNTER MILESTONE "FALLBACK_LABEL"
item-channel ITEM_ID NODE_ID ZERO_BASED_CHANNEL
```

This is our format, not an original resource format. Limits include 16 items, 64 bindings per item, unique item IDs/bindings, declared nodes/in-range channels, ordered angular bounds, printable bounded labels, event 0 and bounded counter/milestone IDs. Channel references shared by different items are allowed. New models describe a fresh supported subset; collected state is not written into this static file.

## Native behavior

`InteractionState` starts supported channels visible, keeps byte variables, ordered inventory and unique milestone membership, and persists channel changes across node transitions. Picked items cannot be collected twice. Hotspots use the original half-open bounds and take priority over exits. Each node starts from its decoded base image and composes visible single-frame channels in channel order using the already verified patch decoder. Unexpected multi-frame collectible bindings fail rather than acquiring an invented clock.

Android draws cyan **ITEM** markers at the real hotspot centres and handles taps alongside the existing EXIT/PUZZLE overlays. The **Inventory** button opens an accessible text list. If original AOBJ.INI is present at the imported root, its English labels replace executable fallback names; otherwise item 10003 is displayed as `mattress`. No original inventory art has been decoded or bundled. Labels are restricted to supported printable ASCII for this UI path; other language rendering remains future work.

A pickup prepares the next state and decodes/composes the current node before committing it. Refreshing the image stops manual animation inspection, retains the camera and keeps shared ambient streams. The current implementation reloads/recomposes the AQC on pickup; resource caching is future work. Item/channel state survives node changes and background/resume, but **leaving navigation, reloading connections or closing the app resets this preview**. There is no save/load or item use/combination yet. The inventory dialog states these limits.

The SDL2 viewer uses the same state/composition. Click cyan markers; press **I** to list inventory in the launching terminal. **A** remains the manual animation inspection control. SDL2 automatic scene audio is still unavailable.

Other initially shown A01 presentations (including palm branch and animations), random timers, intro/movie/dialogue sequences, help/history UI, puzzle reactions and later story states remain unimplemented. This partial initial state must not be described as the complete game opening. Reaching a counter value does not invent subsequent story events, and the disabled node-6 return remains disabled.

## Verification

The optional private reference tool captures original registration arguments and executes only the three bounded normal-pickup bodies with every called API replaced by argument-capture stubs. It compares all three declarations/hotspots, seven channel bindings, startup visibility/counter and **768 effect traces** (three handlers × all 256 byte-counter values) against the extracted model. No Windows API, original inventory UI, game event dispatch or authentication executes.

```sh
build-host/jinspect extract-a01-navigation /path/to/Jerusalem.ex_ /path/to/new-model-directory
python3 tools/validate_pickups_reference.py /path/to/Jerusalem.ex_ /path/to/a01-navigation.tsv
build-host/jerusalem_viewer /path/to/data/a01/node/A01S01N03R01.AQC --navigation /path/to/a01-navigation.tsv
# Optional private composition regression:
build-host/interaction_tests /path/to/Jerusalem.ex_ /path/to/root-containing-all-six-a01-nodes
```

All **seven host CTest groups** pass with address/undefined sanitizers and the documented local LeakSanitizer restriction. There are **171 interaction checks**, prior 119 navigation/audio checks, 233 animation checks, 30 camera checks and **45 Python regressions**. Tests cover malformed/truncated instruction packets, inconsistent IDs/APIs, model validation/quoting, half-open hits, duplicate pickup prevention, pickup order, shared-channel semantics, counter/milestone/inventory updates, exact synthetic pixel restoration and rejected multi-frame bindings. The private six-node composition test passes at startup and after each pickup, adding 29 checks. This does not add full-game original-frame comparisons.

Actions' synthetic Android test retains the scene-audio lifecycle assertions, then tests an item overlapping a puzzle hotspot: the first tap collects it and updates counter/inventory; Inventory shows the item; the next tap reaches the unimplemented puzzle, proving the item is no longer active. Horizontal control lookup now searches both directions so Inventory does not hide earlier controls from the harness. The SDL2 smoke test also launches a V3 model and performs initial item composition. No copyrighted data enters CI.

## Next phone check

After the next Actions build, long-press **A01 navigation** and select the original executable again to obtain V3 connections. Start at **A01S01N03R01**: browse/open that AQC, close its report, then enable A01 navigation. This also works if the previous preview ended at node 6, whose return is disabled. Look around/down for cyan ITEM markers and collect the three supported objects. Check Inventory, revisit another node and return, and confirm the items stay collected during that navigation session. Item use, palm-branch pickup, puzzles and saving are not supported yet. The new Android build and device result remain pending.
