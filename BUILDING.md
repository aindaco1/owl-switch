# Building 240-mp-jellyfin

This fork is macOS-only. CMake intentionally fails configuration on non-macOS hosts.

## macOS (ARM)

### Prerequisites (one-time)

**Set up Build tools:**

```bash
brew install cmake
```

**Install Qt 6.*:**

```bash
brew install qt
```

**Install runtime helpers for development and packaging:**

```bash
brew install mpv ffmpeg
```

240-mp-jellyfin uses mpv as an external subprocess for video playback, `ffprobe` for local audio/subtitle track probing, and `ffmpeg` to merge high-quality Karaoke prefetches. Development runs can use Homebrew copies from `PATH`. Packaged apps embed all three helpers and their non-system dynamic libraries during `cmake --install`.

CMake also downloads pinned Apple Silicon-compatible Deno and standalone universal `yt-dlp` release assets, verifies their SHA-256 checksums, and embeds them with their license files. These power Karaoke catalog extraction and mpv's YouTube handoff without relying on a user's Python, JavaScript runtime, yt-dlp, or Homebrew installation. Maintainers can test a local helper build with `-DYT_DLP_EXECUTABLE_OVERRIDE=/path/to/yt-dlp` or `-DDENO_EXECUTABLE_OVERRIDE=/path/to/deno`; release builds should use the pinned defaults.

Jellyfin playback sends authentication headers to mpv through a temporary owner-only mpv include file. Tokens are not placed in normal Jellyfin stream URLs, and the app's playback launch log redacts known token query parameters.

### Get the source

```bash
git clone <your-fork-url>
cd 240-mp-jellyfin
```

### Build

**First time, and after any CMakeLists.txt changes:**

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt . && cmake --build build
```

**For incremental builds:**

```bash
cmake --build build
```

### Run

The repository Run action and its terminal equivalent build first, stop any prior development instance, and open the app:

```bash
./script/build_and_run.sh
```

You can also double-click `build/240-mp-jellyfin.app` in Finder, or run the executable directly:

```bash
APP_ROOT=$(pwd) ./build/240-mp-jellyfin.app/Contents/MacOS/240-mp-jellyfin
```

### Configuration

On macOS all user configuration is stored at:

```
~/Library/Application Support/240-mp-jellyfin/
  config.json          ← app and module settings
  jellyfin_auth.json   ← Jellyfin auth
  karaoke_catalog.json ← cached public eighteen-source Karaoke catalog (refreshed after 24 hours)
  karaoke_queue.json   ← persistent Karaoke queue
  karaoke_queue.m3u8   ← generated canonical playback URLs
  local_files_history.json ← Local resume history
  local_queue.json     ← persistent Local media and soundtrack queues, including validated YouTube soundtrack entries
  local_queue.m3u8     ← generated root-contained Local playback paths
  nature_observations.json ← cached public iNaturalist metadata (no image files)
```

Tumblr's current URL and favorites are ordinary module settings inside `config.json`; no separate database or credential file is used.

Nature's atomic cache contains at most 100 validated public observation records and CC0 photo URLs. It contains no credentials or downloaded image data and is refreshed after one hour.

This directory is created automatically on first run. It is separate from the app itself, so deleting or rebuilding the app will not wipe your settings.

### App icon

`assets/images/240-mp.iconset/` is the canonical macOS icon source. Regenerate `assets/images/240-mp.icns` with `iconutil -c icns assets/images/240-mp.iconset`; the release uses that ICNS through `MACOSX_BUNDLE_ICON_FILE`, while the authoring iconset is excluded from installed app Resources.

## Debugging & logs

240-mp-jellyfin logs to **stdout/stderr** via Qt's `qDebug` / `qWarning` (used throughout `AppCore`, `MpvController`, and the module backends).

### Running from source

Just run the binary in a terminal and the logs will print right there:

```bash
APP_ROOT=$(pwd) ./build/240-mp-jellyfin.app/Contents/MacOS/240-mp-jellyfin
```

### mpv playback logs

During playback the app hands off to mpv as a subprocess (see [ARCHITECTURE.md → Playback Hand-off](ARCHITECTURE.md#playback-hand-off-mpvcontroller)). `MpvController` writes mpv's own output to a log file in the temp dir alongside its IPC socket (`/tmp/240-mp-jellyfin-mpv.sock`) — useful when a video will not play.

The app also writes a session marker to the same log. Known sensitive URL query keys such as `api_key`, `access_token`, `token`, and `X-Plex-Token` are redacted before that marker is written.

### Qt / QML debugging knobs

These environment variables help when the UI itself is misbehaving:

```bash
QT_LOGGING_RULES="qt.qml.*=true"   # verbose QML engine logging
QML_IMPORT_TRACE=1                 # trace QML import resolution (missing modules/components)
```

Set them inline, e.g. `QML_IMPORT_TRACE=1 APP_ROOT=$(pwd) ./build/240-mp-jellyfin.app/Contents/MacOS/240-mp-jellyfin`.

## GitHub Actions

### Release credentials

The release workflow uses encrypted repository Actions secrets and App Store Connect API-key notarization. Configure these names without committing their values:

- `APPLE_CERT_P12_BASE64`: base64-encoded Developer ID Application certificate archive.
- `APPLE_CERT_PASSWORD`: password for that certificate archive.
- `APPLE_API_KEY_P8_BASE64`: base64-encoded App Store Connect API private key.
- `APPLE_API_KEY_ID`: App Store Connect API key ID.
- `APPLE_API_ISSUER_ID`: App Store Connect API issuer ID.

The workflow writes credentials only under the ephemeral runner temp directory, restricts the API key permissions, and deletes both the temporary keychain and credential files in an `always()` cleanup step. GitHub Actions are pinned to reviewed commit SHAs.

### How to trigger a build

Releases are built automatically when you push a version tag:

```bash
git tag v1.1.0
git push origin v1.1.0
```

And you can use pre-release tags to test CI without making a public release:

```bash
git tag v1.1.0-rc1
git push origin v1.1.0-rc1
```

Before pushing the tag, wait for the exact commit's `main` CI run to succeed.
That run assembles, verifies, provenance-attests, and retains the unsigned
release app for seven days. The release refuses a different commit, a failed or
non-`main` run, a self-hosted provenance claim, modified packaging scripts, an
unsafe archive, or altered bundle contents instead of silently rebuilding.

Tags containing `-rc`, `-beta`, or `-alpha` are published as GitHub pre-releases.
The numeric portion of the tag must match the `project(...)` version in `CMakeLists.txt`, and the full tag must match `APP_RELEASE_TAG`; CI rejects either mismatch before building. Public release notes are generated from the full tag's section in [CHANGELOG.md](CHANGELOG.md), so bump both declarations and finalize the matching dated changelog entry before tagging.

If a tag push does not produce an Actions run, use **Actions → Release → Run workflow** and enter the existing tag. The manual path checks out that tag, requires its retained exact-commit CI app, and runs the same version validation, signing, notarization, and publication steps; it does not build arbitrary untagged source or bypass CI evidence.

### What the workflow does

The intended workflow is a macOS Apple Silicon build:

| Job | Runner | Output |
|---|---|---|
| `build-macos-arm64` | `macos-26` (arm64) | Notarized app artifact |
| `package-macos-arm64` | `macos-26` (arm64) | `240-mp-jellyfin-<tag>-macOS-arm64.dmg` |

The exact-commit `main` CI job pins Xcode 26.3, installs Qt and the media helpers
from the Apple Silicon runner's Homebrew snapshot, configures CMake for `arm64`,
downloads and verifies pinned yt-dlp/Deno, builds and tests, embeds all helpers,
runs `macdeployqt` without its temporary ad-hoc signing, prunes unused QML
plugins, and verifies the unsigned bundle. CI records a complete file, mode,
hash, and contained-symlink manifest, packages the app as a seven-day internal
artifact, and issues GitHub-hosted build provenance for that exact source
commit. The release downloads only that artifact from the exact successful
`main` CI run, verifies its provenance, metadata, packaging-contract hash,
bundle manifest, app identity/version/architecture, helper versions, and every
Mach-O dependency before signing. It then performs the same stripped-runtime
checks (including one live extraction from each Karaoke source, one Local
playlist expansion canary, and one actual Local YouTube audio-stream attempt),
Developer-ID signs, notarizes, and staples the app. Build/test/deployment work
is not repeated after tagging, while signing and all distribution trust checks
remain fresh.

The notarized app crosses jobs only as a `ditto` ZIP so signatures,
entitlements, and the stapled ticket survive intact. A fresh macOS 26 job
verifies that sealed app, stages exactly the app plus an `/Applications`
shortcut, validates that layout with `scripts/macos_dmg_contract.zsh`, downloads
checksum-pinned `rcodesign` 0.29.0, creates and Developer-ID signs the DMG,
verifies the embedded signing certificate's team against the app, validates the
image checksum, notarizes and staples it, mounts it read-only to recheck the
shared layout and app contract, validates Gatekeeper acceptance, and publishes
the DMG plus its SHA-256 checksum.

The in-app updater consumes the same release. GitHub's API asset digest is mandatory, and the downloaded bundle must pass notarization, signature-team, bundle-ID, version, and arm64 checks before installation.

### DMG signing/notarization recovery runbook

The normal release workflow is canonical. Use this recovery only when the app job has completed and produced `notarized-app-arm64`, but the packaging job fails while signing the outer DMG on a hosted runner.

This failure occurred during the 1.1 and 1.2 release investigations: Apple's `codesign` signed and notarized the app successfully, then returned either `No such process` or `host has no guest with the requested attributes` while signing an otherwise valid DMG on hosted macOS runners. The failure reproduced on clean macOS 15 and macOS 26 arm64 runners, and `codesign` returned the same guest/host error when asked only to display a DMG that `rcodesign` had successfully signed. The normal workflow therefore uses the checksum-pinned open-source `rcodesign` implementation only for the outer DMG and its pre-submission signature report. That report must show a valid cryptographic signature, an Apple-rooted Developer ID Application certificate for the same team as the app, and an Apple timestamp. Apple's notarization service then validates the submitted DMG, Apple's tools staple and validate the ticket, and Gatekeeper must accept the final image. The earlier 1.0 DMGs had actually been packaged locally; the `1.0` tag did not match the old workflow's `v*.*` trigger.

Keep these failure modes separate:

- The pinned signer's signature report or `hdiutil verify` fails: the DMG is invalid; rebuild it. Do not submit or publish it.
- `rcodesign` cannot produce a valid, timestamped Developer ID signature with the expected certificate team, or Apple rejects that signature during notarization: recover by signing the DMG on a trusted maintainer Mac.
- `notarytool submit` prints a submission ID but never prints `Successfully uploaded file`, or exits with signal 10/138: the upload did not finish. An indefinitely `In Progress` record is not evidence that Apple is still scanning it.
- `notarytool` reports `Network.NWError error 54` / `Connection reset by peer`: retry from GitHub Actions rather than repeatedly creating incomplete local submissions.
- `gh release upload` reports `tls: bad record MAC`: retry the upload over HTTP/1.1 with `GODEBUG=http2client=0 gh release upload ...`.

The recovery path deliberately separates the two operations at the failure boundary:

1. Download the exact `notarized-app-arm64` artifact from the failed release run.
2. On a trusted maintainer Mac, extract it with `ditto`, validate the app with `codesign --verify --deep --strict` and `xcrun stapler validate`, stage the app plus an exact `Applications -> /Applications` shortcut, validate that staging directory with `scripts/macos_dmg_contract.zsh validate-layout`, create a UDZO DMG with `hdiutil create`, and Developer-ID sign the DMG with a secure timestamp.
3. Validate the candidate before it leaves the Mac:

   ```bash
   hdiutil verify 240-mp-jellyfin-v1.1.0-macOS-arm64.dmg
   codesign --verify --verbose=4 240-mp-jellyfin-v1.1.0-macOS-arm64.dmg
   codesign -dvvv 240-mp-jellyfin-v1.1.0-macOS-arm64.dmg
   ```

   Confirm the output shows the expected Developer ID team, a `Timestamp=...` line, and no verification error.

4. Upload only that signed, verified DMG to a **draft** GitHub release for the existing tag. Never publish the unstapled recovery asset.
5. Run **Actions → Recover DMG Notarization → Run workflow**, entering that tag. The recovery workflow refuses a non-draft release or a mismatched tag/version. It downloads the exact draft asset, verifies the DMG and enclosed app (signature, stapled app ticket, team, bundle ID, version, and arm64 architecture), submits the DMG from GitHub's macOS runner, staples and validates it, replaces the draft asset, creates its SHA-256 file, and only then publishes the release.
6. Download the published DMG and checksum into a clean directory and repeat `shasum -a 256 -c`, `hdiutil verify`, `codesign --verify`, `xcrun stapler validate`, and the enclosed-app checks. This last download tests what users and the in-app updater will actually receive.

Do not weaken or skip signing/notarization checks to make a release green. Keep the draft private until the recovery workflow has replaced the asset with the stapled DMG and all validations pass.

## Local Verification

Recommended checks before committing code changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
qmllint -I views Main.qml views/*.qml views/Components/*.qml modules/jellyfin/views/*.qml modules/karaoke/views/*.qml modules/retro_tv/views/*.qml modules/local_files/views/*.qml modules/tumblr_screensaver/views/*.qml modules/nature/views/*.qml
git diff --check
```

The Karaoke backend suite skips its network integration case by default. Run it explicitly when changing the channel extractor or pinned helpers:

```bash
KARAOKE_LIVE_TEST=1 ./build/karaoke_backend_tests refreshesLiveCatalog
KARAOKE_LIVE_TEST=1 ./build/karaoke_backend_tests prefetchesLivePlaybackMedia
```

Local's backend suite skips its public YouTube playlist canary by default. Run it when changing playlist validation, expansion, or the pinned helpers:

```bash
LOCAL_FILES_LIVE_TEST=1 ./build/local_files_backend_tests importsLiveYouTubePlaylist
```

Nature's backend suite also skips its real-service canary by default. Run it when changing the request, license, or response policy:

```bash
NATURE_LIVE_TEST=1 ./build/nature_backend_tests fetchesLiveObservations
```

For packaging changes, also run a local install into a temporary prefix and confirm bundled helpers launch:

```bash
cmake --install build --prefix /tmp/240mp-jellyfin-install-test
/tmp/240mp-jellyfin-install-test/240-mp-jellyfin.app/Contents/Resources/bin/mpv --version
/tmp/240mp-jellyfin-install-test/240-mp-jellyfin.app/Contents/Resources/bin/ffmpeg -version
/tmp/240mp-jellyfin-install-test/240-mp-jellyfin.app/Contents/Resources/bin/ffprobe -version
/tmp/240mp-jellyfin-install-test/240-mp-jellyfin.app/Contents/Resources/bin/yt-dlp --version
/tmp/240mp-jellyfin-install-test/240-mp-jellyfin.app/Contents/Resources/bin/deno --version
```

Run the DMG layout fixtures independently when changing packaging or release workflows:

```bash
/bin/zsh tests/MacosDmgContractTest.zsh "$PWD/scripts/macos_dmg_contract.zsh"
```

These fixtures validate staging and layout policy only. A local unsigned image is not evidence that the release is Developer-ID signed or notarized; use a release-candidate tag to exercise the complete distribution workflow.

After running `macdeployqt`, use `scripts/macos_prune_qt_deployment.zsh` to retain only QML plugins found by `qmlimportscanner`, then run `scripts/macos_verify_bundle.zsh` before signing. The release workflow is the canonical invocation for both scripts.

### Cleaning Generated Artifacts

Preview ignored build output before removing it:

```bash
git clean -ndX
```

When the preview contains only disposable generated files, remove them and recreate the single development tree:

```bash
git clean -fdX
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt .
cmake --build build
```

This retains tracked development scripts, CMake helpers, entitlements, source, and tests while removing old build/package trees, DMGs, logs, caches, and Finder metadata.

When applying hardened-runtime signatures, preserve the bundled Deno binary's
upstream entitlements. Sign standalone yt-dlp with
`packaging/yt-dlp.entitlements` because its PyInstaller launcher extracts and
loads its embedded Python runtime dynamically. Sign mpv with
`packaging/mpv.entitlements` because its LuaJIT engine generates executable
memory when loading playback scripts.

### Output

**While the workflow is running:**

Go to **Actions** → select the workflow run → each build job has an **Artifacts** section at the bottom where you can download that job's output before the release is published.

**After the workflow completes:**

Go to the repository on GitHub → **Releases** → select the release for the tag you set. The DMG is listed under Assets.
