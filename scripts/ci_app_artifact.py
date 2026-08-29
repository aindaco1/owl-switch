#!/usr/bin/env python3
"""Hash and safely extract the exact-commit unsigned CI app artifact."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
from typing import NoReturn


EXPECTED_ROOT = "owl-switch-ci-app"
MAX_MEMBERS = 50_000
MAX_UNCOMPRESSED_BYTES = 2 * 1024 * 1024 * 1024


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def safe_relative_path(name: str) -> PurePosixPath:
    trimmed = name[:-1] if name.endswith("/") else name
    if not trimmed or trimmed.startswith("/") or "\\" in trimmed or "//" in trimmed:
        fail("CI app archive contains an unsafe path")
    relative = PurePosixPath(trimmed)
    if not relative.parts or relative.parts[0] != EXPECTED_ROOT:
        fail("CI app archive has an unexpected root")
    if any(part in {"", ".", ".."} for part in relative.parts):
        fail("CI app archive contains path traversal")
    return relative


def safe_link_target(member_path: PurePosixPath, target: str) -> None:
    if not target or target.startswith("/") or "\\" in target:
        fail("CI app archive contains an unsafe link target")
    stack = list(member_path.parent.parts)
    for part in PurePosixPath(target).parts:
        if part in {"", "."}:
            continue
        if part == "..":
            if len(stack) <= 1:
                fail("CI app archive link escapes its root")
            stack.pop()
        else:
            stack.append(part)
    if not stack or stack[0] != EXPECTED_ROOT:
        fail("CI app archive link escapes its root")


def iter_tree(root: Path):
    if not root.is_absolute() or not root.is_dir() or root.is_symlink():
        fail("app tree must be an absolute, real directory")

    def walk(directory: Path):
        for entry in sorted(os.scandir(directory), key=lambda value: value.name):
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            stat_result = path.lstat()
            mode = stat_result.st_mode & 0o777
            if entry.is_symlink():
                target = os.readlink(path)
                safe_link_target(PurePosixPath(EXPECTED_ROOT, relative), target)
                yield ("L", relative, mode, target, 0, "")
            elif entry.is_dir(follow_symlinks=False):
                yield ("D", relative, mode, "", 0, "")
                yield from walk(path)
            elif entry.is_file(follow_symlinks=False):
                digest = hashlib.sha256()
                with path.open("rb") as source:
                    for chunk in iter(lambda: source.read(1024 * 1024), b""):
                        digest.update(chunk)
                yield ("F", relative, mode, "", stat_result.st_size, digest.hexdigest())
            else:
                fail("app tree contains a special file")

    yield from walk(root)


def tree_manifest(root: Path) -> dict[str, int | str]:
    digest = hashlib.sha256()
    entries = 0
    total_bytes = 0
    for kind, relative, mode, target, size, file_digest in iter_tree(root):
        record = f"{kind}\0{relative}\0{mode:o}\0{target}\0{size}\0{file_digest}\n"
        digest.update(record.encode("utf-8", "surrogateescape"))
        entries += 1
        total_bytes += size
    return {"sha256": digest.hexdigest(), "entries": entries, "bytes": total_bytes}


def extract(archive: Path, destination: Path) -> None:
    if not archive.is_absolute() or not archive.is_file() or archive.is_symlink():
        fail("CI app archive is missing or unsafe")
    if not destination.is_absolute() or destination.exists() or destination.is_symlink():
        fail("CI app extraction destination must be an absent absolute path")

    with tarfile.open(archive, "r:gz") as tar:
        members = tar.getmembers()
        if not members or len(members) > MAX_MEMBERS:
            fail("CI app archive member count is invalid")
        total_size = 0
        seen: set[PurePosixPath] = set()
        validated: list[tuple[tarfile.TarInfo, PurePosixPath]] = []
        for member in members:
            relative = safe_relative_path(member.name)
            if relative in seen:
                fail("CI app archive contains duplicate paths")
            seen.add(relative)
            if member.issym():
                safe_link_target(relative, member.linkname)
            elif not member.isdir() and not member.isfile():
                fail("CI app archive contains a hard link or special file")
            if member.isfile():
                total_size += member.size
                if total_size > MAX_UNCOMPRESSED_BYTES:
                    fail("CI app archive exceeds its size limit")
            if member.mode & 0o7000:
                fail("CI app archive contains a privileged file mode")
            validated.append((member, relative))

        destination.mkdir(mode=0o755)
        directories: list[tuple[Path, int]] = []
        for member, relative in sorted(validated, key=lambda item: len(item[1].parts)):
            target = destination.joinpath(*relative.parts)
            mode = member.mode & 0o777
            if member.isdir():
                target.mkdir(mode=0o755, parents=True, exist_ok=True)
                directories.append((target, mode))
                continue
            target.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
            if member.issym():
                os.symlink(member.linkname, target)
                continue
            source = tar.extractfile(member)
            if source is None:
                fail("CI app archive file could not be read")
            descriptor = os.open(
                target,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
                mode,
            )
            with source, os.fdopen(descriptor, "wb") as output:
                shutil.copyfileobj(source, output)
            os.chmod(target, mode, follow_symlinks=False)
        for directory, mode in reversed(directories):
            os.chmod(directory, mode, follow_symlinks=False)


def main() -> None:
    if len(sys.argv) < 2 or sys.argv[1] not in {"manifest", "extract"}:
        fail("usage: ci_app_artifact.py manifest <absolute-app> | extract <absolute-archive.tar.gz> <absolute-destination>")
    if sys.argv[1] == "manifest":
        if len(sys.argv) != 3:
            fail("usage: ci_app_artifact.py manifest <absolute-app>")
        print(json.dumps(tree_manifest(Path(sys.argv[2])), separators=(",", ":")))
        return
    if len(sys.argv) != 4:
        fail("usage: ci_app_artifact.py extract <absolute-archive.tar.gz> <absolute-destination>")
    extract(Path(sys.argv[2]), Path(sys.argv[3]))


if __name__ == "__main__":
    main()
