#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'docs/fleditor_site/js/fleditor-hex.js'));
require(path.join(repo, 'docs/fleditor_site/js/fleditor-project.js'));

const P = globalThis.FleditorProject;
const H = globalThis.FleditorHex;

const raw = new Uint8Array([0, 255, 128]);
const stored = P.bytesToProjectValueBinary(raw);
if (!P.isBinaryProjectValue(stored)) {
  console.error('hex_project_smoke: wrapper');
  process.exit(1);
}
const back = P.binaryProjectValueToBytes(stored);
if (!H.bytesEqual(back, raw)) {
  console.error('hex_project_smoke: round-trip bytes');
  process.exit(1);
}
if (P.isBinaryProjectFile('x.bin', '')) {
  console.error('hex_project_smoke: plain path is not binary without wrapper');
  process.exit(1);
}
if (!P.isBinaryProjectFile('x.bin', stored)) {
  console.error('hex_project_smoke: wrapper marks binary');
  process.exit(1);
}
const empty = P.bytesToProjectValueBinary(new Uint8Array(0));
if (!P.isBinaryProjectValue(empty)) {
  console.error('hex_project_smoke: empty binary wrapper');
  process.exit(1);
}

console.log('OK: hex_project_smoke');
