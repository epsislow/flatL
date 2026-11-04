#!/usr/bin/env node
/**
 * Headless smoke: compile+run hello via WASM (needs make fleditor-wasm).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');

const require = createRequire(import.meta.url);
const FlatLModule = require(wasmJs);

async function runSource(Module, source, onNeedLine) {
  let out = '';
  const cfg = {
    locateFile: (p) => path.join(repo, 'docs/fleditor_site/wasm', p),
    flatWasmOnStdout: (u8) => {
      out += Buffer.from(u8).toString('utf8');
    },
    flatWasmOnNeedLine: onNeedLine,
  };
  const M = await FlatLModule(cfg);
  const errBuf = M._malloc(4096);
  let rc = M.ccall(
    'flat_wasm_run_source',
    'number',
    ['string', 'number', 'number'],
    [source, errBuf, 4096],
    { async: true }
  );
  if (rc && typeof rc.then === 'function') rc = await rc;
  const err = M.UTF8ToString(errBuf);
  let term = 0;
  try {
    term = M.ccall('flat_wasm_get_last_termination', 'number', [], []);
  } catch (_e) {
    /* ignore */
  }
  M._free(errBuf);
  return { rc, out, err, term };
}

const hello = "#main\n  io.outln('hello from fleditor')\n  OK\n";
const r1 = await runSource(null, hello, null);
if (r1.rc !== 0 || !r1.out.includes('hello from fleditor')) {
  console.error('wasm_smoke: hello failed', r1);
  process.exit(1);
}

const failProg = "#main\n  io.outln('before fail')\n  FAIL\n  OK\n";
const rFail = await runSource(null, failProg, null);
if (rFail.rc !== 2 || rFail.err !== '' || rFail.term !== 3 || !rFail.out.includes('before fail')) {
  console.error('wasm_smoke: FAIL line failed', rFail);
  process.exit(1);
}

const inln = "#main\n  io.inln('Name:') -> name\n  io.outln(name)\n  OK\n";
const r2 = await runSource(null, inln, (_ae, prompt, kind, wake) => {
  if (typeof prompt === 'function') {
    prompt('Ada', 0);
    return;
  }
  if (prompt !== 'Name:') {
    console.error('wasm_smoke: unexpected prompt', JSON.stringify(prompt), 'kind', kind);
    process.exit(1);
  }
  wake('Ada', 0);
});
if (r2.rc !== 0 || !r2.out.includes('Ada')) {
  console.error('wasm_smoke: inln failed', r2);
  process.exit(1);
}

console.log('OK: fleditor wasm smoke (hello + FAIL + inln)');
