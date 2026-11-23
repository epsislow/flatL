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
  /** @type {ReturnType<typeof T.loadHttpPrefs>} */
  let httpPrefs = T.loadHttpPrefs();
  /** @type {ReturnType<typeof T.loadEnvPrefs>} */
  let envPrefs = T.loadEnvPrefs();
  let editingEnvKey = null;
  /** @type {ReturnType<typeof T.loadPathPrefs>} */
  let pathPrefs = T.loadPathPrefs();

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
  /** @type {null | (() => string | null)} */
  let getWasmMods = null;

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

  function applyHttpPrefs() {
    global.FLAT_HTTP_ALLOW_CROSS_ORIGIN = httpPrefs.allowCrossOrigin !== false;
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
        http: 'HTTP',
        env: 'Env',
        path: 'Path',
        about: 'About',
      };
      title.textContent = labels[sectionId] || 'Settings';
    }
    if (sectionId === 'about') {
      refreshAboutPanel();
    }
    if (sectionId === 'http') {
      refreshHttpPanel();
    }
    if (sectionId === 'env') {
      refreshEnvPanel();
    }
    if (sectionId === 'path') {
      refreshPathPanel();
    }
  }

  function refreshPathPanel() {
    pathPrefs = T.loadPathPrefs();
    const sel = document.getElementById('settingsPathSep');
    if (sel) {
      if (pathPrefs.sep === '/') {
        sel.value = '/';
      } else if (pathPrefs.sep === '\\') {
        sel.value = 'backslash';
      } else {
        sel.value = 'auto';
      }
    }
  }

  function wasmHasModule(name) {
    let modsStr = null;
    if (getWasmMods) {
      try {
        modsStr = getWasmMods();
      } catch (_e) {
        modsStr = null;
      }
    }
    const mods = parseModsList(modsStr);
    return mods.indexOf(name) >= 0;
  }

  function wasmHasHttpModule() {
    return wasmHasModule('http');
  }

  function wasmHasEnvModule() {
    return wasmHasModule('env');
  }

  function refreshHttpPanel() {
    httpPrefs = T.loadHttpPrefs();
    const noMod = document.getElementById('settingsHttpNoModule');
    const controls = document.getElementById('settingsHttpControls');
    const cross = document.getElementById('settingsHttpAllowCrossOrigin');
    const hasHttp = wasmHasHttpModule();
    if (noMod) {
      noMod.hidden = hasHttp;
    }
    if (controls) {
      controls.hidden = !hasHttp;
    }
    if (cross) {
      cross.checked = httpPrefs.allowCrossOrigin !== false;
    }
    applyHttpPrefs();
  }

  function findEnvEntryIndex(key) {
    const k = (key || '').trim();
    for (let i = 0; i < envPrefs.entries.length; i++) {
      if (envPrefs.entries[i].key === k) {
        return i;
      }
    }
    return -1;
  }

  function renderEnvList() {
    const list = document.getElementById('settingsEnvList');
    const empty = document.getElementById('settingsEnvEmpty');
    const count = document.getElementById('settingsEnvCount');
    if (!list) {
      return;
    }
    list.innerHTML = '';
    const entries = envPrefs.entries || [];
    if (empty) {
      empty.hidden = entries.length > 0;
    }
    if (count) {
      count.textContent = entries.length + (entries.length === 1 ? ' entry' : ' entries');
    }
    if (entries.length === 0) {
      list.hidden = true;
      return;
    }
    list.hidden = false;

    const header = document.createElement('div');
    header.className = 'fleditor-env-table-row fleditor-env-table-row--header';
    header.setAttribute('role', 'row');
    [
      ['Key', 'key'],
      ['Value', 'value'],
      ['Type', 'type'],
      ['Actions', 'actions'],
    ].forEach(function (item) {
      const cell = document.createElement('div');
      cell.className = 'fleditor-env-cell fleditor-env-cell--' + item[1];
      cell.setAttribute('role', 'columnheader');
      cell.textContent = item[0];
      header.appendChild(cell);
    });
    list.appendChild(header);

    entries.forEach(function (e) {
      const row = document.createElement('div');
      row.className = 'fleditor-env-table-row';
      row.setAttribute('role', 'row');

      const key = document.createElement('div');
      key.className = 'fleditor-env-cell fleditor-env-cell--key';
      key.setAttribute('role', 'cell');
      const keyCode = document.createElement('code');
      keyCode.textContent = e.key;
      key.appendChild(keyCode);

      const value = document.createElement('div');
      value.className = 'fleditor-env-cell fleditor-env-cell--value';
      value.setAttribute('role', 'cell');
      const valueCode = document.createElement('code');
      valueCode.textContent = e.sensitive ? '••••••••' : (e.value === '' ? '(empty)' : e.value);
      valueCode.title = e.sensitive ? 'Sensitive value hidden' : e.value;
      value.appendChild(valueCode);

      const type = document.createElement('div');
      type.className = 'fleditor-env-cell fleditor-env-cell--type';
      type.setAttribute('role', 'cell');
      const typeTag = document.createElement('span');
      typeTag.className = e.sensitive ? 'fleditor-env-type fleditor-env-type--secret' : 'fleditor-env-type';
      typeTag.textContent = e.sensitive ? 'Secret' : 'Plain';
      type.appendChild(typeTag);

      const actions = document.createElement('div');
      actions.className = 'fleditor-env-cell fleditor-env-cell--actions';
      actions.setAttribute('role', 'cell');

      const editBtn = document.createElement('button');
      editBtn.type = 'button';
      editBtn.className = 'fleditor-env-icon-btn';
      editBtn.title = 'Edit ' + e.key;
      editBtn.setAttribute('aria-label', 'Edit ' + e.key);
      editBtn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 20h9"/><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L8 18l-4 1 1-4Z"/></svg>';
      editBtn.addEventListener('click', function () {
        const keyEl = document.getElementById('settingsEnvKey');
        const valEl = document.getElementById('settingsEnvValue');
        const sensEl = document.getElementById('settingsEnvSensitive');
        const addBtn = document.getElementById('settingsEnvAddBtn');
        editingEnvKey = e.key;
        if (keyEl) keyEl.value = e.key;
        if (valEl) valEl.value = e.sensitive ? '' : e.value;
        if (sensEl) sensEl.checked = !!e.sensitive;
        if (addBtn) {
          addBtn.querySelector('span:last-child').textContent = 'Save changes';
          addBtn.querySelector('.fleditor-env-button-icon').textContent = '✓';
        }
        if (keyEl) keyEl.focus();
      });

      const copyBtn = document.createElement('button');
      copyBtn.type = 'button';
      copyBtn.className = 'fleditor-env-icon-btn';
      copyBtn.title = 'Copy value';
      copyBtn.setAttribute('aria-label', 'Copy value for ' + e.key);
      copyBtn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3"/></svg>';
      copyBtn.addEventListener('click', async function () {
        try {
          if (navigator.clipboard && navigator.clipboard.writeText) {
            await navigator.clipboard.writeText(e.value);
          } else {
            const temp = document.createElement('textarea');
            temp.value = e.value;
            temp.style.position = 'fixed';
            temp.style.opacity = '0';
            document.body.appendChild(temp);
            temp.select();
            document.execCommand('copy');
            temp.remove();
          }
          copyBtn.classList.add('is-copied');
          copyBtn.title = 'Copied';
          window.setTimeout(function () {
            copyBtn.classList.remove('is-copied');
            copyBtn.title = 'Copy value';
          }, 1000);
        } catch (_err) {
          /* Clipboard may be unavailable in an insecure context. */
        }
      });

      actions.appendChild(editBtn);
      actions.appendChild(copyBtn);
      row.appendChild(key);
      row.appendChild(value);
      row.appendChild(type);
      row.appendChild(actions);
      list.appendChild(row);
    });
  }

  function refreshEnvPanel() {
    envPrefs = T.loadEnvPrefs();
    const noMod = document.getElementById('settingsEnvNoModule');
    const controls = document.getElementById('settingsEnvControls');
    const argsEl = document.getElementById('settingsEnvProgramArgs');
    const hasEnv = wasmHasEnvModule();
    if (noMod) {
      noMod.hidden = hasEnv;
    }
    if (controls) {
      controls.hidden = !hasEnv;
    }
    if (argsEl) {
      argsEl.value = envPrefs.programArgs || '';
    }
    renderEnvList();
  }

  function persistEnvPrefs(next) {
    envPrefs = T.saveEnvPrefs(null, next || envPrefs);
    return envPrefs;
  }

  function parseModsList(modsStr) {
    if (!modsStr || typeof modsStr !== 'string') {
      return [];
    }
    return modsStr
      .split(/\s+/)
      .map(function (s) {
        return s.trim();
      })
      .filter(Boolean);
  }

  function refreshAboutPanel() {
    const uiEl = document.getElementById('settingsAboutUiVersion');
    const wasmEl = document.getElementById('settingsAboutWasmVersion');
    const httpEl = document.getElementById('settingsAboutHttpMod');
    const modsEl = document.getElementById('settingsAboutBuiltMods');
    const httpHint = document.getElementById('settingsAboutHttpHint');
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

    let modsStr = null;
    if (getWasmMods) {
      try {
        modsStr = getWasmMods();
      } catch (_e) {
        modsStr = null;
      }
    }
    const mods = parseModsList(modsStr);
    const hasHttp = mods.indexOf('http') >= 0;
    if (modsEl) {
      if (mods.length > 0) {
        modsEl.textContent = mods.join(', ');
      } else if (wasmEl && wasmEl.textContent === 'not loaded') {
        modsEl.textContent = '—';
      } else {
        modsEl.textContent = 'unknown (rebuild WASM for flat_wasm_mods)';
      }
    }
    if (httpEl) {
      if (mods.length > 0) {
        httpEl.textContent = hasHttp
          ? 'yes (use native http)'
          : 'no (built with http=0)';
      } else if (wasmEl && wasmEl.textContent === 'not loaded') {
        httpEl.textContent = '—';
      } else {
        httpEl.textContent = 'unknown (rebuild WASM)';
      }
    }
    if (httpHint) {
      httpHint.hidden = !(mods.length > 0 && !hasHttp);
    }
  }

  function syncEditorTerminalFormInputs() {
    editorPrefs = T.loadEditorPrefs();
    terminalPrefs = T.loadTerminalPrefs();
    httpPrefs = T.loadHttpPrefs();
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
    const httpCross = document.getElementById('settingsHttpAllowCrossOrigin');
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
    if (httpCross) {
      httpCross.checked = httpPrefs.allowCrossOrigin !== false;
    }
    applyHttpPrefs();
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
            'Reset all settings? Theme, editor, terminal, HTTP, Env, and Path preferences will return to defaults. Project and panel layout are not affected.'
          )
        ) {
          return;
        }
        const defaults = T.resetAllSettings(null);
        themeState = defaults.theme;
        editorPrefs = defaults.editorPrefs;
        terminalPrefs = defaults.terminalPrefs;
        httpPrefs = defaults.httpPrefs || Object.assign({}, T.DEFAULT_HTTP_PREFS);
        envPrefs =
          defaults.envPrefs ||
          Object.assign({}, T.DEFAULT_ENV_PREFS, { entries: [] });
        pathPrefs = defaults.pathPrefs || Object.assign({}, T.DEFAULT_PATH_PREFS);
        persistThemeAndApply();
        refreshThemeSelect();
        buildTokenCustomizeUI();
        syncEditorTerminalFormInputs();
        applyEditorPrefs();
        applyTerminalPrefs();
        applyHttpPrefs();
        refreshHttpPanel();
        refreshEnvPanel();
        refreshPathPanel();
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

    const httpCross = document.getElementById('settingsHttpAllowCrossOrigin');
    if (httpCross) {
      httpCross.checked = httpPrefs.allowCrossOrigin !== false;
      httpCross.addEventListener('change', function () {
        httpPrefs = T.saveHttpPrefs(null, { allowCrossOrigin: httpCross.checked });
        applyHttpPrefs();
      });
    }

    const envArgs = document.getElementById('settingsEnvProgramArgs');
    if (envArgs) {
      envArgs.addEventListener('change', function () {
        persistEnvPrefs({ programArgs: envArgs.value });
      });
    }
    const envAdd = document.getElementById('settingsEnvAddBtn');
    const envRemove = document.getElementById('settingsEnvRemoveBtn');
    function resetEnvEditor() {
      const keyEl = document.getElementById('settingsEnvKey');
      const valEl = document.getElementById('settingsEnvValue');
      const sensEl = document.getElementById('settingsEnvSensitive');
      const addBtn = document.getElementById('settingsEnvAddBtn');
      editingEnvKey = null;
      if (keyEl) keyEl.value = '';
      if (valEl) valEl.value = '';
      if (sensEl) sensEl.checked = false;
      if (addBtn) {
        addBtn.querySelector('span:last-child').textContent = 'Add variable';
        addBtn.querySelector('.fleditor-env-button-icon').textContent = '＋';
      }
    }
    if (envAdd) {
      envAdd.addEventListener('click', function () {
        const keyEl = document.getElementById('settingsEnvKey');
        const valEl = document.getElementById('settingsEnvValue');
        const sensEl = document.getElementById('settingsEnvSensitive');
        const key = keyEl ? keyEl.value.trim() : '';
        if (!key) {
          if (keyEl) keyEl.focus();
          return;
        }
        const sensitive = !!(sensEl && sensEl.checked);
        let value = valEl ? valEl.value : '';
        const entries = envPrefs.entries.slice();
        const originalIndex = editingEnvKey
          ? entries.findIndex(function (entry) { return entry.key === editingEnvKey; })
          : -1;
        if (originalIndex >= 0) {
          if (sensitive && value === '') value = entries[originalIndex].value;
          const duplicateIndex = entries.findIndex(function (entry) {
            return entry.key === key && entry.key !== editingEnvKey;
          });
          if (duplicateIndex >= 0) {
            entries[duplicateIndex] = { key: key, value: value, sensitive: sensitive };
            entries.splice(originalIndex, 1);
          } else {
            entries[originalIndex] = { key: key, value: value, sensitive: sensitive };
          }
        } else {
          const ix = entries.findIndex(function (entry) { return entry.key === key; });
          if (ix >= 0) {
            if (sensitive && value === '') value = entries[ix].value;
            entries[ix] = { key: key, value: value, sensitive: sensitive };
          } else {
            entries.push({ key: key, value: value, sensitive: sensitive });
          }
        }
        persistEnvPrefs({ entries: entries });
        resetEnvEditor();
        renderEnvList();
      });
    }
    if (envRemove) {
      envRemove.addEventListener('click', function () {
        const keyEl = document.getElementById('settingsEnvKey');
        const key = editingEnvKey || (keyEl ? keyEl.value.trim() : '');
        if (!key) return;
        persistEnvPrefs({
          entries: envPrefs.entries.filter(function (entry) {
            return entry.key !== key;
          }),
        });
        resetEnvEditor();
        renderEnvList();
      });
    }

    const pathSep = document.getElementById('settingsPathSep');
    if (pathSep) {
      pathSep.addEventListener('change', function () {
        const v = pathSep.value === 'backslash' ? '\\' : pathSep.value;
        pathPrefs = T.savePathPrefs(null, { sep: v });
      });
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
    getWasmMods = options.getWasmMods || null;

    themeState = T.loadThemeState();
    editorPrefs = T.loadEditorPrefs();
    terminalPrefs = T.loadTerminalPrefs();
    httpPrefs = T.loadHttpPrefs();
    envPrefs = T.loadEnvPrefs();
    pathPrefs = T.loadPathPrefs();
    applyHttpPrefs();

    if (appEl) {
      T.applyTheme(appEl, T.effectiveTokens(themeState));
    }

    initSettingsModal();
  }

  function getTerminalPrefs() {
    return terminalPrefs;
  }

  function getHttpPrefs() {
    return httpPrefs;
  }

  function getEnvPrefs() {
    return T.loadEnvPrefs();
  }

  function splitProgramArgs(s) {
    if (!s || typeof s !== 'string') {
      return [];
    }
    const out = [];
    const re = /"([^"\\]*(?:\\.[^"\\]*)*)"|'([^'\\]*(?:\\.[^'\\]*)*)'|(\S+)/g;
    let m;
    while ((m = re.exec(s)) !== null) {
      if (m[1] != null) {
        out.push(m[1].replace(/\\"/g, '"'));
      } else if (m[2] != null) {
        out.push(m[2].replace(/\\'/g, "'"));
      } else {
        out.push(m[3]);
      }
    }
    return out;
  }

  function getPathPrefs() {
    return T.loadPathPrefs();
  }

  /** Push Settings→Path sep into WASM before run. */
  function applyPathToWasm(Module) {
    if (!Module) {
      return;
    }
    const prefs = getPathPrefs();
    const sep = prefs.sep === '/' || prefs.sep === '\\' ? prefs.sep : 'auto';
    try {
      if (typeof Module._flat_wasm_path_sep_set === 'function') {
        Module.ccall('flat_wasm_path_sep_set', 'number', ['string'], [sep]);
      }
    } catch (_e) {
      /* older wasm */
    }
  }

  /** Push Settings→Env into WASM before flat_wasm_run*. Safe no-op on older builds. */
  function applyEnvToWasm(Module) {
    if (!Module) {
      return;
    }
    const prefs = getEnvPrefs();
    try {
      if (typeof Module._flat_wasm_env_clear === 'function') {
        Module._flat_wasm_env_clear();
      } else {
        return;
      }
    } catch (_e) {
      return;
    }
    const entries = prefs.entries || [];
    for (let i = 0; i < entries.length; i++) {
      const e = entries[i];
      if (!e || !e.key) {
        continue;
      }
      try {
        Module.ccall(
          'flat_wasm_env_set',
          'number',
          ['string', 'string'],
          [e.key, e.value != null ? String(e.value) : '']
        );
      } catch (_setErr) {
        /* ignore */
      }
    }
    try {
      if (typeof Module._flat_wasm_env_args_clear === 'function') {
        Module._flat_wasm_env_args_clear();
      }
    } catch (_clearArgs) {
      /* ignore */
    }
    const args = splitProgramArgs(prefs.programArgs || '');
    for (let j = 0; j < args.length; j++) {
      try {
        Module.ccall('flat_wasm_env_args_push', 'number', ['string'], [args[j]]);
      } catch (_pushErr) {
        /* ignore */
      }
    }
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
    getHttpPrefs,
    getEnvPrefs,
    getPathPrefs,
    applyEnvToWasm,
    applyPathToWasm,
    applyThemeFromStorage,
    applyEditorPrefs,
    applyTerminalPrefs,
    applyHttpPrefs,
    applyTerminalTheme,
    refreshAboutPanel,
    refreshHttpPanel,
    refreshEnvPanel,
    refreshPathPanel,
  };
})(typeof window !== 'undefined' ? window : globalThis);
