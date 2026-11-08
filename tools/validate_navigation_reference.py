#!/usr/bin/env python3
"""Compare extracted A01 records with isolated original argument-push instructions.

Only the setup prefix executes; every game API call is replaced by a RET stub
that records arguments. No Windows APIs or game state/DRM routines execute.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from pe_probe import analyze
from reference_cpu import EXE_HASH

BEGIN, END = 0x47cea0, 0x47d8de
ARITIES = {0x428120: 1, 0x42ad80: 1, 0x42af00: 5, 0x42b530: 4,
           0x42b860: 10, 0x42b9b0: 10, 0x42bab0: 10, 0x42bc00: 7}


def capture(executable, begin=BEGIN, end=END, arities=ARITIES, cdecl=()):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI
    if hashlib.sha256(executable).hexdigest() != EXE_HASH:
        raise ValueError('unsupported original executable')
    pe = analyze(executable)
    def read(address, length):
        for section in pe['sections']:
            delta = address - pe['image_base'] - section['rva']
            if 0 <= delta and delta + length <= section['raw_size']:
                start = section['offset'] + delta
                return executable[start:start+length]
        raise ValueError('reference address is not file backed')
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    mapped = set()
    def map_code(address, data):
        for page in range(address & ~4095, (address+len(data)+4095) & ~4095, 4096):
            if page not in mapped:
                cpu.mem_map(page, 4096)
                mapped.add(page)
        cpu.mem_write(address, data)
    map_code(begin, read(begin, end-begin))
    for target, count in arities.items():
        map_code(target, b'\xc2' + struct.pack('<H', 0 if target in cdecl else count*4))
    for page in mapped:
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    stack = 0x1000000
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    # Dynamic setup reads only these zero-initialized load/volume preferences.
    # APIs remain capture stubs; there are no mapped OS services or file access.
    cpu.mem_map(0x2000000, 4096, UC_PROT_READ | UC_PROT_WRITE)
    cpu.reg_write(UC_X86_REG_ESP, stack+32768)
    cpu.reg_write(UC_X86_REG_ECX, 0x2000000)
    cpu.reg_write(UC_X86_REG_ESI, 0x2000000)
    records = []
    def observe(machine, address, size, _):
        if address in arities:
            count = arities[address]
            pointer = machine.reg_read(UC_X86_REG_ESP)
            records.append((address, list(struct.unpack('<'+'I'*count, machine.mem_read(pointer+4, count*4)))))
        elif not (begin <= address and address+size <= end):
            raise ValueError(f'setup capture escaped allowed code at {address:#x}')
    cpu.hook_add(UC_HOOK_CODE, observe)
    cpu.emu_start(begin, end, count=10000, timeout=1000000)
    if cpu.reg_read(UC_X86_REG_EIP) != end:
        raise ValueError('setup capture budget exceeded')
    return records, read


def validate_attenuation(executable, read):
    """Run only original gain/pan arithmetic plus its integer-conversion helper."""
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_FPCW
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    mapped = set()
    def code(address, data):
        for page in range(address & ~4095, (address+len(data)+4095) & ~4095, 4096):
            if page not in mapped:
                cpu.mem_map(page, 4096); mapped.add(page)
        cpu.mem_write(address, data)
    code(0x483cd0, read(0x483cd0, 0x67))
    code(0x490b40, read(0x490b40, 0x27))
    for address in (0x4a6618, 0x4be154, 0x4a67e8, 0x4a67e4, 0x4a67e0):
        code(address, read(address, 4))
    gain, pan, stop = 0x3000000, 0x3000010, 0x3000020
    code(gain, b'\xc2\x04\x00'); code(pan, b'\xc2\x04\x00');code(stop, b'\x90')
    for page in mapped:
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    obj, stack = 0x1000000, 0x2000000
    cpu.mem_map(obj, 4096, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_write(obj, struct.pack('<I', obj+0x200))
    cpu.mem_write(obj+0x204, struct.pack('<II', gain, pan))
    captured = []
    def guard(machine, address, size, _):
        if address in (gain, pan):
            sp = machine.reg_read(UC_X86_REG_ESP)
            captured.append((address, struct.unpack('<i', machine.mem_read(sp+4, 4))[0]))
        elif not (0x483cd0 <= address and address+size <= 0x483d37 or
                  0x490b40 <= address and address+size <= 0x490b67):
            raise ValueError('attenuation reference escaped arithmetic routines')
    cpu.hook_add(UC_HOOK_CODE, guard)
    results = {}
    percent = struct.unpack('<f', read(0x4a6618, 4))[0]
    for volume in range(101):
        for master in (0, 50, 100):
            cpu.mem_write(obj+0x111, struct.pack('<III', volume, master, 0))
            sp = stack+32768
            cpu.mem_write(sp, struct.pack('<I', stop))
            cpu.reg_write(UC_X86_REG_ESP, sp);cpu.reg_write(UC_X86_REG_ECX, obj)
            cpu.reg_write(UC_X86_REG_FPCW, 0x37f)
            captured.clear()
            cpu.emu_start(0x483cd0, stop, count=1000, timeout=100000)
            expected = -10000-int((master*percent)*(volume*percent)*-10000)
            if cpu.reg_read(UC_X86_REG_EIP) != stop or captured != [(gain, expected), (pan, 0)]:
                raise ValueError(f'attenuation mismatch at {volume}/{master}: {captured}')
            if master == 100 and volume in (0, 50, 55, 65, 70, 80, 100):
                results[volume] = expected
    return dict(cases=303, attenuation_hundredths_db=results)


def validate(executable, model):
    calls, read = capture(executable)
    nodes, exits = [], []
    real = lambda value: struct.unpack('<f', struct.pack('<I', value))[0]
    signed = lambda value: value if value < 0x80000000 else value-0x100000000
    for target, args in calls:
        if target == 0x42b530:
            name = read(args[1], 64).split(b'\0')[0].decode('ascii')
            nodes.append([args[0], name, args[3]])
        elif target in (0x42b860, 0x42bab0):
            slot = sum(e[0] == args[0] for e in exits)
            exits.append([args[0], slot, args[1], int(target == 0x42bab0), args[7],
                          *map(signed, args[3:7]), args[8], args[9]])
        elif target in (0x42b9b0, 0x42bc00):
            exit = next(e for e in exits if e[:2] == args[:2])
            camera = args[7:10] if target == 0x42b9b0 else args[2:5]
            exit.extend(map(real, camera))
    lines = model.splitlines()
    if lines[0] not in ('JERUSALEM_A01_NAV_V1', 'JERUSALEM_A01_NAV_V2', 'JERUSALEM_A01_NAV_V3', 'JERUSALEM_A01_NAV_V4', 'JERUSALEM_A01_NAV_V5'):
        raise ValueError('unknown native model')
    native_nodes, native_exits, native_ambient = [], [], []
    for line in lines[1:]:
        cells = line.split()
        if cells[0] == 'node':
            native_nodes.append([int(cells[1]), cells[2], int(cells[3])])
        elif cells[0] == 'exit':
            native_exits.append([*map(int, cells[1:12]), *map(float, cells[12:15])])
        elif cells[0] == 'ambient':
            native_ambient.append([*map(int, cells[1:4]), cells[4]])
        elif cells[0] in ('item', 'item-channel') and lines[0][-1] in '345':
            pass  # Full pickup evidence is checked by validate_pickups_reference.py.
        elif cells[0] == 'combine' and lines[0][-1] in '45':
            pass
        elif cells[0] in ('pickup-set', 'pickup-switch', 'presentation', 'target', 'use', 'dialogue') and lines[0].endswith('V5'):
            pass  # Separately checked by validate_story_reference.py.
        else:
            raise ValueError('unknown native row')
    if nodes != native_nodes or len(exits) != len(native_exits):
        raise ValueError('native node/exit registration differs from original argument capture')
    for original, native in zip(exits, native_exits, strict=True):
        if original[:11] != native[:11] or any(abs(a-b)>1e-8 for a, b in zip(original[11:], native[11:], strict=True)):
            raise ValueError(f'native exit differs: {original} vs {native}')
    ambient, sounds = [], {}
    for start in (0x47f1fe, 0x47f4f7, 0x47fa01):
        registrations, _ = capture(executable, start, start+19, {0x42c8d0: 6})
        _, args = registrations[0]
        sounds[args[0]] = read(args[2], 64).split(b'\0')[0].decode('ascii').upper().rsplit('.', 1)[0]
    for start, end in ((0x47f21f, 0x47f2c1), (0x47f518, 0x47f5ba), (0x47fa22, 0x47faa9)):
        records, _ = capture(executable, start, end, {0x42c040: 7})
        for _, args in records:
            if args[3:] != [0, 1, 1, 10]:
                raise ValueError('unsupported original ambient settings')
            ambient.append([args[0], args[1], args[2], sounds[args[1]]])
    if lines[0].endswith(('V2', 'V3')) and ambient != native_ambient:
        raise ValueError('native ambient registrations differ from original capture')
    return dict(captured_calls=len(calls), nodes=len(nodes), rotation_exits=sum(e[3]==0 for e in exits),
                puzzle_actions=sum(e[3]==1 for e in exits), ambient_bindings=len(native_ambient), original_arguments_match=True,
                volume_reference=validate_attenuation(executable, read))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('model', type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.executable.read_bytes(), args.model.read_text())))


if __name__ == '__main__':
    main()
