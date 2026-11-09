import importlib.util
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('pe_probe', Path(__file__).parents[1] / 'tools/pe_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def fixture():
    b = bytearray(1024)
    b[:2] = b'MZ'
    struct.pack_into('<I', b, 0x3c, 0x80)
    b[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', b, 0x84, 0x14c, 1)
    struct.pack_into('<H', b, 0x94, 224)
    struct.pack_into('<H', b, 0x98, 0x10b)
    struct.pack_into('<I', b, 0x98 + 28, 0x400000)
    struct.pack_into('<I', b, 0x98 + 60, 512)
    struct.pack_into('<I', b, 0x98 + 92, 16)
    b[0x178:0x17d] = b'.text'
    struct.pack_into('<IIII', b, 0x180, 512, 4096, 512, 512)
    return b


class PETests(unittest.TestCase):
    def test_minimal(self):
        result = probe.analyze(fixture())
        self.assertEqual(result['machine'], 0x14c)
        self.assertEqual(result['sections'][0]['name'], '.text')

    def test_every_truncation(self):
        original = fixture()
        for n in range(len(original)):
            with self.subTest(length=n), self.assertRaises(probe.PEError):
                probe.analyze(original[:n])

    def test_invalid_rva(self):
        b = fixture()
        struct.pack_into('<II', b, 0x98 + 104, 0x700000, 40)
        with self.assertRaises(probe.PEError):
            probe.analyze(b)

    def test_bad_section(self):
        b = fixture()
        struct.pack_into('<I', b, 0x180 + 12, 0xffffffff)
        with self.assertRaises(probe.PEError):
            probe.analyze(b)

    def test_named_and_ordinal_import(self):
        b = fixture()
        struct.pack_into('<II', b, 0x98 + 104, 0x1000, 40)
        struct.pack_into('<IIIII', b, 512, 0x1040, 0, 0, 0x1060, 0x1040)
        struct.pack_into('<III', b, 576, 0x1080, 0x80000001, 0)
        b[608:617] = b'TEST.dll\0'
        b[642:648] = b'hello\0'
        result = probe.analyze(b)
        self.assertEqual(result['imports'][0]['symbols'][0]['name'], 'hello')
        self.assertEqual(result['imports'][0]['symbols'][1]['ordinal'], 1)


if __name__ == '__main__':
    unittest.main()
