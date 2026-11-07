#!/usr/bin/env python3
"""Private full-frame comparison of native CNM output against isolated original DLL routines."""
import argparse
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path
from cinema_reference import CinemaReference


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte)*1099511628211) & ((1 << 64)-1)
    return value


def original(dll, data, raw=None):
    width, height = struct.unpack_from('<ii', data, 17)
    frames = struct.unpack_from('<I', data, 28)[0]
    if data[:8] != b'CNM UNR\0' or data[26] or data[25] != 32:
        raise ValueError('unsupported original reference variant')
    reference = CinemaReference(dll, width, height)
    p = 192+16*data[27]+8*frames
    hashes, pcm = [], [bytearray() for _ in range(data[27])]
    image = bytes(width*height*4)
    while p < len(data):
        tag = data[p]; p += 1
        if tag in b'ZAB':
            channel = b'ZAB'.index(tag)
            size, = struct.unpack_from('<I', data, p); p += 4
            pcm[channel] += data[p:p+size]; p += size
        elif tag == ord('T'):
            size, n, d = struct.unpack_from('<IHH', data, p); p += 8
            reference.codebook(data[p:p+size], d, n); p += size
        elif tag in b'SUX':
            header = data[p:p+47]; p += 47
            size, = struct.unpack_from('<I', header)
            if tag != ord('X'):
                image = reference.frame(header, data[p:p+size])
            p += size; hashes.append(fnv(image))
            if raw is not None: raw.write(image)
        else:
            raise ValueError(f'unsupported reference tag {tag}')
        if p > len(data):
            raise ValueError('reference source truncated')
    if len(hashes) != frames:
        raise ValueError('reference frame count differs')
    return hashes, [dict(size=len(channel), hash=fnv(channel)) for channel in pcm]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exact', action='store_true', help='compare complete BGRX bytes via bounded temporary native output')
    parser.add_argument('dll', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('inputs', type=Path, nargs='+')
    args = parser.parse_args()
    dll = args.dll.read_bytes(); files = []
    for path in args.inputs:
        files.extend(sorted(path.glob('*.stored')) if path.is_dir() else [path])
    report = dict(files=0, frames=0, matches=[], failures=[])
    for path in files:
        data = path.read_bytes()
        if data[:8] != b'CNM UNR\0':
            continue
        try:
            with tempfile.TemporaryDirectory(prefix='jerusalem-cnm-') as folder:
                root = Path(folder)
                if args.exact:
                    with (root/'expected').open('wb') as output:
                        expected, pcm = original(dll, data, output)
                    run = subprocess.run([str(args.native.resolve()), 'cinema-raw', str(path), str(root/'actual')], capture_output=True, text=True, timeout=120)
                    if run.returncode: raise ValueError(run.stderr.strip())
                    with (root/'expected').open('rb') as left, (root/'actual').open('rb') as right:
                        while True:
                            chunk = left.read(1024*1024);other = right.read(1024*1024)
                            if chunk != other: raise ValueError('native frame bytes differ from original DLL output')
                            if not chunk: break
                else:
                    expected, pcm = original(dll, data)
                    run = None
            if run is None: run = subprocess.run([str(args.native.resolve()), 'cinema', str(path)], capture_output=True, text=True, timeout=120)
            if run.returncode:
                raise ValueError(run.stderr.strip())
            actual = [int(value, 16) for value in re.findall(r'^frame\t\d+\t([0-9a-f]+)$', run.stdout, re.M)]
            if actual != expected:
                difference = next((i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1]), None)
                raise ValueError(f'frame hashes differ at {difference}; native={len(actual)} reference={len(expected)}')
            report['files'] += 1; report['frames'] += len(actual)
            report['matches'].append(dict(file=str(path), frames=len(actual), pcm=pcm, byte_exact=args.exact))
        except Exception as error:
            report['failures'].append(dict(file=str(path), error=str(error)))
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report['failures']))
