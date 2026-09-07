"""Validate controlled native Vulkan NR pixels; NOT game compatibility."""
import argparse
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("enabled", type=Path)
    parser.add_argument("zero", type=Path)
    parser.add_argument("repeat", type=Path)
    args = parser.parse_args()
    fixtures = [p.resolve() for p in (args.enabled, args.zero, args.repeat)]
    require(len(set(fixtures)) == 3, "Require three distinct runs")
    root = Path(__file__).resolve().parent.parent / "build"
    for directory, intensity in zip(fixtures, (1, 0, 1)):
        require(directory.parent == root and directory.name.startswith("api-vulkan-"),
                "Only workspace Vulkan fixtures accepted")
        log = (directory / "probe.out").read_text(errors="replace")
        require(not (directory / "probe.err").read_text().strip(), "Probe stderr is not empty")
        stages = ["Vulkan NGX init", "direct Vulkan NR init", "Vulkan feature create",
                  "direct Vulkan NR evaluate", "release feature", "direct Vulkan NR shutdown",
                  "destroy parameters", "Vulkan NGX shutdown"]
        cursor = 0
        for stage in stages:
            marker = stage + ": NGX 0x00000001"
            require(log.count(marker) == 1, f"Missing/duplicate successful {stage}")
            offset = log.find(marker, cursor)
            require(offset >= cursor, f"Incorrect lifecycle order: {stage}")
            cursor = offset + len(marker)
        require("Vulkan GPU readback: nonfinite=0 sentinel-pixels=0" in log,
                "GPU output was invalid or unwritten")
        require(f"Direct NR image checkpoint: intensity={intensity};" in log,
                "Incorrect intensity control")
    for filename in ("vulkan-ngx-probe.exe", "nvngx_dlss.dll", "nvngx_dlssnr.dll"):
        hashes = {hashlib.sha256((p / filename).read_bytes()).hexdigest() for p in fixtures}
        require(len(hashes) == 1, f"Different {filename} builds")
        print(f"{filename}: {next(iter(hashes))}")
    data = (fixtures[1] / "vulkan-output.bin").read_bytes()
    require(len(data) == 16 + 1920 * 1080 * 8, "Unexpected readback size")
    require(struct.unpack_from("<4I", data) == (0x314F524E, 1920, 1080, 10),
            "Unexpected RGBA16F header")
    # Exact uploaded half-float bit pattern from vulkan_image_fixture.hpp.
    for index, actual in enumerate(struct.iter_unpack("<4H", data[16:])):
        y, x = divmod(index, 1920)
        checker = ((x // 32 + y // 32) & 1) * 0x400
        expected = (0x3000 + x * 2048 // 1920 + checker,
                    0x3000 + y * 2048 // 1080, 0x3400 + checker, 0x3C00)
        require(actual == expected, f"Zero-intensity output differs from uploaded input at {x},{y}")
    print("PASS: zero intensity exactly preserves all uploaded RGBA half-float bits")
    for directory in fixtures:
        raw = (directory / "vulkan-output.bin").read_bytes()
        require(len(raw) == len(data) and raw[:16] == data[:16], "Mismatched capture dimensions")
        require(all(math.isfinite(v[0]) for v in struct.iter_unpack("<e", raw[16:])),
                "Nonfinite GPU component")
    require((fixtures[0] / "vulkan-output.bin").read_bytes() ==
            (fixtures[2] / "vulkan-output.bin").read_bytes(), "Repeated NR RGBA output differs")
    subprocess.run([sys.executable, str(Path(__file__).with_name("compare_native_output.py")),
                    str(fixtures[1] / "vulkan-output.bin"),
                    str(fixtures[0] / "vulkan-output.bin"), "--expect", "different"], check=True)
    print("PASS: actual native Vulkan NR output and exact repeat. One synthetic frame per run; NOT game/lifecycle/performance acceptance.")


if __name__ == "__main__":
    main()
