import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { specifications, prepareCorpus, budgetFor, evaluate, summarize } from '../scripts/jev-recovery.mjs';

function fixture(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'owl-jev-test-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const evidence = path.join(directory, 'native');
  fs.mkdirSync(evidence);
  for (const [id, spec] of Object.entries(specifications())) {
    fs.writeFileSync(path.join(evidence, `${id}.json`), JSON.stringify({ id, candidate: spec.good,
      facts: Object.fromEntries(spec.facts.map((key) => [key, true])) }));
  }
  return { directory, evidence };
}

function response(payload, choice, model = 'jev-1.13.0') {
  return { model, answers: Object.fromEntries(Object.keys(payload.input.questions).map((key) => [key, {
    type: 'choice', choice, probabilities: { pass: choice === 'pass' ? 0.98 : 0.01,
      fail: choice === 'fail' ? 0.98 : 0.01, uncertain: 0.01 }
  }])), usage: { input_tokens: 100, output_tokens: 0 } };
}

test('fresh evidence includes actual candidates and separate good/bad controls', (t) => {
  const { evidence } = fixture(t);
  const corpus = prepareCorpus(evidence);
  assert.equal(corpus.filter((row) => row.split === 'observed').length, 5);
  assert.equal(corpus.filter((row) => row.split === 'calibration').length, 10);
  assert.equal(corpus.filter((row) => row.split === 'validation').length, 6);
  assert.equal(budgetFor(corpus).questions, 26);
});

test('missing, extra, symlinked, malformed and private evidence fails before review', (t) => {
  const { evidence } = fixture(t);
  const file = path.join(evidence, 'jellyfin-auth.json');
  const original = fs.readFileSync(file);
  for (const candidate of ['https://private.example/token', '/Users/person/movie.mp4', 'token=secret', 'user@example.com', '', 'x'.repeat(2049)]) {
    fs.writeFileSync(file, JSON.stringify({ ...JSON.parse(original), candidate }));
    assert.throws(() => prepareCorpus(evidence));
  }
  for (const changes of [{ facts: { no_stream_started: false } }, { facts: {} }, { id: 'custom' }, { secret: 'extra' }]) {
    fs.writeFileSync(file, JSON.stringify({ ...JSON.parse(original), ...changes }));
    assert.throws(() => prepareCorpus(evidence));
  }
  fs.unlinkSync(file);
  assert.throws(() => prepareCorpus(evidence));
  fs.symlinkSync(path.join(evidence, 'nature-sound.json'), file);
  assert.throws(() => prepareCorpus(evidence));
  fs.unlinkSync(file);
  fs.writeFileSync(file, original);
  fs.writeFileSync(path.join(evidence, 'extra.json'), '{}');
  assert.throws(() => prepareCorpus(evidence));
});

test('offline never authenticates or calls provider and cannot claim combined pass', async (t) => {
  const { directory, evidence } = fixture(t);
  const report = await evaluate(prepareCorpus(evidence), directory, { localPassed: true }, { offline: true,
    authenticate: () => assert.fail('offline authentication'), call: () => assert.fail('offline network') });
  assert.equal(report.networkAttempts, 0);
  assert.equal(report.complete, false);
  assert.equal(report.summary.combinedPassed, false);
  assert.match(fs.readFileSync(path.join(directory, 'review.md'), 'utf8'), /not evaluated/);
});

test('known bad controls must fail while observed candidates must pass', async (t) => {
  const { directory, evidence } = fixture(t);
  const corpus = prepareCorpus(evidence);
  let index = 0;
  const report = await evaluate(corpus, directory, { localPassed: true }, {
    authenticate: () => ({}), call: async (payload) => response(payload, corpus[index++].expected || 'pass') });
  assert.equal(report.summary.combinedPassed, true);
  assert.equal(report.summary.calibration.passed, 10);
  assert.equal(report.summary.validation.passed, 6);
  assert.equal(report.summary.observed.passed, 5);
  // A judge that passes every message, including known bad ones, must fail.
  const allPass = await evaluate(corpus, directory, { localPassed: true }, {
    authenticate: () => ({}), call: async (payload) => response(payload, 'pass') });
  assert.equal(allPass.summary.combinedPassed, false);
  assert.equal(allPass.summary.calibration.flagged, 5);
  assert.equal(allPass.summary.validation.flagged, 3);
});

test('unknown model and missing outcomes prevent a pass', async (t) => {
  const { directory, evidence } = fixture(t);
  const corpus = prepareCorpus(evidence);
  const report = await evaluate(corpus, directory, { localPassed: true }, {
    authenticate: () => ({}), call: async (payload) => response(payload, 'pass', 'unseen-model') });
  assert.equal(report.summary.combinedPassed, false);
  assert.equal(report.summary.observed.flagged, 5);
  assert.equal(summarize({ complete: true, cases: [] }, corpus).combinedPassed, false);
});

test('API failure is retained, fails closed and is never retried', async (t) => {
  const { directory, evidence } = fixture(t);
  let calls = 0;
  const report = await evaluate(prepareCorpus(evidence), directory, { localPassed: true }, {
    authenticate: () => ({}), call: async () => { calls++; throw Error('secret-body'); } });
  assert.equal(calls, 1);
  assert.equal(report.complete, false);
  assert.equal(report.summary.combinedPassed, false);
  assert.ok(report.error);
  assert.doesNotMatch(fs.readFileSync(path.join(directory, 'report.json'), 'utf8'), /secret-body/);
});

test('request budget and malformed late candidates are checked before authentication', async (t) => {
  const { directory, evidence } = fixture(t);
  const corpus = prepareCorpus(evidence);
  const options = { authenticate: () => assert.fail('authentication before validation') };
  await assert.rejects(evaluate([...corpus, { id: 'late', candidate: '' }], directory, {}, options));
  await assert.rejects(evaluate(Array.from({ length: 101 }, (_, i) => ({ ...corpus[0], id: `case-${i}` })), directory, {}, options));
});

test('missing authentication retains an explicitly incomplete report', async (t) => {
  const { directory, evidence } = fixture(t);
  await assert.rejects(evaluate(prepareCorpus(evidence), directory, { localPassed: true }, {
    authenticate: () => { throw Error('No authentication'); }, call: () => assert.fail('network') }));
  const report = JSON.parse(fs.readFileSync(path.join(directory, 'report.json')));
  assert.equal(report.complete, false);
  assert.equal(report.networkAttempts, 0);
  assert.equal(report.summary.combinedPassed, false);
});
