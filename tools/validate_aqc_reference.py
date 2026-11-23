#!/usr/bin/env python3
"""Compare independently decoded AQC indices/codebook against isolated original routines."""
import argparse
import hashlib
from pathlib import Path
import struct
import json

from aqc_decode import metadata, cache_words, decode
from reference_cpu import OriginalDecoder


def validate(executable, data):
    info = metadata(data)
    streams = [('indices', 13, info['width'] * info['height'] // 4), ('palette', 16, 8100 * 4)]
    report = dict(width=info['width'], height=info['height'])
    for kind, width, count in streams:
        start, size = info[kind + '_offset'], info[kind + '_size']
        payload = data[start:start + size]
        words = cache_words(payload, width, count)
        actual = struct.pack('<' + 'H' * count, *words)
        expected, length = OriginalDecoder(executable, kind, size, count * 2).call(payload, [0, size * 8], count * 2)
        if actual != expected:
            raise ValueError(f'{kind}: original output mismatch')
        report[kind] = dict(match=True, original_bytes=length, compared_bytes=len(actual), sha256=hashlib.sha256(actual).hexdigest())
    _, rgb = decode(data)
    report['rgb_sha256'] = hashlib.sha256(rgb).hexdigest()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('assets', type=Path, nargs='+')
    args = parser.parse_args()
    executable = args.executable.read_bytes()
    for path in args.assets:
        print(json.dumps(dict(file=path.name, **validate(executable, path.read_bytes()))), flush=True)


if __name__ == '__main__':
    main()
