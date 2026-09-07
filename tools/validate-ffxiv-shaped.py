"""Compare old/new addon delivery on the synthetic EXE-SDK/_C/D24/DLAA host."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("fixed", type=Path)
    args = parser.parse_args()
    old, fixed = args.old.resolve(), args.fixed.resolve()
    root = Path(__file__).resolve().parent.parent / "build"
    require(old != fixed, "Require distinct fixtures")
    for p in (old, fixed):
        require(p.parent == root and p.name.startswith("api-native-"), "Only workspace fixtures")
        host = (p / "host.out").read_text(errors="replace")
        require("frames 216, evaluates 216, succeeded 216" in host and
                "captured frame 180 native output:" in host and re.search(r"(?m)^ok\r?$", host),
                "Incomplete native frames/capture/cleanup")
    for filename in ("ngxGym.exe", "scenario.txt", "d3d11.dll", "nvngx_dlss.dll", "nvngx_dlssnr.dll"):
        require(hashlib.sha256((old / filename).read_bytes()).digest() ==
                hashlib.sha256((fixed / filename).read_bytes()).digest(), f"Mismatched {filename}")
    previous = (old / "ReShade.log").read_text(errors="replace")
    log = (fixed / "ReShade.log").read_text(errors="replace")
    require("session submitted=0 gate=0 evaluations=0 scaled=0" in previous and
            "transport skipped (0x00000003)" in previous, "Old D24 rejection not reproduced")
    for marker in ("NR INTEGRATED GAME TEST 2:", "interception boundary: executable SDK",
                   "session submitted=216 gate=216 evaluations=431 scaled=431",
                   "DX11 game test: private NGX core shutdown result (0x00000001)",
                   "DX11 game test: private graphics/cache ownership released"):
        require(log.count(marker) == 1, f"Missing/duplicate marker: {marker}")
    require("transport skipped" not in log, "Private output was skipped")
    subprocess.run([sys.executable, str(Path(__file__).with_name("compare_native_output.py")),
                    str(old / "gym-output.bin"), str(fixed / "gym-output.bin"),
                    "--expect", "different"], check=True)
    print("PASS: old packed-depth rejection reproduced; fixed SDK/D24 path delivered 216 frames/431 NR calls with changed finite pixels and clean private teardown. NOT an FFXIV game test.")


if __name__ == "__main__":
    main()
