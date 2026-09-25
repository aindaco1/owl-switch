"""OwlSwitch Qt adapter for Platform's shared native display fixture."""
import importlib.util
import json
import pathlib
import shlex
import subprocess

_source = pathlib.Path(__file__).resolve().parents[2] / "shared/dust-wave-platform/tools/macos-display/support.py"
_spec = importlib.util.spec_from_file_location("dustwave_display_support", _source)
_shared = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_shared)
NAME = _shared.NAME
wait_for = _shared.wait_for
terminate = _shared.terminate
ready = _shared.ready
canonical = _shared.canonical
serial_desktop = _shared.serial_desktop


class NativeTools(_shared.NativeTools):
    def __init__(self, build, window=False):
        build = pathlib.Path(build)
        source = pathlib.Path(__file__).resolve().parent
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "Qt6Gui"], text=True, timeout=10))
        subprocess.run(["clang++", "-std=c++17", str(source / "QtDisplayProbe.cpp"),
                        "-o", str(build / "QtDisplayProbe"), *flags], check=True, timeout=60)
        def screens():
            return json.loads(subprocess.check_output(
                [str(build / "QtDisplayProbe")], text=True, timeout=10))
        super().__init__(build, window=window, screen_probe=screens)
