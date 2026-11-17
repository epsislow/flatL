#!/usr/bin/env node
/**
 * csv writer byte layout: UTF-8-BOM prefix and UTF-16 LE BOM + payload.
 */
import fs from 'fs';
import path from 'path';
import os from 'os';
import { execSync } from 'child_process';
import { fileURLToPath } from 'url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const flc = path.join(repo, 'bin/flc');
const flvm = path.join(repo, 'bin/flvm');

const SRC = `use native csv
use native list
#main
  fp = file.createCtx('out.csv')
  wid = csv.writer(fp)
  csv.setEncoding(wid, 'ENC_PLACEHOLDER')
  csv.setLineEndings(wid, 'LF')
  list.pack('A', 'B') -> row
  list.pack(row) -> rows
  csv.writeRows(wid, rows)
  csv.close(wid)
  file.close(fp)
  OK
`;

function assert(cond, msg) {
  if (!cond) {
    console.error('csv_writer_encoding_bytes:', msg);
    process.exit(1);
  }
}

function bytesEqual(a, b) {
  if (a.length !== b.length) {
    return false;
  }
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) {
      return false;
    }
  }
  return true;
}

function runWriter(encLabel) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'flatl-csv-wbytes-'));
  const fl = path.join(tmp, 'w.fl');
  const fb = path.join(tmp, 'out.fb');
  const csvPath = path.join(tmp, 'out.csv');
  fs.writeFileSync(fl, SRC.replace('ENC_PLACEHOLDER', encLabel), 'utf8');
  execSync(`"${flc}" -o "${fb}" "${fl}"`, { stdio: 'pipe' });
  execSync(`cd "${tmp}" && "${flvm}" out.fb`, { stdio: 'pipe' });
  return new Uint8Array(fs.readFileSync(csvPath));
}

if (!fs.existsSync(flc) || !fs.existsSync(flvm)) {
  console.error('csv_writer_encoding_bytes: build bin/flc and bin/flvm first');
  process.exit(1);
}

const bomUtf8 = runWriter('UTF-8-BOM');
assert(bomUtf8.length >= 7, 'UTF-8-BOM output too short');
assert(bomUtf8[0] === 0xef && bomUtf8[1] === 0xbb && bomUtf8[2] === 0xbf, 'UTF-8-BOM missing EF BB BF');
const bomBody = Buffer.from(bomUtf8.subarray(3)).toString('utf8');
assert(bomBody === 'A,B\n', 'UTF-8-BOM body ' + JSON.stringify(bomBody));

const le = runWriter('UTF-16-LE');
assert(le.length >= 4, 'UTF-16-LE output too short');
assert(le[0] === 0xff && le[1] === 0xfe, 'UTF-16-LE missing BOM');
const payload = le.subarray(2);
const expected = Buffer.from('A,B\n', 'utf16le');
assert(bytesEqual(payload, expected), 'UTF-16-LE payload mismatch');

console.log('OK: csv_writer_encoding_bytes (UTF-8-BOM + UTF-16-LE bytes)');