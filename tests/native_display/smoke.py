#!/usr/bin/env python3
"""Verify real macOS virtual-display creation, Qt enumeration/scaling, and cleanup.

Opt-in: temporarily changes the active desktop's display topology. This is a
fixture smoke check, not yet the video fullscreen/focus regression suite.
"""
import argparse
import json
import pathlib
import selectors
import shlex
import subprocess
import sys
import tempfile
import time

NAME = "OwlSwitch virtual display test"


def run_check(evidence_path):
    source = pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="owlswitch-virtual-display-") as directory:
        build = pathlib.Path(directory)
        subprocess.run([
            "clang++", "-fobjc-arc", "-framework", "AppKit", "-framework", "CoreGraphics",
            str(source / "VirtualDisplayProbe.mm"), "-o", str(build / "virtual-display"),
        ], check=True, timeout=60)
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "Qt6Gui"], text=True, timeout=10))
        subprocess.run([
            "clang++", "-std=c++17", str(source / "QtDisplayProbe.cpp"),
            "-o", str(build / "qt-displays"), *flags,
        ], check=True, timeout=60)

        def screens():
            return json.loads(subprocess.check_output(
                [str(build / "qt-displays")], text=True, timeout=10))

        def canonical(items):
            return sorted(json.dumps(item, sort_keys=True) for item in items)

        baseline = screens()
        if not baseline or any(item["name"] == NAME for item in baseline):
            raise RuntimeError("An active desktop and no concurrent display fixture are required")
        results = {"baseline": baseline, "cases": []}
        for width, height, scale in [(1920, 1080, 1), (1280, 720, 2)]:
            process = subprocess.Popen(
                [str(build / "virtual-display"), str(width), str(height), str(scale)],
                stdout=subprocess.PIPE, text=True)
            case = {"width": width, "height": height, "scale": scale}
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(process.stdout, selectors.EVENT_READ)
                    if not selector.select(timeout=12):
                        raise RuntimeError("Virtual display readiness timed out")
                    ready = process.stdout.readline()
                if not ready:
                    raise RuntimeError(
                        f"Virtual display unavailable (helper exit {process.poll()}); "
                        "this is not a passing GUI test")
                case["native"] = json.loads(ready)
                deadline = time.monotonic() + 10
                while True:
                    observed = screens()
                    virtual = [item for item in observed if item["name"] == NAME]
                    if len(virtual) == 1 and all(virtual[0][key] == value for key, value in
                            [("width", width), ("height", height), ("scale", scale)]):
                        break
                    if time.monotonic() > deadline:
                        raise RuntimeError(f"Qt did not see the expected virtual display: {observed}")
                    time.sleep(0.1)
                physical = [item for item in observed if item["name"] != NAME]
                if canonical(physical) != canonical(baseline):
                    raise RuntimeError("Existing display geometry changed during the fixture")
                case["screens"] = observed
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                process.stdout.close()
                deadline = time.monotonic() + 10
                while canonical(screens()) != canonical(baseline):
                    if time.monotonic() > deadline:
                        raise RuntimeError("Display list did not return to its baseline after cleanup")
                    time.sleep(0.1)
            case["cleanup"] = "original display geometry and scale restored"
            results["cases"].append(case)
            print(f"PASS: virtual {width}x{height} at {scale}x; Qt enumeration and cleanup")
        if evidence_path:
            evidence_path.parent.mkdir(parents=True, exist_ok=True)
            evidence_path.write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=pathlib.Path, help="Write non-secret display-state JSON")
    arguments = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Requires macOS with an active WindowServer desktop")
    try:
        run_check(arguments.evidence)
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
