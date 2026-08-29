#!/usr/bin/env python3
"""Regression fixtures for CI app hashing and safe extraction."""

from __future__ import annotations

import importlib.util
import io
from pathlib import Path
import stat
import sys
import tarfile
import tempfile


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("ci_app_artifact", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load CI app artifact module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def add_directory(tar: tarfile.TarFile, name: str) -> None:
    member = tarfile.TarInfo(name)
    member.type = tarfile.DIRTYPE
    member.mode = 0o755
    tar.addfile(member)


def add_file(tar: tarfile.TarFile, name: str, content: bytes, mode: int = 0o644) -> None:
    member = tarfile.TarInfo(name)
    member.size = len(content)
    member.mode = mode
    tar.addfile(member, io.BytesIO(content))


def expect_rejected(module, archive: Path, destination: Path) -> None:
    try:
        module.extract(archive, destination)
    except SystemExit:
        return
    raise AssertionError(f"unsafe archive was accepted: {archive}")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: CiAppArtifactTest.py <ci_app_artifact.py>")
    module = load_module(Path(sys.argv[1]).resolve())

    with tempfile.TemporaryDirectory(prefix="owl-switch-ci-app-test.") as temporary:
        root = Path(temporary)
        source_app = root / "source.app"
        executable = source_app / "Contents" / "MacOS" / "app"
        framework_version = source_app / "Contents" / "Frameworks" / "Example.framework" / "Versions" / "A"
        executable.parent.mkdir(parents=True)
        framework_version.mkdir(parents=True)
        executable.write_bytes(b"executable\n")
        executable.chmod(0o755)
        (framework_version / "Example").write_bytes(b"framework\n")
        (framework_version.parent / "Current").symlink_to("A")
        (framework_version.parent.parent / "Example").symlink_to("Versions/Current/Example")

        expected_manifest = module.tree_manifest(source_app)
        archive = root / "valid.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(source_app, arcname=f"{module.EXPECTED_ROOT}/app/OwlSwitch.app")
            add_file(tar, f"{module.EXPECTED_ROOT}/metadata.json", b"{}\n")
        extracted = root / "valid-extracted"
        module.extract(archive, extracted)
        restored_app = extracted / module.EXPECTED_ROOT / "app" / "OwlSwitch.app"
        assert module.tree_manifest(restored_app) == expected_manifest
        assert (restored_app / "Contents" / "Frameworks" / "Example.framework" / "Example").is_symlink()

        traversal = root / "traversal.tar.gz"
        with tarfile.open(traversal, "w:gz") as tar:
            add_file(tar, f"{module.EXPECTED_ROOT}/../escape", b"unsafe")
        expect_rejected(module, traversal, root / "traversal-extracted")

        escaping_link = root / "escaping-link.tar.gz"
        with tarfile.open(escaping_link, "w:gz") as tar:
            add_directory(tar, module.EXPECTED_ROOT)
            link = tarfile.TarInfo(f"{module.EXPECTED_ROOT}/escape")
            link.type = tarfile.SYMTYPE
            link.mode = 0o777
            link.linkname = "../outside"
            tar.addfile(link)
        expect_rejected(module, escaping_link, root / "escaping-link-extracted")

        hard_link = root / "hard-link.tar.gz"
        with tarfile.open(hard_link, "w:gz") as tar:
            add_directory(tar, module.EXPECTED_ROOT)
            link = tarfile.TarInfo(f"{module.EXPECTED_ROOT}/hard")
            link.type = tarfile.LNKTYPE
            link.mode = 0o644
            link.linkname = f"{module.EXPECTED_ROOT}/target"
            tar.addfile(link)
        expect_rejected(module, hard_link, root / "hard-link-extracted")

        privileged = root / "privileged.tar.gz"
        with tarfile.open(privileged, "w:gz") as tar:
            add_file(tar, f"{module.EXPECTED_ROOT}/setuid", b"unsafe", stat.S_ISUID | 0o755)
        expect_rejected(module, privileged, root / "privileged-extracted")

    print("CI app artifact tests passed")


if __name__ == "__main__":
    main()
