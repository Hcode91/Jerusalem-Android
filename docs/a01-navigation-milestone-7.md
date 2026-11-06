# Milestone 7: original A01 connections and traversal preview

The user has confirmed original AQC animation inspection on Android, following the earlier audio, static-image and 360-camera checks. This milestone adds an explicitly limited **A01 traversal preview**, derived from the user's executable. It does not reproduce the game's story sequence, channel activation, conversations, inventory or puzzles.

## Verified executable evidence

Supported original: 811008-byte x86 PE32, SHA-256 `b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687`. Addresses below are virtual addresses at image base `00400000`.

| Address | Established role |
| --- | --- |
| `0047cea0` | A01 initializer; constant registration prefix ends before `0047d8de` |
| `0042b530` | Registers node ID, basename and externally supplied channel count |
| `0042b860` | Registers an angular rotation-to-rotation exit |
| `0042b9b0` | Associates arrival camera values with source node/exit slot |
| `0042bab0`, `0042bc00` | Registers a puzzle action and its camera |
| `0041b320` | Hotspot bounds/enabled/type/cursor/event constructor |
| `0041b460` | Enabled, half-open rectangle hit test |
| `00405190` | Source-pixel to angular hotspot coordinate conversion |
| `004256a0` | Rotation click dispatch; activates destination and applies arrival camera |
| `0044c1c0` | Story handler including intro and post-intro camera changes; not implemented |

The initializer registers six nodes, IDs 1010101–1010106, with basenames `A01S01N01R01`–`A01S01N06R01` and channel counts **2, 2, 5, 7, 10, 7**. These external counts explain why channel records alone cannot establish container completeness.

The 16 directed rotation links are:

- 1 → 2, 4, 5
- 2 → 1, 3, 4
- 3 → 2, 4
- 4 → 1, 2, 3, 5
- 5 → 1, 4, 6
- 6 → 5 (**initially disabled**)

Node 6 also has an enabled puzzle action to ID **10039**, overlapping the disabled return region. The preview honors the initial flags, ignores disabled routes, and reports the puzzle as unavailable. It never enables the return route or invents puzzle completion. The complete game may later change these states through story/puzzle logic; that lifecycle remains unimplemented.

Exit call arguments are source ID, target ID, empty movie-name pointer, four signed rectangle bounds, enabled flag, cursor ID and event ID. The movie pointer is the zero-initialized empty string at `004d0518`; these links do not request a transition movie. Slots follow registration order within the source node, shared by rotation and puzzle actions.

Camera setters carry source/slot, start yaw/pitch/FOV, control words and end yaw/pitch/FOV. Every captured A01 rotation transition has equal start/end triples. The click dispatcher applies the **end** triple. Other camera transition forms are rejected rather than inferred.

Hotspot objects store signed bounds at offsets `+0,+4,+8,+0xc`, enabled at `+0x10`, type at `+0x11`, cursor at `+0x15`, event at `+0x19`. Type 2 identifies angular bounds. Hit testing is `enabled && left <= x < right && top <= y < bottom`. The converter uses angular tenths of a degree, not screen pixels: for these 2048×856 nodes, `x=trunc(U/2048*3600)` and `y=trunc((V-428)/856*1500)`, after source-pixel quantization. Longitude wraps at the panorama seam. Camera yaw has the previously verified 180° source-longitude offset. Typical exits span 30° horizontally and pitch −15°…+15°.

## Independent extraction and runtime

`src/navigation.cpp` reads the PE section table and a tightly bounded prefix of constant x86 pushes/calls. It **does not execute the executable**, load Windows libraries or inspect authentication routines. Size plus a whole-file FNV-1a64 version fingerprint gates the sole supported build; that fingerprint is a version discriminator, not a cryptographic security check. PE mappings, instruction framing, names, counts, camera packet semantics and the resulting graph are separately validated. Other executable versions fail explicitly.

Android's **A01 navigation** button asks once for the user's `Jerusalem.ex_` or `.exe` through the document picker. A bounded 1 MiB read feeds the native extractor; only the derived model is retained in app-private storage. The Windows program is never saved or executed. A long press selects a replacement executable. Folder import continues to skip executables/DLLs.

The native model `JERUSALEM_A01_NAV_V1` is our own text interchange format, **not an original game format**. It contains `node ID BASENAME CHANNELS` and `exit SOURCE SLOT TARGET PUZZLE ENABLED LEFT TOP RIGHT BOTTOM CURSOR EVENT YAW PITCH FOV` rows. It is capped at 128 KiB, 64 nodes and 512 exits, with bounded IDs, safe unique basenames/slots, valid destinations, finite camera values and ordered angular rectangles. No game artwork, executable bytes or private extracted graph is committed.

The loader resolves all six AQC basenames case-insensitively under imported data, rejects duplicates/missing assets, and bounds the scan. Entering a node decodes it and verifies its channel count before switching. Touch drag looks around; a tap maps through the verified camera to original hotspot coordinates. Green EXIT markers and orange PUZZLE markers are inspector overlays, not recovered original UI art. Multi-touch and drags cancel tap activation.

Opening traversal from an already inspected A01 node retains that camera. Otherwise it starts at the first registered node using constructor camera defaults. This is **not the game's scripted opening**: the original post-intro handler uses other camera/state changes. The preview does not execute events, automatically activate animation, play ambient/dialogue sound or save progression. Animation inspection remains a separate manual control.

## Host commands

```sh
build-host/jinspect extract-a01-navigation /path/to/Jerusalem.ex_ /path/to/new-output-directory
# Place the model above the original extracted data so all six nodes resolve beneath it.
build-host/jerusalem_viewer /path/to/data/a01/node/A01S01N01R01.AQC \
  --navigation /path/to/a01-navigation.tsv

# Optional isolated original instruction comparison; requires Unicorn, not Windows.
python3 tools/validate_navigation_reference.py /path/to/Jerusalem.ex_ /path/to/a01-navigation.tsv
```

The optional reference validator executes only the original registration prefix in an isolated x86 CPU, replacing all eight called APIs with argument-capture return stubs. It compares IDs, names, channels, bounds, flags, cursor/event values and float32 camera values against the native model. **91 captured calls match all six node records, 16 rotation links and one puzzle action**. No Windows APIs, scene logic or CD checks execute. Only the bounded static extractor is part of the engine; Unicorn is an optional research tool.

## Validation and remaining gates

All six host CTest groups pass with address/undefined sanitizers and the documented local LeakSanitizer restriction. **78 navigation checks** cover instruction truncation/sign extension/argument order, malformed packets, version rejection, model round trips/invalid records, half-open and disabled hits, camera/coordinate round trips, seam handling and safe asset lookup. The SDL2 smoke test loads a synthetic graph and renders synthetic AQC data. The original six-node dataset also resolves and launches in the headless SDL2 viewer; this does not prove physical-device traversal.

Actions now exercises a synthetic two-node transition, disabled overlapping return, unavailable puzzle action and exit from traversal mode. It uses no original executable or assets. **The new Android build, emulator test and phone traversal are pending.** No APK was built locally.

Next priorities are original story/intro handlers, channel enable/visibility and timing, the node-6 puzzle, and dialogue/CI2 decoding. Traversal preview establishes genuine graph/hotspot extraction without claiming that A01 is playable from start to finish.

Subsequent device result: the user confirmed A01 traversal. [Milestone 8](a01-audio-milestone-8.md) adds original ambient bindings and a backward-compatible V2 model; its new audio path has separate pending Actions/device checks.
