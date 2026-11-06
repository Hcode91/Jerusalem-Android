# Milestone 6: original AQC patch frames and animation inspection

The user confirmed that the native 360° camera works on their Android device. This milestone implements bounded AQC channel parsing, original patch-frame decoding/export, and an explicit single-channel preview in Android and the SDL2 viewer. Preview is an inspection feature: original game activation, scheduling, interactions and scene transitions remain unimplemented.

## Evidence and supported layout

The original routines traced in the hash-identified executable are:

| Entry | Established role |
| --- | --- |
| `00411a80` | Channel header/frame dispatch; reuses the 13-bit index decoder |
| `004036b0` | Reads count, f32 field and third word, then frame headers/data |
| `00403a90` | Classifies a channel using zero/nonzero f32; initial visibility remains separate |
| `00403940` | Initializes channel state, including a zero visibility field |
| `00405470` | Writes four-pixel codebook vectors into each patch rectangle row |
| `00405540` | Saves the base region used for restoration |
| `00403ea0` | Restores hidden patches and draws visible selected frames |
| `00403090`, `00403fa0`, `00403f50` | Channel visibility and frame selection accessors |
| `004110f0` | `aAnimation` clock/frame control, including forward/reverse/ping-pong cases |
| `004032d0` | Rotation update couples animation state, channel selection and composition |

Starting immediately after the static image's second compressed stream, supported channel records contain:

| Relative offset | Type | Observed field |
| --- | --- | --- |
| `+0` | u32 LE | Frame count |
| `+4` | f32 LE | Value 0 for observed single-frame channels, 12 for observed multi-frame channels |
| `+8` | u32 LE | Equal to frame-count minus one in the supported files |
| `+12` onward | frame sequence | Exactly the stated number of frame records |

Each frame consists of a u32 compressed length, a 52-byte image header with the same layout as the static header, then the compressed bytes. Source width/height, two-byte source format and angular fields match the base node. The rectangle fields specify the patch position and dimensions; byte count is rectangle width × height × 2 and row stride is rectangle width × 2. The supported patch left coordinate and width are multiples of four and the rectangle must fit the panorama.

The payload uses the previously verified 13-bit-literal/6-bit-cache/repeat decoder, with fresh cache state per frame. It produces rectangle width × height / 4 vector indices, each below 8100. Those indices reference the base node's RGB565 codebook. Each selected vector supplies four consecutive pixels. Composition is an opaque row copy at the stored coordinates; the new renderer adds opaque alpha to the original RGB values.

There is no established top-level channel count in the static header. The original stream object receives a count separately. The inspection parser reads complete supported records to EOF. Truncation inside a record fails; a cut exactly between complete records cannot be distinguished from a valid shorter channel list without the external count. It does not assert full original-container completeness.

## Coverage and the unresolved variant

The complete supported corpus is **18 nodes, 79 channels and 639 patch frames**, including every available A01 node and twelve A09 nodes. All supported record chains end exactly at EOF. The independently implemented Python frame indices match isolated original decoder instructions for all 639 frames. Native exported PPM pixels match Python RGB pixels for every frame. Private aggregate reference records are stored in `private/aqc-raw/all-channel-references.jsonl`; no original images or decompiler output are committed.

For `A01S01N01R01.AQC`, there are two records: one single-frame patch and one 15-frame patch. Its aggregate concatenated frame identities are:

- Indices: `24197eb4733ddab3f2803da4f52804a52d93d4e53fe3d1fa35961e474094913e`.
- RGB pixels: `b78865f4a7079cd4eaa44259918a29e3aa883eb5371627ad58256cd2d257c3df`.

`A09S02N06R01.AQC` remains **unsupported for channels**. At byte 618527, the putative frame header instead contains another plausible channel header after a `(1,0,0)` record. This could represent an unused/special channel or a different layout, but neither interpretation is established. The parser rejects it rather than skipping bytes heuristically. Its independently decoded static image and camera remain usable; the Android report and preview button state the channel limitation.

## Timing and activation limits

Channel records alone do not define gameplay visibility or sequencing. Original state has separate enable, visibility and current-frame fields, with scene/game code controlling them. `004110f0` uses an enabled flag at inferred object offset `+0x26`, clock mode at `+0x21`, previous tick at `+0x4f`, interval at `+0x53`, mode at `+0x14` and current frame at `+0x22`. These are partial reverse-engineering annotations, not a complete recovered class. Mode values 4/8 select forward/reverse behaviour; 16/32 share a ping-pong path. Tick modes include stepping and elapsed-time catch-up.

Although the channel f32 value 12 plausibly relates to a frame rate, the complete route from that field to game clock configuration and scene activation is not proved. **Preview uses an explicitly chosen 12 fps inspector policy. It does not claim to reproduce original timing.** It previews one multi-frame channel at a time; single-frame state patches are not automatically applied. The base panorama is restored before each preview frame, avoiding trails when rectangles differ, and restored when preview stops.

## Android and desktop behaviour

Open an original AQC, close the report and tap **Preview animation**. Scroll the button row horizontally if necessary. The camera focuses on the selected patch at a 30° FOV so small patches are visible. Drag/zoom still work, and Flat / 360 can inspect the same changing pixels as a flat image. Each additional press selects the next multi-frame channel; after the final channel, preview switches off and restores the previous camera and static image. No preview begins automatically on scene selection.

Android pauses preview elapsed time while backgrounded. Reopening an asset clears its previous preview. Frame decoding errors are explicit diagnostics; unknown layouts are not substituted with generated imagery. Only the current patch is decoded, rather than preloading every expanded frame.

In the SDL2 desktop viewer, press **A** to cycle/off. `--preview` selects the first multi-frame channel on launch:

```sh
build-host/jerusalem_viewer /path/to/node.aqc --preview
```

The desktop viewer pauses preview timing while unfocused. Pixel decoding and preview composition are shared C++ code with Android. Game activation/order, original loop modes, randomization, sound/dialogue synchronization and scene rules are not integrated.

## Tools and validation

```sh
python3 tools/aqc_channels.py list /path/to/node.aqc
python3 tools/aqc_channels.py extract /path/to/node.aqc /path/to/new-png-directory
build-host/jinspect aqc-channels /path/to/node.aqc
build-host/jinspect extract-aqc-channels /path/to/node.aqc /path/to/new-ppm-directory

# Optional original-routine comparison and native pixel comparison:
private/validation-venv/bin/python tools/validate_aqc_channels_reference.py \
  /path/to/Jerusalem.ex_ /path/to/node.aqc --native-tool build-host/jinspect
```

Exports retain rectangle coordinates and the original f32 field in JSON/TSV manifests; numeric output names cannot introduce source-path traversal. Output directories must be new. Limits are 64 MiB per input, 256 channels, 1000 frames per channel, 10000 frames total, bounded image geometry, and 512 MiB aggregate RGB per export. Unknown f32/header variants, malformed rectangles, partial tokens, out-of-codebook indices and unexplained alignment are rejected. Failed exports can leave partial output directories and return a nonzero exit status.

All five host CTest groups pass with address/undefined sanitizers (and the documented local LeakSanitizer restriction). There are **233 animation checks**, the prior **30 camera checks**, **44 Python regressions** and existing deterministic malformed-input rounds. New tests cover exact patch colours/coordinates, variable-rectangle restoration, preview cycle/off state, timing boundaries, selection and geometry errors, count/NaN/stride errors, malformed tokens, truncations inside records, valid prefix boundaries and the unresolved-record shape. The SDL2 smoke test renders and advances a generated two-frame preview.

The Actions Android smoke test generates its own animated AQC, scrolls to Preview animation, requires two distinct native frame indices, and verifies switching preview off. It packages no original data. All APK builds remain exclusive to Actions. The new preview feature awaits Actions and device results; the user has already confirmed the earlier static-image and 360 camera functionality.

Next work is to resolve the special A09 record and extract scene-specific channel activation/frame-clock settings from the executable, then link verified scene transitions and hotspots. Inspection preview is not a substitute for that game logic.
