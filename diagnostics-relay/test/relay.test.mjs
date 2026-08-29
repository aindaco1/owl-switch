import assert from 'node:assert/strict';
import test from 'node:test';

import { fingerprint, sanitizePayload } from '../src/index.js';

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
