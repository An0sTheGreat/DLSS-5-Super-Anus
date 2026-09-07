"""Check nine controlled DX11/private NR sessions, not game longevity."""
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
    parser.add_argument("control", type=Path)
    parser.add_argument("churn", type=Path)
    parser.add_argument("--game-test", action="store_true")
    args = parser.parse_args()
    control, churn = args.control.resolve(), args.churn.resolve()
    root = Path(__file__).resolve().parent.parent / "build"
    require(control != churn, "Require distinct runs")
    for directory in (control, churn):
        require(directory.parent == root and directory.name.startswith("api-native-"),
                "Only workspace native fixtures accepted")
    for filename in ("ngxGym.exe", "d3d11.dll", "nvngx_dlss.dll", "nvngx_dlssnr.dll"):
        hashes = [hashlib.sha256((p / filename).read_bytes()).hexdigest() for p in (control, churn)]
        require(hashes[0] == hashes[1], f"Different {filename} builds")
    # Comparing to an uninterrupted SR history is not an equivalent input:
    # capture 180 is only 12 frames after the most recent native feature reset.
    require((control / "scenario.txt").read_bytes() == (churn / "scenario.txt").read_bytes(),
            "Control must use the same feature-reset schedule without device replacement")
    host_logs = [(p / "host.out").read_text(errors="replace") for p in (control, churn)]
    for host in host_logs:
        require("frames 216, evaluates 216, succeeded 216" in host, "Incomplete native coverage")
        require("captured frame 180 native output:" in host, "Missing GPU capture")
        require(re.search(r"(?m)^ok\r?$", host), "Host did not complete cleanup")
    require(host_logs[0].count("Init_with_ProjectID -> 0x00000001") == 1 and
            host_logs[0].count("native session Shutdown1 result -> 0x00000001") == 1 and
            host_logs[0].count("rebuild (recreate):") == 8 and
            "TEST ONLY: replacement" not in host_logs[0], "Invalid feature-reset-only control")
    host = host_logs[1]
    replacements = re.findall(r"TEST ONLY: replacement (\d+), .*distinct=1; old native session shut down\.", host)
    require(replacements == [str(i) for i in range(1, 9)], "Missing distinct source replacements")
    for marker, count in (
        ("Init_with_ProjectID -> 0x00000001", 9),
        ("native session Shutdown1 result -> 0x00000001", 9),
        ("native parameter destroy -> 0x00000001", 18),
        ("TEST ONLY: replaced host graphics references released.", 8),
    ):
        require(host.count(marker) == count, f"Incorrect native lifecycle count: {marker}")
    require("TEST ONLY: 0 retired source hosts retained;" in host, "Old host references retained")
    logs = [(p / "ReShade.log").read_text(errors="replace") for p in (control, churn)]
    private = logs[1]
    prefix = "DX11 game test: " if args.game_test else "TEST ONLY: "
    if args.game_test:
        require(re.search(r"NR INTEGRATED GAME TEST [12]:", private), "Wrong integrated build")
        require("TEST ONLY:" not in private, "Synthetic addon probe enabled")
    ordered = ["private NGX init (0x00000001)",
               "private NGX core shutdown result (0x00000001)",
               "private graphics/cache ownership released"]
    stages = re.findall("|".join(re.escape(s) for s in ordered), private)
    require(stages == ordered * 9, "Incorrect private init/shutdown/graphics-release ordering")
    for marker, count in (
        (prefix + "fresh private core initialization permitted", 8),
        ("shared NR binding closed (0x00000000)", 9),
        ("tracked consumer resources drained (0x00000000)", 9),
    ):
        require(private.count(marker) == count, f"Incorrect private lifecycle count: {marker}")
    require("shared-session teardown incomplete" not in private, "Incomplete private teardown")
    require(all("transport skipped" not in log for log in logs), "Skipped private delivery")
    pattern = r"session submitted=(\d+) gate=(\d+) evaluations=(\d+) scaled=(\d+)"
    expected = [(str(n), str(n), str(2*n-1), str(2*n-1)) for n in range(24, 217, 24)]
    require(re.findall(pattern, private) == expected, "Incomplete private boundary coverage")
    require(re.findall(pattern, logs[0]) == [expected[-1]], "Control coverage mismatch")
    subprocess.run([sys.executable, str(Path(__file__).with_name("compare_native_output.py")),
                    str(control / "gym-output.bin"), str(churn / "gym-output.bin"),
                    "--expect", "equal"], check=True)
    print("PASS: 8 distinct replacements, 9 ordered native/private closes, 216 frames/431 NR evaluations and exact frame-180 RGB. NOT game/configuration-churn/long-session acceptance.")


if __name__ == "__main__":
    main()
