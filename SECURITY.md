# OwlSwitch Security Policy

OwlSwitch handles local media paths and Jellyfin credentials, so security reports are taken seriously even though this is a small fork. The repository, bundle identifier, updater aliases, and Application Support directory retain their historical 240-mp-jellyfin compatibility contract.

## Supported Versions

This project does not maintain long-term support branches. Only the current `main` branch and the latest GitHub release are supported.

## Reporting A Vulnerability

Do not open a public issue for security problems.

Use GitHub's private vulnerability reporting for this repository when available, or contact the maintainer privately through GitHub. Include:

- Affected commit or release.
- macOS version and hardware.
- Steps to reproduce.
- Potential impact.
- Proof of concept, if one is safe to share privately.

## Security-Sensitive Areas

- `jellyfin_auth.json` stores Jellyfin server URL, access token, user ID, username, server identity, and client device ID. It must not store passwords.
- Auth files containing tokens should be written with owner read/write permissions only.
- Tokens, passwords, full auth headers, and token-bearing URLs must not be logged.
- Jellyfin stream URLs should not include `api_key`; mpv should receive Jellyfin auth through the private temporary mpv include file created by `MpvController`.
- Movie library caches are in memory only and are cleared on logout or new Jellyfin authentication.
- Karaoke catalog and queue files contain only public metadata, entry UUIDs, display state, and canonical `https://www.youtube.com/watch?v=<id>` URLs. They must never accept arbitrary playlist URLs from cache or QML.
- Karaoke helper stderr and stored failure messages must redact URLs before they are surfaced or persisted.
- Local queue state contains root-contained local paths or, for soundtrack-only YouTube imports, validated 11-character video IDs with bounded titles; it also stores entry UUIDs, bounded display/error text, and track choices. It is schema/size bounded and owner-only. Local `m3u`/`m3u8` import must reject remote schemes, root escapes, cycles, excessive nesting, newline paths, and unsupported files. The separate YouTube importer accepts only HTTPS playlist/watch URLs on recognized YouTube hosts with a validated playlist ID, canonicalizes every persisted/playback entry, caps helper output/runtime and queue size, and discards helper diagnostics that may echo a submitted URL. It never enables arbitrary remote Local entries or browser-cookie access.
- Nature observation and batched place-name requests are anonymous and bounded. Place IDs are capped, used only to prefer iNaturalist's English public place names, and are not persisted. Only research-grade, non-captive observations are accepted; each displayed or cached photo must independently be CC0 and have an HTTPS URL on `inaturalist-open-data.s3.amazonaws.com`. The cache stores normalized public city/state-or-province/country components rather than coordinates, never stores image files, is size/schema bounded, and is written atomically with owner-only permissions.
- yt-dlp and Deno release assets are version-pinned and SHA-256 verified at configure time. The helper manifest requires the official yt-dlp onedir layout and records versions, archive digests, and installed paths. Release signing covers every nested Mach-O runtime file; helper updates require reviewing the version, checksum, license, manifest, canaries, and signature results.
- Diagnostics are local by default. The rotating owner-only JSONL files contain at most three 512 KiB generations of already-sanitized structured Qt events. Paths, URLs, email addresses, authentication-like values, multiline data, and long fields are redacted or bounded before disk write.
- Diagnostic submission is available only through an explicit Settings action after the user can review the exact bounded preview. A payload contains at most the 20 most recent sanitized events plus app version and coarse OS/architecture; it never includes media, screenshots, arbitrary files, full logs, config/auth files, environment variables, cookies, or credentials.
- The separately deployed diagnostics Worker must use dedicated rate-limit and fingerprint-index KV namespaces, independently sanitize the payload, accept only bundle identifier `com.240mp.jellyfin`, and obtain a short-lived GitHub App installation token. GitHub credentials must never be bundled in OwlSwitch. Without both KV bindings the Worker fails closed.
- The launch and manual update checks only use the repository's latest GitHub Release. The request contains the app version in its user agent but no settings, authentication, media, queue, or playback data. A launch check never downloads or installs an update. User-approved downloads require the API-provided SHA-256 asset digest; the DMG and nested app must pass Apple signature/notarization checks, and the app must match the running app's Developer ID team, bundle identifier, advertised version, and arm64 architecture. The installer re-verifies immediately before a rollback-safe replacement.
- Release CI notarizes and staples the app before placing it in the signed/notarized DMG, requires exactly the real app plus an `Applications -> /Applications` shortcut, mounts the final image read-only to recheck the app identity and trust contract, validates both tickets, and publishes a human-verifiable `.sha256` asset.
- Release CI uses encrypted App Store Connect API-key credentials, ephemeral credential files/keychains, least-privilege workflow permissions, and commit-pinned GitHub Actions. Release tags must match the compiled app version.
- Modules should only communicate directly with their intended media service and should only write state under `~/Library/Application Support/240-mp-jellyfin/`.

## Out Of Scope

- Bugs with no security impact.
- Vulnerabilities in upstream dependencies such as Qt, mpv, FFmpeg, OpenSSL, or Jellyfin Server. Report those to the respective upstream projects.
- Unsupported platforms, including Raspberry Pi OS, Linux packaging, and Intel macOS.
