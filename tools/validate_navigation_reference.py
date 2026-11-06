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


def capture(executable):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP
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
    map_code(BEGIN, read(BEGIN, END-BEGIN))
    for target, count in ARITIES.items():
        map_code(target, b'\xc2' + struct.pack('<H', count*4))
    for page in mapped:
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    stack = 0x1000000
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    cpu.reg_write(UC_X86_REG_ESP, stack+32768)
    cpu.reg_write(UC_X86_REG_ECX, 0x2000000)
    records = []
    def observe(machine, address, size, _):
        if address in ARITIES:
            count = ARITIES[address]
            pointer = machine.reg_read(UC_X86_REG_ESP)
            records.append((address, list(struct.unpack('<'+'I'*count, machine.mem_read(pointer+4, count*4)))))
        elif not (BEGIN <= address and address+size <= END):
            raise ValueError(f'setup capture escaped allowed code at {address:#x}')
    cpu.hook_add(UC_HOOK_CODE, observe)
    cpu.emu_start(BEGIN, END, count=10000, timeout=1000000)
    if cpu.reg_read(UC_X86_REG_EIP) != END:
        raise ValueError('setup capture budget exceeded')
    return records, read


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
    if lines[0] != 'JERUSALEM_A01_NAV_V1':
        raise ValueError('unknown native model')
    native_nodes, native_exits = [], []
    for line in lines[1:]:
        cells = line.split()
        if cells[0] == 'node':
            native_nodes.append([int(cells[1]), cells[2], int(cells[3])])
        elif cells[0] == 'exit':
            native_exits.append([*map(int, cells[1:12]), *map(float, cells[12:15])])
        else:
            raise ValueError('unknown native row')
    if nodes != native_nodes or len(exits) != len(native_exits):
        raise ValueError('native node/exit registration differs from original argument capture')
    for original, native in zip(exits, native_exits, strict=True):
        if original[:11] != native[:11] or any(abs(a-b)>1e-8 for a, b in zip(original[11:], native[11:], strict=True)):
            raise ValueError(f'native exit differs: {original} vs {native}')
    return dict(captured_calls=len(calls), nodes=len(nodes), rotation_exits=sum(e[3]==0 for e in exits),
                puzzle_actions=sum(e[3]==1 for e in exits), original_arguments_match=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('model', type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.executable.read_bytes(), args.model.read_text())))


if __name__ == '__main__':
    main()
