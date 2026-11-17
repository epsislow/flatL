#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-grid.js'));

const G = globalThis.FleditorCsvGrid;

function assert(cond, msg) {
  if (!cond) {
    console.error('csv_selection_stats:', msg);
    process.exit(1);
  }
}

const rows = [
  ['10', '20', 'x'],
  ['5', '1,5', ''],
];
const anchor = { r: 0, c: 0 };
const focus = { r: 1, c: 1 };
assert(G.formatSelectionSummary(rows, anchor, focus) === '4(c) 36.5(sum)', '2x2 block sum');

const big = [];
for (let i = 0; i < 100; i++) big.push(['1000']);
assert(G.formatSelectionSummary(big, { r: 0, c: 0 }, { r: 99, c: 0 }) === '100(c) MAX(sum)', 'sum cap');

const manyCols = [new Array(10000).fill('1')];
assert(
  G.formatSelectionSummary(manyCols, { r: 0, c: 0 }, { r: 0, c: 9999 }) === 'MAX(c) MAX(sum)',
  'cell cap'
);

assert(G.formatSelectionSummary([['-5000']], { r: 0, c: 0 }, { r: 0, c: 0 }) === '1(c) -5000(sum)', 'negative');

console.log('OK: csv_selection_stats');