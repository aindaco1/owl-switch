import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { setTimeout } from 'node:timers/promises';

// Explicit, live acceptance: sends synthetic data and closes only its own issue.
// No app files, environment values, local logs, or credentials enter the report.
const origin = 'https://owlswitch-crash.dustwave.xyz';
const repository = 'aindaco1/owl-switch';
const marker = `Synthetic deployment delivery test ${randomUUID()}`;
const payload = {
  app: {
    name: 'OwlSwitch', version: '0.0.0-relay-smoke', identifier: 'com.240mp.jellyfin',
    channel: 'production', buildProfile: 'release', os: 'macOS synthetic', arch: 'arm64'
  },
  report: {
    id: randomUUID(), kind: 'native-output-error', surface: 'native-output',
    message: 'Synthetic deployment delivery test', capturedAt: new Date().toISOString(),
    context: {
      eventCount: 1,
      recentEvents: [{ at: new Date().toISOString(), severity: 'warning', message: marker }]
    }
  }
};

function github(path, body) {
  const args = ['api', path];
  if (body) args.push('--method', 'PATCH', '--input', '-');
  return JSON.parse(execFileSync('gh', args, {
    encoding: 'utf8', input: body ? JSON.stringify(body) : undefined,
    stdio: ['pipe', 'pipe', 'pipe'], timeout: 30000
  }));
}

function verifyIssue(issue, receipt, count) {
  assert.equal(issue.number, receipt.issueNumber);
  assert.equal(issue.user.type, 'Bot', 'GitHub App must deliver the issue');
  assert.equal(issue.state, 'open');
  assert.ok(issue.body.includes(`<!-- owlswitch-fingerprint:${receipt.fingerprint} -->`));
  assert.ok(issue.body.includes(marker), 'Issue must contain this synthetic test only');
  const state = issue.body.match(/<!-- owlswitch-report-state:([A-Za-z0-9+/=]+) -->/);
  assert.ok(state);
  const aggregate = JSON.parse(Buffer.from(state[1], 'base64').toString('utf8'));
  assert.equal(aggregate.count, count);
  assert.equal(aggregate.versions[payload.app.version], count);
}

async function submit() {
  const response = await fetch(`${origin}/v1/reports`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload), signal: AbortSignal.timeout(8000), redirect: 'error'
  });
  const receipt = await response.json();
  assert.equal(response.status, 200, `Report rejected: ${JSON.stringify(receipt)}`);
  assert.equal(receipt.ok, true);
  assert.match(receipt.fingerprint, /^[a-f0-9]{16}$/);
  assert.ok(Number.isSafeInteger(receipt.issueNumber) && receipt.issueNumber > 0);
  console.log(JSON.stringify(receipt));
  return receipt;
}

async function main() {
  // Check operator access before creating any test issue.
  const repo = github(`repos/${repository}`);
  assert.ok(repo.permissions?.push || repo.permissions?.triage,
    'The signed-in gh account must be able to close the synthetic issue');
  const response = await fetch(`${origin}/health`, {
    signal: AbortSignal.timeout(8000), redirect: 'error'
  });
  const health = await response.json();
  assert.equal(response.status, 200, `Relay is not ready: ${JSON.stringify(health)}`);
  assert.equal(health.ok, true);

  const ownedIssues = new Set();
  try {
    const first = await submit();
    const firstIssue = github(`repos/${repository}/issues/${first.issueNumber}`);
    verifyIssue(firstIssue, first, 1);
    ownedIssues.add(first.issueNumber);
    assert.equal(first.action, 'created');
    console.log(`Creation verified: ${firstIssue.html_url}`);

    // Allow the KV index to propagate before checking sequential aggregation.
    console.log('Waiting 65 seconds for the KV fingerprint index to propagate.');
    await setTimeout(65000);
    payload.report.id = randomUUID();
    const second = await submit();
    const secondIssue = github(`repos/${repository}/issues/${second.issueNumber}`);
    if (second.issueNumber !== first.issueNumber) {
      verifyIssue(secondIssue, second, 1);
      ownedIssues.add(second.issueNumber);
    }
    assert.equal(second.issueNumber, first.issueNumber, 'Reports must aggregate in one issue');
    assert.equal(second.fingerprint, first.fingerprint);
    assert.equal(second.action, 'aggregated');
    verifyIssue(secondIssue, second, 2);
    console.log(`Delivery and aggregation verified: ${secondIssue.html_url}`);
  } finally {
    for (const number of ownedIssues) {
      const closed = github(`repos/${repository}/issues/${number}`, {
        state: 'closed', state_reason: 'completed'
      });
      assert.equal(closed.state, 'closed');
      console.log(`Closed synthetic issue: ${closed.html_url}`);
    }
  }
}

main().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
