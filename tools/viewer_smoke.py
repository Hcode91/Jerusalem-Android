#!/usr/bin/env python3
"""Launch the SDL2 viewer with independently generated AQC pixels and dummy video."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from android_smoke import synthetic_aqc, synthetic_animated_aqc


def main():
    with tempfile.TemporaryDirectory() as directory:
        node = Path(directory) / 'synthetic.aqc'
        node.write_bytes(synthetic_aqc())
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        result = subprocess.run([sys.argv[1], str(node), '--frames', '2'], env=env,
                                capture_output=True, text=True, timeout=30)
        if result.returncode or 'SDL2 panorama viewer completed' not in result.stdout:
            raise RuntimeError(f'SDL2 viewer failed ({result.returncode}): {result.stdout}\n{result.stderr}')
        print('PASS: SDL2 window/renderer, AQC decode, camera render and two-frame clean shutdown with synthetic data')
        node.write_bytes(synthetic_animated_aqc())
        result = subprocess.run([sys.argv[1], str(node), '--frames', '12', '--preview'], env=env,
                                capture_output=True, text=True, timeout=30)
        if result.returncode or 'channel=1 frame=0' not in result.stdout or 'channel=1 frame=1' not in result.stdout:
            raise RuntimeError(f'SDL2 animation preview failed: {result.stdout}\n{result.stderr}')
        print('PASS: native animated patch composition and timed preview advancement')


if __name__ == '__main__':
    main()
