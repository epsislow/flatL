#!/usr/bin/env node
/**
 * Unit smoke for fleditor theme helpers (no browser).
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const themeJs = path.join(repo, 'docs/fleditor_site/js/fleditor-theme.js');

const require = createRequire(import.meta.url);
require(themeJs);

const T = globalThis.FleditorTheme;

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

const dark = T.effectiveTokens(T.defaultThemeState());
if (dark['fl-bg'] !== '#141418' || dark['fl-panel-bg'] !== '#1c1c22') {
  console.error('theme_smoke: dark tokens', dark['fl-bg'], dark['fl-panel-bg']);
  process.exit(1);
}
if (dark['fl-editor-fl-bg'] !== '#1c1c22' || dark['fl-editor-hex-bg'] !== '#1c1c22') {
  console.error('theme_smoke: editor split tokens', dark['fl-editor-fl-bg']);
  process.exit(1);
}

const migrated = T.normalizeTokens({ 'fl-editor-pane-bg': '#010203', 'fl-editor-gutter-bg': '#040506' });
if (migrated['fl-editor-fl-bg'] !== '#010203' || migrated['fl-editor-text-gutter'] !== '#040506') {
  console.error('theme_smoke: legacy editor token migration', migrated);
  process.exit(1);
}

let st = T.setActivePreset(T.defaultThemeState(), 'light');
const light = T.effectiveTokens(st);
if (light['fl-bg'] === dark['fl-bg']) {
  console.error('theme_smoke: light should differ from dark');
  process.exit(1);
}

st = T.setTokenOverride(st, 'fl-bg', '#010101');
if (st.activePresetId !== T.CUSTOM_PRESET_ID) {
  console.error('theme_smoke: override should switch to custom', st.activePresetId);
  process.exit(1);
}
if (T.effectiveTokens(st)['fl-bg'] !== '#010101') {
  console.error('theme_smoke: override value');
  process.exit(1);
}

const added = T.addUserPreset(st, 'Test', T.effectiveTokens(st));
st = added.state;
if (!T.isUserPresetId(added.id)) {
  console.error('theme_smoke: user id');
  process.exit(1);
}
st = T.deleteUserPreset(st, added.id);
if (st.userPresets.length !== 0) {
  console.error('theme_smoke: delete user preset');
  process.exit(1);
}

const store = new MemStorage();
T.saveThemeState(store, T.setActivePreset(T.defaultThemeState(), 'carbon'));
const loaded = T.loadThemeState(store);
if (loaded.activePresetId !== 'carbon') {
  console.error('theme_smoke: roundtrip', loaded);
  process.exit(1);
}

const ep = T.loadEditorPrefs(store);
if (ep.fontSize !== T.DEFAULT_EDITOR_PREFS.fontSize) {
  console.error('theme_smoke: editor prefs default');
  process.exit(1);
}
T.saveEditorPrefs(store, { fontSize: 16 });
if (T.loadEditorPrefs(store).fontSize !== 16) {
  console.error('theme_smoke: editor prefs save');
  process.exit(1);
}

const store2 = new MemStorage();
T.saveThemeState(store2, T.setActivePreset(T.defaultThemeState(), 'light'));
T.saveEditorPrefs(store2, { fontSize: 18 });
T.resetAllSettings(store2);
if (store2.getItem(T.THEME_KEY) != null || T.loadEditorPrefs(store2).fontSize !== 14) {
  console.error('theme_smoke: resetAllSettings');
  process.exit(1);
}

console.log('OK: fleditor theme smoke');
