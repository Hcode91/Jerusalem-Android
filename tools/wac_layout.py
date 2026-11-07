#!/usr/bin/env python3
"""Inspect the verified WAC/WAS wrapper and token boundaries; does not decode PCM."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct


class FormatError(ValueError):
    pass


def integer(data, offset, width):
    if offset < 0 or offset + width > len(data):
        raise FormatError(f'truncated integer at offset {offset}')
    return int.from_bytes(data[offset:offset + width], 'little')


class Bits:
    def __init__(self, data):
        self.data = data
        self.position = 0

    def take(self, width):
        if width < 0 or self.position + width > len(self.data) * 8:
            raise FormatError('compressed block ends inside a token')
        result = 0
        for _ in range(width):
            result = (result << 1) | ((self.data[self.position // 8] >> (7 - self.position % 8)) & 1)
            self.position += 1
        return result


def token_layout(payload, channels, decoded_size):
    bits = Bits(payload)
    prefix = bits.take(3) if channels == 1 else None
    counts = Counter()
    first = None
    # No reconstructed samples are produced here. Only token lengths are verified.
    if channels == 2 and decoded_size // 2 > len(payload) * 4:
        raise FormatError('decoded sample count exceeds the possible token count')
    for index in range(decoded_size // 2):
        leading = bits.take(1)
        if channels == 1:
            if leading == 0:
                bits.take(10)
                kind = 'new_delta'
            else:
                kind = 'repeat_delta'
        elif leading == 0:
            bits.take(12)
            kind = 'literal'
        elif bits.take(1) == 0:
            bits.take(6)
            kind = 'cache_reference'
        else:
            if index == 0:
                raise FormatError('sample-repeat token precedes any output sample')
            kind = 'repeat_sample'
        counts[kind] += 1
        if first is None:
            first = kind
    padding = len(payload) * 8 - bits.position
    if not 0 <= padding < 8:
        raise FormatError(f'unexplained bytes after block tokens: {padding} bits remain')
    return dict(prefix_bits=prefix, first_token=first, token_counts=dict(counts),
                padding_bits=padding, padding_value=bits.take(padding))


def inspect(data, max_blocks=10000):
    if len(data) > 64 * 1024 * 1024:
        raise FormatError('64 MiB inspection limit exceeded')
    if len(data) < 52:
        raise FormatError('truncated wrapper and WAVE header')
    if data[8:12] != b'RIFF' or data[16:24] != b'WAVEfmt ' or data[44:48] != b'data':
        raise FormatError('unsupported layout: expected a fixed 44-byte PCM WAVE header at offset 8')
    count, outer_size = struct.unpack_from('<II', data)
    if outer_size > 64 * 1024 * 1024:
        raise FormatError('64 MiB decoded output limit exceeded')
    if not 1 <= count <= max_blocks:
        raise FormatError(f'block count outside supported inspection range 1..{max_blocks}')
    fmt_size, tag, channels, rate, byte_rate, align, bits = struct.unpack_from('<IHHIIHH', data, 24)
    if fmt_size != 16 or tag != 1 or channels not in (1, 2) or bits != 16:
        raise FormatError('unsupported WAVE geometry: require mono/stereo 16-bit integer PCM')
    if not 1000 <= rate <= 192000 or align != channels * 2 or byte_rate != rate * align:
        raise FormatError('invalid WAVE rate or alignment')
    riff_total = integer(data, 12, 4) + 8
    pcm_size = integer(data, 48, 4)
    if not pcm_size or pcm_size % align or riff_total < 44 + pcm_size:
        raise FormatError('inconsistent embedded RIFF/data lengths')
    if channels == 1:
        if riff_total != 44 + pcm_size or outer_size != 44 + count * 512 or not 0 <= outer_size - riff_total < 512:
            raise FormatError('unsupported mono padded-length convention')
    elif outer_size != riff_total:
        raise FormatError('unsupported stereo output-length convention')

    position, total_decoded = 52, 0
    blocks = []
    for index in range(count):
        header_size = 2 if channels == 1 else 8
        stored_size = integer(data, position, 2 if channels == 1 else 4)
        decoded_size = 512 if channels == 1 else integer(data, position + 4, 4)
        start = position + header_size
        if not stored_size or start > len(data) or stored_size > len(data) - start:
            raise FormatError(f'truncated or empty block {index}')
        if not decoded_size or decoded_size % align:
            raise FormatError(f'invalid decoded size in block {index}')
        details = token_layout(data[start:start + stored_size], channels, decoded_size)
        blocks.append(dict(index=index, header_offset=position, payload_offset=start,
                           stored_size=stored_size, decoded_size=decoded_size, **details))
        position = start + stored_size
        total_decoded += decoded_size
    if position != len(data):
        raise FormatError('trailing bytes outside the declared block sequence')
    if total_decoded + 44 != outer_size:
        raise FormatError('block output sizes disagree with wrapper output length')
    return dict(format='mono WAC layout' if channels == 1 else 'stereo WAS layout',
                channels=channels, sample_rate=rate, bits_per_sample=bits,
                stored_file_size=len(data), block_count=count,
                wrapper_output_size=outer_size, riff_total_size=riff_total,
                pcm_data_size=pcm_size, decoded_padding=outer_size-44-pcm_size,
                normalized_wave_size=44+pcm_size,
                pcm_generated=False, blocks=blocks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    args = parser.parse_args()
    try:
        if args.file.stat().st_size > 64 * 1024 * 1024:
            raise FormatError('64 MiB inspection limit exceeded')
        print(json.dumps(inspect(args.file.read_bytes()), indent=2))
    except (OSError, FormatError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
