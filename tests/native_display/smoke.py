#!/usr/bin/env python3
"""Verify macOS virtual-display creation, Qt enumeration/scaling, and cleanup."""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
from support import NativeTools, serial_desktop


def run_check(evidence_path, require_window_access=False):
    results = {"status": "failed", "cases": []}
    try:
        with serial_desktop(), tempfile.TemporaryDirectory(prefix="owlswitch-virtual-display-") as directory:
            tools = NativeTools(directory, window=require_window_access)
            results["baseline"] = tools.screens()
            if require_window_access:
                results["permissions"] = tools.window("permissions")
                print("Window permissions:", results["permissions"], flush=True)
            for width, height, scale in [(1920, 1080, 1), (1280, 720, 2)]:
                with tools.display(width, height, scale) as display:
                    case = {"width": width, "height": height, "scale": scale, **display}
                case["cleanup"] = "original display geometry and scale restored"
                results["cases"].append(case)
                print(f"PASS: virtual {width}x{height} at {scale}x; Qt enumeration and cleanup")
            if require_window_access and not results["permissions"]["accessibility"]:
                raise RuntimeError("Accessibility inspection unavailable; cannot run native window assertions")
            results["status"] = "passed"
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        results["error"] = str(error)
        raise
    finally:
        if evidence_path:
            evidence_path.parent.mkdir(parents=True, exist_ok=True)
            evidence_path.write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=pathlib.Path, help="Write non-secret display-state JSON")
    parser.add_argument("--require-window-access", action="store_true")
    arguments = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Requires macOS with an active WindowServer desktop")
    try:
        run_check(arguments.evidence, arguments.require_window_access)
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
