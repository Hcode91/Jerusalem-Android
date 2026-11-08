#!/usr/bin/env python3
"""Optional private A01 handler comparison; bounded CPU execution, all engine APIs stubbed.

Only hash-identified input/combination/dialogue callbacks execute. No Windows APIs,
filesystem calls, initialization, CD authentication or original code enters the port.
"""
import argparse
import json
from pathlib import Path
import struct
from validate_navigation_reference import capture

ARITIES = {0x42c5e0:0,0x42c600:0,0x42c620:0,0x42c1d0:1,0x42c1a0:2,
           0x42c3a0:1,0x42c460:1,0x42c4f0:0,0x42a9a0:2,0x42a920:2,
           0x42ab20:1,0x42aaa0:1,0x429d70:1,0x429d50:1,0x429d90:3,
           0x429db0:3,0x42ccd0:2,0x4282a0:2,0x4292c0:3,0x48a5a0:5,
           0x48a610:2,0x432440:1,0x4284d0:5,0x427f60:2,0x424650:1,
           0x42c6e0:0,0x42c670:2,0x42cdd0:1,0x42c550:1,0x49097b:0,
           0x4293e0:3,0x42bfb0:2}
RANGES = {0x44a620:0x44b5f8,0x44b680:0x44b714,0x44c1c0:0x44c820,0x44c8c0:0x44c940}


def trace(read, entry, args, using=False, item=0, random=0):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP
    if entry not in RANGES: raise ValueError('unsupported reference entry')
    cpu=Uc(UC_ARCH_X86,UC_MODE_32);cpu.mem_map(0x44a000,0x3000)
    cpu.mem_write(0x44a000,read(0x44a000,0x3000));cpu.mem_protect(0x44a000,0x3000,UC_PROT_READ|UC_PROT_EXEC)
    pages=set()
    for api,count in ARITIES.items():
        page=api&~4095
        if page not in pages: cpu.mem_map(page,4096);pages.add(page)
        cpu.mem_write(api,b'\xc2'+struct.pack('<H',0 if api in (0x432440,0x49097b) else count*4))
    for page in pages:cpu.mem_protect(page,4096,UC_PROT_READ|UC_PROT_EXEC)
    stop,stack,obj=0x3000000,0x1000000,0x2000000
    cpu.mem_map(stop,4096,UC_PROT_READ|UC_PROT_EXEC);cpu.mem_map(stack,65536,UC_PROT_READ|UC_PROT_WRITE);cpu.mem_map(obj,4096,UC_PROT_READ|UC_PROT_WRITE)
    sp=stack+32768;cpu.mem_write(sp,struct.pack('<9I',stop,*(list(args)+[0]*(8-len(args)))))
    cpu.reg_write(UC_X86_REG_ESP,sp);cpu.reg_write(UC_X86_REG_ECX,obj)
    calls=[]
    def observe(machine,address,size,_):
        if address in ARITIES:
            count=ARITIES[address];pointer=machine.reg_read(UC_X86_REG_ESP)
            a=list(struct.unpack('<'+'I'*count,machine.mem_read(pointer+4,count*4))) if count else []
            if address==0x42c5e0:machine.reg_write(UC_X86_REG_EAX,int(using))
            elif address==0x42c600:machine.reg_write(UC_X86_REG_EAX,item)
            elif address==0x49097b:machine.reg_write(UC_X86_REG_EAX,random)
            elif address==0x42c1d0:machine.reg_write(UC_X86_REG_EAX,0)
            elif address!=0x42c620:calls.append((address,a))
        elif not entry<=address<address+size<=RANGES[entry]:raise ValueError(f'handler escaped at {address:#x}')
    cpu.hook_add(UC_HOOK_CODE,observe)
    try:cpu.emu_start(entry,stop,count=3000,timeout=100000)
    except Exception as error:raise ValueError(f'entry={entry:#x} args={args} ip={cpu.reg_read(UC_X86_REG_EIP):#x}') from error
    if cpu.reg_read(UC_X86_REG_EIP)!=stop:raise ValueError('reference instruction/time budget exceeded')
    return calls


def validate(executable):
    _,read=capture(executable)
    records={};cases=0
    # Exhaust all seven target/card pairs at each possible random branch.
    for target in range(10023,10030):
        for item in range(10023,10030):
            for random in (0,16384,32767):
                calls=trace(read,0x44a620,[target,0],True,item,random)
                removed=[a[0] for api,a in calls if api==0x42c460]
                if removed!=([target] if target==item else []):raise ValueError('card consumption differs')
                records[f'card:{target}:{item}:{random}']=calls;cases+=1
    # Camp pickups, card-table open/back, final water gift, topic speech starts.
    for target,event,item in ((10018,0,0),(10018,0,10020),(10020,0,0),(10021,0,0),(10021,1,0),(10021,2,10019)):
        records[f'camp:{target}:{event}:{item}']=trace(read,0x44a620,[target,event],bool(item),item);cases+=1
    for pair in ((10018,10020),(10020,10018)):
        calls=trace(read,0x44b680,pair)
        if [(api,a) for api,a in calls if api in (0x42c460,0x42c3a0)]!=[(0x42c460,[10018]),(0x42c3a0,[10019])]:raise ValueError('bowl combination differs')
        records[f'combine:{pair[0]}:{pair[1]}']=calls;cases+=1
    for dialogue in range(11017,11043):
        records[f'finish:{dialogue}']=trace(read,0x44c1c0,[dialogue]);cases+=1
    for dialogue in (11018,11022):
        records[f'start:{dialogue}']=trace(read,0x44c8c0,[dialogue]);cases+=1
    # The last A01 speech invokes area-2 entry 3, rather than another A01 puzzle.
    if (0x427f60,[2,3]) not in records['finish:11039']:raise ValueError('A01 ending transfer differs')
    return dict(cases=cases,matches=True,records=records)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=validate(args.executable.read_bytes())
    if args.output:args.output.write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v for k,v in result.items() if k!='records'},indent=2))
