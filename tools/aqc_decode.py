#!/usr/bin/env python3
"""Decode the verified AQC static node image; trailing animation channels are not decoded."""
import argparse
import json
import math
from pathlib import Path
import struct
import zlib

from wac_layout import Bits, FormatError, integer


def cache_words(payload, width, count):
    bits = Bits(payload)
    cache, used = [0] * 64, [0] * 64
    replacement, selected = 0, 0
    words = []
    for _ in range(count):
        if bits.take(1) == 0:
            sample = bits.take(width)
            selected = replacement
            cache[selected] = sample
        else:
            if bits.take(1) == 0:
                selected = bits.take(6)
                sample = cache[selected]
            else:
                if not words:
                    raise FormatError('AQC repeat precedes an output word')
                sample = words[-1]
            used[selected] = bits.position
            if selected == replacement:
                replacement = min(range(64), key=used.__getitem__)
        words.append(sample)
    remaining = len(payload) * 8 - bits.position
    if not 0 <= remaining < 8 or bits.take(remaining):
        raise FormatError('unexplained AQC stream suffix or nonzero alignment')
    return words


def metadata(data):
    if not 60 <= len(data) <= 64 * 1024 * 1024:
        raise FormatError('AQC static node input must be 60 bytes..64 MiB')
    width, height, bpp = struct.unpack_from('<III', data, 4)
    if not (0 < width <= 8192 and 0 < height <= 8192 and bpp == 2 and width % 4 == 0 and
            width * height * 4 <= 64 * 1024 * 1024):
        raise FormatError('unsupported AQC static image geometry')
    angles = struct.unpack_from('<4f', data, 16)
    rect = struct.unpack_from('<4I', data, 32)
    if (not all(math.isfinite(n) for n in angles) or rect != (0, width, 0, height) or
            integer(data, 48, 4) != width * height * 2 or integer(data, 52, 4) != width * 2):
        raise FormatError('unsupported AQC static header variant')
    primary_size = integer(data, 0, 4)
    secondary_header = 56 + primary_size
    secondary_size = integer(data, secondary_header, 4)
    end = secondary_header + 4 + secondary_size
    if not primary_size or not secondary_size or end > len(data):
        raise FormatError('truncated or empty AQC compressed stream')
    return dict(width=width, height=height, source_bytes_per_pixel=bpp,
                angles=list(angles), row_stride_bytes=width*2, indices_offset=56,
                indices_size=primary_size, palette_offset=secondary_header + 4,
                palette_size=secondary_size, static_end=end, undecoded_tail_bytes=len(data)-end)


def decode(data):
    info = metadata(data)
    indices = cache_words(data[56:56 + info['indices_size']], 13, info['width'] * info['height'] // 4)
    start = info['palette_offset']
    # The observed dialect stores 8100 vectors, each containing four RGB565 pixels.
    palette = cache_words(data[start:start + info['palette_size']], 16, 8100 * 4)
    rgb = bytearray()
    for index in indices:
        if index >= 8100:
            raise FormatError('AQC vector index exceeds the verified codebook')
        for pixel in palette[index * 4:index * 4 + 4]:
            # Original expansion left-shifts components; it does not replicate low bits.
            rgb.extend(((pixel >> 8) & 248, (pixel >> 3) & 252, (pixel << 3) & 248))
    return info, bytes(rgb)


def png(width, height, rgb):
    if len(rgb) != width * height * 3:
        raise ValueError('RGB buffer size mismatch')
    def chunk(tag, payload):
        return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag + payload))
    rows = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path, help='new PNG path, never overwritten')
    args = parser.parse_args()
    try:
        if args.input.stat().st_size > 64 * 1024 * 1024:
            raise FormatError('64 MiB input limit exceeded')
        info, rgb = decode(args.input.read_bytes())
        with args.output.open('xb') as output:
            output.write(png(info['width'], info['height'], rgb))
        print(json.dumps(info, indent=2))
    except (OSError, FormatError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
