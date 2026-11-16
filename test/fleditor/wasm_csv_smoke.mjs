#!/usr/bin/env node
/**
 * WASM smoke: write project CSV bytes to MEMFS, read back via FlatL file.*
 * Needs: make fleditor-wasm (docs/fleditor_site/wasm/flatl.js)
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import fs from 'fs';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');
if (!fs.existsSync(wasmJs)) {
  console.error('wasm_csv_smoke: missing', wasmJs, '(run make fleditor-wasm)');
  process.exit(1);
}

const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-text-format.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-parse.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-project.js'));

const P = globalThis.FleditorProject;
const MEMFS_ROOT = '/fleditor';

const readFl = `#main
  fp = file.readCtx('stock.csv')
  file.read(fp) -> body
  file.close(fp)
  io.outln(body)
  OK
`;

async function runEntry(files, entryRel) {
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
    const full = MEMFS_ROOT + '/' + rel;
    M.FS.writeFile(full, content);
  }
  const errBuf = M._malloc(4096);
  const entry = MEMFS_ROOT + '/' + entryRel;
  let rc = M.ccall(
    'flat_wasm_run_entry',
    'number',
    ['string', 'number', 'number'],
    [entry, errBuf, 4096],
    { async: true }
  );
  if (rc && typeof rc.then === 'function') rc = await rc;
  const err = M.UTF8ToString(errBuf);
  M._free(errBuf);
  return { rc, out, err };
}

const examplePath = path.join(repo, 'docs/fleditor_site/examples/test1_csv_commadel.csv');
const u8 = new Uint8Array(fs.readFileSync(examplePath));
const stored = P.bytesToProjectValue(u8, 'stock.csv');
const payload = P.projectValueToMemfsPayload(stored);

const r = await runEntry({ 'main.fl': readFl, 'stock.csv': payload }, 'main.fl');
if (r.rc !== 0) {
  console.error('wasm_csv_smoke: run failed', r.rc, r.err);
  process.exit(1);
}
if (r.out.indexOf('Product') < 0 || r.out.indexOf('TV') < 0) {
  console.error('wasm_csv_smoke: unexpected stdout', JSON.stringify(r.out));
  process.exit(1);
}

const bomPath = path.join(repo, 'docs/fleditor_site/examples/test1_csv_utf8_commadel.csv');
const bomU8 = new Uint8Array(fs.readFileSync(bomPath));
const bomStored = P.bytesToProjectValue(bomU8, 'utf8.csv');
const bomPayload = P.projectValueToMemfsPayload(bomStored);
const r2 = await runEntry({ 'main.fl': readFl.replace(/stock\.csv/g, 'utf8.csv'), 'utf8.csv': bomPayload }, 'main.fl');
if (r2.rc !== 0 || r2.out.indexOf('Quantity') < 0) {
  console.error('wasm_csv_smoke: utf8-bom path', r2.rc, r2.err, r2.out);
  process.exit(1);
}

console.log('OK: wasm_csv_smoke');