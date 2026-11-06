#!/usr/bin/env python3
"""Private A01 palm/offer/response comparison with isolated original handler dispatch."""
import argparse
import json
from pathlib import Path
import shlex
import struct
from validate_navigation_reference import capture

ARITIES = {0x42c5e0: 0, 0x42c600: 0, 0x42c620: 0, 0x42c1d0: 1, 0x42c1a0: 2,
           0x42c3a0: 1, 0x42c460: 1, 0x42a9a0: 2, 0x42a920: 2, 0x429d70: 1, 0x429d50: 1, 0x42ccd0: 2}


def trace(read, target, item, variables, using=False, event=0):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(0x44a000, 8192)
    cpu.mem_write(0x44a000, read(0x44a000, 8192))
    cpu.mem_protect(0x44a000, 8192, UC_PROT_READ | UC_PROT_EXEC)
    pages = set()
    for api, count in ARITIES.items():
        page = api & ~4095
        if page not in pages:
            cpu.mem_map(page, 4096); pages.add(page)
        cpu.mem_write(api, b'\xc2'+struct.pack('<H', count*4))
    for page in pages:
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    stop, stack = 0x3000000, 0x1000000
    cpu.mem_map(stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    sp = stack+32768
    cpu.mem_write(sp, struct.pack('<7I', stop, target, event, 0, 0, 0, 0))
    cpu.reg_write(UC_X86_REG_ESP, sp); cpu.reg_write(UC_X86_REG_ECX, 0x2000000)
    calls = []
    def observe(machine, address, size, _):
        if address in ARITIES:
            pointer = machine.reg_read(UC_X86_REG_ESP)
            count = ARITIES[address]
            args = list(struct.unpack('<'+'I'*count, machine.mem_read(pointer+4, count*4))) if count else []
            if address == 0x42c5e0: machine.reg_write(UC_X86_REG_EAX, int(using))
            elif address == 0x42c600: machine.reg_write(UC_X86_REG_EAX, item)
            elif address == 0x42c1d0: machine.reg_write(UC_X86_REG_EAX, variables.get(args[0], 0))
            elif address != 0x42c620: calls.append((address, args))
        elif not 0x44a620 <= address < address+size <= 0x44b5f8:
            raise ValueError(f'story reference escaped handler at {address:#x}')
    cpu.hook_add(UC_HOOK_CODE, observe)
    cpu.emu_start(0x44a620, stop, count=1000, timeout=100000)
    if cpu.reg_read(UC_X86_REG_EIP) != stop: raise ValueError('story reference budget exceeded')
    return calls


def validate(executable, model):
    _, read = capture(executable)
    rows = [shlex.split(line) for line in model.splitlines() if line]
    if rows[0] != ['JERUSALEM_A01_NAV_V5']: raise ValueError('require V5 model')
    palm, value = map(int, next(r for r in rows if r[0] == 'pickup-set')[1:])
    palm_row = next(r for r in rows if r[0] == 'item' and int(r[1]) == palm)
    variable = int(palm_row[9])
    switches = [list(map(int, r[1:])) for r in rows if r[0] == 'pickup-switch']
    offer = list(map(int, next(r for r in rows if r[0] == 'use' and r[-1] == '1')[1:]))
    _, target, item, phase, phase_value, enable, choice, obj, zero, nonzero, _, _ = offer
    for phase_value_input in range(256):
        expected = [(0x42c1a0, [variable, value]), (0x42c3a0, [palm]), (0x42a9a0, [palm, 0]), (0x429d70, [palm])]
        for _, key, compare, object_id, hide, show in switches:
            if phase_value_input == compare:
                expected += [(0x42a9a0, [object_id, hide]), (0x42a920, [object_id, show])]
        if trace(read, palm, 0, {phase: phase_value_input}) != expected: raise ValueError('original palm effect differs')
    for palm_value in range(256):
        expected = [(0x42c1a0, [phase, phase_value]), (0x42c460, [item]),
                    (0x42a920, [obj, zero if palm_value == 0 else nonzero]), (0x429d50, [enable])]
        if trace(read, target, item, {choice: palm_value}, True) != expected: raise ValueError('original matting offer differs')
    responses = [list(map(int, r[1:])) for r in rows if r[0] == 'use' and r[-1] == '0']
    for row in responses:
        _, target, item, *rest = row
        if trace(read, target, item, {}, True) != [(0x42ccd0, [row[-2], 1])]: raise ValueError('original spoken response differs')
        if trace(read, target, item, {}, True, 1): raise ValueError('response accepted unsupported event')
    return dict(palm_cases=256, offer_cases=256, response_cases=2*len(responses), matches=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path); parser.add_argument('model', type=Path)
    args = parser.parse_args(); print(json.dumps(validate(args.executable.read_bytes(), args.model.read_text()), indent=2))
