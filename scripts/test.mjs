#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { ROOT, sourceHashes, prepareCorpus, evaluate } from './jev-recovery.mjs';

const args = process.argv.slice(2);
if (args.includes('--help')) {
  console.log('node scripts/test.mjs [--offline] [--build-dir PATH]\nBuild, CTest, recovery evidence and live Jev. --offline explicitly skips authentication and Jev requests.');
  process.exit(0);
}
let offline = false;
let build = path.join(ROOT, 'build');
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--offline') offline = true;
  else if (args[i] === '--build-dir' && args[i + 1] && !args[i + 1].startsWith('--')) build = path.resolve(args[++i]);
  else { console.error('Unknown argument; use --help.'); process.exit(2); }
}

function run(command, arguments_, env = process.env) {
  const result = spawnSync(command, arguments_, { cwd: ROOT, stdio: 'inherit', env });
  if (result.error || result.status !== 0) throw new Error(`${command} did not complete successfully; live evaluation was not started.`);
}

let output;
try {
  // Tests use mocked provider responses; nothing live is called by unit tests.
  run(process.execPath, ['--test', 'tests/JevRecoveryTest.mjs', 'shared/dust-wave-platform/packages/test-core/test/jev.test.js']);
  const before = sourceHashes();
  if (!fs.existsSync(path.join(build, 'CMakeCache.txt'))) {
    run('cmake', ['-S', ROOT, '-B', build, `-DCMAKE_PREFIX_PATH=${process.env.QT_ROOT_DIR || '/opt/homebrew/opt/qt'}`]);
  }
  run('cmake', ['--build', build, '--parallel', '3']);
  const parent = path.join(ROOT, 'build', 'jev');
  fs.mkdirSync(parent, { recursive: true });
  output = fs.mkdtempSync(path.join(parent, 'run-'));
  fs.chmodSync(output, 0o700);
  const evidence = path.join(output, 'native');
  fs.mkdirSync(evidence, { mode: 0o700 });
  console.log(`Recovery evidence: ${output}`);
  // Pin live canaries off: synthetic test output is the only exportable input.
  const env = { ...process.env, OWLSWITCH_TEST_EVIDENCE: evidence };
  for (const key of ['KARAOKE_LIVE_TEST', 'LOCAL_FILES_LIVE_TEST', 'NATURE_LIVE_TEST', 'NATURE_AUDIO_LIVE_TEST', 'NATURE_AUDIO_REAL_MPV']) delete env[key];
  run('ctest', ['--test-dir', build, '--output-on-failure'], env);
  const after = sourceHashes();
  if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error('Sources changed during testing; no evidence was sent.');
  const corpus = prepareCorpus(evidence);
  fs.writeFileSync(path.join(output, 'corpus.json'), JSON.stringify(corpus, null, 2) + '\n', { mode: 0o600 });
  const report = await evaluate(corpus, output, { localPassed: true, sourceHashes: after,
    createdAt: new Date().toISOString(), platform: process.platform, architecture: process.arch }, { offline });
  console.log(JSON.stringify({ output, mode: report.mode, networkAttempts: report.networkAttempts, ...report.summary }, null, 2));
  if (offline) console.log('Local checks passed. Jev was explicitly skipped; this is not a combined pass.');
  process.exitCode = offline || report.summary.combinedPassed ? 0 : report.complete ? 1 : 2;
} catch (error) {
  console.error(error.message);
  if (output) console.error(`Local evidence retained: ${output}`);
  process.exitCode = 2;
}
