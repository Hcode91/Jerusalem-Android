import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import io
import sys
import wave

spec = importlib.util.spec_from_file_location('android_smoke', Path(__file__).parents[1] / 'tools/android_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def ui(*nodes):
    root = ET.Element('hierarchy')
    for node in nodes:
        ET.SubElement(root, 'node', node)
    return ET.tostring(root)


class SmokeHarnessTests(unittest.TestCase):
    def test_synthetic_aqc_contains_known_colour_bars(self):
        sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
        from aqc_decode import decode
        info, rgb = decode(smoke.synthetic_aqc())
        self.assertEqual((info['width'], info['height']), (8, 4))
        self.assertEqual(rgb, bytes([248, 0, 0, 0, 252, 0, 0, 0, 248, 248, 252, 248]) * 8)

    def test_compressed_audio_signals_are_nonempty_valid_waves(self):
        sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
        from wac_decode import decode
        for name, data in smoke.compressed_audio_fixtures().items():
            with self.subTest(file=name), wave.open(io.BytesIO(decode(data))) as reader:
                self.assertEqual(reader.getframerate(), 22050)
                self.assertGreaterEqual(reader.getnframes(), 22050)
                self.assertTrue(any(reader.readframes(reader.getnframes())))

    def test_android_all_caps_button_regression(self):
        xml = ui({'text': 'BROWSE ASSETS', 'enabled': 'true', 'bounds': '[12,40][152,88]'})
        self.assertEqual(smoke.control_center(xml, 'Browse assets'), (82, 64))

    def test_accessibility_label_survives_theme_or_visible_text_change(self):
        xml = ui({'text': 'Different displayed text', 'content-desc': 'Browse assets', 'bounds': '[10,20][110,60]'})
        self.assertEqual(smoke.control_center(xml, 'Browse assets'), (60, 40))

    def test_disabled_and_clipped_controls_are_not_tapped(self):
        xml = ui({'text': 'Close', 'enabled': 'false', 'bounds': '[0,0][100,100]'},
                 {'text': 'CLOSE', 'bounds': '[300,20][100,60]'},
                 {'text': 'CLOSE', 'bounds': '[-50,20][-10,60]'})
        self.assertIsNone(smoke.control_center(xml, 'Close'))

    def test_diagnostics_survive_unavailable_device(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(smoke, 'OUTPUT', Path(directory)), \
             patch.object(smoke, 'adb', side_effect=OSError('device disconnected')):
            smoke.collect_diagnostics()
            self.assertIn('device disconnected', (Path(directory) / 'diagnostic-errors.txt').read_text())


if __name__ == '__main__':
    unittest.main()
