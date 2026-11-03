#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-hex.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-hex-grid.js'));

const H = globalThis.FleditorHex;
const G = globalThis.FleditorHexGrid;

function assert(cond, msg) {
  if (!cond) {
    console.error('hex_grid:', msg);
    process.exit(1);
  }
}

const p = G.parseHexCellInput('a', 0x00);
assert(p.ok && p.byte === 0x0a, 'single hex digit padded');

const bad = G.parseHexCellInput('gg', 0x11);
assert(!bad.ok, 'reject non-hex');

const emptyZero = G.parseHexCellInput('', 0x42);
assert(emptyZero.ok && emptyZero.byte === 0x00, 'empty cell becomes 00');

assert(G.isCellDirty(0, new Uint8Array([1]), new Uint8Array([0])), 'dirty when differs');
assert(!G.isCellDirty(0, new Uint8Array([1]), new Uint8Array([1])), 'clean when same');

assert(G.rowCountForPageSlice(0) === 0, 'zero bytes no rows');
assert(G.rowCountForPageSlice(1) === 1, 'one byte one row');
assert(G.rowCountForPageSlice(16) === 1, 'full line');
assert(G.rowCountForPageSlice(17) === 2, 'two lines');

const ascii = G.buildAsciiForRow(new Uint8Array([0x48, 0x00, 0x7f]), 0, 3);
assert(ascii === 'H..', 'ascii column');

assert(G.formatByteHex(255) === 'ff', 'format byte');

const buf = new Uint8Array(640);
buf[0] = 0xde;
buf[639] = 0xad;
assert(H.bytesOnPage(640, 0) === 640, 'full page');
assert(G.rowCountForPageSlice(640) === H.LINES_PER_PAGE, '640 rows');

assert(G.appendSlotPageIndex(0) === 0, 'append page empty');
assert(G.appendSlotPageIndex(639) === 0, 'append page 639');
assert(G.appendSlotPageIndex(640) === 1, 'append page 640');
assert(G.pageShowsAppendSlot(640, 1), 'append on page 1 at 640 bytes');
assert(G.gridRowCountForPage(0, 0) === 1, 'empty file one row for append');

assert(G.arrowTargetOffset(15, 'ArrowRight', 20) === 16, 'right wraps row');
assert(G.arrowTargetOffset(16, 'ArrowLeft', 20) === 15, 'left wraps row');
assert(G.arrowTargetOffset(5, 'ArrowDown', 25) === 21, 'down same column');
assert(G.arrowTargetOffset(5, 'ArrowDown', 20) === 19, 'down clamps partial last row');
assert(G.arrowTargetOffset(5, 'ArrowDown', 10) === null, 'down past end');
assert(G.arrowTargetFromAppend('ArrowLeft', 16) === 15, 'append left');

console.log('OK: hex_grid');
