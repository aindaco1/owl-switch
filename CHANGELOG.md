# Changelog

All notable OwlSwitch changes are documented here. Releases through 1.6.3 used the 240-mp-jellyfin product name; 1.6.4 introduces the renamed bundle while retaining compatibility aliases and trusted identifiers.

## [Unreleased]

## [1.6.4] - 2026-08-29

### Added

- Renamed the product and physical bundle to **OwlSwitch**, a retro video controller inspired by “the owls are not what they seem,” while preserving a hidden legacy updater alias, the existing identifier, updater channel, and Application Support directory.
- Added a Settings diagnostics screen with owner-only rotating structured logs, strict path/URL/email/credential redaction, a bounded report preview, explicit send/clear actions, and no automatic reporting.
- Added a separately deployable Cloudflare diagnostics relay that independently validates, sanitizes, rate-limits, fingerprints, and aggregates matching user reports into GitHub issues using a short-lived GitHub App installation token.
- Added one source-controlled helper manifest and reusable Karaoke, Local playlist, audio, and 720p video YouTube canaries for packaged-app verification.

### Changed

- Replaced yt-dlp's self-extracting executable with the official checksum-pinned onedir archive and added one local, no-network helper warm-up after the controller becomes interactive.
- Centralized YouTube input identity, video/audio format profiles, subprocess limits, cancellation, sanitization, and mpv arguments across Karaoke, Retro, and Local.
- Karaoke now paints its first frame before cache parsing, searches and sorts in C++, pages only 250 results into QML, and exposes progressive cold-refresh results in larger bounded batches.
- Karaoke now labels the initial catalog state as loading instead of briefly displaying `0/0`; Right Shift works as a queue-reorder modifier and triggers Back only when tapped by itself.
- Diagnostics omit routine mpv progress and normal quit telemetry while retaining actionable playback warnings and failures.
- Local YouTube playlist import persists the first valid song immediately and continues adding bounded batches in source order while extraction remains active.
- Retro keeps one mpv process alive and replaces clips/channels through IPC instead of restarting the player for every switch.

### Security

- The helper manifest records the exact yt-dlp/Deno versions, archive checksums, installed paths, and onedir layout. Release signing now covers every nested Mach-O file in the yt-dlp runtime.
- Diagnostic payloads are capped at 20 reviewed structured events and are sanitized on both sides of the network boundary. The desktop app contains no GitHub credentials.

## [1.6.3] - 2026-08-28

### Added

- Added a Karaoke `UP NEXT` card for the next queued song during the first and last 15 seconds of each video. It follows live queue edits, stays visible across overlapping windows in videos shorter than 30 seconds, and stays hidden when no next song exists.

### Changed

- Reworked Local's bottom-right soundtrack card into the shared track-overlay style used by Karaoke, with a larger, higher-contrast background and substantially larger artist/title text centered within the card. Long metadata remains bounded, escaped, and adaptively sized.

## [1.6.2] - 2026-08-26

### Added

- Added a compact bottom-right Local soundtrack overlay over video playback. It appears for 15 seconds when each video begins and restarts for 15 seconds whenever the soundtrack advances to a new song, using embedded audio tags such as ID3 artist/title metadata before falling back to `Artist - Song` names and then `SOUNDTRACK` plus the cleaned title.

### Changed

- Replaced the macOS app icon with the new cassette-and-play design and retained its complete iconset as the single reproducible source for the bundled ICNS.

## [1.6.1] - 2026-08-26

### Changed

- Reduced tag-to-release build time by reusing the exact unsigned app already built, tested, deployed, and verified by the successful `main` CI run. The handoff is commit-bound, short-lived, GitHub-provenance-attested, safely extracted, content-manifested, and reverified before the unchanged Developer ID signing, notarization, DMG, helper, and updater gates run.

## [1.6.0] - 2026-08-26

### Added

- Added one quiet GitHub release check whenever the app opens, following Podcast Visualizer 1.2.2's reviewed launch-update pattern. Current and failed checks stay unobtrusive; a valid newer Apple Silicon release presents a keyboard-first prompt that opens the existing Software Update screen, while downloading and installation remain user initiated.

## [1.5.2] - 2026-08-20

### Fixed

- Kept Local's main media audio disabled after each queued file loads; reapplying a file's saved audio-track choice can no longer override soundtrack mode and re-enable the video's audio.
- Updated the pinned yt-dlp helper for current YouTube playback, made Local wait for the soundtrack's actual mpv playback event before starting the media queue, and added a signed-bundle canary that attempts to open a real YouTube audio stream rather than checking playlist metadata alone.

## [1.5.1] - 2026-08-19

### Added

- Added a pasteable YouTube playlist action to Local's soundtrack pane. Public and unlisted playlists expand through the pinned yt-dlp/Deno helpers into persistent, individually reorderable entries that retain the playlist's source order and stream audio on demand.

### Fixed

- Made Local media-queue playback derive its main-video mute decision directly from whether the soundtrack queue is non-empty, with regression coverage through the backend playback plan and mpv's `--no-audio` launch argument.
- Restored Local's queue keyboard focus after a YouTube playlist import completes or is canceled.

### Security

- Restricted Local's new remote soundtrack support to validated HTTPS YouTube playlist inputs and canonical per-video watch URLs. Imported IDs, titles, output size, queue capacity, helper runtime, and persisted state remain bounded; arbitrary remote Local playlist entries are still rejected, and helper diagnostics that may echo submitted URLs are discarded.

## [1.5.0] - 2026-08-18

### Added

- Added a keyboard-first Nature module that cycles through up to 100 recent research-grade iNaturalist observations, showing only the common name, scientific species name, and an English-preferred `City, State/Province, Country` line in a compact overlay.
- Added a schema-versioned, owner-only metadata cache with one-hour freshness, stale-while-refresh behavior, atomic writes, bounded payloads, and an opt-in live iNaturalist integration test.
- Added persistent duplicate-friendly media and soundtrack queues to Local, with bounded local playlist expansion, reorder/remove/clear controls, Repeat Off/Queue/One, shuffle, optional saved-queue auto-launch, retained completed entries, and visible failed entries.
- Added per-entry Local audio/subtitle choices, queue resume, and a shared bounded playback selector component.

### Changed

- Extracted Tumblr's shuffled, non-repeating still/GIF montage into shared `ImageMontage` and `MontageMedia` components, then migrated Tumblr and Nature to the same tested transition and failed-image handling path.
- Consolidated Loop into Local while preserving its independently looping soundtrack and bounded audio-process recovery; `~/Desktop` remains the default Local media root and can still be changed in Settings.

### Security

- Nature makes anonymous, bounded API requests, rate-limits refreshes, resolves English place names with one capped iNaturalist Places batch when available, and uses offline transliteration for remaining non-Latin locality names. It accepts only research-grade non-captive observations and independently revalidates every photo as CC0 on iNaturalist's HTTPS open-data host before display or cache reuse. It stores normalized public place components rather than coordinates and never caches image files.
- Local queue state is schema- and size-bounded, atomically written owner-only, limited to validated paths under the configured media root, and rejects remote playlist entries, newline path injection, cyclic playlists, and excessive nesting.

### Removed

- Removed the standalone Loop module, backend, settings surface, and ambient-only OSC; its maintained behavior now lives in Local without a duplicate Home entry.

## [1.4.1] - 2026-08-17

### Added

- Added independent Controller Display and Media Display selectors. Automatic preserves the existing primary-controller/other-display media handoff, while either role can target a specific attached display and media can explicitly share the controller display after restart.
- Added in-playback audio and subtitle switching for Jellyfin transcodes. The OSD closes the active server session, requests the next selected track, and resumes the new HLS transcode at the current position.
- Added the preferred local IPv4 address to Settings, episode codes to Jellyfin episode rows, wraparound navigation across the app shell and active Jellyfin/Local lists, repeatable held-key OSD seeking, and a GNU Unifont fallback for CJK, Hangul, and other glyphs missing from VCR OSD Mono.

### Fixed

- Fixed Karaoke playback being hidden on the media display by keeping the auxiliary Qt output window open only while a transition is visible.
- Fixed Retro channels failing on stale YouTube stream selections by making yt-dlp validate candidate formats before handing them to mpv.
- Fixed Jellyfin direct play for high-bitrate media by sending an explicit 1000 Mbps streaming ceiling instead of omitting `MaxStreamingBitrate`.
- Fixed Settings scrolling after wraparound and bounded long display names with the same selected-row marquee behavior used elsewhere.

### Changed

- Selectively incorporated the relevant macOS, navigation, and Jellyfin improvements from upstream [240-MP v2026.08.17](https://github.com/anthonycaccese/240-MP/releases/tag/v2026.08.17). The upstream Scripts module, Plex PIN profiles, non-macOS platform work, and unrelated new modules remain out of scope for this fork.
- Release metadata declares the exact release tag separately from the numeric bundle version, and CI rejects a tag that disagrees with either value.
- Added an Apple Silicon pull-request CI gate for release builds, the complete CTest suite, QML lint, and an mpv OSD smoke test before changes reach the tag-triggered signing workflow.

## [1.4.0-rc2] - 2026-08-17

### Fixed

- Fixed Karaoke playback being hidden on the media display by keeping the auxiliary Qt output window open only while a transition is visible.
- Fixed Retro channels failing on stale YouTube stream selections by making yt-dlp validate candidate formats before handing them to mpv.

## [1.4.0-rc1] - 2026-08-17

### Added

- Added independent Controller Display and Media Display selectors. Automatic preserves the existing primary-controller/other-display media handoff, while either role can target a specific attached display and media can explicitly share the controller display after restart.
- Added in-playback audio and subtitle switching for Jellyfin transcodes. The OSD closes the active server session, requests the next selected track, and resumes the new HLS transcode at the current position.
- Added the preferred local IPv4 address to Settings, episode codes to Jellyfin episode rows, wraparound navigation across the app shell and active Jellyfin/Local lists, repeatable held-key OSD seeking, and a GNU Unifont fallback for CJK, Hangul, and other glyphs missing from VCR OSD Mono.

### Fixed

- Fixed Jellyfin direct play for high-bitrate media by sending an explicit 1000 Mbps streaming ceiling instead of omitting `MaxStreamingBitrate`.
- Fixed Settings scrolling after wraparound and bounded long display names with the same selected-row marquee behavior used elsewhere.

### Changed

- Selectively incorporated the relevant macOS, navigation, and Jellyfin improvements from upstream [240-MP v2026.08.17](https://github.com/anthonycaccese/240-MP/releases/tag/v2026.08.17). The upstream Scripts module, Plex PIN profiles, non-macOS platform work, and unrelated new modules remain out of scope for this fork.
- Release metadata now declares the exact prerelease tag separately from the numeric bundle version, and CI rejects a tag that disagrees with either value.
- Added an Apple Silicon pull-request CI gate for release builds, the complete CTest suite, QML lint, and an mpv OSD smoke test before changes reach the tag-triggered signing workflow.

## [1.3.1] - 2026-08-17

### Added

- Added a standard Applications shortcut to the signed DMG for a clear drag-to-install flow and optional compatibility with cautious single-app DMG handlers such as EasyDMG.
- Added one fail-closed DMG contract shared by release staging, final notarized-image verification, and the recovery workflow. It requires the exact two-entry layout and rechecks image integrity, app identity, Developer ID team, version, Apple Silicon architecture, signatures, stapled tickets, and Gatekeeper acceptance before publication.

## [1.3.0] - 2026-08-15

### Added

- Added KaraFun Karaoke as the eighteenth and least-preferred Karaoke catalog source, with shared title parsing that excludes non-karaoke uploads and normalizes current, alternate, and verified legacy title formats.

## [1.2.0] - 2026-07-29

### Added

- Added Just Sing Karaoke as the seventeenth Karaoke catalog source and lowest duplicate priority, with shared source-registry cache migration and cleanup for its English and Portuguese Karaoke markers.

## [1.1.0] - 2026-07-16

### Added

- Kept long Loop video and audio filenames inside their playback selectors with a shared bounded row layout and middle elision that preserves recognizable filename beginnings and extensions.
- Added animated GIF playback to Tumblr montages through a shared static/animated media component; GIF sources are preserved during feed extraction, pause with the montage, and use transitions that keep animation running.
- Added a persistent Tumblr favorites list with URL normalization, duplicate suppression, quick launch, Save/Remove Favorite controls, and keyboard deletion. QML list/object settings now serialize through the shared `AppCore` JSON path instead of becoming `null`.
- Ported the relevant upstream 240-MP core improvements for 1.1 without bringing over new modules or non-macOS platform code: unified mpv completion reporting, native GameController navigation, Right Shift as Back, media-key playback controls, menu/paused-playback screen savers, startup-module selection, multiple custom themes, SMPTE colors, auto-crop, output-level controls, directory reset, and settings help/capability filtering.
- Added a full Jellyfin TV workflow with Continue Watching and Up Next, series/season/episode browsing, collection and folder traversal, release-date collection sorting, PlaybackInfo negotiation, selectable transcode quality, direct-play failure fallback, playback start/progress/stop reporting, next-episode autoplay, persistent language preferences, and optional intro/outro skip controls when the server supports Media Segments.
- Added secure in-app update checks and installation from GitHub Releases. Downloads require GitHub's SHA-256 asset digest, a valid notarized Apple disk image and app, the same Developer ID team as the running app, the expected bundle identifier/version, and an Apple Silicon executable; replacement is rollback-safe and manual DMG opening remains available when the app is not installed in a writable location.
- Added Local image playback, playlist-relative image handling, safer root-contained symlink browsing, forced/preferred subtitle policies, language selection, configurable still-image duration, extension hiding, and an ask-at-playback shuffle mode.
- Added Loop video/audio shuffle and auto-launch settings plus bounded separate-audio restart recovery, using the shared bundled-helper resolution path.
- Added app-shell, Local path-policy, and updater version tests alongside the existing Karaoke and Loop suites.
- Hardened release automation with commit-pinned Actions, least-privilege permissions, tag/version validation, App Store Connect API-key notarization, ephemeral credential cleanup, changelog-derived release notes, SHA-256 assets, and notarization validation for both the app bundle and final disk image.
- Made Qt's generated MOC include paths reproducible so clean out-of-tree builds also work when macOS resolves the build directory through a symlink such as `/tmp` to `/private/tmp`.

## [1.0] - 2026-06-12

### Added

- Added a Funbox Karaoke module with automatic 24-hour catalog refresh, progressive live search, a persistent duplicate-friendly queue, easy clear/remove/reorder controls, and a manual refresh setting.
- Added KaraokeNerds, Peareoke, CCKaraokeX, and ObsKure as additional Karaoke catalog sources, including source-specific title cleanup and ranked cross-source deduplication (Funbox, KaraokeNerds, Peareoke, CCKaraokeX, ObsKure) that tolerates case, accents, punctuation, and missing articles.
- Added Lemmy Caution Karaoke plus 1Music Karaoke, Janet Email Karaoke, and Couch Potato Karaoke, expanding the catalog to nine sources with source-specific cleanup and automatic legacy-cache migration.
- Added JLo.Instru and Offbeat Karaoke, expanding the catalog to eleven sources with song/artist reordering, Karaoke Version/key-signature cleanup, meaningful qualifier retention, and nine-source-cache migration.
- Added Pants Karaoke and Karaokearr, expanding the catalog to thirteen sources with bracketed/parenthesized Karaoke cleanup, the requested duplicate priority, and immediate schema-7 cache migration.
- Added Nicky Dee Karaoke and Karaoke Balka, expanding the catalog to fifteen sources with parenthesized/bullet marker cleanup, the requested duplicate priority, and immediate schema-8 cache migration.
- Added Karaoke Office, expanding the catalog to sixteen sources with ordinary, malformed featured-artist, and verified song-first title cleanup, the requested duplicate priority, and immediate schema-9 cache migration.
- Corrected JLo.Instru's artist-first `The Smashing Pumpkins - 1979` exception without weakening its normal song-first grammar.
- Corrected Karaoke Office's inverted `Zach Bryan - 28` record, canonicalized `Featuring`/`Feat`/`Ft` credits to artist-side `Ft.`, including misplaced title-side credits, and removed Pants' unattributed `25 Minutes or Less` parody from catalog results while preserving queue editability.
- Normalized Pants' quoted performance/byline, parody, live-cover, and animal-sound titles to `Artist - Song (Qualifier)` across live results, caches, deduplication, and queues.
- Added verified Pants attribution for Jo Lee's `I Wonder What's Inside Your Butthole` and recovered the missing 1Music separator in `Velvet Underground - Femme Fatale`.
- Added article-insensitive alphabetical Karaoke search results, anchored ObsKure Best Version/duet/original-sound cleanup, same-source duplicate suppression, and shared normalization for Acoustic, 7-inch, and video-version labels.
- Added source-aware cleanup for legacy 1Music branding, vocal-removal labels, unspaced separators, and redundant 2Pac/Tupac aliases, plus compact parenthetical formatting for Lemmy performance-version metadata.
- Expanded shared provider cleanup for JLo.Instru's Instrumental Version/Karaoke Lyrics titles, CCKaraokeX's split CC/Karaoke Version tags, and 1Music's trailing Instrumental Version labels.
- Compacted generic edition qualifiers such as Album Version, Live Version, Official Version, Single Version, and Original Version, corrected Bela Lugosi's possessive, and made duplicate matching tolerant of missing apostrophes and artist conjunction punctuation.
- Generalized parenthetical edition cleanup to retain any descriptor while dropping its final Version label, removed every current Offbeat key-signature form, and reordered 1Music's edition-first titles to `Artist - Song (Edition)`.
- Added shared `YYYY Version; Edit` and `Version Descriptor` normalization, normalized JLo.Instru separators before song/artist reversal, and recognized ObsKure's Best Karaoke Version and Instrumental Lyrics suffix forms.
- Removed the legacy 1Music XRINA batch tag and recovered missing artist/title separators for its Smiths uploads.
- Added Karaoke playback that keeps search and upcoming queue editing on the primary display while mpv plays on an external display, removes completed entries, and retains visibly failed entries.
- Added readable artist/song line breaks in both Karaoke columns, Enter-to-play for any selected queue entry, 720p next-song background prefetch, and shared retro fade/slide/block handoff transitions.
- Added anchored Funbox title cleanup so `Artist - Song (Funbox Karaoke, YEAR)` displays as `Artist - Song (YEAR)` while preserving raw metadata.
- Added pinned, checksum-verified standalone yt-dlp and Deno helpers to configure, install, signing, licensing, and release verification; packaged YouTube playback no longer depends on system yt-dlp, Python, or a JavaScript runtime.
- Added shared accent-insensitive QML search, marquee-row, and confirmation components used by Karaoke and Jellyfin where applicable.
- Added Karaoke backend tests for source-specific title normalization, ranked sixteen-source catalog reconciliation, legacy/current cache loading, duplicate queue identity, persistence, reorder, failure/reset/completion, and canonical playlist generation, plus an opt-in live channel integration test.
- Added Jellyfin TV show library browsing through series, season, and episode lists, with episodes using the existing detail and mpv playback flow.
- Added shared Jellyfin movie/episode detail metadata and audio/subtitle selection, including clearer episode labels and default/forced/external stream flags.
- Added a TV show metadata header on the season list with show overview, year, counts, status, rating, and genres when Jellyfin provides them.
- Added the Retro module with MyRetroTVs-backed feeds for the 50s, 60s, 70s, 80s, 90s, and 00s.
- Added Retro channel surfing, content filtering, clip skipping, YouTube handoff through mpv, CRT-style effects, and static transitions.
- Added a Tumblr screensaver module that builds a shuffled, non-repeating image montage from a public Tumblr URL with retro 90s-style QML transitions.
- Added Tumblr falling-block transitions where each falling tile is a clipped piece of the incoming image.
- Renamed Ambient Mode to Loop and Local Files to Local in the UI, showed each media directory on its first view, and defaulted both directories to `~/Desktop`.
- Ordered user-facing modules as Jellyfin, Karaoke, Retro, Tumblr, Local, then Loop.
- Filtered Jellyfin episode list loading so virtual/missing episode rows do not appear in TV seasons.
- Defaulted mpv playback to another connected macOS display when one is available.
- Added a primary-display playback control view and second-display QML output layer for Tumblr playback and Retro overlays.
- Added macOS idle-sleep prevention while the app is running, with a configurable battery threshold that releases the sleep assertion.
- Added release checks that run tests, exercise bundled helpers with a stripped environment and a live item from every Karaoke source, reject Homebrew load paths, and exclude `.DS_Store` files from the app bundle.
- Fixed development YouTube playback accidentally falling through to an outdated system yt-dlp; mpv now receives the pinned helper and Deno paths explicitly and ignores conflicting helper configuration.
- Fixed Loop's separate-audio playback to use the bundled mpv through the shared helper resolver instead of requiring a Homebrew or system mpv on `PATH`.
- Reduced Karaoke search and queue title sizes so substantially more of each song name remains visible.

- Added the Jellyfin module as the primary server-backed media integration.
- Added Jellyfin password login and Quick Connect.
- Added movie-library browsing for Jellyfin.
- Added paged Jellyfin movie loading so large libraries become usable before every item has loaded.
- Added accent-insensitive live filtering in Jellyfin movie lists.
- Added Jellyfin direct-play playback through mpv.
- Added pre-play audio and subtitle selection for Jellyfin movies.
- Added pre-play detail and track-selection flow for Local Files.
- Added local media track probing through `ffprobe`.
- Added sidecar subtitle discovery for Local Files.
- Added packaged macOS helper bundling for `mpv`, `ffmpeg`, `ffprobe`, and their non-system dynamic libraries.
- Added `AGENTS.md` as the single local development-agent guide.
- Added `ROADMAP.md` for post-1.0 project direction.

### Changed

- Renamed the runtime product to `240-mp-jellyfin`.
- Set the app version to `1.0`.
- Changed the app data directory to `~/Library/Application Support/240-mp-jellyfin/`.
- Changed the build to macOS-only; CMake now fails on non-macOS hosts.
- Changed the release workflow to publish an Apple Silicon macOS DMG only.
- Changed release tagging docs and workflow matching to support `v1.0` style tags.
- Changed Plex to a hidden module so it stays available as a reference without appearing in normal module discovery or Settings.
- Changed Jellyfin movie list requests to use lightweight fields and defer heavier metadata until the detail view.
- Changed Jellyfin library caching to be in-memory and session-scoped.
- Updated README, install, build, architecture, contribution, and security docs for the macOS Jellyfin fork.

### Security

- Jellyfin stream URLs no longer include `api_key` tokens.
- mpv receives Jellyfin HTTP auth through a temporary owner-only mpv include file instead of visible command-line header arguments.
- Playback logs redact known token query parameters and auth-header text.
- `jellyfin_auth.json` is written with owner read/write permissions only.
- Jellyfin in-memory library cache is cleared on logout and new authentication.

### Removed

- Removed the stale Raspberry Pi install script from the macOS fork.
- Removed duplicate `CLAUDE.md`; use `AGENTS.md` for local agent instructions.
- Removed `JELLYFIN_MAC_FORK_PLAN.md`; the release history now lives here and future work lives in `ROADMAP.md`.

### Release Scope

- Apple Silicon macOS is the only supported runtime for 1.0.
- The retro CRT-style UI stays.
- Local Files and Ambient Mode stay user-facing.
- Plex stays hidden until Jellyfin has enough parity to remove it.
- Jellyfin starts with movie libraries and direct play before broader media support.
- Packaged macOS apps bundle playback/probing helpers; development builds can still use Homebrew tools from `PATH`.
