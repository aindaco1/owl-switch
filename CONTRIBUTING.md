# Contributing to OwlSwitch

OwlSwitch is a focused macOS Apple Silicon fork of 240-MP. The repository and compatibility identifiers retain their historical 240-mp-jellyfin names. The goal is to keep the retro CRT-style video controller, keep Local, Retro, Tumblr, Nature, and Karaoke user-facing, and make Jellyfin the primary server-backed integration. Local owns the former Loop behavior.

## Getting Started

1. Follow [BUILDING.md](BUILDING.md) to build and run on macOS.
2. Read [ARCHITECTURE.md](ARCHITECTURE.md) before changing module, playback, settings, or auth behavior.
3. Work on a branch and open a pull request against `main` with a clear description and testing notes.

## Project Principles

1. **macOS only for now.** Do not add Raspberry Pi, Linux, or Intel macOS assumptions unless the supported-platform decision changes.
2. **Keep the retro CRT UI.** Use the existing keyboard-first QML patterns and size with `root.sh` / `root.sw`.
3. **Keep navigation simple.** Core browsing should work with arrow keys, Enter, Escape/Backspace, and Tab where forms need field navigation.
4. **Keep modules self-contained.** QML belongs under `modules/<name>/`; backend code belongs under `src/modules/<name>/`; module registration happens once in `src/main.cpp`.
5. **Do not add tracking or analytics.** Modules may call their intended API directly, but they should not report usage to a service controlled by contributors.
6. **Browse and hand off.** The app browses structured media, then hands playback to mpv. Do not replace mpv with embedded playback without a deliberate architecture decision.

## Current Module Direction

- `jellyfin` is the primary server module.
- `karaoke` (Karaoke), `retro_tv` (Retro), `tumblr_screensaver` (Tumblr), `nature` (Nature), and `local_files` (Local) stay user-facing.
- `plex` remains hidden as a reference until Jellyfin has enough parity to remove it.
- Jellyfin supports movies and full TV browsing/playback, including negotiated direct play/transcoding, progress reporting, next-episode autoplay, remembered track languages, and capability-gated segment skipping.

## Adding Or Changing A Module

A module is a folder under `modules/` with a `manifest.json` and QML views, plus an optional C++ backend.

1. Create `modules/<name>/manifest.json`, assets, and `views/Root.qml`.
2. Follow the router/list/detail patterns in [ARCHITECTURE.md](ARCHITECTURE.md#qml-view-patterns).
3. If the module needs C++, add `src/modules/<name>/<Name>Backend.h/.cpp`, add the `.cpp` to `CMakeLists.txt`, and register the backend once in `src/main.cpp`.
4. Use `appCore.get_setting()` / `save_setting()` for settings. Do not write `config.json` directly from QML.
5. Store module auth/state under the app data directory. Auth files containing tokens should use owner read/write permissions only.

## Code Style

- Match the style of the file you are editing.
- Keep changes scoped to the behavior being changed.
- Prefer existing helpers and module patterns over new abstractions.
- Add a new abstraction only when it removes meaningful duplication or matches an existing local pattern.
- Avoid logging secrets, tokens, passwords, full auth headers, or token-bearing URLs.

## Testing

Before opening a PR, run the checks that apply:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
qmllint -I views Main.qml views/*.qml views/Components/*.qml modules/jellyfin/views/*.qml modules/karaoke/views/*.qml modules/retro_tv/views/*.qml modules/local_files/views/*.qml modules/tumblr_screensaver/views/*.qml modules/nature/views/*.qml
git diff --check
```

Manual checks for media changes:

- Build and run the app on Apple Silicon macOS.
- Navigate without a mouse.
- Confirm Jellyfin login, Continue Watching/Up Next, movie and TV library traversal, filtering, detail loading, track selection, direct playback, transcode fallback, progress reporting, and next-episode behavior.
- Confirm Retro feed loading, channel surfing, filtering, static transitions, and mpv playback.
- Confirm Karaoke automatic/manual catalog refresh, live filtering, duplicate adds, persistence, reorder/move mode, clear confirmation, external-display playback, live queue edits, completed removal, and failed retention.
- Confirm Local browsing, Play Now, track probing, sidecar subtitles, queue/soundtrack add, duplicate persistence, reorder/remove/clear, local playlist expansion, ordered YouTube soundtrack import/reorder/audio-only streaming, media-queue muting, tagged/fallback artist-song overlays at video and soundtrack changes, Repeat Off/Queue/One, shuffle, resume, failed retention, configurable root, separate audio recovery, and saved-queue auto-launch.
- Confirm Tumblr URL loading, favorites persistence/editing, shuffled non-repeating still/GIF playback, pause/resume, and 90s-style transitions.
- Confirm Nature cold load, cached and stale-cache load, next/pause/refresh/source controls, compact name/species/location presentation, comma-separated `City, State/Province, Country` formatting, English place resolution, non-repeating playback, and offline fallback.
- Confirm app settings persist after restart.
- Confirm a current or failed launch update check stays unobtrusive, a newer valid release presents the keyboard-first **View / Later** prompt exactly once, **View** opens the existing Software Update screen, and the manual check remains available.
- For packaging changes, run `cmake --install` into a temporary prefix and verify bundled `mpv`, `ffmpeg`, `ffprobe`, `yt-dlp`, and Deno launch with a stripped `PATH`.
- Before tagging, wait for the exact commit's successful `main` CI run. Releases reuse only its seven-day, provenance-attested unsigned app artifact and still perform fresh signing, notarization, DMG, and downloaded-release verification.

## AI Use

AI-assisted contributions are allowed, but contributors are responsible for understanding and testing the code they submit. Pull requests should describe meaningful AI involvement and the human review/testing performed before submission.

## License

By contributing, you agree your contributions are licensed under GPL-3.0. See [LICENSE](LICENSE).
