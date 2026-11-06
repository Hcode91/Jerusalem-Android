# Two-disc intake and Disc 2 resource survey

The user supplied both European disc images through four numbered ZIP parts per disc and separate CUE files. All eight parts were downloaded privately through the connected Drive. The split byte streams form ordinary ZIPs with one BIN member each. Reading every member to EOF passed ZIP CRC checks. No original disc, extracted asset, decompiler output, authenticated download link or private manifest is committed.

## Identity and layout

| Image | Raw bytes | SHA-256 |
| --- | --- | --- |
| Disc 1 | 763565040 | `8f7a3fcbb9604529b3818688ed2feeba0beb4e3a7bfcd3bcb2c85caa9a4d4538` |
| Disc 2 | 817733952 | `6c4054a1ade4e574cebb3bb27b4b2e830157f118d8f12b065f0b4edfdbc5e456` |

The newly supplied Disc 1 matches the existing private image byte for byte by SHA-256. Each CUE declares a single MODE1/2352 track at INDEX 01 00:00:00. The existing ISO9660 reader indexes Disc 2 successfully; its DATA/CD.INI contains `2\r\n`. ZIP checksums establish archive transfer integrity, not optical-sector error correction or authentication. The ISO reader does not implement EDC/ECC correction.

Disc 2 contains **208 original files**: 120 AQC, seven AT3, 80 CI2 and one INI. The user's 218-file phone listing also includes local image/archive copies and helper scripts; those are not additional original ISO resources.

The area split is significant: Disc 2 carries node, animation archive and movie data for A03–A08 and A10. The existing Disc 1 index supplies their loose sound/dialogue data. Consequently, absence of SOUND/DIA directories on Disc 2 does not establish missing game files. An eventual importer must merge complementary disc resources and handle disc-specific metadata deliberately, preserving original area paths. The current app's folder importer has not gained automatic disc-image or split-ZIP import in this pass.

## Native static decoder coverage

The existing C++ decoder, built with address/undefined sanitizers, generated a complete PPM for **all 119 full-node AQCs**. Temporary outputs were hashed and removed; private source files and results remain under the ignored private directory.

| Area | Full nodes decoded |
| --- | --- |
| A03 | 29 |
| A04 | 33 |
| A05 | 26 |
| A06 | 12 |
| A07 | 5 |
| A08 | 10 |
| A10 | 4 |

This demonstrates native decoder coverage on these complete inputs. It does **not** add original-routine pixel comparisons for Disc 2, physical-device checks for every node, scene traversal outside A01 or game logic. The A03 filenames ending R01N decode as full nodes; their gameplay meaning is still unverified.

The existing Python channel parser accepts the record chains in all 119 full nodes, totaling **498 channel records and 5880 frame records**. These are structural/header counts, not a claim that all 5880 frame payloads have been decoded or independently compared. Original external channel counts for these nodes still require executable registration tracing. Zero channel records occur in several complete static views and are not treated as an error.

## Numbered A07 patch resource

`A07S02N02R01_000.AQC` is 8617 bytes and is correctly rejected as a full static node. It contains two consecutive records matching the previously established 56-byte patch framing:

| Record offset | Compressed bytes | Source geometry | Patch bounds | Source bytes / stride |
| --- | --- | --- | --- | --- |
| 0 | 4246 | 2048×856, two bytes/pixel | left 1356, right 1416, top 388, bottom 554 | 19920 / 120 |
| 4302 | 4259 | same | same | same |

The second record ends exactly at byte 8617. No codebook/base image or normal full-node static header occurs at the start. The original AddRot path already references numbered `%s_%03d.aqc` files in its separate-channel loading mode, but this particular file's binding, palette provenance and activation are not established by its filename alone. The companion full node parses as five inline channel records (1, 15, 1, 1, 1 frames). The runtime does not silently splice in the numbered file or pretend it is a panorama. Separate-patch inspection/decoding and its connection to original loading modes are follow-up work.

## Archives and movies

All seven complete AT3 directories pass the existing bounded parser; every declared stored payload lies within the corresponding file:

| Archive | Entries |
| --- | --- |
| A03 | 2145 |
| A04 | 576 |
| A05 | 13 |
| A06 | 369 |
| A07 | 128 |
| A08 | 272 |
| A10 | 209 |
| Total | 3712 |

This validates directory geometry and stored extents, not proprietary payload decompression. All **80 CI2 files** have the eight-byte `CNM UNR\0` signature. Their complete bytes are accessible in the private image; the survey inspected headers, not decoded frames/audio. Cinema decoding remains a separate blocker.

## Private evidence and next priorities

Local ignored evidence includes `private/disc-upload-verification.json`, `private/disc2-files.json`, `private/disc2-survey.json`, `private/disc2-movie-headers.json` and the extracted AQC/AT3 corpus. The survey runs existing host tools and introduces no new engine behavior or APK build. The earlier Android scene-audio feature remains a separate validation checkpoint.

The immediate corpus is sufficient to continue without more user uploads. Priorities are comparing representative new nodes/channels against original routines, understanding the numbered patch, tracing Cinema/CI2 decoding, and extending the independently extracted scene/presentation logic. Neither the broader corpus nor successful static decoding establishes a complete playable game.
