#!/usr/bin/env python3
"""Optional private-data comparison of A01 pickup registrations and normal-pickup effects.

Runs only three bounded case bodies with capture stubs replacing every API.
It does not run Windows, inventory UI, game initialization or authentication.
"""
import argparse
import json
from pathlib import Path
import shlex
import struct
from validate_navigation_reference import capture

CASES = (0x44a74c, 0x44a7b1, 0x44a816)
APIS = {0x42c1d0: 1, 0x42c1a0: 2, 0x432440: 1, 0x42a9a0: 2, 0x429d70: 1, 0x42c3a0: 1}


def trace(read, start, value):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ESI, UC_X86_REG_EIP, UC_X86_REG_EAX
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    mapped = set()
    def code(address, data):
        for page in range(address & ~4095, (address+len(data)+4095) & ~4095, 4096):
            if page not in mapped:
                cpu.mem_map(page, 4096); mapped.add(page)
        cpu.mem_write(address, data)
    code(start, read(start, 79))
    for target, count in APIS.items():
        # The unique-history helper is cdecl; its caller removes that argument.
        code(target, b'\xc3' if target == 0x432440 else b'\xc2'+struct.pack('<H', count*4))
    stop, stack = 0x3000000, 0x1000000
    code(stop, b'\x90')
    for page in mapped:
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    sp = stack+32768
    cpu.mem_write(sp, struct.pack('<8I', 0, stop, *([0]*6)))
    cpu.reg_write(UC_X86_REG_ESP, sp); cpu.reg_write(UC_X86_REG_ESI, 0x2000000)
    calls = []
    def observe(machine, address, size, _):
        if address in APIS:
            pointer = machine.reg_read(UC_X86_REG_ESP)
            args = list(struct.unpack('<'+'I'*APIS[address], machine.mem_read(pointer+4, APIS[address]*4)))
            calls.append((address, args))
            if address == 0x42c1d0:
                machine.reg_write(UC_X86_REG_EAX, value)
        elif not start <= address or address+size > start+79:
            raise ValueError('pickup reference escaped its case body')
    cpu.hook_add(UC_HOOK_CODE, observe)
    cpu.emu_start(start, stop, count=1000, timeout=100000)
    if cpu.reg_read(UC_X86_REG_EIP) != stop:
        raise ValueError('pickup reference budget exceeded')
    return calls


def validate(executable, model):
    _, read = capture(executable)  # Checks the exact analyzed executable SHA-256.
    lines = model.splitlines()
    if not lines or lines[0] != 'JERUSALEM_A01_NAV_V3':
        raise ValueError('require a native V3 model extracted by jinspect')
    items = {}
    for line in lines[1:]:
        row = shlex.split(line)
        if row[0] == 'item':
            values = list(map(int, row[1:11]))
            items[values[0]] = dict(values=values, name=row[11], channels=[])
        elif row[0] == 'item-channel':
            items[int(row[1])]['channels'].append(list(map(int, row[2:4])))
    if len(items) != 3:
        raise ValueError('require all three supported original pickups')
    for start in CASES:
        sample = trace(read, start, 0)
        item_id, counter, marker = sample[-1][1][0], sample[0][1][0], sample[2][1][0]
        item = items[item_id]['values']
        if (item[8], item[9]) != (counter, marker):
            raise ValueError('native pickup counter/milestone differs')
        for value in range(256):
            expected = [(0x42c1d0, [counter]), (0x42c1a0, [counter, (value+1) & 255]),
                        (0x432440, [marker]), (0x42a9a0, [item_id, 0]),
                        (0x429d70, [item_id]), (0x42c3a0, [item_id])]
            if trace(read, start, value) != expected:
                raise ValueError(f'original pickup effects differ at {item_id}/{value}')
    records, _ = capture(executable, 0x47e493, 0x47e62b, {0x429700: 4, 0x429c20: 9, 0x42a000: 1, 0x42a270: 4})
    actual = {key: dict(channels=[]) for key in items}
    signed = lambda v: v if v < 0x80000000 else v-0x100000000
    for target, args in records:
        if args[0] not in actual:
            continue
        item = actual[args[0]]
        if target == 0x429700:
            item['name'] = read(args[1], 96).split(b'\0')[0].decode('ascii')
        elif target == 0x429c20:
            if args[6:] != [1, 60, 0]:
                raise ValueError('unsupported collectible hotspot flags')
            item['hit'] = [args[1], *map(signed, args[2:6]), args[7], args[8]]
        elif target == 0x42a270:
            if args[1] != 0:
                raise ValueError('unsupported collectible presentation')
            item['channels'].append(args[2:4])
    for key, native in items.items():
        original = actual[key]
        if original['name'] != native['name'] or original['hit'] != native['values'][1:8] or original['channels'] != native['channels']:
            raise ValueError(f'collectible registration mismatch at {key}')
    initial, _ = capture(executable, 0x44f06b, 0x44f095, {0x42a920: 2})
    if [args for _, args in initial] != [[key, 0] for key in items]:
        raise ValueError('initial presentation activation differs')
    variables, _ = capture(executable, 0x44f0ff, 0x44f145, {0x42c1a0: 2})
    initial_variables = dict(args for _, args in variables)
    if any(initial_variables.get(item['values'][8]) != 0 for item in items.values()):
        raise ValueError('fresh pickup counter is not established')
    return dict(items=3, static_bindings=sum(len(i['channels']) for i in items.values()), normal_pickup_cases=768,
                initial_visibility_matches=True, registrations_and_effects_match=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path); parser.add_argument('model', type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.executable.read_bytes(), args.model.read_text())))


if __name__ == '__main__':
    main()
