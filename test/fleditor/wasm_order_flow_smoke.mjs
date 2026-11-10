#!/usr/bin/env node
/**
 * WASM smoke: fleditor order_flow.fl + blocks/executor.fl (user def, v0.4).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import fs from 'fs';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');
const examplesDir = path.join(repo, 'docs/fleditor_site/examples');

const require = createRequire(import.meta.url);
const FlatLModule = require(wasmJs);

const MEMFS_ROOT = '/fleditor';

function writeProject(M, files) {
  if (typeof M.FS.rmTree === 'function') {
    try {
      M.FS.rmTree(MEMFS_ROOT);
    } catch (_e) {
      /* ok */
    }
  }
  M.FS.mkdirTree(MEMFS_ROOT);
  for (const [rel, content] of Object.entries(files)) {
    const full = MEMFS_ROOT + '/' + rel;
    const slash = full.lastIndexOf('/');
    if (slash > MEMFS_ROOT.length) {
      M.FS.mkdirTree(full.slice(0, slash));
    }
    M.FS.writeFile(full, content);
  }
}

async function runEntry(files, entryRel) {
  let out = '';
  const cfg = {
    locateFile: (p) => path.join(repo, 'docs/fleditor_site/wasm', p),
    flatWasmOnStdout: (u8) => {
      out += Buffer.from(u8).toString('utf8');
    },
  };
  const M = await FlatLModule(cfg);
  writeProject(M, files);
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

const order = fs.readFileSync(path.join(examplesDir, 'order_flow.fl'), 'utf8');
const executor = fs.readFileSync(path.join(examplesDir, 'blocks/executor.fl'), 'utf8');

const r = await runEntry(
  {
    'examples/order_flow.fl': order,
    'examples/blocks/executor.fl': executor,
  },
  'examples/order_flow.fl'
);

if (r.err && r.err.includes('expected use directive')) {
  console.error('wasm_order_flow_smoke: stale WASM (missing flat_func) — run: make fleditor-wasm');
  console.error(r.err);
  process.exit(1);
}
if (r.rc !== 0 || !r.out.includes('payment_status=SUCCESS')) {
  console.error('wasm_order_flow_smoke failed', r);
  process.exit(1);
}

console.log('OK: fleditor wasm order_flow smoke');
