#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-text-format.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-parse.js'));

const T = globalThis.FleditorTextFormat;
const C = globalThis.FleditorCsvParse;

function assert(cond, msg) {
  if (!cond) {
    console.error('text_format:', msg);
    process.exit(1);
  }
}

assert(T.detectLineEnding('a\r\nb') === '\r\n', 'detect CRLF');
assert(T.detectLineEnding('a\rb') === '\r', 'detect CR');
assert(T.detectLineEnding('a\nb') === '\n', 'detect LF');

const crlf = T.convertLineEnding('x\ny', '\r\n');
assert(crlf === 'x\r\ny', 'convert to CRLF');

assert(T.textFromCmNewlines('a\nb', '\r\n') === 'a\r\nb', 'cm to CRLF');
assert(T.textToCmNewlines('a\r\nb') === 'a\nb', 'stored to cm');

assert(T.wrapProjectText('hi', 'utf-8') === 'hi', 'utf-8 text wrap plain');
const w = T.wrapProjectText('x', 'utf-16le');
assert(w.startsWith('FLEDITOR::TEXT::utf-16le::\n'), 'text wrapper');

const bytes = T.encodeTextBytes('ab', 'utf-8-bom');
const dec = T.decodeTextBytes(bytes);
assert(dec.ok && dec.encoding === 'utf-8-bom' && dec.text === 'ab', 'text bytes bom');

assert(C.detectLineEnding('a\r\nb') === '\r\n', 'csv parse uses shared detect');

console.log('OK: text_format');