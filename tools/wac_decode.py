#!/usr/bin/env python3
"""Convert the verified mono WAC / stereo WAS dialect to 16-bit PCM WAVE."""
import argparse
from pathlib import Path
import struct

from wac_layout import Bits, FormatError, inspect


def decode(data, max_output=64 * 1024 * 1024):
    info = inspect(data)
    if info['wrapper_output_size'] > max_output:
        raise FormatError('decoded WAVE exceeds output limit')
    output = bytearray(data[8:52])
    struct.pack_into('<I', output, 4, 36 + info['pcm_data_size'])
    predictor, delta = 0, None
    for block in info['blocks']:
        start = block['payload_offset']
        bits = Bits(data[start:start + block['stored_size']])
        if info['channels'] == 1:
            if bits.take(3) != 0:
                raise FormatError('unsupported nonzero mono block prefix')
            for _ in range(256):
                if bits.take(1) == 0:
                    code = bits.take(10)
                    delta = -(code & 511) * 64 if code & 512 else code * 64
                if delta is None:
                    raise FormatError('mono delta reused before initialization')
                predictor = (predictor + delta) & 65535
                output.extend(struct.pack('<H', predictor))
        else:
            cache, used = [0] * 64, [0] * 64
            replacement, selected, previous = 0, 0, None
            for _ in range(block['decoded_size'] // 2):
                if bits.take(1) == 0:
                    sample = bits.take(12) << 4
                    selected = replacement
                    cache[selected] = sample
                else:
                    if bits.take(1) == 0:
                        selected = bits.take(6)
                        sample = cache[selected]
                    else:
                        if previous is None:
                            raise FormatError('sample repeat before initialization')
                        sample = previous
                    used[selected] = bits.position
                    if selected == replacement:
                        replacement = min(range(64), key=used.__getitem__)
                output.extend(struct.pack('<H', sample))
                previous = sample
        if bits.take(len(bits.data) * 8 - bits.position) != 0:
            raise FormatError('unsupported nonzero block alignment bits')
    return bytes(output[:info['normalized_wave_size']])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path, help='new .wav file (never overwritten)')
    args = parser.parse_args()
    try:
        if args.input.stat().st_size > 64 * 1024 * 1024:
            raise FormatError('64 MiB input limit exceeded')
        wave = decode(args.input.read_bytes())
        with args.output.open('xb') as target:
            target.write(wave)
        print(f'Wrote {len(wave)} bytes of PCM WAVE to {args.output}')
    except (OSError, FormatError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
