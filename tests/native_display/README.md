# Native display tests

This test-only fixture creates a real macOS virtual display, checks its geometry
and scale through Qt, and verifies cleanup. It is not included in the app bundle.

From the checkout root:

```sh
python3 tests/native_display/smoke.py --evidence dist/window-investigation/virtual-display-evidence.json
```

Requirements: macOS with an active WindowServer desktop, Xcode command-line
tools, Python 3, `pkg-config`, and Qt 6. The script compiles its two small helpers
into a temporary directory and removes them afterward. No third-party display
app or driver installation is required.

The check temporarily adds a monitor to the active desktop. Run it on its own,
not concurrently with another display test or while changing display settings.
It checks 1920×1080 at 1× and 1280×720 logical pixels at 2×. Each case verifies
Qt's enumeration, dimensions, scale, unchanged existing displays, and restoration
of the original display list when the helper exits. The helper also has a
30-second lifetime limit if its driver is interrupted.

Local verification on September 6, 2026: both cases passed on macOS 26.6.2,
Apple Silicon, Qt 6.11.1. The existing built-in 1728×1117 display remained at 2×.

## Coverage boundary

This is a passing **fixture capability check**. It does not yet launch OwlSwitch
video, assert its title bar, test its focus behavior, or fix either reported bug.
Those regression assertions are specified in
[`docs/video-fullscreen-focus-plan.md`](../../docs/video-fullscreen-focus-plan.md).

The helper uses private `CGVirtualDisplay` interfaces, following the approach
documented in [Chromium's own macOS display tests](https://chromium.googlesource.com/chromium/src/+/HEAD/ui/display/mac/test/virtual_display_util_mac.mm).
Runtime lookup detects missing classes. Any unsupported environment, timeout,
wrong geometry, or cleanup failure makes the smoke check fail; it is never a
silent success. An active GUI session and API availability on the hosted runner
must be verified before making this a required CI/release check.

Future native window tests may require Screen Recording or Accessibility
permission for pixel/chrome assertions. Detect those prerequisites without
automatically prompting or editing system permissions. An unavailable native
test is not evidence of fullscreen/focus acceptance.
