#!/usr/bin/env node
/**
 * Headless FLEditor WASM integration: Task natives and runtime Channels run
 * through the cooperative FlatRuntime root-task bridge.
 */
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmDir = path.join(repo, 'docs/fleditor_site/wasm');
const examplesDir = path.join(repo, 'docs/fleditor_site/examples/task_channel');
const require = createRequire(import.meta.url);
const FlatLModule = require(path.join(wasmDir, 'flatl.js'));

let stdout = '';
let stderr = '';
const M = await FlatLModule({
  locateFile: (name) => path.join(wasmDir, name),
  flatWasmOnStdout: (bytes) => { stdout += Buffer.from(bytes).toString('utf8'); },
  flatWasmOnStderr: (bytes) => { stderr += Buffer.from(bytes).toString('utf8'); },
});

async function runExample(file, expected) {
  const source = fs.readFileSync(path.join(examplesDir, file), 'utf8');
  const errPtr = M._malloc(4096);
  assert.ok(errPtr, 'WASM error buffer allocation');
  M.HEAPU8.fill(0, errPtr, errPtr + 4096);
  stdout = '';
  stderr = '';
  let rc = M.ccall('flat_wasm_run_source', 'number',
    ['string', 'number', 'number'], [source, errPtr, 4096], { async: true });
  if (rc && typeof rc.then === 'function') rc = await rc;
  const err = M.UTF8ToString(errPtr);
  const term = M.ccall('flat_wasm_get_last_termination', 'number', [], []);
  M._free(errPtr);
  assert.equal(rc, 0, `${file}: rc=${rc}, err=${err}, stderr=${stderr}`);
  assert.equal(term, 1, `${file}: expected FLAT_TERM_OK, got ${term}`);
  for (const fragment of expected) {
    assert.ok(stdout.includes(fragment), `${file}: missing ${JSON.stringify(fragment)} in ${JSON.stringify(stdout)}`);
  }
}

try {
  await runExample('parallel_inventory.fl', [
    'available=\n8\n', 'worker states:\n5\n5\n5\n',
  ]);
  await runExample('transaction_rollback.fl', ['committed price=\n7\n']);
  await runExample('task_results.fl', [
    'successful state=\n5\nsuccessful exit=\n0\n',
    'failed state=\n6\nfailed exit=\n2\nfailed termination=\n3\n',
  ]);
  console.log('OK: fleditor wasm Task/Channel cooperative runtime (inventory, rollback, task results)');
} catch (error) {
  console.error('wasm_task_channel_smoke: FAILED');
  console.error(error);
  process.exit(1);
}
