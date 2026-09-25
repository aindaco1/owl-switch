# Shared support and Sparkle migration

OwlSwitch 1.7.1 pins Platform Qt 0.1.0 and Desktop Core 0.2.0. Platform owns a
new MIT Qt transport and a narrow Objective-C++ Sparkle bridge. Product code,
report schemas, UI, storage and this relay stay GPLv3 here; no OwlSwitch source
was copied into Platform.

Diagnostics freezes the full sanitized JSON preview and random report ID in an
owner-only pending file. New events do not silently change reviewed bytes.
Explicit Refresh starts a new report; retries reuse the same bytes and ID.
Only a matching confirmed receipt is shown as sent. Clear removes the draft.
Reports remain local until Send. Server retry receipts last 30 days; resending
an older draft after that retention window may count as a new submission.

The relay retains its endpoint, GitHub App, sanitizer and KV namespaces. Two
Durable Objects serialize IDs and groups, reject ID/payload conflicts and adopt
historical counts. GitHub uncertainty fails closed. Bot sections are replaced
without discarding maintainer notes. Do not delete namespaces during rollback.

Sparkle 2.10.0 owns version comparison, download, trust validation, installation
and relaunch. Launch calls request information only; manual checks open Sparkle’s
standard UI. The bundle requires signed feeds and archive verification before
extraction. Update identity, Developer ID team and arm64 release gates remain.
The old DMG name, checksum and hidden app/executable aliases remain so earlier
installed versions can reach this release. The ZIP contains one real app.

Validation includes native frozen-draft/privacy/retry tests, injected updater
policy tests, Qt receipt/size/deadline tests and relay concurrency/legacy-count/
uncertain-create tests. Existing full CTest and recovery-corpus gates remain.
CI builds the complete app; release reuses its exact attested artifact, signs
Sparkle helpers inside-out, notarizes, packages and signs the feed. Source
archives include the pinned Platform source. Public download, legacy updater
and Sparkle replacement acceptance are separate release checks.

Rollback: revert this migration to restore Platform
`6bb9854149203ee71445bf150c4ba86fac607d04` and the prior app adapters. Verify the
actual old gitlink from the parent commit when executing rollback. App data has
no destructive migration; retain pending drafts and both new Durable Object
namespaces if worker code is rolled back.
