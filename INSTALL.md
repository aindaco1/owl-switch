# Install OwlSwitch

OwlSwitch is distributed as `OwlSwitch.app` inside a macOS Apple Silicon DMG. Version 1.6.4 keeps a hidden `240-mp-jellyfin.app` alias in the DMG so older signed updaters can locate the renamed bundle without changing its trusted identity or user-data location.

## Requirements

- Apple Silicon Mac.
- macOS current enough to run the Qt 6 app bundle.
- Network access to your Jellyfin server for Jellyfin playback, to YouTube for Karaoke/Retro playback and imported Local soundtracks, and to iNaturalist for fresh Nature observations. Nature can reuse its saved metadata while offline.

The packaged app bundles `mpv`, `ffmpeg`, `ffprobe`, the pinned official yt-dlp onedir runtime, pinned Deno, and required non-system dynamic libraries. End users do not need to install Homebrew, Python, mpv, FFmpeg, yt-dlp, or a JavaScript runtime.

## Install

1. Download the latest `240-mp-jellyfin-<version>-macOS-arm64.dmg` from [GitHub Releases](https://github.com/aindaco1/240-mp-jellyfin/releases/latest).
2. Open the DMG.
3. Drag `OwlSwitch.app` onto the Applications shortcut in the same window.
4. Launch `OwlSwitch.app`.

The notarized DMG presents the app and the standard `/Applications` shortcut. A hidden, bundle-local compatibility alias exists only for older automatic updaters. If EasyDMG is already configured as the Mac's default DMG handler, opening the same image can automate the copy. EasyDMG is optional and is not an application dependency.

The app opens as a full-screen, keyboard-first media interface.

## Update

The app quietly checks GitHub for a newer release whenever it opens. Current and failed checks do not interrupt the app. When a valid newer Apple Silicon release is available, choose **View** to open **Settings → Software Update**, or **Later** to dismiss the prompt. The same screen retains **Check for Updates** as a manual fallback.

Downloading and installation remain user initiated. A signed app in a writable Applications folder can download, verify, install, and restart after approval. If the running app is a development build or its install location cannot be replaced safely, the same screen verifies and opens the DMG for manual installation.

The updater rejects releases without GitHub's SHA-256 asset digest. It also requires Apple code-signature and notarization checks, the same Developer ID team as the running app, bundle identifier `com.240mp.jellyfin`, the advertised version, and an Apple Silicon executable.

Manual updating remains supported:

1. Download the newer DMG and its `.sha256` file from GitHub Releases.
2. Verify the checksum with `shasum -a 256 -c <downloaded-file>.sha256`.
3. Quit the older app, remove a legacy `/Applications/240-mp-jellyfin.app` copy if present, then drag `OwlSwitch.app` onto the Applications shortcut.

Your settings, Jellyfin authentication, Karaoke catalog cache and queue, Local queues (including imported YouTube soundtrack entries) and resume history, and Nature observation metadata cache are kept in `~/Library/Application Support/240-mp-jellyfin/`, so replacing the app bundle does not erase them.

## Uninstall

Remove the application:

```bash
rm -rf /Applications/OwlSwitch.app
```

Optionally remove user configuration and Jellyfin auth:

```bash
rm -rf "$HOME/Library/Application Support/240-mp-jellyfin"
```

## Development Installs

Development builds can be run directly from the build directory:

```bash
APP_ROOT=$(pwd) ./build/OwlSwitch.app/Contents/MacOS/OwlSwitch
```

For development prerequisites and packaging steps, see [BUILDING.md](BUILDING.md).
