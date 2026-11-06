import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
from wac_decode import decode
from wac_layout import FormatError


def packed(bits):
    bits += '0' * (-len(bits) % 8)
    return int(bits, 2).to_bytes(len(bits) // 8, 'big')


def literal(value):
    return '0' + f'{value:012b}'


def reference(index):
    return '10' + f'{index:06b}'


def container(channels, blocks, pcm_size=None):
    size = sum(n for _, n in blocks)
    pcm_size = size if pcm_size is None else pcm_size
    header = (b'RIFF' + struct.pack('<I', 36 + pcm_size) + b'WAVEfmt ' +
              struct.pack('<IHHIIHH', 16, 1, channels, 22050, 44100 * channels, channels * 2, 16) +
              b'data' + struct.pack('<I', pcm_size))
    output = bytearray(struct.pack('<II', len(blocks), 44 + size) + header)
    for bits, decoded_size in blocks:
        payload = packed(bits)
        output += struct.pack('<H', len(payload)) if channels == 1 else struct.pack('<II', len(payload), decoded_size)
        output += payload
    return bytes(output)


class DecodeTests(unittest.TestCase):
    def verify(self, source, samples):
        actual = decode(source)
        header = bytearray(source[8:52])
        struct.pack_into('<I', header, 4, 36 + len(samples) * 2)
        expected = bytes(header) + struct.pack('<' + 'H' * len(samples), *samples)
        self.assertEqual(actual, expected)
        with wave.open(io.BytesIO(actual)) as reader:
            self.assertEqual(reader.getsampwidth(), 2)
            self.assertEqual(reader.getframerate(), 22050)
            self.assertEqual(reader.readframes(reader.getnframes()), expected[44:])
        executable = os.environ.get('JERUSALEM_JINSPECT')
        if executable:
            with tempfile.TemporaryDirectory() as temp:
                source_path = Path(temp) / 'sound.bin'
                source_path.write_bytes(source)
                target = Path(temp) / 'converted'
                run = subprocess.run([executable, 'decode-audio', str(source_path), str(target)], capture_output=True)
                self.assertEqual(run.returncode, 0, run.stderr.decode())
                self.assertEqual((target / 'decoded.wav').read_bytes(), expected)

    def reject(self, source):
        with self.assertRaises(FormatError):
            decode(source)
        executable = os.environ.get('JERUSALEM_JINSPECT')
        if executable:
            with tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / 'bad.bin'
                path.write_bytes(source)
                result = subprocess.run([executable, 'decode-audio', str(path), str(Path(temp) / 'output')], capture_output=True)
                self.assertEqual(result.returncode, 1, result.stderr.decode())
                self.assertTrue(result.stderr.startswith(b'Error:'), result.stderr.decode())

    def test_mono_signed_delta_wrapping_and_trim(self):
        codes = [511, 511, 513, 512] + [1] * 252
        samples, predictor = [], 0
        for code in codes:
            predictor = (predictor + (-(code & 511) if code & 512 else code) * 64) & 65535
            samples.append(predictor)
        source = container(1, [('000' + ''.join('0' + f'{c:010b}' for c in codes), 512)], 510)
        self.verify(source, samples[:255])

    def test_mono_state_crosses_blocks(self):
        source = container(1, [('000' + '0' + f'{1:010b}' + '1' * 255, 512), ('000' + '1' * 256, 512)])
        self.verify(source, [(64 * i) & 65535 for i in range(1, 513)])

    def test_stereo_cache_replacement_repeats_and_reset(self):
        # Literal overwrites the current slot. Referencing it chooses the oldest slot.
        tokens = (literal(0x123) + literal(0x456) + reference(0) + literal(0x789) +
                  '11' + reference(0) + literal(0xabc) + reference(1))
        # Cache resets in the next block, including entries never written by a literal.
        source = container(2, [(tokens, 16), (reference(1) + literal(0xfff), 4)])
        self.verify(source, [0x1230, 0x4560, 0x4560, 0x7890, 0x7890, 0x4560, 0xabc0, 0x7890, 0, 0xfff0])

    def test_stereo_all_cache_entries_and_oldest_replacement(self):
        tokens, expected = '', []
        for i in range(64):
            tokens += literal(i + 1) + reference(i)
            expected += [(i + 1) << 4] * 2
        tokens += literal(0xabc) + reference(0)
        expected += [0xabc0] * 2
        self.verify(container(2, [(tokens, len(expected) * 2)]), expected)

    def test_stereo_padded_riff_is_normalized_to_data_length(self):
        source = bytearray(container(2, [(literal(0x123) + '11' * 5, 12)], pcm_size=4))
        # Original RUG11 reports padded RIFF length, but a shorter data chunk.
        struct.pack_into('<I', source, 12, 48)
        self.verify(source, [0x1230, 0x1230])

    def test_uninitialized_delta_prefix_and_alignment(self):
        self.reject(container(1, [('000' + '1' * 256, 512)]))
        self.reject(container(1, [('001' + '0' * 11 + '1' * 255, 512)]))
        b = bytearray(container(1, [('000' + '0' * 11 + '1' * 255, 512)]))
        b[-1] |= 1
        self.reject(b)
        self.reject(container(2, [('11' * 2, 4)]))

    def test_every_truncation_and_oversized_output(self):
        for original in (container(1, [('000' + '0' * 11 + '1' * 255, 512)]),
                         container(2, [(literal(1) + '11', 4)])):
            for end in range(len(original)):
                self.reject(original[:end])
        b = bytearray(original)
        struct.pack_into('<I', b, 4, 0xffffffff)
        self.reject(b)
        with self.assertRaises(FormatError):
            decode(original, max_output=46)


if __name__ == '__main__':
    unittest.main()
