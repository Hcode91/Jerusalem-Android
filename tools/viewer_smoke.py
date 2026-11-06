#!/usr/bin/env python3
"""Launch the SDL2 viewer with independently generated AQC pixels and dummy video."""
import os
from pathlib import Path
import subprocess
import struct
import zlib
import sys
import tempfile

from android_smoke import synthetic_aqc, synthetic_animated_aqc, synthetic_cinema
from cinema_extract import extract as extract_cinema
import wave


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

        checkpoint = Path(directory) / 'native-preview.jrs'
        for action in ('save', 'load'):
            result = subprocess.run([sys.argv[1], str(Path(directory) / 'SYNTH_A.aqc'),
                                     '--navigation', str(model), '--checkpoint', str(checkpoint),
                                     '--'+action+'-checkpoint', '--frames', '2'], env=env,
                                    capture_output=True, text=True, timeout=30)
            if result.returncode or ('Checkpoint '+('saved' if action == 'save' else 'loaded')) not in result.stdout:
                raise RuntimeError(f'SDL2 checkpoint {action} failed: {result.stdout}\n{result.stderr}')
        raw = checkpoint.read_bytes()
        if (raw[:8] != b'JRNSAVE\0' or len(raw) != 68 or struct.unpack_from('<II', raw, 8) != (1, 1) or
                struct.unpack_from('<II', raw, 32) != (1, 0) or struct.unpack_from('<ddd', raw, 40) != (225., 0., 85.) or
                struct.unpack_from('<I', raw, len(raw)-4)[0] != zlib.crc32(raw[:-4])):
            raise RuntimeError('Native checkpoint does not match its documented LE/IEEE754/CRC32 format')
        print('PASS: native checkpoint saved and loaded by separate SDL2 viewer processes')

        model.write_text(model.read_text().replace('NAV_V3', 'NAV_V4') +
                         'item 11 1 0 -750 3600 750 60 0 50 1 "Synthetic stone"\n'
                         'item-channel 11 2 0\ncombine 10 11 20 "Synthetic sling"\n')
        command = [sys.argv[1], str(Path(directory) / 'SYNTH_A.aqc'), '--navigation', str(model),
                   '--checkpoint', str(checkpoint), '--frames', '2']
        def launch(options):
            run = subprocess.run(command+options, env=env, capture_output=True, text=True, timeout=30)
            if run.returncode:
                raise RuntimeError(f'SDL2 combination failed: {run.stdout}\n{run.stderr}')
            return run.stdout
        launch(['--save-checkpoint'])
        # Independent synthetic checkpoint construction uses the genuine model/data digest.
        # It represents two pickups; the native loader must reconstruct their visibility/counter.
        prefix = bytearray(checkpoint.read_bytes()[:64])
        struct.pack_into('<I', prefix, 36, 2)
        owned = prefix+struct.pack('<II', 10, 11)
        checkpoint.write_bytes(owned+struct.pack('<I', zlib.crc32(owned)))
        if 'Combined: Synthetic sling' not in launch(['--load-checkpoint', '--combine', '11', '10', '--save-checkpoint']):
            raise RuntimeError('SDL2 did not perform the reverse-order native combination')
        combined = checkpoint.read_bytes()
        if struct.unpack_from('<I', combined, 12)[0] != 2 or struct.unpack_from('<III', combined, 64) != (10, 11, 0x80000014):
            raise RuntimeError('SDL2 did not preserve the combination action log')
        if 'inventory=1' not in launch(['--load-checkpoint']):
            raise RuntimeError('SDL2 combination result did not survive process restart')
        print('PASS: native inventory combination, checkpoint action log and independent-process restoration')

        movie = Path(directory)/'generated.CI2';movie.write_bytes(synthetic_cinema())
        result = subprocess.run([sys.argv[2], str(movie), '--exit-at-end'], env=env, capture_output=True, text=True, timeout=30)
        if result.returncode or 'Cinema decoded: 3 frames' not in result.stdout:
            raise RuntimeError(f'SDL2 cinema failed: {result.stdout}\n{result.stderr}')
        print('PASS: SDL2 native CNM T/U/S decoding, timed presentation and stereo PCM queue')
        converted = Path(directory)/'cinema-export'
        native = Path(sys.argv[1]).parent/'jinspect'
        if extract_cinema(movie, converted, native) != 3:
            raise RuntimeError('native Cinema PNG extraction count differs')
        if any(not frame.read_bytes().startswith(b'\x89PNG\r\n\x1a\n') for frame in converted.glob('*.png')):
            raise RuntimeError('Cinema converter did not produce standard PNG files')
        with wave.open(str(converted/'audio-0.wav'), 'rb') as sound:
            if (sound.getnchannels(), sound.getsampwidth(), sound.getframerate(), sound.getnframes()) != (2, 2, 22050, 2646):
                raise RuntimeError('Cinema WAV export lost stereo geometry or sample count')
        print('PASS: native-backed PNG/WAV extraction with independently checked stereo PCM format')



if __name__ == '__main__':
    main()
