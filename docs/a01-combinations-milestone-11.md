# Milestone 11: the A01 sling combination

The user confirmed native checkpoint save/load on Android. This milestone adds the first inventory combination: **Fragment of harness (10002) + Stone (10004) → Sling (10017)**. Either ingredient order works. The executable's fallback label is `Loaded sling-like`; the original optional English AOBJ.INI label is `Sling`. This implements an inventory operation, not using the sling on a character or progressing the story.

## Verified evidence

The supported executable remains SHA-256 `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`. Static Ghidra analysis of handler **0044b680** shows symmetric switch arms for objects 10002 and 10004. Their common success path at **0044b6b8..0044b6dc** calls BagRem (0042c460) twice, then BagAdd (0042c3a0). It removes 10002 and 10004 and adds 10017. It does not modify scene presentations, pickup counters, milestones or variables. The result object registration and label are at **0047e5af..0047e5c7**.

The native extractor reads these three API packets and the result registration from the fingerprinted original executable. It checks API identities, argument counts and the result declaration. It never executes or retains the original executable. Ghidra output and original bytes remain private. The independent runtime consumes the extracted data; it does not contain hard-coded Jerusalem item IDs or translated decompiler code.

The optional private-data tool `tools/validate_combinations_reference.py` executes only the bounded original dispatch with capture stubs replacing both inventory APIs. Its **36 pair cases** include both valid orders, matting, the result, zero and an out-of-range ID. Calls match the extracted rule exactly. This tests the original handler's dispatch/effects, not Windows inventory UI behavior. Native ownership checks additionally require both distinct ingredients to be present. Existing pickup evidence still matches all **768 counter cases** and seven bindings.

The same original handler includes a separate bowl/jug operation: it removes only the bowl and adds another object. That operation is deliberately outside this milestone's consuming-two-item rule model. Palm pickup also has a different variable-write effect and conditional character presentations; it must not be forced through the current counter-increment pickup model.

## Runtime and interface

The V4 navigation model extends V3 with:

```text
combine FIRST_PICKUP SECOND_PICKUP RESULT_ID "Result label"
```

V1–V3 remain readable. Validation limits rules to 16, checks printable bounded labels, existing distinct ingredient pickups, unique unordered ingredient pairs and unique non-pickup result IDs. This model supports consuming two original pickups; chaining, reusable ingredients and NPC interactions require further evidence and a richer model.

Android's **Inventory → Combine items** opens two ingredient dialogs. The native main thread receives a bounded queue of ID pairs and rechecks current ownership/rules; invalid, stale or unsupported selections leave state untouched and report a diagnostic. A successful operation removes both ingredients, appends the result and records the action. Previously collected scene objects stay hidden and their hotspots stay disabled. Shared-channel flags, pickup counters and milestone membership remain unchanged. Optional root AOBJ.INI supplies the result's English label too.

SDL2 lists numbered inventory entries with **I**. Press **1..9** for the first ingredient, then **1..9** for the second. Startup tooling also accepts `--combine FIRST SECOND` after `--load-checkpoint`, before `--save-checkpoint`; missing ingredients or unsupported pairs exit nonzero.

## Checkpoint compatibility

The native envelope stays format version 1. Models containing combination rules write **state revision 2**. The ordered u32 payload now stores pickup IDs and `0x80000000 | RESULT_ID` combination actions. The historical C++ field `Checkpoint::inventory` represents this action history; the live inventory is reconstructed by replay. Saving uses `InteractionState::history()`, never the final bag contents. The count is bounded by pickups plus rules, at most 32 actions (**68..196 bytes**); this original three-pickup/one-rule model needs at most 84 bytes.

Replay rejects missing ingredients, unknown results, repeated combinations and reacquiring previously collected items. Fingerprints include combination rules and semantics revision, while excluding translated labels. Revision-1 checkpoints can migrate into V4 only if their full legacy rule hash (current graph with combinations removed) and original node-data hash match. Saving again writes revision 2. New combination checkpoints cannot load against older connections. The existing private milestone-10 checkpoint was read successfully against the new extracted V4 graph.

## Validation and device check

All eight host CTest groups pass with ASan/UBSan, using the documented local LeakSanitizer restriction. There are **208 interaction checks** and **507 checkpoint checks**, covering both orders, missing/self/unsupported ingredients, consumption, retained pickup state, bounded model parsing, malformed action sequences, migration and corruption. Private original-data tests add six-node rendering/state checks. The SDL2 smoke test performs a reverse-order combination, saves the action history and restores it in a separate process. The Android Actions smoke test now exercises ingredient dialogs and combination persistence after force-stop/relaunch using independently generated assets. The new APK/emulator/phone result is pending; APK builds remain exclusively in Actions.

After installing the new APK:

1. **Long-press A01 navigation** and select the original executable again to refresh V4 connections.
2. Load an existing checkpoint or collect the harness and stone at A01S01N03R01. Matting is optional.
3. Open **Inventory → Combine items** and select harness then stone, or stone then harness.
4. Check that only the sling remains from those ingredients, with matting retained if collected. The scene pickups must stay absent.
5. **Save checkpoint**, close/reopen the app, **Load checkpoint**, and check the inventory again.

Next: independently model the palm's different pickup effects and conditional presentations, then trace supported object-use/character transitions. Dialogue/movie dependencies must be implemented before claiming story progression.

## First Actions result and harness correction

Run [42254369140](https://github.com/Hcode91/Jerusalem-Android/actions/runs/42254369140) passed all host tests, ARM64 compilation/packaging and Android lint, and uploaded the APK. The emulator smoke test reached the V4-session setup, then failed because it expected SYNTH_A after toggling navigation off/on. Captured app logs instead show navigation correctly reopening SYNTH_B, the asset restored by the preceding checkpoint. The harness now explicitly waits for the old session to stop, expects the retained SYNTH_B selection and collects the two ingredients without an extra exit tap. Combination/device validation remains pending the corrected Actions run; no checks are skipped or relaxed.
