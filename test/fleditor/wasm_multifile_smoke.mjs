#!/usr/bin/env node
/**
 * Headless smoke: two-file use "lib/a.fl" via MEMFS + flat_wasm_run_entry.
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmJs = path.join(repo, 'fleditor_site/wasm/flatl.js');

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
    locateFile: (p) => path.join(repo, 'fleditor_site/wasm', p),
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

const libA = "#helper\n  io.outln('from lib')\n  return\n";
const main = 'use "lib/a.fl" as #Lib_\n\n#main\n  call #Lib_helper\n  io.outln(\'done\')\n  OK\n';

const r = await runEntry({ 'main.fl': main, 'lib/a.fl': libA }, 'main.fl');
if (r.rc !== 0 || !r.out.includes('from lib') || !r.out.includes('done')) {
  console.error('wasm_multifile_smoke failed', r);
  process.exit(1);
}

console.log('OK: fleditor wasm multifile smoke');
