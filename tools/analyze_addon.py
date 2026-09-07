from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / ".analysis-deps"))

import capstone
import pefile


def printable_at(data: bytes, offset: int) -> str | None:
    if offset < 0 or offset >= len(data):
        return None
    end = data.find(b"\0", offset, min(len(data), offset + 256))
    if end < 0:
        return None
    raw = data[offset:end]
    if len(raw) < 4 or any(byte < 0x20 or byte > 0x7E for byte in raw):
        return None
    return raw.decode("ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--section", default=".hotkey")
    parser.add_argument("--offset", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    pe = pefile.PE(data=data)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    section = next(
        item
        for item in pe.sections
        if item.Name.rstrip(b"\0").decode("ascii", "replace") == args.section
    )
    section_data = section.get_data()[args.offset : section.Misc_VirtualSize]
    section_va = image_base + section.VirtualAddress + args.offset

    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    disassembler.detail = True
    for instruction in disassembler.disasm(section_data, section_va):
        annotations: list[str] = []
        for operand in instruction.operands:
            if operand.type != capstone.x86.X86_OP_MEM:
                continue
            if operand.mem.base != capstone.x86.X86_REG_RIP:
                continue
            target_va = instruction.address + instruction.size + operand.mem.disp
            target_rva = target_va - image_base
            try:
                target_offset = pe.get_offset_from_rva(target_rva)
            except pefile.PEFormatError:
                continue
            text = printable_at(data, target_offset)
            if text:
                annotations.append(f'{target_va:#x}="{text}"')
            else:
                annotations.append(f"{target_va:#x}")
        suffix = f"  ; {', '.join(annotations)}" if annotations else ""
        print(
            f"{instruction.address:016x}: {instruction.mnemonic:8} "
            f"{instruction.op_str}{suffix}"
        )


if __name__ == "__main__":
    main()
