# Roadmap

Version 1.1 completed the fork's daily-use Apple Silicon macOS foundation. Future work stays focused on reliability and depth in the existing modules rather than expanding platform or module scope.

## Shipped In 1.6.2

- New macOS cassette-and-play app icon with one reproducible iconset source.
- Compact 15-second Local artist/song overlay at every video start and soundtrack change, preferring embedded tags and retaining safe title fallbacks.

## Shipped In 1.5.2

- Local soundtrack mode keeps the main media audio disabled after every queued file reloads its saved track choice.
- Local waits for separate soundtrack playback to begin before starting the media queue, and the updated pinned yt-dlp helper restores current YouTube audio streaming.

## Shipped In 1.5.1

- Local YouTube soundtrack playlist import with persistent ordered/reorderable entries and audio-only streaming through bundled helpers.
- Explicit main-video muting for every non-empty Local soundtrack queue during media-queue playback.

## Prepared For 1.5.0

- A research-grade, CC0-only iNaturalist Nature montage with compact name/species/location display, metadata-only offline cache, and keyboard refresh/source controls.
- A shared, tested image montage/media path used by both Nature and Tumblr instead of parallel transition implementations.
- One consolidated Local module with persistent media/soundtrack queues, Repeat Off/Queue/One, shuffle, auto-launch, per-entry tracks, and the former Loop audio-recovery behavior.

## Shipped In 1.4.0-rc1

- Independent controller/media display selection while preserving the automatic two-display handoff.
- Jellyfin audio/subtitle changes during transcoding, high-bitrate direct-play negotiation, and clearer episode list labels.
- Settings IP visibility, wraparound list navigation, held-key seeking, and CJK/Hangul glyph fallback.

## Shipped In 1.1

- Full Jellyfin TV flow: Continue Watching, Up Next, collections/folders, playback negotiation, quality limits, direct-to-transcode fallback, server progress, next-episode autoplay, remembered languages, and capability-gated intro/outro skipping.
- Shared playback lifecycle and named mpv options used across active modules.
- Local images, playlist-relative media, safer symlink handling, subtitle policies/languages, image duration, extension hiding, and ask-at-playback shuffle.
- Loop shuffle/auto-launch and bounded recovery for separate audio.
- Native macOS GameController navigation, Right Shift Back, media keys, menu/paused screen savers, startup module, themes, crop, and output levels.
- Secure signed in-app updates plus hardened notarized release packaging.
- Tumblr GIF playback and persistent normalized favorites.
- Bounded Loop playback selectors for long video and audio filenames.

## Next 1.x Priorities

### Jellyfin Polish

- Add explicit watched/unwatched controls on detail screens.
- Add server-side search for exceptionally large libraries while retaining instant local filtering for loaded pages.
- Add a quick server switch without logging out manually.
- Decide whether mixed-video and music-video libraries fit the intentionally small UI.

### Diagnostics And Recovery

- Add a diagnostics screen for bundled helper versions, app/config/log paths, display selection, and redacted playback failures.
- Distinguish helper, authentication, negotiation, network, and codec failures in user-facing playback errors.
- Add a redacted copy-diagnostics action.

### First-Run Experience

- Guide new users through Jellyfin sign-in and the optional Local media directory.
- Explain the primary-display controller and external playback display on first use.
- Keep setup recoverable entirely in-app.

### Reliability And Tests

- Add a fake-server Jellyfin backend suite for auth, paging, PlaybackInfo, reporting, and token-free URLs.
- Establish a QML lint baseline so new runtime warnings are actionable.
- Add a signed-update fixture that exercises DMG identity rejection and rollback behavior in CI.
- Cache Local `ffprobe` results by path and modification time.

### Source Cleanup

- Remove the hidden Plex source after Jellyfin's remaining quick-switch and watched-state parity work is complete.
- Continue consolidating shared list, search, transition, and playback behavior rather than duplicating it per module.
