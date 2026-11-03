#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-hex.js'));

const H = globalThis.FleditorHex;

function assert(cond, msg) {
  if (!cond) {
    console.error('hex_roundtrip:', msg);
    process.exit(1);
  }
}

const sample = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x0a]);
const dump = H.hexDumpAllFromBytes(sample);
assert(dump.includes('48 65 6c 6c 6f'), 'dump contains hello hex');
const page0 = H.hexDumpPageFromBytes(sample, 0);
const back = H.hexDumpPageToBytes(page0, sample.length);
assert(back.ok && H.bytesEqual(back.bytes, sample), 'page round-trip');

const seventeen = new Uint8Array(17);
for (let i = 0; i < 17; i++) {
  seventeen[i] = i;
}
assert(H.pageCountForLength(17) === 1, '17 bytes one page');
assert(H.pageCountForLength(640) === 1, '640 one page');
assert(H.pageCountForLength(641) === 2, '641 two pages');

const p641 = new Uint8Array(641);
p641[640] = 0xff;
const d0 = H.hexDumpPageFromBytes(p641, 0);
const d1 = H.hexDumpPageFromBytes(p641, 1);
const b0 = H.hexDumpPageToBytes(d0, 640);
const b1 = H.hexDumpPageToBytes(d1, 1);
assert(b0.ok && b1.ok, '641 parse pages');
const merged = new Uint8Array(641);
merged.set(b0.bytes, 0);
merged.set(b1.bytes, 640);
assert(H.bytesEqual(merged, p641), '641 merged');

const bad = H.hexDumpPageToBytes('00000000  gg', 1);
assert(!bad.ok, 'invalid hex rejected');

const zeroText = H.hexDumpPageToBytes('asdada', 0);
assert(!zeroText.ok, 'non-hex text rejected on empty file');

const emptyBuf = H.newEmptyBinaryBuffer();
assert(emptyBuf.length === H.DEFAULT_NEW_BINARY_SIZE, 'default new binary size');

const ins = H.insertBytes(sample, 5, new Uint8Array([0x21]));
assert(ins.length === 7 && ins[5] === 0x21, 'insert');
const del = H.deleteBytes(ins, 0, 1);
assert(del.length === 6 && del[0] === 0x65, 'delete');
const sized = H.setFileSize(sample, 10);
assert(sized.length === 10 && sized[5] === 0x0a, 'set size pad');

const pat = H.parseSearchPattern('48 65');
assert(pat.ok && pat.bytes.length === 2, 'search hex');
const at = H.findMatch(sample, pat.bytes, 0, 'next');
assert(at === 0, 'find match');

const all = H.findAllMatches(sample, pat.bytes);
assert(all.length === 1 && all[0] === 0, 'find all matches');

const dup = new Uint8Array([0x48, 0x65, 0x48, 0x65]);
const all2 = H.findAllMatches(dup, pat.bytes);
assert(all2.length === 2, 'two matches');
assert(H.findMatchWrap(dup, pat.bytes, 1, 'next') === 2, 'next after first byte');
assert(H.findMatchWrap(dup, pat.bytes, 3, 'next') === 0, 'next wraps to start');
assert(H.findMatchWrap(dup, pat.bytes, 2, 'prev') === 0, 'prev from second match');
assert(H.findMatchWrap(dup, pat.bytes, 0, 'prev') === 2, 'prev wraps to end');

const b64 = H.parseSearchPattern('SGVs');
assert(b64.ok, 'search base64 hello prefix');

console.log('OK: hex_roundtrip');
