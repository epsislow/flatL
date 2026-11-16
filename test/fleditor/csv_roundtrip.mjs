#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import fs from 'fs';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const fixturesDir = path.join(repo, 'test/fleditor/csv/fixtures');
const examplesDir = path.join(repo, 'docs/fleditor_site/examples');
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

const excelExports = [
  'test1_csv_commadel.csv',
  'test1_csv_mac.csv',
  'test1_csv_msdos.csv',
  'test1_csv_utf8_commadel.csv',
];
excelExports.forEach((name) => {
  const filePath = path.join(examplesDir, name);
  if (!fs.existsSync(filePath)) {
    console.error('csv_roundtrip: missing', name);
    process.exit(1);
  }
  const u8 = new Uint8Array(fs.readFileSync(filePath));
  const stored = P.bytesToProjectValue(u8, name);
  const back = payloadToU8(P.projectValueToMemfsPayload(stored));
  assert(bytesEqual(u8, back), 'excel export round-trip ' + name);
});

const macBody = C.projectTextFromContent(
  P.bytesToProjectValue(new Uint8Array(fs.readFileSync(path.join(examplesDir, 'test1_csv_mac.csv'))), 'mac.csv')
);
assert(globalThis.FleditorTextFormat.detectLineEnding(macBody) === '\r', 'excel mac CR line endings');
const macParsed = C.parseFile(macBody, { autoDetect: true, quoteChar: '"' });
assert(macParsed.delimiter === ',' && macParsed.rows[0][0] === 'Product', 'excel mac parse');
assert(macParsed.rows.length === 8, 'excel mac row count');

const msdosPath = path.join(examplesDir, 'test1_csv_msdos.csv');
const msdosBody = C.projectTextFromContent(
  P.bytesToProjectValue(new Uint8Array(fs.readFileSync(msdosPath)), 'msdos.csv')
);
assert(globalThis.FleditorTextFormat.detectLineEnding(msdosBody) === '\r\n', 'excel msdos CRLF');
const msdosParsed = C.parseFile(msdosBody, { autoDetect: true, quoteChar: '"' });
assert(msdosParsed.rows.length === 8 && msdosParsed.delimiter === ',', 'excel msdos parse');

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