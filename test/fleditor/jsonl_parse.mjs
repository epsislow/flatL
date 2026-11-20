#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-jsonl-parse.js'));

const J = globalThis.FleditorJsonlParse;

function assert(cond, msg) {
  if (!cond) {
    console.error('jsonl_parse:', msg);
    process.exit(1);
  }
}

assert(J.isJsonlPath('a.jsonl') && J.isJsonlPath('b.ndjson') && J.isJsonlPath('c.json') && J.isJsonlPath('d.jsl'), 'exts');
assert(!J.isJsonlPath('a.jsonx') && !J.isJsonlPath('a.csv'), 'non jsonl ext');

assert(J.splitLines('').length === 0, 'empty file -> 0 lines');
assert(J.splitLines('{}').length === 1 && J.splitLines('{}')[0] === '{}', 'one line no nl');

const two = J.splitLines('{"a":1}\ntrue\n');
assert(two.length === 3 && two[0] === '{"a":1}' && two[1] === 'true' && two[2] === '', 'trailing nl empty line');

const crlf = J.splitLines('1\r\n2');
assert(crlf.length === 2 && J.detectLineEnding('1\r\n2') === '\r\n', 'crlf');

const emptyLine = J.parseLine('');
assert(emptyLine.empty && !emptyLine.ok, 'empty line');
assert(J.parseLine('   ').empty, 'whitespace empty');

const obj = J.parseLine('{"x":1}');
assert(obj.ok && obj.value.x === 1, 'object');
const arr = J.parseLine('[1,2]');
assert(arr.ok && Array.isArray(arr.value) && arr.value[1] === 2, 'array');
const sc = J.parseLine('true');
assert(sc.ok && sc.value === true, 'scalar bool');
assert(J.parseLine('"hi"').ok && J.parseLine('"hi"').value === 'hi', 'scalar string');
assert(J.parseLine('12').ok && J.parseLine('12').value === 12, 'scalar num');

const bad = J.parseLine('{"a":');
assert(!bad.ok && !bad.empty && typeof bad.error === 'string', 'invalid');

assert(J.serializeLine({ a: 1 }) === '{"a":1}', 'serialize');
assert(J.joinLines(['a', 'b'], '\n') === 'a\nb', 'join');
assert(J.previewLine('', 10) === '(empty)', 'preview empty');
assert(J.defaultPanelModeForLine('{}') === 'lock', 'default lock');
assert(J.defaultPanelModeForLine('') === 'free', 'default free');
assert(J.LINES_PER_PAGE === 40, '40 lines per page');
assert(J.pageCount(0) === 1 && J.pageCount(40) === 1 && J.pageCount(41) === 2, 'page count');
assert(J.pageCount(200) === 5, 'page count for 200 lines');
const range = J.pageRange(1, 95);
assert(range.start === 40 && range.end === 80, 'page range');

assert(J.isTooLarge(J.MAX_LINES_FOR_RECORDS + 1), 'too large');
assert(!J.isTooLarge(10), 'not too large');

const trunc = J.truncateUiString('x'.repeat(400), 300);
assert(trunc.truncated && trunc.length === 400, 'truncate ui');

const pretty = '{\n  "id": 2,\n  "name": "beta"\n}';
const stored = J.storeLineFromFreeText(pretty);
assert(stored === '{"id":2,"name":"beta"}', 'pretty free text stores compact');
assert(stored.indexOf('\n') < 0, 'stored has no newlines');
assert(J.storeLineFromFreeText('{"a":') === '{"a":', 'invalid stays one line');
assert(J.storeLineFromFreeText('{\n  "a":') === '{   "a":', 'invalid pretty collapses newlines');
assert(J.storeLineFromFreeText('   ') === '', 'whitespace stores empty');

console.log('OK: jsonl_parse');
