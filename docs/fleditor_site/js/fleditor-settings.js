/* global FleditorTheme, FleditorVersion */
(function (global) {
  'use strict';

  const T = FleditorTheme;
  const V = global.FleditorVersion || { FLEDITOR_UI_VERSION: '?' };

  /** @type {ReturnType<typeof T.loadThemeState>} */
  let themeState = T.loadThemeState();
  /** @type {ReturnType<typeof T.loadEditorPrefs>} */
  let editorPrefs = T.loadEditorPrefs();
  /** @type {ReturnType<typeof T.loadTerminalPrefs>} */
  let terminalPrefs = T.loadTerminalPrefs();

  /** @type {null | HTMLElement} */
  let appEl = null;
  /** @type {null | (() => import('codemirror').Editor | null)} */
  let getCm = null;
  /** @type {null | (() => import('xterm').Terminal | null)} */
  let getTerm = null;
  /** @type {null | (() => void)} */
  let onRefreshLayout = null;

  /** @type {null | ((opts: object) => Promise<string | null>)} */
  let showPromptFn = null;
  /** @type {null | ((msg: string, title?: string) => Promise<void>)} */
  let showAlertFn = null;
  /** @type {null | (() => string | null)} */
  let getWasmToolchainVersion = null;

  let dialog = null;
  let themeSelect = null;
  let tokenCustomizeRoot = null;

  function persistThemeAndApply() {
    themeState = T.saveThemeState(null, themeState);
    if (appEl) {
      T.applyTheme(appEl, T.effectiveTokens(themeState));
    }
    applyTerminalTheme();
    refreshThemeSelect();
    syncTokenInputs();
  }

  function applyTerminalTheme() {
    const term = getTerm ? getTerm() : null;
    if (!term) {
      return;
    }
    const tokens = T.effectiveTokens(themeState);
    term.options.theme = T.xtermThemeFromTokens(tokens);
    if (typeof term.refresh === 'function') {
      term.refresh(0, term.rows - 1);
    }
  }

  function applyEditorPrefs() {
    const cm = getCm ? getCm() : null;
    if (!cm) {
      return;
    }
    cm.setOption('lineNumbers', editorPrefs.lineNumbers);
    cm.setOption('lineWrapping', editorPrefs.wordWrap);
    cm.setOption('tabSize', editorPrefs.tabSize);
    cm.setOption('indentUnit', editorPrefs.tabSize);
    const wrap = cm.getWrapperElement();
    if (wrap) {
      wrap.style.fontSize = editorPrefs.fontSize + 'px';
    }
    cm.refresh();
  }

  function applyTerminalPrefs() {
    const term = getTerm ? getTerm() : null;
    if (!term) {
      return;
    }
    term.options.fontSize = terminalPrefs.fontSize;
    if (onRefreshLayout) {
      onRefreshLayout();
    }
  }

  function refreshThemeSelect() {
    if (!themeSelect) {
      return;
    }
    themeSelect.innerHTML = '';
    const opts = T.listPresetOptions(themeState);
    /** @type {HTMLOptGroupElement | null} */
    let userGroup = null;
    for (let i = 0; i < opts.length; i++) {
      const o = opts[i];
      if (o.kind === 'user' && !userGroup) {
        userGroup = document.createElement('optgroup');
        userGroup.label = 'My themes';
        themeSelect.appendChild(userGroup);
      }
      const opt = document.createElement('option');
      opt.value = o.id;
      opt.textContent = o.name;
      if (o.kind === 'user' && userGroup) {
        userGroup.appendChild(opt);
      } else {
        themeSelect.appendChild(opt);
      }
    }
    const active = themeState.activePresetId;
    themeSelect.value = active;
    if (themeSelect.value !== active) {
      themeSelect.value = T.CUSTOM_PRESET_ID;
    }
  }

  function hexForColorInput(cssColor) {
    if (!cssColor || cssColor.charAt(0) !== '#') {
      return '#000000';
    }
    if (cssColor.length === 4) {
      return (
        '#' +
        cssColor[1] +
        cssColor[1] +
        cssColor[2] +
        cssColor[2] +
        cssColor[3] +
        cssColor[3]
      );
    }
    return cssColor.slice(0, 7);
  }

  function buildTokenCustomizeUI() {
    if (!tokenCustomizeRoot) {
      return;
    }
    tokenCustomizeRoot.innerHTML = '';
    const tokens = T.effectiveTokens(themeState);
    const groups = T.GROUP_LABELS;
    const byGroup = {};
    for (let i = 0; i < T.TOKEN_KEYS.length; i++) {
      const key = T.TOKEN_KEYS[i];
      const meta = T.TOKEN_META[key] || { label: key, group: 'chrome' };
      if (!byGroup[meta.group]) {
        byGroup[meta.group] = [];
      }
      byGroup[meta.group].push({ key: key, label: meta.label });
    }
    const groupOrder = [
      'chrome',
      'project',
      'editorFl',
      'syntaxFl',
      'editorText',
      'editorHex',
      'terminal',
    ];
    for (let g = 0; g < groupOrder.length; g++) {
      const gid = groupOrder[g];
      const items = byGroup[gid];
      if (!items || !items.length) {
        continue;
      }
      const details = document.createElement('details');
      details.className = 'fleditor-settings-token-group';
      details.open = gid === 'project' || gid === 'editorFl';
      const summary = document.createElement('summary');
      summary.textContent = groups[gid] || gid;
      details.appendChild(summary);
      for (let j = 0; j < items.length; j++) {
        const item = items[j];
        const row = document.createElement('div');
        row.className = 'fleditor-settings-token-row';
        row.dataset.tokenKey = item.key;
        const lab = document.createElement('label');
        lab.textContent = item.label;
        const color = document.createElement('input');
        color.type = 'color';
        color.value = hexForColorInput(tokens[item.key]);
        color.dataset.tokenKey = item.key;
        const text = document.createElement('input');
        text.type = 'text';
        text.value = tokens[item.key] || '';
        text.spellcheck = false;
        text.autocomplete = 'off';
        text.dataset.tokenKey = item.key;
        function applyFromInputs(key, val) {
          themeState = T.setTokenOverride(themeState, key, val.trim());
          persistThemeAndApply();
        }
        color.addEventListener('input', function () {
          text.value = color.value;
          applyFromInputs(item.key, color.value);
        });
        text.addEventListener('change', function () {
          color.value = hexForColorInput(text.value);
          applyFromInputs(item.key, text.value);
        });
        row.appendChild(lab);
        row.appendChild(color);
        row.appendChild(text);
        details.appendChild(row);
      }
      tokenCustomizeRoot.appendChild(details);
    }
  }

  function syncTokenInputs() {
    if (!tokenCustomizeRoot) {
      return;
    }
    const tokens = T.effectiveTokens(themeState);
    const rows = tokenCustomizeRoot.querySelectorAll('.fleditor-settings-token-row');
    rows.forEach(function (row) {
      const key = row.dataset.tokenKey;
      if (!key) {
        return;
      }
      const color = row.querySelector('input[type="color"]');
      const text = row.querySelector('input[type="text"]');
      if (color && text && tokens[key]) {
        color.value = hexForColorInput(tokens[key]);
        text.value = tokens[key];
      }
    });
  }

  function showSection(sectionId) {
    const sections = document.querySelectorAll('.fleditor-settings-section');
    sections.forEach(function (el) {
      el.hidden = el.dataset.settingsSection !== sectionId;
    });
    const navBtns = document.querySelectorAll('.fleditor-settings-nav-btn');
    navBtns.forEach(function (btn) {
      btn.classList.toggle(
        'fleditor-settings-nav-btn--active',
        btn.dataset.settingsSection === sectionId
      );
    });
    const title = document.getElementById('settingsSectionTitle');
    if (title) {
      const labels = {
        display: 'Display',
        editor: 'Editor',
        terminal: 'Terminal',
        about: 'About',
      };
      title.textContent = labels[sectionId] || 'Settings';
    }
    if (sectionId === 'about') {
      refreshAboutPanel();
    }
  }

  function refreshAboutPanel() {
    const uiEl = document.getElementById('settingsAboutUiVersion');
    const wasmEl = document.getElementById('settingsAboutWasmVersion');
    if (uiEl) {
      uiEl.textContent = V.FLEDITOR_UI_VERSION || '—';
    }
    if (wasmEl) {
      let wasmVer = null;
      if (getWasmToolchainVersion) {
        try {
          wasmVer = getWasmToolchainVersion();
        } catch (_e) {
          wasmVer = null;
        }
      }
      if (wasmVer) {
        wasmEl.textContent = wasmVer;
      } else if (typeof global.FlatLModule === 'function') {
        wasmEl.textContent = 'loaded (version unknown — rebuild WASM)';
      } else {
        wasmEl.textContent = 'not loaded';
      }
    }
  }

  function syncEditorTerminalFormInputs() {
    editorPrefs = T.loadEditorPrefs();
    terminalPrefs = T.loadTerminalPrefs();
    const editorFont = document.getElementById('settingsEditorFontSize');
    const editorLines = document.getElementById('settingsEditorLineNumbers');
    const editorTab = document.getElementById('settingsEditorTabSize');
    const editorWrap = document.getElementById('settingsEditorWordWrap');
    const termFont = document.getElementById('settingsTerminalFontSize');
    const termClear = document.getElementById('settingsTerminalClearOnRun');
    const termProgramInput = document.getElementById('settingsTerminalProgramInput');
    const termMaxJumps = document.getElementById('settingsTerminalMaxJumps');
    const termMaxSteps = document.getElementById('settingsTerminalMaxSteps');
    const termNoJumpLimit = document.getElementById('settingsTerminalNoJumpLimit');
    if (editorFont) {
      editorFont.value = String(editorPrefs.fontSize);
    }
    if (editorLines) {
      editorLines.checked = editorPrefs.lineNumbers;
    }
    if (editorTab) {
      editorTab.value = String(editorPrefs.tabSize);
    }
    if (editorWrap) {
      editorWrap.checked = editorPrefs.wordWrap;
    }
    if (termFont) {
      termFont.value = String(terminalPrefs.fontSize);
    }
    if (termClear) {
      termClear.checked = terminalPrefs.clearOnRun;
    }
    if (termProgramInput) {
      termProgramInput.value =
        terminalPrefs.programInput === 'modal' ? 'modal' : 'terminal';
    }
    if (termMaxJumps) {
      termMaxJumps.value = String(terminalPrefs.maxJumps);
      termMaxJumps.disabled = !!terminalPrefs.noJumpLimit;
    }
    if (termMaxSteps) {
      termMaxSteps.value = String(terminalPrefs.maxSteps);
    }
    if (termNoJumpLimit) {
      termNoJumpLimit.checked = terminalPrefs.noJumpLimit;
    }
  }

  function syncTerminalRunLimitFieldsDisabled() {
    const termMaxJumps = document.getElementById('settingsTerminalMaxJumps');
    const termNoJumpLimit = document.getElementById('settingsTerminalNoJumpLimit');
    if (termMaxJumps && termNoJumpLimit) {
      termMaxJumps.disabled = termNoJumpLimit.checked;
    }
  }

  function initSettingsModal() {
    dialog = document.getElementById('settingsModal');
    themeSelect = document.getElementById('settingsThemeSelect');
    tokenCustomizeRoot = document.getElementById('settingsTokenCustomize');
    const settingsBtn = document.getElementById('settingsBtn');
    const doneBtn = document.getElementById('settingsDoneBtn');
    const resetThemeBtn = document.getElementById('settingsResetThemeBtn');
    const saveThemeBtn = document.getElementById('settingsSaveThemeBtn');
    const deleteThemeBtn = document.getElementById('settingsDeleteThemeBtn');

    if (!dialog || !settingsBtn) {
      return;
    }

    settingsBtn.addEventListener('click', function () {
      refreshThemeSelect();
      buildTokenCustomizeUI();
      syncEditorTerminalFormInputs();
      showSection('display');
      dialog.showModal();
    });

    const resetAllBtn = document.getElementById('settingsResetAllBtn');
    if (resetAllBtn) {
      resetAllBtn.addEventListener('click', async function () {
        if (
          !window.confirm(
            'Reset all settings? Theme, editor, and terminal preferences will return to defaults. Project and panel layout are not affected.'
          )
        ) {
          return;
        }
        const defaults = T.resetAllSettings(null);
        themeState = defaults.theme;
        editorPrefs = defaults.editorPrefs;
        terminalPrefs = defaults.terminalPrefs;
        persistThemeAndApply();
        refreshThemeSelect();
        buildTokenCustomizeUI();
        syncEditorTerminalFormInputs();
        applyEditorPrefs();
        applyTerminalPrefs();
        if (showAlertFn) {
          await showAlertFn('Settings restored to defaults.', 'About');
        }
      });
    }

    if (doneBtn) {
      doneBtn.addEventListener('click', function () {
        dialog.close();
      });
    }

    document.querySelectorAll('.fleditor-settings-nav-btn').forEach(function (btn) {
      btn.addEventListener('click', function () {
        showSection(btn.dataset.settingsSection || 'display');
      });
    });

    if (themeSelect) {
      themeSelect.addEventListener('change', function () {
        const id = themeSelect.value;
        if (id === T.CUSTOM_PRESET_ID) {
          return;
        }
        themeState = T.setActivePreset(themeState, id);
        persistThemeAndApply();
      });
    }

    if (resetThemeBtn) {
      resetThemeBtn.addEventListener('click', function () {
        themeState = T.resetThemeState();
        persistThemeAndApply();
        buildTokenCustomizeUI();
      });
    }

    if (saveThemeBtn) {
      saveThemeBtn.addEventListener('click', async function () {
        if (!showPromptFn) {
          return;
        }
        const name = await showPromptFn({
          title: 'Save theme',
          message: 'Name for this theme preset:',
          defaultValue: 'My theme',
        });
        if (!name || !name.trim()) {
          return;
        }
        const tokens = T.effectiveTokens(themeState);
        const result = T.addUserPreset(themeState, name.trim(), tokens);
        themeState = result.state;
        persistThemeAndApply();
        refreshThemeSelect();
        if (themeSelect) {
          themeSelect.value = result.id;
        }
      });
    }

    if (deleteThemeBtn) {
      deleteThemeBtn.addEventListener('click', async function () {
        const id = themeSelect ? themeSelect.value : '';
        if (!T.isUserPresetId(id)) {
          if (showAlertFn) {
            await showAlertFn('Only user-created themes can be deleted.', 'Settings');
          }
          return;
        }
        themeState = T.deleteUserPreset(themeState, id);
        persistThemeAndApply();
        refreshThemeSelect();
        buildTokenCustomizeUI();
      });
    }

    const editorFont = document.getElementById('settingsEditorFontSize');
    const editorLines = document.getElementById('settingsEditorLineNumbers');
    const editorTab = document.getElementById('settingsEditorTabSize');
    const editorWrap = document.getElementById('settingsEditorWordWrap');

    if (editorFont) {
      editorFont.value = String(editorPrefs.fontSize);
      editorFont.addEventListener('change', function () {
        editorPrefs.fontSize = T.loadEditorPrefs().fontSize;
        editorPrefs = T.saveEditorPrefs(null, {
          fontSize: Number(editorFont.value),
        });
        applyEditorPrefs();
      });
    }
    if (editorLines) {
      editorLines.checked = editorPrefs.lineNumbers;
      editorLines.addEventListener('change', function () {
        editorPrefs = T.saveEditorPrefs(null, { lineNumbers: editorLines.checked });
        applyEditorPrefs();
      });
    }
    if (editorTab) {
      editorTab.value = String(editorPrefs.tabSize);
      editorTab.addEventListener('change', function () {
        editorPrefs = T.saveEditorPrefs(null, { tabSize: Number(editorTab.value) });
        applyEditorPrefs();
      });
    }
    if (editorWrap) {
      editorWrap.checked = editorPrefs.wordWrap;
      editorWrap.addEventListener('change', function () {
        editorPrefs = T.saveEditorPrefs(null, { wordWrap: editorWrap.checked });
        applyEditorPrefs();
      });
    }

    const termFont = document.getElementById('settingsTerminalFontSize');
    const termClear = document.getElementById('settingsTerminalClearOnRun');
    const termProgramInput = document.getElementById('settingsTerminalProgramInput');
    if (termFont) {
      termFont.value = String(terminalPrefs.fontSize);
      termFont.addEventListener('change', function () {
        terminalPrefs = T.saveTerminalPrefs(null, {
          fontSize: Number(termFont.value),
        });
        applyTerminalPrefs();
      });
    }
    if (termClear) {
      termClear.checked = terminalPrefs.clearOnRun;
      termClear.addEventListener('change', function () {
        terminalPrefs = T.saveTerminalPrefs(null, { clearOnRun: termClear.checked });
      });
    }
    if (termProgramInput) {
      termProgramInput.value =
        terminalPrefs.programInput === 'modal' ? 'modal' : 'terminal';
      termProgramInput.addEventListener('change', function () {
        terminalPrefs = T.saveTerminalPrefs(null, {
          programInput: termProgramInput.value === 'modal' ? 'modal' : 'terminal',
        });
      });
    }

    const termMaxJumps = document.getElementById('settingsTerminalMaxJumps');
    const termMaxSteps = document.getElementById('settingsTerminalMaxSteps');
    const termNoJumpLimit = document.getElementById('settingsTerminalNoJumpLimit');
    if (termMaxJumps) {
      termMaxJumps.value = String(terminalPrefs.maxJumps);
      termMaxJumps.addEventListener('change', function () {
        terminalPrefs = T.saveTerminalPrefs(null, {
          maxJumps: Number(termMaxJumps.value),
        });
      });
    }
    if (termMaxSteps) {
      termMaxSteps.value = String(terminalPrefs.maxSteps);
      termMaxSteps.addEventListener('change', function () {
        terminalPrefs = T.saveTerminalPrefs(null, {
          maxSteps: Number(termMaxSteps.value),
        });
      });
    }
    if (termNoJumpLimit) {
      termNoJumpLimit.checked = terminalPrefs.noJumpLimit;
      termNoJumpLimit.addEventListener('change', function () {
        terminalPrefs = T.saveTerminalPrefs(null, { noJumpLimit: termNoJumpLimit.checked });
        syncTerminalRunLimitFieldsDisabled();
      });
      syncTerminalRunLimitFieldsDisabled();
    }
  }

  function init(options) {
    appEl = options.appEl || document.querySelector('.fleditor-app');
    getCm = options.getCm || null;
    getTerm = options.getTerm || null;
    onRefreshLayout = options.onRefreshLayout || null;
    showPromptFn = options.showPrompt || null;
    showAlertFn = options.showAlert || null;
    getWasmToolchainVersion = options.getWasmToolchainVersion || null;

    themeState = T.loadThemeState();
    editorPrefs = T.loadEditorPrefs();
    terminalPrefs = T.loadTerminalPrefs();

    if (appEl) {
      T.applyTheme(appEl, T.effectiveTokens(themeState));
    }

    initSettingsModal();
  }

  function getTerminalPrefs() {
    return terminalPrefs;
  }

  function applyThemeFromStorage() {
    themeState = T.loadThemeState();
    if (appEl) {
      T.applyTheme(appEl, T.effectiveTokens(themeState));
    }
    applyTerminalTheme();
  }

  global.FleditorSettings = {
    init,
    getTerminalPrefs,
    applyThemeFromStorage,
    applyEditorPrefs,
    applyTerminalPrefs,
    applyTerminalTheme,
    refreshAboutPanel,
  };
})(typeof window !== 'undefined' ? window : globalThis);
