#!/usr/bin/env python3
"""Keep shared framework dependencies independent of the launching executable."""
import argparse
import os
from pathlib import Path
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True)


def normalize(app, check=False):
    frameworks = (app / "Contents/Frameworks").resolve(strict=True)
    prefix = "@executable_path/../Frameworks/"
    changes = []
    for binary in sorted(frameworks.rglob("*")):
        if binary.is_symlink() or not binary.is_file():
            continue
        if "Mach-O" not in output("/usr/bin/file", "-b", str(binary)):
            continue
        install_ids = set(output("/usr/bin/otool", "-D", str(binary)).splitlines()[1:])
        for line in output("/usr/bin/otool", "-L", str(binary)).splitlines()[1:]:
            dependency = line.strip().split(" (", 1)[0]
            if dependency in install_ids or not dependency.startswith(prefix):
                continue
            target = (frameworks / dependency[len(prefix):]).resolve(strict=True)
            if frameworks not in target.parents or not target.is_file():
                raise ValueError(f"Unsafe shared framework target: {dependency}")
            replacement = "@loader_path/" + os.path.relpath(target, binary.parent)
            changes.append((binary, dependency, replacement))
    if check and changes:
        raise ValueError(f"{len(changes)} framework links depend on the launching executable")
    # Validate the whole plan before changing any load commands. Release signing
    # runs after this step; never rewrite an already accepted signed artifact.
    for binary, dependency, replacement in changes:
        subprocess.run(["/usr/bin/codesign", "--remove-signature", str(binary)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["/usr/bin/install_name_tool", "-change", dependency,
                        replacement, str(binary)], check=True)
    return len(changes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    print(f"Shared framework links: {normalize(args.app, args.check)} rewritten")
