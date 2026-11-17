#!/usr/bin/env node
/**
 * CSV5/7: flatL csv.reader vs FleditorCsvParse on test1_csv_* (UTF-8 and UTF-16).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import fs from 'fs';
import path from 'path';
import os from 'os';
import { execSync } from 'child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const examplesDir = path.join(repo, 'docs/fleditor_site/examples');
const flc = path.join(repo, 'bin/flc');
const flvm = path.join(repo, 'bin/flvm');
const parityFl = path.join(repo, 'test/fleditor/csv_vm_parity.fl');

const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-text-format.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-parse.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-project.js'));

const P = globalThis.FleditorProject;
const C = globalThis.FleditorCsvParse;

/* test1_csv_* share one 8×2 table. */
const PARITY_FIXTURES = [
  'test1_csv_commadel.csv',
  'test1_csv_msdos.csv',
  'test1_csv_mac.csv',
  'test1_csv_utf8_commadel.csv',
  'test1_csv_utf16le.csv',
  'test1_csv_utf16be.csv',
];

function assert(cond, msg) {
  if (!cond) {
    console.error('csv_vm_parity:', msg);
    process.exit(1);
  }
}

function jsExpectedRows(name) {
  const filePath = path.join(examplesDir, name);
  const u8 = new Uint8Array(fs.readFileSync(filePath));
  const body = C.projectTextFromContent(P.bytesToProjectValue(u8, name));
  const parsed = C.parseFile(body, { autoDetect: true, quoteChar: '"' });
  const lines = parsed.rows.map((r) => r.join('|'));
  return { n: parsed.rows.length, lines, delimiter: parsed.delimiter };
}

function vmDumpRows(fixtureName) {
  const src = path.join(examplesDir, fixtureName);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'flatl-csv-parity-'));
  const fb = path.join(tmp, 'out.fb');
  fs.copyFileSync(src, path.join(tmp, 'data.csv'));
  execSync(`"${flc}" -o "${fb}" "${parityFl}"`, { stdio: 'pipe' });
  const out = execSync(`cd "${tmp}" && printf '' | "${flvm}" out.fb`, {
    encoding: 'utf8',
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  const lines = out
    .split(/\r?\n/)
    .map((l) => l.trimEnd())
    .filter((l) => l.length > 0);
  assert(lines.length >= 1, fixtureName + ': no VM output');
  return { n: lines.length, lines };
}

if (!fs.existsSync(flc) || !fs.existsSync(flvm)) {
  console.error('csv_vm_parity: build bin/flc and bin/flvm first (make)');
  process.exit(1);
}

for (const name of PARITY_FIXTURES) {
  const js = jsExpectedRows(name);
  assert(js.delimiter === ',', name + ': JS delimiter ' + js.delimiter);
  const vm = vmDumpRows(name);
  assert(vm.n === js.n, name + ': row count VM ' + vm.n + ' JS ' + js.n);
  for (let i = 0; i < js.n; i++) {
    assert(
      vm.lines[i] === js.lines[i],
      name + ' row ' + i + ': VM ' + JSON.stringify(vm.lines[i]) + ' JS ' + JSON.stringify(js.lines[i])
    );
  }
}

console.log('OK: csv_vm_parity (' + PARITY_FIXTURES.length + ' fixtures)');