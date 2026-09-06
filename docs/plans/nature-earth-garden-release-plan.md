# Nature sound — OwlSwitch 1.6.6

Status: historical — shipped in [1.6.6](https://github.com/aindaco1/owl-switch/releases/tag/v1.6.6).
[PR #23](https://github.com/aindaco1/owl-switch/pull/23) merged at
`50daf669415555eb688ef9c0ce8a8ef7e486fc30`. Release status was reconciled on
September 6, 2026; local acceptance below records the original release work.
Current behavior and commands live in the [architecture](../ARCHITECTURE.md#nature-module)
and [build](../BUILDING.md#local-verification) guides.

Prepared September 6, 2026. Implementation authorized by the maintainer after the
scope decisions below. Branch: `release/nature-earth-garden`, based on clean main
`84519a98987aa8c62de1a8694e2c70336fea145d` (the published 1.6.5 baseline).

## Confirmed scope

- Release **1.6.6**; keep the existing iNaturalist slideshow.
- Use **CC0 recordings only** from Earth Garden's Freesound catalog.
- Rotate recordings independently of photos, playing each to its natural end.
- Begin the next recording during the outgoing recording's final **five seconds**,
  with an overlapping crossfade. Do not use a fixed sound-rotation timer.
- Proceed through implementation and release validation without further scope questions.

Implementation defaults: sound ON, volume 30%, shuffled recordings lasting
30 seconds–10 minutes, combined Space/Enter pause, and sound stopped on exit.
Only catalog metadata is cached. Location/species matching, globe controls,
SomaFM music, offline audio downloads, and CC BY recordings are outside this release.

## Context and provider evidence

The source review covered the repository [agent instructions](../../AGENTS.md),
[project guides](../README.md), [1.6.5 upstream review](upstream-sync-1.6.5.md),
[diagnostics relay](../../diagnostics-relay/README.md), PR template, and build/release
workflows. Implementation review covered Nature's backend and QML, shared montage,
Local sound, mpv, helper resolution, settings, input, idle, and updater contracts.

[Earth Garden](https://earth-garden.alen.ro/) loads a
[public JSON catalog](https://earth-garden-sound-worker.workers2000.workers.dev/api/manifest)
and streams Freesound preview URLs. Its optional SomaFM layer is separate.
The planning snapshot contained 2,500 recordings: 1,073 CC0 and 1,427 CC BY;
837 CC0 recordings met the proposed duration bounds. A subsequent implementation
live check accepted 831 recordings, confirming that the catalog changes over time.
These counts are observations, not product guarantees.

The catalog requires no credentials. No public project repository, code license,
or third-party API stability commitment was established. The integration uses
native requests to this existing public endpoint, copies no website code, and
embeds neither the globe nor its analytics. It does not replace Earth Garden
with an authenticated Freesound search service. Catalog availability remains an
external dependency; a failure leaves the slideshow usable.

[Freesound API documentation](https://freesound.org/docs/api/overview.html),
[sound licenses](https://freesound.org/help/faq/#licenses), and
[API service terms](https://freesound.org/docs/api/terms_of_use.html) distinguish
recording licensing from service access. Runtime acceptance requires every row's
explicit CC0 identifier and the expected CC0 license URL, plus validated preview
identity. The source-page shortcut remains available without cluttering the photo.

## Implementation checklist

- [x] Add `NatureSoundtrack`, exposed through `NatureBackend.soundtrack`, using
  the existing module settings and registration. Keep photo/audio requests independent.
- [x] Centralize catalog and cache validation: exact HTTPS Freesound CDN path,
  numeric matching sound ID, explicit CC0 and license URL, bounded title/uploader,
  finite 30–600 second duration, and deduplication. Discard coordinates and tags.
- [x] Bound anonymous catalog requests to 8 MiB, 5,000 input rows, one request,
  and 15 seconds. Reject redirects; honor bounded numeric/date Retry-After values.
- [x] Save only revalidated metadata in atomic owner-only `nature_sounds.json`:
  schema 1, 24-hour freshness, at most 2,500 rows/4 MiB, with stale-cache fallback.
- [x] Extend `MpvController` with an audio-only launch profile, precise pause/volume
  controls, readiness, cancellation, and unique private IPC/config/log directories.
  Keep existing video launch behavior and updater trust checks intact.
- [x] Implement one `AudioCrossfadePlayer` using at most two shared controller
  instances. An ordinary mpv playlist or `--af=acrossfade` cannot supply two live
  inputs; continuously replacing a complex graph would add a second queue path.
  The bounded two-player coordinator reuses the established process/IPC seam.
- [x] Prepare only the next recording within 30 seconds of the current ending,
  muted/paused with memory-only 16 MiB buffers,
  verified TLS, redirects disabled, and no user config, scripts, video, or yt-dlp.
  Use 20-second readiness/progress deadlines and five failures with increasing backoff.
  Exhausting preparation retries lets the current usable recording finish.
- [x] Drive five-second equal-power overlaps from decoded time. Compensate for
  [mpv's cubic volume curve](https://github.com/mpv-player/mpv/blob/v0.41.0/player/audio.c)
  and reserve 30% mix headroom. Fade the first recording in and stop over 200 ms.
  This preserves source dynamics; it does not normalize loudness. Failed preparation
  may leave a silence gap rather than a hard switch to an unready recording.
- [x] Keep audio independent of photo next/refresh. Pause both inputs with the
  slideshow; cancel on exit/error/destruction. Make start idempotent and quick
  reentry safe. Route inactive-window remote controls through the existing bridge.
- [x] Prevent the app screensaver from covering an active Nature session, including
  when sound is OFF or unavailable. Reuse the existing output lease for both displays.
- [x] Add NATURE SOUND / SOUND VOLUME settings and the A source shortcut; retain
  the compact species/location overlay and shared Tumblr/Nature montage renderer.
- [x] Bump both version declarations and add the dated 1.6.6 changelog. Update
  user, architecture, security, installation, contributor, and project guidance.

## Recorded Validation and Release Outcome

Local source validation on macOS Apple Silicon, Qt 6.11.1, mpv 0.41.0:

- [x] Full CMake build and all **30 CTest suites** pass.
- [x] Full repository QML lint passes with the existing context-property warnings.
- [x] Catalog fixtures reject CC BY, wrong licenses/hosts/IDs, duplicates, oversized
  lists/responses, and poisoned caches. Test anonymous requests, redirects, freshness,
  preserved stale cache, transfer bounds, cancellation, and idempotent/quick reentry.
- [x] Fake-mpv tests cover private players, audio-only arguments, repeated overlaps,
  non-repeating decks, pause/resume, master volume, bounded failures, and stopping both.
- [x] Real mpv decoder test completes three generated 12-second recordings at
  natural EOF with repeated overlaps in about 27 seconds, using null audio output.
- [x] A live HTTPS Freesound preview decodes successfully with the audio profile.
  Blocking reads from Homebrew’s certificate directory exposed a bundled OpenSSL
  trust-store dependency. Nature now resolves certifi from the pinned helper runtime;
  the same restricted HTTPS test passes, and packaging rejects missing/malformed PEM.
- [x] Rendered Nature QML checks cover first-image start, pause/refresh, next,
  source/status UI, exit, and controller/external output leases. Physical second-screen
  and subjective listening acceptance remain distinct from these automated checks.
- [x] Finish the running-app smoke with isolated `DATA_ROOT`: live images/audio,
  pause/refresh/resume, stop, persisted OFF/volume after relaunch, and no idle overlay.
  Verify unsigned package dependencies/helpers and repeat the real decoder test
  with locally ad-hoc-signed bundled mpv. Signed-artifact results are recorded below.
- [x] Bound CI compilation to three jobs. The prior bare parallel flag expands to
  unlimited `make -j`; the previous release launched 83 compilations within a minute.
- [x] [PR #23](https://github.com/aindaco1/owl-switch/pull/23) merged after its checks passed.
- [x] [Exact main CI](https://github.com/aindaco1/owl-switch/actions/runs/34054499513)
  passed for the release commit and produced the attested app.
- [x] [Release workflow](https://github.com/aindaco1/owl-switch/actions/runs/34054946147)
  passed and published `v1.6.6` with signed, notarized, stapled app and DMG.
- The original local report, `dist/releases/v1.6.6-validation.md` (ignored output),
  records a matching public DMG digest, successful mounted app/DMG trust checks,
  bundled HTTPS/decoder checks, and a real signed 1.6.5 → 1.6.6 updater test with
  isolated data. Sound OFF/20% settings survived the update; the restarted app
  then passed live Nature playback, pause, and stop checks.
- Subjective listening and a physical second-display check were not performed.
  These limits remain separate from source, CI, signed-artifact, and isolated
  updater acceptance. The earlier local checks were not rerun for this doc move.

Reusable commands are maintained in [Local Verification](../BUILDING.md#local-verification).

Public artifact, installation/update, physical display, and listening results must
be reported separately. Automated decoder/gain checks establish playback mechanics;
they do not claim that a person has auditioned every source recording.
