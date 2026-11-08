#!/usr/bin/env python3
"""Package original-free assets and a camp save produced by native route tests.

The native tests reach the camp through actual game-state transitions. Only the
asset fingerprint and checksum are filled after generating these test resources.
No original game files are read and this fixture is never shipped inside the APK.
"""
import argparse
import io
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import wave
import zipfile
import zlib
from android_smoke import synthetic_stage_fixture, synthetic_menu_bitmap, synthetic_story_aqc


def export(model, output, native):
    bitmap = synthetic_menu_bitmap(2, 2, (90, 20, 70))
    audio = io.BytesIO()
    with wave.open(audio, 'wb') as writer:
        writer.setnchannels(2);writer.setsampwidth(2);writer.setframerate(8000)
        writer.writeframes(struct.pack('<hh', 2000, -2000)*1600)
    files = synthetic_stage_fixture(bitmap, audio.getvalue())
    for name in ('a01-navigation.tsv', 'a01-stage.tsv'):
        files[name] = (model/name).read_bytes()
    navigation = [shlex.split(line) for line in files['a01-navigation.tsv'].decode().splitlines()]
    for row in navigation:
        if row[0] == 'node':
            if not row[2].startswith('SYNTH_'): raise ValueError('Only original-free native test models are permitted')
            files[row[2]+'.aqc'] = synthetic_story_aqc(int(row[3]))
    pictures = {'SYNTH.BMP'}
    for line in files['a01-stage.tsv'].decode().splitlines():
        row = shlex.split(line)
        if row[0] == 'background' and row[2] != 'SYNTH.BMP': raise ValueError('Original-free fixture required')
        if row[0] == 'dialogue':
            if not row[3].startswith('SYNTH_'): raise ValueError('Original-free speech required')
            files[f'data/a01/sound/eng/{row[3]}.WAC'] = audio.getvalue()
            files[f'data/a01/dia/eng/{row[3]}.DIA'] = b'0 Synthetic camp speech\n200 END\n'
        if row[0] == 'layer' and row[4] == '0':
            if row[6] != 'SYNTH': raise ValueError('Original-free layer required')
            pictures.update(f'SYNTH.{i:04d}.BMP' for i in range(1, int(row[8])+1))
    files['data/a01/dia/eng/SYNTH_11018.DAN'] = b'1\n1 10016 3\n0 99 1\n100 199 0\n200 END\n'
    offset = 32+255*len(pictures);entries = bytearray();payload = bytearray()
    for picture in sorted(pictures):
        name = ('\\bgr\\'+picture).encode()
        entries += name+bytes(243-len(name))+struct.pack('<III', offset+len(payload), len(bitmap), len(bitmap));payload += bitmap
    files['data/A01.AT3'] = b'ATIII'+bytes(3)+struct.pack('<III', len(entries), len(pictures), 255)+bytes(12)+entries+payload
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in files.items():
            path = root/name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(content)
        result = subprocess.check_output([str(native), 'story-tags', str(root/'a01-navigation.tsv'), str(root/'a01-stage.tsv'), str(root)], text=True)
    current, _ = map(int, result.split())
    state = bytearray((model/'camp.jrs').read_bytes())
    if state[:8] != b'JRNSTORY' or struct.unpack_from('<I', state, 8)[0] != 2: raise ValueError('Native v2 save required')
    struct.pack_into('<Q', state, 20, current)
    struct.pack_into('<I', state, len(state)-4, zlib.crc32(state[:-4]))
    files['camp.jrs'] = state
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, content in sorted(files.items()): archive.writestr(name, content)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', type=Path);parser.add_argument('output', type=Path);parser.add_argument('--native', type=Path, default=Path('build-host/jinspect'))
    args = parser.parse_args();export(args.model, args.output, args.native.resolve())
