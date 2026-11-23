#!/usr/bin/env python3
"""Compare native spherical mapping with the isolated original mapping grid routine."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

from pe_probe import analyze
from reference_cpu import EXE_HASH


def reference(executable, yaw, pitch, fov, width, height, cols=9, rows=7):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_BLOCK
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_FPCW, UC_X86_REG_FPTAG
    pe = analyze(executable)
    if pe['sha256'] != EXE_HASH:
        raise ValueError('wrong executable identity')
    if not 2 <= cols <= 64 or not 2 <= rows <= 64:
        raise ValueError('original mapping grid limit exceeded')
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    ranges = [(0x404a90, 0x404f75), (0x404f80, 0x404fb6),
              (0x490b40, 0x490c40), (0x493bb8, 0x493c05)]
    pages = set()
    for start, end in ranges:
        for page in range(start & ~4095, (end + 4095) & ~4095, 4096):
            if page not in pages:
                cpu.mem_map(page, 4096)
                pages.add(page)
        for section in pe['sections']:
            delta = start - pe['image_base'] - section['rva']
            if 0 <= delta and delta + end - start <= section['raw_size']:
                offset = section['offset'] + delta
                cpu.mem_write(start, executable[offset:offset + end - start])
                break
        else:
            raise ValueError('unmapped reference routine')
    for page in pages:
        cpu.mem_protect(page, 4096, UC_PROT_READ | UC_PROT_EXEC)
    for section in pe['sections']:
        if section['name'] in ('.data', '.rdata'):
            start = pe['image_base'] + section['rva']
            cpu.mem_map(start, (max(section['virtual_size'], section['raw_size']) + 4095) & ~4095,
                        UC_PROT_READ | UC_PROT_WRITE)
            cpu.mem_write(start, executable[section['offset']:section['offset'] + section['raw_size']])
    cpu.mem_write(0x4d0508, struct.pack('<d', math.pi))
    # This flag selects CRT error handling; the finite-domain path requires no host APIs.
    cpu.mem_write(0x4d187c, bytes(4))
    obj, stack, stop = 0x2000000, 0x3000000, 0x4000000
    cpu.mem_map(obj, 0x9000, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_map(stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
    cpu.mem_map(stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
    cpu.mem_write(obj + 0x8028, struct.pack('<II', cols - 1, rows - 1))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    half = math.radians(fov) / 2
    sx = math.sin(half) * width / max(width, height)
    sy_screen = math.sin(half) * height / max(width, height)
    depth = math.cos(half)
    for index, (nx, ny) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        rx, ry = nx * sx, ny * sy_screen
        ray = (cy * rx - sy * sp * ry + sy * cp * depth,
               cp * ry + sp * depth,
               -sy * rx - cy * sp * ry + cy * cp * depth)
        cpu.mem_write(obj + 0x8030 + index * 12, struct.pack('<3f', *ray))
    cpu.mem_write(obj + 0x8084, struct.pack('<6f', 150, 0, 360, 180, 0, 856))
    sp_stack = stack + 32768
    cpu.mem_write(sp_stack, struct.pack('<I', stop))
    cpu.reg_write(UC_X86_REG_ESP, sp_stack)
    cpu.reg_write(UC_X86_REG_ECX, obj)
    cpu.reg_write(UC_X86_REG_FPCW, 0x27f)
    cpu.reg_write(UC_X86_REG_FPTAG, 0xffff)
    def guard(machine, address, size, _):
        if not any(start <= address and address + size <= end for start, end in ranges):
            raise ValueError(f'original mapper escaped permitted ranges at {address:#x}')
    cpu.hook_add(UC_HOOK_BLOCK, guard)
    cpu.emu_start(0x404a90, stop, timeout=10000000, count=10000000)
    if cpu.reg_read(UC_X86_REG_EIP) != stop:
        raise ValueError('reference mapper budget exceeded')
    result = []
    for y in range(rows):
        for x in range(cols):
            offset = (y * 64 + x) * 4
            u = struct.unpack('<i', cpu.mem_read(obj + 8 + offset, 4))[0] / 65536
            v = struct.unpack('<i', cpu.mem_read(obj + 0x4008 + offset, 4))[0] / 65536
            result.append((u, v))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('jinspect', type=Path)
    parser.add_argument('aqc', type=Path)
    args = parser.parse_args()
    executable = args.executable.read_bytes()
    maximum = 0
    cases = points = 0
    for width, height in ((640, 448), (640, 480), (480, 640)):
        for yaw, pitch, fov in ((0, 0, 85), (30, 20, 30), (90, -20, 87), (180, 0, 85),
                                (225, 25, 85), (359, -25, 60), (0, 90, 85), (720, -90, 5)):
            run = subprocess.run([str(args.jinspect.resolve()), 'map-panorama', str(args.aqc),
                                  str(yaw), str(pitch), str(fov), str(width), str(height), '9', '7'],
                                 check=True, capture_output=True, text=True)
            lines = run.stdout.splitlines()
            camera = list(map(float, lines[0].split(',')[1:]))
            native = [tuple(map(float, line.split(','))) for line in lines[1:]]
            original = reference(executable, *camera, width, height)
            for (u, v), (ru, rv) in zip(native, original, strict=True):
                du = abs(u - ru) % 2048
                error = max(min(du, 2048 - du), abs(v - rv))
                if error > 0.01:
                    raise ValueError(f'projection differs by {error} source pixels at camera {camera}: {(u,v)} vs {(ru,rv)}')
                maximum = max(maximum, error)
            cases += 1
            points += len(original)
    # Also validate the original 40x28-cell (41x29-vertex) grid dimensions.
    cols, rows = 41, 29
    run = subprocess.run([str(args.jinspect.resolve()), 'map-panorama', str(args.aqc),
                          '225', '0', '85', '640', '448', str(cols), str(rows)],
                         check=True, capture_output=True, text=True)
    native = [tuple(map(float, line.split(','))) for line in run.stdout.splitlines()[1:]]
    original = reference(executable, 225, 0, 85, 640, 448, cols, rows)
    for (u, v), (ru, rv) in zip(native, original, strict=True):
        du = abs(u-ru) % 2048
        error = max(min(du, 2048-du), abs(v-rv))
        if error > 0.01:
            raise ValueError(f'original grid mapping differs by {error} source pixels')
        maximum = max(maximum, error)
    cases += 1
    points += len(original)
    print(json.dumps(dict(cases=cases, points=points, max_source_pixel_error=maximum)))


if __name__ == '__main__':
    main()
