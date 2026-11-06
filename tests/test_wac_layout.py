import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('wac_layout', Path(__file__).parents[1] / 'tools/wac_layout.py')
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


def packed(bits):
    padding = (-len(bits)) % 8
    return int(bits + '0' * padding, 2).to_bytes((len(bits) + padding) // 8, 'big')


def fixture(channels=1, sample_bytes=None):
    pcm_size = (512 if channels == 1 else 4) if sample_bytes is None else sample_bytes
    wave = (b'RIFF' + struct.pack('<I', 36 + pcm_size) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 1, channels, 22050, 22050 * channels * 2, channels * 2, 16) +
            b'data' + struct.pack('<I', pcm_size))
    if channels == 1:
        payload = packed('000' + '0' + '0' * 10 + '1' * 255)
        block = struct.pack('<H', len(payload)) + payload
        total = 556
    else:
        payload = packed('0' + f'{0x123:012b}' + '10' + '000000')
        block = struct.pack('<II', len(payload), pcm_size) + payload
        total = 44 + pcm_size
    return struct.pack('<II', 1, total) + wave + block


class LayoutTests(unittest.TestCase):
    def test_mono_tokens_and_padded_final_block(self):
        result = layout.inspect(fixture(sample_bytes=510))
        self.assertEqual(result['decoded_padding'], 2)
        self.assertEqual(result['blocks'][0]['token_counts'], {'new_delta': 1, 'repeat_delta': 255})
        self.assertFalse(result['pcm_generated'])

    def test_stereo_literal_and_cache_reference(self):
        result = layout.inspect(fixture(2))
        self.assertEqual(result['blocks'][0]['token_counts'], {'literal': 1, 'cache_reference': 1})
        self.assertEqual(result['pcm_data_size'], 4)

    def test_every_truncation(self):
        for original in (fixture(), fixture(2)):
            for n in range(len(original)):
                with self.subTest(length=n, full_size=len(original)), self.assertRaises(layout.FormatError):
                    layout.inspect(original[:n])

    def test_overrun_and_trailing_bytes(self):
        b = bytearray(fixture())
        struct.pack_into('<H', b, 52, 65535)
        with self.assertRaises(layout.FormatError):
            layout.inspect(b)
        with self.assertRaises(layout.FormatError):
            layout.inspect(fixture() + b'extra')

    def test_oversized_block_count(self):
        b = bytearray(fixture())
        struct.pack_into('<I', b, 0, 0xffffffff)
        with self.assertRaises(layout.FormatError):
            layout.inspect(b)

    def test_repeat_before_first_stereo_sample(self):
        b = bytearray(fixture(2))
        b[60] = 0xff
        with self.assertRaisesRegex(layout.FormatError, 'precedes any output'):
            layout.inspect(b)

    def test_partial_token(self):
        b = bytearray(fixture(2))
        # Two literal tokens require 26 bits; this block has only 24.
        b[60:] = bytes(3)
        struct.pack_into('<I', b, 52, 3)
        with self.assertRaisesRegex(layout.FormatError, 'ends inside a token'):
            layout.inspect(b)


if __name__ == '__main__':
    unittest.main()
