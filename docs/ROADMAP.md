# Roadmap

Future work focuses on reliability and depth in the existing Apple Silicon macOS
modules. See [the changelog](CHANGELOG.md) for shipped changes and the
[documentation index](README.md#plans-and-investigations) for scoped work and
historical decisions.

## Next 1.x Priorities

### Jellyfin Polish

- Add explicit watched/unwatched controls on detail screens.
- Add server-side search for exceptionally large libraries while retaining instant local filtering for loaded pages.
- Add a quick server switch without logging out manually.
- Decide whether mixed-video and music-video libraries fit the intentionally small UI.

### Diagnostics And Recovery

- Distinguish helper, authentication, negotiation, network, and codec failures in user-facing playback errors.
- Add a local export action for the same bounded report preview when offline support needs a file handoff.

### First-Run Experience

- Guide new users through Jellyfin sign-in and the optional Local media directory.
- Explain the primary-display controller and external playback display on first use.
- Keep setup recoverable entirely in-app.

### Reliability And Tests

- Extend the existing fake-server Jellyfin backend tests beyond direct/transcode playback and start reporting to cover authentication, paging, and the remaining session lifecycle.
- Establish a QML lint baseline so new runtime warnings are actionable.
- Add a signed-update fixture that exercises DMG identity rejection and rollback behavior in CI.
- Cache Local `ffprobe` results by path and modification time.

### Source Cleanup

- Remove the hidden Plex source after Jellyfin's remaining quick-switch and watched-state parity work is complete.
- Continue consolidating shared list, search, transition, and playback behavior rather than duplicating it per module.
