import assert from 'node:assert/strict';
import test from 'node:test';

import relay, { fingerprint, sanitizePayload } from '../src/index.js';

const env = { ALLOWED_APP_IDENTIFIER: 'com.240mp.jellyfin' };
const payload = {
  app: {
    name: 'OwlSwitch', version: '1.6.4', identifier: 'com.240mp.jellyfin',
    os: 'macOS 26.6', arch: 'arm64'
  },
  report: {
    context: {
      eventCount: 2,
      recentEvents: [
        { at: '2026-08-29T01:02:03Z', severity: 'warning',
          message: 'Failed /Users/alice/Private.mov token=secret https://example.com/video' }
      ]
    }
  }
};

test('sanitizes client diagnostics again at the trust boundary', () => {
  const sanitized = sanitizePayload(payload, env);
  const text = JSON.stringify(sanitized);
  assert.doesNotMatch(text, /alice|secret|example\.com/);
  assert.match(text, /redacted-path/);
  assert.equal(sanitized.report.context.recentEvents.length, 1);
});

test('rejects reports for another application', () => {
  const changed = structuredClone(payload);
  changed.app.identifier = 'com.example.other';
  assert.throws(() => sanitizePayload(changed, env), /not allowed/);
});

test('creates a stable bounded fingerprint', async () => {
  const sanitized = sanitizePayload(payload, env);
  const first = await fingerprint(sanitized);
  const second = await fingerprint(sanitized);
  assert.equal(first, second);
  assert.match(first, /^[a-f0-9]{16}$/);
});

const configured = {
  ...env, REPORTS_ENABLED: 'true', RATELIMIT: {}, REPORT_INDEX: {}, REPORT_GROUPS: {}, REPORT_IDS: {},
  GITHUB_APP_ID: 'synthetic-app', GITHUB_APP_INSTALLATION_ID: 'synthetic-installation',
  GITHUB_APP_PRIVATE_KEY: 'synthetic-key'
};

test('health fails closed for every missing deployment prerequisite without exposing secrets', async () => {
  for (const key of ['REPORTS_ENABLED', 'RATELIMIT', 'REPORT_INDEX', 'REPORT_GROUPS', 'REPORT_IDS',
    'GITHUB_APP_ID', 'GITHUB_APP_INSTALLATION_ID', 'GITHUB_APP_PRIVATE_KEY']) {
    const incomplete = { ...configured };
    delete incomplete[key];
    const response = await relay.fetch(new Request('https://relay.test/health'), incomplete);
    assert.equal(response.status, 503, key);
    const body = await response.json();
    assert.equal(body.ok, false);
    assert.doesNotMatch(JSON.stringify(body), /synthetic/);
  }
});

test('health reports configuration readiness without making a GitHub request', async () => {
  const response = await relay.fetch(new Request('https://relay.test/health'), configured);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), {
    ok: true, service: 'owlswitch-diagnostics-relay', reportingEnabled: true,
    storageConfigured: true, githubConfigured: true
  });
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
});

test('report intake fails closed while disabled or either KV binding is absent', async () => {
  for (const missing of ['REPORTS_ENABLED', 'RATELIMIT', 'REPORT_INDEX', 'REPORT_GROUPS', 'REPORT_IDS']) {
    const incomplete = { ...configured };
    delete incomplete[missing];
    const response = await relay.fetch(new Request('https://relay.test/v1/reports', {
      method: 'POST', body: JSON.stringify(payload)
    }), incomplete);
    assert.equal(response.status, 503, missing);
  }
});

test('bounds the report to the latest 20 events and drops unrecognized fields', () => {
  const changed = structuredClone(payload);
  changed.report.context.recentEvents = Array.from({ length: 25 }, (_, index) => ({
    message: `Event ${index}`, severity: 'warning', file: 'private.mov'
  }));
  changed.report.context.environment = { secret: 'private-value' };
  const sanitized = sanitizePayload(changed, env);
  assert.equal(sanitized.report.context.recentEvents.length, 20);
  assert.equal(sanitized.report.context.recentEvents[0].message, 'Event 5');
  assert.doesNotMatch(JSON.stringify(sanitized), /private/);
});
