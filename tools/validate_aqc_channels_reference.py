#!/usr/bin/env python3
"""Validate original AQC patch indices with original CPU instructions; optionally compare native RGB."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from aqc_channels import channels, frame_rgb
from aqc_decode import metadata, cache_words
from reference_cpu import OriginalDecoder


def validate(executable, path, native_tool=None):
    data = path.read_bytes()
    directory = channels(data)
    frames = [frame for channel in directory for frame in channel['frames']]
    if not frames:
        raise ValueError('no channel frames to validate')
    decoder = OriginalDecoder(executable, 'indices', max(f['stored_size'] for f in frames),
                              max(f['width']*f['height']//2 for f in frames))
    info = metadata(data)
    offset = info['palette_offset']
    palette = cache_words(data[offset:offset+info['palette_size']], 16, 32400)
    index_digest, rgb_digest = hashlib.sha256(), hashlib.sha256()
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / 'native'
        if native_tool:
            run = subprocess.run([str(native_tool.resolve()), 'extract-aqc-channels', str(path), str(output)],
                                 capture_output=True, text=True)
            if run.returncode:
                raise ValueError(f'native export failed: {run.stderr}')
        for channel in directory:
            for frame in channel['frames']:
                start = frame['payload_offset']
                payload = data[start:start+frame['stored_size']]
                count = frame['width']*frame['height']//4
                words = cache_words(payload, 13, count)
                actual = struct.pack('<'+'H'*count, *words)
                expected, _ = decoder.call(payload, [0, len(payload)*8], count*2)
                if actual != expected:
                    raise ValueError(f'original index mismatch: channel {channel["index"]}, frame {frame["index"]}')
                rgb = frame_rgb(data, frame, palette)
                if native_tool:
                    name = f'channel-{channel["index"]}-frame-{frame["index"]}.ppm'
                    header = f'P6\n{frame["width"]} {frame["height"]}\n255\n'.encode()
                    if (output / name).read_bytes() != header + rgb:
                        raise ValueError(f'native/Python RGB mismatch: {name}')
                index_digest.update(actual)
                rgb_digest.update(rgb)
    return dict(file=path.name, channels=len(directory), frames=len(frames), original_indices_match=True,
                native_rgb_match=bool(native_tool), indices_sha256=index_digest.hexdigest(), rgb_sha256=rgb_digest.hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('assets', type=Path, nargs='+')
    parser.add_argument('--native-tool', type=Path)
    args = parser.parse_args()
    executable = args.executable.read_bytes()
    for path in args.assets:
        print(json.dumps(validate(executable, path, args.native_tool)), flush=True)


if __name__ == '__main__':
    main()
