/* global FlatLModule, CodeMirror, Terminal, FleditorProject, FleditorLayout */
(function () {
  'use strict';

  const P = FleditorProject;
  const L = FleditorLayout;

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
  const projectPanel = document.getElementById('projectPanel');
  const terminalPanel = document.getElementById('terminalPanel');
  const workPanel = document.getElementById('workPanel');
  const chipProject = document.getElementById('chipProject');
  const chipTerminal = document.getElementById('chipTerminal');
  const projectPanelToggle = document.getElementById('projectPanelToggle');
  const terminalPanelToggle = document.getElementById('terminalPanelToggle');
  const resetLayoutBtn = document.getElementById('resetLayoutBtn');
  const resetProjectBtn = document.getElementById('resetProjectBtn');
  const editorEmptyOverlay = document.getElementById('editorEmptyOverlay');

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

  let deleteDialog = null;
  let deleteModalMessage = null;
  let deleteConfirmBtn = null;
  /** @type {null | (() => void)} */
  let deleteContinue = null;

  let resetProjectDialog = null;
  let resetProjectConfirmBtn = null;
  let resetProjectCancelBtn = null;

  let promptDialog = null;
  let promptForm = null;
  let promptTitle = null;
  let promptHint = null;
  let promptInput = null;
  /** @type {null | ((value: string | null) => void)} */
  let promptResolve = null;

  let alertDialog = null;
  let alertTitle = null;
  let alertMessage = null;
  /** @type {null | (() => void)} */
  let alertResolve = null;

  /** @type {ReturnType<typeof L.loadLayout>} */
  let layoutState = L.loadLayout();

  const TREE_FOLDER_STATE_KEY = 'flatl-fleditor-tree-folders';
  /** @type {Map<string, boolean>} explicit expand/collapse; missing = default */
  let treeFolderState = new Map();

  function loadTreeFolderState() {
    try {
      const raw = localStorage.getItem(TREE_FOLDER_STATE_KEY);
      if (!raw) {
        return new Map();
      }
      const data = JSON.parse(raw);
      if (!data || typeof data !== 'object') {
        return new Map();
      }
      const map = new Map();
      for (const k of Object.keys(data)) {
        map.set(k, !!data[k]);
      }
      return map;
    } catch (_e) {
      return new Map();
    }
  }

  function saveTreeFolderState() {
    const obj = {};
    treeFolderState.forEach(function (v, k) {
      obj[k] = v;
    });
    localStorage.setItem(TREE_FOLDER_STATE_KEY, JSON.stringify(obj));
  }

  function ancestorDirPaths(filePath) {
    const norm = P.normalizePath(filePath);
    if (!norm) {
      return [];
    }
    const parts = norm.split('/');
    parts.pop();
    const out = [];
    let acc = '';
    for (const p of parts) {
      acc = acc ? acc + '/' + p : p;
      out.push(acc);
    }
    return out;
  }

  function folderIsExpanded(dirPath, openPath) {
    if (treeFolderState.has(dirPath)) {
      return treeFolderState.get(dirPath);
    }
    if (openPath && ancestorDirPaths(openPath).indexOf(dirPath) >= 0) {
      return true;
    }
    return false;
  }

  function toggleFolderExpanded(dirPath) {
    const openPath = currentOpenPath();
    const currently = folderIsExpanded(dirPath, openPath);
    treeFolderState.set(dirPath, !currently);
    saveTreeFolderState();
    renderFileTree();
  }

  treeFolderState = loadTreeFolderState();

  function persistLayout() {
    layoutState = L.saveLayout(null, layoutState);
  }

  function applyLayout() {
    if (projectPanel) {
      projectPanel.hidden = !layoutState.projectOpen;
    }
    if (chipProject) {
      chipProject.classList.toggle('fleditor-chip--on', layoutState.projectOpen);
      chipProject.setAttribute('aria-pressed', layoutState.projectOpen ? 'true' : 'false');
    }
    if (terminalPanel && workPanel) {
      terminalPanel.hidden = !layoutState.terminalOpen;
      workPanel.classList.toggle('fleditor-work--no-terminal', !layoutState.terminalOpen);
      if (layoutState.terminalOpen) {
        const pct = Math.round(layoutState.terminalFraction * 1000) / 10;
        terminalPanel.style.flexBasis = pct + '%';
      } else {
        terminalPanel.style.flexBasis = '';
      }
    }
    if (chipTerminal) {
      chipTerminal.classList.toggle('fleditor-chip--on', layoutState.terminalOpen);
      chipTerminal.setAttribute('aria-pressed', layoutState.terminalOpen ? 'true' : 'false');
    }
    scheduleLayoutRefresh();
  }

  function resizeTerminalToHost() {
    if (!term || !terminalPanel || terminalPanel.hidden) {
      return;
    }
    const host = document.getElementById('terminalHost');
    if (!host) {
      return;
    }
    const rect = host.getBoundingClientRect();
    const w = Math.floor(rect.width);
    const h = Math.floor(rect.height);
    if (w < 8 || h < 8) {
      return;
    }
    const fontSize = term.options.fontSize || 14;
    const cellW = fontSize * 0.62;
    const cellH = fontSize + 3;
    const cols = Math.max(2, Math.floor(w / cellW));
    const rows = Math.max(1, Math.floor(h / cellH));
    term.resize(cols, rows);
  }

  function scheduleLayoutRefresh() {
    window.requestAnimationFrame(function () {
      if (cm) {
        cm.refresh();
      }
      resizeTerminalToHost();
    });
  }

  function setProjectOpen(open) {
    layoutState.projectOpen = !!open;
    persistLayout();
    applyLayout();
  }

  function setTerminalOpen(open) {
    layoutState.terminalOpen = !!open;
    persistLayout();
    applyLayout();
  }

  function toggleProjectPanel() {
    setProjectOpen(!layoutState.projectOpen);
  }

  function toggleTerminalPanel() {
    setTerminalOpen(!layoutState.terminalOpen);
  }

  function resetLayout() {
    layoutState = L.resetLayout();
    applyLayout();
  }

  function writeTerminalEpilog(line) {
    if (!term) {
      return;
    }
    term.write('\r\n\x1b[90m' + line + '\x1b[0m\r\n');
  }

  const FLAT_TERM = {
    UNKNOWN: 0,
    OK: 1,
    COMPLETE: 2,
    FAIL: 3,
    PANIC: 4,
    ERROR: 5
  };

  function runFinishedEpilog(exitCode, termReason) {
    if (termReason === FLAT_TERM.OK) {
      return '-- finished: OK';
    }
    if (termReason === FLAT_TERM.FAIL || exitCode === 2) {
      return '-- finished: FAIL';
    }
    if (termReason === FLAT_TERM.COMPLETE) {
      return '-- finished: complete';
    }
    if (termReason === FLAT_TERM.PANIC) {
      return '-- finished: panic';
    }
    if (exitCode !== 0) {
      return '-- finished: error';
    }
    return '-- finished: complete';
  }

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.classList.toggle('fleditor-status--error', !!isError);
  }

  function showBanner(msg) {
    bannerEl.hidden = !msg;
    bannerEl.textContent = msg || '';
  }

  function hasOpenFile() {
    const p = project.openPath != null ? P.normalizePath(project.openPath) : '';
    return !!(p && project.files[p] != null);
  }

  function currentOpenPath() {
    if (!hasOpenFile()) {
      return null;
    }
    return P.normalizePath(project.openPath);
  }

  function pickFallbackOpenPath(excludePath) {
    const paths = P.listFilePaths(project).filter(function (p) {
      return p !== excludePath;
    });
    if (paths.includes(P.DEFAULT_MAIN)) {
      return P.DEFAULT_MAIN;
    }
    return paths[0] || null;
  }

  function updateEditorHeader() {
    if (!editorPathLabel) {
      return;
    }
    if (!hasOpenFile()) {
      editorPathLabel.textContent = 'No file open';
      return;
    }
    const path = currentOpenPath();
    editorPathLabel.textContent = dirty ? '(not saved) ' + path : path;
  }

  function updateEditorEmptyState() {
    const noFiles = P.listFilePaths(project).length === 0;
    if (editorEmptyOverlay) {
      editorEmptyOverlay.hidden = !noFiles;
    }
    if (cm) {
      const hideEditor = noFiles || !hasOpenFile();
      cm.setOption('readOnly', hideEditor);
      const wrap = cm.getWrapperElement();
      if (wrap) {
        wrap.style.visibility = hideEditor ? 'hidden' : 'visible';
      }
    }
    updateRunSaveButtons();
  }

  function updateRunSaveButtons() {
    const open = hasOpenFile();
    if (!running) {
      runBtn.disabled = !open;
    }
    saveBtn.disabled = !open || !dirty;
  }

  function setDirty(value) {
    dirty = value;
    if (dirtyBadge) {
      dirtyBadge.hidden = true;
    }
    updateEditorHeader();
    renderFileTree();
    updateRunSaveButtons();
  }

  function flushEditorToProject() {
    if (!hasOpenFile()) {
      return;
    }
    const path = currentOpenPath();
    project.files[path] = cm.getValue();
    project.openPath = path;
  }

  function persistProject() {
    P.saveProject(project);
  }

  function saveCurrentFile() {
    if (!hasOpenFile()) {
      return;
    }
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

  function enterEmptyEditorState() {
    project.openPath = null;
    suppressChange = true;
    cm.setValue('');
    cm.clearHistory();
    suppressChange = false;
    dirty = false;
    updateEditorHeader();
    updateEditorEmptyState();
    renderFileTree();
  }

  function loadFileIntoEditor(path) {
    const norm = P.normalizePath(path);
    if (!project.files[norm]) {
      return;
    }
    suppressChange = true;
    cm.setValue(project.files[norm]);
    cm.clearHistory();
    suppressChange = false;
    project.openPath = norm;
    setDirty(false);
    updateEditorEmptyState();
  }

  function afterOpenFileChange(deletedPath) {
    const open = currentOpenPath();
    if (open && project.files[open]) {
      renderFileTree();
      return;
    }
    const next = pickFallbackOpenPath(deletedPath || null);
    if (next) {
      loadFileIntoEditor(next);
    } else {
      enterEmptyEditorState();
    }
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
      onContinue();
      return;
    }
    unsavedContinue = onContinue;
    unsavedDialog.showModal();
  }

  function initPromptModal() {
    promptDialog = document.getElementById('promptModal');
    promptForm = document.getElementById('promptModalForm');
    promptTitle = document.getElementById('promptModalTitle');
    promptHint = document.getElementById('promptModalHint');
    promptInput = document.getElementById('promptModalInput');
    const promptCancel = document.getElementById('promptModalCancel');
    if (!promptDialog || !promptForm || !promptInput) {
      return;
    }
    promptForm.addEventListener('submit', function (e) {
      e.preventDefault();
      const value = promptInput.value;
      promptDialog.close();
      const resolve = promptResolve;
      promptResolve = null;
      if (resolve) {
        resolve(value);
      }
    });
    promptCancel.addEventListener('click', function () {
      promptDialog.close();
      const resolve = promptResolve;
      promptResolve = null;
      if (resolve) {
        resolve(null);
      }
    });
    promptDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      promptDialog.close();
      const resolve = promptResolve;
      promptResolve = null;
      if (resolve) {
        resolve(null);
      }
    });
  }

  function initAlertModal() {
    alertDialog = document.getElementById('alertModal');
    alertTitle = document.getElementById('alertModalTitle');
    alertMessage = document.getElementById('alertModalMessage');
    const alertOk = document.getElementById('alertModalOk');
    if (!alertDialog || !alertOk) {
      return;
    }
    alertOk.addEventListener('click', function () {
      alertDialog.close();
      const resolve = alertResolve;
      alertResolve = null;
      if (resolve) {
        resolve();
      }
    });
    alertDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      alertDialog.close();
      const resolve = alertResolve;
      alertResolve = null;
      if (resolve) {
        resolve();
      }
    });
  }

  function showPrompt(options) {
    return new Promise(function (resolve) {
      if (!promptDialog) {
        resolve(null);
        return;
      }
      promptTitle.textContent = options.title || 'Input';
      if (promptHint) {
        const msg = options.message || '';
        promptHint.textContent = msg;
        promptHint.hidden = !msg;
      }
      promptInput.value = options.defaultValue != null ? options.defaultValue : '';
      promptResolve = resolve;
      promptDialog.showModal();
      promptInput.focus();
      promptInput.select();
    });
  }

  function showAlert(message, title) {
    return new Promise(function (resolve) {
      if (!alertDialog) {
        resolve();
        return;
      }
      if (alertTitle) {
        alertTitle.textContent = title || 'Notice';
      }
      if (alertMessage) {
        alertMessage.textContent = message;
      }
      alertResolve = resolve;
      alertDialog.showModal();
    });
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

    function addDeleteButton(row, title, onDelete) {
      const del = document.createElement('button');
      del.type = 'button';
      del.className = 'fleditor-tree-delete';
      del.textContent = '×';
      del.title = title;
      del.setAttribute('aria-label', title);
      del.addEventListener('click', function (e) {
        e.stopPropagation();
        onDelete();
      });
      row.appendChild(del);
    }

    function renderNodes(nodes, depth) {
      for (const node of nodes) {
        const row = document.createElement('div');
        row.className = 'fleditor-tree-row';
        row.style.paddingLeft = 0.5 + depth * 0.75 + 'rem';
        row.setAttribute('role', 'treeitem');
        const label = document.createElement('span');
        label.className = 'fleditor-tree-row-label';
        if (node.kind === 'dir') {
          row.classList.add('fleditor-tree-row--dir');
          const hasChildren = node.children && node.children.length > 0;
          if (hasChildren) {
            const expanded = folderIsExpanded(node.path, open);
            const toggle = document.createElement('button');
            toggle.type = 'button';
            toggle.className = 'fleditor-tree-toggle';
            toggle.textContent = expanded ? '▾' : '▸';
            toggle.title = expanded ? 'Collapse folder' : 'Expand folder';
            toggle.setAttribute('aria-label', toggle.title);
            toggle.setAttribute('aria-expanded', expanded ? 'true' : 'false');
            toggle.addEventListener('click', function (e) {
              e.stopPropagation();
              toggleFolderExpanded(node.path);
            });
            row.appendChild(toggle);
          } else {
            const spacer = document.createElement('span');
            spacer.className = 'fleditor-tree-toggle-spacer';
            spacer.setAttribute('aria-hidden', 'true');
            row.appendChild(spacer);
          }
          label.textContent = node.name + '/';
          label.addEventListener('click', function () {
            if (hasChildren) {
              toggleFolderExpanded(node.path);
            }
          });
          row.appendChild(label);
          addDeleteButton(row, 'Delete folder', function () {
            requestDeleteFolder(node.path);
          });
        } else {
          row.classList.add('fleditor-tree-row--file');
          const spacer = document.createElement('span');
          spacer.className = 'fleditor-tree-toggle-spacer';
          spacer.setAttribute('aria-hidden', 'true');
          row.appendChild(spacer);
          if (node.path === open) {
            row.classList.add('fleditor-tree-row--active');
          }
          if (node.path === open && dirty) {
            row.classList.add('fleditor-tree-row--dirty');
          }
          label.textContent = node.name;
          label.tabIndex = 0;
          label.addEventListener('click', function () {
            openFile(node.path);
          });
          label.addEventListener('keydown', function (e) {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault();
              openFile(node.path);
            }
          });
          row.appendChild(label);
          addDeleteButton(row, 'Delete file', function () {
            requestDeleteFile(node.path);
          });
        }
        fileTreeEl.appendChild(row);
        if (node.kind === 'dir' && node.children && node.children.length) {
          if (folderIsExpanded(node.path, open)) {
            renderNodes(node.children, depth + 1);
          }
        }
      }
    }

    renderNodes(P.buildTree(project), 0);
  }

  function initDeleteModal() {
    deleteDialog = document.getElementById('deleteModal');
    deleteModalMessage = document.getElementById('deleteModalMessage');
    deleteConfirmBtn = document.getElementById('deleteConfirmBtn');
    const deleteCancelBtn = document.getElementById('deleteCancelBtn');
    if (!deleteDialog || !deleteConfirmBtn) {
      return;
    }
    deleteConfirmBtn.addEventListener('click', function () {
      deleteDialog.close();
      const cont = deleteContinue;
      deleteContinue = null;
      if (cont) {
        cont();
      }
    });
    deleteCancelBtn.addEventListener('click', function () {
      deleteDialog.close();
      deleteContinue = null;
    });
    deleteDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      deleteDialog.close();
      deleteContinue = null;
    });
  }

  function confirmDelete(message, onConfirm) {
    deleteModalMessage.textContent = message;
    deleteContinue = onConfirm;
    deleteDialog.showModal();
  }

  function performDeleteFile(path) {
    const norm = P.normalizePath(path);
    confirmDelete('Delete `' + norm + '`? This cannot be undone.', function () {
      const wasOpen = currentOpenPath() === norm;
      P.removeFile(project, norm);
      persistProject();
      if (wasOpen) {
        afterOpenFileChange(norm);
      } else {
        renderFileTree();
      }
    });
  }

  function requestDeleteFile(path) {
    const norm = P.normalizePath(path);
    function go() {
      performDeleteFile(norm);
    }
    if (norm === currentOpenPath() && dirty) {
      confirmDiscardIfDirty(go);
    } else {
      go();
    }
  }

  function performDeleteFolder(dirPath) {
    const norm = P.normalizePath(dirPath);
    const files = P.filesUnderFolder(project, norm);
    if (files.length === 0) {
      confirmDelete('Delete folder `' + norm + '`?', function () {
        P.removeEmptyFolder(project, norm);
        persistProject();
        renderFileTree();
      });
      return;
    }
    const open = currentOpenPath();
    const openInFolder = open && (open.startsWith(norm + '/') || files.includes(open));
    confirmDelete(
      'Delete folder `' + norm + '` and ' + files.length + ' file(s)? This cannot be undone.',
      function () {
        P.removeFolder(project, norm);
        persistProject();
        if (openInFolder) {
          afterOpenFileChange(open);
        } else {
          renderFileTree();
        }
      }
    );
  }

  function requestDeleteFolder(dirPath) {
    const norm = P.normalizePath(dirPath);
    const files = P.filesUnderFolder(project, norm);
    const open = currentOpenPath();
    const openInFolder = open && files.includes(open);
    function go() {
      performDeleteFolder(norm);
    }
    if (openInFolder && dirty) {
      confirmDiscardIfDirty(go);
    } else {
      go();
    }
  }

  function initResetProjectModal() {
    resetProjectDialog = document.getElementById('resetProjectModal');
    resetProjectConfirmBtn = document.getElementById('resetProjectConfirmBtn');
    resetProjectCancelBtn = document.getElementById('resetProjectCancelBtn');
    if (!resetProjectDialog || !resetProjectConfirmBtn) {
      return;
    }
    resetProjectConfirmBtn.addEventListener('click', async function () {
      resetProjectDialog.close();
      project = await P.resetProjectWithExamples();
      loadFileIntoEditor(P.DEFAULT_MAIN);
      renderFileTree();
      updateEditorEmptyState();
    });
    resetProjectCancelBtn.addEventListener('click', function () {
      resetProjectDialog.close();
    });
    resetProjectDialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      resetProjectDialog.close();
    });
  }

  function onResetProjectClick() {
    confirmDiscardIfDirty(function () {
      resetProjectDialog.showModal();
    });
  }

  async function promptNewFilePath() {
    const raw = await showPrompt({
      title: 'New file',
      message: 'Path (e.g. lib/util.fl):',
      defaultValue: 'main.fl',
    });
    if (raw == null) {
      return null;
    }
    const norm = P.normalizePath(raw);
    if (!norm) {
      await showAlert('Invalid path.', 'New file');
      return null;
    }
    if (!norm.endsWith('.fl')) {
      await showAlert('File path must end with .fl', 'New file');
      return null;
    }
    return norm;
  }

  async function onNewFile() {
    const path = await promptNewFilePath();
    if (!path) {
      return;
    }
    if (project.files[path]) {
      await showAlert('File already exists.', 'New file');
      return;
    }
    confirmDiscardIfDirty(function () {
      P.ensureParentDirs(project, path);
      project.files[path] = '#main\n  OK\n';
      persistProject();
      loadFileIntoEditor(path);
    });
  }

  async function onNewFolder() {
    const raw = await showPrompt({
      title: 'New folder',
      message: 'Folder path (e.g. lib):',
      defaultValue: 'lib',
    });
    if (raw == null) {
      return;
    }
    const norm = P.normalizePath(raw);
    if (!norm) {
      await showAlert('Invalid folder path.', 'New folder');
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
      options.wake('', IO_PANIC_EOF);
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

  async function initEditor() {
    const hadProject = !!localStorage.getItem(P.PROJECT_KEY);
    const hadLegacy = !!localStorage.getItem(P.LEGACY_KEY);
    project = P.loadProject();
    if (!hadProject && !hadLegacy) {
      await P.hydrateProjectExamples(project);
      P.saveProject(project);
    }
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
    updateEditorHeader();
    updateEditorEmptyState();
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
    scheduleLayoutRefresh();
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

    let compileFailed = false;
    if (runRc !== 0) {
      const msg = Module.UTF8ToString(errBuf);
      if (isCompileErrorMessage(msg)) {
        compileFailed = true;
        term.write('\r\n[compile/link error] ' + msg + '\r\n');
      } else if (msg) {
        term.write('\r\n[run] ' + msg + '\r\n');
      } else {
        term.write('\r\n[error] program failed (exit ' + runRc + ')\r\n');
      }
    }
    let termination = FLAT_TERM.UNKNOWN;
    try {
      termination = Module.ccall('flat_wasm_get_last_termination', 'number', [], []);
    } catch (_e) {
      /* older wasm without export */
    }
    Module._free(errBuf);
    return { exitCode: runRc, compileFailed: compileFailed, termination: termination };
  }

  async function onRun() {
    if (running) return;
    if (!hasOpenFile()) {
      return;
    }
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
    setTerminalOpen(true);
    scheduleLayoutRefresh();
    term.clear();
    let runOutcome = { exitCode: 1, compileFailed: false, termination: FLAT_TERM.ERROR };
    try {
      implicitSaveForRun();
      P.syncToMemfs(wasmModule, project.files);
      runOutcome = await runProjectEntry(wasmModule, currentOpenPath());
    } catch (e) {
      term.write('\r\n[error] ' + e.message + '\r\n');
      runOutcome = { exitCode: 1, compileFailed: false, termination: FLAT_TERM.ERROR };
      setStatus('failed', true);
    } finally {
      if (runOutcome.compileFailed) {
        writeTerminalEpilog('-- compile failed');
      } else {
        writeTerminalEpilog(
          runFinishedEpilog(runOutcome.exitCode, runOutcome.termination)
        );
      }
      running = false;
      updateRunSaveButtons();
      ioPendingWake = null;
      if (!ioDialog || !ioDialog.open) {
        setStatus('idle');
      }
    }
  }

  function initLayoutControls() {
    applyLayout();
    if (chipProject) {
      chipProject.addEventListener('click', toggleProjectPanel);
    }
    if (chipTerminal) {
      chipTerminal.addEventListener('click', toggleTerminalPanel);
    }
    if (projectPanelToggle) {
      projectPanelToggle.addEventListener('click', () => setProjectOpen(false));
    }
    if (terminalPanelToggle) {
      terminalPanelToggle.addEventListener('click', () => setTerminalOpen(false));
    }
    if (resetLayoutBtn) {
      resetLayoutBtn.addEventListener('click', resetLayout);
    }
  }

  async function boot() {
    initIoModal();
    initUnsavedModal();
    initDeleteModal();
    initResetProjectModal();
    initPromptModal();
    initAlertModal();
    await initEditor();
    initTerminal();
    initLayoutControls();
    window.addEventListener('resize', scheduleLayoutRefresh);
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
    if (resetProjectBtn) {
      resetProjectBtn.addEventListener('click', onResetProjectClick);
    }
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
