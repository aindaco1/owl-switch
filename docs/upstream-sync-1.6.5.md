# OwlSwitch 1.6.5 Upstream Review

This release reviews `anthonycaccese/240-MP` through commit `fbbcd3c1fecfc56967f43999495c722553d19a5a`, from the shared base `11c215f27b4b55e098881821634e13e95e10044d`. At review time OwlSwitch was 75 commits ahead of that base and upstream was 100 commits ahead, so the safe integration strategy is a behavior-level selective port rather than a merge or broad cherry-pick.

## Adopted

- `fbbcd3c`: removed the unused root `QtQuick.Controls` import. The upstream AppImage environment changes are not applicable to this macOS-only fork, but the generic QML dependency cleanup is. OwlSwitch also adds a release packaging guard that rejects any remaining Qt Quick Controls framework, QML module, or plugin.

## Adapted

- `c45dad6` plus `1f899aa`: global keyboard/remote control remapping and wraparound behavior. OwlSwitch retains upstream's additive safety model but implements it in the existing Objective-C++ macOS `InputManager` rather than importing the SDL/Linux input stack. One canonical action table owns IDs, labels, Qt keys, mpv keys, persistence, capture, duplicate replacement, and reset behavior for every module.

## Already Present Or Superseded

- Native gamepad navigation, Right Shift Back, media controls, held navigation, Settings wraparound, display selection, startup modules, themes, screensaver behavior, crop, output levels, CJK fallback, and mpv lifecycle improvements.
- Jellyfin Quick Connect, collections, SortName ordering, PlaybackInfo negotiation, high-bitrate direct play, transcode track switching, remembered tracks, intro/outro support, progress reporting, and token-safe playback.
- Local subtitle policy/language controls, symlink safety, EOF recovery, images, queues, shuffle/repeat/resume, soundtrack playback, and validated YouTube playlist import.
- OwlSwitch's updater is stricter than upstream's general updater work: it requires digest, notarization, Developer ID team, bundle ID, version, architecture, rollback, and public-artifact checks.

## Deferred

- `2a6e21d`: Plex quick-server switching. A Jellyfin equivalent would require a multi-profile authentication and token-storage design. The maintainer explicitly deferred it because the current release does not need multiple Jellyfin servers.

## Excluded

- `221fd105`: Plex/NFC card playback.
- `a36490e`: Weather unit initialization.
- AppImage, Linux, Raspberry Pi, Xorg, NFC, Weather, Emby, Scripts, standalone YouTube, and Plex-specific authentication/profile/live-TV work.

These exclusions follow OwlSwitch's documented Apple Silicon macOS, Jellyfin-first, fixed-module scope and avoid adding inactive code, unsupported dependencies, or arbitrary script execution.

## Verification Contract

- Compare QML import-scan output before and after the root import removal.
- Require the packaged app to contain no Qt Quick Controls framework, QML directory, or plugin.
- Run focused mapping/capture/persistence/reset/Right Shift tests plus the complete CTest and QML checks.
- Exercise Local, Karaoke, Retro, Jellyfin, diagnostics, signed packaging, the public DMG, and a real 1.6.4 → 1.6.5 install/relaunch path before acceptance.

## Local Evidence

- The source QML import scan changed from the complete Qt Quick Controls/style family to zero Controls imports.
- A fresh release-style package pruned 24 unused QML modules, 51 unused quick plugins, and 28 unreachable Qt frameworks derived from the remaining Mach-O dependency graph. The resulting app contains no Qt Quick Controls path and retains a closed dependency graph across 224 Mach-O files.
- The public 1.6.4 app contained 15 `QtQuickControls*` frameworks plus `QtQuickTemplates2`, totaling 13,007 KiB of apparent file size. The 1.6.5 candidate contains none of them.
- In an alternating ten-run warm launch benchmark, measured from process start to the first module event after QML load, 1.6.4 averaged 649.7 ms and the 1.6.5 candidate averaged 641.8 ms (1.2% faster; medians 649.5 ms and 642.5 ms). This is a modest no-regression result rather than a claim of a large perceptible startup change.
- The isolated release-style bundle validated yt-dlp 2026.08.19 onedir and Deno 2.9.3, then loaded all seven installed manifests successfully after temporary ad-hoc signing for the local smoke test.
