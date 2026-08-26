# 240-mp-jellyfin

240-mp-jellyfin is a macOS Apple Silicon fork of 240-MP: a retro VCR-style media shell built with C++ Qt 6 and QML. The fork keeps the CRT/240p-inspired interface, Local playback, Retro decade feeds, and a Tumblr image screensaver, adds Jellyfin as the main server-backed media module, an eighteen-source Karaoke queue, and an iNaturalist-powered Nature montage. Local incorporates the former Loop module's repeatable queue, soundtrack, shuffle, and auto-launch workflows.

The app is a browsing shell, not an embedded video renderer. It launches `mpv` as a subprocess for playback and uses `ffprobe` to inspect local audio/subtitle tracks. CMake supplies pinned standalone `yt-dlp` and Deno helpers for YouTube extraction. Packaged macOS apps also bundle `ffmpeg` for high-quality Karaoke prefetch, along with all required non-system libraries, so end users do not need Homebrew or system helper installs.

## Supported Platform

- Apple Silicon macOS only.
- CMake intentionally fails on non-macOS hosts.
- Intel macOS, Raspberry Pi OS, and Linux packaging are out of scope for this fork.

## Current Modules

The home screen order is Jellyfin, Karaoke, Retro, Tumblr, Nature, then Local.

### Jellyfin

- Password login and Jellyfin Quick Connect.
- Movie libraries and TV show libraries.
- TV browsing through shows, seasons, episodes, then the same metadata, track-selection, and playback flow used for movies.
- Large movie and TV lists load progressively in 250-item pages.
- Live title filter narrows the list as you type.
- Accent-insensitive filtering, so names with characters like `e` and `é` match naturally.
- Direct-play movie and episode playback through mpv.
- Continue Watching and Up Next rows.
- Collection and ordinary folder browsing, with collection items sorted by release date.
- Jellyfin PlaybackInfo negotiation with direct play, direct stream, configurable 480p–1080p transcoding, and automatic transcode retry when direct playback fails.
- Audio and subtitle changes remain available during transcoding by restarting the server transcode at the current position with the newly selected track.
- Playback start, progress, stop, and completion reporting back to Jellyfin.
- Optional next-episode autoplay and server-capability-gated intro/outro skip modes.
- Resume prompt based on Jellyfin playback position.
- Audio and subtitle selection before movie and episode playback, with language preferences remembered across items.
- External Jellyfin subtitle URLs are loaded through authenticated mpv requests without putting access tokens in media URLs.

Not yet implemented: music libraries and explicit watched/unwatched controls from the detail screen.

### Karaoke

- Automatically indexes the Funbox, KaraokeNerds, JLo.Instru, Offbeat Karaoke, Peareoke, Karaoke Office, CCKaraokeX, Nicky Dee Karaoke, Karaoke Balka, Pants Karaoke, Karaokearr, ObsKure, 1Music Karaoke, Janet Email Karaoke, Couch Potato Karaoke, Lemmy Caution Karaoke, Just Sing Karaoke, and KaraFun Karaoke YouTube channels and keeps a persistent 24-hour catalog cache. After the first complete sync, later launches show saved results immediately while stale catalogs reconcile additions, removals, and metadata changes in the background.
- Live accent-insensitive title search with article-insensitive alphabetical results and progressive results during a cold catalog load.
- Cleans `(Funbox Karaoke, YEAR)` to `(YEAR)`; removes provider-specific Karaoke/Instrumental branding, including Karaoke Office's ordinary suffix and verified malformed/inverted aliases, Nicky Dee's parenthesized and Balka's bullet-delimited markers, plain parenthesized or bracketed Karaoke markers from Karaokearr and Pants Karaoke, and Just Sing's English and Portuguese quality/lyrics markers; accepts only KaraFun's current, alternate, and verified legacy karaoke-title grammars, converting its song-first forms to `Artist - Song` and excluding promos or internal/offline rows; converts Pants' quoted performance/byline, parody, live-cover, and attributed cover-version sentences to `Artist - Song (Qualifier)`, canonicalizes its animal-sound Eye of the Tiger uploads, and excludes the one unattributed `25 Minutes or Less` parody, while leaving any previously queued copy editable; strips split CCKaraokeX forms and ObsKure Best Karaoke Version forms; removes all Offbeat key-signature forms while retaining remix/cover qualifiers; converts JLo.Instru's variably spaced `Song - Artist - Instrumental[-Version] - Karaoke[-Lyrics]` conventions to `Artist - Song` while retaining verified artist-first exceptions; strips legacy 1Music `MusicKaraoke`/vocal-removal/instrumental-version/XRINA branding, repairs its unspaced or omitted separators through a centralized artist-prefix list, reorders edition-first titles to `Artist - Song (Edition)`, and collapses redundant `2Pac - Tupac Shakur` aliases; normalizes Janet's em-dash separators and Couch Potato's dash-delimited Karaoke markers; and retains meaningful qualifiers. Lemmy's trailing performance labels and repeated-artist live/year metadata become compact parentheticals such as `(Stop Making Sense)` and `(Live) (1969)`. Shared display cleanup also normalizes square brackets, quoted `"Weird Al"`, leading context tags such as `(Sonic Adventure 2)`, removes redundant leading or trailing `Version` from parenthetical edition labels, moves `YYYY Version; Edit` into `(Edit) (YYYY)`, shortens `7 Inch Version` to `(7")`, and canonicalizes `Featuring`/`Feat`/`Ft` credits as `Ft.` on the artist side, moving misplaced title-side credits there.
- Reconciles equivalent titles across and within sources with case-, accent-, punctuation-, apostrophe-, conjunction-, and article-insensitive matching. Duplicate preference is Funbox, KaraokeNerds, JLo.Instru, Offbeat Karaoke, Peareoke, Karaoke Office, CCKaraokeX, Nicky Dee Karaoke, Karaoke Balka, Pants Karaoke, Karaokearr, ObsKure, 1Music Karaoke, Janet Email Karaoke, Couch Potato Karaoke, Lemmy Caution Karaoke, Just Sing Karaoke, then KaraFun Karaoke.
- One persistent queue with duplicate songs, keyboard reorder, remove, and clear controls.
- Search, add, reorder, and remove remain available on the primary display while mpv plays fullscreen on an external display.
- Artist and song render as readable two-line rows; selecting any queued song and pressing Enter jumps to it immediately.
- While a song plays, the next queued song is downloaded and merged at up to 720p in a bounded persistent cache, then substituted into mpv's live playlist for a fast handoff.
- Retro fade, slide, and falling-block transitions mask the handoff on the media display.
- Completed songs leave the queue; failed songs stay visibly marked for retry or manual removal.
- A manual catalog refresh action is available in Karaoke settings.

### Retro

- MyRetroTVs-backed feeds for the 50s, 60s, 70s, 80s, 90s, and 00s.
- Fullscreen mpv playback of decoded YouTube clips, with no TV-frame overlay.
- Keyboard channel surfing, clip skipping, feed filtering, and decade jumping.
- CRT-style noise, glow, black-and-white, and static transition effects.

### Local

- Two-pane folder browsing and persistent queue editing, modeled on Karaoke's keyboard workflow.
- The first view shows the configurable media directory, defaulting to `~/Desktop`.
- Common video file support: `mp4`, `mkv`, `avi`, `mov`, `m4v`, `webm`, `wmv`, `flv`, `f4v`, `mpg`, `mpeg`, `vob`.
- Still images and common audio files; audio can be collected in an independent soundtrack queue.
- A pasteable YouTube playlist action in the soundtrack pane expands public or unlisted playlists into persistent, individually reorderable video entries in source order and streams them audio-only through the bundled helpers.
- Local-only `m3u` and `m3u8` imports expand into validated, root-contained queue entries with bounded nesting and queue size.
- Persistent, duplicate-friendly media and soundtrack queues with reorder, remove, and clear controls. Completed media remains queued; failed media stays visibly marked.
- Repeat Off, Repeat Queue, Repeat One, queue shuffle, soundtrack shuffle, and optional saved-queue auto-launch.
- Any non-empty soundtrack queue loops in a separate bounded-recovery mpv process while media-queue video audio is muted.
- Resume history.
- Play Now and Add to Queue actions after audio and subtitle selection; queued entries retain their file-specific track choices.
- Sidecar subtitle discovery for common subtitle formats.
- Still-image playback in folders and playlists, configurable image duration, and extension hiding.
- Automatic subtitle policies for preferred language, forced-only, on, or off.
- Standalone playlist playback retains its fixed/ask-at-start shuffle setting.

### Tumblr

- Public Tumblr URL input, defaulting to `https://pixelskylines.tumblr.com/` for quick testing.
- Persistent favorites with normalized URLs, duplicate suppression, one-key launch, and keyboard removal.
- Image discovery through Tumblr's public `/api/read/json` feed pages.
- Animated GIF playback with montage pause/resume support; static images and GIFs share the same transition/player path.
- Fullscreen image montage that shuffles the image deck and does not repeat until every discovered image has been shown.
- Retro 90s-style QML transitions, including falling blocks built from clipped pieces of the incoming image.

### Nature

- Up to 100 recent research-grade, non-captive iNaturalist observations per refresh.
- One CC0 photo per observation, hosted by iNaturalist's HTTPS open-data service. A live policy check still yields the full 100-observation rotation without requiring an attribution line over the image.
- A shuffled, non-repeating montage with only the common name, scientific species name, and an English-preferred `City, State/Province, Country` line visible in a compact overlay. Non-Latin locality names fall back to offline Latin transliteration when iNaturalist has no English place record.
- Keyboard controls for next, pause, refresh, and opening the current source observation.
- A one-hour metadata-only cache that displays saved observations immediately, refreshes stale data in the background, and leaves saved data visible when the network is unavailable. Image files are not persisted.

### Plex

The original Plex module remains in the source tree as a reference implementation, but its manifest is marked hidden so it does not appear in normal module discovery or Settings. It can be removed after Jellyfin reaches the desired parity.

## Build And Run

See [BUILDING.md](BUILDING.md) for the full build, run, packaging, and release workflow.

Quick macOS development build:

```bash
brew install cmake qt mpv ffmpeg
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt .
cmake --build build
APP_ROOT=$(pwd) ./build/240-mp-jellyfin.app/Contents/MacOS/240-mp-jellyfin
```

## Install

See [INSTALL.md](INSTALL.md). [Download 240-mp-jellyfin 1.6.0 for Apple Silicon](https://github.com/aindaco1/240-mp-jellyfin/releases/download/v1.6.0/240-mp-jellyfin-v1.6.0-macOS-arm64.dmg), open the notarized DMG, and drag the app onto its Applications shortcut. If EasyDMG is already configured as the Mac's default DMG handler, the same single-app image can automate that copy; no additional installer is required. Checksums and release notes remain available from [GitHub Releases](https://github.com/aindaco1/240-mp-jellyfin/releases).

The app quietly checks for a newer signed GitHub release whenever it opens. A current or failed check stays out of the way; a valid newer Apple Silicon release presents **View** and **Later**, with **View** opening the existing **Settings → Software Update** screen. The manual check remains available there, and downloading and installation always require user action. The updater verifies GitHub's SHA-256 digest, Apple notarization, the Developer ID team, bundle identity, version, and Apple Silicon architecture before replacing the app.

## Project Docs

- [CHANGELOG.md](CHANGELOG.md) records user-facing changes.
- [ROADMAP.md](ROADMAP.md) tracks planned work and improvement ideas.
- [ARCHITECTURE.md](ARCHITECTURE.md) explains the module, playback, and backend structure.
- [CONTRIBUTING.md](CONTRIBUTING.md) covers contribution and testing expectations.

## Configuration

User configuration is stored outside the app bundle:

```text
~/Library/Application Support/240-mp-jellyfin/
  config.json
  jellyfin_auth.json
  karaoke_catalog.json
  karaoke_queue.json
  karaoke_queue.m3u8
  local_files_history.json
  local_queue.json
  local_queue.m3u8
  nature_observations.json
```

`jellyfin_auth.json` stores the Jellyfin server URL, access token, user ID, username, server identity, and client device ID. Passwords are not persisted. Karaoke files contain public catalog metadata, queue state, and validated canonical YouTube watch URLs; they contain no credentials. Local files contain owner-only resume state, root-contained local paths, queue UUIDs, track choices, validated YouTube video IDs/titles for imported soundtrack entries, and a generated local media playlist. `nature_observations.json` is a bounded metadata-only cache of validated public observations and CC0 photo URLs; image files are not stored.

## Security Notes

- Jellyfin access tokens are stored with owner read/write permissions only.
- Jellyfin playback tokens are sent to mpv through a temporary private mpv config include instead of command-line header arguments.
- Jellyfin stream URLs do not include `api_key` tokens.
- Playback logs redact known token query parameters.
- Local accepts remote soundtrack input only through validated public/unlisted YouTube playlist URLs and persists canonical video identities rather than submitted URLs.
- Nature requests are anonymous and cached Nature photo URLs are revalidated against the CC0 and trusted-host policy before reuse.

## License

This project remains licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.

The bundled VCR OSD Mono font is by Riciery Santos Leal (mrmanet); its license is in [assets/fonts/LICENSE-vcr-osd-mono.txt](assets/fonts/LICENSE-vcr-osd-mono.txt). GNU Unifont provides fallback glyphs for CJK, Hangul, and other scripts under the SIL Open Font License v1.1; see [assets/fonts/LICENSE-unifont.txt](assets/fonts/LICENSE-unifont.txt).

If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.
