from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / ".analysis-deps"))

import capstone
import pefile


def containing_runtime_function(pe: pefile.PE, rva: int):
    for entry in getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", []):
        begin = entry.struct.BeginAddress
        end = entry.struct.EndAddress
        if begin <= rva < end:
            return begin, end
    return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("needle")
    parser.add_argument("--context-functions", type=int, default=0)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    needle = args.needle.encode("ascii") + b"\0"
    offsets: list[int] = []
    start = 0
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            break
        offsets.append(offset)
        start = offset + 1
    if not offsets:
        raise SystemExit(f"string not found: {args.needle!r}")

    targets = {base + pe.get_rva_from_offset(offset): offset for offset in offsets}
    print("targets:", ", ".join(f"{va:#x} (file {off:#x})" for va, off in targets.items()))

    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    disassembler.detail = True
    functions: set[tuple[int, int]] = set()
    xrefs: list[tuple[int, int]] = []
    for section in pe.sections:
        if not section.IMAGE_SCN_MEM_EXECUTE:
            continue
        blob = section.get_data()[: section.Misc_VirtualSize]
        section_va = base + section.VirtualAddress
        for insn in disassembler.disasm(blob, section_va):
            for operand in insn.operands:
                target = None
                if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                    target = insn.address + insn.size + operand.mem.disp
                elif operand.type == capstone.x86.X86_OP_IMM:
                    target = operand.imm
                if target in targets:
                    xrefs.append((insn.address, target))
                    fn = containing_runtime_function(pe, insn.address - base)
                    if fn:
                        functions.add(fn)

    for source, target in xrefs:
        print(f"xref {source:#x} -> {target:#x}")

    for begin, end in sorted(functions):
        print(f"\nfunction {base + begin:#x}..{base + end:#x} ({end - begin:#x} bytes)")
        blob = pe.get_data(begin, end - begin)
        for insn in disassembler.disasm(blob, base + begin):
            print(f"{insn.address:016x}: {insn.mnemonic:8} {insn.op_str}")


if __name__ == "__main__":
    main()
