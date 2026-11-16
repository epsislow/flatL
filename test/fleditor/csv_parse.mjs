#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-parse.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-grid.js'));

const C = globalThis.FleditorCsvParse;
const G = globalThis.FleditorCsvGrid;

function assert(cond, msg) {
  if (!cond) {
    console.error('csv_parse:', msg);
    process.exit(1);
  }
}

const empty = C.parseFile('');
assert(empty.rows.length === 1 && empty.rows[0][0] === '' && empty.empty, 'empty file -> 1x1');

assert(C.serializeCsv([['']], ',', '\n') === '', 'empty 1x1 serializes to empty string');

const semi = C.parseFile('a;b;c\n1;2;3', {});
assert(semi.delimiter === ';' && semi.rows[0][0] === 'a' && semi.rows[1][2] === '3', 'semicolon detect');

const quoted = C.parseFile('"a,b",c\n', { delimiter: ',' });
assert(quoted.rows[0][0] === 'a,b' && quoted.rows[0][1] === 'c', 'quoted comma in field');

const rt = 'x,y\r\np,q';
const p = C.parseFile(rt, {});
const back = C.serializeCsv(p.rows, p.delimiter, p.lineEnding);
assert(back === 'x,y\r\np,q', 'round-trip CRLF');

const tsv = C.parseFile('h\tv', { preferTab: true });
assert(tsv.delimiter === '\t' && tsv.rows[0][1] === 'v', 'tsv tab');

const big = C.resizeGrid([['a']], 3, 2);
assert(big.length === 3 && big[0].length === 2 && big[2][1] === '', 'resize grid');

assert(C.isTooLarge(Array(2001).fill(['x'])), '2001 rows too large');
assert(!C.isTooLarge([['']]), '1x1 ok');
assert(C.tooLargeMessage([[''], ...Array(5).fill([''])]) === 'Too large (6 rows, 1 columns). Open as text.', 'too large message');

const hits = G.findCellMatches([['a', 'bb'], ['cc', 'a']], 'a');
assert(hits.length === 2, 'find cells with a');

const pos = G.parseCellPos('2,1', [['x', 'y'], ['p', 'q']]);
assert(pos.ok && pos.r === 1 && pos.c === 0, 'parse cell pos');

console.log('OK: csv_parse');