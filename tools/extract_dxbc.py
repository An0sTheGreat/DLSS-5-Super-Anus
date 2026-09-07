#!/usr/bin/env python3
"""Extract embedded DXBC/DXIL containers from a PE or arbitrary binary."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    data = args.input.read_bytes()
    args.output.mkdir(parents=True, exist_ok=True)

    found = 0
    cursor = 0
    while True:
        offset = data.find(b"DXBC", cursor)
        if offset < 0:
            break
        cursor = offset + 4
        if offset + 32 > len(data):
            continue

        # DXBC header: magic, 16-byte hash, version, total size, chunk count.
        total_size = struct.unpack_from("<I", data, offset + 24)[0]
        chunk_count = struct.unpack_from("<I", data, offset + 28)[0]
        if total_size < 32 or offset + total_size > len(data) or chunk_count > 128:
            continue

        blob = data[offset : offset + total_size]
        digest = hashlib.sha256(blob).hexdigest()[:16]
        target = args.output / f"shader_{found:02d}_off_{offset:08x}_{digest}.dxbc"
        target.write_bytes(blob)
        print(f"{found:02d} offset=0x{offset:08x} size={total_size:7d} chunks={chunk_count:2d} {target.name}")
        found += 1

    print(f"extracted={found}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
