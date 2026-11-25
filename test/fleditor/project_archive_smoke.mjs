import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const source = fs.readFileSync(path.join(here, '../../docs/fleditor_site/js/fleditor-project-archive.js'), 'utf8');
vm.runInThisContext(source, { filename: 'fleditor-project-archive.js' });
const api = globalThis.FleditorProjectArchive;
assert.ok(api, 'archive API should be exposed');

const encoder = new TextEncoder();
const original = [
  { path: 'main.fl', bytes: encoder.encode('#main\n  OK\n') },
  { path: 'data/binary.bin', bytes: new Uint8Array([0, 1, 2, 127, 128, 255]) },
  { path: 'unicode/ă.txt', bytes: encoder.encode('Salut, lume!') },
];
const zip = api.createZip(original);
assert.ok(zip instanceof Uint8Array);
const decoded = await api.readZip(zip.buffer);
assert.equal(decoded.length, original.length);
for (let i = 0; i < original.length; i++) {
  assert.equal(decoded[i].path, original[i].path);
  assert.deepEqual(Array.from(decoded[i].bytes), Array.from(original[i].bytes));
}

assert.equal(api.safePath('../escape.txt'), '');
assert.equal(api.safePath('/absolute.txt'), '');
assert.equal(api.safePath('folder/../escape.txt'), '');
assert.equal(api.safePath('folder/ok.txt'), 'folder/ok.txt');

const corrupt = new Uint8Array(zip);
corrupt[40] ^= 0xff;
await assert.rejects(() => api.readZip(corrupt.buffer), /integrity|Invalid|Truncated/i);

console.log('PASS: project ZIP create/read roundtrip, binary and UTF-8 entries, path traversal rejection, CRC validation');
