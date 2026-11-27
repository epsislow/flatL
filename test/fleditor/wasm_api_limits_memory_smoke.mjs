#!/usr/bin/env node
/**
 * Headless WASM API regression for VM run limits, panic recovery and linear-memory growth.
 * Requires the real Emscripten module produced by `make fleditor-wasm`.
 * This test intentionally runs in Node.js; it is not a Python/browser test.
 */
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const wasmDir = path.join(repo, 'docs/fleditor_site/wasm');
const require = createRequire(import.meta.url);
const FlatLModule = require(path.join(wasmDir, 'flatl.js'));

const loopSource = '#main\n  jump #main\n';
const panicSource = '#main\n  io.out(v)\n  OK\n';
const okSource = "#main\n  io.outln('recovered')\n  OK\n";

function readCString(M, ptr) {
  return M.UTF8ToString(ptr);
}

function allocError(M, size = 4096) {
  const ptr = M._malloc(size);
  assert.notEqual(ptr, 0, 'WASM malloc for error buffer');
  M.HEAPU8.fill(0, ptr, ptr + size);
  return ptr;
}

async function runSource(M, source, errPtr, errSize = 4096) {
  M.HEAPU8.fill(0, errPtr, errPtr + errSize);
  let rc = M.ccall(
    'flat_wasm_run_source', 'number', ['string', 'number', 'number'],
    [source, errPtr, errSize], { async: true }
  );
  if (rc && typeof rc.then === 'function') rc = await rc;
  return rc;
}

function setLimits(M, maxJumps, maxSteps, noJumpLimit) {
  M.ccall('flat_wasm_set_run_limits', null,
    ['number', 'number', 'number'], [maxJumps, maxSteps, noJumpLimit]);
}

let stdout = '';
let stderr = '';
const M = await FlatLModule({
  locateFile: (name) => path.join(wasmDir, name),
  flatWasmOnStdout: (bytes) => { stdout += Buffer.from(bytes).toString('utf8'); },
  flatWasmOnStderr: (bytes) => { stderr += Buffer.from(bytes).toString('utf8'); },
});

const errPtr = allocError(M);
try {
  // Force a modest allocation before compilation so this checks the Emscripten
  // memory-growth path without multiplying the compiler's own high-water mark.
  const heapBeforeGrowth = M.HEAPU8.byteLength;
  const growthAllocationSize = heapBeforeGrowth + (8 * 1024 * 1024);
  const growthPtr = M._malloc(growthAllocationSize);
  assert.notEqual(growthPtr, 0, 'growth allocation must succeed');
  const heapAfterGrowth = M.HEAPU8.byteLength;
  assert.ok(heapAfterGrowth > heapBeforeGrowth,
    `expected linear-memory growth (${heapBeforeGrowth} -> ${heapAfterGrowth})`);
  M.HEAPU8[growthPtr] = 0x5a;
  M.HEAPU8[growthPtr + growthAllocationSize - 1] = 0xa5;
  assert.equal(M.HEAPU8[growthPtr], 0x5a);
  assert.equal(M.HEAPU8[growthPtr + growthAllocationSize - 1], 0xa5);
  M._free(growthPtr);

  // A configured jump budget must stop a tight jump loop with a VM PANIC.
  setLimits(M, 3, 0, 0);
  let rc = await runSource(M, loopSource, errPtr);
  assert.equal(rc, 1, 'jump limit must fail the run');
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 4,
    'jump limit must terminate as FLAT_TERM_PANIC');
  assert.match(readCString(M, errPtr), /vm\.jump_limit/);
  assert.match(readCString(M, M.ccall('flat_wasm_get_last_panic', 'number', [], [])),
    /vm\.jump_limit/);
  const heapAfterCompileWarmup = M.HEAPU8.byteLength;

  // max_steps is independent from max_jumps; disable jump budget so step limit wins.
  setLimits(M, 0, 8, 1);
  rc = await runSource(M, loopSource, errPtr);
  assert.equal(rc, 1, 'step limit must fail the run');
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 4,
    'step limit must terminate as FLAT_TERM_PANIC');
  assert.match(readCString(M, errPtr), /vm\.step_limit/);
  assert.match(readCString(M, M.ccall('flat_wasm_get_last_panic', 'number', [], [])),
    /vm\.step_limit/);

  // Restore defaults and prove a successful run clears the previous panic state.
  setLimits(M, 0, 0, 0);
  stdout = '';
  rc = await runSource(M, okSource, errPtr);
  assert.equal(rc, 0, `run after limit panic failed: ${readCString(M, errPtr)}`);
  assert.match(stdout, /recovered/);
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 1);
  assert.equal(readCString(M, M.ccall('flat_wasm_get_last_panic', 'number', [], [])), '');

  // A native panic is reported through both the run error buffer and last-panic API.
  rc = await runSource(M, panicSource, errPtr);
  assert.equal(rc, 1);
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 4);
  assert.match(readCString(M, errPtr), /io\.out\.args/);
  assert.match(readCString(M, M.ccall('flat_wasm_get_last_panic', 'number', [], [])),
    /io\.out\.args/);

  // The following independent run must recover and clear the previous native panic.
  stdout = '';
  rc = await runSource(M, okSource, errPtr);
  assert.equal(rc, 0, `run after native panic failed: ${readCString(M, errPtr)}`);
  assert.match(stdout, /recovered/);
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 1);
  assert.equal(readCString(M, M.ccall('flat_wasm_get_last_panic', 'number', [], [])), '');

  // Repeated API compile/run/free cycles after warm-up exercise allocator reuse
  // and verify that memory does not keep growing with each independent run.
  // HEAPU8 size is a high-water mark, not a direct live-allocation/leak metric.
  stdout = '';
  for (let i = 0; i < 100; i += 1) {
    rc = await runSource(M,
      `#main\n  io.outln('post-growth ${i}')\n  OK\n`, errPtr);
    assert.equal(rc, 0, `post-growth run ${i} failed: ${readCString(M, errPtr)}`);
    assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 1);
  }
  assert.match(stdout, /post-growth 99/);
  const heapAfterStress = M.HEAPU8.byteLength;
  assert.ok(heapAfterStress <= heapAfterCompileWarmup + (64 * 1024 * 1024),
    `linear memory kept growing after warm-up: ${heapAfterCompileWarmup} -> ${heapAfterStress}`);

  console.log(
    `OK: WASM limits/recovery/memory (jump limit, step limit, panic recovery, ` +
    `linear memory ${heapBeforeGrowth} -> ${heapAfterGrowth} bytes, ` +
    `compile high-water ${heapAfterCompileWarmup}, after 100 runs ${heapAfterStress})`
  );
} catch (error) {
  console.error('wasm_api_limits_memory_smoke: FAILED');
  console.error(error?.stack || error);
  if (stderr) console.error('WASM stderr:', stderr);
  process.exitCode = 1;
} finally {
  setLimits(M, 0, 0, 0);
  M._free(errPtr);
}
