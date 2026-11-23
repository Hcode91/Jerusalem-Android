#!/usr/bin/env python3
"""Optional private comparison of A01 inventory dispatch with independent native rules.

Runs only the bounded original handler with BagRem/BagAdd capture stubs. No
Windows APIs, initialization, movie playback or authentication code executes.
Original instructions are read from the user's private executable, never bundled.
"""
import argparse
import json
from pathlib import Path
import shlex
import struct
from validate_navigation_reference import capture


def trace(read, first, second):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(0x44b000, 4096)
    cpu.mem_write(0x44b000, read(0x44b000, 4096))  # Includes the switch tables.
    cpu.mem_map(0x42c000, 4096)
    for api in (0x42c460, 0x42c3a0):
        cpu.mem_write(api, b'\xc2\x04\x00')
    for page in (0x44b000, 0x42c000):
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    stack, stop = 0x1000000, 0x3000000
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_map(stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
    sp = stack+32768
    cpu.mem_write(sp, struct.pack('<III', stop, first, second))
    cpu.reg_write(UC_X86_REG_ESP, sp)
    cpu.reg_write(UC_X86_REG_ECX, 0x2000000)
    calls = []
    def observe(machine, address, size, _):
        if address in (0x42c460, 0x42c3a0):
            pointer = machine.reg_read(UC_X86_REG_ESP)
            calls.append((address, struct.unpack('<I', machine.mem_read(pointer+4, 4))[0]))
        elif not 0x44b680 <= address < address+size <= 0x44b70c:
            raise ValueError('inventory reference escaped bounded handler')
    cpu.hook_add(UC_HOOK_CODE, observe)
    cpu.emu_start(0x44b680, stop, count=128, timeout=100000)
    if cpu.reg_read(UC_X86_REG_EIP) != stop:
        raise ValueError('inventory reference instruction budget exceeded')
    return calls


def validate(executable, model):
    _, read = capture(executable)  # Checks exact supported SHA-256.
    rows = [shlex.split(line) for line in model.splitlines() if line]
    if not rows or rows[0] not in (['JERUSALEM_A01_NAV_V4'], ['JERUSALEM_A01_NAV_V5']):
        raise ValueError('require a native V4/V5 model extracted by jinspect')
    rules = [row for row in rows if row[0] == 'combine']
    if len(rules) != 1:
        raise ValueError('require exactly the supported consuming A01 combination')
    first, second, result = map(int, rules[0][1:4])
    expected = [(0x42c460, first), (0x42c460, second), (0x42c3a0, result)]
    items = [int(row[1]) for row in rows if row[0] == 'item']
    domain = [0, 0xffffffff, *items, result]
    checks = 0
    for a in domain:
        for b in domain:
            effects = expected if (a, b) in ((first, second), (second, first)) else []
            if trace(read, a, b) != effects:
                raise ValueError(f'original combination differs for {a}/{b}')
            checks += 1
    declarations, _ = capture(executable, 0x47e5af, 0x47e5c7, {0x429700: 4})
    if len(declarations) != 1 or declarations[0][1][0] != result:
        raise ValueError('combination result declaration differs')
    label = read(declarations[0][1][1], 96).split(b'\0')[0].decode('ascii')
    if label != rules[0][4]:
        raise ValueError('combination result label differs')
    return dict(pairs=checks, first=first, second=second, result=result, label=label,
                scope='consuming harness/stone rule only; no bowl/jug or character interactions')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('model', type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.executable.read_bytes(), args.model.read_text()), indent=2))
