#!/usr/bin/env python3
"""Read ISO9660 files from the confirmed single-track MODE1/2352 layout.

No mounting, installer execution, CD authentication, or protection bypass.
"""
import argparse
import json
from pathlib import Path


class DiscError(ValueError):
    pass


class Disc:
    def __init__(self, source):
        self.file = source
        source.seek(0, 2)
        size = source.tell()
        if size % 2352:
            raise DiscError('incomplete raw sector')
        self.sectors = size // 2352
        pvd = self.sector(16)
        if pvd[:7] != b'\x01CD001\x01':
            raise DiscError('missing ISO9660 primary volume descriptor')
        if self.both(pvd, 128, 2) != 2048:
            raise DiscError('unsupported logical block size')
        self.volume_blocks = self.both(pvd, 80, 4)
        if self.volume_blocks > self.sectors or self.volume_blocks < 18:
            raise DiscError('invalid ISO volume size')
        self.root = self.record(pvd[156:156 + pvd[156]])

    @staticmethod
    def both(data, offset, width):
        if offset + width * 2 > len(data):
            raise DiscError('truncated dual-endian field')
        little = int.from_bytes(data[offset:offset + width], 'little')
        big = int.from_bytes(data[offset + width:offset + width * 2], 'big')
        if little != big:
            raise DiscError('inconsistent dual-endian field')
        return little

    def sector(self, lba):
        if not 0 <= lba < self.sectors:
            raise DiscError('sector out of range')
        self.file.seek(lba * 2352)
        raw = self.file.read(2352)
        if len(raw) != 2352 or raw[:12] != b'\0' + b'\xff' * 10 + b'\0' or raw[15] != 1:
            raise DiscError('invalid or unsupported MODE1 raw sector')
        # EDC/ECC correction is not implemented; user data is read verbatim.
        return raw[16:2064]

    def record(self, raw):
        if len(raw) < 34 or raw[0] != len(raw) or raw[32] > len(raw) - 33:
            raise DiscError('truncated ISO directory record')
        if raw[1] or raw[25] & 0x80 or raw[26] or raw[27]:
            raise DiscError('extended attributes, multi-extent, or interleaved files unsupported')
        lba, size = self.both(raw, 2, 4), self.both(raw, 10, 4)
        if self.both(raw, 28, 2) != 1:
            raise DiscError('multiple-volume file unsupported')
        if lba >= self.volume_blocks or size > (self.volume_blocks - lba) * 2048:
            raise DiscError('file extent outside ISO volume')
        return dict(lba=lba, size=size, directory=bool(raw[25] & 2), raw_name=raw[33:33 + raw[32]])

    def content(self, entry):
        remaining = entry['size']
        for lba in range(entry['lba'], entry['lba'] + (remaining + 2047) // 2048):
            data = self.sector(lba)[:min(remaining, 2048)]
            yield data
            remaining -= len(data)

    def entries(self):
        pending = [('', self.root, 0)]
        seen = set()
        count = 0
        while pending:
            parent, directory, depth = pending.pop()
            if depth > 32 or directory['lba'] in seen or directory['size'] > 8 * 1024 * 1024:
                raise DiscError('directory cycle, depth, or size limit')
            seen.add(directory['lba'])
            data = b''.join(self.content(directory))
            p = 0
            while p < len(data):
                length = data[p]
                if length == 0:
                    p = ((p // 2048) + 1) * 2048
                    continue
                if p + length > len(data) or (p % 2048) + length > 2048:
                    raise DiscError('directory record crosses sector boundary')
                entry = self.record(data[p:p + length])
                p += length
                if entry['raw_name'] in (b'\0', b'\1'):
                    continue
                try:
                    name = entry.pop('raw_name').decode('ascii').split(';')[0]
                except UnicodeError as error:
                    raise DiscError('non-ASCII ISO identifier unsupported') from error
                if name in ('', '.', '..') or any(c in name for c in '/\\:') or any(ord(c) < 32 for c in name):
                    raise DiscError('unsafe ISO identifier')
                entry['path'] = parent + name
                count += 1
                if count > 100000:
                    raise DiscError('directory entry limit')
                if entry['directory']:
                    pending.append((entry['path'] + '/', entry, depth + 1))
                yield entry


def extract(disc, entry, target):
    # Caller selects a new output path; ISO names are never filesystem paths.
    created = False
    try:
        with target.open('xb') as output:
            created = True
            for block in disc.content(entry):
                output.write(block)
    except Exception:
        if created:
            target.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['list', 'extract'])
    parser.add_argument('bin', type=Path)
    parser.add_argument('iso_path', nargs='?')
    parser.add_argument('output', nargs='?', type=Path)
    args = parser.parse_args()
    try:
        with args.bin.open('rb') as source:
            disc = Disc(source)
            entries = list(disc.entries())
            if args.mode == 'list':
                print(json.dumps(entries, indent=2))
            else:
                if not args.iso_path or not args.output:
                    raise DiscError('extract requires ISO_PATH and NEW_OUTPUT_FILE')
                matches = [e for e in entries if e['path'].casefold() == args.iso_path.casefold() and not e['directory']]
                if len(matches) != 1:
                    raise DiscError('file not found or ambiguous')
                extract(disc, matches[0], args.output)
    except (OSError, DiscError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
