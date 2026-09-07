"""Read-only string references, decoded within PE runtime-function bounds."""
import pathlib
import re
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / '.analysis-deps'))
import capstone
import pefile

path, pattern = sys.argv[1:3]
data = pathlib.Path(path).read_bytes()
pe = pefile.PE(data=data)
base = pe.OPTIONAL_HEADER.ImageBase
targets = {base + pe.get_rva_from_offset(m.start()): m.group().decode()
           for m in re.finditer(rb'[ -~]{4,}', data)
           if re.search(pattern, m.group().decode(), re.I)}
if pattern.startswith('@'):
    targets = {int(address, 16): address for address in pattern[1:].split(',')}
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
for entry in pe.DIRECTORY_ENTRY_EXCEPTION:
    start, end = entry.struct.BeginAddress, entry.struct.EndAddress
    for ins in md.disasm(pe.get_data(start, end-start), base+start):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_IMM and op.imm in targets:
                print(f'{base+start:#x}..{base+end:#x} ref={ins.address:#x} {ins.mnemonic} {ins.op_str} {targets[op.imm]}')
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target = ins.address + ins.size + op.mem.disp
                if target in targets:
                    print(f'{base+start:#x}..{base+end:#x} ref={ins.address:#x} {ins.mnemonic} {ins.op_str} {targets[target]}')
