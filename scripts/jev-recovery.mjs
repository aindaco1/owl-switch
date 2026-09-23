// Development-only adapter. Transport, response validation and review routing
// come from the pinned shared package, not a second OwlSwitch implementation.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createJevRequest, evaluateJevCases, callCloudflareJev } from '../shared/dust-wave-platform/packages/test-core/src/jev.js';

export const ROOT = fileURLToPath(new URL('../', import.meta.url));
export const POLICY = { minimumMargin: 0.10, models: ['jev-1.13.0'] };
// Dated TypeSafe list-price estimate; not a provider-enforced billing limit.
const INPUT_USD_PER_MILLION = 0.042;
const hash = (value) => crypto.createHash('sha256').update(value).digest('hex');
const object = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
const keysEqual = (value, keys) => object(value) && Object.keys(value).sort().join('\0') === [...keys].sort().join('\0');

export function readJson(file) {
  const info = fs.lstatSync(file);
  if (!info.isFile() || info.size > 64_000) throw new Error('Expected bounded regular evidence');
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

export function specifications() {
  return readJson(path.join(ROOT, 'tests/fixtures/jev-recovery.json'));
}

export function prepareCorpus(directory) {
  const specs = specifications();
  if (!fs.lstatSync(directory).isDirectory()) throw new Error('Expected a fresh evidence directory');
  const names = fs.readdirSync(directory).sort();
  if (names.join('\0') !== Object.keys(specs).map((id) => `${id}.json`).sort().join('\0')) {
    throw new Error('Missing or unexpected recovery evidence; skipped tests cannot pass');
  }
  const observed = Object.entries(specs).map(([id, spec]) => {
    const row = readJson(path.join(directory, `${id}.json`));
    if (!keysEqual(row, ['id', 'candidate', 'facts']) || row.id !== id ||
        !keysEqual(row.facts, spec.facts) || !Object.values(row.facts).every((v) => v === true)) {
      throw new Error('Recovery evidence does not match its deterministic contract');
    }
    // Only these freshly executed synthetic tests are exportable. This bound
    // is defense in depth, not a general-purpose private-data redactor.
    if (typeof row.candidate !== 'string' || !row.candidate.trim() || row.candidate.length > 2048 ||
        /[\x00-\x1f]|https?:\/\/|file:|\/(?:Users|Volumes|private|tmp|home)\/|[\w.+-]+@[\w.-]+|(?:token|password|secret)\s*[:=]/i.test(row.candidate)) {
      throw new Error('Only bounded public synthetic messages can be evaluated');
    }
    return { id, candidate: row.candidate, requirements: spec.requirements, facts: row.facts, split: 'observed' };
  });
  const controls = Object.entries(specs).flatMap(([id, spec]) => ['good', 'bad'].map((kind) => ({
    id: `calibration-${id}-${kind}`, candidate: spec[kind], requirements: spec.requirements,
    expected: kind === 'good' ? 'pass' : 'fail', split: 'calibration'
  })));
  const validation = readJson(path.join(ROOT, 'tests/fixtures/jev-validation.json')).flatMap((spec) => ['good', 'bad'].map((kind) => ({
    id: `validation-${spec.id}-${kind}`, candidate: spec[kind], requirements: specs[spec.case].requirements,
    expected: kind === 'good' ? 'pass' : 'fail', split: 'validation'
  })));
  return [...controls, ...validation, ...observed];
}

export function budgetFor(corpus) {
  const requests = corpus.map((row) => createJevRequest(row.candidate, row.requirements));
  const questions = requests.reduce((sum, row) => sum + Object.keys(row.input.questions).length, 0);
  const estimatedUsd = questions * 32_000 * INPUT_USD_PER_MILLION / 1_000_000;
  if (questions > 100 || estimatedUsd > 0.25) throw new Error('Jev question/estimated spending limit exceeded');
  return { questions, estimatedUsd, inputUsdPerMillion: INPUT_USD_PER_MILLION };
}

export function sourceHashes() {
  const paths = ['CMakeLists.txt', 'Main.qml', 'scripts/test.mjs', 'scripts/jev-recovery.mjs',
    'shared/dust-wave-platform/packages/test-core/src/jev.js',
    'shared/dust-wave-platform/packages/worker-core/src/response-body.js',
    'shared/dust-wave-platform/packages/worker-core/src/bounded-stream.js'];
  function walk(directory) {
    for (const item of fs.readdirSync(path.join(ROOT, directory), { withFileTypes: true })) {
      const relative = path.join(directory, item.name);
      if (item.isSymbolicLink()) throw new Error('Source provenance cannot follow symlinks');
      if (item.isDirectory()) walk(relative);
      else if (/\.(?:cpp|h|mm|qml|json|py|mjs|lua|cmake|zsh)$/.test(item.name)) paths.push(relative);
    }
  }
  for (const directory of ['src', 'modules', 'views', 'tests']) walk(directory);
  return Object.fromEntries(paths.sort().map((file) => [file, hash(fs.readFileSync(path.join(ROOT, file)))]));
}

export function credentials() {
  let accountId = process.env.CLOUDFLARE_ACCOUNT_ID;
  let token = process.env.CLOUDFLARE_API_TOKEN;
  try {
    const config = path.join(ROOT, '.owl-switch-development.json');
    if (!accountId && fs.existsSync(config)) accountId = readJson(config).cloudflare_account_id;
    if (!/^[a-fA-F0-9]{32}$/.test(accountId || '')) throw new Error();
    if (!token) {
      const value = JSON.parse(execFileSync('npx', ['--no-install', 'wrangler@4.136.2', 'auth', 'token', '--json'],
        { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 45_000 }));
      token = value.token || value.access_token;
    }
    if (typeof token !== 'string' || !token.trim()) throw new Error();
  } catch {
    throw new Error('Jev authentication unavailable. Configure CLOUDFLARE_ACCOUNT_ID and CLOUDFLARE_API_TOKEN or existing Wrangler login; use --offline for local checks.');
  }
  return { accountId, token };
}

export function summarize(report, corpus) {
  const results = new Map(report.cases.map((row) => [row.id, row]));
  const summary = { calibration: { passed: 0, flagged: 0, unevaluated: 0 }, validation: { passed: 0, flagged: 0, unevaluated: 0 },
    observed: { passed: 0, flagged: 0, unevaluated: 0 }, inputTokens: 0, combinedPassed: report.complete === true };
  for (const row of corpus) {
    const result = results.get(row.id)?.result;
    const findings = Object.values(result?.findings || {});
    const passed = findings.length === Object.keys(row.requirements).length &&
      findings.every((finding) => finding.decision === (row.expected || 'pass'));
    summary[row.split][!result ? 'unevaluated' : passed ? 'passed' : 'flagged']++;
    summary.inputTokens += result?.usage.input_tokens || 0;
    summary.combinedPassed &&= passed;
  }
  summary.estimatedInferenceUsd = summary.inputTokens * INPUT_USD_PER_MILLION / 1_000_000;
  return summary;
}

export function reviewMarkdown(report, corpus) {
  const lines = ['# OwlSwitch Jev recovery review', '',
    `Mode: ${report.mode}. Local checks passed: ${report.localPassed}. Live evaluation complete: ${report.complete}.`,
    `Combined development pass: ${report.summary.combinedPassed}. Network attempts: ${report.networkAttempts}.`,
    ...(report.error ? [`Error: ${report.error}`] : []), '',
    'Meaning checks complement deterministic recovery assertions. This is not GUI, physical media, or release acceptance.', '',
    'Fixed 0.10 margin; near ties, uncertainty, and unseen models require review. No threshold tuning occurs during tests.', '',
    `Summary: ${JSON.stringify(report.summary)}`, ''];
  const results = new Map(report.cases.map((row) => [row.id, row]));
  for (const source of corpus) {
    const row = results.get(source.id);
    lines.push(`## ${source.id}`, '', `Candidate: ${source.candidate}`, '');
    for (const [key, requirement] of Object.entries(source.requirements)) {
      const finding = row?.result?.findings[key];
      lines.push(`- ${finding?.decision || 'not evaluated'} (expected ${source.expected || 'pass'}): ${requirement}`);
      if (finding) lines.push(`  Model: ${row.result.model}; probabilities: ${JSON.stringify(finding.probabilities)}; margin: ${finding.margin}.`);
    }
    lines.push('');
  }
  return lines.join('\n');
}

export async function evaluate(corpus, output, metadata, { offline = false, authenticate = credentials, call = callCloudflareJev } = {}) {
  const budget = budgetFor(corpus); // Entire batch prepared before authentication.
  let final;
  const save = async (report) => {
    final = { ...report, ...metadata, mode: offline ? 'offline' : 'live', budget, summary: summarize(report, corpus) };
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(final, null, 2) + '\n', { mode: 0o600 });
    fs.writeFileSync(path.join(output, 'review.md'), reviewMarkdown(final, corpus), { mode: 0o600 });
  };
  const preview = await evaluateJevCases(corpus, { policy: POLICY, onProgress: save });
  if (offline) return final;
  let auth;
  try { auth = authenticate(); } catch (error) {
    await save({ ...preview, error: 'Jev authentication unavailable; no requests sent. Configure credentials or explicitly use --offline.' });
    throw error;
  }
  await evaluateJevCases(corpus, { policy: POLICY, onProgress: save, call: (payload) => call(payload, auth) });
  return final;
}
