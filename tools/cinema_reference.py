"""Optional private Cinema DLL decoder comparison. Executes only bounded decoder code.

The selected user-owned DLL supplies instructions; no DLL bytes are distributed.
This does not execute Windows APIs, file loading or game authentication.
"""
import hashlib
import struct
from pe_probe import analyze

DLL_HASH = '0d9759b42d0e7f47fe4ecc8d65f0388195dcf66c557d4619f306243ebe88f43e'


class CinemaReference:
    def __init__(self, dll, width, height):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_BLOCK
        if hashlib.sha256(dll).hexdigest() != DLL_HASH:
            raise ValueError('unsupported Cinema DLL identity')
        if not 0 < width <= 4096 or not 0 < height <= 4096:
            raise ValueError('reference geometry limit')
        self.cpu = cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        pe = analyze(dll)
        cpu.mem_map(0x10001000, 0x2000)
        for section in pe['sections']:
            start = 0x10001000-pe['image_base']-section['rva']
            if 0 <= start and start+0x2000 <= section['raw_size']:
                cpu.mem_write(0x10001000, dll[section['offset']+start:section['offset']+start+0x2000])
                break
        else:
            raise ValueError('decoder code not file backed')
        cpu.mem_protect(0x10001000, 0x2000, UC_PROT_READ | UC_PROT_EXEC)
        self.obj, self.file, self.source, self.pixels, self.indices, self.stack, self.stop = (
            0x1000000, 0x1100000, 0x2000000, 0x7000000, 0xb000000, 0xc000000, 0xd000000)
        self.size = width*height*4
        for address, size in ((self.obj, 0x55000), (self.file, 4096), (self.source, 64*1024*1024),
                              (self.pixels, self.size+64), (self.indices, width*height*2+64), (self.stack, 65536)):
            cpu.mem_map(address, (size+4095)&~4095, UC_PROT_READ | UC_PROT_WRITE)
        cpu.mem_map(self.stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
        cpu.mem_write(self.obj, struct.pack('<I', self.file))
        cpu.mem_write(self.file+0x98, struct.pack('<I', self.source))
        cpu.mem_write(self.obj+0x10008, struct.pack('<I', self.indices))
        def guard(machine, address, size, _):
            if not (0x10001490 <= address < address+size <= 0x100019d0 or
                    0x100019d0 <= address < address+size <= 0x100025b3):
                raise ValueError(f'reference escaped permitted decoder code at {address:#x}')
        cpu.hook_add(UC_HOOK_BLOCK, guard)

    def call(self, entry, args):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP
        sp = self.stack+32768
        self.cpu.mem_write(sp, struct.pack('<'+'I'*(len(args)+1), self.stop, *args))
        self.cpu.reg_write(UC_X86_REG_ESP, sp)
        self.cpu.reg_write(UC_X86_REG_ECX, self.obj)
        self.cpu.emu_start(entry, self.stop, timeout=20000000, count=100000000)
        if self.cpu.reg_read(UC_X86_REG_EIP) != self.stop:
            raise ValueError('Cinema reference instruction/time budget exceeded')

    def codebook(self, payload, d, n):
        if len(payload)+64 > 64*1024*1024 or d not in (2, 4) or not 0 < n < 4096:
            raise ValueError('reference codebook limit')
        self.cpu.mem_write(self.source, payload+bytes(64))
        self.call(0x10001490, [self.source, d*4, n])

    def frame(self, header, payload):
        if len(header) != 47 or len(payload)+64 > 64*1024*1024:
            raise ValueError('reference frame limit')
        _, offset, n, d = struct.unpack_from('<IIHH', header)
        if offset < len(payload):
            self.codebook(payload[offset:], d, n)
        self.cpu.mem_write(self.source, payload+bytes(64))
        self.cpu.mem_write(self.obj+0x54051, header)
        self.call(0x100019d0, [self.pixels])
        return bytes(self.cpu.mem_read(self.pixels, self.size))
