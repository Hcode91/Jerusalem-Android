import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
from android_smoke import synthetic_aqc, synthetic_animated_aqc
from aqc_channels import channels, frame_rgb
from aqc_decode import cache_words, metadata, decode
from wac_layout import FormatError


class ChannelTests(unittest.TestCase):
    def native_reject(self, source):
        executable = os.environ.get('JERUSALEM_JINSPECT')
        if executable:
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'bad.aqc'
                path.write_bytes(source)
                run = subprocess.run([executable, 'extract-aqc-channels', str(path), str(Path(directory) / 'out')], capture_output=True)
                self.assertEqual(run.returncode, 1, run.stderr.decode())
                self.assertTrue(run.stderr.startswith(b'Error:'), run.stderr.decode())

    def test_channel_directory_and_frame_pixels(self):
        source = synthetic_animated_aqc()
        directory = channels(source)
        self.assertEqual([len(c['frames']) for c in directory], [1, 2])
        self.assertEqual([c['timing_value'] for c in directory], [0, 12])
        self.assertEqual((directory[1]['frames'][1]['left'], directory[1]['frames'][1]['top']), (4, 2))
        info = metadata(source)
        start = info['palette_offset']
        palette = cache_words(source[start:start+info['palette_size']], 16, 32400)
        self.assertEqual(frame_rgb(source, directory[1]['frames'][0], palette), bytes([248, 252, 248]) * 8)
        expected = bytes([248, 0, 0, 0, 252, 0, 0, 0, 248, 248, 252, 248]) * 2
        self.assertEqual(frame_rgb(source, directory[1]['frames'][1], palette), expected)
        executable = os.environ.get('JERUSALEM_JINSPECT')
        if executable:
            with tempfile.TemporaryDirectory() as temp:
                path, target = Path(temp) / 'node.aqc', Path(temp) / 'output'
                path.write_bytes(source)
                run = subprocess.run([executable, 'extract-aqc-channels', str(path), str(target)], capture_output=True)
                self.assertEqual(run.returncode, 0, run.stderr.decode())
                self.assertEqual((target / 'channel-1-frame-1.ppm').read_bytes(), b'P6\n4 2\n255\n' + expected)
                self.assertEqual(len((target / 'manifest.tsv').read_text().splitlines()), 4)

    def test_truncations_inside_records_and_valid_prefix_boundaries(self):
        source = synthetic_animated_aqc()
        directory = channels(source)
        base_end = metadata(source)['static_end']
        boundary = directory[1]['header_offset']
        self.assertEqual(channels(source[:base_end]), [])
        self.assertEqual(len(channels(source[:boundary])), 1)
        for end in range(base_end+1, len(source)):
            if end != boundary:
                with self.subTest(length=end), self.assertRaises(FormatError):
                    channels(source[:end])
        self.native_reject(source[:-1])

    def test_unsupported_dummy_record_keeps_base_decodable(self):
        source = synthetic_aqc()
        # Reproduce the structural issue in A09S02N06 without copying original data.
        tail = synthetic_animated_aqc()[len(source):]
        broken = source + struct.pack('<III', 1, 0, 0) + tail
        self.assertEqual(decode(broken)[1], decode(source)[1])
        with self.assertRaisesRegex(FormatError, 'frame header'):
            channels(broken)
        self.native_reject(broken)

    def test_invalid_counts_timing_and_patch_bounds(self):
        source = synthetic_animated_aqc()
        directory = channels(source)
        channel_start = directory[0]['header_offset']
        frame_start = directory[0]['frames'][0]['header_offset']
        fields = [(channel_start, 1001), (channel_start+4, 0x7fc00000),
                  (channel_start+8, 99), (frame_start, 0xffffffff),
                  (frame_start+4, 4096), (frame_start+16, 1), (frame_start+32, 1),
                  (frame_start+36, 12), (frame_start+44, 5), (frame_start+48, 0), (frame_start+52, 0)]
        for offset, value in fields:
            b = bytearray(source)
            struct.pack_into('<I', b, offset, value)
            with self.subTest(offset=offset), self.assertRaises(FormatError):
                channels(b)
            self.native_reject(b)

    def test_invalid_compressed_indices_and_partial_tokens(self):
        source = synthetic_animated_aqc()
        directory = channels(source)
        info = metadata(source)
        start = info['palette_offset']
        palette = cache_words(source[start:start+info['palette_size']], 16, 32400)
        frame = directory[0]['frames'][0]
        b = bytearray(source)
        value = int('0' + f'{8100:013b}' + '11', 2)
        b[frame['payload_offset']:frame['payload_offset']+2] = value.to_bytes(2, 'big')
        with self.assertRaisesRegex(FormatError, 'codebook'):
            frame_rgb(b, frame, palette)
        self.native_reject(b)
        # A frame advertising more tokens than physically present is not silently padded.
        b[frame['payload_offset']:frame['payload_offset']+2] = b'\0\0'
        with self.assertRaises(FormatError):
            frame_rgb(b, frame, palette)
        self.native_reject(b)


if __name__ == '__main__':
    unittest.main()
