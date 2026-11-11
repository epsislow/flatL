#!/usr/bin/env node
/**
 * WASM: simple assign + io.outln(var) (regression for set_var_value stack UB).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');

const require = createRequire(import.meta.url);
const FlatLModule = require(wasmJs);

async function runSource(source) {
  let out = '';
  const M = await FlatLModule({
    locateFile: (p) => path.join(repo, 'docs/fleditor_site/wasm', p),
    flatWasmOnStdout: (u8) => {
      out += Buffer.from(u8).toString('utf8');
    },
  });
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
  M._free(errBuf);
  return { rc, out, err };
}

const prog = "#main\n  x = 1\n  io.outln(x)\n  OK\n";
const r = await runSource(prog);
if (r.rc !== 0 || !r.out.includes('1\n') || r.err !== '') {
  console.error('wasm_var_smoke failed', r);
  process.exit(1);
}

console.log('OK: fleditor wasm var smoke');
