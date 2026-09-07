#!/usr/bin/env python3
"""Convert a binary blob to a small C++ byte-array header."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("symbol")
    args = parser.parse_args()

    data = args.input.read_bytes()
    rows = []
    for offset in range(0, len(data), 12):
        values = ", ".join(f"0x{value:02X}" for value in data[offset : offset + 12])
        rows.append(f"    {values},")

    text = (
        "#pragma once\n\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        f"inline constexpr std::uint8_t {args.symbol}[] = {{\n"
        + "\n".join(rows)
        + "\n};\n"
        f"inline constexpr std::size_t {args.symbol}_size = sizeof({args.symbol});\n"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
