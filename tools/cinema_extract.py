#!/usr/bin/env python3
"""Extract progressive CNM/CI2 to PNG frames and PCM WAV using the native decoder."""
import argparse
import os
from pathlib import Path
import subprocess
from aqc_decode import png


def extract(source, output, native):
    subprocess.run([str(native), 'extract-cinema', str(source), str(output)], check=True)
    count = 0
    for image in output.glob('*.ppm'):
        raw = image.read_bytes()
        magic, size, maximum, rgb = raw.split(b'\n', 3)
        width, height = map(int, size.split())
        if magic != b'P6' or maximum != b'255' or not 0 < width <= 4096 or not 0 < height <= 4096:
            raise ValueError('native export is not a supported PPM image')
        with image.with_suffix('.png').open('xb') as target:
            target.write(png(width, height, rgb))
        image.unlink()
        count += 1
    return count


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path, help='new directory, never overwritten')
    parser.add_argument('--native', type=Path, default=Path(os.environ.get('JERUSALEM_JINSPECT', 'build-host/jinspect')))
    args = parser.parse_args()
    print(f'{extract(args.input, args.output, args.native)} PNG frames exported; PCM WAV files retain native track numbers')
