/* global window, document */
(function (global) {
  'use strict';

  const THEME_KEY = 'flatl-fleditor-theme';
  const EDITOR_PREFS_KEY = 'flatl-fleditor-editor-prefs';
  const TERMINAL_PREFS_KEY = 'flatl-fleditor-terminal-prefs';

  const CUSTOM_PRESET_ID = 'custom';
  const DEFAULT_PRESET_ID = 'dark';

  /** @type {string[]} */
  const TOKEN_KEYS = [
    'fl-bg',
    'fl-surface',
    'fl-panel-bg',
    'fl-border',
    'fl-text',
    'fl-text-muted',
    'fl-string',
    'fl-error',
    'fl-btn-run',
    'fl-btn-run-hover',
    'fl-tree-dir',
    'fl-tree-fl',
    'fl-tree-binary',
    'fl-tree-text',
    'fl-editor-fl-bg',
    'fl-editor-fl-gutter',
    'fl-editor-text-bg',
    'fl-editor-text-gutter',
    'fl-editor-hex-bg',
    'fl-cm-string',
    'fl-cm-number',
    'fl-cm-keyword',
    'fl-cm-block',
    'fl-cm-native',
    'fl-cm-comment',
    'fl-xterm-bg',
    'fl-xterm-fg',
    'fl-xterm-cursor',
  ];

  /** @type {Record<string, { label: string, group: string }>} */
  const TOKEN_META = {
    'fl-bg': { label: 'App background', group: 'chrome' },
    'fl-surface': { label: 'Surface', group: 'chrome' },
    'fl-border': { label: 'Borders', group: 'chrome' },
    'fl-text': { label: 'Text', group: 'chrome' },
    'fl-text-muted': { label: 'Muted text', group: 'chrome' },
    'fl-string': { label: 'Accent string', group: 'chrome' },
    'fl-error': { label: 'Error', group: 'chrome' },
    'fl-btn-run': { label: 'Run button', group: 'chrome' },
    'fl-btn-run-hover': { label: 'Run button hover', group: 'chrome' },
    'fl-panel-bg': { label: 'Project panel background', group: 'project' },
    'fl-tree-dir': { label: 'Folder names', group: 'project' },
    'fl-tree-fl': { label: '.fl files', group: 'project' },
    'fl-tree-binary': { label: 'Binary files', group: 'project' },
    'fl-tree-text': { label: 'Text files', group: 'project' },
    'fl-editor-fl-bg': { label: 'Editor background', group: 'editorFl' },
    'fl-editor-fl-gutter': { label: 'Line gutter', group: 'editorFl' },
    'fl-cm-string': { label: 'Syntax: string', group: 'syntaxFl' },
    'fl-cm-number': { label: 'Syntax: number', group: 'syntaxFl' },
    'fl-cm-keyword': { label: 'Syntax: keyword', group: 'syntaxFl' },
    'fl-cm-block': { label: 'Syntax: block', group: 'syntaxFl' },
    'fl-cm-native': { label: 'Syntax: native', group: 'syntaxFl' },
    'fl-cm-comment': { label: 'Syntax: comment', group: 'syntaxFl' },
    'fl-editor-text-bg': { label: 'Editor background', group: 'editorText' },
    'fl-editor-text-gutter': { label: 'Line gutter', group: 'editorText' },
    'fl-editor-hex-bg': { label: 'Hex grid background', group: 'editorHex' },
    'fl-xterm-bg': { label: 'Terminal background', group: 'terminal' },
    'fl-xterm-fg': { label: 'Terminal text', group: 'terminal' },
    'fl-xterm-cursor': { label: 'Terminal cursor', group: 'terminal' },
  };

  const GROUP_LABELS = {
    chrome: 'Chrome',
    project: 'Project',
    editorFl: 'Fl editor',
    syntaxFl: 'FlatL syntax (Fl editor)',
    editorText: 'Text editor',
    editorHex: 'Hex editor',
    terminal: 'Terminal',
  };

  function darkTokens() {
    return {
      'fl-bg': '#141418',
      'fl-surface': '#1c1c22',
      'fl-panel-bg': '#1c1c22',
      'fl-border': '#3a3844',
      'fl-text': '#e6dcc8',
      'fl-text-muted': '#6e6878',
      'fl-string': '#c6e548',
      'fl-error': '#f07178',
      'fl-btn-run': '#5a7a4a',
      'fl-btn-run-hover': '#6d9460',
      'fl-tree-dir': '#a89ec4',
      'fl-tree-fl': '#b8d67a',
      'fl-tree-binary': '#e8b060',
      'fl-tree-text': '#7eb8da',
      'fl-editor-fl-bg': '#1c1c22',
      'fl-editor-fl-gutter': '#1c1c22',
      'fl-editor-text-bg': '#1c1c22',
      'fl-editor-text-gutter': '#1c1c22',
      'fl-editor-hex-bg': '#1c1c22',
      'fl-cm-string': '#c6e548',
      'fl-cm-number': '#9bb8d4',
      'fl-cm-keyword': '#d4a574',
      'fl-cm-block': '#b8a0e8',
      'fl-cm-native': '#7ec8e3',
      'fl-cm-comment': '#8b8372',
      'fl-xterm-bg': '#1c1c22',
      'fl-xterm-fg': '#e6dcc8',
      'fl-xterm-cursor': '#e6dcc8',
    };
  }

  function lightTokens() {
    return {
      'fl-bg': '#e8e4dc',
      'fl-surface': '#f4f1ea',
      'fl-panel-bg': '#f4f1ea',
      'fl-border': '#b8b4ac',
      'fl-text': '#2a2830',
      'fl-text-muted': '#6a6570',
      'fl-string': '#5a7a18',
      'fl-error': '#c03840',
      'fl-btn-run': '#4a6a3a',
      'fl-btn-run-hover': '#5a8050',
      'fl-tree-dir': '#6a5898',
      'fl-tree-fl': '#4a7828',
      'fl-tree-binary': '#a06820',
      'fl-tree-text': '#2878a8',
      'fl-editor-fl-bg': '#faf8f4',
      'fl-editor-fl-gutter': '#eeebe4',
      'fl-editor-text-bg': '#faf8f4',
      'fl-editor-text-gutter': '#eeebe4',
      'fl-editor-hex-bg': '#f0ece6',
      'fl-cm-string': '#5a7a18',
      'fl-cm-number': '#2878a0',
      'fl-cm-keyword': '#a05828',
      'fl-cm-block': '#6848a8',
      'fl-cm-native': '#2088a8',
      'fl-cm-comment': '#7a7568',
      'fl-xterm-bg': '#f0ece4',
      'fl-xterm-fg': '#2a2830',
      'fl-xterm-cursor': '#2a2830',
    };
  }

  function carbonTokens() {
    const d = darkTokens();
    return Object.assign({}, d, {
      'fl-bg': '#0e1014',
      'fl-surface': '#161a22',
      'fl-panel-bg': '#161a22',
      'fl-border': '#2e3644',
      'fl-text': '#d8dee8',
      'fl-text-muted': '#6b7788',
      'fl-tree-dir': '#8899b8',
      'fl-tree-fl': '#98c070',
      'fl-tree-binary': '#d4a050',
      'fl-tree-text': '#6ab0d8',
      'fl-editor-fl-bg': '#161a22',
      'fl-editor-fl-gutter': '#121620',
      'fl-editor-text-bg': '#161a22',
      'fl-editor-text-gutter': '#121620',
      'fl-editor-hex-bg': '#121620',
      'fl-xterm-bg': '#161a22',
      'fl-xterm-fg': '#d8dee8',
    });
  }

  function turquoiseGrayTokens() {
    const d = darkTokens();
    return Object.assign({}, d, {
      'fl-bg': '#12181a',
      'fl-surface': '#1a2428',
      'fl-panel-bg': '#1a2428',
      'fl-border': '#2e4448',
      'fl-text': '#d6e8e4',
      'fl-text-muted': '#688880',
      'fl-btn-run': '#3a7868',
      'fl-btn-run-hover': '#489880',
      'fl-tree-dir': '#88a8c0',
      'fl-tree-fl': '#98d080',
      'fl-tree-binary': '#e0b060',
      'fl-tree-text': '#58c8d8',
      'fl-editor-fl-bg': '#1a2428',
      'fl-editor-fl-gutter': '#152022',
      'fl-editor-text-bg': '#1a2428',
      'fl-editor-text-gutter': '#152022',
      'fl-editor-hex-bg': '#152022',
      'fl-xterm-bg': '#1a2428',
      'fl-xterm-fg': '#d6e8e4',
    });
  }

  /** @type {Record<string, { id: string, name: string, tokens: Record<string, string> }>} */
  const BUILTIN_PRESETS = {
    dark: { id: 'dark', name: 'Dark', tokens: darkTokens() },
    light: { id: 'light', name: 'Light', tokens: lightTokens() },
    carbon: { id: 'carbon', name: 'Carbon', tokens: carbonTokens() },
    'turquoise-gray': {
      id: 'turquoise-gray',
      name: 'Turquoise Gray',
      tokens: turquoiseGrayTokens(),
    },
  };

  const DEFAULT_EDITOR_PREFS = {
    fontSize: 14,
    lineNumbers: true,
    tabSize: 2,
    wordWrap: false,
  };

  const DEFAULT_TERMINAL_PREFS = {
    fontSize: 14,
    clearOnRun: true,
    /** 'terminal' | 'modal' — where io.in* reads while WASM is running */
    programInput: 'terminal',
    /** 0 = VM default (1000); see FLAT_VM_DEFAULT_MAX_JUMPS */
    maxJumps: 1000,
    /** 0 = unlimited opcode steps per run */
    maxSteps: 0,
    noJumpLimit: false,
  };

  function normalizeProgramInput(v) {
    return v === 'modal' ? 'modal' : 'terminal';
  }

  function isBuiltInPresetId(id) {
    return !!id && Object.prototype.hasOwnProperty.call(BUILTIN_PRESETS, id);
  }

  function isUserPresetId(id) {
    return typeof id === 'string' && id.startsWith('user-');
  }

  function cloneTokens(tokens) {
    const out = {};
    for (let i = 0; i < TOKEN_KEYS.length; i++) {
      const k = TOKEN_KEYS[i];
      out[k] = tokens[k];
    }
    return out;
  }

  function migrateLegacyEditorTokens(partial, base) {
    if (!partial || typeof partial !== 'object') {
      return;
    }
    const legacyPane = partial['fl-editor-pane-bg'];
    const legacyGutter = partial['fl-editor-gutter-bg'];
    if (typeof legacyPane === 'string' && legacyPane.length > 0) {
      if (typeof partial['fl-editor-fl-bg'] !== 'string') {
        base['fl-editor-fl-bg'] = legacyPane;
      }
      if (typeof partial['fl-editor-text-bg'] !== 'string') {
        base['fl-editor-text-bg'] = legacyPane;
      }
      if (typeof partial['fl-editor-hex-bg'] !== 'string') {
        base['fl-editor-hex-bg'] = legacyPane;
      }
    }
    if (typeof legacyGutter === 'string' && legacyGutter.length > 0) {
      if (typeof partial['fl-editor-fl-gutter'] !== 'string') {
        base['fl-editor-fl-gutter'] = legacyGutter;
      }
      if (typeof partial['fl-editor-text-gutter'] !== 'string') {
        base['fl-editor-text-gutter'] = legacyGutter;
      }
    }
  }

  function normalizeTokens(partial) {
    const base = darkTokens();
    if (!partial || typeof partial !== 'object') {
      return base;
    }
    migrateLegacyEditorTokens(partial, base);
    for (let i = 0; i < TOKEN_KEYS.length; i++) {
      const k = TOKEN_KEYS[i];
      if (typeof partial[k] === 'string' && partial[k].length > 0) {
        base[k] = partial[k];
      }
    }
    return base;
  }

  function presetTokens(presetId, userPresets) {
    if (isBuiltInPresetId(presetId)) {
      return cloneTokens(BUILTIN_PRESETS[presetId].tokens);
    }
    if (isUserPresetId(presetId) && Array.isArray(userPresets)) {
      for (let i = 0; i < userPresets.length; i++) {
        const p = userPresets[i];
        if (p && p.id === presetId && p.tokens) {
          return normalizeTokens(p.tokens);
        }
      }
    }
    return cloneTokens(BUILTIN_PRESETS.dark.tokens);
  }

  function defaultThemeState() {
    return {
      activePresetId: DEFAULT_PRESET_ID,
      customBaseId: null,
      overrides: {},
      userPresets: [],
    };
  }

  function normalizeThemeState(raw) {
    const out = defaultThemeState();
    if (!raw || typeof raw !== 'object') {
      return out;
    }
    if (typeof raw.activePresetId === 'string' && raw.activePresetId.length > 0) {
      out.activePresetId = raw.activePresetId;
    }
    if (typeof raw.customBaseId === 'string') {
      out.customBaseId = raw.customBaseId;
    }
    if (raw.overrides && typeof raw.overrides === 'object') {
      out.overrides = {};
      for (const k of Object.keys(raw.overrides)) {
        if (TOKEN_KEYS.indexOf(k) >= 0 && typeof raw.overrides[k] === 'string') {
          out.overrides[k] = raw.overrides[k];
        }
      }
      migrateLegacyEditorTokens(raw.overrides, out.overrides);
    }
    if (Array.isArray(raw.userPresets)) {
      out.userPresets = raw.userPresets
        .filter(function (p) {
          return p && typeof p.id === 'string' && typeof p.name === 'string' && p.tokens;
        })
        .map(function (p) {
          return {
            id: p.id,
            name: p.name,
            tokens: normalizeTokens(p.tokens),
          };
        });
    }
    if (
      out.activePresetId !== CUSTOM_PRESET_ID &&
      !isBuiltInPresetId(out.activePresetId) &&
      !isUserPresetId(out.activePresetId)
    ) {
      const userMatch = out.userPresets.some(function (p) {
        return p.id === out.activePresetId;
      });
      if (!userMatch) {
        out.activePresetId = DEFAULT_PRESET_ID;
      }
    }
    if (out.activePresetId === CUSTOM_PRESET_ID && !out.customBaseId) {
      out.customBaseId = DEFAULT_PRESET_ID;
    }
    return out;
  }

  function effectiveTokens(state) {
    const st = normalizeThemeState(state);
    if (st.activePresetId === CUSTOM_PRESET_ID) {
      const base = presetTokens(st.customBaseId || DEFAULT_PRESET_ID, st.userPresets);
      return Object.assign(base, st.overrides);
    }
    return presetTokens(st.activePresetId, st.userPresets);
  }

  function tokensEqual(a, b) {
    for (let i = 0; i < TOKEN_KEYS.length; i++) {
      const k = TOKEN_KEYS[i];
      if (a[k] !== b[k]) {
        return false;
      }
    }
    return true;
  }

  function diffOverrides(base, current) {
    /** @type {Record<string, string>} */
    const out = {};
    for (let i = 0; i < TOKEN_KEYS.length; i++) {
      const k = TOKEN_KEYS[i];
      if (current[k] !== base[k]) {
        out[k] = current[k];
      }
    }
    return out;
  }

  function applyTheme(appEl, tokens) {
    if (!appEl || !tokens) {
      return;
    }
    for (let i = 0; i < TOKEN_KEYS.length; i++) {
      const k = TOKEN_KEYS[i];
      if (tokens[k]) {
        appEl.style.setProperty('--' + k, tokens[k]);
      }
    }
  }

  function xtermThemeFromTokens(tokens) {
    return {
      background: tokens['fl-xterm-bg'] || '#1c1c22',
      foreground: tokens['fl-xterm-fg'] || '#e6dcc8',
      cursor: tokens['fl-xterm-cursor'] || '#e6dcc8',
    };
  }

  function loadThemeState(storage) {
    const store = storage || global.localStorage;
    try {
      const raw = store.getItem(THEME_KEY);
      if (!raw) {
        return defaultThemeState();
      }
      return normalizeThemeState(JSON.parse(raw));
    } catch (_e) {
      return defaultThemeState();
    }
  }

  function saveThemeState(storage, state) {
    const store = storage || global.localStorage;
    const merged = normalizeThemeState(state);
    store.setItem(THEME_KEY, JSON.stringify(merged));
    return merged;
  }

  function setActivePreset(state, presetId) {
    const st = normalizeThemeState(state);
    st.activePresetId = presetId;
    st.overrides = {};
    st.customBaseId = null;
    return st;
  }

  function setTokenOverride(state, key, value) {
    const st = normalizeThemeState(state);
    if (TOKEN_KEYS.indexOf(key) < 0) {
      return st;
    }
    if (st.activePresetId !== CUSTOM_PRESET_ID) {
      st.customBaseId =
        isBuiltInPresetId(st.activePresetId) || isUserPresetId(st.activePresetId)
          ? st.activePresetId
          : DEFAULT_PRESET_ID;
      st.activePresetId = CUSTOM_PRESET_ID;
    }
    if (!st.customBaseId) {
      st.customBaseId = DEFAULT_PRESET_ID;
    }
    const base = presetTokens(st.customBaseId, st.userPresets);
    if (value === base[key]) {
      delete st.overrides[key];
    } else {
      st.overrides[key] = value;
    }
    if (Object.keys(st.overrides).length === 0) {
      st.activePresetId = st.customBaseId;
      st.customBaseId = null;
    }
    return st;
  }

  function newUserPresetId() {
    return (
      'user-' +
      Date.now().toString(36) +
      '-' +
      Math.random().toString(36).slice(2, 8)
    );
  }

  function addUserPreset(state, name, tokens) {
    const st = normalizeThemeState(state);
    const id = newUserPresetId();
    st.userPresets.push({
      id: id,
      name: name,
      tokens: normalizeTokens(tokens),
    });
    st.activePresetId = id;
    st.overrides = {};
    st.customBaseId = null;
    return { state: st, id: id };
  }

  function deleteUserPreset(state, presetId) {
    const st = normalizeThemeState(state);
    st.userPresets = st.userPresets.filter(function (p) {
      return p.id !== presetId;
    });
    if (st.activePresetId === presetId) {
      st.activePresetId = DEFAULT_PRESET_ID;
      st.overrides = {};
      st.customBaseId = null;
    }
    return st;
  }

  function resetThemeState() {
    return defaultThemeState();
  }

  function resetAllSettings(storage) {
    const store = storage || global.localStorage;
    store.removeItem(THEME_KEY);
    store.removeItem(EDITOR_PREFS_KEY);
    store.removeItem(TERMINAL_PREFS_KEY);
    return {
      theme: defaultThemeState(),
      editorPrefs: Object.assign({}, DEFAULT_EDITOR_PREFS),
      terminalPrefs: Object.assign({}, DEFAULT_TERMINAL_PREFS),
    };
  }

  function loadEditorPrefs(storage) {
    const store = storage || global.localStorage;
    try {
      const raw = store.getItem(EDITOR_PREFS_KEY);
      if (!raw) {
        return Object.assign({}, DEFAULT_EDITOR_PREFS);
      }
      const p = JSON.parse(raw);
      return {
        fontSize: clampNum(p.fontSize, 10, 24, DEFAULT_EDITOR_PREFS.fontSize),
        lineNumbers: typeof p.lineNumbers === 'boolean' ? p.lineNumbers : DEFAULT_EDITOR_PREFS.lineNumbers,
        tabSize: clampNum(p.tabSize, 2, 8, DEFAULT_EDITOR_PREFS.tabSize),
        wordWrap: typeof p.wordWrap === 'boolean' ? p.wordWrap : DEFAULT_EDITOR_PREFS.wordWrap,
      };
    } catch (_e) {
      return Object.assign({}, DEFAULT_EDITOR_PREFS);
    }
  }

  function saveEditorPrefs(storage, prefs) {
    const store = storage || global.localStorage;
    const merged = loadEditorPrefs(store);
    Object.assign(merged, prefs);
    store.setItem(EDITOR_PREFS_KEY, JSON.stringify(merged));
    return merged;
  }

  function loadTerminalPrefs(storage) {
    const store = storage || global.localStorage;
    try {
      const raw = store.getItem(TERMINAL_PREFS_KEY);
      if (!raw) {
        return Object.assign({}, DEFAULT_TERMINAL_PREFS);
      }
      const p = JSON.parse(raw);
      return {
        fontSize: clampNum(p.fontSize, 10, 24, DEFAULT_TERMINAL_PREFS.fontSize),
        clearOnRun: typeof p.clearOnRun === 'boolean' ? p.clearOnRun : DEFAULT_TERMINAL_PREFS.clearOnRun,
        programInput: normalizeProgramInput(p.programInput),
        maxJumps: clampNum(p.maxJumps, 0, 100000000, DEFAULT_TERMINAL_PREFS.maxJumps),
        maxSteps: clampNum(p.maxSteps, 0, 100000000, DEFAULT_TERMINAL_PREFS.maxSteps),
        noJumpLimit: typeof p.noJumpLimit === 'boolean' ? p.noJumpLimit : DEFAULT_TERMINAL_PREFS.noJumpLimit,
      };
    } catch (_e) {
      return Object.assign({}, DEFAULT_TERMINAL_PREFS);
    }
  }

  function saveTerminalPrefs(storage, prefs) {
    const store = storage || global.localStorage;
    const merged = loadTerminalPrefs(store);
    Object.assign(merged, prefs);
    store.setItem(TERMINAL_PREFS_KEY, JSON.stringify(merged));
    return merged;
  }

  function clampNum(n, min, max, fallback) {
    if (typeof n !== 'number' || Number.isNaN(n)) {
      return fallback;
    }
    return Math.min(max, Math.max(min, Math.round(n)));
  }

  function listPresetOptions(state) {
    const st = normalizeThemeState(state);
    /** @type {{ id: string, name: string, kind: 'builtin' | 'custom' | 'user' }[]} */
    const out = [];
    const builtins = Object.keys(BUILTIN_PRESETS);
    for (let i = 0; i < builtins.length; i++) {
      const id = builtins[i];
      out.push({ id: id, name: BUILTIN_PRESETS[id].name, kind: 'builtin' });
    }
    out.push({ id: CUSTOM_PRESET_ID, name: 'Custom', kind: 'custom' });
    for (let j = 0; j < st.userPresets.length; j++) {
      const u = st.userPresets[j];
      out.push({ id: u.id, name: u.name, kind: 'user' });
    }
    return out;
  }

  global.FleditorTheme = {
    THEME_KEY,
    EDITOR_PREFS_KEY,
    TERMINAL_PREFS_KEY,
    TOKEN_KEYS,
    TOKEN_META,
    GROUP_LABELS,
    CUSTOM_PRESET_ID,
    DEFAULT_PRESET_ID,
    BUILTIN_PRESETS,
    DEFAULT_EDITOR_PREFS,
    DEFAULT_TERMINAL_PREFS,
    isBuiltInPresetId,
    isUserPresetId,
    darkTokens,
    normalizeTokens,
    presetTokens,
    effectiveTokens,
    diffOverrides,
    tokensEqual,
    applyTheme,
    xtermThemeFromTokens,
    loadThemeState,
    saveThemeState,
    setActivePreset,
    setTokenOverride,
    addUserPreset,
    deleteUserPreset,
    resetThemeState,
    resetAllSettings,
    defaultThemeState,
    loadEditorPrefs,
    saveEditorPrefs,
    loadTerminalPrefs,
    saveTerminalPrefs,
    listPresetOptions,
    normalizeThemeState,
  };
})(typeof window !== 'undefined' ? window : globalThis);
