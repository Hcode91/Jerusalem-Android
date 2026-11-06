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

        for name in ('SYNTH_A', 'SYNTH_B'):
            (Path(directory) / (name + '.aqc')).write_bytes(synthetic_animated_aqc())
        model = Path(directory) / 'a01-navigation.tsv'
        model.write_text('JERUSALEM_A01_NAV_V3\nnode 1 SYNTH_A 2\nnode 2 SYNTH_B 2\nexit 1 0 2 0 1 0 -750 3600 750 53 0 0 0 85\nitem 10 1 0 -750 3600 750 60 0 50 1 "Synthetic pickup"\nitem-channel 10 1 0\n')
        result = subprocess.run([sys.argv[1], str(Path(directory) / 'SYNTH_A.aqc'),
                                 '--navigation', str(model), '--frames', '2'], env=env,
                                capture_output=True, text=True, timeout=30)
        if result.returncode or 'Pickup preview ready' not in result.stdout or 'SDL2 panorama viewer completed' not in result.stdout:
            raise RuntimeError(f'SDL2 navigation launch failed: {result.stdout}\n{result.stderr}')
        print('PASS: traversal model loading and asset resolution with synthetic data')


if __name__ == '__main__':
    main()
