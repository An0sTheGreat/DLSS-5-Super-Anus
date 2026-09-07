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
    parser.add_argument("target", type=lambda value: int(value, 0))
    parser.add_argument("--disassemble", action="store_true")
    args = parser.parse_args()

    data = args.binary.read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    disassembler.detail = True
    functions: set[tuple[int, int]] = set()

    for section in pe.sections:
        if not section.IMAGE_SCN_MEM_EXECUTE:
            continue
        blob = section.get_data()[: section.Misc_VirtualSize]
        section_va = base + section.VirtualAddress
        for insn in disassembler.disasm(blob, section_va):
            if insn.mnemonic != "call" or not insn.operands:
                continue
            operand = insn.operands[0]
            if operand.type != capstone.x86.X86_OP_IMM or operand.imm != args.target:
                continue
            fn = containing_runtime_function(pe, insn.address - base)
            print(f"call {insn.address:#x} from {fn}")
            if fn:
                functions.add(fn)

    if not args.disassemble:
        return
    for begin, end in sorted(functions):
        print(f"\nfunction {base + begin:#x}..{base + end:#x} ({end - begin:#x} bytes)")
        blob = pe.get_data(begin, end - begin)
        for insn in disassembler.disasm(blob, base + begin):
            print(f"{insn.address:016x}: {insn.mnemonic:8} {insn.op_str}")


if __name__ == "__main__":
    main()
