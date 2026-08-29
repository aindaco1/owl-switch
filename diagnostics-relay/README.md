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
npm test
npm run check
```

Before any deployment, create dedicated production and preview KV namespaces
for `RATELIMIT` and `REPORT_INDEX`:

```bash
wrangler kv namespace create OWLSWITCH_REPORT_RATELIMIT
wrangler kv namespace create OWLSWITCH_REPORT_RATELIMIT --preview
wrangler kv namespace create OWLSWITCH_REPORT_INDEX
wrangler kv namespace create OWLSWITCH_REPORT_INDEX --preview
```

Then add the returned IDs to `wrangler.jsonc` as `kv_namespaces` bindings named
exactly `RATELIMIT` and `REPORT_INDEX`. They are intentionally absent from the
checked-in configuration: the Worker fails closed with HTTP 503 without both,
and the repository never pretends that placeholder IDs are deployable resources.

Install the existing GitHub App on `aindaco1/owl-switch` with Issues
write access, then set the same three PKCS#8-backed secrets used by ASCII VJ
Remix:

```bash
wrangler secret put GITHUB_APP_ID
wrangler secret put GITHUB_APP_INSTALLATION_ID
wrangler secret put GITHUB_APP_PRIVATE_KEY
```

The configured production endpoint is
`POST https://owlswitch-crash.dustwave.xyz/v1/reports`. It intentionally remains
unavailable until the dedicated KV IDs, custom domain, secrets, and GitHub App
installation are verified and the Worker is deliberately deployed. This
release work does not deploy it.
