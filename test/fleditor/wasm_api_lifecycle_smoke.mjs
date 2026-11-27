#!/usr/bin/env node
/**
 * Headless WASM C API + lifecycle regression test.
 * Requires docs/fleditor_site/wasm/flatl.js and flatl.wasm (make fleditor-wasm).
 * Runs the actual Emscripten module in Node.js; no browser or Python runner.
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

const source = "#main\n  io.outln('api lifecycle ok')\n  OK\n";
const failSource = "#main\n  io.outln('logical fail')\n  FAIL\n  OK\n";

function readCString(M, ptr) {
  return M.UTF8ToString(ptr);
}

function allocError(M, size = 4096) {
  const ptr = M._malloc(size);
  assert.notEqual(ptr, 0, 'WASM malloc for error buffer');
  M.HEAPU8.fill(0, ptr, ptr + size);
  return ptr;
}

function compileSource(M, text) {
  const outPtr = M._malloc(4);
  const outLen = M._malloc(4);
  const errPtr = allocError(M);
  assert.ok(outPtr && outLen);
  M.setValue(outPtr, 0, 'i32');
  M.setValue(outLen, 0, 'i32');
  const rc = M.ccall(
    'flat_wasm_compile', 'number',
    ['string', 'number', 'number', 'number', 'number'],
    [text, outPtr, outLen, errPtr, 4096]
  );
  const fbPtr = M.getValue(outPtr, 'i32');
  const fbLen = M.getValue(outLen, 'i32') >>> 0;
  const err = readCString(M, errPtr);
  M._free(outPtr);
  M._free(outLen);
  M._free(errPtr);
  return { rc, fbPtr, fbLen, err };
}

function freeCompiled(M, compiled) {
  if (compiled.fbPtr) M.ccall('flat_wasm_free', null, ['number'], [compiled.fbPtr]);
  compiled.fbPtr = 0;
}

async function callRun(M, name, args) {
  let rc = M.ccall(name, 'number', args.types, args.values, { async: true });
  if (rc && typeof rc.then === 'function') rc = await rc;
  return rc;
}

let stdout = '';
let stderr = '';
const M = await FlatLModule({
  locateFile: (name) => path.join(wasmDir, name),
  flatWasmOnStdout: (bytes) => { stdout += Buffer.from(bytes).toString('utf8'); },
  flatWasmOnStderr: (bytes) => { stderr += Buffer.from(bytes).toString('utf8'); },
});

try {
  // Exported metadata is callable and returns stable, non-empty strings.
  assert.ok(readCString(M, M.ccall('flat_wasm_toolchain_version', 'number', [], [])));
  assert.ok(readCString(M, M.ccall('flat_wasm_mods', 'number', [], [])));

  // Host-configuration setters are part of the public C API. Exercise both
  // accepted path separators and restore the host default before file tests.
  assert.equal(M.ccall('flat_wasm_path_sep_set', 'number', ['string'], ['/']), 0);
  assert.equal(M.ccall('flat_wasm_path_sep_set', 'number', ['string'], ['\\']), 0);
  M.ccall('flat_wasm_path_sep_reset', null, [], []);
  M.ccall('flat_wasm_set_run_limits', null, ['number', 'number', 'number'], [0, 0, 0]);
  M.ccall('flat_wasm_set_run_limits', null, ['number', 'number', 'number'], [0, 0, 0]);

  // Compile to caller-owned bytecode, validate output, run it, and free it.
  const compiled = compileSource(M, source);
  assert.equal(compiled.rc, 0, `compile failed: ${compiled.err}`);
  assert.ok(compiled.fbPtr, 'compile must return allocated bytecode');
  assert.ok(compiled.fbLen > 0, 'compiled bytecode must not be empty');
  stdout = '';
  stderr = '';
  const runErr = allocError(M);
  const runRc = await callRun(M, 'flat_wasm_run', {
    types: ['number', 'number', 'number', 'number'],
    values: [compiled.fbPtr, compiled.fbLen, runErr, 4096],
  });
  assert.equal(runRc, 0, `run failed: ${readCString(M, runErr)}`);
  assert.match(stdout, /api lifecycle ok/);
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 1);
  assert.equal(readCString(M, M.ccall('flat_wasm_get_last_panic', 'number', [], [])), '');
  M._free(runErr);
  freeCompiled(M, compiled);

  // Invalid source must fail cleanly and leave output pointers empty.
  const badCompile = compileSource(M, '#main\n  this is not valid syntax !!!\n');
  assert.notEqual(badCompile.rc, 0, 'invalid source unexpectedly compiled');
  assert.equal(badCompile.fbPtr, 0, 'failed compile leaked an output pointer');
  assert.equal(badCompile.fbLen, 0, 'failed compile returned a bytecode length');
  assert.ok(badCompile.err.length > 0, 'failed compile should provide a diagnostic');

  // Empty and malformed bytecode must fail without crashing the module.
  const errPtr = allocError(M);
  let rc = await callRun(M, 'flat_wasm_run', {
    types: ['number', 'number', 'number', 'number'],
    values: [0, 0, errPtr, 4096],
  });
  assert.equal(rc, 1);
  assert.match(readCString(M, errPtr), /empty bytecode/);
  const malformed = M._malloc(8);
  assert.ok(malformed);
  M.HEAPU8.fill(0xA5, malformed, malformed + 8);
  M.HEAPU8.fill(0, errPtr, errPtr + 4096);
  rc = await callRun(M, 'flat_wasm_run', {
    types: ['number', 'number', 'number', 'number'],
    values: [malformed, 8, errPtr, 4096],
  });
  assert.equal(rc, 1);
  assert.match(readCString(M, errPtr), /invalid \.fb bytecode/);
  M._free(malformed);
  M._free(errPtr);

  // Logical FAIL is a program result, not an API panic.
  stdout = '';
  stderr = '';
  const failErr = allocError(M);
  rc = await callRun(M, 'flat_wasm_run_source', {
    types: ['string', 'number', 'number'],
    values: [failSource, failErr, 4096],
  });
  assert.equal(rc, 2);
  assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 3);
  assert.match(stdout, /logical fail/);
  assert.equal(readCString(M, failErr), '');
  M._free(failErr);

  // Compile+run repeatedly in one module instance to expose stale global state
  // and lifecycle leaks across independent executions.
  for (let i = 0; i < 40; i += 1) {
    stdout = '';
    const err = allocError(M, 512);
    rc = await callRun(M, 'flat_wasm_run_source', {
      types: ['string', 'number', 'number'],
      values: [`#main\n  io.outln('iteration ${i}')\n  OK\n`, err, 512],
    });
    assert.equal(rc, 0, `repeated run ${i} failed: ${readCString(M, err)}`);
    assert.match(stdout, new RegExp(`iteration ${i}(?:\\r?\\n|$)`));
    assert.equal(M.ccall('flat_wasm_get_last_termination', 'number', [], []), 1);
    M._free(err);
  }

  // Entry-point compile and disassembly exercise the MEMFS/file-path API.
  const entryPath = '/api_lifecycle_entry.fl';
  M.FS.writeFile(entryPath, source);
  const entryOut = M._malloc(4);
  const entryLen = M._malloc(4);
  const entryErr = allocError(M);
  M.setValue(entryOut, 0, 'i32');
  M.setValue(entryLen, 0, 'i32');
  rc = M.ccall(
    'flat_wasm_compile_entry', 'number',
    ['string', 'number', 'number', 'number', 'number'],
    [entryPath, entryOut, entryLen, entryErr, 4096]
  );
  const entryFb = M.getValue(entryOut, 'i32');
  const entryFbLen = M.getValue(entryLen, 'i32') >>> 0;
  assert.equal(rc, 0, `compile_entry failed: ${readCString(M, entryErr)}`);
  assert.ok(entryFb && entryFbLen > 0);
  M.ccall('flat_wasm_free', null, ['number'], [entryFb]);

  const textOut = M._malloc(4);
  const textLen = M._malloc(4);
  M.setValue(textOut, 0, 'i32');
  M.setValue(textLen, 0, 'i32');
  rc = M.ccall(
    'flat_wasm_disassemble_entry', 'number',
    ['string', 'number', 'number', 'number', 'number'],
    [entryPath, textOut, textLen, entryErr, 4096]
  );
  const disasmPtr = M.getValue(textOut, 'i32');
  const disasmLen = M.getValue(textLen, 'i32') >>> 0;
  assert.equal(rc, 0, `disassemble_entry failed: ${readCString(M, entryErr)}`);
  assert.ok(disasmPtr && disasmLen > 0);
  const disasm = readCString(M, disasmPtr);
  assert.ok(disasm.length > 0);
  M.ccall('flat_wasm_free', null, ['number'], [disasmPtr]);
  M._free(textOut);
  M._free(textLen);
  M._free(entryOut);
  M._free(entryLen);
  M._free(entryErr);

  // run_entry exercises the compile+run convenience API over MEMFS.
  stdout = '';
  const entryRunErr = allocError(M);
  rc = await callRun(M, 'flat_wasm_run_entry', {
    types: ['string', 'number', 'number'],
    values: [entryPath, entryRunErr, 4096],
  });
  assert.equal(rc, 0, `run_entry failed: ${readCString(M, entryRunErr)}`);
  assert.match(stdout, /api lifecycle ok/);
  M._free(entryRunErr);

  console.log('OK: WASM API/lifecycle (compile, run, fail, invalid input, entry, disassembly, 40 repeated runs)');
} catch (error) {
  console.error('wasm_api_lifecycle_smoke: FAILED');
  console.error(error?.stack || error);
  if (stderr) console.error('WASM stderr:', stderr);
  process.exitCode = 1;
}
