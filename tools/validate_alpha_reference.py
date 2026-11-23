#!/usr/bin/env python3
"""Compare independent scalar alpha arithmetic with isolated original MMX routines.

No Windows initialization or APIs execute. The DLL is private user-supplied evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from pe_probe import analyze
HASH = '7defec2ee31f54e814e8277d6ba72ab1c0141bfd7ea47a822b790dc4c397fdd1'


def validate(dll):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
    if hashlib.sha256(dll).hexdigest() != HASH: raise ValueError('unsupported original mmxImage.dll')
    pe = analyze(dll)
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    for section in pe['sections']:
        if section['rva'] == 0x1000:
            length = min(section['raw_size'], 0x3000)
            cpu.mem_map(0x10001000, (length+4095)&~4095)
            cpu.mem_write(0x10001000, dll[section['offset']:section['offset']+length])
            cpu.mem_protect(0x10001000, (length+4095)&~4095, UC_PROT_READ | UC_PROT_EXEC)
            break
    src, dst, stack, stop = 0x2000000, 0x2100000, 0x2200000, 0x2300000
    for address in (src, dst, stack): cpu.mem_map(address, 16384, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_map(stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
    def call(address, args):
        sp = stack+8192
        cpu.mem_write(sp, struct.pack('<'+'I'*(len(args)+1), stop, *args));cpu.reg_write(UC_X86_REG_ESP, sp)
        cpu.emu_start(address, stop, count=1000000, timeout=1000000)
        if cpu.reg_read(UC_X86_REG_EIP) != stop: raise ValueError('alpha reference budget exceeded')
    total = 0
    for width in (1, 2, 255, 256, 257):
        height, stride = 3, width*4+12
        raw = bytearray(stride*height)
        background = bytearray(stride*height)
        expected = bytearray(background)
        for y in range(height):
            for x in range(width):
                p = y*stride+x*4;a = (x+y*17)%256
                raw[p:p+4] = bytes(((x*37+y*11)%256, (x*3+y*31)%256, (x*17+y*7)%256, a))
                background[p:p+4] = bytes(((x*13+101)%256, (x*19+55)%256, (x*29+199)%256, 0))
        expected[:] = background
        for y in range(height):
            for x in range(width):
                p = y*stride+x*4;a = raw[p+3]
                for c in range(3): expected[p+c] = (raw[p+c]*a//256)+(background[p+c]*(255-a)//256)
                expected[p+3] = 255-a
        cpu.mem_write(src, bytes(raw));cpu.mem_write(dst, bytes(background))
        call(0x10001270, (src, stride, width, height))
        call(0x10001070, (dst, stride, src, stride, width, height))
        if bytes(cpu.mem_read(dst, len(expected))) != expected: raise ValueError(f'alpha mismatch at width {width}')
        total += width*height
    return dict(sha256=HASH, pixel_cases=total, matches=True,
                rgb='floor(src*alpha/256) + floor(dst*(255-alpha)/256)',
                scope='PreAlpha 10001270 and CopyAlpha 10001070/10001d50; odd/even widths and padded strides')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__);parser.add_argument('dll', type=Path)
    args = parser.parse_args();print(json.dumps(validate(args.dll.read_bytes()), indent=2))
