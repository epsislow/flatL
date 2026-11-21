#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-jsonl-adv-filter.js'));

const A = globalThis.FleditorJsonlAdvFilter;

function assert(cond, msg) {
  if (!cond) {
    console.error('jsonl_adv_filter:', msg);
    process.exit(1);
  }
}

function assertMatch(query, value, expect, label) {
  const r = A.match(query, value);
  assert(r.ok, (label || query) + ' parse: ' + (r.error || ''));
  assert(r.match === expect, (label || query) + ' → ' + r.match + ' (want ' + expect + ')');
}

function assertBad(query, label) {
  const r = A.parse(query);
  assert(!r.ok, (label || query) + ' should fail parse');
}

function indicesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

// --- 1. Path existence ---
assertMatch('$.items', { items: [] }, true, 'exists items');
assertMatch('$.items', { other: 1 }, false, 'missing items');
assertMatch('$.items[2].location', { items: [{}, {}, { location: 'x' }] }, true);
assertMatch('$.items[2].location', { items: [{}, {}] }, false);
assertMatch('$.items[*].location', { items: [{}, { location: 1 }] }, true);
assertMatch('$.items[*].location', { items: [{ a: 1 }] }, false);
assertMatch('$."weird key"', { 'weird key': 1 }, true, 'quoted key');

// --- 2. Root scalar ---
assertMatch('$ is true', true, true);
assertMatch('$ is true', false, false);
assertMatch('$ = 1', 1, true);
assertMatch('$ = "a"', 'a', true);
assertMatch('$ = "a"', 'b', false);

// --- 3. Comparisons ---
assertMatch('$.id < 3', { id: 2 }, true);
assertMatch('$.id < 3', { id: 3 }, false);
assertMatch('$.id <= 3', { id: 3 }, true);
assertMatch('$.id <= 3', { id: 4 }, false);
assertMatch('$.id >= 5', { id: 5 }, true);
assertMatch('$.id >= 5', { id: 4 }, false);
assertMatch('$.name len >= 3', { name: 'Bob' }, true);
assertMatch('$.items[] len <= 1', { items: [1] }, true);
assertMatch('$.id != 2', { id: 5 }, true);
assertMatch('$.id != 2', { id: 2 }, false);
assertMatch('$.type = "string"', { type: 'string' }, true);
assertMatch('$.admin is false', { admin: false }, true);
assertMatch('$.a is null', { a: null }, true);
assertMatch('$.a is null', { a: 1 }, false);
assertMatch('$.a is null', {}, false, 'missing is null → false');
assertMatch('$.a is not null', { a: 1 }, true);
assertMatch('$.a is not null', { a: null }, false);
assertMatch('$.a is not null', {}, false, 'missing is not null → false');

// --- 4. List len / index ---
assertMatch('$[] len = 2', [1, 2], true);
assertMatch('$[] len = 2', [1], false);
assertMatch('$.items[] len > 0', { items: [1] }, true);
assertMatch('$.items[] len > 0', { items: [] }, false);
assertMatch('$[2] = "2"', [0, 1, '2'], true);
assertMatch('$[2] = 2', [0, 1, 2], true);
assertMatch('$[*] is false', [true, false], true);
assertMatch('$[*] is false', [true, true], false);

// --- 5. String len vs list ---
assertMatch('$.name len > 5', { name: 'abcdef' }, true);
assertMatch('$.name len > 5', { name: 'ab' }, false);
assertMatch('$.items len > 0', { items: [1, 2] }, false, 'array with string-len → false');
assertMatch('$.items len > 0', { items: 'hi' }, true, 'string items with string-len');

// --- 6. IN / has ---
assertMatch('$.location IN ["A1","A2","A3"]', { location: 'A2' }, true);
assertMatch('$.location not IN ["A1","A2"]', { location: 'A3' }, true);
assertMatch('$.location not IN ["A1","A2"]', { location: 'A1' }, false);
assertMatch('$.locations has "A2"', { locations: ['A1', 'A2'] }, true);
assertMatch('$.locations[*] = "A2"', { locations: ['A1', 'A2'] }, true, 'has ≡ [*] =');
assertMatch('$.locations not has "A2"', { locations: ['A1'] }, true);
assertMatch('$.locations has ["A1","Z"]', { locations: ['A1', 'A2'] }, true, 'has OR');
assertMatch('$.locations has all ["A1","A2"]', { locations: ['A1', 'A2', 'A3'] }, true);
assertMatch('$.locations has all ["A1","Z"]', { locations: ['A1', 'A2'] }, false);
assertMatch('$.locations not has all ["A1","Z"]', { locations: ['A1', 'A2'] }, true);

// --- 7. String ops ---
assertMatch('$.mesaj contains "test"', { mesaj: 'xx test yy' }, true);
assertMatch('$.mesaj not contains "test"', { mesaj: 'xx' }, true);
assertMatch('$.mesaj starts with "test"', { mesaj: 'test123' }, true);
assertMatch('$.mesaj ends with "test"', { mesaj: 'abctest' }, true);
assertMatch('$.mesaj contains "Test"', { mesaj: 'xx test yy' }, false, 'case-sensitive');
assertMatch('$.mesaj contains "x"', { mesaj: 12 }, false, 'non-string → false');

// --- 8. type ---
assertMatch('$.val type object', { val: { a: 1 } }, true);
assertMatch('$.val type number', { val: 3 }, true);
assertMatch('$.val type bool', { val: true }, true);
assertMatch('$.val type list', { val: [1] }, true);
assertMatch('$.val type array', { val: [1] }, true, 'array ≡ list');
assertMatch('$.val type string', { val: 's' }, true);
assertMatch('$.val type null', { val: null }, true);
assertMatch('$.val not type number', { val: 's' }, true);
assertMatch('not ( $.val type number )', { val: 's' }, true);

// --- 9. and / or / not / parens ---
assertMatch('$.name = "Bob" and $.admin is true', { name: 'Bob', admin: true }, true);
assertMatch('$.name = "Bob" and $.admin is true', { name: 'Bob', admin: false }, false);
assertMatch('$.name = "Bob" or ($.admin is false)', { name: 'Ann', admin: false }, true);
assertMatch('$ = 2 and not $ = 3', 2, true);
assertMatch('$ = 2 and not $ = 3', 3, false);
assertMatch('$.type = "admin" and not ( $.name = "Bob" )', { type: 'admin', name: 'Ann' }, true);
assertMatch('$.type = "admin" and not ( $.name = "Bob" )', { type: 'admin', name: 'Bob' }, false);

// --- 10. Parse errors ---
assertBad('$$$', 'bad path');
assertBad('$.', 'trailing dot');
assertBad('name = 1', 'missing $');
assertBad('$.x type widget', 'bad type');
assertBad('($.a = 1', 'unbalanced');
assertBad('', 'empty');

// --- 11. Fixtures (mental example from plan) ---
const lines = [
  { name: 'Bob', admin: true, id: 2 },
  { name: 'Ann', admin: false, id: 5 },
  true,
  [1, 2, 3],
];
function expectIndices(query, want) {
  const r = A.filterValues(query, lines);
  assert(r.ok, query + ' filter parse: ' + (r.error || ''));
  assert(indicesEqual(r.indices, want), query + ' indices ' + JSON.stringify(r.indices) + ' want ' + JSON.stringify(want));
}
expectIndices('$.name = "Bob" and $.admin is true', [0]);
expectIndices('$.id > 4', [1]);
expectIndices('$ is true', [2]);
expectIndices('$[] len = 3', [3]);

console.log('OK: jsonl_adv_filter');
