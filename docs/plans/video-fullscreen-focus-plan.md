# Separate-screen video: fullscreen and controller focus

Status: merged through [PR #24](https://github.com/aindaco1/owl-switch/pull/24)
for **1.6.7**. Source and packaged regression acceptance are recorded below.
The release workflow records signed-app acceptance separately. Reconciled
September 6, 2026.

Implementation branch: `plan/video-fullscreen-focus`, based on released 1.6.6
(`50daf669415555eb688ef9c0ce8a8ef7e486fc30`). The maintainer selected 1.6.7
after source/package acceptance; 1.6.6 remains the published comparison baseline.

## Confirmed scope

- Videos only. Nature/Tumblr slideshows keep their existing window path.
- The unwanted chrome is the window title bar and traffic-light buttons.
- Prioritize the first video after a cold app launch.
- Fill the selected media display and keep that frame when another window is focused.
- Keep keyboard focus on the controller, respecting a deliberate switch to another app while loading.
- Preserve aspect ratio, Auto Crop, overlays, remote navigation, same-screen playback, and Nature audio.

## Investigation and implementation

The signed 1.6.6 baseline took focus in all ten cold-start cases, and in the
controlled delayed-start case where another test app had been activated. Settled
video bounds and inactive-window chrome passed on the tested virtual displays.
A finer WindowServer trace exposed a separate startup transition: the first
visible window was approximately **628×352**, then expanded to 1920×1080. A
persistent undersized/chromed window was not reproduced on this Mac.

All video modules use `MpvController`. Its existing display configuration now
carries `DisplaySelection::hasSeparateMediaScreen()`. Only macOS video on a
separate display receives these additional mpv options:

```text
--focus-on=never
--border=no
--geometry=100%x100%+0+0
--macos-geometry-calculation=whole
```

The geometry includes the initial window that mpv creates before it queues
non-native fullscreen. Explicit position matters: setting dimensions alone left
the initial large window offset from the display's origin. The existing
`--fullscreen`, `--no-native-fs`, and selected screen indices remain authoritative.
The implementation uses supported helper options; it needs no readiness-triggered
activation, retries, per-module window policy, or native production bridge.

The candidate preserved controller focus, respected a different app during the
delayed-start test, and started at display-sized geometry. Normal macOS window
opening animation remains; settled bounds must match all edges within one point.
The Qt-to-mpv screen index mapping passed the tested topologies, so no mapping
rewrite was justified.

## Regression and release checks

The reusable suite is documented in
[`tests/native_display/README.md`](../../tests/native_display/README.md). It launches
the real app with an isolated saved Local queue, generated clips, private IPC
paths, a native virtual display, and an independent test app for focus checks.
It uses WindowServer geometry, Accessibility chrome inspection, mpv state, and
actual controller keyboard actions. A deliberately windowed/decorated negative
control must be rejected by both the frame and chrome checks.

- [x] Implement shared video policy and same-screen/audio-only argument tests.
- [x] Reproduce focus stealing in signed 1.6.6 and record its initial undersized window.
- [x] Pass all 31 CTest targets and the full repository QML lint command.
- [x] Pass the 17-case source native suite, including ten cold starts, delayed focus,
  cancellation, same-screen playback, 1×/2× scaling, and left/above placement.
- [x] Verify the hosted `macos-26` runner can create displays and inspect native windows.
- [x] Pass the expanded 18-case packaged suite, including a non-16:9 display and aspect-ratio assertions.
- Final hosted CI acceptance is tracked by the required check on [PR #24](https://github.com/aindaco1/owl-switch/pull/24).
- [x] Open a focused PR with source/package/native evidence.
- [x] Require the same suite against the Developer ID signed app before publication.
  Each release run retains `signed-native-display-evidence`; that artifact records
  the signed version/hash and results for the release being accepted.

CI now runs the native suite after bundle preparation and before the exact-commit
app is packaged and attested. The prepared test app receives ad-hoc signatures
because normalized Mach-O executables require valid local signatures on Apple
Silicon. Release signing replaces those signatures, and the release workflow
runs the same tests against the signed app before notarization/publication.
Missing prerequisites and failures stop the corresponding job; an offscreen or
fixture-only pass cannot satisfy this gate.

The 1.6.7 signed gate also exposed a missing dynamically loaded MoltenVK driver:
CI used Homebrew's driver, while the release runner fell back to a software Cocoa
renderer with repeated Accessibility timeouts. The bundled-driver contract is
documented in [BUILDING.md](../BUILDING.md). Native cases now require `gpu-next`
with an invalid inherited Vulkan discovery path, so external development tools
cannot conceal an incomplete release bundle.

The hosted VM's `Apple Virtual` primary automatically resizes when a monitor is
added. The fixture permits that controller dimension change only on that hosted
synthetic desktop, checks its identity/origin/scale, and still requires exact
restoration afterward. Physical-display geometry remains strict locally.

Evidence lives under ignored `dist/window-investigation/` locally and is retained
as named CI artifacts. Reports identify the tested executable hash, app version,
OS, full-matrix status, display/window/focus observations, and cleanup. An
interrupted run or concurrent human desktop input can invalidate focus tests;
use an idle GUI session. Native tests do not simulate monitor firmware, cables,
or HDMI/DisplayPort wake behavior. A physical-monitor spot check remains useful
for the originally reported persistent symptom that was not reproduced here.

## Primary references

- [mpv window options](https://mpv.io/manual/stable/#window): focus, borders, geometry, screen selection.
- [mpv macOS geometry calculation](https://mpv.io/manual/stable/#options-macos-geometry-calculation).
- [mpv 0.41.0 window lifecycle](https://github.com/mpv-player/mpv/blob/v0.41.0/video/out/mac/window.swift).
- [mpv 0.41.0 activation and display geometry](https://github.com/mpv-player/mpv/blob/v0.41.0/video/out/mac/common.swift).
- [Chromium virtual display fixture](https://chromium.googlesource.com/chromium/src/+/HEAD/ui/display/mac/test/virtual_display_util_mac.mm).
