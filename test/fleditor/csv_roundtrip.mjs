#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import fs from 'fs';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const fixturesDir = path.join(repo, 'test/fleditor/csv/fixtures');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-text-format.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-csv-parse.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-project.js'));

const P = globalThis.FleditorProject;
const C = globalThis.FleditorCsvParse;

function assert(cond, msg) {
  if (!cond) {
    console.error('csv_roundtrip:', msg);
    process.exit(1);
  }
}

function bytesEqual(a, b) {
  const u8a = a instanceof Uint8Array ? a : new Uint8Array(a);
  const u8b = b instanceof Uint8Array ? b : new Uint8Array(b);
  if (u8a.length !== u8b.length) return false;
  for (let i = 0; i < u8a.length; i++) {
    if (u8a[i] !== u8b[i]) return false;
  }
  return true;
}

function payloadToU8(payload) {
  if (payload instanceof Uint8Array) return payload;
  return new TextEncoder().encode(String(payload));
}

function roundTripFixture(name, relPath) {
  const filePath = path.join(fixturesDir, name);
  const u8 = new Uint8Array(fs.readFileSync(filePath));
  const stored = P.bytesToProjectValue(u8, relPath);
  const back = payloadToU8(P.projectValueToMemfsPayload(stored));
  assert(bytesEqual(u8, back), name + ' byte round-trip');
}

roundTripFixture('excel_semicolon_crlf.csv', 'excel_semicolon_crlf.csv');
roundTripFixture('quoted_semicolon.csv', 'quoted_semicolon.csv');
roundTripFixture('utf8_bom.csv', 'utf8_bom.csv');
roundTripFixture('utf16le_semicolon.csv', 'utf16le_semicolon.csv');

const raw = new Uint8Array(fs.readFileSync(path.join(fixturesDir, 'excel_semicolon_crlf.csv')));
const stored = P.bytesToProjectValue(raw, 'data.csv');
const body = C.projectTextFromContent(stored);
const parsed = C.parseFile(body, { preferTab: false, autoDetect: true, quoteChar: '"' });
assert(parsed.delimiter === ';' && parsed.rows[0][0] === 'Product', 'parse excel fixture');
const le = globalThis.FleditorTextFormat.detectLineEnding(body);
const ser = C.serializeCsv(parsed.rows, parsed.delimiter, le, '"');
const lines = ser.split(/\r\n|\n|\r/).filter((l) => l.length > 0);
assert(lines.length === 3 && lines[1] === 'TV;19', 'semantic serialize excel');

const bomStored = P.bytesToProjectValue(
  new Uint8Array(fs.readFileSync(path.join(fixturesDir, 'utf8_bom.csv'))),
  'bom.csv'
);
assert(bomStored.startsWith('FLEDITOR::CSV::utf-8-bom::\n'), 'utf8-bom project wrapper');

console.log('OK: csv_roundtrip');