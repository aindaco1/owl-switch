#!/usr/bin/env python3
"""Bounded mpv IPC fixture: twelve-second files at 4x time, with pause/gain."""
import json
import os
import socket
import sys
import time

path = next(a.split("=", 1)[1] for a in sys.argv if a.startswith("--input-ipc-server="))
with open(path + ".args", "w") as f:
    json.dump(sys.argv[1:], f)
server = socket.socket(socket.AF_UNIX)
server.bind(path)
server.listen(1)
server.settimeout(10)
client, _ = server.accept()
client.settimeout(0.02)
paused = True
position = 0.0
duration = 12.0
buffer = b""
finished = False
started = last = time.monotonic()

def send(value):
    client.sendall(json.dumps(value).encode() + b"\n")

def prop(name, value):
    send({"event": "property-change", "name": name, "data": value})

try:
    with open(path + ".commands", "a", buffering=1) as log:
        while time.monotonic() - started < 25:
            now = time.monotonic()
            if not paused and not finished:
                position = min(duration, position + (now - last) * 4)
                prop("time-pos", position)
                if position >= duration:
                    finished = True
                    send({"event": "end-file", "reason": "eof"})
            last = now
            try:
                data = client.recv(65536)
                if not data:
                    break
                buffer += data
            except socket.timeout:
                continue
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                command = json.loads(line).get("command", [])
                log.write(json.dumps(command) + "\n")
                if command[0] == "observe_property":
                    name = command[2]
                    values = {"time-pos": position, "duration": duration,
                              "audio-params": {"samplerate": 48000}, "pause": paused}
                    if name in values:
                        prop(name, values[name])
                elif command[:2] == ["set_property", "pause"]:
                    paused = command[2]
                elif command[0] == "quit":
                    sys.exit(0)
except (BrokenPipeError, ConnectionResetError):
    pass
