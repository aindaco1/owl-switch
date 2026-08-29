# OwlSwitch Development Guidelines

OwlSwitch is a macOS Apple Silicon fork of 240-MP. The repository, checkout, build target, module IDs, Application Support path, and future release artifacts use the OwlSwitch identity. Hidden legacy aliases and the existing signed bundle identifier remain only for compatibility with already-released updaters. It keeps the retro VHS/CRT-style Qt 6 + QML video controller, keeps Local, Retro, Tumblr, and Nature, hides Plex, adds Jellyfin as the primary server-backed module, and adds a multi-source Karaoke queue. Local incorporates the former Loop behavior.

**Playback engine**: the app launches `mpv` as a subprocess through `MpvController`. Local track probing uses `ffprobe`. CMake downloads a pinned, checksum-verified official yt-dlp onedir runtime and Deno for YouTube extraction. Packaged macOS apps bundle all helpers; end users do not need system copies.

---

## Build & Run

```bash
# First time / after CMakeLists.txt changes:
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt . && cmake --build build

# Incremental:
cmake --build build

# Run:
APP_ROOT=$(pwd) ./build/OwlSwitch.app/Contents/MacOS/OwlSwitch
```

For packaging, CI, and config paths, see **[BUILDING.md](BUILDING.md)** and **[INSTALL.md](INSTALL.md)**.

---

## Where Things Live

| If you need... | Read |
|---|---|
| Architecture, module anatomy, `manifest.json`, `AppCore`, `registerModule`, backends, QML navigation, playback, config shape | **[ARCHITECTURE.md](ARCHITECTURE.md)** |
| Contribution principles, testing, coding style | **[CONTRIBUTING.md](CONTRIBUTING.md)** |
| macOS build, run, package, release workflow, config paths | **[BUILDING.md](BUILDING.md)** |
| End-user macOS install/update/uninstall | **[INSTALL.md](INSTALL.md)** |
| Token handling and security-sensitive areas | **[SECURITY.md](SECURITY.md)** |
| User-facing change history | **[CHANGELOG.md](CHANGELOG.md)** |
| Planned work and improvement ideas | **[ROADMAP.md](ROADMAP.md)** |

---

## Key Facts

- CMake intentionally fails on non-macOS hosts.
- User-facing modules are `jellyfin`, `karaoke`, `retro_tv`, `tumblr_screensaver`, `nature`, and `local_files`, displayed in that order as Jellyfin, Karaoke, Retro, Tumblr, Nature, and Local.
- Plex remains in the source tree but is hidden by `modules/plex/manifest.json`.
- Modules are discovered from `modules/*/manifest.json`; a backend module adds one `registerModule(...)` call in `src/main.cpp`.
- `registerModule` wires optional backend signals/slots by introspection: `dynamicOptionsReady`, `authStateChanged`, and `onSettingChanged`.
- Every module's QML entry point is `Root.qml`. Views are `FocusScope`s that pass state via `navParams` and communicate through `navigateTo` / `goBack`.
- Size QML layouts with `root.sh` / `root.sw`.
- Config is `config.json` under `~/Library/Application Support/owl-switch/`; the first future build migrates the legacy directory atomically when possible.
- Controller and media display roles are selected independently; automatic keeps the controller on the primary screen and media on the first other screen, while explicit changes take effect after restart.
- Jellyfin auth is `jellyfin_auth.json`; passwords are never persisted.
- Karaoke stores a non-secret 24-hour catalog cache, persistent queue, and generated playback playlist under the same app data directory.
- Local stores owner-only resume history plus persistent media/soundtrack queues. Local queue paths must remain under its configurable media root (default `~/Desktop`); soundtrack-only YouTube imports retain validated canonical video identities and titles. While soundtrack-backed video plays, bounded artist/song metadata appears for 15 seconds at each video start and soundtrack change, preferring embedded tags before safe title fallbacks. Loop's retired directory is not migrated.
- Tumblr stores its current URL and normalized favorites list in `config.json`; Tumblr and Nature share the tested `ImageMontage.qml` / `MontageMedia.qml` still/GIF transition path.
- Nature anonymously fetches up to 100 research-grade iNaturalist observations, uses one capped Places batch to prefer English public place names, accepts only independently validated CC0 photos from the HTTPS open-data host, exposes only name/species and comma-separated `City, State/Province, Country` in its compact overlay, and keeps an owner-only metadata cache without image files or place IDs.
- Signed releases update through `UpdateManager`; never weaken its SHA-256, notarization, Developer ID team, bundle ID, version, or arm64 checks.
- Jellyfin TV playback negotiates PlaybackInfo, reports session progress, can retry through transcoding, and optionally uses server Media Segments for intro/outro skipping.
- Do not log tokens, passwords, full auth headers, or token-bearing URLs.
