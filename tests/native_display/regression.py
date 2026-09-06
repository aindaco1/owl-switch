#!/usr/bin/env python3
"""Exercise the real OwlSwitch app and bundled mpv on native virtual monitors."""
import argparse
import json
import os
import pathlib
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from support import NativeTools, ready, serial_desktop, terminate, wait_for


class IPC:
    def __init__(self, path):
        self.path = str(path)
        self.request = 0

    def command(self, *command):
        self.request += 1
        with socket.socket(socket.AF_UNIX) as connection:
            connection.settimeout(3)
            connection.connect(self.path)
            connection.sendall(json.dumps({"command": command, "request_id": self.request}).encode() + b"\n")
            with connection.makefile("rb") as stream:
                while True:
                    line = stream.readline(65536)
                    if not line:
                        raise RuntimeError("mpv IPC closed before its response")
                    response = json.loads(line)
                    if response.get("request_id") == self.request:
                        if response.get("error") != "success":
                            raise RuntimeError(f"mpv {command[0]}: {response.get('error')}")
                        return response.get("data")

    def get(self, property_name):
        return self.command("get_property", property_name)


def frame_matches(frame, screen):
    return all(abs(frame[key] - screen[value]) <= 1 for key, value in [
        ("X", "x"), ("Y", "y"), ("Width", "width"), ("Height", "height")])


def window_failures(state, screen, front):
    windows = [w for w in state["windows"] if w["layer"] == 0 and w["alpha"] > 0]
    failures = []
    if len(windows) != 1 or not frame_matches(windows[0]["frame"], screen):
        failures.append("video does not cover the selected display")
    accessible = state["accessibleWindows"]
    if len(accessible) != 1:
        failures.append("one accessible video window is required for the chrome assertion")
    elif accessible[0]["buttons"]:
        failures.append("video exposes title-bar controls")
    if state["frontPID"] != front:
        failures.append("foreground app changed")
    return failures


class AppCase:
    def __init__(self, tools, app, root, media, display, same=False):
        self.tools, self.root, self.app, self.mpv = tools, root, app, None
        self.trace = None
        root.mkdir()
        data = root / "data"; data.mkdir()
        temporary = root / "tmp"; temporary.mkdir()
        config = root / "mpv-config"; config.mkdir()
        # Assert the app's separate-screen policy wins over these normal defaults.
        (config / "mpv.conf").write_text("focus-on=open\nborder=yes\n")
        controller = next(i for i in range(len(display["screens"])) if i != display["index"])
        media_index = controller if same else display["index"]
        self.screen = display["screens"][media_index]
        settings = {"app": {"startup_module": "com.owlswitch.local_files", "prevent_sleep": "OFF",
                    "controller_display_index": controller, "media_display_index": media_index},
                    "modules": {"com.owlswitch.local_files": {"media_directory": str(media),
                    "auto_launch": "ON", "resume_playback": "no"}}}
        (data / "config.json").write_text(json.dumps(settings))
        (data / "local_queue.json").write_text(json.dumps({"schemaVersion": 2, "media": [
            {"entryId": str(uuid.uuid4()), "filePath": str(clip)} for clip in sorted(media.glob("*.mp4"))],
            "soundtrack": []}))
        self.log = open(root / "app.log", "w")
        self.process = subprocess.Popen([str(app / "Contents/MacOS/OwlSwitch")],
            env={**os.environ, "DATA_ROOT": str(data), "TMPDIR": str(temporary) + "/",
                 "MPV_HOME": str(config)}, stdout=self.log, stderr=self.log, start_new_session=True)
        self.ipc = IPC(temporary / "owl-switch-mpv.sock")

    def child(self):
        result = subprocess.run(["pgrep", "-P", str(self.process.pid), "-x", "mpv"],
                                capture_output=True, text=True, timeout=5)
        pids = result.stdout.split()
        return int(pids[0]) if len(pids) == 1 else None

    def wait_video(self):
        wait_for(lambda: pathlib.Path(self.ipc.path).exists(), "mpv IPC socket")
        def decoded():
            try:
                return self.ipc.get("time-pos") > 0.3 and self.ipc.get("vo-configured")
            except (OSError, RuntimeError, TypeError):
                return False
        wait_for(decoded, "first decoded video frame", timeout=20)

    def snapshot(self):
        state = self.tools.window("snapshot", self.mpv)
        state["fullscreen"] = self.ipc.get("fullscreen")
        state["border"] = self.ipc.get("border")
        return state

    def activate(self, pid):
        self.tools.window("activate", pid)
        wait_for(lambda: self.tools.window("snapshot", pid)["frontPID"] == pid,
                 "test-owned app activation")

    def close(self):
        # Only this app's newly created process group; includes its own mpv/helpers.
        try:
            os.killpg(self.process.pid, signal.SIGCONT)
            os.killpg(self.process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait(timeout=5)
        if self.trace is not None:
            terminate(self.trace)
        try:
            wait_for(lambda: not self.group_running(), "owned app/helper process cleanup", timeout=3)
        except RuntimeError:
            os.killpg(self.process.pid, signal.SIGKILL)
            wait_for(lambda: not self.group_running(), "owned process group exits", timeout=3)
        self.log.close()

    def group_running(self):
        try:
            os.killpg(self.process.pid, 0)
            return True
        except ProcessLookupError:
            return False


def run(arguments):
    results = {"app": str(arguments.app), "cases": [], "status": "failed"}
    arguments.evidence.parent.mkdir(parents=True, exist_ok=True)
    try:
        with serial_desktop(), tempfile.TemporaryDirectory(prefix="owl-window-", dir="/tmp") as directory:
            root = pathlib.Path(directory)
            tools = NativeTools(root, window=True)
            permissions = tools.window("permissions")
            results["permissions"] = permissions
            if not permissions["accessibility"]:
                raise RuntimeError("Accessibility inspection unavailable; native tests cannot pass")
            results["baselineDisplays"] = tools.screens()
            media = root / "media"; media.mkdir()
            ffmpeg = arguments.app / "Contents/Resources/bin/ffmpeg"
            if not ffmpeg.is_file():
                ffmpeg = pathlib.Path(shutil.which("ffmpeg") or "")
            subprocess.run([str(ffmpeg), "-loglevel", "error", "-f", "lavfi", "-i",
                "color=c=0x208050:s=640x360:r=10:d=60", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                str(media / "first.mp4")], check=True, timeout=30)
            shutil.copyfile(media / "first.mp4", media / "second.mp4")
            sentinel = subprocess.Popen([str(root / "WindowProbe"), "sentinel"],
                                        stdout=subprocess.PIPE, text=True)
            try:
                ready(sentinel)
                matrix = [("standard", 1920, 1080, 1, None)]
                if not arguments.basic_only:
                    matrix += [("retina", 1280, 720, 2, None),
                               ("left", 1920, 1080, 1, (-1920, 0)),
                               ("above", 1280, 720, 2, (0, -720))]
                for label, width, height, scale, origin in matrix:
                    with tools.display(width, height, scale, origin) as display:
                        scenarios = [f"cold-{i + 1}" for i in range(arguments.cold_starts if label == "standard" else 1)]
                        if label == "standard" and not arguments.basic_only:
                            scenarios += ["delayed-switch", "cancelled-start", "same-screen", "negative-control"]
                        for scenario in scenarios:
                            name = f"{label}/{scenario}"
                            case = {"name": name, "display": display, "observations": [], "failures": []}
                            results["cases"].append(case)
                            app = AppCase(tools, arguments.app, root / f"case-{len(results['cases'])}",
                                          media, display, same=scenario == "same-screen")
                            try:
                                app.mpv = wait_for(app.child, "owned mpv process")
                                app.trace = subprocess.Popen([str(root / "WindowProbe"), "trace", str(app.mpv)],
                                                             stdout=subprocess.PIPE, text=True)
                                if scenario in ("delayed-switch", "cancelled-start"):
                                    # Pause this owned child before any window is created, then verify
                                    # that precondition. No production delays or network dependency.
                                    os.kill(app.mpv, signal.SIGSTOP)
                                    if tools.window("snapshot", app.mpv)["windows"]:
                                        raise RuntimeError("Could not hold mpv before window creation")
                                    app.activate(app.process.pid)
                                    app.activate(sentinel.pid)
                                    if scenario == "cancelled-start":
                                        app.activate(app.process.pid)
                                        tools.window("key", app.process.pid, 53)  # Escape -> real QML stop
                                        os.kill(app.mpv, signal.SIGCONT)
                                        wait_for(lambda: app.child() is None, "cancelled mpv exits")
                                        state = tools.window("snapshot", sentinel.pid)
                                        if state["frontPID"] != app.process.pid:
                                            case["failures"].append("cancelled startup stole focus")
                                        case["observations"].append({"cancelled": state})
                                        continue
                                    os.kill(app.mpv, signal.SIGCONT)
                                app.wait_video()
                                expected_front = (sentinel.pid if scenario == "delayed-switch" else
                                                  app.mpv if scenario == "same-screen" else app.process.pid)

                                def observe(stage):
                                    state = app.snapshot()
                                    case["observations"].append({"stage": stage, **state})
                                    case["failures"] += [f"{stage}: {error}" for error in
                                                         window_failures(state, app.screen, expected_front)]
                                    if state["fullscreen"] is not True:
                                        case["failures"].append(f"{stage}: mpv fullscreen is false")

                                observe("first-frame")
                                trace_output, _ = app.trace.communicate(timeout=5)
                                case["initialWindowTrace"] = json.loads(trace_output)
                                if scenario != "same-screen":
                                    visible = [w for sample in case["initialWindowTrace"]["changes"]
                                               for w in sample["windows"] if w["layer"] == 0 and w["alpha"] > 0]
                                    if not visible:
                                        case["failures"].append("no initial native video frame observed")
                                    # macOS animates a newly ordered window at roughly 98% of its
                                    # final size. Reject the small video-sized launch while allowing
                                    # that OS animation; settled frames must still match every edge.
                                    elif any(abs(w["frame"][key] - app.screen[value]) > app.screen[axis] * 0.05
                                             for w in visible for key, value, axis in [
                                                 ("X", "x", "width"), ("Y", "y", "height"),
                                                 ("Width", "width", "width"), ("Height", "height", "height")]):
                                        case["failures"].append("initial video window is undersized or misplaced")
                                if scenario == "negative-control":
                                    app.ipc.command("set_property", "border", True)
                                    app.ipc.command("set_property", "geometry", "640x360")
                                    app.ipc.command("set_property", "fullscreen", False)
                                    wait_for(lambda: not app.ipc.get("fullscreen"), "windowed negative control")
                                    time.sleep(0.5)  # Allow AppKit's window transition to finish.
                                    state = app.snapshot()
                                    errors = window_failures(state, app.screen, state["frontPID"])
                                    case["observations"].append({"stage": "deliberately-decorated", **state})
                                    for error in ["video does not cover the selected display", "video exposes title-bar controls"]:
                                        if error not in errors:
                                            case["failures"].append(f"negative control did not detect: {error}")
                                elif scenario == "same-screen":
                                    time.sleep(0.3)
                                    observe("settled")
                                else:
                                    app.activate(app.process.pid)
                                    expected_front = app.process.pid
                                    observe("controller-focused")
                                    if scenario == "cold-1":
                                        tools.window("key", app.process.pid, 49)  # Space -> real QML pause
                                        wait_for(lambda: app.ipc.get("pause") is True, "controller pause key")
                                        observe("paused")
                                        tools.window("key", app.process.pid, 49)
                                        wait_for(lambda: app.ipc.get("pause") is False, "controller resume key")
                                        tools.window("key", app.process.pid, 124)  # Right -> real QML seek
                                        wait_for(lambda: app.ipc.get("time-pos") > 4, "controller seek key")
                                        observe("seeked")
                                        app.ipc.command("playlist-play-index", 1)
                                        wait_for(lambda: app.ipc.get("playlist-pos") == 1, "next queued video")
                                        app.wait_video()
                                        observe("next-video")
                                    app.activate(sentinel.pid)
                                    expected_front = sentinel.pid
                                    observe("other-app-focused")
                                    app.activate(app.process.pid)
                                    tools.window("key", app.process.pid, 53)
                                    wait_for(lambda: app.child() is None, "controller Escape stops video")
                            except KeyboardInterrupt:
                                case["failures"].append("interrupted; incomplete native case")
                                raise
                            except (RuntimeError, OSError, subprocess.SubprocessError) as error:
                                case["failures"].append(str(error))
                            finally:
                                app.close()
                                arguments.evidence.write_text(json.dumps(results, indent=2) + "\n")
                                print(f"{'FAIL' if case['failures'] else 'PASS'}: {name}: {'; '.join(case['failures'])}", flush=True)
                    results.setdefault("cleanup", []).append(f"{label}: original displays restored")
            finally:
                terminate(sentinel)
            if any(case["failures"] for case in results["cases"]):
                raise RuntimeError("Native video regression failures; see evidence JSON")
            results["status"] = "passed"
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        results["error"] = str(error)
        print(f"FAIL: {error}", file=sys.stderr)
    finally:
        arguments.evidence.write_text(json.dumps(results, indent=2) + "\n")
    return 0 if results["status"] == "passed" else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=lambda p: pathlib.Path(p).absolute(), required=True)
    parser.add_argument("--evidence", type=pathlib.Path, required=True)
    parser.add_argument("--cold-starts", type=int, default=10)
    parser.add_argument("--basic-only", action="store_true", help="Diagnostic subset, not release acceptance")
    arguments = parser.parse_args()
    if sys.platform != "darwin" or not 1 <= arguments.cold_starts <= 20:
        parser.error("Requires macOS and 1–20 cold starts")
    sys.exit(run(arguments))
