/* global FlatLModule, CodeMirror, Terminal, FleditorProject */
(function () {
  'use strict';

  const P = FleditorProject;

  const IO_OK = 0;
  const IO_PANIC_EOF = 2;
  const IO_PANIC_CANCEL = 3;

  const IO_PROMPT_MAX = 32;
  const IO_KIND_SUFFIX = {
    0: ' (ln)',
    1: ' (num)',
    2: ' (bool)',
    3: ' (list)',
    4: ' (obj)',
    5: ' (bools)',
  };

  const statusEl = document.getElementById('status');
  const bannerEl = document.getElementById('wasmBanner');
  const runBtn = document.getElementById('runBtn');
  const clearBtn = document.getElementById('clearBtn');
  const saveBtn = document.getElementById('saveBtn');
  const undoBtn = document.getElementById('undoBtn');
  const redoBtn = document.getElementById('redoBtn');
  const fileTreeEl = document.getElementById('fileTree');
  const editorPathLabel = document.getElementById('editorPathLabel');
  const dirtyBadge = document.getElementById('dirtyBadge');
  const newFileBtn = document.getElementById('newFileBtn');
  const newFolderBtn = document.getElementById('newFolderBtn');

  let wasmModule = null;
  let wasmLoadPromise = null;
  let running = false;
  let term = null;
  let cm = null;

  /** @type {ReturnType<typeof P.loadProject>} */
  let project = null;
  let dirty = false;
  let suppressChange = false;
  /** @type {null | (() => void)} */
  let unsavedContinue = null;

  let ioDialog = null;
  let ioForm = null;
  let ioTitle = null;
  let ioHint = null;
  let ioInput = null;
  let ioCancelBtn = null;
  /** @type {null | ((line: string, status: number) => void)} */
  let ioPendingWake = null;
  let ioAllowEmpty = false;
  let ioCharMode = false;

  let unsavedDialog = null;
  let unsavedContinueBtn = null;
  let unsavedCancelBtn = null;

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.classList.toggle('fleditor-status--error', !!isError);
  }

  function showBanner(msg) {
    bannerEl.hidden = !msg;
    bannerEl.textContent = msg || '';
  }

  function setDirty(value) {
    dirty = value;
    dirtyBadge.hidden = !dirty;
    saveBtn.disabled = !dirty;
    renderFileTree();
  }

  function currentOpenPath() {
    return P.normalizePath(project.openPath) || P.DEFAULT_MAIN;
  }

  function flushEditorToProject() {
    const path = currentOpenPath();
    project.files[path] = cm.getValue();
    project.openPath = path;
  }

  function persistProject() {
    P.saveProject(project);
  }

  function saveCurrentFile() {
    flushEditorToProject();
    persistProject();
    setDirty(false);
    setStatus('saved');
    window.setTimeout(function () {
      if (!running && statusEl.textContent === 'saved') {
        setStatus('idle');
      }
    }, 1200);
  }

  function implicitSaveForRun() {
    flushEditorToProject();
    persistProject();
    setDirty(false);
  }

  function loadFileIntoEditor(path) {
    const norm = P.normalizePath(path);
    suppressChange = true;
    cm.setValue(project.files[norm] != null ? project.files[norm] : '');
    cm.clearHistory();
    suppressChange = false;
    project.openPath = norm;
    editorPathLabel.textContent = norm;
    setDirty(false);
    renderFileTree();
  }

  function initUnsavedModal() {
    unsavedDialog = document.getElementById('unsavedModal');
    unsavedContinueBtn = document.getElementById('unsavedContinueBtn');
    unsavedCancelBtn = document.getElementById('unsavedCancelBtn');
    if (!unsavedDialog) {
      return;
    }
    unsavedContinueBtn.addEventListener('click', function () {
      unsavedDialog.close();
      const cont = unsavedContinue;
      unsavedContinue = null;
      if (cont) {
        cont();
      }
    });
    unsavedCancelBtn.addEventListener('click', function () {
      unsavedDialog.close();
      unsavedContinue = null;
    });
    unsavedDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      unsavedDialog.close();
      unsavedContinue = null;
    });
  }

  function confirmDiscardIfDirty(onContinue) {
    if (!dirty) {
      onContinue();
      return;
    }
    if (!unsavedDialog) {
      if (window.confirm('You have unsaved changes. Continue and lose them?')) {
        onContinue();
      }
      return;
    }
    unsavedContinue = onContinue;
    unsavedDialog.showModal();
  }

  function openFile(path) {
    const norm = P.normalizePath(path);
    if (!project.files[norm]) {
      return;
    }
    if (norm === currentOpenPath()) {
      return;
    }
    confirmDiscardIfDirty(function () {
      loadFileIntoEditor(norm);
    });
  }

  function renderFileTree() {
    if (!fileTreeEl) {
      return;
    }
    fileTreeEl.innerHTML = '';
    const open = currentOpenPath();

    function renderNodes(nodes, depth) {
      for (const node of nodes) {
        const row = document.createElement('div');
        row.className = 'fleditor-tree-row';
        row.style.paddingLeft = 0.5 + depth * 0.75 + 'rem';
        row.setAttribute('role', 'treeitem');
        if (node.kind === 'dir') {
          row.classList.add('fleditor-tree-row--dir');
          row.textContent = node.name + '/';
        } else {
          row.classList.add('fleditor-tree-row--file');
          if (node.path === open) {
            row.classList.add('fleditor-tree-row--active');
          }
          if (node.path === open && dirty) {
            row.classList.add('fleditor-tree-row--dirty');
          }
          row.textContent = node.name;
          row.tabIndex = 0;
          row.addEventListener('click', function () {
            openFile(node.path);
          });
          row.addEventListener('keydown', function (e) {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault();
              openFile(node.path);
            }
          });
        }
        fileTreeEl.appendChild(row);
        if (node.children && node.children.length) {
          renderNodes(node.children, depth + 1);
        }
      }
    }

    renderNodes(P.buildTree(project), 0);
  }

  function promptNewPath(label, defaultValue) {
    const raw = window.prompt(label, defaultValue || 'main.fl');
    if (raw == null) {
      return null;
    }
    const norm = P.normalizePath(raw);
    if (!norm) {
      window.alert('Invalid path.');
      return null;
    }
    if (!norm.endsWith('.fl')) {
      window.alert('File path must end with .fl');
      return null;
    }
    return norm;
  }

  function onNewFile() {
    const path = promptNewPath('New file path (e.g. lib/util.fl):', 'main.fl');
    if (!path) {
      return;
    }
    if (project.files[path]) {
      window.alert('File already exists.');
      return;
    }
    confirmDiscardIfDirty(function () {
      P.ensureParentDirs(project, path);
      project.files[path] = '#main\n  OK\n';
      persistProject();
      loadFileIntoEditor(path);
    });
  }

  function onNewFolder() {
    const raw = window.prompt('New folder path (e.g. lib):', 'lib');
    if (raw == null) {
      return;
    }
    const norm = P.normalizePath(raw);
    if (!norm) {
      window.alert('Invalid folder path.');
      return;
    }
    project.dirs = project.dirs || [];
    if (!project.dirs.includes(norm)) {
      project.dirs.push(norm);
      persistProject();
      renderFileTree();
    }
  }

  function updateUndoRedoButtons() {
    if (!cm || !undoBtn || !redoBtn) {
      return;
    }
    const hist = cm.historySize ? cm.historySize() : { undo: 0, redo: 0 };
    undoBtn.disabled = !hist.undo;
    redoBtn.disabled = !hist.redo;
  }

  function initIoModal() {
    ioDialog = document.getElementById('ioModal');
    ioForm = document.getElementById('ioModalForm');
    ioTitle = document.getElementById('ioModalTitle');
    ioHint = document.getElementById('ioModalHint');
    ioInput = document.getElementById('ioModalInput');
    ioCancelBtn = document.getElementById('ioModalCancel');
    if (!ioDialog || !ioForm || !ioInput) {
      return;
    }

    ioForm.addEventListener('submit', function (e) {
      e.preventDefault();
      submitIoModal();
    });
    ioCancelBtn.addEventListener('click', function () {
      dismissIoModal(null);
    });
    ioDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      dismissIoModal(null);
    });
  }

  function submitIoModal() {
    if (!ioPendingWake) {
      ioDialog.close();
      return;
    }
    const raw = ioInput.value;
    if (ioCharMode) {
      if (raw.length === 0) {
        dismissIoModal(null);
        return;
      }
      const ch = raw.charAt(0);
      if (term) term.write(ch);
      finishIoWake(ch, IO_OK);
      return;
    }
    dismissIoModal(raw);
  }

  function dismissIoModal(value) {
    if (!ioPendingWake) {
      if (ioDialog.open) ioDialog.close();
      return;
    }
    if (value === null) {
      finishIoWake('', IO_PANIC_EOF);
      return;
    }
    if (!ioAllowEmpty && value.trim() === '') {
      finishIoWake('', IO_PANIC_CANCEL);
      return;
    }
    if (term) term.write(value + '\r\n');
    finishIoWake(value, IO_OK);
  }

  function truncateIoPrompt(text) {
    const s = (text || '').trim();
    if (s.length <= IO_PROMPT_MAX) {
      return s;
    }
    return s.slice(0, IO_PROMPT_MAX) + '…';
  }

  function formatLineModalTitle(prompt, kind) {
    const suffix = IO_KIND_SUFFIX[kind] || ' (ln)';
    const p = truncateIoPrompt(prompt);
    if (!p) {
      return 'Input:' + suffix;
    }
    return p + suffix;
  }

  function formatCharModalTitle(prompt) {
    const p = truncateIoPrompt(prompt);
    if (!p) {
      return 'Input:';
    }
    return p;
  }

  function parseNeedLineHook(allowEmpty, a, b, c) {
    if (typeof a === 'function') {
      return { allowEmpty: allowEmpty, prompt: '', kind: 0, wake: a };
    }
    return { allowEmpty: allowEmpty, prompt: a || '', kind: b | 0, wake: c };
  }

  function parseNeedCharHook(a, b) {
    if (typeof a === 'function') {
      return { prompt: '', wake: a };
    }
    return { prompt: a || '', wake: b };
  }

  function finishIoWake(line, status) {
    const wake = ioPendingWake;
    ioPendingWake = null;
    ioCharMode = false;
    ioDialog.close();
    setStatus('running');
    if (wake) wake(line, status);
  }

  function openIoModal(options) {
    if (!ioDialog) {
      const line = window.prompt(options.title, '');
      if (line === null) {
        options.wake('', IO_PANIC_EOF);
      } else if (!options.allowEmpty && line.trim() === '') {
        options.wake('', IO_PANIC_CANCEL);
      } else {
        if (term && !options.charMode) term.write(line + '\r\n');
        else if (term && options.charMode && line.length) term.write(line.charAt(0));
        options.wake(options.charMode ? line.charAt(0) : line, IO_OK);
      }
      setStatus('running');
      return;
    }

    ioPendingWake = options.wake;
    ioAllowEmpty = !!options.allowEmpty;
    ioCharMode = !!options.charMode;
    ioTitle.textContent = options.title;
    if (ioHint) {
      ioHint.hidden = true;
      ioHint.textContent = '';
    }
    ioInput.value = '';
    ioInput.maxLength = options.charMode ? 8 : 8000;
    ioDialog.showModal();
    ioInput.focus();
    ioInput.select();
  }

  function initEditor() {
    project = P.loadProject();
    const open = currentOpenPath();
    document.getElementById('code').value = project.files[open] || P.DEFAULT_SOURCE;
    cm = CodeMirror.fromTextArea(document.getElementById('code'), {
      lineNumbers: true,
      theme: 'flatl-dark',
      mode: 'flatl',
      indentUnit: 2,
      tabSize: 2,
    });
    editorPathLabel.textContent = open;
    cm.on('change', function () {
      if (suppressChange) {
        return;
      }
      setDirty(true);
      updateUndoRedoButtons();
    });
    cm.on('keydown', function (_cm, e) {
      if ((e.ctrlKey || e.metaKey) && e.key === 's') {
        e.preventDefault();
        saveCurrentFile();
      }
    });
    updateUndoRedoButtons();
    renderFileTree();
    saveBtn.disabled = true;
  }

  function initTerminal() {
    term = new Terminal({
      theme: {
        background: '#1c1c22',
        foreground: '#e6dcc8',
        cursor: '#e6dcc8',
      },
      fontFamily: 'ui-monospace, Menlo, Consolas, monospace',
      fontSize: 14,
      cursorBlink: true,
      convertEol: true,
    });
    const host = document.getElementById('terminalHost');
    term.open(host);
    host.addEventListener('mousedown', function () {
      term.focus();
    });
  }

  function wasmModuleConfig() {
    return {
      locateFile: (p) => (p.endsWith('.wasm') ? 'wasm/' + p : p),
      flatWasmOnStdout: function (u8) {
        const text = new TextDecoder().decode(u8);
        if (term) term.write(text);
      },
      flatWasmOnNeedLine: function (allowEmpty, prompt, kind, wake) {
        const io = parseNeedLineHook(allowEmpty, prompt, kind, wake);
        setStatus(io.allowEmpty ? 'waiting input (line, empty ok)' : 'waiting input (line)');
        openIoModal({
          title: formatLineModalTitle(io.prompt, io.kind),
          allowEmpty: io.allowEmpty,
          charMode: false,
          wake: io.wake,
        });
      },
      flatWasmOnNeedChar: function (prompt, wake) {
        const io = parseNeedCharHook(prompt, wake);
        setStatus('waiting input (char)');
        openIoModal({
          title: formatCharModalTitle(io.prompt),
          allowEmpty: false,
          charMode: true,
          wake: io.wake,
        });
      },
    };
  }

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = src;
      s.onload = () => resolve();
      s.onerror = () => reject(new Error('failed to load ' + src));
      document.body.appendChild(s);
    });
  }

  function attachWasmHooks(Module) {
    const cfg = wasmModuleConfig();
    Module.flatWasmOnStdout = cfg.flatWasmOnStdout;
    Module.flatWasmOnNeedLine = cfg.flatWasmOnNeedLine;
    Module.flatWasmOnNeedChar = cfg.flatWasmOnNeedChar;
    Module.locateFile = cfg.locateFile;
    return Module;
  }

  async function loadWasm() {
    if (wasmLoadPromise) {
      return wasmLoadPromise;
    }
    wasmLoadPromise = (async () => {
      if (typeof FlatLModule !== 'function') {
        try {
          await loadScript('wasm/flatl.js');
        } catch (_e) {
          showBanner('WASM not built. Run: make -f Makefile.wasm fleditor-wasm');
          return null;
        }
      }
      if (typeof FlatLModule !== 'function') {
        showBanner('WASM not built. Run: make -f Makefile.wasm fleditor-wasm');
        return null;
      }
      try {
        const cfg = wasmModuleConfig();
        const Module = await FlatLModule(cfg);
        Module.noExitRuntime = true;
        return attachWasmHooks(Module);
      } catch (e) {
        showBanner('WASM init failed: ' + e.message);
        wasmLoadPromise = null;
        return null;
      }
    })();
    return wasmLoadPromise;
  }

  function isCompileErrorMessage(msg) {
    if (!msg) {
      return false;
    }
    return (
      msg.startsWith('flc:') ||
      msg.includes('use:') ||
      msg.includes('parse') ||
      msg.includes('link') ||
      msg.includes('compile') ||
      msg.includes('cannot open') ||
      msg.includes('cannot read')
    );
  }

  async function runProjectEntry(Module, entryPath) {
    const errBuf = Module._malloc(4096);
    Module.setValue(errBuf, 0, 'i8');
    attachWasmHooks(Module);
    setStatus('running');
    const memPath = P.memfsEntryPath(entryPath);
    let runRc = Module.ccall(
      'flat_wasm_run_entry',
      'number',
      ['string', 'number', 'number'],
      [memPath, errBuf, 4096],
      { async: true }
    );
    if (runRc != null && typeof runRc.then === 'function') {
      runRc = await runRc;
    }

    if (runRc !== 0) {
      const msg = Module.UTF8ToString(errBuf);
      if (isCompileErrorMessage(msg)) {
        term.write('\r\n[compile/link error] ' + msg + '\r\n');
      } else if (msg) {
        term.write('\r\n[run] ' + msg + '\r\n');
      } else {
        term.write('\r\n[error] program failed (exit ' + runRc + ')\r\n');
      }
    }
    Module._free(errBuf);
  }

  async function onRun() {
    if (running) return;
    if (ioDialog && ioDialog.open) {
      return;
    }
    if (!wasmModule) {
      wasmModule = await loadWasm();
      if (!wasmModule) return;
    }
    running = true;
    runBtn.disabled = true;
    setStatus('compiling');
    term.clear();
    try {
      implicitSaveForRun();
      P.syncToMemfs(wasmModule, project.files);
      await runProjectEntry(wasmModule, currentOpenPath());
    } catch (e) {
      term.write('\r\n[error] ' + e.message + '\r\n');
      setStatus('failed', true);
    } finally {
      running = false;
      runBtn.disabled = false;
      ioPendingWake = null;
      if (!ioDialog || !ioDialog.open) {
        setStatus('idle');
      }
    }
  }

  function boot() {
    initIoModal();
    initUnsavedModal();
    initEditor();
    initTerminal();
    clearBtn.addEventListener('click', () => term.clear());
    runBtn.addEventListener('click', () => onRun());
    saveBtn.addEventListener('click', () => saveCurrentFile());
    undoBtn.addEventListener('click', () => {
      cm.undo();
      updateUndoRedoButtons();
    });
    redoBtn.addEventListener('click', () => {
      cm.redo();
      updateUndoRedoButtons();
    });
    newFileBtn.addEventListener('click', onNewFile);
    newFolderBtn.addEventListener('click', onNewFolder);
    loadWasm().then((m) => {
      wasmModule = m;
      if (m) showBanner('');
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
