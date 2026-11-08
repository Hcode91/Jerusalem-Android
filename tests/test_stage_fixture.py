"""Cross-check original-free emulator stage data against native readers."""
import os
import io
import wave
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from android_smoke import synthetic_stage_fixture, synthetic_menu_archive


class StageFixtureTests(unittest.TestCase):
    def test_exported_camp_save_loads_with_its_generated_assets(self):
        native = os.environ.get('JERUSALEM_JINSPECT')
        model = os.environ.get('JERUSALEM_CAMP_MODEL')
        if not native or not model: self.skipTest('native CTest route export required')
        from export_a01_smoke_fixture import export
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);archive = root/'fixture.zip'
            export(Path(model), archive, Path(native))
            with zipfile.ZipFile(archive) as fixture: fixture.extractall(root/'data-root')
            data = root/'data-root'
            report = subprocess.check_output([native, 'story-checkpoint', str(data/'a01-navigation.tsv'), str(data/'a01-stage.tsv'), str(data), str(data/'camp.jrs')], text=True)
            self.assertIn('Story scene=10023 finished=0 inventory=0', report)

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
        sound = io.BytesIO()
        with wave.open(sound, 'wb') as writer:
            writer.setnchannels(2);writer.setsampwidth(2);writer.setframerate(8000);writer.writeframes(bytes(320))
        files = synthetic_stage_fixture(bytes(bitmap), sound.getvalue())
        self.assertIn('data/a01/sound/eng/SYNTH_VOICE_10999.WAS', files)
        self.assertNotIn('data/a01/sound/eng/SYNTH_VOICE_10999.WAC', files)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, data in files.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            report = subprocess.check_output([native, 'stage', str(root/'a01-stage.tsv')], text=True)
            self.assertIn('5 backgrounds, 0 hotspots, 0 layers, 41 voice bindings', report)
            subprocess.run([native, 'stage-art', str(root/'data/A01.AT3'), 'SYNTH.BMP', str(root/'image.pam')], check=True)
            header, pixels = (root/'image.pam').read_bytes().split(b'ENDHDR\n', 1)
            self.assertIn(b'TUPLTYPE RGB_ALPHA', header)
            self.assertEqual(len(pixels), 16)
            self.assertEqual(pixels[3::4], bytes([255])*4)
            self.assertNotEqual(pixels[:3], pixels[4:7])
            subprocess.run([native, 'stage-art', str(root/'data/SY.AT3'), 'MENU.BMP', str(root/'menu.pam')], check=True)
            header, pixels = (root/'menu.pam').read_bytes().split(b'ENDHDR\n', 1)
            self.assertIn(b'WIDTH 640\nHEIGHT 480', header)
            self.assertEqual(len(pixels), 640*480*4)
            report = subprocess.check_output([native, 'menu', str(root/'data/SY.AT3')], text=True)
            self.assertIn('640x480, 10 highlights', report)
            self.assertIn('99002\tResume Game\t269,64,469,100\tMEN2_H.BMP', report)
            # Optional art failures must not hide the working background/other actions.
            (root/'partial.at3').write_bytes(synthetic_menu_archive(bytes(bitmap), omitted=('MEN1_H.BMP',), invalid='MEN2_H.BMP'))
            report = subprocess.check_output([native, 'menu', str(root/'partial.at3')], text=True)
            self.assertIn('640x480, 8 highlights', report)
            self.assertIn('MEN1_H.BMP: missing AT3 image:', report)
            self.assertIn('MEN2_H.BMP: unexpected menu highlight dimensions', report)
            for bad in (files['data/A01.AT3'], files['data/SY.AT3'][:-1], synthetic_menu_archive(bytes(bitmap), background=bytes(bitmap))):
                (root/'invalid.at3').write_bytes(bad)
                result = subprocess.run([native, 'menu', str(root/'invalid.at3')], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
            report = subprocess.check_output([native, 'cinema', str(root/'data/a01/pla/eng/A01S01_AN001.CI2')], text=True)
            self.assertIn('Decoded 3 frame(s)', report)
