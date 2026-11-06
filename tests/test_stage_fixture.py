"""Cross-check original-free emulator stage data against native readers."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from android_smoke import synthetic_stage_fixture


class StageFixtureTests(unittest.TestCase):
    def test_stage_model_and_archive_use_native_parsers(self):
        native = os.environ.get('JERUSALEM_JINSPECT')
        if not native:
            self.skipTest('native tool provided by CTest')
        bitmap = bytearray(70)
        bitmap[:2] = b'BM'
        struct.pack_into('<I', bitmap, 2, len(bitmap))
        struct.pack_into('<I', bitmap, 10, 54)
        struct.pack_into('<IiiHH', bitmap, 14, 40, 2, 2, 1, 24)
        bitmap[54:] = bytes([255, 0, 0, 0, 255, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0])
        files = synthetic_stage_fixture(bytes(bitmap), b'synthetic PCM supplied separately')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, data in files.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            report = subprocess.check_output([native, 'stage', str(root/'a01-stage.tsv')], text=True)
            self.assertIn('5 backgrounds, 0 hotspots, 0 layers, 16 voice bindings', report)
            subprocess.run([native, 'stage-art', str(root/'data/A01.AT3'), 'SYNTH.BMP', str(root/'image.pam')], check=True)
            header, pixels = (root/'image.pam').read_bytes().split(b'ENDHDR\n', 1)
            self.assertIn(b'TUPLTYPE RGB_ALPHA', header)
            self.assertEqual(len(pixels), 16)
            self.assertEqual(pixels[3::4], bytes([255])*4)
            self.assertNotEqual(pixels[:3], pixels[4:7])
            report = subprocess.check_output([native, 'cinema', str(root/'data/a01/pla/eng/A01S01_AN001.CI2')], text=True)
            self.assertIn('Decoded 3 frame(s)', report)
