# Milestone 10: native A01 preview checkpoints

**Device result:** the user confirmed save/load works. The revision-1 format and original milestone validation below are historical; [milestone 11](a01-combinations-milestone-11.md) adds combination history, revision 2 and explicit legacy migration.

The user confirmed collecting all three supported items and viewing them in Inventory on Android. This milestone adds one manual checkpoint slot for the existing A01 preview, shared by the Android app and Linux SDL2 viewer. It saves the current node, 360° camera and collected-item order. It does not implement original Windows `.ARS` saves or serialize unimplemented story/puzzle systems.

## Behavior

Android adds **Save checkpoint** and **Load checkpoint** to the horizontally scrolling control row. Save requires active A01 navigation with V3 pickup connections. It replaces the previous native checkpoint only after the new metadata has been validated, written and synced. Load works from a freshly launched app or an active session: it reads the cached connections and imported nodes, validates compatibility, prepares restored state and enters the saved node. Corrupt, truncated or incompatible checkpoints are rejected before replacing the active session. Scene preparation failures also restore the prior logical scene state; a graphics upload failure may still require reopening the view.

No connection refresh is needed when the current V3 pickup model already works. Labels can come from AOBJ.INI as before. Checkpoints omit display labels, so a label/localization change alone does not invalidate the slot. They are stored at app-private `files/native-saves/a01-preview.jrs`, outside imported `game-data`. Source assets and original saves are never written. Uninstalling or clearing app data removes this slot along with other private data.

The saved pickup sequence is replayed through the independent `InteractionState` implementation. This restores inventory order, the byte counter, unique milestone membership, disabled item hotspots and channel visibility, including shared-channel hide behavior. Parallel copies of those fields are not trusted from a file. Unknown or repeated item IDs fail validation.

This is manual save/load: unsaved changes still reset when leaving navigation or closing the app. Loading restores the last saved point, replacing current unsaved preview state. Music is handled through ordinary node entry; playback positions and Stop audio state are not saved. Manual animation inspection stops on load, and the underlying 360° view is restored rather than flat-image panning/zoom. The camera is constrained for the current viewport; changing device orientation/aspect can clamp pitch. Item use, combinations, palm-branch interaction, story/dialogue/puzzle state and original animation scheduling remain unsupported.

## Native binary format, version 1

This is an independently designed format, not an inferred original game layout. Integers are little-endian; camera values are IEEE754 binary64. No compiler struct layout, pointers, filenames, original artwork or executable bytes are serialized.

| Offset | Type | Meaning |
| --- | --- | --- |
| `0x00` | 8 bytes | `JRNSAVE\0` |
| `0x08` | u32 | Format version, currently 1 |
| `0x0c` | u32 | Preview state semantics revision, currently 1 |
| `0x10` | u64 | Scene-rule compatibility fingerprint |
| `0x18` | u64 | Original node-data compatibility fingerprint |
| `0x20` | u32 | Current node ID |
| `0x24` | u32 | Number of collected items |
| `0x28` | f64 | Camera yaw |
| `0x30` | f64 | Camera pitch |
| `0x38` | f64 | Camera FOV |
| `0x40` | u32 array | Collected IDs in pickup order |
| `0x40 + count*4` | u32 | Standard CRC32 over all preceding bytes |

Length must be exactly `68 + count*4` bytes, bounded to **68..132** bytes. The actual supported three-item graph requires at most 80 bytes. Counts cannot exceed the current graph's item count or 16. Camera values must be finite, yaw in [0,360), pitch within ±90° and FOV 30°..87°. Node and item references must exist. Version/state revisions, exact length, checksum, fingerprints and semantic fields are validated before replay.

The rule fingerprint is FNV-1a64 over the state revision and canonical native scene model, replacing display names with a constant. Thus destinations, enabled routes, cameras, ambient bindings, item effects and channel bindings affect compatibility, while translated labels do not. The data fingerprint hashes each node ID, source byte length and complete AQC bytes in ID order. It excludes paths, so relocating identical resources is allowed. Each input is capped at 64 MiB and total node data at 256 MiB. Missing, changed or incomplete sets fail. Audio files are not part of visual/pickup-state identity.

These fingerprints detect accidental data/rule mismatches; they are not authentication, cryptographic signatures or a licence mechanism. CRC32 detects checkpoint corruption. Later engine changes that introduce more mutable state require a new semantics revision/format or an explicit migration. No migration is guessed.

## Storage and restoration

On Android/Linux, the writer creates a unique same-directory temporary file with mode 0600, handles short writes/EINTR, fsyncs the completed metadata and atomically renames it over the slot. Failures before replacement remove the temporary and leave the previous file intact. Parent-directory sync is attempted; power-loss durability remains filesystem-dependent. The reader rejects oversized files before allocation and rejects short/changed reads. Atomic writes on a native Windows desktop host are currently unavailable; serialization/reading are portable, but a Windows file adapter is future work.

Android navigation preparation is shared between ordinary entry and checkpoint load. Format and compatibility failures leave the existing node/inventory intact. A valid load reconstructs all supported state before applying it; it does not execute Windows code or re-run original initialization/intro handlers. SDL's lifecycle event watch and audio refill thread remain unchanged.

## Desktop and inspection tools

```sh
build-host/jerusalem_viewer /path/to/A01S01N03R01.AQC \
  --navigation /path/to/a01-navigation.tsv --checkpoint /path/to/preview.jrs
# F5 saves, F9 loads, I lists inventory.

# Explicit noninteractive save/load entry for tooling and smoke tests:
build-host/jerusalem_viewer /path/to/A01S01N03R01.AQC \
  --navigation /path/to/a01-navigation.tsv --checkpoint /path/to/preview.jrs \
  --load-checkpoint --frames 2

build-host/jinspect checkpoint /path/to/preview.jrs /path/to/a01-navigation.tsv /path/to/data-root
# Optional private-data regression:
build-host/checkpoint_tests /path/to/Jerusalem.ex_ /path/to/root-containing-the-six-a01-nodes
```

The SDL2 viewer also supports `--save-checkpoint` for an explicit startup save. A checkpoint path alone does not save automatically. Failed interactive loads report an error and retain current state; a failed explicitly requested startup load exits nonzero. The inspection command validates the model/assets and prints node/camera and inventory metadata.

## Validation

All **eight host CTest groups** pass with address/undefined sanitizers and the documented local LeakSanitizer restriction. The new group has **403 checkpoint checks**: all pickup-order/prefix round trips, counters/milestones/channel visibility, every-byte truncation and bit corruption, invalid camera/item/node state, changed rules/assets, label-only compatibility, path relocation, replacement/failed-write cleanup, missing/corrupt/oversized files and input-size limits. Prior 171 interaction, 119 navigation/audio, 233 animation and 30 camera checks remain enabled, alongside **46 Python regressions** and existing malformed-input rounds.

The SDL2 smoke test saves and loads in separate viewer processes. Python independently checks native file offsets, little-endian integers, IEEE754 camera fields and CRC32 against `struct`/`zlib`. The private original-data comparison restores a reversed three-item pickup sequence and verifies identical inventory/counter/visibility and composited pixels across all six original nodes (17 additional assertions). No original data is committed or used in CI.

The Actions Android smoke test now saves a synthetic collected item, force-stops/relaunches the app, loads the checkpoint and verifies saved node/camera/inventory, item-hotspot removal and corrupt-load preservation of active inventory. It keeps the existing audio/lifecycle tests. The user subsequently confirmed save/load on Android; all APK builds remain exclusive to Actions.

## Next phone check

Install the next APK and use the current V3 connections. Collect an item, choose a recognizable view and tap **Save checkpoint**. Change nodes or collect another item, then use **Load checkpoint** to confirm rollback to the saved point. Close/reopen the app and load again; verify node, view, inventory and item removal. Save overwrites the single slot. This validates native preview persistence, not full-game save compatibility.
