import importlib.util
import io
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('disc_extract', Path(__file__).parents[1] / 'tools/disc_extract.py')
disc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(disc)


def both(b, p, n, width=4):
    b[p:p + width] = n.to_bytes(width, 'little')
    b[p + width:p + width * 2] = n.to_bytes(width, 'big')


def record(name, lba, size, directory=False):
    b = bytearray(33 + len(name) + (1 if len(name) % 2 == 0 else 0))
    b[0] = len(b)
    both(b, 2, lba)
    both(b, 10, size)
    b[25] = 2 if directory else 0
    both(b, 28, 1, 2)
    b[32] = len(name)
    b[33:33 + len(name)] = name
    return b


def fixture(name=b'TEST.TXT;1'):
    sectors = [bytearray(2048) for _ in range(20)]
    pvd = sectors[16]
    pvd[:7] = b'\x01CD001\x01'
    both(pvd, 80, 20)
    both(pvd, 128, 2048, 2)
    root = record(b'\0', 18, 2048, True)
    pvd[156:156 + len(root)] = root
    file_record = record(name, 19, 5)
    sectors[18][:len(file_record)] = file_record
    sectors[19][:5] = b'hello'
    return b''.join(b'\0' + b'\xff' * 10 + b'\0\0\0\0\x01' + s + bytes(288) for s in sectors)


class DiscTests(unittest.TestCase):
    def test_listing_and_data(self):
        source = disc.Disc(io.BytesIO(fixture()))
        entries = list(source.entries())
        self.assertEqual(entries[0]['path'], 'TEST.TXT')
        self.assertEqual(b''.join(source.content(entries[0])), b'hello')

    def test_incomplete_raw_sector(self):
        with self.assertRaises(disc.DiscError):
            disc.Disc(io.BytesIO(fixture()[:-1]))

    def test_bad_mode(self):
        b = bytearray(fixture())
        b[16 * 2352 + 15] = 2
        with self.assertRaises(disc.DiscError):
            disc.Disc(io.BytesIO(b))

    def test_endian_mismatch(self):
        b = bytearray(fixture())
        b[16 * 2352 + 16 + 84] = 1
        with self.assertRaises(disc.DiscError):
            disc.Disc(io.BytesIO(b))

    def test_unsafe_identifier(self):
        source = disc.Disc(io.BytesIO(fixture(b'../BAD;1')))
        with self.assertRaises(disc.DiscError):
            list(source.entries())


if __name__ == '__main__':
    unittest.main()
