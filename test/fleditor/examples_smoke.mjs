#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const examplesDir = path.join(repo, 'fleditor_site/examples');
const manifest = JSON.parse(fs.readFileSync(path.join(examplesDir, 'manifest.json'), 'utf8'));

if (!Array.isArray(manifest.files) || manifest.files.length < 3) {
  console.error('examples_smoke: manifest too small');
  process.exit(1);
}

for (const rel of manifest.files) {
  const fp = path.join(examplesDir, rel);
  if (!fs.existsSync(fp)) {
    console.error('examples_smoke: missing', rel);
    process.exit(1);
  }
}

console.log('OK: fleditor examples smoke (' + manifest.files.length + ' files)');
