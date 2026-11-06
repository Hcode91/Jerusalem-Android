#!/usr/bin/env python3
"""Optional private-data differential check against two isolated original CPU routines.

Requires unicorn==2.1.4. Does not launch the game or implement any Windows APIs.
Only the hash-identified decoder instruction ranges and pointer-return stubs execute.
Never used by the Android app or required for conversion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from pe_probe import analyze
from wac_layout import inspect
from wac_decode import decode

EXE_HASH = 'b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687'


def reference(executable, data):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_BLOCK
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_EAX

    pe = analyze(executable)
    if pe['sha256'] != EXE_HASH:
        raise ValueError('reference executable hash does not match the analyzed release')
    info = inspect(data)
    if info['wrapper_output_size'] > 64 * 1024 * 1024:
        raise ValueError('reference output limit exceeded')
    mono = info['channels'] == 1
    entry, end, accessor = (0x4a3500, 0x4a35ef, 0x4a34c0) if mono else (0x41df50, 0x41e0e7, 0x41cec0)
    obj, payload, output, stack, stop = 0x1000000, 0x2000000, 0x3000000, 0x4000000, 0x5000000
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    code_page = entry & ~4095
    code_size = ((end + 4095) & ~4095) - code_page
    cpu.mem_map(code_page, code_size)
    for section in pe['sections']:
        rva = entry - pe['image_base'] - section['rva']
        if 0 <= rva and rva + end - entry <= section['raw_size']:
            offset = section['offset'] + rva
            cpu.mem_write(entry, executable[offset:offset + end - entry])
            break
    else:
        raise ValueError('decoder is not file backed')
    accessor_page = accessor & ~4095
    if not code_page <= accessor_page < code_page + code_size:
        cpu.mem_map(accessor_page, 4096)
    # A newly written MOV EAX, immediate; RET replaces only the file-buffer accessor.
    cpu.mem_write(accessor, b'\xb8' + struct.pack('<I', payload) + b'\xc3')
    cpu.mem_protect(code_page, code_size, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_protect(accessor_page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_map(stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_map(obj, 4096, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    largest_input = max(b['stored_size'] for b in info['blocks'])
    largest_output = max(b['decoded_size'] for b in info['blocks'])
    cpu.mem_map(payload, (largest_input + 4 + 4095) & ~4095, UC_PROT_READ | UC_PROT_WRITE)
    # Original stereo loop may consume alignment as an extra token; keep bounded slack.
    cpu.mem_map(output, (largest_output + 16 + 4095) & ~4095, UC_PROT_READ | UC_PROT_WRITE)

    def guard(machine, address, size, _):
        if not (entry <= address and address + size <= end or
                accessor <= address and address + size <= accessor + 6):
            raise ValueError(f'reference escaped permitted decoder code at {address:#x}')
    cpu.hook_add(UC_HOOK_BLOCK, guard)
    result, lengths = bytearray(data[8:52]), []
    struct.pack_into('<I', result, 4, 36 + info['pcm_data_size'])
    for block in info['blocks']:
        start = block['payload_offset']
        cpu.mem_write(payload, data[start:start + block['stored_size']] + bytes(4))
        args = [10, 0, block['stored_size'] * 8, output] if mono else [12, 6, 0, block['stored_size'] * 8, output]
        sp = stack + 32768
        cpu.mem_write(sp, struct.pack('<' + 'I' * (len(args) + 1), stop, *args))
        cpu.reg_write(UC_X86_REG_ESP, sp)
        cpu.reg_write(UC_X86_REG_ECX, obj)
        cpu.emu_start(entry, stop, timeout=10000000, count=100000000)
        if cpu.reg_read(UC_X86_REG_EIP) != stop:
            raise ValueError(f'reference instruction/time budget exceeded at {cpu.reg_read(UC_X86_REG_EIP):#x}')
        length = cpu.reg_read(UC_X86_REG_EAX)
        if not block['decoded_size'] <= length <= block['decoded_size'] + 8:
            raise ValueError('unexpected original decoder output length')
        lengths.append(length)
        result.extend(cpu.mem_read(output, block['decoded_size']))
    return bytes(result[:info['normalized_wave_size']]), lengths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('assets', type=Path, nargs='+')
    args = parser.parse_args()
    executable = args.executable.read_bytes()
    for asset in args.assets:
        data = asset.read_bytes()
        expected, lengths = reference(executable, data)
        actual = decode(data)
        if actual != expected:
            first = next(i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1])
            raise ValueError(f'{asset.name}: sample mismatch at WAVE byte {first}')
        print(json.dumps(dict(file=asset.name, match=True, wave_bytes=len(actual),
                              sha256=hashlib.sha256(actual).hexdigest(), original_block_bytes=lengths)))


if __name__ == '__main__':
    main()
