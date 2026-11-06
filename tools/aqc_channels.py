#!/usr/bin/env python3
"""Inspect/export verified AQC channel frames; does not reproduce game activation/timing."""
import argparse
import json
import math
from pathlib import Path
import struct

from aqc_decode import metadata, cache_words, png
from wac_layout import FormatError, integer


def channels(data):
    base = metadata(data)
    position, total_frames, result = base['static_end'], 0, []
    while position < len(data):
        start = position
        count = integer(data, position, 4)
        timing_word = integer(data, position + 4, 4)
        timing = struct.unpack('<f', struct.pack('<I', timing_word))[0]
        last = integer(data, position + 8, 4)
        if (len(result) >= 256 or not 1 <= count <= 1000 or total_frames + count > 10000 or
                not math.isfinite(timing) or timing not in (0., 12.) or last != count - 1 or
                (count == 1) != (timing == 0)):
            raise FormatError(f'unsupported AQC channel header at byte {position}')
        position += 12
        frames = []
        for index in range(count):
            length = integer(data, position, 4)
            if position + 56 > len(data):
                raise FormatError(f'truncated AQC frame header at byte {position}')
            width, height, bpp = struct.unpack_from('<3I', data, position + 4)
            if (width, height, bpp) != (base['width'], base['height'], 2) or data[position+16:position+32] != data[16:32]:
                raise FormatError(f'unsupported AQC frame header at byte {position}')
            left, right, top, bottom, size, stride = struct.unpack_from('<6I', data, position + 32)
            if (not 0 <= left < right <= width or not 0 <= top < bottom <= height or
                    left % 4 or (right-left) % 4 or stride != (right-left)*2 or size != stride*(bottom-top)):
                raise FormatError(f'unsupported AQC patch geometry at byte {position}')
            payload = position + 56
            if not length or length > len(data) - payload:
                raise FormatError(f'truncated AQC frame payload at byte {position}')
            if (right-left)*(bottom-top)//4 > length*4:
                raise FormatError('AQC frame output exceeds possible token count')
            frames.append(dict(index=index, header_offset=position, payload_offset=payload,
                               stored_size=length, left=left, top=top, width=right-left, height=bottom-top))
            position = payload + length
        result.append(dict(index=len(result), header_offset=start, timing_value=timing,
                           declared_last_frame=last, frames=frames))
        total_frames += count
    return result


def frame_rgb(data, frame, palette):
    start = frame['payload_offset']
    indices = cache_words(data[start:start + frame['stored_size']], 13, frame['width']*frame['height']//4)
    rgb = bytearray()
    for index in indices:
        if index >= 8100:
            raise FormatError('AQC patch index exceeds codebook')
        for word in palette[index*4:index*4+4]:
            rgb.extend(((word >> 8) & 248, (word >> 3) & 252, (word << 3) & 248))
    return bytes(rgb)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('list', 'extract'))
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path, nargs='?')
    args = parser.parse_args()
    try:
        if args.input.stat().st_size > 64 * 1024 * 1024:
            raise FormatError('64 MiB input limit exceeded')
        data = args.input.read_bytes()
        directory = channels(data)
        if args.mode == 'list':
            print(json.dumps(directory, indent=2))
            return
        if args.output is None:
            raise FormatError('extract requires a new output directory')
        if sum(f['width']*f['height']*3 for c in directory for f in c['frames']) > 512*1024*1024:
            raise FormatError('512 MiB decoded RGB export limit exceeded')
        base = metadata(data)
        start = base['palette_offset']
        palette = cache_words(data[start:start+base['palette_size']], 16, 32400)
        args.output.mkdir()
        for channel in directory:
            for frame in channel['frames']:
                rgb = frame_rgb(data, frame, palette)
                name = f"channel-{channel['index']:03d}-frame-{frame['index']:04d}.png"
                (args.output / name).write_bytes(png(frame['width'], frame['height'], rgb))
        (args.output / 'manifest.json').write_text(json.dumps(directory, indent=2))
        print(f'Exported {sum(len(c["frames"]) for c in directory)} original patch frames; game timing/activation not reproduced')
    except (OSError, FormatError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
