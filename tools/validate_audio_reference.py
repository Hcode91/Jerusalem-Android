#!/usr/bin/env python3
"""Optional private-data differential check against two isolated original CPU routines.

Requires unicorn==2.1.4. Does not launch the game or implement any Windows APIs.
Only the hash-identified decoder instruction ranges and pointer-return stubs execute.
Never used by the Android app or required for conversion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from wac_layout import inspect
from wac_decode import decode
from reference_cpu import OriginalDecoder


def reference(executable, data):
    info = inspect(data)
    mono = info['channels'] == 1
    decoder = OriginalDecoder(executable, 'mono' if mono else 'stereo',
                              max(b['stored_size'] for b in info['blocks']),
                              max(b['decoded_size'] for b in info['blocks']))
    result, lengths = bytearray(data[8:52]), []
    struct.pack_into('<I', result, 4, 36 + info['pcm_data_size'])
    for block in info['blocks']:
        start = block['payload_offset']
        args = [10, 0, block['stored_size'] * 8] if mono else [12, 6, 0, block['stored_size'] * 8]
        samples, length = decoder.call(data[start:start + block['stored_size']], args, block['decoded_size'])
        lengths.append(length)
        result.extend(samples)
    return bytes(result[:info['normalized_wave_size']]), lengths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('assets', type=Path, nargs='+')
    args = parser.parse_args()
    executable = args.executable.read_bytes()
    for asset in args.assets:
        data = asset.read_bytes()
        expected, lengths = reference(executable, data)
        actual = decode(data)
        if actual != expected:
            first = next(i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1])
            raise ValueError(f'{asset.name}: sample mismatch at WAVE byte {first}')
        print(json.dumps(dict(file=asset.name, match=True, wave_bytes=len(actual),
                              sha256=hashlib.sha256(actual).hexdigest(), original_block_bytes=lengths)))


if __name__ == '__main__':
    main()
