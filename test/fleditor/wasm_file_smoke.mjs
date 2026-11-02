#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const wasmJs = path.join(repo, 'docs/fleditor_site/wasm/flatl.js');
const require = createRequire(import.meta.url);
const FlatLModule = require(wasmJs);

const MEMFS_ROOT = '/fleditor';

async function runEntry(files, entryRel) {
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
    M.FS.writeFile(MEMFS_ROOT + '/' + rel, content);
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

const relPath = `#main
  fp = file.createCtx('out.txt')
  file.write(fp, 'wasm-file')
  file.close(fp)
  fp = file.readCtx('out.txt')
  file.read(fp) -> body
  file.close(fp)
  io.outln(body)
  OK
`;

const absPath = `#main
  fp = file.createCtx('/fleditor/out2.txt')
  file.write(fp, 'wasm-abs')
  file.close(fp)
  fp = file.readCtx('/fleditor/out2.txt')
  file.read(fp) -> body
  file.close(fp)
  io.outln(body)
  OK
`;

const r1 = await runEntry({ 'main.fl': relPath }, 'main.fl');
const r2 = await runEntry({ 'main.fl': absPath }, 'main.fl');
const synced = `#main
  fp = file.readCtx('/fleditor/data.txt')
  file.read(fp) -> body
  file.close(fp)
  io.outln(body)
  OK
`;
const r3 = await runEntry({ 'main.fl': synced, 'data.txt': 'from-project' }, 'main.fl');
console.log('rel', r1.rc, r1.out.trim(), r1.err);
console.log('abs', r2.rc, r2.out.trim(), r2.err);
console.log('synced-rel', r3.rc, r3.out.trim(), r3.err);
