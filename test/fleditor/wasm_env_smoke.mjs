#!/usr/bin/env node
/**
 * WASM env contract: no OS getenv leak; Settings-style inject for get/args.
 * Also checks FleditorSettings.splitProgramArgs quoting (Program args field).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');
const settingsJs = path.join(repo, 'docs/fleditor_site/js/fleditor-settings.js');
const themeJs = path.join(repo, 'docs/fleditor_site/js/fleditor-theme.js');

const require = createRequire(import.meta.url);
const FlatLModule = require(wasmJs);

/* Load settings helpers (depends on FleditorTheme on globalThis). */
require(themeJs);
require(settingsJs);
const S = globalThis.FleditorSettings;
if (!S || typeof S.splitProgramArgs !== 'function') {
  console.error('wasm_env_smoke: splitProgramArgs not exported');
  process.exit(1);
}

const quoted = S.splitProgramArgs(`alpha "hello world" 'x y'`);
if (quoted.length !== 3 || quoted[0] !== 'alpha' || quoted[1] !== 'hello world' || quoted[2] !== 'x y') {
  console.error('wasm_env_smoke: splitProgramArgs failed', quoted);
  process.exit(1);
}

/* Intentionally set a process env that must NOT be visible to env.has in WASM. */
process.env.FLAT_ENV_WASM_LEAK = 'should_not_appear';

async function withModule(fn) {
  let out = '';
  const M = await FlatLModule({
    locateFile: (p) => path.join(repo, 'docs/fleditor_site/wasm', p),
    flatWasmOnStdout: (u8) => {
      out += Buffer.from(u8).toString('utf8');
    },
  });
  return fn(M, () => out, () => {
    out = '';
  });
}

async function runSource(M, source, clearOut) {
  clearOut();
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
  return { rc, err };
}

const leakProg = `use native env
#main
  io.outln(env.has('FLAT_ENV_WASM_LEAK'))
  OK
`;

const injectProg = `use native env
use native list
#main
  io.outln(env.get('FLAT_ENV_WASM_OK'))
  io.outln(env.argc())
  xs = env.args()
  io.outln(list.get(xs, 0))
  io.outln(list.get(xs, 1))
  OK
`;

await withModule(async (M, getOut, clearOut) => {
  /* Even without flat_wasm_env_clear, run must not see Node process env. */
  let r = await runSource(M, leakProg, clearOut);
  if (r.rc !== 0 || !getOut().includes('false')) {
    console.error('wasm_env_smoke: process env leaked into WASM', r, getOut());
    process.exit(1);
  }

  M._flat_wasm_env_clear();
  M.ccall('flat_wasm_env_set', 'number', ['string', 'string'], ['FLAT_ENV_WASM_OK', 'from_settings']);
  M._flat_wasm_env_args_clear();
  const args = S.splitProgramArgs(`'hello world' beta`);
  for (const a of args) {
    M.ccall('flat_wasm_env_args_push', 'number', ['string'], [a]);
  }

  r = await runSource(M, injectProg, clearOut);
  const out = getOut();
  if (
    r.rc !== 0 ||
    !out.includes('from_settings') ||
    !out.includes('2\n') ||
    !out.includes('hello world') ||
    !out.includes('beta')
  ) {
    console.error('wasm_env_smoke: inject failed', r, out);
    process.exit(1);
  }
});

console.log('OK: fleditor wasm env smoke (no leak + Settings inject + splitProgramArgs)');
