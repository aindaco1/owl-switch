import test from 'node:test';
import assert from 'node:assert/strict';
import { OwlReportGroup, OwlReportID, sanitizePayload, fingerprint, issueBody } from '../src/index.js';

function storage() {
  const data = new Map(); let alarm = null;
  return { get: async key => structuredClone(data.get(key)), put: async (key, value) => data.set(key, structuredClone(value)),
    list: async ({ prefix }) => new Map([...data].filter(([key]) => key.startsWith(prefix))),
    delete: async key => { for (const item of Array.isArray(key) ? key : [key]) data.delete(item); },
    deleteAll: async () => data.clear(), getAlarm: async () => alarm, setAlarm: async value => { alarm = value; } };
}
function report(id, message = 'Synthetic playback failure') {
  return sanitizePayload({ app: { identifier: 'com.240mp.jellyfin', version: '1.7.0', os: 'synthetic', arch: 'arm64' },
    report: { id, capturedAt: '2026-09-25T00:00:00Z', context: { recentEvents: [{ at: '2026-09-25T00:00:00Z', severity: 'warning', message }] } } });
}
const request = value => new Request('https://internal/report', { method: 'POST', body: JSON.stringify(value) });

function service({ legacy = null, uncertainCreate = false } = {}) {
  const issues = new Map(); const calls = []; const groups = new Map(); const ids = new Map();
  if (legacy) issues.set(legacy.number, structuredClone(legacy));
  let loseResponse = uncertainCreate;
  const api = async (_env, path, options) => {
    calls.push({ path, method: options.method });
    if (path.startsWith('/search/issues')) return { items: [...issues.values()] };
    if (options.method === 'GET') {
      const value = issues.get(Number(path.split('/').at(-1)));
      if (!value) throw Object.assign(new Error('missing'), { status: 404 });
      return structuredClone(value);
    }
    if (options.method === 'POST') {
      const value = { ...JSON.parse(options.body), number: 7, state: 'open' }; issues.set(7, value);
      if (loseResponse) { loseResponse = false; throw new Error('response lost after create'); }
      return structuredClone(value);
    }
    const number = Number(path.split('/').at(-1));
    const value = { ...issues.get(number), ...JSON.parse(options.body), number }; issues.set(number, value);
    return structuredClone(value);
  };
  const env = { REPORT_INDEX: { get: async () => legacy ? { number: legacy.number } : null },
    REPORT_GROUPS: { idFromName: x => x, get: key => {
      if (!groups.has(key)) groups.set(key, new OwlReportGroup({ storage: storage() }, env, api));
      return groups.get(key);
    } } };
  function inbox(id) {
    if (!ids.has(id)) ids.set(id, new OwlReportID({ storage: storage() }, env));
    return ids.get(id);
  }
  const send = value => inbox(value.report.id).fetch(request(value));
  return { env, calls, issues, send };
}

test('concurrent reports serialize counts and same-ID retries return matching receipts', async () => {
  const service = serviceFixture();
  const receipts = await Promise.all(['one', 'one', 'two'].map(id => service.send(report(id)).then(r => r.json())));
  assert.deepEqual(receipts.map(r => r.action), ['created', 'duplicate', 'updated']);
  assert.deepEqual(receipts.map(r => r.reportId), ['one', 'one', 'two']);
  assert.equal(service.calls.filter(c => c.method === 'POST').length, 1);
  assert.match(service.issues.get(7).body, /Count: `2`/);
});
function serviceFixture(options) { return service(options); }

test('changed payload cannot reuse an ID even across fingerprints', async () => {
  const service = serviceFixture();
  assert.equal((await service.send(report('one'))).status, 200);
  assert.equal((await service.send(report('one', 'Entirely different failure'))).status, 409);
  assert.equal(service.calls.filter(c => c.method === 'POST').length, 1);
});

test('uncertain create is reconciled without another creation or count increment', async () => {
  const service = serviceFixture({ uncertainCreate: true });
  assert.equal((await service.send(report('one'))).status, 502);
  const receipt = await (await service.send(report('one'))).json();
  assert.equal(receipt.issueNumber, 7); assert.equal(receipt.action, 'updated');
  assert.equal(service.calls.filter(c => c.method === 'POST').length, 1);
  assert.match(service.issues.get(7).body, /Count: `1`/);
});

test('adopts legacy count, issue marker and maintainer text before incrementing', async () => {
  const value = report('new'); const fp = await fingerprint(value);
  const body = issueBody(value, fp, { count: 19, firstSeen: '2026-01-01', versions: { '1.6.8': 19 } })
    .replace('<!-- owlswitch-diagnostics:start -->\n', '').replace('\n<!-- owlswitch-diagnostics:end -->', '') + '\nMaintainer note: preserve this.';
  const service = serviceFixture({ legacy: { number: 7, body, state: 'closed', labels: [] } });
  const receipt = await (await service.send(value)).json();
  assert.equal(receipt.issueNumber, 7); assert.equal(receipt.action, 'updated');
  assert.equal(service.calls.filter(c => c.method === 'POST').length, 0);
  const issue = service.issues.get(7);
  assert.match(issue.body, /Count: `20`/); assert.match(issue.body, /Maintainer note: preserve this/);
  assert.equal(issue.state, 'open');
  await service.send(report('newer'));
  assert.match(service.issues.get(7).body, /Count: `21`/);
});

test('legacy provider failure never creates a replacement issue', async () => {
  const ctx = { storage: storage() }; let posts = 0;
  const env = { REPORT_INDEX: { get: async () => ({ number: 7 }) } };
  const group = new OwlReportGroup(ctx, env, async (_env, _path, options) => {
    if (options.method === 'POST') posts++;
    throw Object.assign(new Error('provider unavailable'), { status: 503 });
  });
  assert.equal((await group.fetch(request(report('one')))).status, 502);
  assert.equal(posts, 0); assert.equal(await ctx.storage.get('group'), undefined);
});
