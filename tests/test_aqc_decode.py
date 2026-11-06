import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
from aqc_decode import cache_words, decode, metadata, png
from wac_layout import FormatError


def pack(bits):
    bits += '0' * (-len(bits) % 8)
    return int(bits, 2).to_bytes(len(bits) // 8, 'big')


def fixture():
    header = struct.pack('<III4f6I', 8, 1, 2, 0., 360., -75., 75., 0, 8, 0, 1, 16, 16)
    indices = pack('0' + f'{0:013b}' + '0' + f'{1:013b}')
    colors = [0xf800, 0x07e0, 0x001f, 0xffff, 0, 0x7800, 0x03e0, 0x000f]
    palette = pack(''.join('0' + f'{c:016b}' for c in colors) + '11' * (32400 - len(colors)))
    return struct.pack('<I', len(indices)) + header + indices + struct.pack('<I', len(palette)) + palette


class AqcTests(unittest.TestCase):
    def native(self, source, expected=None):
        executable = os.environ.get('JERUSALEM_JINSPECT')
        if not executable:
            return
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'node.aqc'
            path.write_bytes(source)
            target = Path(temp) / 'converted'
            result = subprocess.run([executable, 'decode-aqc', str(path), str(target)], capture_output=True)
            if expected is None:
                self.assertEqual(result.returncode, 1, result.stderr.decode())
                self.assertTrue(result.stderr.startswith(b'Error:'), result.stderr.decode())
            else:
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                self.assertEqual((target / 'decoded.ppm').read_bytes(), b'P6\n8 1\n255\n' + expected)

    def test_four_pixel_vectors_rgb565_and_native(self):
        source = fixture()
        info, rgb = decode(source)
        self.assertEqual((info['width'], info['height'], info['undecoded_tail_bytes']), (8, 1, 0))
        expected = bytes([248, 0, 0, 0, 252, 0, 0, 0, 248, 248, 252, 248,
                          0, 0, 0, 120, 0, 0, 0, 124, 0, 0, 0, 120])
        self.assertEqual(rgb, expected)
        self.native(source, expected)

    def test_undecoded_animation_tail_is_reported(self):
        source = fixture() + b'UNDECODED'
        info, rgb = decode(source)
        self.assertEqual(info['undecoded_tail_bytes'], 9)
        self.assertEqual(rgb, decode(fixture())[1])
        self.native(source, rgb)

    def test_every_static_payload_truncation(self):
        original = fixture()
        for end in range(len(original)):
            with self.assertRaises(FormatError):
                decode(original[:end])
        for end in (0, 55, 56, 60, len(original) - 1):
            self.native(original[:end])

    def test_invalid_geometry_stride_lengths_and_index(self):
        original = fixture()
        for offset, value in ((0, 0xffffffff), (4, 0xffffffff), (8, 0), (12, 4),
                              (16, 0x7fc00000), (32, 1), (48, 0), (52, 0),
                              (60, 0xffffffff)):
            broken = bytearray(original)
            struct.pack_into('<I', broken, offset, value)
            with self.subTest(offset=offset), self.assertRaises(FormatError):
                decode(broken)
            self.native(broken)
        broken = bytearray(original)
        broken[56:60] = pack('0' + f'{8100:013b}' + '0' + f'{1:013b}')
        with self.assertRaisesRegex(FormatError, 'codebook'):
            decode(broken)
        self.native(broken)

    def test_shared_cache_syntax_and_padding(self):
        self.assertEqual(cache_words(pack('0' + f'{123:013b}' + '10000000' + '11'), 13, 3), [123] * 3)
        for bits, count in (('11', 1), ('0', 1), ('0' * 14 + '1', 1)):
            with self.assertRaises(FormatError):
                cache_words(pack(bits), 13, count)

    def test_png_chunks_crc_dimensions_and_pixels(self):
        info, rgb = decode(fixture())
        data = png(info['width'], info['height'], rgb)
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        cursor, compressed = 8, bytearray()
        names = []
        while cursor < len(data):
            size = struct.unpack_from('>I', data, cursor)[0]
            name = data[cursor + 4:cursor + 8]
            payload = data[cursor + 8:cursor + 8 + size]
            crc = struct.unpack_from('>I', data, cursor + 8 + size)[0]
            self.assertEqual(crc, zlib.crc32(name + payload))
            names.append(name)
            if name == b'IHDR':
                self.assertEqual(struct.unpack('>IIBBBBB', payload), (8, 1, 8, 2, 0, 0, 0))
            if name == b'IDAT':
                compressed.extend(payload)
            cursor += size + 12
        self.assertEqual(names, [b'IHDR', b'IDAT', b'IEND'])
        self.assertEqual(zlib.decompress(compressed), b'\0' + rgb)


if __name__ == '__main__':
    unittest.main()
