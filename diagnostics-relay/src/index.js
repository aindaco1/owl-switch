import { createAppAuth } from '@octokit/auth-app';

const MAX_PAYLOAD_BYTES = 24576;
const MAX_EVENTS = 20;
const GITHUB_API_VERSION = '2022-11-28';

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
  if (!response.ok) throw new Error(data?.message || `GitHub API error ${response.status}`);
  return data;
}

function stateMarker(state) {
  return `<!-- owlswitch-report-state:${btoa(JSON.stringify(state))} -->`;
}

function parseState(body, reportFingerprint) {
  const match = String(body || '').match(/<!-- owlswitch-report-state:([A-Za-z0-9+/=]+) -->/);
  if (match) {
    try { return { ...JSON.parse(atob(match[1])), fingerprint: reportFingerprint }; } catch {}
  }
  const now = new Date().toISOString();
  return { fingerprint: reportFingerprint, count: 0, firstSeen: now, lastSeen: now, versions: {} };
}

function issueBody(sanitized, reportFingerprint, state) {
  const events = JSON.stringify(sanitized.report.context.recentEvents, null, 2)
    .replace(/```/g, "''' ");
  const versions = Object.entries(state.versions || {})
    .map(([version, count]) => `- ${boundText(version, 80)}: ${count}`).join('\n') || '- none';
  return `<!-- owlswitch-fingerprint:${reportFingerprint} -->
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
}

async function rateLimit(request, env) {
  if (!env.RATELIMIT || !env.REPORT_INDEX) {
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

async function submitToGitHub(env, sanitized, reportFingerprint) {
  const owner = String(env.GITHUB_OWNER || 'aindaco1');
  const repo = String(env.GITHUB_REPO || 'owl-switch');
  const indexed = await env.REPORT_INDEX.get(`fp:${reportFingerprint}`, { type: 'json' });
  let issue = null;
  if (indexed?.number) {
    issue = await githubRequest(env, `/repos/${owner}/${repo}/issues/${indexed.number}`,
      { method: 'GET' }).catch(() => null);
    if (issue?.state !== 'open') issue = null;
  }
  const state = parseState(issue?.body, reportFingerprint);
  state.count = Number(state.count || 0) + 1;
  state.lastSeen = new Date().toISOString();
  state.versions = { ...(state.versions || {}) };
  state.versions[sanitized.app.version] = Number(state.versions[sanitized.app.version] || 0) + 1;
  const body = issueBody(sanitized, reportFingerprint, state);

  if (issue) {
    issue = await githubRequest(env, `/repos/${owner}/${repo}/issues/${issue.number}`, {
      method: 'PATCH', body: JSON.stringify({ body })
    });
  } else {
    const title = `[OwlSwitch ${reportFingerprint}] ${sanitized.report.context.recentEvents.at(-1)?.message || 'Diagnostic report'}`
      .slice(0, 120);
    const labels = String(env.GITHUB_LABELS || '').split(',').map((item) => item.trim()).filter(Boolean);
    const create = { title, body, labels };
    try {
      issue = await githubRequest(env, `/repos/${owner}/${repo}/issues`, {
        method: 'POST', body: JSON.stringify(create)
      });
    } catch (error) {
      if (!labels.length) throw error;
      issue = await githubRequest(env, `/repos/${owner}/${repo}/issues`, {
        method: 'POST', body: JSON.stringify({ title, body })
      });
    }
  }
  await env.REPORT_INDEX.put(`fp:${reportFingerprint}`, JSON.stringify({
    number: issue.number, url: issue.html_url, updatedAt: new Date().toISOString()
  }));
  return { action: state.count === 1 ? 'created' : 'aggregated', issueNumber: issue.number };
}

function json(value, status = 200) {
  return new Response(JSON.stringify(value), {
    status, headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' }
  });
}

async function readPayload(request) {
  const length = Number(request.headers.get('Content-Length') || 0);
  if (length > MAX_PAYLOAD_BYTES) throw new Error('Report is too large');
  const bytes = await request.arrayBuffer();
  if (bytes.byteLength > MAX_PAYLOAD_BYTES) throw new Error('Report is too large');
  return JSON.parse(new TextDecoder().decode(bytes));
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === 'GET' && url.pathname === '/health') {
      return json({ ok: true, service: 'owlswitch-diagnostics-relay' });
    }
    if (request.method !== 'POST' || url.pathname !== '/v1/reports') return json({ error: 'Not found' }, 404);
    if (String(env.REPORTS_ENABLED || 'false') !== 'true') return json({ error: 'Reporting disabled' }, 503);
    const limited = await rateLimit(request, env);
    if (!limited.ok) return json({ error: limited.error }, limited.status);
    try {
      const sanitized = sanitizePayload(await readPayload(request), env);
      const reportFingerprint = await fingerprint(sanitized);
      const result = await submitToGitHub(env, sanitized, reportFingerprint);
      return json({ ok: true, fingerprint: reportFingerprint, ...result });
    } catch (error) {
      return json({ error: boundText(error?.message || 'Report rejected', 200) }, 400);
    }
  }
};

export { boundText, fingerprint, issueBody, normalizedFingerprintInput, sanitizePayload };
