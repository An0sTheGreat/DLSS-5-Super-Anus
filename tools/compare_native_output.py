"""Compare test-host RGBA16F GPU readbacks. This is not an image-quality score."""
import argparse
import math
import struct
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--expect", choices=("equal", "different"), required=True)
    args = parser.parse_args()
    left, right = args.reference.read_bytes(), args.candidate.read_bytes()
    assert len(left) >= 16 and len(right) >= 16, "Missing readback header"
    header = struct.unpack("<4I", left[:16])
    assert header == struct.unpack("<4I", right[:16]), "Readback shapes/formats differ"
    magic, width, height, fmt = header
    assert magic == 0x314F524E and fmt == 10, "Expected test-host RGBA16F capture"
    assert width and height and len(left) == len(right) == 16 + width * height * 8
    changed = 0
    total = maximum = 0.0
    for index, ((a,), (b,)) in enumerate(zip(struct.iter_unpack("<e", left[16:]),
                                           struct.iter_unpack("<e", right[16:]))):
        assert math.isfinite(a) and math.isfinite(b), f"Non-finite component {index}"
        if index % 4 == 3:
            continue  # alpha is not an NR image-quality comparison
        difference = abs(a - b)
        changed += a != b
        total += difference
        maximum = max(maximum, difference)
    components = width * height * 3
    print(f"{width}x{height} RGBA16F: all components finite; {changed}/{components} RGB components differ")
    print(f"RGB mean absolute difference={total / components:.9f}, maximum={maximum:.9f}")
    assert (changed == 0) == (args.expect == "equal"), f"Expected {args.expect} RGB outputs"
    print(f"PASS: expected {args.expect} output; no perceptual/performance claim")


if __name__ == "__main__":
    main()
