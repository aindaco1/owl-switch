# Native video display regressions

These tests run the actual OwlSwitch app and its mpv subprocess on real macOS
virtual monitors. The private CoreGraphics display helper is test-only and is
never installed in the application. No display driver or third-party app is needed.

Run serially on an idle, logged-in macOS desktop. The suite activates its own app
windows; concurrent human input or other GUI tests can invalidate focus checks.
Use the existing release bundle, or the development app after building:

```sh
python3 tests/native_display/regression.py --app build/OwlSwitch.app \
  --evidence dist/native-display-regression.json
```

The default suite checks ten fresh app launches, the first visible video window,
settled bounds on all four edges, absence of accessible title-bar controls,
controller focus, pause/resume/seek/stop through actual keyboard input, the next
queued video, a deliberately delayed startup with another test app focused,
cancelled startup, same-screen playback, 1×/2× scaling, and left/above placement.
It seeds the existing Local saved queue with two generated clips under an isolated
`DATA_ROOT`; `TMPDIR` and mpv configuration are isolated too. It does not use a
product test mode or alter the installed app's settings.

The native trace polls WindowServer during window creation. It rejects a small
video-sized initial window while allowing macOS's brief window-opening animation;
settled frames must match the selected screen within one logical point. The
chrome oracle requires a real accessible video window and no close/minimize/zoom
controls. A deliberately decorated, windowed negative control must fail both the
geometry and chrome assertions. A true mpv fullscreen property alone cannot pass.

Requirements: macOS with WindowServer, Xcode command-line tools, Python 3,
`pkg-config`, Qt 6, and existing Accessibility inspection/event-posting permission
for the test runner. The bundled ffmpeg generates the clip; development builds can
use ffmpeg on PATH. Permission checks do not prompt or modify system permissions.
Missing prerequisites fail explicitly. Screen Recording is reported but is not
required: this suite uses WindowServer geometry and Accessibility, not screenshots.

A quick capability check (no video assertions) is also available:

```sh
python3 tests/native_display/smoke.py --require-window-access \
  --evidence dist/native-display-capability.json
```

Both scripts share the same display fixture and desktop lock. Each display helper
has a ten-minute lifetime bound. Normal exit, assertion failure, and interruption
close only test-owned processes and restore the original display geometry/scale.
JSON evidence contains only test-owned window/focus/display state. Reduced
`--cold-starts` or `--basic-only` runs are diagnostics, not full release acceptance.

The virtual display API follows [Chromium's own macOS display tests](https://chromium.googlesource.com/chromium/src/+/HEAD/ui/display/mac/test/virtual_display_util_mac.mm).
It is runtime-checked because it is private. Local fixture capability passed on
macOS 26.6.2, Apple Silicon, Qt 6.11.1. Hosted-runner availability must be verified
separately; a passing offscreen test or fixture smoke is not a native video pass.
Physical cable/firmware/wake behavior is outside this simulated display coverage.
