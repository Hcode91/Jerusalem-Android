#!/usr/bin/env python3
"""Bounded PE32 static analysis. Never loads or executes the inspected file."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


class PEError(ValueError):
    pass


def analyze(data):
    def take(offset, length):
        if offset < 0 or length < 0 or offset > len(data) or length > len(data) - offset:
            raise PEError(f"truncated PE data at {offset:#x}")
        return data[offset:offset + length]

    def u16(p):
        return struct.unpack('<H', take(p, 2))[0]

    def u32(p):
        return struct.unpack('<I', take(p, 4))[0]

    if take(0, 2) != b'MZ':
        raise PEError('not a DOS/PE image; compressed .ex_ must first be identified')
    pe = u32(0x3c)
    if take(pe, 4) != b'PE\0\0':
        raise PEError('invalid PE signature')
    machine, count = u16(pe + 4), u16(pe + 6)
    optional_size = u16(pe + 20)
    optional = pe + 24
    take(optional, optional_size)
    if optional_size < 96 or u16(optional) != 0x10b:
        raise PEError('only PE32 supported')
    if not 1 <= count <= 96:
        raise PEError('invalid section count')
    image_base, header_size = u32(optional + 28), u32(optional + 60)
    take(0, header_size)
    sections = []
    for i in range(count):
        p = optional + optional_size + i * 40
        raw = take(p, 40)
        virtual_size, rva, raw_size, offset = struct.unpack_from('<IIII', raw, 8)
        take(offset, raw_size)
        sections.append(dict(name=raw[:8].split(b'\0')[0].decode('ascii', 'replace'),
                             rva=rva, virtual_size=virtual_size, raw_size=raw_size, offset=offset))

    def mapped(rva):
        if rva < header_size:
            return rva, header_size - rva
        for section in sections:
            delta = rva - section['rva']
            if 0 <= delta < section['raw_size']:
                return section['offset'] + delta, section['raw_size'] - delta
        raise PEError(f'RVA is not file-backed: {rva:#x}')

    def at_rva(rva, length):
        offset, remaining = mapped(rva)
        if length > remaining:
            raise PEError('RVA spans section boundary')
        return take(offset, length)

    def word_rva(rva):
        return struct.unpack('<I', at_rva(rva, 4))[0]

    def cstring(rva):
        offset, remaining = mapped(rva)
        raw = take(offset, min(remaining, 4096))
        end = raw.find(b'\0')
        if end < 0:
            raise PEError('unterminated PE name')
        return raw[:end].decode('ascii', 'replace')

    directories = u32(optional + 92)
    if directories > (optional_size - 96) // 8:
        raise PEError('data directory count exceeds optional header')
    imports = []
    if directories > 1:
        import_rva, import_size = u32(optional + 104), u32(optional + 108)
        if import_rva:
            if import_size < 20 or import_size > 1024 * 1024:
                raise PEError('invalid import directory size')
            terminated = False
            for displacement in range(0, import_size - 19, 20):
                fields = struct.unpack('<IIIII', at_rva(import_rva + displacement, 20))
                if not any(fields):
                    terminated = True
                    break
                lookup, _, _, name_rva, iat = fields
                entries = []
                for index in range(65536):
                    value = word_rva((lookup or iat) + index * 4)
                    if not value:
                        break
                    entries.append(dict(iat_rva=iat + index * 4,
                                        name=None if value & 0x80000000 else cstring(value + 2),
                                        ordinal=value & 0xffff if value & 0x80000000 else None))
                else:
                    raise PEError('unterminated import thunk table')
                imports.append(dict(dll=cstring(name_rva), symbols=entries))
            if not terminated:
                raise PEError('unterminated import directory')
    strings = []
    pattern = re.compile(r'(?i)(aArt::|aDialog::|aImageFileCinema::|aSecCom|\.at3|\.aqc|\.ci2|\.was|\.wac|\.dan|\.dia|\.out|aApplication::LoadSave)')
    for match in re.finditer(rb'[\x20-\x7e]{6,}', data):
        text = match.group().decode('ascii')
        if not pattern.search(text):
            continue
        virtual_address = None
        for section in sections:
            if section['offset'] <= match.start() < section['offset'] + section['raw_size']:
                virtual_address = image_base + section['rva'] + match.start() - section['offset']
                break
        strings.append(dict(offset=match.start(), va=virtual_address, text=text))
    return dict(sha256=hashlib.sha256(data).hexdigest(), file_size=len(data), machine=machine,
                timestamp=u32(pe + 8), image_base=image_base, entry_rva=u32(optional + 16),
                sections=sections, imports=imports, evidence_strings=strings)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    args = parser.parse_args()
    try:
        if args.file.stat().st_size > 64 * 1024 * 1024:
            raise PEError('64 MiB inspection limit exceeded')
        print(json.dumps(analyze(args.file.read_bytes()), indent=2))
    except (OSError, PEError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
