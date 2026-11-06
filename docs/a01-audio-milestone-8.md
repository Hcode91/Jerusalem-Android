# Milestone 8: original A01 ambient audio

The user confirmed that A01 traversal works on Android. This milestone adds the original node-to-sound bindings and concurrent looping scene audio. Story callbacks, inventory, puzzles and automatic object animation remain outside the implemented runtime.

## Verified registrations and activation

Evidence comes from the same 811008-byte European executable identified in milestone 7. A01 initializer `0047cea0` registers three sounds used by rotations:

| Sound ID | Registration name | Complete Disc 1 resource |
| --- | --- | --- |
| 11113 | `a01_wind.wav` | `DATA/A01/SOUND/A01_WIND.WAV` |
| 19001 | `track02.wav` | `DATA/A01/SOUND/TRACK02.WAS` |
| 10028 | `bees.was` | `DATA/A01/SOUND/BEES.WAS` |

`0042c8d0` (SouAdd) removes the supplied suffix and tries **WAS, WAC, WAV**, using suffix strings at `004b263c`, `004b2644`, `004b264c`. A `.wav` registration does not prove the stored file is PCM WAV. The native resolver follows that order, searches within the node's own area and rejects duplicate candidates. It does not accidentally select the system area's other TRACK02 file.

`0042c040` (RotAddAmbSou) validates rotation/sound IDs, requires sound category 2 and constructs an ambient sound item through `00409c50`. The three bounded registration ranges are `0047f21f..0047f2c1`, `0047f518..0047f5ba`, `0047fa22..0047faa9` (exclusive ends). Their 17 calls carry source node, sound ID, volume, pan=0, two control values=1, fade-count=10. Unknown settings fail extraction rather than acquiring invented meanings.

| A01 node suffix | Wind volume | Music volume | Bees volume |
| --- | --- | --- | --- |
| N01 | 55 | 80 | 50 |
| N02 | 55 | 80 | 70 |
| N03 | 55 | 80 | 70 |
| N04 | 55 | 80 | 80 |
| N05 | 65 | 80 | 65 |
| N06 | 80 | 80 | absent |

The node activation path `004293e0` calls `00409e30`, which walks rotation ambient items and starts the enabled ones through `00415c20`. That routine sets volume/pan and calls the sound object's Play virtual method with mode 2. Streaming implementation `004846c0` passes the mode into the stream object at `+0x28`. `00486530` starts its DirectSound ring buffer with `DSBPLAY_LOOPING`; refill thread `00486960` distinguishes mode 0 (pad/end) from nonzero mode (rewind source and continue filling). These are actual looping registrations, independent of manual asset playback.

## Volume conversion

`00483c30` clamps sound volume to 0..100; `00483c90` clamps pan to −100..100. `00483cd0` converts the volume and category master to DirectSound attenuation, using the stored float32 approximation to 0.01, multiplier 1, multiplier −10000, and truncation toward zero. Both volume fields initialize to 100 in `00483f90`.

The native gain helper preserves that arithmetic, then converts hundredths of decibels to linear amplitude via `10^(attenuation/2000)`. The native category master is currently 100%; Android's media volume remains available. Original preference/menu category master settings are not imported. The float32 constant and truncation matter: volume 80 with master 100 yields **−2001**, not −2000 hundredths of a decibel. Captured reference values include 55→−4501, 65→−3501, 70→−3001 and 100→−1. Zero maps to the original minimum attenuation, not a fabricated linear-volume curve.

The optional research validator executes only the original gain/pan arithmetic and its float-to-integer helper, with output calls replaced by capture stubs. All **303 volume/master cases** match the new conversion formula. It never calls Windows APIs, stream playback or authentication code. Six captured golden values also check the C++ linear-gain helper.

## Implemented runtime and model

The static extractor now writes **our own** `JERUSALEM_A01_NAV_V2` text model. Existing node/exit rows are unchanged; added rows are `ambient NODE_ID SOUND_ID VOLUME RESOURCE_STEM`. All 17 bindings and three name/ID registrations match isolated original argument captures. V1 models remain readable and traversal still works; they explicitly request a refresh before scene audio can start. No executable is retained or run by the app.

Native model validation limits ambient rows to 192, three voices per node, valid node references/volumes, safe uppercase resource stems and consistent sound IDs/names. Duplicate/conflicting bindings fail. Asset scanning is bounded, excludes symlinks and uses the original area when the AQC lives in a NODE directory. A flat import is searched within its own containing directory.

`LoopPcm` uses the existing bounded PCM/WAC/WAS parsers/decoders, then loops complete sample frames with checked block sizes. SDL3 provides one stream per active sound, mixing their independently decoded formats. A dedicated refill thread keeps small queued blocks supplied while the rendering thread loads another panorama. Shared sound IDs retain their source position across node changes and receive the new volume; absent sounds close. Source memory is capped at 64 MiB per track and 128 MiB across a prepared scene. Preparing replacement tracks can temporarily coexist with the previous scene's buffers. Corrupt/missing sound files report an explicit audio error while leaving traversal available.

Backgrounding pauses devices and refill; returning resumes them. Stop audio closes scene streams. Moving to another node starts that node's bindings again. Browsing an asset or leaving traversal stops scene audio. The inspector's existing one-shot playback remains available separately. `Scene audio active/opened/loop/closed/paused/resumed` logcat diagnostics distinguish stream setup, looping, retention and lifecycle changes.

Original fades, preference masters, positional footsteps, dialogue, dynamic script changes and cross-platform desktop scene-audio playback are not implemented. Volume changes are immediate. The SDL2 viewer continues to render traversal and manual animation without automatic ambience. This is not full original scene/audio fidelity.

## Android check after the Actions build

1. Keep/import the original A01 NODE and SOUND directories, including A01_WIND.WAV, BEES.WAS and TRACK02.WAS. Preserve the original directory tree.
2. **Long-press A01 navigation**, select the original executable again and let the app refresh its derived connections. Previous builds retained a V1 model without audio bindings.
3. Traverse A01 and listen for looping music and ambience. Node 6 has no bees binding. Shared tracks should continue instead of restarting on every move.
4. Check Stop audio, background/return and leaving traversal. Report audio errors from Status if sound files are missing or unsupported.

The complete original files pass native loop-boundary checks: wind **96128 sample frames**, bees **332287**, music **2221492**. These counts refer to decoded sample frames, not compressed bytes or scalar stereo samples. All six host CTest groups pass with the documented sanitizers/local LeakSanitizer restriction, including **119 navigation/scene-audio checks**, prior 233 animation checks, 30 panorama checks and 44 Python regressions. The optional three-original-file invocation adds six checks without shipping fixtures.

Actions' synthetic Android smoke test now checks concurrent voices, repeated refill beyond the initial queue, pause/resume, retained-track volume changes, removal of unused voices and stream closure on leaving traversal. New Android compilation/runtime and real-speaker results are pending. All APK builds remain exclusive to Actions.

## Story and animation findings to pursue next

The original object-presentation APIs register static channels through `0042a270` and animated channels through `0042a390`. A01 object 10005 has presentation 0 bound to channel 1 on all six nodes, with explicit count 15, rate value 12.5 and mode 0x10. `0041c4f0`/`00418f10` activates a presentation and its animation clocks/channels; `0042a9a0` hides a presentation. The rotation update `004032d0` combines active animation state with channel visibility. These are separate from the AQC header's float32 value 12; the manual 12 fps inspector policy must not be promoted to a game clock.

A01 interaction handler `0044a620` contains pickup cases for objects 10001–10004, with variable updates, hidden presentations, disabled hotspots and inventory calls. The dialogue-completion handler `0044c1c0` also changes scene/presentation state and requests Cinema sequences. These establish that important story effects live in executable code, but do not yet establish a complete initial state or event system. Implementing automatic object animation or pickup progression requires tracing those remaining initialization/dispatch paths. No item acquisition, puzzle completion or story callback is fabricated in this milestone.

## Android lifecycle regression and fix

Actions run [42234532804](https://github.com/Hcode91/Jerusalem-Android/actions/runs/42234532804) successfully built/linted/packaged the ARM64 APK, uploaded it, built the emulator APK and exercised concurrent looping scene streams. It failed the explicit background-pause assertion. Device logs show `nativePause()` and the SDL source explains why: Android can suspend its event pump after queueing background events, before `SDL_PollEvent` returns them to the application's loop.

Audio lifecycle handling now runs in an SDL event watch at enqueue time. The watch pauses both audio paths and the ambient refill worker, records background state atomically and resumes on foreground entry. It is removed before its referenced resources are destroyed. One-shot stream operations now share a mutex because event watches may run on another thread; the scene-audio path already had that protection. Renderer and gesture state continue to update on the main loop. The emulator pause/resume assertion is retained unchanged. The workflow summary now distinguishes APK compilation, ARM64 packaging and emulator validation.

The correction still needs a new Actions/emulator result; no local APK build or physical-speaker success is claimed.
