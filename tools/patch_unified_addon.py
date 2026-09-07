"""Build a unified RenoDX addon by appending the linked control blob.

The source binary hash and every patched instruction are verified before write.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
from pathlib import Path


EXPECTED_SHA256 = "1d855cf226857dce890cffbf7206ba9b6497ce1d471b217c1c8b44b6cd5d27e9"
UI_HOOK_RVA = 0x0A82E3
UI_ORIGINAL = bytes.fromhex("49 8B 8F 78 03 00 00")
OVERLAY_HOOK_RVA = 0x0AEC60
OVERLAY_ORIGINAL = bytes.fromhex("55 41 57 41 56")
NEW_SECTION_NAME = b".unified"


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


class PeImage:
    def __init__(self, data: bytearray):
        self.data = data
        self.pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[self.pe : self.pe + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        self.coff = self.pe + 4
        self.section_count = struct.unpack_from("<H", data, self.coff + 2)[0]
        self.optional_size = struct.unpack_from("<H", data, self.coff + 16)[0]
        self.optional = self.coff + 20
        if struct.unpack_from("<H", data, self.optional)[0] != 0x20B:
            raise ValueError("expected PE32+ image")
        self.section_alignment = struct.unpack_from("<I", data, self.optional + 32)[0]
        self.file_alignment = struct.unpack_from("<I", data, self.optional + 36)[0]
        self.sections = self.optional + self.optional_size

    def section(self, index: int) -> tuple[int, int, int, int, bytes]:
        off = self.sections + index * 40
        name = bytes(self.data[off : off + 8]).rstrip(b"\0")
        vsize, rva, raw_size, raw = struct.unpack_from("<IIII", self.data, off + 8)
        return vsize, rva, raw_size, raw, name

    def rva_to_offset(self, rva: int) -> int:
        for i in range(self.section_count):
            vsize, section_rva, raw_size, raw, _ = self.section(i)
            if section_rva <= rva < section_rva + max(vsize, raw_size):
                return raw + rva - section_rva
        raise ValueError(f"RVA 0x{rva:X} is outside all sections")

    def patch(self, rva: int, expected: bytes, replacement: bytes) -> None:
        off = self.rva_to_offset(rva)
        actual = bytes(self.data[off : off + len(expected)])
        if actual != expected:
            raise ValueError(
                f"patch mismatch at RVA 0x{rva:X}: expected {expected.hex()}, got {actual.hex()}"
            )
        if len(replacement) != len(expected):
            raise ValueError("replacement length differs from expected instruction length")
        self.data[off : off + len(replacement)] = replacement

    def add_section(self, payload: bytes) -> tuple[int, int]:
        last = self.section(self.section_count - 1)
        new_rva = align(last[1] + max(last[0], last[2]), self.section_alignment)
        new_raw = align(len(self.data), self.file_alignment)
        new_raw_size = align(len(payload), self.file_alignment)
        header = self.sections + self.section_count * 40
        first_raw = min(self.section(i)[3] for i in range(self.section_count) if self.section(i)[3])
        if header + 40 > first_raw:
            raise ValueError("PE headers have no room for another section")
        if len(self.data) < new_raw:
            self.data.extend(b"\0" * (new_raw - len(self.data)))
        self.data.extend(payload)
        self.data.extend(b"\0" * (new_raw_size - len(payload)))
        section_header = struct.pack(
            "<8sIIIIIIHHI",
            NEW_SECTION_NAME,
            len(payload),
            new_rva,
            new_raw_size,
            new_raw,
            0,
            0,
            0,
            0,
            0xE0000060,  # code + initialized data + execute/read/write
        )
        self.data[header : header + 40] = section_header
        self.section_count += 1
        struct.pack_into("<H", self.data, self.coff + 2, self.section_count)
        struct.pack_into(
            "<I", self.data, self.optional + 4,
            struct.unpack_from("<I", self.data, self.optional + 4)[0] + new_raw_size,
        )
        struct.pack_into(
            "<I", self.data, self.optional + 8,
            struct.unpack_from("<I", self.data, self.optional + 8)[0] + new_raw_size,
        )
        struct.pack_into(
            "<I", self.data, self.optional + 56,
            align(new_rva + len(payload), self.section_alignment),
        )
        return new_rva, new_raw


def read_map_symbols(path: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    pattern = re.compile(r"^\s*[0-9A-Fa-f]+:([0-9A-Fa-f]{8,16})\s+(\S+)\s+([0-9A-Fa-f]{16})")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            symbols[match.group(2).lstrip("_")] = int(match.group(1), 16)
    return symbols


def extract_single_section(path: Path) -> tuple[bytes, int]:
    image = PeImage(bytearray(path.read_bytes()))
    candidates = []
    for i in range(image.section_count):
        section = image.section(i)
        if section[4] == b".text":
            candidates.append(section)
    if len(candidates) != 1:
        raise ValueError("linked blob does not contain exactly one .text section")
    vsize, rva, raw_size, raw, _ = candidates[0]
    payload = bytes(image.data[raw : raw + raw_size])
    if vsize > raw_size:
        payload += b"\0" * (vsize - raw_size)
    return payload[: max(vsize, raw_size)], rva


def rel32(source_rva: int, target_rva: int, instruction_size: int = 5) -> bytes:
    displacement = target_rva - (source_rva + instruction_size)
    if not -(1 << 31) <= displacement < (1 << 31):
        raise ValueError("hook target is outside rel32 range")
    return struct.pack("<i", displacement)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--blob", type=Path, required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.base.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"unsupported base SHA-256: {digest}")

    payload, _linked_rva = extract_single_section(args.blob)
    symbols = read_map_symbols(args.map_file)
    required = ("unified_entry", "settings_hook", "overlay_hook")
    missing = [name for name in required if name not in symbols]
    if missing:
        raise SystemExit(f"missing linked symbol(s): {', '.join(missing)}")

    image = PeImage(bytearray(source))
    new_rva, _ = image.add_section(payload)

    def injected_symbol(name: str) -> int:
        # MAP addresses are offsets within segment 0001 (the linked .text
        # section), while payload byte zero is the start of that section.
        return new_rva + symbols[name]

    settings_rva = injected_symbol("settings_hook")
    overlay_rva = injected_symbol("overlay_hook")
    entry_rva = injected_symbol("unified_entry")

    image.patch(UI_HOOK_RVA, UI_ORIGINAL, b"\xE8" + rel32(UI_HOOK_RVA, settings_rva) + b"\x90\x90")
    image.patch(OVERLAY_HOOK_RVA, OVERLAY_ORIGINAL, b"\xE9" + rel32(OVERLAY_HOOK_RVA, overlay_rva))
    struct.pack_into("<I", image.data, image.optional + 16, entry_rva)
    # Clear the stale image checksum. Windows and ReShade do not require it.
    struct.pack_into("<I", image.data, image.optional + 64, 0)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image.data)
    print(f"wrote {args.output}")
    print(f"section RVA=0x{new_rva:X}, entry=0x{entry_rva:X}, settings=0x{settings_rva:X}, overlay=0x{overlay_rva:X}")
    print(f"sha256={hashlib.sha256(image.data).hexdigest()}")


if __name__ == "__main__":
    main()
