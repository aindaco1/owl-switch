import { createAppAuth } from '@octokit/auth-app';
import { createGitHubIssueReporter } from '@dustwave/desktop-core/github-issues';
import { ReviewedReportGroup } from '@dustwave/desktop-core/reviewed-report-group';
import { readBoundedBytes } from '@dustwave/worker-core/request-validation';

const MAX_PAYLOAD_BYTES = 24576;
const MAX_EVENTS = 20;
const GITHUB_API_VERSION = '2022-11-28';

function readiness(env) {
  const reportingEnabled = String(env.REPORTS_ENABLED || 'false') === 'true';
  const storageConfigured = Boolean(env.RATELIMIT && env.REPORT_INDEX && env.REPORT_GROUPS && env.REPORT_IDS);
  const githubConfigured = ['GITHUB_APP_ID', 'GITHUB_APP_INSTALLATION_ID',
    'GITHUB_APP_PRIVATE_KEY'].every((key) => Boolean(env[key]));
  return { ok: reportingEnabled && storageConfigured && githubConfigured,
    service: 'owlswitch-diagnostics-relay', reportingEnabled, storageConfigured, githubConfigured };
}

function boundText(value, maximum = 600) {
  return String(value ?? '')
    .replace(/["']\/(?:Users|Volumes|private|tmp)\/[^"'\r\n]*["']/g, '[redacted-path]')
    .replace(/\/(?:Users|Volumes|private|tmp)\/[^\r\n]*/g, '[redacted-path]')
    .replace(/\b(?:https?|file|asset):\/\/[^\s"']+/gi, '[redacted-url]')
    .replace(/[A-Za-z]:\\[^\s"']+/g, '[redacted-path]')
    .replace(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g, '[redacted-email]')
    .replace(/(authorization|password|cookie|token|api[_-]?key)(\s*[:=]\s*)[^\s,;]+/gi,
      '$1$2[redacted]')
    .replace(/[\r\n\t]+/g, ' ')
    .slice(0, maximum);
}

function sanitizeEvent(raw) {
  const value = raw && typeof raw === 'object' ? raw : {};
  const severity = ['debug', 'info', 'warning', 'critical', 'fatal'].includes(value.severity)
    ? value.severity : 'info';
  return {
    at: boundText(value.at, 80),
    severity,
    message: boundText(value.message, 600)
  };
}

function sanitizePayload(payload, env = {}) {
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) {
    throw new Error('Report payload must be an object');
  }
  const app = payload.app && typeof payload.app === 'object' ? payload.app : {};
  const report = payload.report && typeof payload.report === 'object' ? payload.report : {};
  const identifier = boundText(app.identifier, 120);
  if (identifier !== String(env.ALLOWED_APP_IDENTIFIER || 'com.240mp.jellyfin')) {
    throw new Error('Report app identifier is not allowed');
  }
  const rawEvents = Array.isArray(report.context?.recentEvents)
    ? report.context.recentEvents.slice(-MAX_EVENTS) : [];
  return {
    app: {
      name: boundText(app.name || 'OwlSwitch', 120),
      version: boundText(app.version || 'unknown', 80),
      identifier,
      channel: boundText(app.channel || 'production', 40),
      buildProfile: boundText(app.buildProfile || 'release', 40),
      os: boundText(app.os || 'unknown', 100),
      arch: boundText(app.arch || 'unknown', 40)
    },
    report: {
      id: boundText(report.id || crypto.randomUUID(), 120),
      kind: 'native-output-error',
      surface: 'native-output',
      message: boundText(report.message || 'User-submitted OwlSwitch diagnostics', 600),
      capturedAt: boundText(report.capturedAt || new Date().toISOString(), 80),
      context: {
        eventCount: Math.max(0, Math.min(100000, Number(report.context?.eventCount) || 0)),
        recentEvents: rawEvents.map(sanitizeEvent)
      }
    }
  };
}

function normalizedFingerprintInput(sanitized) {
  const events = sanitized.report.context.recentEvents;
  const diagnostic = [...events].reverse().find((event) =>
    ['warning', 'critical', 'fatal'].includes(event.severity)) || events.at(-1);
  const message = boundText(diagnostic?.message || sanitized.report.message, 300)
    .toLowerCase()
    .replace(/\b\d+(?:\.\d+)?\b/g, '#')
    .replace(/\s+/g, ' ')
    .trim();
  return [sanitized.report.kind, sanitized.report.surface,
    sanitized.app.os, sanitized.app.arch, message].join('|');
}

async function fingerprint(sanitized) {
  const bytes = new TextEncoder().encode(normalizedFingerprintInput(sanitized));
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].slice(0, 8)
    .map((value) => value.toString(16).padStart(2, '0')).join('');
}

function normalizePrivateKey(value) {
  return String(value || '').replace(/\\n/g, '\n').trim();
}

async function installationToken(env) {
  for (const key of ['GITHUB_APP_ID', 'GITHUB_APP_INSTALLATION_ID', 'GITHUB_APP_PRIVATE_KEY']) {
    if (!env[key]) throw new Error(`${key} is not configured`);
  }
  const auth = createAppAuth({
    appId: env.GITHUB_APP_ID,
    installationId: env.GITHUB_APP_INSTALLATION_ID,
    privateKey: normalizePrivateKey(env.GITHUB_APP_PRIVATE_KEY)
  });
  return (await auth({ type: 'installation' })).token;
}

async function githubRequest(env, path, options = {}) {
  const response = await fetch(`https://api.github.com${path}`, {
    ...options,
    headers: {
      Accept: 'application/vnd.github+json',
      Authorization: `Bearer ${await installationToken(env)}`,
      'Content-Type': 'application/json',
      'User-Agent': 'owlswitch-diagnostics-relay',
      'X-GitHub-Api-Version': GITHUB_API_VERSION,
      ...(options.headers || {})
    }
  });
  const text = await response.text();
  const data = text ? JSON.parse(text) : null;
  if (!response.ok) throw Object.assign(new Error('GitHub delivery failed'), { status: response.status, errors: data?.errors, providerOperation: options.method });
  return data;
}

function issueBody(sanitized, reportFingerprint, state, existing = '') {
  const events = JSON.stringify(sanitized.report.context.recentEvents, null, 2)
    .replace(/```/g, "''' ");
  const versions = Object.entries(state.versions || {})
    .map(([version, count]) => `- ${boundText(version, 80)}: ${count}`).join('\n') || '- none';
  const body = `<!-- owlswitch-fingerprint:${reportFingerprint} -->
${stateMarker(state)}

## Summary

- Fingerprint: \`${reportFingerprint}\`
- Count: \`${state.count}\`
- First seen: \`${state.firstSeen}\`
- Last seen: \`${state.lastSeen}\`
- Latest version: \`${sanitized.app.version}\`
- Platform: \`${sanitized.app.os} / ${sanitized.app.arch}\`

## Recent sanitized events

\`\`\`json
${events}
\`\`\`

## Versions

${versions}

Reports are user initiated. The client and relay both exclude media, URLs, local paths,
emails, credentials, environment dumps, screenshots, and unrestricted log files.
`;
  const start = '<!-- owlswitch-diagnostics:start -->';
  const end = '<!-- owlswitch-diagnostics:end -->';
  const block = `${start}\n${body}\n${end}`;
  const first = existing.indexOf(start), last = existing.indexOf(end, first);
  if (first >= 0 && last >= first) return existing.slice(0, first) + block + existing.slice(last + end.length);
  const oldStart = existing.indexOf(`<!-- owlswitch-fingerprint:${reportFingerprint} -->`);
  const footer = 'emails, credentials, environment dumps, screenshots, and unrestricted log files.';
  const oldEnd = existing.indexOf(footer, oldStart);
  if (oldStart >= 0 && oldEnd >= oldStart) return existing.slice(0, oldStart) + block + existing.slice(oldEnd + footer.length);
  return existing ? `${existing}\n\n${block}` : block;
}

async function rateLimit(request, env) {
  if (!env.RATELIMIT || !env.REPORT_INDEX || !env.REPORT_GROUPS || !env.REPORT_IDS) {
    return { ok: false, status: 503, error: 'Report storage is not configured' };
  }
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  const windowSeconds = Math.max(60, Number(env.IP_WINDOW_SECONDS) || 3600);
  const bucket = Math.floor(Date.now() / (windowSeconds * 1000));
  const ipDigest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(ip));
  const ipKey = [...new Uint8Array(ipDigest)].slice(0, 8)
    .map((value) => value.toString(16).padStart(2, '0')).join('');
  const key = `ip:${ipKey}:${bucket}`;
  const count = Number(await env.RATELIMIT.get(key) || 0);
  if (count >= Math.max(1, Number(env.IP_LIMIT) || 10)) {
    return { ok: false, status: 429, error: 'Too many reports' };
  }
  await env.RATELIMIT.put(key, String(count + 1), { expirationTtl: windowSeconds * 2 });
  return { ok: true };
}

function createReporter(request = githubRequest) {
  return createGitHubIssueReporter({
    request, owner: 'aindaco1', repository: 'owl-switch',
    defaultLabels: 'crash,automated-report,needs-triage',
    markers: { state: 'owlswitch-report-state', fingerprint: 'owlswitch-fingerprint' },
    issueTitle: (report, fp) => `[OwlSwitch ${fp}] ${report.report.context.recentEvents.at(-1)?.message || 'Diagnostic report'}`.slice(0, 120),
    issueBody, groupingSummary: () => null,
    checkDailyIssueLimit: async () => ({ ok: true }), shouldUpdateIssue: async () => true
  });
}
const reporter = createReporter();
const { stateMarker, parseState } = reporter;

export class OwlReportGroup extends ReviewedReportGroup {
  constructor(ctx, env, request = githubRequest) {
    const api = createReporter(request);
    const adapter = {
      validate: value => {
        const report = sanitizePayload(value, env);
        return { ...report, id: report.report.id };
      },
      fingerprint, relayReport: report => report, repository: 'owl-switch',
      failureCode: 'owlswitch-submit-failed', receiptRetentionMS: 30 * 86400000,
      labels: () => env.GITHUB_LABELS || 'crash,automated-report,needs-triage',
      issueBody,
      reopen: issue => !issue.labels?.some(label => ['duplicate', 'do-not-reopen'].includes(typeof label === 'string' ? label : label.name)),
      initialState: async (_report, fp, index) => {
        // Adopt the existing issue and count before the first durable increment.
        // Provider failures stay failures; they must never become a second POST.
        const indexed = await env.REPORT_INDEX.get(`fp:${fp}`, { type: 'json' });
        let issue;
        if (indexed?.number) {
          try { issue = await request(env, `/repos/aindaco1/owl-switch/issues/${indexed.number}`, { method: 'GET' }); }
          catch (error) { if (error.status !== 404) throw error; }
        }
        if (!issue) {
          const q = encodeURIComponent(`repo:aindaco1/owl-switch is:issue in:body ${fp}`);
          const found = await request(env, `/search/issues?q=${q}&per_page=5`, { method: 'GET' });
          issue = found.items?.find(item => String(item.body || '').includes(api.fingerprintMarker(fp)));
        }
        if (!issue) return null;
        if (!String(issue.body || '').includes(api.fingerprintMarker(fp))) throw new Error('Legacy issue marker mismatch');
        const state = api.parseState(issue.body, fp);
        if (!Number.isSafeInteger(state.count) || state.count < 0) throw new Error('Invalid legacy aggregate');
        await index.put(`fp:${fp}`, JSON.stringify({ number: issue.number, url: issue.html_url || '' }));
        return state;
      }
    };
    super(ctx, env, adapter, { submit: api.submitCrashReport, updateAggregateState: api.updateAggregateState, owner: 'aindaco1' });
  }
  alarm() {
    const operation = this.tail.then(async () => {
      const entries = await this.ctx.storage.list({ prefix: 'receipt:' });
      const expired = [...entries].filter(([, value]) => value.expires <= Date.now()).map(([key]) => key);
      for (let i = 0; i < expired.length; i += 128) await this.ctx.storage.delete(expired.slice(i, i + 128));
      if (entries.size > expired.length) await this.ctx.storage.setAlarm(Date.now() + 86400000);
    });
    this.tail = operation.catch(() => {});
    return operation;
  }
}

// One durable inbox per report ID makes edited retries fail even if the edit
// changes the fingerprint. The original reviewed payload remains client-owned.
export class OwlReportID {
  constructor(ctx, env) { this.ctx = ctx; this.env = env; this.tail = Promise.resolve(); }
  fetch(request) {
    const operation = this.tail.then(() => this.accept(request)).catch(() => json({ error: 'Delivery not confirmed; retry the same report' }, 503));
    this.tail = operation.catch(() => {});
    return operation;
  }
  async accept(request) {
    const report = sanitizePayload(await request.json(), this.env);
    const bytes = new TextEncoder().encode(JSON.stringify(report));
    const digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map(x => x.toString(16).padStart(2, '0')).join('');
    let saved = await this.ctx.storage.get('report');
    if (saved && saved.expires <= Date.now()) saved = null;
    if (saved && saved.digest !== digest) return json({ error: 'Report ID belongs to a different reviewed draft' }, 409);
    if (saved?.receipt) return json({ ...saved.receipt, action: 'duplicate' });
    saved ??= { digest, expires: Date.now() + 30 * 86400000 };
    await this.ctx.storage.put('report', saved);
    await this.ctx.storage.setAlarm(saved.expires);
    const fp = await fingerprint(report);
    const group = this.env.REPORT_GROUPS.get(this.env.REPORT_GROUPS.idFromName(fp));
    const response = await group.fetch(new Request('https://internal/report', { method: 'POST', body: JSON.stringify(report) }));
    const receipt = await response.json();
    if (response.ok && receipt.ok === true && receipt.reportId === report.report.id && receipt.issueNumber > 0) {
      saved.receipt = receipt; await this.ctx.storage.put('report', saved);
    }
    return json(receipt, response.status);
  }
  async alarm() {
    const saved = await this.ctx.storage.get('report');
    if (!saved || saved.expires <= Date.now()) await this.ctx.storage.deleteAll();
    else await this.ctx.storage.setAlarm(saved.expires);
  }
}

function json(value, status = 200) {
  return new Response(JSON.stringify(value), {
    status, headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' }
  });
}

async function readPayload(request) {
  const bytes = await readBoundedBytes(request, MAX_PAYLOAD_BYTES, 'Report');
  return JSON.parse(new TextDecoder().decode(bytes));
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === 'GET' && url.pathname === '/health') {
      const status = readiness(env);
      return json(status, status.ok ? 200 : 503);
    }
    if (request.method !== 'POST' || url.pathname !== '/v1/reports') return json({ error: 'Not found' }, 404);
    if (String(env.REPORTS_ENABLED || 'false') !== 'true') return json({ error: 'Reporting disabled' }, 503);
    const limited = await rateLimit(request, env);
    if (!limited.ok) return json({ error: limited.error }, limited.status);
    try {
      const sanitized = sanitizePayload(await readPayload(request), env);
      const inbox = env.REPORT_IDS.get(env.REPORT_IDS.idFromName(sanitized.report.id));
      return await inbox.fetch(new Request('https://internal/report', { method: 'POST', body: JSON.stringify(sanitized) }));
    } catch (error) {
      return json({ error: boundText(error?.message || 'Report rejected', 200) }, 400);
    }
  }
};

export { boundText, fingerprint, issueBody, normalizedFingerprintInput, sanitizePayload };
