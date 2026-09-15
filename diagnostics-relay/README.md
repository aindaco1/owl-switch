# OwlSwitch Diagnostics Relay

This Cloudflare Worker is the server-side half of OwlSwitch's explicit **Send
Report** action. It follows the existing ASCII VJ Remix relay pattern: the app
contains no GitHub credential, the Worker independently bounds and sanitizes
each payload, requests a short-lived GitHub App installation token, rate-limits
intake, and creates or updates one issue per stable fingerprint.

Nothing is submitted automatically. The client sends at most 20 sanitized
structured events after the user reviews the report and selects **Send Report**.
Media, screenshots, paths, URLs, emails, credentials, cookies, auth data,
environment dumps, and unrestricted logs are not accepted.

## Local verification

```bash
cd diagnostics-relay
npm install
npm run check
```

## Deployment status (September 15, 2026)

Worker version `adfd8189-9398-4469-8513-f4c8c2855f9f` is deployed and enabled with
its custom domain, all four dedicated KV namespaces, and all three GitHub App
secrets. The existing App installation includes OwlSwitch; credential preflight
verified metadata read and Issues read/write access with its other five selected
repositories retained.

The explicit synthetic delivery test created
[issue #28](https://github.com/aindaco1/owl-switch/issues/28), verified a second
report aggregated into the same issue at count two, and closed the test issue.
The initial test used a temporary public-DNS resolver because this Mac cached the
hostname's earlier NXDOMAIN result; hostname and TLS verification remained enabled.
No private application logs, configuration, or media were submitted.

Seven local tests, deployment dry-run, workflow lint, and dependency audit pass.
The dedicated relay CI check also passed. A temporary native harness using the
actual `DiagnosticsManager::submitReport` compiles; its live verification remains
pending this Mac's DNS cache refresh.

The dedicated Diagnostics relay CI workflow runs these tests and the deployment
dry-run without credentials or live reports. Deployment remains a deliberate
operator action; it does not require a new desktop app release.

## Storage and credentials

`wrangler.jsonc` contains the dedicated production and preview KV namespace IDs
for `RATELIMIT` and `REPORT_INDEX`, provisioned September 15, 2026. Retain those
bindings across deployments to preserve rate limits and fingerprint aggregation.
To provision replacement namespaces only:

```bash
wrangler kv namespace create OWLSWITCH_REPORT_RATELIMIT
wrangler kv namespace create OWLSWITCH_REPORT_RATELIMIT --preview
wrangler kv namespace create OWLSWITCH_REPORT_INDEX
wrangler kv namespace create OWLSWITCH_REPORT_INDEX --preview
```

Then replace the corresponding `id` and `preview_id` in `wrangler.jsonc`; keep
the binding names exactly `RATELIMIT` and `REPORT_INDEX`. The Worker fails closed
with HTTP 503 without both bindings.

The existing **ASCII VJ Crash Relay** GitHub App installation includes
`aindaco1/owl-switch` with metadata read and Issues read/write access. Its other
five selected repositories remain installed. Configure the three Worker secrets
below using that App's credentials. Secret values never belong in source, app
bundles, command arguments, or logs. Cloudflare cannot return previously stored
secret values; retain the source key in a secure location.

```bash
wrangler secret put GITHUB_APP_ID
wrangler secret put GITHUB_APP_INSTALLATION_ID
openssl pkcs8 -topk8 -inform PEM -outform PEM -nocrypt \
  -in github-app-private-key.pem \
  | wrangler secret put GITHUB_APP_PRIVATE_KEY
```

GitHub downloads may use PKCS#1; the Worker requires PKCS#8. This is separate from
any change to GitHub access-token formats. Never replace the App authentication
with a personal access token.

## Deploy and verify delivery

The production endpoint is
`POST https://owlswitch-crash.dustwave.xyz/v1/reports`, matching the shipped app.
The custom domain is managed by Wrangler; workers.dev and preview URLs are off.
After the secrets and installation are verified, set `REPORTS_ENABLED` to
`"true"` in `wrangler.jsonc`, then:

```bash
npm ci
npm run check
npm run deploy
curl --fail https://owlswitch-crash.dustwave.xyz/health
npm run smoke:delivery
```

`GET /health` returns HTTP 503 if reporting is disabled, either storage binding
is absent, or any GitHub credential is missing. HTTP 200 means the configuration
is present; it does **not** validate the private key, installation permissions,
or GitHub delivery. The response exposes only readiness booleans, never values.

`smoke:delivery` is an explicit live test requiring a signed-in `gh` account that
can close issues in this repository. It sends two generated synthetic reports,
verifies the GitHub App authored the resulting issue, verifies its aggregate
count increased from one to two, and closes the synthetic issue. It reads no
app data or logs. Allow about 65 seconds for KV propagation between submissions;
each POST uses the desktop client's eight-second timeout. This verifies sequential
aggregation, not concurrent delivery, retry deduplication, or closed-issue reuse.
Save the Worker version and the printed issue URL as deployment evidence.

For an initial deployment before setting secrets, or to pause intake:

```bash
npm run deploy -- --var REPORTS_ENABLED:false
```

The next ordinary deploy restores the checked-in configuration. To keep an
outage paused across deploys, also set `REPORTS_ENABLED` to `"false"` in
`wrangler.jsonc`. Preserve the namespaces and credentials when rolling back.
