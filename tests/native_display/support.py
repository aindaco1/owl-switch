"""Shared, test-only macOS display/window fixtures. Nothing is installed in the app."""
import contextlib
import fcntl
import json
import pathlib
import selectors
import shlex
import subprocess
import tempfile
import time

NAME = "OwlSwitch virtual display test"


def wait_for(predicate, description, timeout=12):
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Timed out: {description}")
        time.sleep(0.05)


def terminate(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    if process.stdout:
        process.stdout.close()


def ready(process):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        if not selector.select(timeout=12):
            raise RuntimeError("Native fixture readiness timed out")
        line = process.stdout.readline()
    if not line:
        raise RuntimeError(f"Native fixture unavailable (exit {process.poll()}); not a GUI pass")
    return json.loads(line)


def canonical(items):
    return sorted(json.dumps(item, sort_keys=True) for item in items)


class NativeTools:
    def __init__(self, build, window=False):
        self.build = pathlib.Path(build)
        source = pathlib.Path(__file__).resolve().parent
        for stem, frameworks in [("VirtualDisplayProbe", ["AppKit", "CoreGraphics"])] + (
                [("WindowProbe", ["AppKit", "ApplicationServices"])] if window else []):
            arguments = ["clang++", "-std=c++17", "-fobjc-arc"]
            for framework in frameworks:
                arguments += ["-framework", framework]
            subprocess.run([*arguments, str(source / f"{stem}.mm"), "-o",
                            str(self.build / stem)], check=True, timeout=60)
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "Qt6Gui"], text=True, timeout=10))
        subprocess.run(["clang++", "-std=c++17", str(source / "QtDisplayProbe.cpp"),
                        "-o", str(self.build / "QtDisplayProbe"), *flags], check=True, timeout=60)

    def screens(self):
        return json.loads(subprocess.check_output(
            [str(self.build / "QtDisplayProbe")], text=True, timeout=10))

    def window(self, command, *arguments):
        return json.loads(subprocess.check_output(
            [str(self.build / "WindowProbe"), command, *map(str, arguments)],
            text=True, timeout=10))

    @contextlib.contextmanager
    def display(self, width, height, scale, origin=None):
        baseline = self.screens()
        if not baseline or any(item["name"] == NAME for item in baseline):
            raise RuntimeError("An active desktop and no concurrent virtual fixture are required")
        args = [str(self.build / "VirtualDisplayProbe"), str(width), str(height), str(scale)]
        if origin is None:
            origin = (max(s["x"] + s["width"] for s in baseline), 0)
        args += [str(origin[0]), str(origin[1]), "600"]
        process = subprocess.Popen(args, stdout=subprocess.PIPE, text=True)
        try:
            native = ready(process)

            def configured():
                observed = self.screens()
                virtual = [s for s in observed if s["name"] == NAME]
                if len(virtual) != 1 or any(virtual[0][key] != value for key, value in [
                        ("width", width), ("height", height), ("scale", scale),
                        ("x", origin[0]), ("y", origin[1])]):
                    return None
                if canonical([s for s in observed if s["name"] != NAME]) != canonical(baseline):
                    raise RuntimeError("Existing display geometry changed during the fixture")
                return observed

            try:
                observed = wait_for(configured, "Qt virtual display geometry and scale")
            except RuntimeError as error:
                raise RuntimeError(f"{error}; expected {width}x{height}@{scale} at {origin}; "
                                   f"observed {self.screens()}; native {native}") from error
            yield {"native": native, "screens": observed, "screen": next(
                s for s in observed if s["name"] == NAME),
                "index": next(i for i, s in enumerate(observed) if s["name"] == NAME)}
        finally:
            terminate(process)
            wait_for(lambda: canonical(self.screens()) == canonical(baseline),
                     "original display geometry and scale restored")


@contextlib.contextmanager
def serial_desktop():
    # Also prevents the smoke check and full suite from racing each other.
    with open(pathlib.Path(tempfile.gettempdir()) / "owlswitch-native-display.lock", "w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("Another OwlSwitch native display test is running") from error
        yield
