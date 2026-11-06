# Milestone 5: verified panorama camera and SDL2 viewer

The user confirmed that the prior Android APK displays the original A01 oasis image on their device. This milestone adds a native perspective camera over that decoded image, drag-to-look controls, FOV zoom, a flat/360 toggle, and an SDL2 desktop viewer sharing the C++ camera/decoder core. The Android inspector retains its working SDL3 shell; no Android framework migration is included.

Subsequent progress: the user confirmed the 360 camera on Android. [Milestone 6](aqc-animation-milestone-6.md) adds verified channel-frame decoding and explicit inspection preview for 18 nodes; the special A09 layout remains unresolved.

## Findings checked against original code

| Address | Evidence |
| --- | --- |
| `00402240` | Constructor defaults: yaw 225°, pitch 0°, FOV 85° |
| `004022f0` | Camera basis/ray construction, yaw normalization, FOV clamp 30°..87°, conservative pitch clamp |
| `00402b30` | World-up basis construction |
| `00404a90` | Ray normalization and angular-to-texture conversion |
| `00404f80` | Grid interpolation state initialization |
| `00490b70`, `00490b8d` | Original inverse-sine wrapper/body; x87 square-root/atan operations |
| `00490b40` | Float-to-integer truncation for 16.16 lookup coordinates |
| `00402210` | Original lookup grid: 40×28 cells, logical camera size 640×448 |
| `00401e40` | Window and DirectDraw mode: 640×480, 32-bit |
| `00403520` | Input conversion subtracts 16 from mouse Y |
| `00404010`, `00404070` | π initialization via inverse cosine of -1, then copying the result to `004d0508` |

The map is **spherical/equirectangular**. The routine normalizes a 3D ray, calculates latitude from inverse sine of its Y component, and longitude from inverse sine of X divided by cosine(latitude), with a Z-sign quadrant correction. Longitude is equivalently `atan2(X,Z)`. It does not use a cylindrical projection.

The four AQC floats at file offsets `0x10`, `0x14`, `0x18`, `0x1c` are now traced as horizontal minimum/maximum and vertical minimum/maximum. All 19 available nodes use `(0,360,-75,75)`. The implementation supports a complete horizontal turn, finite ordered vertical bounds within ±90°, and the established source geometry. Unknown horizontal coverage is rejected by the camera; a still-decodable image remains available in the Android flat view.

## Independently implemented camera

Let θ be the FOV on the longer viewport dimension, W/H the viewport size, and M = max(W,H). A local ray has:

```text
x = normalized_screen_x × sin(θ/2) × W/M
y = normalized_screen_y × sin(θ/2) × H/M
z = cos(θ/2)
```

The ray is transformed by the yaw/pitch basis established by the original world-up construction. Positive yaw moves right through texture longitude. Positive pitch moves downward through the stored image. Camera yaw 0 looks at the horizontal midpoint of the source; the texture seam is at yaw 180. These conventions matter when later mapping hotspots.

For a transformed ray `(X,Y,Z)` and source size `(Sw,Sh)`:

```text
latitude  = degrees(asin(Y / length(ray)))
longitude = degrees(atan2(X,Z))
u = (longitude + 180) / 360 × Sw
v = (latitude - midpoint(vertical_bounds)) / vertical_span × Sh + Sh/2
```

Yaw wraps through 360°. FOV clamps to 30°..87°. The original pitch clamp uses `asin(sin(θ/2) × H/M)` as the conservative vertical half-angle and retains a five-degree edge margin; the new camera reproduces that clamp. The 85°/225° constructor defaults are used by the inspector and desktop viewer. **Scene-specific initial orientations and camera overrides are not recovered**, so this is not a claim to start at the same view as the game's scripted opening.

The C++ renderer evaluates the verified mapping per output pixel and samples the decoded image with nearest-neighbour lookup, wrapping the horizontal seam. It does not reproduce the original grid-interpolated rasterizer, fixed-point quantization, animated distortion or every filtering detail. Whole frames are therefore not claimed byte-identical to the original. The original uses a 41×29-vertex lookup grid; this implementation verifies that grid's coordinates separately, then uses direct sampling.

## Reference validation

`tools/validate_projection_reference.py` executes the original mapping routine, grid setup, inverse-sine and integer-conversion helpers in an isolated Unicorn CPU environment. Only hash-identified instruction ranges execute, finite input rays are supplied, known π/data constants are initialized, and an instruction/time budget applies. No Windows APIs or game startup execute. No original code is copied into the camera implementation or distributed.

The new native mapping was compared at **2701 vertices across 25 cases**, including 640×448, landscape 640×480, portrait 480×640, seam crossings, different yaw/pitch/FOV values, clamped inputs, and the original 41×29 grid. Maximum measured difference was **0.0000267471 source pixels**. This establishes the tested mapping's agreement, not full-frame rasterizer equivalence. The private reference output is under `private/aqc-raw/projection-reference.json`.

Thirty native regression checks cover cardinal/centre directions, longer-axis FOV, pitch bounds, yaw/FOV normalization, seam sampling, actual output pixels and invalid geometry/camera/source buffers. All native/desktop/Python CTest groups pass with address and undefined-behaviour sanitizers; the existing local LeakSanitizer restriction is documented in milestone 3. There are 37 Python regressions. The SDL2 viewer also passed a dummy-video launch/render/two-frame shutdown with both a generated node and the complete original A01 node. Physical desktop window interaction was not tested in this headless environment.

## Header correction and channel investigation

`00405280` establishes that the u32 at AQC file offset **`0x34` is row stride**, computed as rectangle width × source bytes per pixel. The earlier unknown-flag label is superseded. It is 4096 for the 2048-pixel base image, and smaller for patches. The Python metadata now reports `row_stride_bytes`; native/Python synthetic fixtures were corrected accordingly. Original decoded pixels are unchanged.

The trailing bytes contain candidate channel records: u32 frame count, an f32 field observed as 0 or 12, a u32 observed as frame-count-minus-one, then per-frame compressed length, 52-byte image header and 13-bit index stream sharing the base codebook. Patch rectangles, byte counts and row strides agree. Candidate framing reaches EOF on 18 of 19 files, including all six A01 nodes. **A09S02N06R01 does not follow that framing at byte 618527**: a `(1,0,0)` record is followed immediately by another plausible channel header. This has not been resolved; no generic channel parser or animation scheduler is claimed. Activation/order/timing must be traced before enabling overlays automatically.

## Usage

On Android, open a complete AQC and close the report. Supported nodes start in 360 view. Drag to look around, use Zoom to change FOV, **Fit image** to restore constructor defaults, and **Flat / 360** to switch to the previous flat pan/zoom inspector. The button row scrolls horizontally. Static-image display has user confirmation; these new camera controls await the next Actions/device result.

The software camera output is capped at a 640-pixel longer dimension and upscaled on Android/desktop. It redraws on camera movement or viewport changes, rather than recalculating unchanged frames. Device performance and visual comparison against the original remain to be measured.

Build the optional SDL2 viewer on a machine with SDL2 development files (for Debian/Ubuntu: `sudo apt-get install libsdl2-dev`):

```sh
cmake -S . -B build-desktop -DJERUSALEM_DESKTOP_VIEWER=ON
cmake --build build-desktop --parallel 2
ctest --test-dir build-desktop --output-on-failure
build-desktop/jerusalem_viewer /path/to/A01S01N01R01.AQC
```

Drag with the left mouse button, scroll to zoom, Home to reset and Escape to exit. No assets are bundled. The initial desktop window preserves the original camera aspect ratio; resizing is supported. Local validation built SDL2 release-2.32.10 at commit `5d249570393f7a37e037abf22cd6012a4cc56a71` under ignored `private/`.

Additional host tools:

```sh
# Write a 640×448 PPM perspective image into a NEW output directory.
build-host/jinspect render-panorama /path/to/node.aqc /path/to/new-view 225 0 85

# Optional private reference check; requires Unicorn 2.1.4 and the verified executable.
private/validation-venv/bin/python tools/validate_projection_reference.py \
  /path/to/Jerusalem.ex_ build-host/jinspect /path/to/node.aqc
```

Actions installs SDL2 development files, builds/tests the desktop viewer, then builds the Android APK as before. Its Android emulator check now requires a rendered panorama frame and a changed camera yaw after a swipe. Neither original game data nor reference executables enter CI. All APK builds remain exclusively in GitHub Actions.
