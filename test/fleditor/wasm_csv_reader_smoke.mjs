#!/usr/bin/env node
/**
 * CSV6: create the project directory with FlatL's fs module, then read stock.csv
 * through the native CSV module on MEMFS. The CSV module does not create folders.
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import fs from 'fs';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');
const exDir = path.join(repo, 'docs/fleditor_site/examples/csv_reader');

if (!fs.existsSync(wasmJs)) {
  console.error('wasm_csv_reader_smoke: missing', wasmJs, '(run make fleditor-wasm)');
  process.exit(1);
}

const require = createRequire(import.meta.url);
const mainFl = fs.readFileSync(path.join(exDir, 'csv_reader.fl'), 'utf8');
const stockCsv = fs.readFileSync(path.join(exDir, 'stock.csv'), 'utf8');

const MEMFS_ROOT = '/fleditor';

async function runEntry(files) {
  const FlatLModule = require(wasmJs);
  let out = '';
  const cfg = {
    locateFile: (p) => path.join(repo, 'docs/fleditor_site/wasm', p),
    flatWasmOnStdout: (u8) => {
      out += Buffer.from(u8).toString('utf8');
    },
  };
  const M = await FlatLModule(cfg);
  M.FS.mkdirTree(MEMFS_ROOT);
  for (const [rel, content] of Object.entries(files)) {
    // Only root-level fixtures are staged by the harness. The FlatL program
    // itself creates the nested project directories with fs.mkdir().
    M.FS.writeFile(MEMFS_ROOT + '/' + rel, content);
  }
  const errBuf = M._malloc(4096);
  let rc = M.ccall(
    'flat_wasm_run_entry',
    'number',
    ['string', 'number', 'number'],
    [MEMFS_ROOT + '/csv_reader.fl', errBuf, 4096],
    { async: true }
  );
  if (rc && typeof rc.then === 'function') rc = await rc;
  const err = M.UTF8ToString(errBuf);
  M._free(errBuf);
  return { rc, out, err };
}

const r = await runEntry({
  'csv_reader.fl': mainFl,
  'stock.csv': stockCsv,
});
if (r.rc !== 0) {
  console.error('wasm_csv_reader_smoke: run failed', r.rc, r.err);
  process.exit(1);
}
if (
  r.out.indexOf('sku,qty,price') < 0 ||
  r.out.indexOf('A001') < 0 ||
  r.out.indexOf('9.99') < 0
) {
  console.error('wasm_csv_reader_smoke: unexpected stdout', JSON.stringify(r.out));
  process.exit(1);
}

console.log('OK: wasm_csv_reader_smoke (FlatL fs.mkdir + CSV reader)');
