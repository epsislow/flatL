#!/usr/bin/env node
/**
 * Unit smoke for fleditor layout persistence helpers (no browser).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const layoutJs = path.join(repo, 'docs/fleditor_site/js/fleditor-layout.js');

const require = createRequire(import.meta.url);
require(layoutJs);

const { mergeLayout, resetLayout, saveLayout, loadLayout, DEFAULT_LAYOUT } = globalThis.FleditorLayout;

class MemStorage {
  constructor() {
    this.map = new Map();
  }
  getItem(k) {
    return this.map.has(k) ? this.map.get(k) : null;
  }
  setItem(k, v) {
    this.map.set(k, String(v));
  }
  removeItem(k) {
    this.map.delete(k);
  }
}

const d = mergeLayout(null);
if (d.projectOpen !== true || d.terminalOpen !== false) {
  console.error('layout_smoke: defaults wrong', d);
  process.exit(1);
}

const m = mergeLayout({ terminalOpen: true, terminalFraction: 0.99 });
if (m.terminalFraction !== 0.6) {
  console.error('layout_smoke: clamp failed', m);
  process.exit(1);
}

const store = new MemStorage();
saveLayout(store, { projectOpen: false, terminalOpen: true });
const loaded = loadLayout(store);
if (loaded.projectOpen !== false || loaded.terminalOpen !== true) {
  console.error('layout_smoke: roundtrip failed', loaded);
  process.exit(1);
}

const after = resetLayout(store);
if (store.getItem('flatl-fleditor-layout') != null) {
  console.error('layout_smoke: reset should remove key');
  process.exit(1);
}
if (after.terminalOpen !== DEFAULT_LAYOUT.terminalOpen) {
  console.error('layout_smoke: reset defaults wrong', after);
  process.exit(1);
}

console.log('OK: fleditor layout smoke');
