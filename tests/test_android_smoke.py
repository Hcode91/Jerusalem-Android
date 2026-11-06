import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('android_smoke', Path(__file__).parents[1] / 'tools/android_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def ui(*nodes):
    root = ET.Element('hierarchy')
    for node in nodes:
        ET.SubElement(root, 'node', node)
    return ET.tostring(root)


class SmokeHarnessTests(unittest.TestCase):
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
