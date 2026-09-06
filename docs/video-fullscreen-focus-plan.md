# Separate-screen video: fullscreen and controller focus

Prepared September 6, 2026, against released OwlSwitch 1.6.6
(`50daf669415555eb688ef9c0ce8a8ef7e486fc30`). Planning branch:
`plan/video-fullscreen-focus`. Investigation and implementation plan only.

## Confirmed scope

- The problem affects **videos**, not Nature/Tumblr slideshows.
- The unwanted chrome is the **window title bar and red/yellow/green buttons**,
  not the macOS Apple menu bar.
- Incomplete fullscreen occurs on the **first video after opening OwlSwitch**.
- Video should cover the selected media display reliably, including while the
  controller has focus.
- Starting video should leave keyboard focus on the controller. If the user
  switches to another app during loading, respect that choice.
- Preserve the media's aspect ratio and existing Auto Crop setting. Black bars
  inside a correctly sized video window are different from an undersized window.

Prioritize the first mpv window creation after a cold app launch. Subsequent
videos are regression coverage. Monitor wake/reconnection is not the reported
trigger and does not justify a broader display-management rewrite.

## Findings and confidence

| Finding | Evidence | Conclusion |
| --- | --- | --- |
| All video modules launch through one controller | `src/player/MpvController.cpp`, desktop launch around lines 628–650 | There is one shared place to fix video-window policy. |
| Video requests fullscreen but disables native macOS fullscreen Spaces | `--fullscreen --no-native-fs`, with a comment explaining early OSD/Space-transition problems | The app intends full-display playback; adding another fullscreen launch flag would duplicate what it already does. |
| mpv is allowed to activate its window | OwlSwitch supplies no `focus-on` override; bundled mpv 0.41.0 reports the default as `open` | This directly explains the focus handoff to mpv. |
| The controller has no native activation handoff on video readiness | QML `forceActiveFocus()` selects an item; no `QWindow::requestActivate()`/native handoff follows video startup | QML item focus does not undo mpv app activation. Prefer preventing the activation first. |
| Window decorations are not explicitly disabled | Bundled mpv reports `border=yes`; OwlSwitch supplies no override | `--border=no` is an appropriate candidate, but not proof of a complete fullscreen fix. |
| mpv's non-native fullscreen already sets a borderless frame to the target screen | Upstream 0.41.0 `Window.setToFullScreen()`; fullscreen is queued after initial window creation | Persistent chrome or incomplete bounds could mean a fullscreen transition/state issue. The precise intermittent trigger is unconfirmed. |
| Display selection is captured at app startup | `src/main.cpp:124–158`; Qt's selected screen index is passed to mpv, which indexes `NSScreen.screens` | Verify both sides select the same physical display. Ordering or stale topology is a hypothesis, not an established cause. |
| Existing tests check arguments and logical display selection | `tests/MpvControllerTest.cpp`, `tests/DisplaySelectionTest.cpp` | They do not verify real macOS window bounds, inactive-window chrome, or foreground ownership. |

Only the built-in display was connected during this investigation. Source and
bundled-option checks were completed; a physical second-monitor reproduction
was not performed. No application code or installed settings were changed.

## Execution plan

### 1. Capture a failing baseline

- Use the signed 1.6.6 app with isolated test data and a physical second monitor.
  Use a generated local clip first, then the affected streaming module.
- Record the selected controller/media display, the actual mpv display, logical
  screen bounds and scale, video-window bounds, fullscreen state, border setting,
  and which application/window owns keyboard focus. Compare like units.
- Inspect immediately after creation, after the first decoded video frame, after
  focusing the controller, and after switching to a third app. Distinguish a brief
  startup transition from a persistent wrong frame.
- Repeat cold app launch followed by the first video, including different
  selected media displays. Use video replacement as a comparison. Also check
  mouse movement near the video's top edge.
- Read state through existing mpv IPC and native window inspection. Any temporary
  diagnostic output should contain window/display state only, not media URLs or
  authentication data.

Done when the incomplete frame/chrome is associated with an observable state
change, or the unreproduced cases are clearly recorded. Do not label a two-flag
patch a fullscreen fix without this evidence.

### 2. Apply the smallest shared video policy

- Extend the existing playback-display configuration to pass
  `DisplaySelection::hasSeparateMediaScreen()` alongside the selected display.
  Keep `resolveDisplaySelection` as the authority; no per-module flags or new setting.
- For macOS **separate-screen video**, test `--focus-on=never` and `--border=no`
  in the existing desktop argument builder. The packaged helper supports both.
- Keep `--fullscreen`, the existing screen selection, and `--no-native-fs` for
  the first candidate. Native Spaces are a separate behavior change with known
  OSD/transition implications, not an automatic improvement.
- Confirm that preventing mpv activation keeps the controller focused and leaves
  a deliberately selected third app focused during a slow load. Upstream mpv
  captures the active app while initializing its video window; verify the actual
  packaged behavior, including window recreation.
- Preserve single-display playback and Nature's isolated audio-only profile.
  Reuse existing keyboard/remote routing. Do not add an always-on focus timer.

Done when opening or replacing external video leaves the correct app focused
without title-bar controls, and the baseline regression is resolved.

### 3. Add a targeted correction only if the baseline requires it

Use the measured failure to choose the correction; these are conditional tasks,
not a request to build several competing fullscreen systems.

- **Wrong display:** correct the Qt-to-mpv display mapping centrally. Match the
  resolved physical screen rather than assuming equal enumeration order. Keep
  existing safe fallback behavior if a display is unavailable.
- **Fullscreen remains false after video becomes ready:** reuse the controller's
  IPC path for one bounded, idempotent request to enter fullscreen, with readback
  and cancellation on stop/replacement. Anchor it to first video readiness for
  the current process, not an arbitrary sleep.
- **Fullscreen is true but frame/chrome is wrong:** isolate that case with the
  bundled helper and the selected display. Compare supported mpv geometry/window
  options before considering a native bridge or helper change. A redundant
  `set fullscreen yes` is not a repair for an already-true state, and repeated
  fullscreen toggles risk flashes and transition regressions.
- **The built-in focus policy proves insufficient:** add a narrow, one-time
  controller activation using the existing Qt window and macOS utility seam.
  Permit it only for a user-initiated separate-screen video session whose focus
  was taken by its own mpv process. Cancel it if another app was deliberately
  activated while loading. Guard against stale sessions, audio-only readiness,
  pause/seek notifications, and automatic playlist advancement. Prefer Qt's
  activation request; use AppKit only for behavior Qt cannot supply.

The existing `playbackReady` signal can fire for audio parameters and playback
restarts, so it must not become an unconditional focus-restoration trigger.
No repeated polling, blanket activation, raised system window levels, or module
specific timers should be introduced as a default solution.

### 4. Verify the behavior and prepare a reviewable change

- Extend `MpvControllerTest` for separate-screen versus same-screen arguments,
  audio-only exclusion, and explicit policy overriding conflicting defaults.
  Extend `DisplaySelectionTest` only if display resolution changes.
- If a readiness/focus correction is needed, test its actual failure modes:
  delayed load, third-app activation, cancelled/replaced session, failed startup,
  seek/resume, and playlist advancement. Do not add timing tests that merely
  assert a chosen delay.
- On real displays, repeat at least ten cold app launches/first-video starts
  plus subsequent-video replacements for a local clip;
  smoke Jellyfin, Retro, Karaoke, and Local through the shared controller.
  Include mixed display scaling and an external display positioned left/above
  the controller. Verify controller focus and all four output edges after blur.
- Check delayed network startup, command-tab during loading, Back/stop, and
  single-screen playback. Keep Retro static transitions, Karaoke transitions,
  subtitles/OSD, and remote navigation working. Smoke Nature/Tumblr without
  changing their output implementation.
- Run the normal build, relevant tests, full CTest, and repository QML lint.
  Verify the packaged app on the physical display setup before calling the fix
  accepted. Open a focused PR with the reproduced before/after behavior.

The initial implementation should be concentrated in `MpvController.h/.cpp`,
its configuration in `src/main.cpp`, and the existing controller tests. Touch
`src/macos_utils.*` or display-resolution code only when the measured failure
requires it. Release/version selection follows implementation acceptance.

## Definition of done

The video window covers the chosen display with no title bar or traffic-light
buttons, stays that way when the controller or another app is focused, and opens
without taking keyboard focus away from the intended app. This holds across
first launch and subsequent videos. Existing aspect-ratio, overlay, remote,
single-display, slideshow, and audio-only behavior remains valid.

## Primary references

- [mpv window options](https://mpv.io/manual/stable/#window): `focus-on`, `border`,
  `screen`, `fs-screen`, and `native-fs`. Options were also checked against the
  actual bundled mpv 0.41.0 executable.
- [mpv 0.41.0 window lifecycle](https://github.com/mpv-player/mpv/blob/v0.41.0/video/out/mac/window.swift).
- [mpv 0.41.0 activation and screen selection](https://github.com/mpv-player/mpv/blob/v0.41.0/video/out/mac/common.swift).
- [mpv 0.41.0 video initialization](https://github.com/mpv-player/mpv/blob/v0.41.0/video/out/mac_common.swift).
- [Qt native window activation](https://doc.qt.io/qt-6/qwindow.html#requestActivate).
