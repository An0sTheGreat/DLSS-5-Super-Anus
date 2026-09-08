"""Create the V6.4 single-file addon from the verified official RenoDX image.

The embedded component is linked as a small PE so normal x64 code generation and
imports remain available. Its sections are grafted at one uniform RVA delta,
then imports, relocations and unwind entries are merged into the official image.
Every code patch is byte-verified before the output is written.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
from pathlib import Path


EXPECTED_SHA256 = "1d855cf226857dce890cffbf7206ba9b6497ce1d471b217c1c8b44b6cd5d27e9"
EMBEDDED_FIRST_RVA = 0x1000
NEW_SECTION_NAME = b".nr-v64"
ADDON_NAME_POINTER_RVA = 0x2193D0
ORIGINAL_ADDON_NAME = b"RenoDX DLSS\0"
DISPLAY_ADDON_NAME = b"RenoDX DLSS S_A\0"

PATCHES = {
    0x09C815: (bytes.fromhex("E8 26 57 01 00"), "native_evaluation_gate", "call"),
    0x05C281: (bytes.fromhex("E8 FA 15 00 00"), "scaled_evaluate_create", "call"),
    0x05C3D2: (bytes.fromhex("E8 A9 14 00 00"), "scaled_evaluate_existing", "call"),
    0x0A85DF: (bytes.fromhex("49 83 BC 24 B8 00 00 00 10"), "settings_bridge", "call9"),
    0x0AC0D0: (bytes.fromhex("55 41 57 41 56"), "init_device_bridge", "jmp"),
    0x0AC6B0: (bytes.fromhex("55 41 57 41 56"), "destroy_device_bridge", "jmp"),
    0x0AAC50: (bytes.fromhex("E9 FB 0A 02 00"), "init_command_list_bridge", "jmp"),
    0x0AB890: (bytes.fromhex("56 57 53 48 83 EC 50"), "destroy_command_list_bridge", "jmp7"),
    0x0AB9B0: (bytes.fromhex("55 41 57 41 56"), "destroy_resource_bridge", "jmp"),
}
CAPTURE_PATCHES = {
    # Validated native SR descriptor already exists. Preserve its final store,
    # replace only the source==Upscaled predicate, and retain the original gate.
    0x09C776: (bytes.fromhex("0F 11 85 68 01 00 00"), "auto_native_source_bridge", "call7"),
    # Both registrations of the native FG observer; no function-entry detour.
    0x07CCCD: (bytes.fromhex("48 8D 35 DC D7 01 00"), "observed_framegen_callback", "lea7"),
    0x07CCFE: (bytes.fromhex("48 8D 15 AB D7 01 00"), "observed_framegen_callback", "lea7"),
    # Observe every remaining direct call to B1F40. Its body and return decisions
    # are unchanged; the legacy native counter still observes only 09C815.
    0x09E2A1: (bytes.fromhex("E8 9A 3C 01 00"), "observed_evaluation_gate", "call"),
    0x09F8DD: (bytes.fromhex("E8 5E 26 01 00"), "observed_evaluation_gate", "call"),
    0x0B008D: (bytes.fromhex("E8 AE 1E 00 00"), "observed_evaluation_gate", "call"),
    0x0C9D37: (bytes.fromhex("E8 04 82 FE FF"), "observed_evaluation_gate", "call"),
    0x09C8CE: (bytes.fromhex("E8 DD AA FB FF"), "embedded_capture_parent", "call"),
    0x09E348: (bytes.fromhex("E8 63 90 FB FF"), "embedded_capture_parent", "call"),
    0x0B0364: (bytes.fromhex("E8 47 70 FA FF"), "embedded_capture_parent", "call"),
    0x0B8C06: (bytes.fromhex("E8 A5 E7 F9 FF"), "embedded_capture_parent", "call"),
    0x0C205E: (bytes.fromhex("E8 4D 53 F9 FF"), "embedded_capture_parent", "call"),
    0x0C9DBA: (bytes.fromhex("E8 F1 D5 F8 FF"), "embedded_capture_parent", "call"),
    0x0F4142: (bytes.fromhex("E8 69 32 F6 FF"), "embedded_capture_parent", "call"),
    0x057DE0: (bytes.fromhex("E8 DB 3D 00 00"), "embedded_capture_api_codec", "call"),
    0x0588A2: (bytes.fromhex("E8 19 33 00 00"), "embedded_capture_api_codec", "call"),
    0x09B130: (bytes.fromhex("E8 1B 41 05 00"), "embedded_capture_codec", "call"),
    0x09B618: (bytes.fromhex("E8 33 3C 05 00"), "embedded_capture_codec", "call"),
}


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


class PeImage:
    def __init__(self, data: bytearray):
        self.data = data
        self.pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[self.pe:self.pe + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        self.coff = self.pe + 4
        self.section_count = struct.unpack_from("<H", data, self.coff + 2)[0]
        self.optional_size = struct.unpack_from("<H", data, self.coff + 16)[0]
        self.optional = self.coff + 20
        if struct.unpack_from("<H", data, self.optional)[0] != 0x20B:
            raise ValueError("expected PE32+ image")
        self.image_base = struct.unpack_from("<Q", data, self.optional + 24)[0]
        self.section_alignment = struct.unpack_from("<I", data, self.optional + 32)[0]
        self.file_alignment = struct.unpack_from("<I", data, self.optional + 36)[0]
        self.sections_offset = self.optional + self.optional_size

    def section(self, index: int) -> tuple[int, int, int, int, bytes, int]:
        off = self.sections_offset + index * 40
        name = bytes(self.data[off:off + 8]).rstrip(b"\0")
        vsize, rva, raw_size, raw = struct.unpack_from("<IIII", self.data, off + 8)
        characteristics = struct.unpack_from("<I", self.data, off + 36)[0]
        return vsize, rva, raw_size, raw, name, characteristics

    def rva_to_offset(self, rva: int) -> int:
        if rva < struct.unpack_from("<I", self.data, self.optional + 60)[0]:
            return rva
        for index in range(self.section_count):
            vsize, section_rva, raw_size, raw, _, _ = self.section(index)
            if section_rva <= rva < section_rva + max(vsize, raw_size):
                return raw + rva - section_rva
        raise ValueError(f"RVA 0x{rva:X} is outside all sections")

    def directory(self, index: int) -> tuple[int, int]:
        return struct.unpack_from("<II", self.data, self.optional + 112 + index * 8)

    def set_directory(self, index: int, rva: int, size: int) -> None:
        struct.pack_into("<II", self.data, self.optional + 112 + index * 8, rva, size)

    def patch(self, rva: int, expected: bytes, replacement: bytes) -> None:
        if len(expected) != len(replacement):
            raise ValueError("patch length mismatch")
        offset = self.rva_to_offset(rva)
        actual = bytes(self.data[offset:offset + len(expected)])
        if actual != expected:
            raise ValueError(
                f"patch mismatch at RVA 0x{rva:X}: expected {expected.hex()}, got {actual.hex()}")
        self.data[offset:offset + len(replacement)] = replacement

    def next_section_rva(self) -> int:
        last = self.section(self.section_count - 1)
        return align(last[1] + max(last[0], last[2]), self.section_alignment)

    def add_section(self, payload: bytes, expected_rva: int) -> None:
        new_rva = self.next_section_rva()
        if new_rva != expected_rva:
            raise ValueError("calculated section RVA changed")
        new_raw = align(len(self.data), self.file_alignment)
        new_raw_size = align(len(payload), self.file_alignment)
        header = self.sections_offset + self.section_count * 40
        first_raw = min(self.section(i)[3] for i in range(self.section_count) if self.section(i)[3])
        if header + 40 > first_raw:
            raise ValueError("PE headers have no room for the V6 section")
        if len(self.data) < new_raw:
            self.data.extend(b"\0" * (new_raw - len(self.data)))
        self.data.extend(payload)
        self.data.extend(b"\0" * (new_raw_size - len(payload)))
        self.data[header:header + 40] = struct.pack(
            "<8sIIIIIIHHI", NEW_SECTION_NAME, len(payload), new_rva,
            new_raw_size, new_raw, 0, 0, 0, 0, 0xE0000060)
        self.section_count += 1
        struct.pack_into("<H", self.data, self.coff + 2, self.section_count)
        struct.pack_into("<I", self.data, self.optional + 4,
            struct.unpack_from("<I", self.data, self.optional + 4)[0] + new_raw_size)
        struct.pack_into("<I", self.data, self.optional + 8,
            struct.unpack_from("<I", self.data, self.optional + 8)[0] + new_raw_size)
        struct.pack_into("<I", self.data, self.optional + 56,
            align(new_rva + len(payload), self.section_alignment))


def read_map_symbols(path: Path, image_base: int) -> dict[str, int]:
    symbols: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+(\S+)\s+([0-9A-Fa-f]{16})")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            symbols[match.group(1).lstrip("_")] = int(match.group(2), 16) - image_base
    return symbols


def iter_import_descriptors(image: PeImage):
    rva, _ = image.directory(1)
    if rva == 0:
        return
    offset = image.rva_to_offset(rva)
    while True:
        fields = struct.unpack_from("<IIIII", image.data, offset)
        if fields == (0, 0, 0, 0, 0):
            break
        yield fields
        offset += 20


def relocation_blocks(image: PeImage):
    rva, size = image.directory(5)
    if rva == 0 or size == 0:
        return []
    cursor = image.rva_to_offset(rva)
    end = cursor + size
    blocks = []
    while cursor + 8 <= end:
        page, block_size = struct.unpack_from("<II", image.data, cursor)
        if page == 0 or block_size < 8 or cursor + block_size > end:
            break
        count = (block_size - 8) // 2
        entries = list(struct.unpack_from(f"<{count}H", image.data, cursor + 8))
        blocks.append((page, entries))
        cursor += block_size
    return blocks


def encode_relocations(blocks) -> bytes:
    output = bytearray()
    for page, entries in blocks:
        values = list(entries)
        if len(values) & 1:
            values.append(0)
        output.extend(struct.pack("<II", page, 8 + len(values) * 2))
        output.extend(struct.pack(f"<{len(values)}H", *values))
    return bytes(output)


def rel32(source_rva: int, target_rva: int) -> bytes:
    value = target_rva - (source_rva + 5)
    if not -(1 << 31) <= value < (1 << 31):
        raise ValueError("relative hook target is out of range")
    return struct.pack("<i", value)


def append_blob(payload: bytearray, new_rva: int, blob: bytes, alignment: int = 8) -> int:
    offset = align(len(payload), alignment)
    payload.extend(b"\0" * (offset - len(payload)))
    payload.extend(blob)
    return new_rva + offset


def main() -> None:
    global NEW_SECTION_NAME
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--embedded", type=Path, required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--screenshot-capture", action="store_true")
    parser.add_argument("--section-name", default=".nr-v64", choices=(".nr-v64", ".nr-v65", ".nr-v66", ".nr-dx11"))
    args = parser.parse_args()
    NEW_SECTION_NAME = args.section_name.encode("ascii")

    source = args.base.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"unsupported official base SHA-256: {digest}")

    base = PeImage(bytearray(source))
    embedded = PeImage(bytearray(args.embedded.read_bytes()))
    if embedded.image_base != base.image_base:
        raise SystemExit("embedded and official preferred image bases differ")
    new_rva = base.next_section_rva()
    shift = new_rva - EMBEDDED_FIRST_RVA

    mapped_end = max(
        section[1] + max(section[0], section[2])
        for section in (embedded.section(i) for i in range(embedded.section_count)))
    payload = bytearray(mapped_end - EMBEDDED_FIRST_RVA)
    for index in range(embedded.section_count):
        vsize, rva, raw_size, raw, _, _ = embedded.section(index)
        destination = rva - EMBEDDED_FIRST_RVA
        payload[destination:destination + raw_size] = embedded.data[raw:raw + raw_size]

    # Keep the official config/preset identifier untouched. Only redirect the
    # exported NAME pointer to a display string in the injected section.
    original_name_va = struct.unpack_from(
        "<Q", base.data, base.rva_to_offset(ADDON_NAME_POINTER_RVA))[0]
    original_name_rva = original_name_va - base.image_base
    original_name_offset = base.rva_to_offset(original_name_rva)
    if bytes(base.data[original_name_offset:original_name_offset + len(ORIGINAL_ADDON_NAME)]) != ORIGINAL_ADDON_NAME:
        raise ValueError("official addon NAME string changed")
    if not any(page + (entry & 0xFFF) == ADDON_NAME_POINTER_RVA and entry >> 12 == 10
               for page, entries in relocation_blocks(base) for entry in entries):
        raise ValueError("official addon NAME pointer is not covered by a DIR64 relocation")
    display_name_rva = append_blob(payload, new_rva, DISPLAY_ADDON_NAME, 1)

    # Fix every absolute VA in the embedded component and retain its relocation
    # records at the uniformly shifted pages for normal ASLR processing.
    embedded_relocs = relocation_blocks(embedded)
    for page, entries in embedded_relocs:
        for entry in entries:
            kind, within_page = entry >> 12, entry & 0xFFF
            target_rva = page + within_page
            target_offset = target_rva - EMBEDDED_FIRST_RVA
            if kind == 10:
                value = struct.unpack_from("<Q", payload, target_offset)[0]
                struct.pack_into("<Q", payload, target_offset, value + shift)
            elif kind not in (0,):
                raise ValueError(f"unsupported embedded relocation type {kind}")

    # Point both import lookup and address thunks at the shifted name records.
    adjusted_imports = []
    patched_thunks: set[int] = set()
    for original_first, stamp, chain, name, first in iter_import_descriptors(embedded):
        for thunk_rva in {original_first, first}:
            if thunk_rva == 0 or thunk_rva in patched_thunks:
                continue
            patched_thunks.add(thunk_rva)
            cursor = thunk_rva - EMBEDDED_FIRST_RVA
            while True:
                value = struct.unpack_from("<Q", payload, cursor)[0]
                if value == 0:
                    break
                if value & (1 << 63) == 0:
                    struct.pack_into("<Q", payload, cursor, value + shift)
                cursor += 8
        adjusted_imports.append((
            original_first + shift if original_first else 0,
            stamp, chain, name + shift, first + shift))

    base_imports = list(iter_import_descriptors(base))
    import_blob = bytearray()
    for descriptor in base_imports + adjusted_imports:
        import_blob.extend(struct.pack("<IIIII", *descriptor))
    import_blob.extend(b"\0" * 20)
    imports_rva = append_blob(payload, new_rva, import_blob, 8)

    all_relocs = relocation_blocks(base)
    all_relocs.extend((page + shift, entries) for page, entries in embedded_relocs)
    reloc_blob = encode_relocations(all_relocs)
    relocs_rva = append_blob(payload, new_rva, reloc_blob, 8)

    base_exception_rva, base_exception_size = base.directory(3)
    embedded_exception_rva, embedded_exception_size = embedded.directory(3)
    embedded_exception_offset = embedded.rva_to_offset(embedded_exception_rva)

    # x64 unwind records store handler/chained-function RVAs outside the .pdata
    # table. Shift those embedded RVAs too so stack unwinding remains valid.
    patched_unwind_records: set[int] = set()
    for offset in range(0, embedded_exception_size, 12):
        _, _, unwind = struct.unpack_from("<III", embedded.data, embedded_exception_offset + offset)
        unwind &= ~3
        if unwind == 0 or unwind in patched_unwind_records:
            continue
        patched_unwind_records.add(unwind)
        unwind_offset = unwind - EMBEDDED_FIRST_RVA
        version_flags = payload[unwind_offset]
        flags = version_flags >> 3
        code_count = payload[unwind_offset + 2]
        trailer = unwind_offset + 4 + align(code_count * 2, 4)
        if flags & 4:  # UNW_FLAG_CHAININFO
            for field_offset in (0, 4, 8):
                value = struct.unpack_from("<I", payload, trailer + field_offset)[0]
                struct.pack_into("<I", payload, trailer + field_offset, value + shift)
        elif flags & 3:  # UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER
            value = struct.unpack_from("<I", payload, trailer)[0]
            struct.pack_into("<I", payload, trailer, value + shift)

    exception_blob = bytearray(base.data[
        base.rva_to_offset(base_exception_rva):
        base.rva_to_offset(base_exception_rva) + base_exception_size])
    for offset in range(0, embedded_exception_size, 12):
        begin, end, unwind = struct.unpack_from("<III", embedded.data, embedded_exception_offset + offset)
        exception_blob.extend(struct.pack("<III", begin + shift, end + shift, unwind + shift))
    exceptions_rva = append_blob(payload, new_rva, exception_blob, 4)

    base.add_section(payload, new_rva)
    base.set_directory(1, imports_rva, len(import_blob))
    base.set_directory(3, exceptions_rva, len(exception_blob))
    base.set_directory(5, relocs_rva, len(reloc_blob))
    base.patch(ADDON_NAME_POINTER_RVA, struct.pack("<Q", original_name_va),
               struct.pack("<Q", base.image_base + display_name_rva))

    symbols = read_map_symbols(args.map_file, embedded.image_base)
    patches = PATCHES | CAPTURE_PATCHES if args.screenshot_capture else PATCHES
    required = {name for _, name, _ in patches.values()} | {"combined_entry"}
    missing = sorted(required - symbols.keys())
    if missing:
        raise SystemExit(f"missing embedded symbol(s): {', '.join(missing)}")

    def injected(name: str) -> int:
        return symbols[name] + shift

    for site, (expected, symbol, kind) in patches.items():
        opcode = b"\xE8" if kind.startswith("call") else b"\xE9"
        replacement = opcode + rel32(site, injected(symbol))
        if kind == "lea7":
            replacement = expected[:3] + struct.pack("<i", injected(symbol) - (site + 7))
        replacement += b"\x90" * (len(expected) - len(replacement))
        base.patch(site, expected, replacement)

    struct.pack_into("<I", base.data, base.optional + 16, injected("combined_entry"))
    struct.pack_into("<I", base.data, base.optional + 64, 0)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(base.data)
    print(f"wrote {args.output}")
    print(f"embedded shift=0x{shift:X}, section RVA=0x{new_rva:X}")
    print(f"imports=0x{imports_rva:X}, relocations=0x{relocs_rva:X}, exceptions=0x{exceptions_rva:X}")
    print(f"sha256={hashlib.sha256(base.data).hexdigest()}")


if __name__ == "__main__":
    main()
