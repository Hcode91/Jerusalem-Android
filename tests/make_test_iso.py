#!/usr/bin/env python3
"""Builds small ISO 9660 test images (optionally with Joliet, optionally as raw
2352-byte Mode 1 / Mode 2 .bin) plus a manifest of what they contain.
Test-only helper for IsoImageTest; not used by the app.

usage: make_test_iso.py OUT_DIR
"""
import hashlib
import os
import struct
import sys

S = 2048


def both16(v): return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)


def dir_record(name_bytes, lba, size, is_dir, flags_extra=0):
    nlen = len(name_bytes)
    rlen = 33 + nlen + (1 if nlen % 2 == 0 else 0)
    rec = bytes([rlen, 0]) + both32(lba) + both32(size) + bytes(7) + \
        bytes([(0x02 if is_dir else 0) | flags_extra, 0, 0]) + both16(1) + bytes([nlen]) + name_bytes
    if nlen % 2 == 0:
        rec += b'\0'
    assert len(rec) == rlen
    return rec


def build_tree():
    """Returns {path: bytes} with nested dirs, mixed case and a long name."""
    files = {}
    files['Jerusalem.ex_'] = b'MZ' + os.urandom(5000)
    files['fl.ini'] = b'17\r\nCDPATH:\tCDROM\r\n'
    files['data/cd.ini'] = b'1\r\n'
    files['data/a01/sound/A01_FOOTSTEP_SAND01.wac'] = os.urandom(18716)
    files['data/a01/dia/eng/Adri_Dia002.dia'] = b' 0  Hmm...\r\n'
    files['data/empty.txt'] = b''
    files['data/big.bin'] = os.urandom(3 * S + 17)
    for i in range(70):  # forces a multi-sector directory
        files['data/many/File_With_A_Long_Name_%03d.dat' % i] = bytes([i]) * (i * 31)
    return files


def iso_name(name, is_dir):
    n = name.upper()
    if is_dir:
        return n.encode('ascii')
    return (n + (';1' if '.' in n else '.;1')).encode('ascii')


def build_iso(files, joliet):
    # directory tree
    dirs = {'': set()}
    for p in files:
        parts = p.split('/')
        for i in range(1, len(parts)):
            d = '/'.join(parts[:i])
            dirs.setdefault(d, set())
            dirs['/'.join(parts[:i - 1])].add((parts[i - 1], True))
        dirs['/'.join(parts[:-1])].add((parts[-1], False))

    order = sorted(dirs, key=lambda d: (d.count('/') if d else -1, d))
    trees = [('p', False)] + ([('j', True)] if joliet else [])

    def recs_for(d, j):
        out = []
        for name, is_dir in sorted(dirs[d]):
            nb = name.encode('utf-16-be') if j else iso_name(name, is_dir)
            out.append((name, is_dir, nb))
        return out

    def dir_size(d, j):
        size, off = 0, 0
        for rec_len in [34, 34] + [33 + len(nb) + (1 if len(nb) % 2 == 0 else 0) for _, _, nb in recs_for(d, j)]:
            if off + rec_len > S:
                size += S
                off = 0
            off += rec_len
        return size + S

    lba = 16 + 1 + len(trees) + 1  # PVD, SVD, terminator
    path_table_lba = lba
    lba += 1
    dir_lba = {}
    for t, j in trees:
        for d in order:
            dir_lba[(t, d)] = (lba, dir_size(d, j))
            lba += dir_size(d, j) // S
    file_lba = {}
    for p in sorted(files):
        file_lba[p] = lba
        lba += max(1, (len(files[p]) + S - 1) // S)
    total = lba

    img = bytearray(total * S)

    def put(sector, data):
        img[sector * S:sector * S + len(data)] = data

    for t, j in trees:
        for d in order:
            start, size = dir_lba[(t, d)]
            parent = '/'.join(d.split('/')[:-1]) if d else ''
            blob, cur = bytearray(), bytearray()
            entries = [dir_record(b'\0', start, size, True), dir_record(b'\1', *dir_lba[(t, parent)], True)]
            for name, is_dir, nb in recs_for(d, j):
                full = (d + '/' + name) if d else name
                if is_dir:
                    entries.append(dir_record(nb, *dir_lba[(t, full)], True))
                else:
                    entries.append(dir_record(nb, file_lba[full], len(files[full]), False))
            for e in entries:
                if len(cur) + len(e) > S:
                    blob += cur + bytes(S - len(cur))
                    cur = bytearray()
                cur += e
            blob += cur + bytes(S - len(cur))
            assert len(blob) <= size, (d, len(blob), size)
            put(start, bytes(blob))

    for p, data in files.items():
        put(file_lba[p], data)

    def vd(vtype, j):
        root_lba, root_size = dir_lba[('j' if j else 'p', '')]
        b = bytearray(S)
        b[0] = vtype
        b[1:6] = b'CD001'
        b[6] = 1
        b[40:72] = b'JERUSALEM'.ljust(32)
        b[80:88] = both32(total)
        if j:
            b[88:91] = b'%/E'
        b[120:124] = both16(1)
        b[124:128] = both16(1)
        b[128:132] = both16(S)
        b[132:140] = both32(10)
        b[140:144] = struct.pack('<I', path_table_lba)
        b[148:152] = struct.pack('>I', path_table_lba)
        b[156:190] = dir_record(b'\0', root_lba, root_size, True)
        b[881] = 1
        return bytes(b)

    put(16, vd(1, False))
    if joliet:
        put(17, vd(2, True))
    term = bytearray(S)
    term[0] = 255
    term[1:6] = b'CD001'
    term[6] = 1
    put(16 + len(trees), bytes(term))
    return bytes(img)


def to_raw(iso, mode):
    out = bytearray()
    sync = b'\x00' + b'\xff' * 10 + b'\x00'
    for i in range(len(iso) // S):
        data = iso[i * S:(i + 1) * S]
        if mode == 1:
            out += sync + b'\0\0\0\x01' + data + bytes(288)
        else:
            out += sync + b'\0\0\0\x02' + bytes(8) + data + bytes(280)
    return bytes(out)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    files = build_tree()
    with open(os.path.join(out, 'manifest.txt'), 'w') as m:
        for p in sorted(files):
            m.write('%s\t%d\t%s\n' % (p, len(files[p]), hashlib.sha1(files[p]).hexdigest()))
    joliet = build_iso(files, True)
    plain = build_iso(files, False)
    open(os.path.join(out, 'joliet.iso'), 'wb').write(joliet)
    open(os.path.join(out, 'plain.iso'), 'wb').write(plain)
    open(os.path.join(out, 'mode1.bin'), 'wb').write(to_raw(joliet, 1))
    open(os.path.join(out, 'mode2.bin'), 'wb').write(to_raw(joliet, 2))


if __name__ == '__main__':
    main()
