# Jev development evaluation

This testing-only integration reviews five synthetic error messages from the
actual OwlSwitch backends, alongside their existing deterministic tests. The
confirmed scope is a small error/recovery pilot, live by default with an explicit
offline mode. It needs no app release, version change, runtime provider, or
installed-app modification.

## Project context and reuse

OwlSwitch is a macOS Apple Silicon Qt/QML controller that delegates playback to
mpv. Its 31 CTest targets, fake servers/helpers, shared QML components, and native
display regression suite already verify state and playback contracts. Those
remain authoritative for authentication, queue preservation, retry, window
geometry/focus, packaging and update trust.

CutNotes' development evaluator demonstrated the complementary role of semantic
and exact checks. OwlSwitch applies that approach to backend feedback rather
than importing CutNotes' transcript-specific rubric. The existing
`@dustwave/test-core/jev` implementation is reused through the pinned
`shared/dust-wave-platform` submodule (commit
`6bb9854149203ee71445bf150c4ba86fac607d04`). It owns request construction, bounded
Cloudflare transport, response validation and review routing. There is no second
HTTP client or copied judge implementation. Node uses built-in modules; no npm
install or app dependency is added.

Context reviewed: the project overview, architecture, build, contribution,
security, installation, roadmap and release-history guides; the native-display
and diagnostics-relay guides; the upstream, Nature audio and display-focus plan
records; and CutNotes' current Jev/testing docs and implementation.

## Run

Install Node.js 20.9+ if needed, alongside the existing Qt/CMake prerequisites:

```sh
brew install node
git submodule update --init shared/dust-wave-platform
node scripts/test.mjs
```

The command configures `build` if absent, builds current source, runs evaluator
unit tests and all CTest targets, verifies complete synthetic evidence, and then
calls Jev. `--build-dir /absolute/path` selects another existing/configurable
build directory. Optional service canaries are disabled in this command and
remain separate commands in [Building](BUILDING.md#local-verification).

Set `CLOUDFLARE_ACCOUNT_ID` and `CLOUDFLARE_API_TOKEN` locally. Alternatively, put
only the account ID in ignored `.owl-switch-development.json`:

```json
{"cloudflare_account_id": "YOUR_ACCOUNT_ID"}
```

Without an environment token, authentication uses the existing cached Wrangler
4.136.2 login (`npx --no-install wrangler@4.136.2 auth token --json`), as in
CutNotes. It never logs the token, installs Wrangler automatically, opens a login
flow, or silently skips missing credentials.

```sh
node scripts/test.mjs --offline
```

Offline runs perform the same local checks and prepare the exact requests, with
zero Jev authentication or requests. The report marks the live evaluation
incomplete and cannot claim a combined pass. CI uses this mode explicitly;
maintainers run live evaluation locally. CMake may download its pinned helpers
on first configuration; "offline" here disables Jev, not first-time build setup.

## Coverage

| Case | Deterministic evidence | Jev meaning check |
|---|---|---|
| Jellyfin missing inputs | No auth file is saved; a subsequent rejected attempt still emits an error. | Server URL, username and password are required. |
| Jellyfin unauthenticated playback | No stream starts. | Playback lacks authentication. |
| Local invalid playlist URL | The soundtrack queue remains empty. | A valid public/unlisted YouTube playlist URL is requested. |
| Nature sound unavailable | Audio remains inactive and retry backoff is set. | Sound is unavailable and the slideshow continues. |
| Invalid update response | No launch prompt appears; manual retry against a valid fake response recovers. | GitHub returned invalid release data. |

The test-only `RecoveryEvidence.h` exporter reads the messages emitted by the
production classes after assertions succeed. Ordinary CTest does not export
unless the runner supplies its fresh evidence directory. No production test mode
or alternate message renderer is introduced. Existing QML tests separately cover
Nature's slideshow/audio lifecycle. These backend captures do not prove message
visibility/layout, keyboard navigation, or a complete user journey.

## Review policy and evidence

Each run also evaluates five good/bad calibration pairs and three separate
good/bad validation pairs: 16 controls plus five observed cases, 26 questions in
21 requests. Labels are engineering-authored examples, not independent human
ratings. This is small, targeted calibration coverage, not a general reliability
claim. After prompt tuning, previously inspected validation examples become
regressions and fresh validation examples are needed.

The fixed policy accepts `jev-1.13.0` with a minimum probability margin of 0.10.
Near ties, uncertain answers and unseen models become `review`. Ordinary runs
never change thresholds, questions or accepted model versions. Every control
must match its label and every observed case must pass; Jev cannot override a
local failure. A false pass on a deliberately bad control fails the command.
Questions follow TypeSafe's [atomic-question guidance](https://docs.typesafe.ai/introduction)
and [confidence guidance](https://docs.typesafe.ai/confidence).

Exit codes: `0` means the requested mode passed (`--offline` still means Jev was
skipped); `1` means completed live evaluation has a failure/review; `2` means
local tests, credentials, or evaluation could not complete.

Fresh evidence is retained under ignored `build/jev/run-*/`: native messages,
local-only observed facts, the full corpus, `report.json` and `review.md`. Reports
retain source hashes, exact requests, raw answers/probabilities, model, usage,
timings and flagged candidates. Missing, unexpected or symlinked evidence,
failed facts, oversized/private-looking text, or changing source during the
local run prevents authentication. There is no arbitrary-file or saved-private-
output upload option. Remote requests contain only public synthetic messages
and fixed questions, not source code, paths, facts, logs, media, settings, auth
files, user diagnostics or credentials.

API failures stop without retry or fallback. The shared transport forbids
redirects and requests disabled gateway logging/cache. The runner caps batches
at 100 questions and a $0.25 conservative estimate. This 26-question corpus
reserves $0.034944 using 32,000 input tokens per question and TypeSafe's dated
$0.042/million input price ([model reference](https://docs.typesafe.ai/models),
checked September 22, 2026). Estimates are not billing receipts or provider caps.
No purchase, automatic retry or top-up occurs.

No changes are made to app version 1.6.8, the signed updater contract or release
workflows. Native display, physical listening/monitor and distributed-app gates
remain separate; this work establishes development-test evidence only.

## Initial validation

September 22, 2026, Apple Silicon development checkout:

- The standard offline command passed all 31 CTest targets and 15 evaluator/shared
  client tests, exported all five native cases, and made zero Jev requests.
- The standard live command passed the same local checks and all 21 cases:
  10/10 calibration examples, 6/6 separate validation examples, and 5/5 observed
  backend messages. All 26 questions matched expectations with `jev-1.13.0`.
- The live run used 9,807 input tokens, with a dated estimated inference cost of
  $0.000411894. This is an estimate, not a receipt.
- QML lint, `actionlint` for the changed CI workflow, and `git diff --check` passed.

Local evidence: `build/jev/run-aqGgWz/` (offline) and
`build/jev/run-uT3fa7/` (live). The earlier offline report predates the summary's
separate `unevaluated` count; its `complete: false` and zero network attempts
remain explicit. No prompt or threshold was changed in response to live results.
Hosted CI, physical media/display checks and distribution acceptance are separate
from these local results.

## Deployment and local retention

This integration is delivered through [PR #32](https://github.com/aindaco1/owl-switch/pull/32)
to `main`, with no app release. The CI workflow checks both the stable macOS
toolchain and Xcode 27, including the existing packaged native-display suite.
CI uses explicit offline Jev mode; the live results above remain a separate gate.

Keep the current `build/` tree, its pinned helper runtime, the small reference
Jev reports above, the shared package, test fixtures and ignored local account
configuration. Obsolete packaging trees and verified duplicate cloud-sync files
can be removed recoverably. Follow the scoped
[cleanup instructions](BUILDING.md#cleaning-generated-artifacts); a blanket
ignored-file cleanup would erase the local configuration needed for live tests.
