"""Static integrity checks for the unified RenoDX DLSS V5 artifact."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

from patch_unified_addon import (
    EXPECTED_SHA256,
    OVERLAY_HOOK_RVA,
    PeImage,
    UI_HOOK_RVA,
)


def rel32_target(image: PeImage, rva: int) -> int:
    offset = image.rva_to_offset(rva)
    displacement = struct.unpack_from("<i", image.data, offset + 1)[0]
    return rva + 5 + displacement


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--addon", type=Path, required=True)
    args = parser.parse_args()

    base_bytes = args.base.read_bytes()
    if hashlib.sha256(base_bytes).hexdigest() != EXPECTED_SHA256:
        raise SystemExit("base hash is not the supported updated official build")

    base = PeImage(bytearray(base_bytes))
    addon = PeImage(bytearray(args.addon.read_bytes()))
    if addon.section_count != base.section_count + 1:
        raise SystemExit("expected exactly one appended section")
    unified = addon.section(addon.section_count - 1)
    if unified[4] != b".unified" or unified[1] != 0x292000:
        raise SystemExit("unified section identity or RVA is invalid")

    allowed_changes = set(range(UI_HOOK_RVA, UI_HOOK_RVA + 7))
    allowed_changes.update(range(OVERLAY_HOOK_RVA, OVERLAY_HOOK_RVA + 5))
    actual_changes: set[int] = set()
    for index in range(base.section_count):
        b_vsize, b_rva, b_raw_size, b_raw, b_name = base.section(index)
        a_vsize, a_rva, a_raw_size, a_raw, a_name = addon.section(index)
        if (b_vsize, b_rva, b_raw_size, b_name) != (a_vsize, a_rva, a_raw_size, a_name):
            raise SystemExit(f"existing section metadata changed: {b_name!r}")
        for position in range(b_raw_size):
            if base.data[b_raw + position] != addon.data[a_raw + position]:
                actual_changes.add(b_rva + position)
    if not actual_changes or not actual_changes.issubset(allowed_changes):
        unexpected = sorted(actual_changes - allowed_changes)
        raise SystemExit(f"unexpected changes in original sections: {unexpected[:8]}")

    entry = struct.unpack_from("<I", addon.data, addon.optional + 16)[0]
    section_end = unified[1] + unified[0]
    if not unified[1] <= entry < section_end:
        raise SystemExit("entry point is outside the unified section")
    if addon.data[addon.rva_to_offset(UI_HOOK_RVA)] != 0xE8:
        raise SystemExit("settings hook is not a direct CALL")
    if addon.data[addon.rva_to_offset(OVERLAY_HOOK_RVA)] != 0xE9:
        raise SystemExit("overlay hook is not a direct JMP")
    for name, target in (
        ("entry", entry),
        ("settings", rel32_target(addon, UI_HOOK_RVA)),
        ("overlay", rel32_target(addon, OVERLAY_HOOK_RVA)),
    ):
        if not unified[1] <= target < section_end:
            raise SystemExit(f"{name} target is outside the unified section")

    # RenoDX still registers its original CFG-valid overlay callback address.
    registration = bytes(addon.data[addon.rva_to_offset(0x7F007) : addon.rva_to_offset(0x7F007) + 7])
    if registration != bytes.fromhex("48 8D 15 52 FC 02 00"):
        raise SystemExit("upstream overlay registration was unexpectedly changed")

    required_markers = (
        b"FrameGenerationPresentationPath\0",
        b"DLSSGEarlyLoadConfiguration\0",
        b"DLSSNRComputeScalingRatioCallback\0",
        b"DLSSNR.LocalStructureStrength\0",
        b"Neural Rendering Performance\0",
        b"Apply Resolution Scale\0",
        b"NR ON\0",
        b"NR OFF\0",
        b"PRESET 3\0",
    )
    output = bytes(addon.data)
    for marker in required_markers:
        if marker not in output:
            raise SystemExit(f"missing marker: {marker!r}")

    print("PASS: supported official base hash")
    print("PASS: one appended executable/read/write .unified section")
    print("PASS: original sections differ only at the two verified hook sites")
    print("PASS: entry, settings, and overlay targets resolve inside .unified")
    print("PASS: upstream frame-generation and native neural-rendering markers retained")
    print(f"SHA256 {hashlib.sha256(output).hexdigest()}  {args.addon.name}")


if __name__ == "__main__":
    main()
