"""Bounded private research execution of four hash-identified decoder routines only."""
import struct
from pe_probe import analyze

EXE_HASH = 'b4293e49167bc85a105fb8bef554972cc8c70406afa6da7096facf6640040687'
# Entry, exclusive end, buffer accessor, explicit integer arguments before output pointer.
ROUTINES = {
    'mono': (0x4a3500, 0x4a35ef, 0x4a34c0),
    'stereo': (0x41df50, 0x41e0e7, 0x41cec0),
    'indices': (0x41dad0, 0x41dc38, 0x411820),
    'palette': (0x41dc40, 0x41dda8, 0x411820),
}


class OriginalDecoder:
    def __init__(self, executable, kind, max_input, max_output):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_BLOCK
        if not 0 < max_input <= 64 * 1024 * 1024 or not 0 < max_output <= 64 * 1024 * 1024:
            raise ValueError('reference memory limit exceeded')
        pe = analyze(executable)
        if pe['sha256'] != EXE_HASH:
            raise ValueError('reference executable hash does not match analyzed release')
        self.entry, end, accessor = ROUTINES[kind]
        self.obj, self.payload, self.output, self.stack, self.stop = 0x1000000, 0x2000000, 0x7000000, 0xc000000, 0xd000000
        self.max_input, self.max_output = max_input, max_output
        self.cpu = cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        code_page = self.entry & ~4095
        code_size = ((end + 4095) & ~4095) - code_page
        cpu.mem_map(code_page, code_size)
        for section in pe['sections']:
            rva = self.entry - pe['image_base'] - section['rva']
            if 0 <= rva and rva + end - self.entry <= section['raw_size']:
                offset = section['offset'] + rva
                cpu.mem_write(self.entry, executable[offset:offset + end - self.entry])
                break
        else:
            raise ValueError('decoder is not file backed')
        accessor_page = accessor & ~4095
        if not code_page <= accessor_page < code_page + code_size:
            cpu.mem_map(accessor_page, 4096)
        # Newly written MOV EAX, immediate; RET substitutes only buffer access.
        cpu.mem_write(accessor, b'\xb8' + struct.pack('<I', self.payload) + b'\xc3')
        cpu.mem_protect(code_page, code_size, UC_PROT_READ | UC_PROT_EXEC)
        cpu.mem_protect(accessor_page, 4096, UC_PROT_READ | UC_PROT_EXEC)
        cpu.mem_map(self.stop, 4096, UC_PROT_READ | UC_PROT_EXEC)
        cpu.mem_map(self.obj, 4096, UC_PROT_READ | UC_PROT_WRITE)
        cpu.mem_map(self.stack, 65536, UC_PROT_READ | UC_PROT_WRITE)
        cpu.mem_map(self.payload, (max_input + 4 + 4095) & ~4095, UC_PROT_READ | UC_PROT_WRITE)
        cpu.mem_map(self.output, (max_output + 16 + 4095) & ~4095, UC_PROT_READ | UC_PROT_WRITE)

        def guard(machine, address, size, _):
            if not (self.entry <= address and address + size <= end or
                    accessor <= address and address + size <= accessor + 6):
                raise ValueError(f'reference escaped permitted decoder code at {address:#x}')
        cpu.hook_add(UC_HOOK_BLOCK, guard)

    def call(self, payload, args, output_size):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_EAX
        if len(payload) > self.max_input or output_size > self.max_output:
            raise ValueError('reference invocation exceeds allocation')
        self.cpu.mem_write(self.payload, payload + bytes(4))
        sp = self.stack + 32768
        values = [self.stop, *args, self.output]
        self.cpu.mem_write(sp, struct.pack('<' + 'I' * len(values), *values))
        self.cpu.reg_write(UC_X86_REG_ESP, sp)
        self.cpu.reg_write(UC_X86_REG_ECX, self.obj)
        self.cpu.emu_start(self.entry, self.stop, timeout=10000000, count=100000000)
        if self.cpu.reg_read(UC_X86_REG_EIP) != self.stop:
            raise ValueError('reference instruction/time budget exceeded')
        length = self.cpu.reg_read(UC_X86_REG_EAX)
        if not output_size <= length <= output_size + 8:
            raise ValueError(f'unexpected original output length: {length}, expected {output_size}..{output_size+8}')
        return bytes(self.cpu.mem_read(self.output, output_size)), length
