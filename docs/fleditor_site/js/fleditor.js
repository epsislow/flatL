/* global FlatLModule, CodeMirror, Terminal, FleditorProject, FleditorLayout, FleditorHex, FleditorHexGrid, FleditorTextFormat, FleditorCsvParse, FleditorCsvGrid, FleditorJsonlParse, FleditorJsonlAdvFilter, FleditorJsonlUi, FleditorTheme, FleditorSettings, FleditorBlockAnalyzer, FleditorCodeMap */
(function () {
  'use strict';

  const P = FleditorProject;
  const L = FleditorLayout;
  const H = FleditorHex;
  const G = FleditorHexGrid;
  const CsvP = FleditorCsvParse;
  const CsvG = FleditorCsvGrid;
  const Jp = FleditorJsonlParse;
  const Af = typeof FleditorJsonlAdvFilter !== 'undefined' ? FleditorJsonlAdvFilter : null;
  const Ju = FleditorJsonlUi;
  const Txt = FleditorTextFormat;

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
  const codeMapBtn = document.getElementById('codeMapBtn');
  const csvViewToggleBtn = document.getElementById('csvViewToggleBtn');
  const jsonlViewToggleBtn = document.getElementById('jsonlViewToggleBtn');
  const jsonlBar = document.getElementById('jsonlBar');
  const jsonlRecordsHost = document.getElementById('jsonlRecordsHost');
  const jsonlListHost = document.getElementById('jsonlListHost');
  const jsonlLockPanel = document.getElementById('jsonlLockPanel');
  const jsonlFreePanel = document.getElementById('jsonlFreePanel');
  const jsonlFreeTa = document.getElementById('jsonlFreeTa');
  const jsonlFreeHint = document.getElementById('jsonlFreeHint');
  const jsonlFreeErr = document.getElementById('jsonlFreeErr');
  const jsonlLineLabel = document.getElementById('jsonlLineLabel');
  const jsonlFreeBtn = document.getElementById('jsonlFreeBtn');
  const jsonlLockBtn = document.getElementById('jsonlLockBtn');
  const jsonlApplyBtn = document.getElementById('jsonlApplyBtn');
  const jsonlRevertBtn = document.getElementById('jsonlRevertBtn');
  const jsonlWordWrapBtn = document.getElementById('jsonlWordWrapBtn');
  const jsonlPageLabel = document.getElementById('jsonlPageLabel');
  const jsonlPageInput = document.getElementById('jsonlPageInput');
  const jsonlPageTotal = document.getElementById('jsonlPageTotal');
  const jsonlFindModeBtn = document.getElementById('jsonlFindModeBtn');
  const jsonlSearchInput = document.getElementById('jsonlSearchInput');
  const jsonlSearchCount = document.getElementById('jsonlSearchCount');
  const jsonlAdvFilterErr = document.getElementById('jsonlAdvFilterErr');
  const jsonlHelpBanner = document.getElementById('jsonlHelpBanner');
  const jsonlHelpLabel = document.getElementById('jsonlHelpLabel');
  const jsonlTooLargeOpenRecords = document.getElementById('jsonlTooLargeOpenRecords');
  const csvTableBar = document.getElementById('csvTableBar');
  const csvGridHost = document.getElementById('csvGridHost');
  const csvTableSizeLabel = document.getElementById('csvTableSizeLabel');
  const csvSelectionStats = document.getElementById('csvSelectionStats');
  const csvFormatDelimiterSelect = document.getElementById('csvFormatDelimiterSelect');
  const csvFormatQuoteSelect = document.getElementById('csvFormatQuoteSelect');
  const csvFormatModal = document.getElementById('csvFormatModal');
  const csvFormatModalForm = document.getElementById('csvFormatModalForm');
  const csvFormatEncodingSelect = document.getElementById('csvFormatEncodingSelect');
  const csvFormatLineEndingSelect = document.getElementById('csvFormatLineEndingSelect');
  const csvFormatAutoDelimiterBtn = document.getElementById('csvFormatAutoDelimiterBtn');
  const csvFormatQuoteAllBtn = document.getElementById('csvFormatQuoteAllBtn');
  const textFormatBtn = document.getElementById('textFormatBtn');
  const textFormatModal = document.getElementById('textFormatModal');
  const textFormatModalForm = document.getElementById('textFormatModalForm');
  const textFormatLineEndingSelect = document.getElementById('textFormatLineEndingSelect');
  const textFormatEncodingSelect = document.getElementById('textFormatEncodingSelect');
  const csvTableSizeModal = document.getElementById('csvTableSizeModal');
  const csvTableSizeModalForm = document.getElementById('csvTableSizeModalForm');
  const csvTableSizeRowsInput = document.getElementById('csvTableSizeRowsInput');
  const csvTableSizeColsInput = document.getElementById('csvTableSizeColsInput');
  const csvHeaderRowBtn = document.getElementById('csvHeaderRowBtn');
  const csvTableSizeBtn = document.getElementById('csvTableSizeBtn');
  const csvFormatBtn = document.getElementById('csvFormatBtn');
  const csvGoCell = document.getElementById('csvGoCell');
  const csvGoBtn = document.getElementById('csvGoBtn');
  const csvSelectExtendBtn = document.getElementById('csvSelectExtendBtn');
  const csvCopyBtn = document.getElementById('csvCopyBtn');
  const csvPasteBtn = document.getElementById('csvPasteBtn');
  const csvPasteModal = document.getElementById('csvPasteModal');
  const csvPasteModalForm = document.getElementById('csvPasteModalForm');
  const csvPasteModalInput = document.getElementById('csvPasteModalInput');
  const csvPasteModalCancel = document.getElementById('csvPasteModalCancel');
  const csvSearchInput = document.getElementById('csvSearchInput');
  const csvSearchCount = document.getElementById('csvSearchCount');
  const csvSearchPrev = document.getElementById('csvSearchPrev');
  const csvSearchNext = document.getElementById('csvSearchNext');
  const csvHelpBanner = document.getElementById('csvHelpBanner');
  const csvHelpLabel = document.getElementById('csvHelpLabel');
  const csvTooLargeOpenTable = document.getElementById('csvTooLargeOpenTable');
  const codeMapView = document.getElementById('codeMapView');
  const clearBtn = document.getElementById('clearBtn');
  const saveBtn = document.getElementById('saveBtn');
  const undoBtn = document.getElementById('undoBtn');
  const redoBtn = document.getElementById('redoBtn');
  const fileTreeEl = document.getElementById('fileTree');
  const editorKindLabel = document.getElementById('editorKindLabel');
  const editorPathLabel = document.getElementById('editorPathLabel');
  const dirtyBadge = document.getElementById('dirtyBadge');
  const binaryHexBar = document.getElementById('binaryHexBar');
  const binarySizeLabel = document.getElementById('binarySizeLabel');
  const binaryPageLabel = document.getElementById('binaryPageLabel');
  const binaryParseLabel = document.getElementById('binaryParseLabel');
  const binaryGoOffset = document.getElementById('binaryGoOffset');
  const binarySearchInput = document.getElementById('binarySearchInput');
  const binarySearchCount = document.getElementById('binarySearchCount');
  const binaryImportFile = document.getElementById('binaryImportFile');
  const newFileBtn = document.getElementById('newFileBtn');
  const newFolderBtn = document.getElementById('newFolderBtn');
  const mainLayout = document.querySelector('.fleditor-main');
  const projectPanel = document.getElementById('projectPanel');
  const projectPanelResize = document.getElementById('projectPanelResize');
  const editorPanelResize = document.getElementById('editorPanelResize');
  const terminalPanelResize = document.getElementById('terminalPanelResize');
  const terminalPanel = document.getElementById('terminalPanel');
  const workPanel = document.getElementById('workPanel');
  const editorWrap = document.getElementById('editorWrap');
  const projectPanelToggle = document.getElementById('projectPanelToggle');
  const terminalPanelToggle = document.getElementById('terminalPanelToggle');
  const dockRestoreProject = document.getElementById('dockRestoreProject');
  const dockRestoreTerminal = document.getElementById('dockRestoreTerminal');
  const dockResetLayoutBtn = document.getElementById('dockResetLayoutBtn');
  const resetProjectBtn = document.getElementById('resetProjectBtn');
  const editorEmptyOverlay = document.getElementById('editorEmptyOverlay');

  let wasmModule = null;
  let wasmLoadPromise = null;
  let running = false;
  let term = null;
  let cm = null;
  let codeMapVisible = false;
  let codeMapModel = null;
  let blockNavigationMark = null;

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
  let ioLineBuffer = '';

  /** 'flatl' | 'plaintext' | 'binary' | 'csv' | 'jsonl' */
  let editorViewMode = 'flatl';
  /** @type {null | { path: string, rows: string[][], savedRows: string[][], delimiter: string, lineEnding: string, preferTab: boolean, savedSerialized: string, headerRow: boolean, gridForced: boolean }} */
  let csvSession = null;
  /** @type {null | { path: string, lines: string[], lineEnding: string, savedSerialized: string, pageIndex: number, selected: number|null, panelMode: 'free'|'lock'|'none', lockValue: *, draftText: string, draftDirty: boolean, expand: Record<string, boolean>, recordsForced: boolean }} */
  let jsonlSession = null;
  /** @type {number[]} */
  let jsonlSearchMatches = [];
  let jsonlSearchMatchIndex = -1;
  /** 'select' | 'filter' | 'advfilter' — cycle; Filter/Adv.Filter narrow the list when input non-empty */
  let jsonlFindMode = 'select';
  let jsonlWordWrapEnabled = false;
  let jsonlPendingDialog = null;
  let jsonlPendingAction = null;
  /** @type {null | { path: string, lineEnding: string, encoding: string, savedProjectValue: string }} */
  let textSession = null;
  /** @type {{r:number,c:number}[]} */
  let csvSearchMatches = [];
  let csvSearchMatchIndex = -1;
  /** @type {{r:number,c:number}} */
  let csvSearchAnchor = { r: 0, c: 0 };
  let csvSelectExtend = false;
  /** @type {Uint8Array | null} */
  let binaryBuffer = null;
  /** @type {Uint8Array | null} */
  let binarySavedSnapshot = null;
  let binaryPage = 0;
  let binarySearchAnchor = 0;
  /** @type {number[]} */
  let binarySearchMatches = [];
  let binarySearchMatchIndex = -1;
  let binaryHighlightOffset = -1;
  /** @type {{ type: 'append' } | { type: 'offset', off: number } | null} */
  let binaryGridFocusAfterRender = null;
  const BINARY_HELP_DISMISS_KEY = 'flatl-fleditor-hex-help-dismissed';

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

  function expandAncestorDirsForPath(filePath) {
    const norm = P.normalizePath(filePath);
    if (!norm) {
      return;
    }
    for (const d of ancestorDirPaths(norm)) {
      treeFolderState.set(d, true);
    }
    saveTreeFolderState();
  }

  treeFolderState = loadTreeFolderState();

  function persistLayout() {
    layoutState = L.saveLayout(null, layoutState);
  }

  function syncDock() {
    if (dockRestoreProject) {
      dockRestoreProject.hidden = layoutState.projectOpen;
    }
    if (dockRestoreTerminal) {
      dockRestoreTerminal.hidden = layoutState.terminalOpen;
    }
  }

  const stackLayoutMq = window.matchMedia('(max-aspect-ratio: 1/1)');

  function isStackLayout() {
    return stackLayoutMq.matches;
  }

  function syncLayoutModeClass() {
    const stack = isStackLayout();
    document.documentElement.classList.toggle('fleditor-layout--stack', stack);
    document.documentElement.classList.toggle('fleditor-layout--row', !stack);
  }

  function workPanelHeightPx() {
    if (!workPanel) {
      return 480;
    }
    const h = workPanel.getBoundingClientRect().height;
    return h > 0 ? h : 480;
  }

  function stackTerminalHeightPx() {
    const workH = workPanelHeightPx();
    if (typeof layoutState.stackTerminalHeightPx === 'number') {
      return L.clampStackTerminalHeightPx(layoutState.stackTerminalHeightPx, workH);
    }
    return L.clampStackTerminalHeightPx(layoutState.terminalFraction * workH, workH);
  }

  function captureStackTerminalHeightFromDom() {
    if (!terminalPanel || !layoutState.terminalOpen) {
      return;
    }
    const h = terminalPanel.getBoundingClientRect().height;
    if (h > 8) {
      layoutState.stackTerminalHeightPx = L.clampStackTerminalHeightPx(h, workPanelHeightPx());
    }
  }

  function syncLayoutModeTerminalSizing(fromStack, toStack) {
    if (!layoutState.terminalOpen || !terminalPanel || !workPanel) {
      return;
    }
    if (toStack && !fromStack) {
      captureStackTerminalHeightFromDom();
    } else if (!toStack && fromStack) {
      const workH = workPanelHeightPx();
      const px =
        typeof layoutState.stackTerminalHeightPx === 'number'
          ? layoutState.stackTerminalHeightPx
          : terminalPanel.getBoundingClientRect().height;
      layoutState.terminalFraction = L.clampFraction(px / workH);
    }
  }

  function applyTerminalPanelSizing(stack) {
    if (!terminalPanel || !workPanel) {
      return;
    }
    if (layoutState.terminalOpen) {
      if (stack) {
        const px = stackTerminalHeightPx();
        terminalPanel.style.flexBasis = px + 'px';
      } else {
        const pct = Math.round(layoutState.terminalFraction * 1000) / 10;
        terminalPanel.style.flexBasis = pct + '%';
      }
      terminalPanel.style.flexGrow = '0';
      terminalPanel.style.flexShrink = '0';
    } else {
      terminalPanel.style.flexBasis = '';
      terminalPanel.style.flexGrow = '';
      terminalPanel.style.flexShrink = '';
    }
  }

  function applyLayout() {
    syncLayoutModeClass();
    const stack = isStackLayout();
    if (projectPanel) {
      projectPanel.hidden = !layoutState.projectOpen;
      if (layoutState.projectOpen) {
        if (stack) {
          projectPanel.style.width = '100%';
          const stackPct = Math.round(layoutState.stackProjectFraction * 1000) / 10;
          projectPanel.style.flexBasis = stackPct + '%';
          projectPanel.style.flexGrow = '0';
          projectPanel.style.flexShrink = '0';
        } else {
          projectPanel.style.flexBasis = '';
          projectPanel.style.flexGrow = '';
          projectPanel.style.flexShrink = '0';
          projectPanel.style.width = layoutState.projectWidthPx + 'px';
        }
      } else {
        projectPanel.style.width = '';
        projectPanel.style.flexBasis = '';
      }
    }
    if (projectPanelResize) {
      projectPanelResize.hidden =
        stack || !layoutState.projectOpen;
    }
    if (terminalPanel && workPanel) {
      terminalPanel.hidden = !layoutState.terminalOpen;
      workPanel.classList.toggle('fleditor-work--no-terminal', !layoutState.terminalOpen);
      applyTerminalPanelSizing(stack);
    }
    syncDock();
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

  function runFinishedEpilog(exitCode, termReason, panicDetail) {
    const detail = (panicDetail || '').trim();
    if (termReason === FLAT_TERM.OK) {
      return '-- finished: OK';
    }
    if (termReason === FLAT_TERM.FAIL || exitCode === 2) {
      return detail ? '-- finished: FAIL — ' + detail : '-- finished: FAIL';
    }
    if (termReason === FLAT_TERM.COMPLETE) {
      return '-- finished: complete';
    }
    if (termReason === FLAT_TERM.PANIC) {
      return detail ? '-- finished: panic — ' + detail : '-- finished: panic';
    }
    if (exitCode !== 0) {
      return detail ? '-- finished: error — ' + detail : '-- finished: error';
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

  function isBinaryOpenFile() {
    const path = currentOpenPath();
    if (!path) {
      return false;
    }
    return editorViewMode === 'binary';
  }

  function refreshBinaryDirty() {
    if (!binaryBuffer || !binarySavedSnapshot) {
      return;
    }
    setDirty(!H.bytesEqual(binaryBuffer, binarySavedSnapshot));
  }

  function binaryHexGridEl() {
    return document.getElementById('binaryHexGrid');
  }

  function setEditorSurfaceMode(mode) {
    const grid = binaryHexGridEl();
    if (cm) {
      const wrap = cm.getWrapperElement();
      if (wrap) {
        wrap.style.display = mode === 'binary' || mode === 'csv' || mode === 'jsonl' ? 'none' : '';
      }
    }
    if (grid) {
      grid.hidden = mode !== 'binary';
    }
    if (csvGridHost) {
      csvGridHost.hidden = mode !== 'csv';
    }
    if (jsonlRecordsHost) {
      jsonlRecordsHost.hidden = mode !== 'jsonl';
    }
    if (jsonlBar) {
      jsonlBar.hidden = mode !== 'jsonl';
    }
    syncEditorWrapKind();
  }

  function syncEditorWrapKind() {
    if (!editorWrap) {
      return;
    }
    editorWrap.classList.remove(
      'fleditor-editor-wrap--flatl',
      'fleditor-editor-wrap--text',
      'fleditor-editor-wrap--binary',
      'fleditor-editor-wrap--csv',
      'fleditor-editor-wrap--jsonl',
      'fleditor-editor-wrap--idle'
    );
    if (!hasOpenFile()) {
      editorWrap.classList.add('fleditor-editor-wrap--idle');
    } else if (editorViewMode === 'binary') {
      editorWrap.classList.add('fleditor-editor-wrap--binary');
    } else if (editorViewMode === 'csv') {
      editorWrap.classList.add('fleditor-editor-wrap--csv');
    } else if (editorViewMode === 'jsonl') {
      editorWrap.classList.add('fleditor-editor-wrap--jsonl');
    } else if (editorViewMode === 'flatl') {
      editorWrap.classList.add('fleditor-editor-wrap--flatl');
    } else {
      editorWrap.classList.add('fleditor-editor-wrap--text');
    }
  }

  function setBinaryPosFieldFromGrid(offset) {
    if (!binaryGoOffset) {
      return;
    }
    binaryGoOffset.value = String(offset);
  }

  function focusBinaryGridAppend() {
    if (!binaryBuffer) {
      return;
    }
    const page = G.appendSlotPageIndex(binaryBuffer.length);
    if (page !== binaryPage) {
      binaryPage = page;
      renderBinaryGrid();
      updateBinaryBarMeta();
      refreshBinaryDirty();
    }
    const grid = binaryHexGridEl();
    window.requestAnimationFrame(function () {
      if (grid && G.focusAppendCell(grid)) {
        setBinaryPosFieldFromGrid(binaryBuffer.length);
      }
    });
  }

  function focusBinaryGridOffset(offset) {
    if (!binaryBuffer || offset < 0) {
      return;
    }
    if (offset >= binaryBuffer.length) {
      if (offset === binaryBuffer.length) {
        focusBinaryGridAppend();
      }
      return;
    }
    const page = Math.floor(offset / H.BYTES_PER_PAGE);
    if (page !== binaryPage) {
      binaryPage = page;
      renderBinaryGrid();
      updateBinaryBarMeta();
      refreshBinaryDirty();
    }
    const grid = binaryHexGridEl();
    window.requestAnimationFrame(function () {
      if (grid && G.focusCellByOffset(grid, offset)) {
        setBinaryPosFieldFromGrid(offset);
      }
    });
  }

  function updateBinarySearchCountLabel() {
    if (!binarySearchCount) {
      return;
    }
    const n = binarySearchMatches.length;
    if (n === 0) {
      binarySearchCount.textContent = '0';
      return;
    }
    const pos = binarySearchMatchIndex >= 0 ? binarySearchMatchIndex + 1 : '—';
    binarySearchCount.textContent = pos + ' / ' + n;
  }

  function recomputeBinarySearchMatches() {
    if (!binaryBuffer || !binarySearchInput) {
      binarySearchMatches = [];
      binarySearchMatchIndex = -1;
      updateBinarySearchCountLabel();
      return;
    }
    const q = binarySearchInput.value.trim();
    if (!q) {
      binarySearchMatches = [];
      binarySearchMatchIndex = -1;
      updateBinarySearchCountLabel();
      return;
    }
    const pat = H.parseSearchPattern(binarySearchInput.value);
    if (!pat.ok) {
      binarySearchMatches = [];
      binarySearchMatchIndex = -1;
      updateBinarySearchCountLabel();
      return;
    }
    binarySearchMatches = H.findAllMatches(binaryBuffer, pat.bytes);
    binarySearchMatchIndex = binarySearchMatches.indexOf(binarySearchAnchor);
    updateBinarySearchCountLabel();
  }

  function scheduleBinaryGridFocusAfterRender() {
    const grid = binaryHexGridEl();
    if (!binaryGridFocusAfterRender || !grid || !binaryBuffer) {
      return;
    }
    const f = binaryGridFocusAfterRender;
    binaryGridFocusAfterRender = null;
    window.requestAnimationFrame(function () {
      if (f.type === 'append') {
        if (G.focusAppendCell(grid)) {
          setBinaryPosFieldFromGrid(binaryBuffer.length);
        }
        return;
      }
      if (G.focusCellByOffset(grid, f.off)) {
        setBinaryPosFieldFromGrid(f.off);
        return;
      }
      focusBinaryGridOffset(f.off);
    });
  }

  function afterBinaryShrink(focusOff) {
    setDirty(true);
    recomputeBinarySearchMatches();
    binaryGridFocusAfterRender =
      focusOff >= binaryBuffer.length ? { type: 'append' } : { type: 'offset', off: focusOff };
    const page =
      focusOff >= binaryBuffer.length
        ? G.appendSlotPageIndex(binaryBuffer.length)
        : Math.floor(focusOff / H.BYTES_PER_PAGE);
    if (page !== binaryPage) {
      binaryPage = page;
    }
    renderBinaryGrid();
    updateBinaryBarMeta();
    refreshBinaryDirty();
  }

  function renderBinaryGrid() {
    const grid = binaryHexGridEl();
    if (!grid || !binaryBuffer || !G) {
      return;
    }
    G.renderHexGrid(grid, binaryBuffer, binaryPage, {
      highlightOffset: binaryHighlightOffset >= 0 ? binaryHighlightOffset : undefined,
      savedBuffer: binarySavedSnapshot || undefined,
      onCellChange: function (offset, byte) {
        binaryBuffer[offset] = byte;
        setDirty(true);
        updateBinaryBarMeta();
      },
      onCellFocus: function (offset) {
        setBinaryPosFieldFromGrid(offset);
      },
      onAppendByte: function (byte) {
        const off = binaryBuffer.length;
        binaryBuffer = H.insertBytes(binaryBuffer, off, new Uint8Array([byte]));
        setDirty(true);
        recomputeBinarySearchMatches();
        binaryGridFocusAfterRender = { type: 'append' };
        const page = G.appendSlotPageIndex(binaryBuffer.length);
        if (page !== binaryPage) {
          binaryPage = page;
        }
        renderBinaryGrid();
        updateBinaryBarMeta();
        refreshBinaryDirty();
      },
      onDeleteByteAt: function (off) {
        if (!binaryBuffer || off < 0 || off >= binaryBuffer.length) {
          return;
        }
        binaryBuffer = H.deleteBytes(binaryBuffer, off, 1);
        afterBinaryShrink(off);
      },
      onBackspaceByteAt: function (off) {
        if (!binaryBuffer || binaryBuffer.length === 0) {
          return;
        }
        let delAt;
        let focusAt;
        if (off >= binaryBuffer.length) {
          delAt = binaryBuffer.length - 1;
          focusAt = delAt;
        } else if (off > 0) {
          delAt = off - 1;
          focusAt = delAt;
        } else {
          return;
        }
        binaryBuffer = H.deleteBytes(binaryBuffer, delAt, 1);
        afterBinaryShrink(focusAt);
      },
      onRequestFocusOffset: focusBinaryGridOffset,
      onRequestFocusAppend: focusBinaryGridAppend,
    });
    scheduleBinaryGridFocusAfterRender();
  }

  function showBinaryPage(pageIndex) {
    if (!binaryBuffer) {
      return;
    }
    const pages = H.pageCountForLength(binaryBuffer.length);
    let p = pageIndex;
    if (p < 0) {
      p = 0;
    }
    if (p >= pages) {
      p = pages - 1;
    }
    binaryPage = p;
    renderBinaryGrid();
    updateBinaryBarMeta();
    refreshBinaryDirty();
  }

  function updateBinaryBarMeta() {
    if (!binaryHexBar || editorViewMode !== 'binary') {
      if (binaryHexBar) {
        binaryHexBar.hidden = true;
      }
      return;
    }
    binaryHexBar.hidden = false;
    const n = binaryBuffer ? binaryBuffer.length : 0;
    const pages = H.pageCountForLength(n);
    const off = H.pageByteOffset(binaryPage);
    const onPage = H.bytesOnPage(n, binaryPage);
    if (binarySizeLabel) {
      binarySizeLabel.textContent = n + ' bytes';
    }
    if (binaryPageLabel) {
      binaryPageLabel.textContent =
        'Page ' +
        (binaryPage + 1) +
        '/' +
        pages +
        ' · bytes ' +
        off +
        '–' +
        (onPage > 0 ? off + onPage - 1 : off);
    }
    if (binaryParseLabel) {
      binaryParseLabel.textContent = dirty ? 'Modified' : 'Grid';
      binaryParseLabel.classList.remove('fleditor-binary-parse--err');
    }
    updateBinaryHelpBanner(n);
  }

  function isBinaryHelpDismissed() {
    try {
      return localStorage.getItem(BINARY_HELP_DISMISS_KEY) === '1';
    } catch (_e) {
      return false;
    }
  }

  function updateBinaryHelpBanner(byteLength) {
    const banner = document.getElementById('binaryHelpBanner');
    const helpEl = document.getElementById('binaryHelpLabel');
    if (!banner || !helpEl) {
      return;
    }
    if (editorViewMode !== 'binary') {
      banner.hidden = true;
      return;
    }
    if (isBinaryHelpDismissed()) {
      banner.hidden = true;
      return;
    }
    if (byteLength === 0) {
      helpEl.innerHTML =
        'File is <strong>empty</strong> (0 bytes). Click the <strong>+</strong> cell to append bytes, or use <strong>Size…</strong> / <strong>Insert…</strong>.';
    } else {
      helpEl.innerHTML =
        'Click a cell and type hex (overwrite, then auto-advance). At EOF use the <strong>+</strong> cell to append bytes. ' +
        'Clear a cell → <strong>00</strong> on blur. Modified bytes stay highlighted until Save.';
    }
    banner.hidden = false;
  }

  function enterBinaryEditorFromBytes(bytes) {
    leaveCsvEditor();
    leaveJsonlEditor();
    binaryBuffer = H.copyBytes(bytes);
    binarySavedSnapshot = H.copyBytes(bytes);
    binaryPage = 0;
    binarySearchAnchor = 0;
    binaryHighlightOffset = -1;
    editorViewMode = 'binary';
    if (binaryHexBar) {
      binaryHexBar.hidden = false;
    }
    setEditorSurfaceMode('binary');
    showBinaryPage(0);
    recomputeBinarySearchMatches();
    updateEditorHeader();
    updateEditorEmptyState();
    updateUndoRedoButtons();
  }

  function leaveBinaryEditor() {
    binaryBuffer = null;
    binarySavedSnapshot = null;
    binaryPage = 0;
    binaryHighlightOffset = -1;
    editorViewMode = 'flatl';
    setEditorSurfaceMode('text');
    if (binaryHexBar) {
      binaryHexBar.hidden = true;
    }
    const grid = binaryHexGridEl();
    if (grid) {
      grid.innerHTML = '';
    }
    const banner = document.getElementById('binaryHelpBanner');
    if (banner) {
      banner.hidden = true;
    }
    updateUndoRedoButtons();
  }

  function isCsvPath(path) {
    const low = String(path || '').toLowerCase();
    return low.endsWith('.csv') || low.endsWith('.tsv');
  }

  function isCsvSessionActive() {
    return csvSession != null && isCsvPath(csvSession.path);
  }

  function isJsonlPath(path) {
    return Jp.isJsonlPath(path);
  }

  function isJsonlSessionActive() {
    return jsonlSession != null && isJsonlPath(jsonlSession.path);
  }

  function leaveTextSession() {
    textSession = null;
  }

  function isTextFormatEditorActive() {
    if (!hasOpenFile() || editorViewMode !== 'plaintext') return false;
    const path = currentOpenPath();
    if (P.isFlatlProjectFile(path)) return false;
    if (isCsvSessionActive()) return false;
    if (isJsonlSessionActive()) return false;
    return textSession != null && textSession.path === path;
  }

  function textStoredProjectValue(logicalBody) {
    if (!textSession) return logicalBody || '';
    return Txt.wrapProjectText(logicalBody, textSession.encoding);
  }

  function textLogicalBodyFromCm() {
    if (!textSession) return cm ? cm.getValue() : '';
    return Txt.textFromCmNewlines(cm.getValue(), textSession.lineEnding);
  }

  function recomputeTextDirty() {
    if (!textSession) return;
    setDirty(textStoredProjectValue(textLogicalBodyFromCm()) !== textSession.savedProjectValue);
  }

  function enterTextFromProject(path, content) {
    leaveTextSession();
    const raw = typeof content === 'string' ? content : '';
    const savedProjectValue = raw;
    const encoding = Txt.encodingFromProjectContent(raw);
    const body = Txt.projectTextFromContent(raw);
    const lineEnding = Txt.detectLineEnding(body);
    textSession = {
      path: path,
      lineEnding: lineEnding,
      encoding: encoding,
      savedProjectValue: savedProjectValue,
    };
    const forCm = Txt.textToCmNewlines(body);
    cm.setValue(forCm);
    cm.clearHistory();
    recomputeTextDirty();
  }

  function syncCsvSelectExtendBtn() {
    if (!csvSelectExtendBtn) return;
    csvSelectExtendBtn.setAttribute('aria-pressed', csvSelectExtend ? 'true' : 'false');
    csvSelectExtendBtn.classList.toggle('fleditor-csv-header-btn--active', csvSelectExtend);
  }

  function leaveCsvEditor() {
    csvSession = null;
    csvSelectExtend = false;
    syncCsvSelectExtendBtn();
    if (csvSelectionStats) csvSelectionStats.textContent = '';
    csvSearchMatches = [];
    csvSearchMatchIndex = -1;
    csvSearchAnchor = { r: 0, c: 0 };
    if (csvSearchInput) csvSearchInput.value = '';
    if (csvSearchCount) csvSearchCount.textContent = '0';
    if (csvGoCell) csvGoCell.value = '';
    if (csvTableBar) csvTableBar.hidden = true;
    if (csvGridHost) {
      csvGridHost.innerHTML = '';
      csvGridHost.hidden = true;
    }
    if (csvHelpBanner) csvHelpBanner.hidden = true;
    if (csvViewToggleBtn) csvViewToggleBtn.hidden = true;
    syncEditorWrapKind();
  }

  function leaveJsonlEditor() {
    jsonlSession = null;
    jsonlSearchMatches = [];
    jsonlSearchMatchIndex = -1;
    jsonlFindMode = 'select';
    if (jsonlSearchInput) {
      jsonlSearchInput.value = '';
      jsonlSearchInput.removeAttribute('aria-invalid');
    }
    if (jsonlSearchCount) jsonlSearchCount.textContent = '0';
    clearJsonlAdvFilterError();
    syncJsonlFindModeUi();
    if (jsonlBar) jsonlBar.hidden = true;
    if (jsonlRecordsHost) {
      jsonlRecordsHost.hidden = true;
    }
    if (jsonlListHost) jsonlListHost.innerHTML = '';
    if (jsonlLockPanel) jsonlLockPanel.innerHTML = '';
    if (jsonlFreeTa) jsonlFreeTa.value = '';
    if (jsonlHelpBanner) jsonlHelpBanner.hidden = true;
    if (jsonlTooLargeOpenRecords) jsonlTooLargeOpenRecords.hidden = true;
    if (jsonlViewToggleBtn) jsonlViewToggleBtn.hidden = true;
    syncEditorWrapKind();
  }

  function showJsonlHelpBanner(kind, lineCount) {
    if (!jsonlHelpBanner || !jsonlHelpLabel) return;
    if (kind === 'none') {
      jsonlHelpBanner.hidden = true;
      if (jsonlTooLargeOpenRecords) jsonlTooLargeOpenRecords.hidden = true;
      return;
    }
    if (kind === 'too-large') {
      jsonlHelpLabel.textContent = Jp.tooLargeMessage(lineCount);
      if (jsonlTooLargeOpenRecords) jsonlTooLargeOpenRecords.hidden = false;
      jsonlHelpBanner.hidden = false;
      return;
    }
    jsonlHelpLabel.textContent = '';
    if (jsonlTooLargeOpenRecords) jsonlTooLargeOpenRecords.hidden = true;
    jsonlHelpBanner.hidden = true;
  }

  function updateJsonlToggleButton() {
    if (!jsonlViewToggleBtn) return;
    if (!isJsonlSessionActive()) {
      jsonlViewToggleBtn.hidden = true;
      return;
    }
    jsonlViewToggleBtn.hidden = false;
    if (editorViewMode === 'jsonl') {
      jsonlViewToggleBtn.textContent = 'Text';
      jsonlViewToggleBtn.title = 'Open text editor';
      jsonlViewToggleBtn.setAttribute('aria-label', 'Open text editor');
      jsonlViewToggleBtn.setAttribute('aria-pressed', 'true');
    } else {
      jsonlViewToggleBtn.textContent = 'Records';
      jsonlViewToggleBtn.title = 'Open records editor';
      jsonlViewToggleBtn.setAttribute('aria-label', 'Open records editor');
      jsonlViewToggleBtn.setAttribute('aria-pressed', 'false');
    }
  }

  function jsonlFreeDraftRaw() {
    if (!jsonlSession || jsonlSession.selected == null) return '';
    if (jsonlFreeTa && jsonlSession.panelMode === 'free') return jsonlFreeTa.value;
    return jsonlSession.draftText || '';
  }

  function jsonlLinesForSerialize() {
    if (!jsonlSession) return [];
    const lines = jsonlSession.lines.slice();
    if (
      editorViewMode === 'jsonl' &&
      jsonlSession.panelMode === 'free' &&
      jsonlSession.selected != null &&
      jsonlSession.draftDirty
    ) {
      lines[jsonlSession.selected] = Jp.storeLineFromFreeText(jsonlFreeDraftRaw());
    }
    return lines;
  }

  function jsonlSerializedNow() {
    if (!jsonlSession) return '';
    if (editorViewMode === 'jsonl') {
      return Jp.joinLines(jsonlLinesForSerialize(), jsonlSession.lineEnding);
    }
    return cm ? cm.getValue() : '';
  }

  function recomputeJsonlDirty() {
    if (!jsonlSession) return;
    const text = jsonlSerializedNow();
    setDirty(text !== jsonlSession.savedSerialized);
  }

  function clearJsonlAdvFilterError() {
    if (jsonlAdvFilterErr) {
      jsonlAdvFilterErr.hidden = true;
      jsonlAdvFilterErr.textContent = '';
    }
    if (jsonlSearchInput) jsonlSearchInput.removeAttribute('aria-invalid');
  }

  function setJsonlAdvFilterError(msg) {
    const text = msg || 'invalid query';
    if (jsonlAdvFilterErr) {
      jsonlAdvFilterErr.hidden = false;
      jsonlAdvFilterErr.textContent = text;
      jsonlAdvFilterErr.title = text;
    }
    if (jsonlSearchInput) jsonlSearchInput.setAttribute('aria-invalid', 'true');
  }

  /** True when list is narrowed to match set (Filter or Adv.Filter with non-empty input). */
  function jsonlListIsNarrowed() {
    if (jsonlFindMode !== 'filter' && jsonlFindMode !== 'advfilter') return false;
    return !!(jsonlSearchInput && jsonlSearchInput.value);
  }

  function jsonlListItemCount() {
    if (!jsonlSession) return 0;
    return jsonlListIsNarrowed() ? jsonlSearchMatches.length : jsonlSession.lines.length;
  }

  function jsonlPageIndexForLine(lineIndex) {
    if (jsonlListIsNarrowed()) {
      const mi = jsonlSearchMatches.indexOf(lineIndex);
      if (mi < 0) return 0;
      return Math.floor(mi / Jp.LINES_PER_PAGE);
    }
    return Math.floor(Math.max(0, lineIndex) / Jp.LINES_PER_PAGE);
  }

  function jsonlIndicesForCurrentPage() {
    if (!jsonlSession) return [];
    if (jsonlListIsNarrowed()) {
      const range = Jp.pageRange(jsonlSession.pageIndex, jsonlSearchMatches.length);
      return jsonlSearchMatches.slice(range.start, range.end);
    }
    const n = jsonlSession.lines.length;
    const range = Jp.pageRange(jsonlSession.pageIndex, n);
    const out = [];
    for (let i = range.start; i < range.end; i++) out.push(i);
    return out;
  }

  function syncJsonlFindModeUi() {
    if (jsonlFindModeBtn) {
      let label = 'Select';
      let title = 'Select mode: full list; Prev/Next jump matches. Click to cycle.';
      if (jsonlFindMode === 'filter') {
        label = 'Filter';
        title = 'Filter mode: list shows matching lines only. Click to cycle.';
      } else if (jsonlFindMode === 'advfilter') {
        label = 'Adv.Filter';
        title =
          'Adv.Filter: $.field = "x" | $.id <= 3 | $.a is null | PATH[] len = N | between N to N | has / IN / contains / type | and or not. Click to cycle.';
      }
      jsonlFindModeBtn.textContent = label;
      jsonlFindModeBtn.classList.toggle('fleditor-csv-header-btn--active', jsonlFindMode === 'advfilter');
      jsonlFindModeBtn.setAttribute('aria-pressed', jsonlFindMode === 'advfilter' ? 'true' : 'false');
      jsonlFindModeBtn.title = title;
    }
    if (jsonlSearchInput) {
      if (jsonlFindMode === 'advfilter') {
        jsonlSearchInput.placeholder = '$.field = "x"';
        jsonlSearchInput.setAttribute('aria-label', 'Advanced filter expression');
      } else if (jsonlFindMode === 'filter') {
        jsonlSearchInput.placeholder = 'text…';
        jsonlSearchInput.setAttribute('aria-label', 'Filter lines');
      } else {
        jsonlSearchInput.placeholder = 'text…';
        jsonlSearchInput.setAttribute('aria-label', 'Select matches');
      }
    }
  }

  function cycleJsonlFindMode() {
    if (jsonlFindMode === 'select') jsonlFindMode = 'filter';
    else if (jsonlFindMode === 'filter') jsonlFindMode = 'advfilter';
    else jsonlFindMode = 'select';
  }

  function applyJsonlFindModeToSelection() {
    if (!jsonlSession) return;
    recomputeJsonlSearchMatches();
    if (jsonlListIsNarrowed()) {
      if (jsonlSession.selected != null && jsonlSearchMatches.indexOf(jsonlSession.selected) >= 0) {
        jsonlSession.pageIndex = jsonlPageIndexForLine(jsonlSession.selected);
      } else if (jsonlSearchMatches.length) {
        jsonlSession.selected = jsonlSearchMatches[0];
        jsonlSession.pageIndex = 0;
        loadJsonlSelectedIntoPanel();
      } else {
        jsonlSession.pageIndex = 0;
      }
    } else if (jsonlSession.selected != null) {
      jsonlSession.pageIndex = jsonlPageIndexForLine(jsonlSession.selected);
    }
  }

  function updateJsonlBarMeta() {
    if (!jsonlSession || !jsonlPageLabel) return;
    const n = jsonlListItemCount();
    const narrowed = jsonlListIsNarrowed();
    if (n <= 0) {
      jsonlPageLabel.textContent = narrowed ? 'Found 0 of 0' : 'Lines 0 of 0';
      if (jsonlPageInput) jsonlPageInput.value = '1';
      if (jsonlPageTotal) jsonlPageTotal.textContent = '/ 1';
      ['jsonlPagePrev', 'jsonlPageNext'].forEach(function (id) {
        const button = document.getElementById(id);
        if (button) button.disabled = true;
      });
      return;
    }
    const range = Jp.pageRange(jsonlSession.pageIndex, n);
    if (narrowed) {
      jsonlPageLabel.textContent =
        'Found ' + (range.start + 1) + '\u2013' + range.end + ' of ' + n;
    } else {
      jsonlPageLabel.textContent =
        'Lines ' + (range.start + 1) + '\u2013' + range.end + ' of ' + n;
    }
    const pageCount = Jp.pageCount(n);
    const pageIndex = Math.max(0, Math.min(jsonlSession.pageIndex, pageCount - 1));
    jsonlSession.pageIndex = pageIndex;
    if (jsonlPageInput) jsonlPageInput.value = String(pageIndex + 1);
    if (jsonlPageTotal) jsonlPageTotal.textContent = '/ ' + pageCount;
    [
      ['jsonlPagePrev', pageIndex <= 0],
      ['jsonlPageNext', pageIndex >= pageCount - 1],
    ].forEach(function (entry) {
      const button = document.getElementById(entry[0]);
      if (button) button.disabled = entry[1];
    });
  }

  function syncJsonlModeButtons() {
    if (!jsonlSession) return;
    const mode = jsonlSession.panelMode;
    if (jsonlFreeBtn) jsonlFreeBtn.classList.toggle('is-on', mode === 'free');
    if (jsonlLockBtn) jsonlLockBtn.classList.toggle('is-on', mode === 'lock');
  }

  function ensureJsonlSelectionOnPage() {
    if (!jsonlSession) return;
    if (jsonlSession.lines.length <= 0) {
      jsonlSession.selected = null;
      jsonlSession.panelMode = 'none';
      return;
    }
    if (jsonlListIsNarrowed()) {
      if (!jsonlSearchMatches.length) return;
      const range = Jp.pageRange(jsonlSession.pageIndex, jsonlSearchMatches.length);
      const pageMatches = jsonlSearchMatches.slice(range.start, range.end);
      if (!pageMatches.length) return;
      if (jsonlSession.selected == null || pageMatches.indexOf(jsonlSession.selected) < 0) {
        jsonlSession.selected = pageMatches[0];
      }
      return;
    }
    const n = jsonlSession.lines.length;
    const range = Jp.pageRange(jsonlSession.pageIndex, n);
    if (jsonlSession.selected == null || jsonlSession.selected < range.start || jsonlSession.selected >= range.end) {
      jsonlSession.selected = range.start;
    }
  }

  function loadJsonlSelectedIntoPanel() {
    if (!jsonlSession) return;
    jsonlSession.expand = {};
    jsonlSession.draftDirty = false;
    if (jsonlSession.selected == null) {
      jsonlSession.panelMode = 'none';
      jsonlSession.lockValue = undefined;
      jsonlSession.draftText = '';
      return;
    }
    const line = jsonlSession.lines[jsonlSession.selected];
    const parsed = Jp.parseLine(line);
    if (parsed.ok) {
      jsonlSession.panelMode = 'lock';
      jsonlSession.lockValue = parsed.value;
      jsonlSession.draftText = Jp.prettyLine(parsed.value);
    } else {
      jsonlSession.panelMode = 'free';
      jsonlSession.lockValue = undefined;
      jsonlSession.draftText = line;
    }
  }

  function commitJsonlFreeDraftToLines(force) {
    if (!jsonlSession || jsonlSession.selected == null) return;
    if (jsonlSession.panelMode !== 'free') return;
    if (!force && !jsonlSession.draftDirty) return;
    const stored = Jp.storeLineFromFreeText(jsonlFreeDraftRaw());
    jsonlSession.lines[jsonlSession.selected] = stored;
    /* Keep Free textarea pretty if still valid; lines[] stays compact. */
    const parsed = Jp.parseLine(stored);
    jsonlSession.draftText = parsed.ok ? Jp.prettyLine(parsed.value) : stored;
    if (jsonlFreeTa && document.activeElement !== jsonlFreeTa) {
      jsonlFreeTa.value = jsonlSession.draftText;
    }
    jsonlSession.draftDirty = false;
  }

  function renderJsonlFreePanel() {
    if (!jsonlSession || !jsonlFreePanel || !jsonlFreeTa) return;
    if (jsonlSession.selected == null) {
      jsonlFreeHint.hidden = false;
      jsonlFreeHint.textContent = 'No line selected. Use + to add a line.';
      jsonlFreeErr.hidden = true;
      jsonlFreeTa.value = '';
      jsonlFreeTa.classList.remove('is-bad');
      return;
    }
    const text = jsonlSession.draftText;
    jsonlFreeTa.value = text;
    const parsed = Jp.parseLine(text);
    if (parsed.empty) {
      jsonlFreeHint.hidden = false;
      jsonlFreeHint.textContent = 'no json detected';
      jsonlFreeErr.hidden = true;
      jsonlFreeTa.classList.remove('is-bad');
    } else if (!parsed.ok) {
      jsonlFreeHint.hidden = true;
      jsonlFreeErr.hidden = false;
      jsonlFreeErr.textContent =
        parsed.error + (parsed.errorPos != null ? ' (position ' + parsed.errorPos + ')' : '');
      jsonlFreeTa.classList.add('is-bad');
      Ju.applyFreeErrorHighlight(jsonlFreeTa, parsed.errorPos);
    } else {
      jsonlFreeHint.hidden = true;
      jsonlFreeErr.hidden = true;
      jsonlFreeTa.classList.remove('is-bad');
    }
  }

  function runAfterJsonlDraftDecision(onContinue) {
    if (!jsonlSession || !jsonlSession.draftDirty) {
      onContinue();
      return;
    }
    if (!jsonlPendingDialog) {
      const dialog = document.createElement('dialog');
      dialog.className = 'fleditor-io-modal';
      dialog.setAttribute('aria-labelledby', 'jsonlPendingTitle');
      const form = document.createElement('div');
      form.className = 'fleditor-io-form';
      const title = document.createElement('h2');
      title.className = 'fleditor-io-title';
      title.id = 'jsonlPendingTitle';
      title.textContent = 'Pending line changes';
      const message = document.createElement('p');
      message.className = 'fleditor-io-hint';
      message.textContent = 'The selected line has unapplied changes. Apply them or revert them before continuing.';
      const actions = document.createElement('div');
      actions.className = 'fleditor-io-actions';
      const apply = document.createElement('button');
      apply.type = 'button';
      apply.className = 'fleditor-io-btn fleditor-io-btn--primary';
      apply.textContent = 'Apply';
      const revert = document.createElement('button');
      revert.type = 'button';
      revert.className = 'fleditor-io-btn';
      revert.textContent = 'Revert';
      const cancel = document.createElement('button');
      cancel.type = 'button';
      cancel.className = 'fleditor-io-btn';
      cancel.textContent = 'Cancel';
      actions.appendChild(apply);
      actions.appendChild(revert);
      actions.appendChild(cancel);
      form.appendChild(title);
      form.appendChild(message);
      form.appendChild(actions);
      dialog.appendChild(form);
      document.body.appendChild(dialog);
      apply.addEventListener('click', function () {
        dialog.close();
        if (!jsonlSession) return;
        commitJsonlFreeDraftToLines(true);
        renderJsonlRecords();
        recomputeJsonlDirty();
        const action = jsonlPendingAction;
        jsonlPendingAction = null;
        if (action) action();
      });
      revert.addEventListener('click', function () {
        dialog.close();
        if (!jsonlSession) return;
        loadJsonlSelectedIntoPanel();
        renderJsonlRecords();
        recomputeJsonlDirty();
        const action = jsonlPendingAction;
        jsonlPendingAction = null;
        if (action) action();
      });
      cancel.addEventListener('click', function () {
        dialog.close();
        jsonlPendingAction = null;
      });
      dialog.addEventListener('cancel', function (event) {
        event.preventDefault();
        dialog.close();
        jsonlPendingAction = null;
      });
      jsonlPendingDialog = dialog;
    }
    jsonlPendingAction = onContinue;
    jsonlPendingDialog.showModal();
  }

  function syncJsonlWordWrapUi() {
    if (jsonlWordWrapBtn) {
      jsonlWordWrapBtn.classList.toggle('fleditor-csv-header-btn--active', jsonlWordWrapEnabled);
      jsonlWordWrapBtn.setAttribute('aria-pressed', jsonlWordWrapEnabled ? 'true' : 'false');
    }
    if (jsonlLockPanel) jsonlLockPanel.classList.toggle('is-word-wrap', jsonlWordWrapEnabled);
    if (jsonlFreeTa) jsonlFreeTa.classList.toggle('is-word-wrap', jsonlWordWrapEnabled);
  }

  function renderJsonlLockPanel() {
    if (!jsonlSession || !jsonlLockPanel) return;
    jsonlLockPanel.textContent = '';
    if (jsonlSession.selected == null) {
      const p = document.createElement('p');
      p.className = 'fleditor-jsonl-hint';
      p.textContent = 'No line selected. Use + to add a line.';
      jsonlLockPanel.appendChild(p);
      return;
    }
    const parsed = Jp.parseLine(jsonlSession.lines[jsonlSession.selected]);
    if (!parsed.ok) {
      const p = document.createElement('p');
      p.className = 'fleditor-jsonl-err';
      p.textContent = 'Cannot lock this line. Switch to Free to edit.';
      jsonlLockPanel.appendChild(p);
      return;
    }
    jsonlSession.lockValue = parsed.value;
    syncJsonlWordWrapUi();
    const searchQuery =
      jsonlFindMode !== 'advfilter' && jsonlSearchInput ? jsonlSearchInput.value : '';
    if (searchQuery) {
      function expandMatchingBranches(value, path) {
        if (!value || typeof value !== 'object') return;
        let contains = false;
        try { contains = JSON.stringify(value).indexOf(searchQuery) >= 0; } catch (_e) {}
        if (!contains) return;
        jsonlSession.expand[Ju.pathKey(path)] = true;
        if (Array.isArray(value)) {
          value.forEach(function (child, index) {
            expandMatchingBranches(child, path.concat([String(index)]));
          });
        } else {
          Object.keys(value).forEach(function (key) {
            expandMatchingBranches(value[key], path.concat([key]));
          });
        }
      }
      expandMatchingBranches(parsed.value, ['$']);
    }
    Ju.renderLockTree(jsonlLockPanel, parsed.value, {
      expand: jsonlSession.expand,
      onToggle: function (path) {
        const k = Ju.pathKey(path);
        jsonlSession.expand[k] = !jsonlSession.expand[k];
        renderJsonlLockPanel();
      },
    });
    if (searchQuery) Ju.highlightText(jsonlLockPanel, searchQuery);
  }

  function renderJsonlDetail() {
    if (!jsonlSession) return;
    if (jsonlLineLabel) {
      jsonlLineLabel.textContent =
        jsonlSession.selected == null ? 'No line' : 'Line ' + (jsonlSession.selected + 1);
    }
    syncJsonlModeButtons();
    const mode = jsonlSession.panelMode;
    if (jsonlLockPanel) jsonlLockPanel.hidden = mode !== 'lock';
    if (jsonlFreePanel) jsonlFreePanel.hidden = mode !== 'free';
    if (mode === 'lock') renderJsonlLockPanel();
    else if (mode === 'free') renderJsonlFreePanel();
    else {
      if (jsonlLockPanel) {
        jsonlLockPanel.hidden = false;
        renderJsonlLockPanel();
      }
      if (jsonlFreePanel) jsonlFreePanel.hidden = true;
    }
  }

  function renderJsonlRecords() {
    if (!jsonlSession || !jsonlListHost) return;
    // Keep the list viewport stable when an operation rebuilds the rows.
    const previousListScroll = jsonlListHost.querySelector('.fleditor-jsonl-list-scroll');
    const previousScrollTop = previousListScroll ? previousListScroll.scrollTop : 0;
    recomputeJsonlSearchMatches();
    const pc = Jp.pageCount(Math.max(1, jsonlListItemCount()));
    if (jsonlSession.pageIndex >= pc) jsonlSession.pageIndex = Math.max(0, pc - 1);
    ensureJsonlSelectionOnPage();
    Ju.renderLineList(jsonlListHost, {
      lines: jsonlSession.lines,
      lineIndices: jsonlIndicesForCurrentPage(),
      selected: jsonlSession.selected,
      onSelect: function (index) {
        if (jsonlSession.selected === index) return;
        // Only an unapplied edit in the current Free draft needs a decision.
        // Structural list changes (add, duplicate, move, delete) are already
        // part of the JSONL document and must not trigger a discard warning.
        runAfterJsonlDraftDecision(function () {
          jsonlSession.selected = index;
          loadJsonlSelectedIntoPanel();
          renderJsonlRecords();
          recomputeJsonlDirty();
        });
      },
      onDelete: function (index) {
        confirmDiscardIfDirty(function () {
          const wasSel = jsonlSession.selected;
          jsonlSession.lines.splice(index, 1);
          if (!jsonlSession.lines.length) {
            jsonlSession.selected = null;
            jsonlSession.panelMode = 'none';
          } else if (wasSel === index) {
            jsonlSession.selected = Math.max(0, index - 1);
            loadJsonlSelectedIntoPanel();
          } else if (wasSel != null && wasSel > index) {
            jsonlSession.selected = wasSel - 1;
          }
          renderJsonlRecords();
          recomputeJsonlDirty();
        });
      },
      onAdd: function () {
        // Adding a line is a repeatable list operation: keep the current draft,
        // insert the new empty line, and do not prompt about unsaved changes.
        commitJsonlFreeDraftToLines(false);
        const at =
          jsonlSession.selected == null ? jsonlSession.lines.length : jsonlSession.selected + 1;
        jsonlSession.lines.splice(at, 0, '');
        jsonlSession.selected = at;
        recomputeJsonlSearchMatches();
        jsonlSession.pageIndex = jsonlPageIndexForLine(at);
        loadJsonlSelectedIntoPanel();
        renderJsonlRecords();
        recomputeJsonlDirty();
      },
      onDuplicate: function (index) {
        runAfterJsonlDraftDecision(function () {
          if (!jsonlSession || index < 0 || index >= jsonlSession.lines.length) return;
          const copy = jsonlSession.lines[index];
          const at = index + 1;
          jsonlSession.lines.splice(at, 0, copy);
          jsonlSession.selected = at;
          recomputeJsonlSearchMatches();
          jsonlSession.pageIndex = jsonlPageIndexForLine(at);
          loadJsonlSelectedIntoPanel();
          renderJsonlRecords();
          recomputeJsonlDirty();
        });
      },
      onMove: function (index, direction) {
        runAfterJsonlDraftDecision(function () {
          if (!jsonlSession) return;
          const target = index + direction;
          if (index < 0 || index >= jsonlSession.lines.length ||
              target < 0 || target >= jsonlSession.lines.length) return;
          const tmp = jsonlSession.lines[index];
          jsonlSession.lines[index] = jsonlSession.lines[target];
          jsonlSession.lines[target] = tmp;
          jsonlSession.selected = target;
          recomputeJsonlSearchMatches();
          jsonlSession.pageIndex = jsonlPageIndexForLine(target);
          loadJsonlSelectedIntoPanel();
          renderJsonlRecords();
          recomputeJsonlDirty();
        });
      },
    });
    const listScroll = jsonlListHost.querySelector('.fleditor-jsonl-list-scroll');
    if (listScroll) {
      listScroll.scrollTop = previousScrollTop;
      const selectedRow = listScroll.querySelector('.fleditor-jsonl-list-item--sel');
      if (selectedRow) {
        const listRect = listScroll.getBoundingClientRect();
        const rowRect = selectedRow.getBoundingClientRect();
        const rowTop = rowRect.top - listRect.top + listScroll.scrollTop;
        const rowBottom = rowRect.bottom - listRect.top + listScroll.scrollTop;
        const viewTop = listScroll.scrollTop;
        const viewBottom = viewTop + listScroll.clientHeight;
        if (rowTop < viewTop) listScroll.scrollTop = rowTop;
        else if (rowBottom > viewBottom) listScroll.scrollTop = rowBottom - listScroll.clientHeight;
      }
    }
    updateJsonlBarMeta();
    syncJsonlFindModeUi();
    renderJsonlDetail();
  }

  function setJsonlPanelMode(next) {
    if (!jsonlSession || jsonlSession.selected == null) return;
    if (next === 'free') {
      if (jsonlSession.panelMode === 'lock') {
        const parsed = Jp.parseLine(jsonlSession.lines[jsonlSession.selected]);
        jsonlSession.draftText = parsed.ok ? Jp.prettyLine(parsed.value) : jsonlSession.lines[jsonlSession.selected];
        jsonlSession.draftDirty = false;
      }
      jsonlSession.panelMode = 'free';
      renderJsonlDetail();
      return;
    }
    if (next === 'lock') {
      if (jsonlSession.panelMode === 'free' && jsonlFreeTa) {
        jsonlSession.draftText = jsonlFreeTa.value;
      }
      const text = jsonlSession.draftText;
      const parsed = Jp.parseLine(text);
      if (!parsed.ok) {
        jsonlSession.panelMode = 'free';
        jsonlSession.draftDirty = true;
        renderJsonlDetail();
        if (jsonlFreeErr) {
          jsonlFreeErr.hidden = false;
          jsonlFreeErr.textContent =
            (parsed.empty ? 'no json detected' : parsed.error || 'Invalid JSON') +
            ' — stay in Free until fixed.';
        }
        if (!parsed.empty) Ju.applyFreeErrorHighlight(jsonlFreeTa, parsed.errorPos);
        return;
      }
      jsonlSession.lines[jsonlSession.selected] = Jp.serializeLine(parsed.value);
      jsonlSession.lockValue = parsed.value;
      jsonlSession.draftText = Jp.prettyLine(parsed.value);
      jsonlSession.draftDirty = false;
      jsonlSession.expand = {};
      jsonlSession.panelMode = 'lock';
      renderJsonlRecords();
      recomputeJsonlDirty();
    }
  }

  function updateJsonlSearchCount() {
    if (!jsonlSearchCount) return;
    const n = jsonlSearchMatches.length;
    jsonlSearchCount.textContent = n
      ? (jsonlSearchMatchIndex >= 0 ? jsonlSearchMatchIndex + 1 : '\u2014') + ' / ' + n
      : '0';
  }

  function collectJsonlSubstringMatches(q) {
    const out = [];
    if (!jsonlSession || !q) return out;
    for (let i = 0; i < jsonlSession.lines.length; i++) {
      if (String(jsonlSession.lines[i]).indexOf(q) >= 0) out.push(i);
    }
    return out;
  }

  function collectJsonlAdvFilterMatches(q) {
    const out = [];
    if (!jsonlSession || !q) return { ok: true, indices: out };
    if (!Af || typeof Af.parse !== 'function' || typeof Af.eval !== 'function') {
      return { ok: false, error: 'Adv.Filter engine not loaded' };
    }
    const parsed = Af.parse(q);
    if (!parsed.ok) return { ok: false, error: parsed.error || 'invalid query', pos: parsed.pos };
    for (let i = 0; i < jsonlSession.lines.length; i++) {
      const pl = Jp.parseLine(jsonlSession.lines[i]);
      if (!pl.ok) continue;
      if (Af.eval(parsed.ast, pl.value)) out.push(i);
    }
    return { ok: true, indices: out };
  }

  function recomputeJsonlSearchMatches() {
    const previousLine = jsonlSession && jsonlSession.selected != null
      ? jsonlSession.selected : -1;
    jsonlSearchMatches = [];
    jsonlSearchMatchIndex = -1;
    if (!jsonlSession || !jsonlSearchInput) {
      clearJsonlAdvFilterError();
      updateJsonlSearchCount();
      return;
    }
    const q = jsonlSearchInput.value;
    if (!q) {
      clearJsonlAdvFilterError();
      updateJsonlSearchCount();
      return;
    }
    if (jsonlFindMode === 'advfilter') {
      const res = collectJsonlAdvFilterMatches(q);
      if (!res.ok) {
        setJsonlAdvFilterError(res.error);
        updateJsonlSearchCount();
        return;
      }
      clearJsonlAdvFilterError();
      jsonlSearchMatches = res.indices;
    } else {
      clearJsonlAdvFilterError();
      jsonlSearchMatches = collectJsonlSubstringMatches(q);
    }
    if (previousLine >= 0) jsonlSearchMatchIndex = jsonlSearchMatches.indexOf(previousLine);
    updateJsonlSearchCount();
  }

  function gotoJsonlSearchMatch(dir) {
    if (!jsonlSession || !jsonlSearchInput) return;
    const q = jsonlSearchInput.value;
    if (!q) return;
    // Rebuild matches in case the selected line was edited since the last search.
    const selected = jsonlSession.selected == null ? -1 : jsonlSession.selected;
    recomputeJsonlSearchMatches();
    if (!jsonlSearchMatches.length) {
      jsonlSearchMatchIndex = -1;
      updateJsonlSearchCount();
      return;
    }
    let idx = -1;
    if (dir > 0) {
      for (let i = 0; i < jsonlSearchMatches.length; i++) {
        if (jsonlSearchMatches[i] > selected) { idx = i; break; }
      }
      if (idx < 0) idx = 0;
    } else {
      for (let i = jsonlSearchMatches.length - 1; i >= 0; i--) {
        if (jsonlSearchMatches[i] < selected) { idx = i; break; }
      }
      if (idx < 0) idx = jsonlSearchMatches.length - 1;
    }
    jsonlSearchMatchIndex = idx;
    const lineIndex = jsonlSearchMatches[idx];
    jsonlSession.selected = lineIndex;
    jsonlSession.pageIndex = jsonlPageIndexForLine(lineIndex);
    loadJsonlSelectedIntoPanel();
    renderJsonlRecords();
    // Keep the active search result visible inside the JSONL line-list scroller.
    const selectedRow = jsonlListHost &&
      jsonlListHost.querySelector('.fleditor-jsonl-list-item--sel');
    if (selectedRow && typeof selectedRow.scrollIntoView === 'function') {
      selectedRow.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'smooth' });
    }
    updateJsonlSearchCount();
  }

  function enterJsonlRecordsMode() {
    if (!jsonlSession) return;
    editorViewMode = 'jsonl';
    setEditorSurfaceMode('jsonl');
    ensureJsonlSelectionOnPage();
    if (jsonlSession.selected != null && jsonlSession.panelMode === 'none') {
      loadJsonlSelectedIntoPanel();
    } else if (jsonlSession.selected != null && jsonlSession.lockValue === undefined && jsonlSession.panelMode !== 'free') {
      loadJsonlSelectedIntoPanel();
    }
    renderJsonlRecords();
    showJsonlHelpBanner('none');
    updateJsonlToggleButton();
    updateEditorHeader();
    updateEditorEmptyState();
    updateUndoRedoButtons();
  }

  function enterJsonlFromProject(path, content) {
    leaveJsonlEditor();
    leaveCsvEditor();
    const raw = typeof content === 'string' ? content : '';
    const lineEnding = Jp.detectLineEnding(raw);
    const lines = Jp.splitLines(raw);
    jsonlSession = {
      path: path,
      lines: lines,
      lineEnding: lineEnding,
      savedSerialized: raw,
      pageIndex: 0,
      selected: lines.length ? 0 : null,
      panelMode: 'none',
      lockValue: undefined,
      draftText: '',
      draftDirty: false,
      expand: {},
      recordsForced: false,
    };
    updateJsonlToggleButton();
    if (Jp.isTooLarge(lines.length)) {
      suppressChange = true;
      loadTextEditorForPath(path, raw);
      suppressChange = false;
      showJsonlHelpBanner('too-large', lines.length);
      updateEditorHeader();
      updateEditorEmptyState();
      updateUndoRedoButtons();
      return;
    }
    if (lines.length) loadJsonlSelectedIntoPanel();
    enterJsonlRecordsMode();
  }

  function saveJsonlEditorToProject() {
    if (!jsonlSession || !hasOpenFile()) return;
    const path = currentOpenPath();
    let text = '';
    if (editorViewMode === 'jsonl') {
      if (jsonlSession.panelMode === 'free' && jsonlSession.selected != null) {
        commitJsonlFreeDraftToLines(true);
      }
      text = Jp.joinLines(jsonlSession.lines, jsonlSession.lineEnding);
    } else if (cm) {
      text = cm.getValue();
      jsonlSession.lines = Jp.splitLines(text);
      jsonlSession.lineEnding = Jp.detectLineEnding(text);
    }
    project.files[path] = text;
    jsonlSession.savedSerialized = text;
  }

  function doToggleJsonlView() {
    if (!jsonlSession) return;
    if (editorViewMode === 'jsonl') {
      commitJsonlFreeDraftToLines(true);
      const text = Jp.joinLines(jsonlSession.lines, jsonlSession.lineEnding);
      suppressChange = true;
      loadTextEditorForPath(jsonlSession.path, text);
      suppressChange = false;
      if (jsonlBar) jsonlBar.hidden = true;
      showJsonlHelpBanner('none');
      recomputeJsonlDirty();
    } else {
      const raw = cm ? cm.getValue() : '';
      const lines = Jp.splitLines(raw);
      if (Jp.isTooLarge(lines.length) && !jsonlSession.recordsForced) {
        showAlert(Jp.tooLargeMessage(lines.length), 'Jsonl editor');
        return;
      }
      jsonlSession.lines = lines;
      jsonlSession.lineEnding = Jp.detectLineEnding(raw);
      jsonlSession.pageIndex = 0;
      jsonlSession.selected = lines.length ? 0 : null;
      jsonlSession.expand = {};
      if (lines.length) loadJsonlSelectedIntoPanel();
      else {
        jsonlSession.panelMode = 'none';
      }
      enterJsonlRecordsMode();
      recomputeJsonlDirty();
    }
    updateJsonlToggleButton();
    updateEditorHeader();
  }

  function toggleJsonlView() {
    if (!jsonlSession) return;
    confirmDiscardIfDirty(doToggleJsonlView);
  }

  function openJsonlRecordsDespiteSize() {
    if (!jsonlSession) return;
    confirmDiscardIfDirty(function () {
      if (editorViewMode === 'plaintext' && cm) {
        jsonlSession.lines = Jp.splitLines(cm.getValue());
        jsonlSession.lineEnding = Jp.detectLineEnding(cm.getValue());
      }
      jsonlSession.recordsForced = true;
      jsonlSession.pageIndex = 0;
      jsonlSession.selected = jsonlSession.lines.length ? 0 : null;
      if (jsonlSession.selected != null) loadJsonlSelectedIntoPanel();
      enterJsonlRecordsMode();
      recomputeJsonlDirty();
      updateJsonlToggleButton();
    });
  }

  function csvParseOptsFromSession() {
    if (!csvSession) return {};
    return {
      preferTab: csvSession.preferTab,
      delimiter: csvSession.autoDetectDelimiter ? undefined : csvSession.delimiter,
      autoDetect: !!csvSession.autoDetectDelimiter,
      quoteChar: csvSession.quoteChar,
    };
  }

  function csvLogicalTextNow() {
    if (!csvSession) return '';
    if (editorViewMode === 'csv') return csvSerializedNow();
    return cm ? cm.getValue() : '';
  }

  function csvStoredProjectValue(logicalText) {
    if (!csvSession) return '';
    return CsvP.wrapProjectCsv(logicalText, csvSession.encoding);
  }

  function csvSerializedNow() {
    if (!csvSession) return '';
    return CsvP.serializeCsv(
      csvSession.rows,
      csvSession.delimiter,
      csvSession.lineEnding,
      csvSession.quoteChar,
      !!csvSession.quoteAllFields
    );
  }

  function recomputeCsvDirty() {
    if (!csvSession) return;
    const logical = csvLogicalTextNow();
    setDirty(csvStoredProjectValue(logical) !== csvSession.savedProjectValue);
  }

  function snapshotCsvSavedRows() {
    if (!csvSession) return;
    csvSession.savedRows = JSON.parse(JSON.stringify(csvSession.rows));
  }

  function csvColCount() {
    if (!csvSession || !csvSession.rows.length) return 1;
    return csvSession.rows[0].length;
  }

  function setCsvPosFieldFromCell(r, c) {
    if (!csvGoCell) return;
    csvGoCell.value = r + 1 + ',' + (c + 1);
  }

  function updateCsvSearchCountLabel() {
    if (!csvSearchCount) return;
    const n = csvSearchMatches.length;
    if (!n) {
      csvSearchCount.textContent = '0';
      return;
    }
    const pos = csvSearchMatchIndex >= 0 ? csvSearchMatchIndex + 1 : '\u2014';
    csvSearchCount.textContent = pos + ' / ' + n;
  }

  function recomputeCsvSearchMatches() {
    if (!csvSession || !csvSearchInput) {
      csvSearchMatches = [];
      csvSearchMatchIndex = -1;
      updateCsvSearchCountLabel();
      return;
    }
    const q = csvSearchInput.value.trim();
    if (!q) {
      csvSearchMatches = [];
      csvSearchMatchIndex = -1;
      updateCsvSearchCountLabel();
      applyCsvSearchVisuals();
      return;
    }
    csvSearchMatches = CsvG.findCellMatches(csvSession.rows, q);
    csvSearchMatchIndex = CsvG.matchIndexForAnchor(
      csvSearchMatches,
      csvSearchAnchor.r,
      csvSearchAnchor.c,
      csvColCount()
    );
    updateCsvSearchCountLabel();
    applyCsvSearchVisuals();
  }

  function applyCsvSearchVisuals() {
    if (!csvGridHost || editorViewMode !== 'csv') return;
    CsvG.applySearchHighlights(csvGridHost, csvSearchMatches, csvSearchMatchIndex);
  }

  function csvGoToCell(r, c) {
    if (!csvSession || !csvGridHost) return;
    const cols = csvColCount();
    if (r < 0 || c < 0 || r >= csvSession.rows.length || c >= cols) return;
    csvSearchAnchor = { r: r, c: c };
    csvSearchMatchIndex = CsvG.matchIndexForAnchor(csvSearchMatches, r, c, cols);
    setCsvPosFieldFromCell(r, c);
    updateCsvSearchCountLabel();
    applyCsvSearchVisuals();
    CsvG.focusCell(csvGridHost, r, c);
    const inp = csvGridHost.querySelector(
      'input.fleditor-csv-grid-cell[data-row="' + r + '"][data-col="' + c + '"]'
    );
    if (inp && inp.scrollIntoView) inp.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }

  function csvSearchStep(direction) {
    if (!csvSession || !csvSearchInput) return;
    const q = csvSearchInput.value.trim();
    if (!q) {
      showAlert('Enter a search pattern', 'CSV editor');
      return;
    }
    csvSearchMatches = CsvG.findCellMatches(csvSession.rows, q);
    const list = csvSearchMatches;
    if (!list.length) {
      csvSearchMatchIndex = -1;
      updateCsvSearchCountLabel();
      applyCsvSearchVisuals();
      return;
    }
    const cols = csvColCount();
    const anchorIdx = CsvG.cellLinearIndex(csvSearchAnchor.r, csvSearchAnchor.c, cols);
    let idx = -1;
    if (direction === 'next') {
      for (let i = 0; i < list.length; i++) {
        const li = CsvG.cellLinearIndex(list[i].r, list[i].c, cols);
        if (li > anchorIdx) {
          idx = i;
          break;
        }
      }
      if (idx < 0) idx = 0;
    } else {
      for (let i = list.length - 1; i >= 0; i--) {
        const li = CsvG.cellLinearIndex(list[i].r, list[i].c, cols);
        if (li < anchorIdx) {
          idx = i;
          break;
        }
      }
      if (idx < 0) idx = list.length - 1;
    }
    csvSearchMatchIndex = idx;
    csvGoToCell(list[idx].r, list[idx].c);
  }

  function updateCsvSelectionStats() {
    if (!csvSelectionStats) return;
    if (!csvSession || editorViewMode !== 'csv') {
      csvSelectionStats.textContent = '';
      return;
    }
    csvSelectionStats.textContent = CsvG.formatSelectionSummary(
      csvSession.rows,
      csvSession.selAnchor,
      csvSession.selFocus
    );
  }

  function updateCsvBarMeta() {
    if (!csvSession || !csvTableSizeLabel) return;
    const t = CsvP.tableSize(csvSession.rows);
    csvTableSizeLabel.textContent = t.rows + ' \u00d7 ' + t.cols;
    updateCsvSelectionStats();
    if (csvHeaderRowBtn) {
      csvHeaderRowBtn.setAttribute('aria-pressed', csvSession.headerRow ? 'true' : 'false');
      csvHeaderRowBtn.classList.toggle('fleditor-csv-header-btn--active', !!csvSession.headerRow);
    }
  }

  function updateCsvToggleButton() {
    if (!csvViewToggleBtn) return;
    if (!isCsvSessionActive()) {
      csvViewToggleBtn.hidden = true;
      return;
    }
    csvViewToggleBtn.hidden = false;
    if (editorViewMode === 'csv') {
      csvViewToggleBtn.textContent = 'Text';
      csvViewToggleBtn.title = 'Open text editor';
      csvViewToggleBtn.setAttribute('aria-label', 'Open text editor');
      csvViewToggleBtn.setAttribute('aria-pressed', 'true');
    } else {
      csvViewToggleBtn.textContent = '\u25a6';
      csvViewToggleBtn.title = 'Open table editor';
      csvViewToggleBtn.setAttribute('aria-label', 'Open table editor');
      csvViewToggleBtn.setAttribute('aria-pressed', 'false');
    }
  }

  function showCsvHelpBanner(kind, rows) {
    if (!csvHelpBanner || !csvHelpLabel) return;
    if (kind === 'too-large' && rows) {
      csvHelpLabel.textContent = CsvP.tooLargeMessage(rows);
      if (csvTooLargeOpenTable) csvTooLargeOpenTable.hidden = false;
      csvHelpBanner.hidden = false;
      return;
    }
    if (kind === 'empty') {
      csvHelpLabel.textContent =
        'File is empty. Type in a cell or use + to grow the table, or Size\u2026.';
      if (csvTooLargeOpenTable) csvTooLargeOpenTable.hidden = true;
      csvHelpBanner.hidden = false;
      return;
    }
    csvHelpBanner.hidden = true;
  }

  function applyCsvSelectionVisuals() {
    if (!csvGridHost || !csvSession) return;
    CsvG.applySelectionHighlights(csvGridHost, csvSession.selAnchor, csvSession.selFocus);
    applyCsvAxisHighlights();
    updateCsvSelectionStats();
  }

  function csvSetSelectionRect(r0, c0, r1, c1) {
    if (!csvSession) return;
    csvSession.selAnchor = { r: r0, c: c0 };
    csvSession.selFocus = { r: r1, c: c1 };
    applyCsvSelectionVisuals();
  }

  function csvSelectRow(r, shift) {
    if (!csvSession) return;
    const lastCol = Math.max(0, csvColCount() - 1);
    if (shift && csvSession.selAnchor && csvSession.selFocus) {
      const b = CsvG.normalizeSelection(csvSession.selAnchor, csvSession.selFocus);
      const r0 = Math.min(b.r0, b.r1, r);
      const r1 = Math.max(b.r0, b.r1, r);
      csvSetSelectionRect(r0, 0, r1, lastCol);
    } else {
      csvSetSelectionRect(r, 0, r, lastCol);
    }
    csvGoToCell(r, 0);
  }

  function csvSelectCol(c, shift) {
    if (!csvSession) return;
    const lastRow = Math.max(0, csvSession.rows.length - 1);
    if (shift && csvSession.selAnchor && csvSession.selFocus) {
      const b = CsvG.normalizeSelection(csvSession.selAnchor, csvSession.selFocus);
      const c0 = Math.min(b.c0, b.c1, c);
      const c1 = Math.max(b.c0, b.c1, c);
      csvSetSelectionRect(0, c0, lastRow, c1);
    } else {
      csvSetSelectionRect(0, c, lastRow, c);
    }
    csvGoToCell(0, c);
  }

  function applyCsvAxisHighlights() {
    if (!csvGridHost || !csvSession) return;
    csvGridHost.querySelectorAll('.fleditor-csv-grid-axis').forEach(function (el) {
      el.classList.remove('fleditor-csv-grid-axis--active');
    });
    const b = CsvG.normalizeSelection(csvSession.selAnchor, csvSession.selFocus);
    if (!b) return;
    const cols = csvColCount();
    const rows = csvSession.rows.length;
    const fullRowSel = b.c0 === 0 && b.c1 >= cols - 1;
    const fullColSel = b.r0 === 0 && b.r1 >= rows - 1;
    if (fullRowSel) {
      for (let ri = b.r0; ri <= b.r1; ri++) {
        const el = csvGridHost.querySelector('[data-csv-row="' + ri + '"]');
        if (el) el.classList.add('fleditor-csv-grid-axis--active');
      }
    }
    if (fullColSel) {
      for (let ci = b.c0; ci <= b.c1; ci++) {
        const el = csvGridHost.querySelector('[data-csv-col="' + ci + '"]');
        if (el) el.classList.add('fleditor-csv-grid-axis--active');
      }
    }
  }

  function csvWriteClipboard(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text).catch(function () {
        return csvWriteClipboardFallback(text);
      });
    }
    return csvWriteClipboardFallback(text);
  }

  function csvWriteClipboardFallback(text) {
    return new Promise(function (resolve, reject) {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.setAttribute('readonly', '');
      ta.style.position = 'fixed';
      ta.style.left = '-9999px';
      document.body.appendChild(ta);
      ta.select();
      let ok = false;
      try {
        ok = document.execCommand('copy');
      } catch (_e) {
        ok = false;
      }
      document.body.removeChild(ta);
      if (ok) resolve();
      else reject(new Error('copy failed'));
    });
  }

  function csvPasteAnchorCell() {
    if (!csvSession) return { r: 0, c: 0 };
    const b = CsvG.normalizeSelection(csvSession.selAnchor, csvSession.selFocus);
    if (b) return { r: b.r0, c: b.c0 };
    const f = csvSession.selFocus || { r: 0, c: 0 };
    return { r: f.r, c: f.c };
  }

  function csvCopySelection() {
    if (!csvSession) return;
    const text = CsvG.copySelectionForClipboard(
      csvSession.rows,
      csvSession.selAnchor,
      csvSession.selFocus,
      csvSession.delimiter,
      csvSession.lineEnding,
      csvSession.quoteChar,
      csvSession.quoteAllFields
    );
    void csvWriteClipboard(text).catch(function () {
      void showAlert('Could not copy to clipboard', 'CSV editor');
    });
  }

  async function csvApplyPasteBlock(r, c, text) {
    if (!csvSession || !text) return;
    const block = CsvP.parseClipboardGrid(text, {
      preferTab: csvSession.preferTab,
      delimiter: csvSession.autoDetectDelimiter ? undefined : csvSession.delimiter,
      quoteChar: csvSession.quoteChar,
    });
    if (!block.length) return;
    let maxW = 0;
    block.forEach(function (row) {
      if (row.length > maxW) maxW = row.length;
    });
    const needRows = r + block.length;
    const needCols = c + maxW;
    const curCols = sessColCount(csvSession.rows);
    let rows = csvSession.rows;
    if (needRows > rows.length || needCols > curCols) {
      rows = CsvP.resizeGrid(
        rows,
        Math.max(rows.length, needRows),
        Math.max(curCols, needCols)
      );
    }
    for (let pr = 0; pr < block.length; pr++) {
      const src = block[pr];
      for (let pc = 0; pc < src.length; pc++) {
        if (!rows[r + pr]) rows[r + pr] = [];
        rows[r + pr][c + pc] = src[pc];
      }
    }
    if (CsvP.isTooLarge(rows)) {
      await showAlert(CsvP.tooLargeMessage(rows), 'CSV editor');
      return;
    }
    csvSession.rows = rows;
    csvSession.selAnchor = { r: r, c: c };
    csvSession.selFocus = {
      r: r + block.length - 1,
      c: c + Math.max(0, maxW - 1),
    };
    renderCsvGrid();
    recomputeCsvDirty();
    csvGoToCell(csvSession.selFocus.r, csvSession.selFocus.c);
  }

  function showCsvPasteModal() {
    return new Promise(function (resolve) {
      if (!csvPasteModal || !csvPasteModalInput) {
        resolve(null);
        return;
      }
      csvPasteModalInput.value = '';
      csvPasteResolve = resolve;
      csvPasteModal.showModal();
      window.setTimeout(function () {
        csvPasteModalInput.focus();
      }, 0);
    });
  }

  let csvPasteResolve = null;

  function initCsvPasteModal() {
    if (!csvPasteModal || !csvPasteModalForm || !csvPasteModalInput) return;
    csvPasteModalForm.addEventListener('submit', function (e) {
      e.preventDefault();
      const value = csvPasteModalInput.value;
      csvPasteModal.close();
      const resolve = csvPasteResolve;
      csvPasteResolve = null;
      if (resolve) resolve(value);
    });
    if (csvPasteModalCancel) {
      csvPasteModalCancel.addEventListener('click', function () {
        csvPasteModal.close();
        const resolve = csvPasteResolve;
        csvPasteResolve = null;
        if (resolve) resolve(null);
      });
    }
    csvPasteModal.addEventListener('cancel', function (e) {
      e.preventDefault();
      csvPasteModal.close();
      const resolve = csvPasteResolve;
      csvPasteResolve = null;
      if (resolve) resolve(null);
    });
  }

  async function csvPasteAt(r, c) {
    if (!csvSession) return;
    let text = '';
    if (navigator.clipboard && navigator.clipboard.readText) {
      try {
        text = await navigator.clipboard.readText();
      } catch (_e) {
        text = '';
      }
    }
    if (!text) {
      const manual = await showCsvPasteModal();
      if (manual == null || manual === '') return;
      text = manual;
    }
    await csvApplyPasteBlock(r, c, text);
  }

  async function csvPasteFromBar() {
    if (!csvSession) return;
    const anchor = csvPasteAnchorCell();
    await csvPasteAt(anchor.r, anchor.c);
  }

  function sessColCount(rows) {
    return rows[0] ? rows[0].length : 1;
  }

  function renderCsvGrid() {
    if (!csvSession || !csvGridHost) return;
    const sess = csvSession;
    CsvG.renderGrid(csvGridHost, sess.rows, sess.savedRows, {
      headerRow: sess.headerRow,
      getSelection: function () {
        return { anchor: sess.selAnchor, focus: sess.selFocus };
      },
      isMultiCellSelection: function () {
        return CsvG.isMultiCellSelection(sess.selAnchor, sess.selFocus);
      },
      isSelectExtend: function () {
        return csvSelectExtend;
      },
      onCellSelect: function (r, c) {
        sess.selAnchor = { r: r, c: c };
        sess.selFocus = { r: r, c: c };
        applyCsvSelectionVisuals();
      },
      onCellExtendSelect: function (r, c) {
        if (!sess.selAnchor) sess.selAnchor = { r: r, c: c };
        sess.selFocus = { r: r, c: c };
        applyCsvSelectionVisuals();
      },
      onCopy: function () {
        csvCopySelection();
      },
      onPaste: function (r, c) {
        void csvPasteAt(r, c);
      },
      onPasteText: function (r, c, text) {
        void csvApplyPasteBlock(r, c, text);
      },
      onRowHeaderSelect: function (r, shift) {
        csvSelectRow(r, shift);
      },
      onColHeaderSelect: function (c, shift) {
        csvSelectCol(c, shift);
      },
      onCellChange: function (r, c, val) {
        if (!sess.rows[r]) sess.rows[r] = [];
        sess.rows[r][c] = val;
        recomputeCsvDirty();
        updateCsvSelectionStats();
        if (csvSearchInput && csvSearchInput.value.trim()) recomputeCsvSearchMatches();
      },
      onGrowRow: function () {
        const cols = sess.rows[0] ? sess.rows[0].length : 1;
        const line = [];
        for (let i = 0; i < cols; i++) line.push('');
        sess.rows.push(line);
        if (CsvP.isTooLarge(sess.rows)) {
          showAlert(CsvP.tooLargeMessage(sess.rows), 'CSV editor');
          sess.rows.pop();
          return;
        }
        renderCsvGrid();
        updateCsvBarMeta();
        recomputeCsvDirty();
      },
      onCellFocus: function (r, c) {
        csvSearchAnchor = { r: r, c: c };
        setCsvPosFieldFromCell(r, c);
        if (csvSearchInput && csvSearchInput.value.trim()) {
          csvSearchMatchIndex = CsvG.matchIndexForAnchor(csvSearchMatches, r, c, csvColCount());
          updateCsvSearchCountLabel();
          applyCsvSearchVisuals();
        }
      },
      onGrowCol: function () {
        sess.rows.forEach(function (row) {
          row.push('');
        });
        if (CsvP.isTooLarge(sess.rows)) {
          showAlert(CsvP.tooLargeMessage(sess.rows), 'CSV editor');
          sess.rows.forEach(function (row) {
            row.pop();
          });
          return;
        }
        renderCsvGrid();
        updateCsvBarMeta();
        recomputeCsvDirty();
      },
    });
    updateCsvBarMeta();
    recomputeCsvSearchMatches();
    if (csvSession.rows.length) setCsvPosFieldFromCell(0, 0);
  }

  function enterCsvTableMode() {
    if (!csvSession) return;
    editorViewMode = 'csv';
    if (csvTableBar) csvTableBar.hidden = false;
    setEditorSurfaceMode('csv');
    renderCsvGrid();
    if (CsvP.isGridEmpty(csvSession.rows)) {
      showCsvHelpBanner('empty');
    } else {
      showCsvHelpBanner('none');
    }
    updateCsvToggleButton();
    updateEditorHeader();
    updateEditorEmptyState();
    updateUndoRedoButtons();
  }

  function enterCsvFromProject(path, content) {
    leaveCsvEditor();
    leaveJsonlEditor();
    const preferTab = path.toLowerCase().endsWith('.tsv');
    const raw = typeof content === 'string' ? content : '';
    const savedProjectValue = raw;
    const encoding = CsvP.encodingFromProjectContent(raw);
    const text = CsvP.projectTextFromContent(raw);
    const parsed = CsvP.parseFile(text, { preferTab: preferTab, autoDetect: true, quoteChar: '"' });
    csvSession = {
      path: path,
      rows: parsed.rows,
      savedRows: JSON.parse(JSON.stringify(parsed.rows)),
      delimiter: parsed.delimiter,
      lineEnding: parsed.lineEnding,
      quoteChar: parsed.quoteChar || '"',
      quoteAllFields: false,
      autoDetectDelimiter: true,
      encoding: encoding,
      preferTab: preferTab,
      savedSerialized: text,
      savedProjectValue: savedProjectValue,
      headerRow: false,
      gridForced: false,
      selAnchor: { r: 0, c: 0 },
      selFocus: { r: 0, c: 0 },
    };
    const tooLarge = CsvP.isTooLarge(parsed.rows);
    updateCsvToggleButton();
    if (tooLarge) {
      suppressChange = true;
      loadTextEditorForPath(path, text);
      suppressChange = false;
      showCsvHelpBanner('too-large', parsed.rows);
      updateEditorHeader();
      updateEditorEmptyState();
      updateUndoRedoButtons();
      return;
    }
    enterCsvTableMode();
    if (parsed.empty) showCsvHelpBanner('empty');
  }

  function saveCsvEditorToProject() {
    if (!csvSession || !hasOpenFile()) return;
    const path = currentOpenPath();
    let text = '';
    if (editorViewMode === 'csv') {
      text = csvSerializedNow();
    } else if (cm) {
      text = cm.getValue();
    }
    const stored = csvStoredProjectValue(text);
    project.files[path] = stored;
    csvSession.savedSerialized = text;
    csvSession.savedProjectValue = stored;
    if (editorViewMode === 'csv') {
      const reparsed = CsvP.parseFile(text, csvParseOptsFromSession());
      csvSession.rows = reparsed.rows;
      csvSession.quoteChar = reparsed.quoteChar || csvSession.quoteChar;
      snapshotCsvSavedRows();
    }
  }

  function applyCsvDelimiterFromUi(val) {
    if (!csvSession) return;
    const d = val === 'tab' ? '\t' : val;
    csvSession.autoDetectDelimiter = false;
    csvSession.delimiter = d;
    recomputeCsvDirty();
  }

  function applyCsvQuoteFromUi(val) {
    if (!csvSession) return;
    csvSession.quoteChar = val === "'" ? "'" : '"';
    recomputeCsvDirty();
  }

  function applyCsvEncoding(enc) {
    if (!csvSession) return;
    csvSession.encoding = CsvP.normalizeEncoding(enc);
    recomputeCsvDirty();
  }

  function syncCsvFormatModalFromSession() {
    if (!csvSession) return;
    if (csvFormatDelimiterSelect) {
      const d = csvSession.delimiter;
      csvFormatDelimiterSelect.value = d === '\t' ? 'tab' : d;
    }
    if (csvFormatQuoteSelect) {
      const q = csvSession.quoteChar || '"';
      csvFormatQuoteSelect.value = q === "'" ? "'" : '"';
    }
    if (csvFormatLineEndingSelect) {
      csvFormatLineEndingSelect.value = Txt.lineEndingKeyFromValue(
        csvSession.lineEnding || '\n'
      );
    }
    if (csvFormatEncodingSelect) {
      const enc = CsvP.normalizeEncoding(csvSession.encoding);
      csvFormatEncodingSelect.value =
        CsvP.ENCODINGS.indexOf(enc) >= 0 ? enc : 'utf-8';
    }
    if (csvFormatAutoDelimiterBtn) {
      const on = !!csvSession.autoDetectDelimiter;
      csvFormatAutoDelimiterBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
      csvFormatAutoDelimiterBtn.classList.toggle('fleditor-csv-header-btn--active', on);
    }
    if (csvFormatDelimiterSelect) {
      csvFormatDelimiterSelect.disabled = !!csvSession.autoDetectDelimiter;
    }
    if (csvFormatQuoteAllBtn) {
      const on = !!csvSession.quoteAllFields;
      csvFormatQuoteAllBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
      csvFormatQuoteAllBtn.classList.toggle('fleditor-csv-header-btn--active', on);
    }
  }

  function applyCsvLineEndingFromUi(key) {
    if (!csvSession) return;
    csvSession.lineEnding = Txt.lineEndingFromKey(key);
    recomputeCsvDirty();
  }

  function applyCsvFormatFromModal() {
    if (!csvSession) return;
    if (csvFormatQuoteSelect) {
      applyCsvQuoteFromUi(csvFormatQuoteSelect.value);
    }
    const autoOn =
      csvFormatAutoDelimiterBtn &&
      csvFormatAutoDelimiterBtn.getAttribute('aria-pressed') === 'true';
    if (autoOn) {
      csvSession.autoDetectDelimiter = true;
      const body = csvLogicalTextNow();
      const parsed = CsvP.parseFile(body, csvParseOptsFromSession());
      csvSession.delimiter = parsed.delimiter;
    } else if (csvFormatDelimiterSelect) {
      applyCsvDelimiterFromUi(csvFormatDelimiterSelect.value);
    }
    if (csvFormatLineEndingSelect) {
      applyCsvLineEndingFromUi(csvFormatLineEndingSelect.value);
    }
    if (csvFormatEncodingSelect) {
      applyCsvEncoding(csvFormatEncodingSelect.value);
    }
    if (csvFormatQuoteAllBtn) {
      csvSession.quoteAllFields =
        csvFormatQuoteAllBtn.getAttribute('aria-pressed') === 'true';
    }
    recomputeCsvDirty();
  }

  function syncTextFormatModalFromSession() {
    if (!textSession) return;
    if (textFormatLineEndingSelect) {
      textFormatLineEndingSelect.value = Txt.lineEndingKeyFromValue(textSession.lineEnding);
    }
    if (textFormatEncodingSelect) {
      const enc = Txt.normalizeEncoding(textSession.encoding);
      const encs = Txt.ENCODINGS();
      textFormatEncodingSelect.value = encs.indexOf(enc) >= 0 ? enc : 'utf-8';
    }
  }

  function applyTextFormatFromModal() {
    if (!textSession) return;
    if (textFormatLineEndingSelect) {
      textSession.lineEnding = Txt.lineEndingFromKey(textFormatLineEndingSelect.value);
    }
    if (textFormatEncodingSelect) {
      textSession.encoding = Txt.normalizeEncoding(textFormatEncodingSelect.value);
    }
    recomputeTextDirty();
  }

  function openTextFormatDialog() {
    if (!textSession || !textFormatModal) return;
    syncTextFormatModalFromSession();
    textFormatModal.showModal();
    if (textFormatLineEndingSelect) textFormatLineEndingSelect.focus();
  }

  function openCsvFormatDialog() {
    if (!csvSession || !csvFormatModal) return;
    syncCsvFormatModalFromSession();
    csvFormatModal.showModal();
    if (csvFormatDelimiterSelect) {
      csvFormatDelimiterSelect.focus();
    }
  }

  function openCsvTableSizeDialog() {
    if (!csvSession || !csvTableSizeModal || !csvTableSizeRowsInput || !csvTableSizeColsInput) return;
    const t = CsvP.tableSize(csvSession.rows);
    csvTableSizeRowsInput.value = String(t.rows);
    csvTableSizeColsInput.value = String(t.cols);
    csvTableSizeModal.showModal();
    csvTableSizeRowsInput.focus();
    csvTableSizeRowsInput.select();
  }

  async function applyCsvTableSizeFromInputs() {
    if (!csvSession || editorViewMode !== 'csv') return false;
    const t = CsvP.tableSize(csvSession.rows);
    const r = Math.max(1, parseInt(csvTableSizeRowsInput.value, 10) || 1);
    const c = Math.max(1, parseInt(csvTableSizeColsInput.value, 10) || 1);
    if (r > CsvP.MAX_ROWS || c > CsvP.MAX_COLS) {
      await showAlert('Too large (' + r + ' rows, ' + c + ' columns). Open as text.', 'CSV editor');
      return false;
    }
    const shrunk = r < t.rows || c < t.cols;
    let hasData = false;
    if (shrunk) {
      for (let y = r; y < csvSession.rows.length && !hasData; y++) {
        for (let x = 0; x < (csvSession.rows[y] || []).length; x++) {
          if (csvSession.rows[y][x]) hasData = true;
        }
      }
      for (let y = 0; y < Math.min(r, csvSession.rows.length) && !hasData; y++) {
        for (let x = c; x < (csvSession.rows[y] || []).length; x++) {
          if (csvSession.rows[y][x]) hasData = true;
        }
      }
    }
    if (hasData) {
      const ok = await showPrompt({
        title: 'Shrink table',
        message: 'Shrink will delete cells. Type yes to continue:',
        defaultValue: '',
      });
      if (ok !== 'yes') return false;
    }
    csvSession.rows = CsvP.resizeGrid(csvSession.rows, r, c);
    renderCsvGrid();
    recomputeCsvDirty();
    return true;
  }

  function doToggleCsvView() {
    if (!csvSession) return;
    if (editorViewMode === 'csv') {
      const text = csvSerializedNow();
      suppressChange = true;
      loadTextEditorForPath(csvSession.path, text);
      suppressChange = false;
      if (csvTableBar) csvTableBar.hidden = true;
      showCsvHelpBanner('none');
      recomputeCsvDirty();
    } else {
      const raw = cm ? cm.getValue() : '';
      const parsed = CsvP.parseFile(raw, csvParseOptsFromSession());
      if (CsvP.isTooLarge(parsed.rows) && !csvSession.gridForced) {
        showAlert(CsvP.tooLargeMessage(parsed.rows), 'CSV editor');
        return;
      }
      csvSession.rows = CsvP.ensureMinGrid(parsed.rows);
      csvSession.delimiter = parsed.delimiter;
      csvSession.lineEnding = parsed.lineEnding;
      enterCsvTableMode();
      recomputeCsvDirty();
    }
    updateCsvToggleButton();
    updateEditorHeader();
  }

  function toggleCsvView() {
    if (!csvSession) return;
    confirmDiscardIfDirty(doToggleCsvView);
  }

  function openCsvTableDespiteSize() {
    if (!csvSession) return;
    confirmDiscardIfDirty(function () {
      if (editorViewMode === 'plaintext' && cm) {
        const parsed = CsvP.parseFile(cm.getValue(), csvParseOptsFromSession());
        csvSession.rows = CsvP.ensureMinGrid(parsed.rows);
      }
      csvSession.gridForced = true;
      enterCsvTableMode();
      recomputeCsvDirty();
      updateCsvToggleButton();
    });
  }

  function saveBinaryEditorToProject() {
    const path = currentOpenPath();
    project.files[path] = P.bytesToProjectValueBinary(binaryBuffer);
    binarySavedSnapshot = H.copyBytes(binaryBuffer);
    return true;
  }

  function runEntryPath() {
    const open = currentOpenPath();
    if (open && P.isFlatlProjectFile(open)) {
      return open;
    }
    const paths = P.listFilePaths(project);
    if (paths.includes(P.DEFAULT_MAIN)) {
      return P.DEFAULT_MAIN;
    }
    for (let i = 0; i < paths.length; i++) {
      if (P.isFlatlProjectFile(paths[i])) {
        return paths[i];
      }
    }
    return open || P.DEFAULT_MAIN;
  }

  function loadTextEditorForPath(norm, content) {
    editorViewMode = P.isFlatlProjectFile(norm) ? 'flatl' : 'plaintext';
    setEditorSurfaceMode('text');
    cm.setOption('mode', editorViewMode === 'flatl' ? 'flatl' : 'text/plain');
    if (editorViewMode === 'flatl') {
      leaveTextSession();
      cm.setValue(typeof content === 'string' ? content : '');
      cm.clearHistory();
    } else if (isCsvSessionActive() && isCsvPath(norm)) {
      leaveTextSession();
      cm.setValue(typeof content === 'string' ? content : '');
      cm.clearHistory();
    } else if (isJsonlSessionActive() && isJsonlPath(norm)) {
      leaveTextSession();
      cm.setValue(typeof content === 'string' ? content : '');
      cm.clearHistory();
    } else {
      enterTextFromProject(norm, typeof content === 'string' ? content : '');
    }
    const wrap = cm.getWrapperElement();
    if (wrap) {
      wrap.style.display = '';
      wrap.style.visibility = 'visible';
    }
  }

  function updateEditorHeader() {
    if (editorKindLabel) {
      if (!hasOpenFile()) {
        editorKindLabel.textContent = 'Editor';
      } else if (editorViewMode === 'binary') {
        editorKindLabel.textContent = 'Hex editor';
      } else if (editorViewMode === 'csv') {
        editorKindLabel.textContent = 'CSV editor';
      } else if (editorViewMode === 'jsonl') {
        editorKindLabel.textContent = 'Jsonl editor';
      } else if (editorViewMode === 'flatl') {
        editorKindLabel.textContent = 'Fl editor';
      } else if (isCsvSessionActive()) {
        editorKindLabel.textContent = 'CSV editor (text)';
      } else if (isJsonlSessionActive()) {
        editorKindLabel.textContent = 'Jsonl editor (text)';
      } else {
        editorKindLabel.textContent = 'Text editor';
      }
    }
    if (codeMapBtn) {
      const showCodeMap = hasOpenFile() && editorViewMode === 'flatl';
      codeMapBtn.hidden = !showCodeMap;
      codeMapBtn.disabled = !showCodeMap;
    }
    if (textFormatBtn) {
      const showTextFormat = isTextFormatEditorActive();
      textFormatBtn.hidden = !showTextFormat;
    }
    if (!editorPathLabel) {
      return;
    }
    if (!hasOpenFile()) {
      editorPathLabel.textContent = 'No file open';
      return;
    }
    const path = currentOpenPath();
    const prefix = dirty ? '(not saved) ' : '';
    editorPathLabel.textContent = prefix + path;
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
      if (wrap && editorViewMode !== 'binary') {
        wrap.style.visibility = hideEditor ? 'hidden' : 'visible';
        wrap.style.display = hideEditor ? 'none' : '';
      }
    }
    const grid = binaryHexGridEl();
    if (grid && editorViewMode === 'binary') {
      grid.hidden = noFiles || !hasOpenFile();
    }
    if (csvGridHost && editorViewMode === 'csv') {
      csvGridHost.hidden = noFiles || !hasOpenFile();
    }
    if (jsonlRecordsHost && editorViewMode === 'jsonl') {
      jsonlRecordsHost.hidden = noFiles || !hasOpenFile();
    }
    updateCsvToggleButton();
    updateJsonlToggleButton();
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
    if (editorViewMode === 'binary') {
      saveBinaryEditorToProject();
    } else if (isCsvSessionActive()) {
      saveCsvEditorToProject();
    } else if (isJsonlSessionActive()) {
      saveJsonlEditorToProject();
    } else if (textSession && textSession.path === path) {
      const stored = textStoredProjectValue(textLogicalBodyFromCm());
      project.files[path] = stored;
      textSession.savedProjectValue = stored;
    } else {
      project.files[path] = cm.getValue();
    }
    project.openPath = path;
  }

  function persistProject() {
    P.saveProject(project);
  }

  function saveCurrentFile() {
    if (!hasOpenFile()) {
      return;
    }
    if (editorViewMode === 'binary') {
      saveBinaryEditorToProject();
    } else if (isCsvSessionActive()) {
      saveCsvEditorToProject();
    } else if (isJsonlSessionActive()) {
      saveJsonlEditorToProject();
    } else {
      flushEditorToProject();
    }
    persistProject();
    setDirty(false);
    if (editorViewMode === 'binary') {
      renderBinaryGrid();
      updateBinaryBarMeta();
    } else if (editorViewMode === 'csv' && csvSession) {
      snapshotCsvSavedRows();
      renderCsvGrid();
    } else if (editorViewMode === 'jsonl' && jsonlSession) {
      renderJsonlRecords();
    }
    setStatus('saved');
    window.setTimeout(function () {
      if (!running && statusEl.textContent === 'saved') {
        setStatus('idle');
      }
    }, 1200);
  }

  function implicitSaveForRun() {
    if (editorViewMode === 'binary') {
      saveBinaryEditorToProject();
    } else if (isCsvSessionActive()) {
      saveCsvEditorToProject();
    } else if (isJsonlSessionActive()) {
      saveJsonlEditorToProject();
    } else {
      flushEditorToProject();
    }
    persistProject();
    setDirty(false);
  }

  function enterEmptyEditorState() {
    if (codeMapVisible) setCodeMapVisible(false);
    leaveBinaryEditor();
    leaveCsvEditor();
    leaveJsonlEditor();
    leaveTextSession();
    editorViewMode = 'flatl';
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
    if (codeMapVisible) setCodeMapVisible(false);
    const norm = P.resolveProjectFilePath(project, path);
    if (project.files[norm] == null) {
      return;
    }
    leaveBinaryEditor();
    leaveCsvEditor();
    leaveJsonlEditor();
    leaveTextSession();
    suppressChange = true;
    if (P.isBinaryProjectFile(norm, project.files[norm])) {
      const bytes = P.binaryProjectValueToBytes(project.files[norm]);
      suppressChange = false;
      enterBinaryEditorFromBytes(bytes);
    } else if (isCsvPath(norm)) {
      suppressChange = false;
      enterCsvFromProject(norm, project.files[norm]);
    } else if (isJsonlPath(norm)) {
      suppressChange = false;
      enterJsonlFromProject(norm, project.files[norm]);
    } else {
      loadTextEditorForPath(norm, project.files[norm]);
      suppressChange = false;
      updateEditorEmptyState();
      updateEditorHeader();
      updateUndoRedoButtons();
    }
    project.openPath = norm;
    setDirty(false);
    updateEditorEmptyState();
    updateEditorHeader();
    updateRunSaveButtons();
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

  function discardUnsavedEditorChanges() {
    if (!hasOpenFile()) {
      setDirty(false);
      return;
    }
    const path = currentOpenPath();
    if (editorViewMode === 'binary') {
      binaryBuffer = H.copyBytes(binarySavedSnapshot);
      setDirty(false);
      renderBinaryGrid();
      updateBinaryBarMeta();
      return;
    }
    if (isCsvSessionActive() && csvSession && csvSession.path === path) {
      const text = CsvP.projectTextFromContent(csvSession.savedProjectValue);
      const parsed = CsvP.parseFile(text, csvParseOptsFromSession());
      csvSession.rows = CsvP.ensureMinGrid(parsed.rows);
      csvSession.savedRows = JSON.parse(JSON.stringify(csvSession.rows));
      csvSession.delimiter = parsed.delimiter;
      csvSession.lineEnding = parsed.lineEnding;
      csvSession.quoteChar = parsed.quoteChar || csvSession.quoteChar;
      if (editorViewMode === 'csv') {
        renderCsvGrid();
      } else if (cm) {
        suppressChange = true;
        cm.setValue(text);
        cm.clearHistory();
        suppressChange = false;
      }
      setDirty(false);
      updateEditorHeader();
      updateUndoRedoButtons();
      return;
    }
    if (isJsonlSessionActive() && jsonlSession && jsonlSession.path === path) {
      const text = jsonlSession.savedSerialized;
      jsonlSession.lines = Jp.splitLines(text);
      jsonlSession.lineEnding = Jp.detectLineEnding(text);
      jsonlSession.draftDirty = false;
      jsonlSession.expand = {};
      if (jsonlSession.selected != null && jsonlSession.selected >= jsonlSession.lines.length) {
        jsonlSession.selected = jsonlSession.lines.length ? jsonlSession.lines.length - 1 : null;
      }
      if (editorViewMode === 'jsonl') {
        if (jsonlSession.selected != null) loadJsonlSelectedIntoPanel();
        else jsonlSession.panelMode = 'none';
        renderJsonlRecords();
      } else if (cm) {
        suppressChange = true;
        cm.setValue(text);
        cm.clearHistory();
        suppressChange = false;
      }
      setDirty(false);
      updateEditorHeader();
      updateUndoRedoButtons();
      return;
    }
    if (textSession && textSession.path === path && editorViewMode === 'plaintext') {
      const body = Txt.projectTextFromContent(textSession.savedProjectValue);
      const forCm = Txt.textToCmNewlines(body);
      suppressChange = true;
      cm.setValue(forCm);
      cm.clearHistory();
      suppressChange = false;
      setDirty(false);
      updateEditorHeader();
      updateUndoRedoButtons();
      return;
    }
    const saved = project.files[path];
    suppressChange = true;
    cm.setValue(typeof saved === 'string' ? saved : '');
    cm.clearHistory();
    suppressChange = false;
    setDirty(false);
    updateEditorHeader();
    updateUndoRedoButtons();
  }

  function confirmDiscardIfDirty(onContinue) {
    if (!dirty) {
      onContinue();
      return;
    }
    if (!unsavedDialog) {
      discardUnsavedEditorChanges();
      onContinue();
      return;
    }
    unsavedContinue = function () {
      discardUnsavedEditorChanges();
      onContinue();
    };
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
    const norm = P.resolveProjectFilePath(project, path);
    if (project.files[norm] == null) {
      const hint = P.listFilePaths(project)
        .filter(function (p) {
          return p.split('/').pop() === P.normalizePath(path).split('/').pop();
        })
        .join(', ');
      const msg = hint
        ? 'File not in project: ' + path + ' (try: ' + hint + ')'
        : 'File not in project: ' + path;
      void showAlert(msg, 'Open file');
      return;
    }
    const switching = norm !== currentOpenPath();
    if (switching && hasOpenFile() && dirty) {
      confirmDiscardIfDirty(function () {
        persistProject();
        loadFileIntoEditor(norm);
      });
      return;
    }
    if (switching && hasOpenFile()) {
      flushEditorToProject();
      persistProject();
    }
    loadFileIntoEditor(norm);
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
          label.classList.add('fleditor-tree-row-label--dir');
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
          row.dataset.path = node.path;
          const spacer = document.createElement('span');
          spacer.className = 'fleditor-tree-toggle-spacer';
          spacer.setAttribute('aria-hidden', 'true');
          row.appendChild(spacer);
          if (node.path === open) {
            row.classList.add('fleditor-tree-row--active');
          }
          const kind = P.projectFileEditorKind(node.path, project.files[node.path]);
          label.textContent = node.name;
          label.classList.add('fleditor-tree-row-label--' + kind);
          row.tabIndex = 0;
          row.appendChild(label);
          if (node.path === open && dirty) {
            const dirtyMark = document.createElement('span');
            dirtyMark.className = 'fleditor-tree-dirty';
            dirtyMark.textContent = '●';
            dirtyMark.title = 'Unsaved changes';
            dirtyMark.setAttribute('aria-hidden', 'true');
            row.appendChild(dirtyMark);
          }
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

  function initFileTreeOpen() {
    if (!fileTreeEl || fileTreeEl.dataset.openBound === '1') {
      return;
    }
    fileTreeEl.dataset.openBound = '1';
    fileTreeEl.addEventListener('click', function (e) {
      const row = e.target.closest('.fleditor-tree-row--file');
      if (!row || e.target.closest('.fleditor-tree-delete')) {
        return;
      }
      const path = row.dataset.path;
      if (path) {
        openFile(path);
      }
    });
    fileTreeEl.addEventListener('keydown', function (e) {
      if (e.key !== 'Enter' && e.key !== ' ') {
        return;
      }
      const row = e.target.closest('.fleditor-tree-row--file');
      if (!row || e.target.closest('.fleditor-tree-delete')) {
        return;
      }
      e.preventDefault();
      const path = row.dataset.path;
      if (path) {
        openFile(path);
      }
    });
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

  let newFileModalResolve = null;

  function initNewFileModal() {
    const dialog = document.getElementById('newFileModal');
    const form = document.getElementById('newFileModalForm');
    const pathInput = document.getElementById('newFileModalPath');
    const binaryCheck = document.getElementById('newFileModalBinary');
    const cancelBtn = document.getElementById('newFileModalCancel');
    if (!dialog || !form || !pathInput || !binaryCheck) {
      return;
    }
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      const raw = pathInput.value;
      const isBinary = binaryCheck.checked;
      const resolve = newFileModalResolve;
      newFileModalResolve = null;
      dialog.close();
      if (resolve) {
        window.requestAnimationFrame(function () {
          resolve({ path: raw, binary: isBinary });
        });
      }
    });
    cancelBtn.addEventListener('click', function () {
      dialog.close();
      const resolve = newFileModalResolve;
      newFileModalResolve = null;
      if (resolve) {
        resolve(null);
      }
    });
    dialog.addEventListener('cancel', function (e) {
      e.preventDefault();
      dialog.close();
      const resolve = newFileModalResolve;
      newFileModalResolve = null;
      if (resolve) {
        resolve(null);
      }
    });
  }

  function showNewFileModal() {
    return new Promise(function (resolve) {
      const dialog = document.getElementById('newFileModal');
      const pathInput = document.getElementById('newFileModalPath');
      const binaryCheck = document.getElementById('newFileModalBinary');
      if (!dialog || !pathInput || !binaryCheck) {
        resolve(null);
        return;
      }
      pathInput.value = 'main.fl';
      binaryCheck.checked = false;
      newFileModalResolve = resolve;
      dialog.showModal();
      pathInput.focus();
      pathInput.select();
    });
  }

  function initialTextContentForPath(path) {
    if (path.endsWith('.fl')) {
      return '#main\n  OK\n';
    }
    return '';
  }

  async function onNewFile() {
    const choice = await showNewFileModal();
    if (!choice) {
      return;
    }
    const norm = P.normalizePath(choice.path);
    if (!norm) {
      await showAlert('Invalid path.', 'New file');
      return;
    }
    if (project.files[norm]) {
      await showAlert('File already exists.', 'New file');
      return;
    }
    if (hasOpenFile() && dirty) {
      flushEditorToProject();
      dirty = false;
    }
    P.ensureParentDirs(project, norm);
    if (choice.binary) {
      project.files[norm] = P.bytesToProjectValueBinary(H.newEmptyBinaryBuffer());
    } else {
      project.files[norm] = initialTextContentForPath(norm);
    }
    expandAncestorDirsForPath(norm);
    persistProject();
    loadFileIntoEditor(norm);
    renderFileTree();
    window.requestAnimationFrame(function () {
      renderFileTree();
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
    if (editorViewMode === 'binary' || editorViewMode === 'csv' || editorViewMode === 'jsonl') {
      undoBtn.disabled = true;
      redoBtn.disabled = true;
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
    ioLineBuffer = '';
    if (ioDialog && ioDialog.open) {
      ioDialog.close();
    }
    setStatus('running');
    if (wake) wake(line, status);
  }

  function submitTerminalLine() {
    if (!ioPendingWake || ioCharMode) {
      return;
    }
    if (term) {
      term.write('\r\n');
    }
    const line = ioLineBuffer;
    ioLineBuffer = '';
    if (!ioAllowEmpty && line.trim() === '') {
      finishIoWake('', IO_PANIC_CANCEL);
      return;
    }
    finishIoWake(line, IO_OK);
  }

  function programInputUsesTerminal() {
    const prefs = FleditorSettings.getTerminalPrefs();
    return prefs.programInput !== 'modal';
  }

  function cancelTerminalIoInput() {
    ioLineBuffer = '';
    if (term) {
      term.write('\r\n');
    }
    finishIoWake('', IO_PANIC_CANCEL);
  }

  function handleTerminalIoData(data) {
    if (!programInputUsesTerminal() || !ioPendingWake || !term) {
      return;
    }

    if (data === '\x1b') {
      cancelTerminalIoInput();
      return;
    }

    if (ioCharMode) {
      if (data === '\x03') {
        term.write('^C\r\n');
        finishIoWake('', IO_PANIC_EOF);
        return;
      }
      if (data === '\r' || data === '\n') {
        finishIoWake('', IO_PANIC_CANCEL);
        return;
      }
      if (data.charCodeAt(0) === 27) {
        cancelTerminalIoInput();
        return;
      }
      const ch = data.charAt(0);
      if (!ch) {
        return;
      }
      term.write(ch);
      finishIoWake(ch, IO_OK);
      return;
    }

    let i = 0;
    while (i < data.length) {
      const ch = data[i];
      if (ch === '\r') {
        i += 1;
        if (i < data.length && data[i] === '\n') {
          i += 1;
        }
        submitTerminalLine();
        return;
      }
      if (ch === '\n') {
        i += 1;
        submitTerminalLine();
        return;
      }
      if (ch === '\x03') {
        term.write('^C\r\n');
        ioLineBuffer = '';
        finishIoWake('', IO_PANIC_EOF);
        return;
      }
      if (ch === '\x7f' || ch === '\b') {
        if (ioLineBuffer.length > 0) {
          ioLineBuffer = ioLineBuffer.slice(0, -1);
          term.write('\b \b');
        }
        i += 1;
        continue;
      }
      if (ch === '\x1b') {
        const seqStart = i + 1;
        i += 1;
        while (i < data.length && !/[A-Za-z~]/.test(data[i])) {
          i += 1;
        }
        if (i < data.length) {
          i += 1;
          continue;
        }
        if (seqStart >= data.length) {
          cancelTerminalIoInput();
          return;
        }
        continue;
      }
      if (ch.charCodeAt(0) < 32) {
        i += 1;
        continue;
      }
      ioLineBuffer += ch;
      term.write(ch);
      i += 1;
    }
  }

  function openTerminalIo(options) {
    if (!term) {
      options.wake('', IO_PANIC_EOF);
      setStatus('running');
      return;
    }

    ioPendingWake = options.wake;
    ioAllowEmpty = !!options.allowEmpty;
    ioCharMode = !!options.charMode;
    ioLineBuffer = '';
    setTerminalOpen(true);
    scheduleLayoutRefresh();
    term.focus();
    if (options.charMode) {
      setStatus('type a character in the terminal');
    } else if (options.allowEmpty) {
      setStatus('waiting input in terminal (Enter, empty ok)');
    } else {
      setStatus('waiting input in terminal');
    }
  }

  function showProgramInputModal(options) {
    if (!ioDialog || !ioInput) {
      openTerminalIo(options);
      return;
    }

    ioPendingWake = options.wake;
    ioAllowEmpty = !!options.allowEmpty;
    ioCharMode = !!options.charMode;
    ioLineBuffer = '';
    if (ioTitle) {
      ioTitle.textContent = options.title || 'Program input';
    }
    if (ioHint) {
      ioHint.hidden = true;
      ioHint.textContent = '';
    }
    ioInput.value = '';
    ioInput.maxLength = options.charMode ? 8 : 8000;
    ioDialog.showModal();
    ioInput.focus();
    ioInput.select();
    if (options.charMode) {
      setStatus('waiting input (dialog)');
    } else if (options.allowEmpty) {
      setStatus('waiting input (dialog, empty ok)');
    } else {
      setStatus('waiting input (dialog)');
    }
  }

  function openProgramIo(options) {
    if (programInputUsesTerminal()) {
      openTerminalIo(options);
    } else {
      showProgramInputModal(options);
    }
  }

  function getCodeMapSelection() {
    if (!cm || !codeMapModel || !currentOpenPath()) return null;
    const cursor = cm.getCursor();
    const line = cursor.line + 1;
    const path = currentOpenPath();
    const block = codeMapModel.blocks.find(function (item) {
      return item.file === path && line >= item.startLine && line <= item.endLine;
    });
    return block ? block.id : null;
  }

  function setCodeMapVisible(visible) {
    if (!cm || !codeMapView || !codeMapBtn) return;
    codeMapVisible = !!visible;
    const wrapper = cm.getWrapperElement();
    wrapper.hidden = codeMapVisible;
    codeMapView.hidden = !codeMapVisible;
    codeMapBtn.textContent = codeMapVisible ? 'Editor' : 'Code Map';
    codeMapBtn.title = codeMapVisible ? 'Return to source editor' : 'Show block transfer map';
    if (codeMapVisible) {
      flushEditorToProject();
      const openPath = currentOpenPath();
      const mapEntry = openPath && /\.fl$/i.test(openPath)
        ? openPath
        : (project.files[P.DEFAULT_MAIN] != null
          ? P.DEFAULT_MAIN
          : Object.keys(project.files).find(function (path) { return /\.fl$/i.test(path) && typeof project.files[path] === 'string'; }));
      codeMapModel = FleditorBlockAnalyzer.analyze(project.files, mapEntry || P.DEFAULT_MAIN);
      FleditorCodeMap.render(codeMapView, codeMapModel, function (block) {
        navigateToBlock(block, block.startLine);
      }, function (edge) {
        const source = codeMapModel.blocks.find(function (block) { return block.id === edge.from; });
        if (source) navigateToBlock(source, edge.lines.length ? edge.lines[0] : source.startLine);
      });
      FleditorCodeMap.setSelectedNode(getCodeMapSelection(), true);
    } else {
      cm.refresh();
      cm.focus();
    }
  }

  function navigateToBlock(block, lineNumber) {
    if (!block || !block.file) return;
    if (codeMapVisible) setCodeMapVisible(false);
    if (currentOpenPath() !== block.file) openFile(block.file);
    if (currentOpenPath() !== block.file || editorViewMode !== 'flatl') return;
    const line = Math.max(0, Math.min(cm.lineCount() - 1, (lineNumber || block.startLine || 1) - 1));
    if (blockNavigationMark) {
      cm.removeLineClass(blockNavigationMark.line, 'background', 'cm-fl-block-target');
      blockNavigationMark = null;
    }
    cm.setCursor({ line: line, ch: 0 });
    cm.scrollIntoView({ line: line, ch: 0 }, 120);
    cm.addLineClass(line, 'background', 'cm-fl-block-target');
    blockNavigationMark = { line: line };
    window.setTimeout(function () {
      if (blockNavigationMark && blockNavigationMark.line === line) {
        cm.removeLineClass(line, 'background', 'cm-fl-block-target');
        blockNavigationMark = null;
      }
    }, 1400);
    cm.focus();
  }

  async function initEditor() {
    const hadProject = !!localStorage.getItem(P.PROJECT_KEY);
    const hadLegacy = !!localStorage.getItem(P.LEGACY_KEY);
    project = P.loadProject();
    if (!hadProject && !hadLegacy) {
      await P.hydrateProjectExamples(project);
      P.saveProject(project);
    }
    cm = CodeMirror.fromTextArea(document.getElementById('code'), {
      lineNumbers: true,
      theme: 'flatl-dark',
      mode: 'flatl',
      indentUnit: 2,
      tabSize: 2,
    });
    const open = currentOpenPath();
    if (open && project.files[open]) {
      loadFileIntoEditor(open);
    } else {
      cm.setValue(project.files[open] || P.DEFAULT_SOURCE);
      updateEditorHeader();
    }
    cm.on('change', function () {
      if (suppressChange) {
        return;
      }
      if (editorViewMode === 'binary') {
        return;
      }
      if (isCsvSessionActive()) {
        recomputeCsvDirty();
        updateUndoRedoButtons();
        return;
      }
      if (isJsonlSessionActive()) {
        recomputeJsonlDirty();
        updateUndoRedoButtons();
        return;
      }
      if (textSession && editorViewMode === 'plaintext') {
        recomputeTextDirty();
        updateUndoRedoButtons();
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
    initFileTreeOpen();
    renderFileTree();
    updateEditorHeader();
    updateEditorEmptyState();
  }

  function initTerminal() {
    const tokens = FleditorTheme.effectiveTokens(FleditorTheme.loadThemeState());
    const tPrefs = FleditorSettings.getTerminalPrefs();
    term = new Terminal({
      theme: FleditorTheme.xtermThemeFromTokens(tokens),
      fontFamily: 'ui-monospace, Menlo, Consolas, monospace',
      fontSize: tPrefs.fontSize,
      cursorBlink: true,
      convertEol: true,
    });
    const host = document.getElementById('terminalHost');
    term.open(host);
    host.addEventListener('mousedown', function () {
      term.focus();
    });
    term.onData(function (data) {
      handleTerminalIoData(data);
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
      flatWasmOnStderr: function (u8) {
        const text = new TextDecoder().decode(u8);
        if (term) term.write('\x1b[90m' + text + '\x1b[0m');
      },
      flatWasmOnNeedLine: function (allowEmpty, prompt, kind, wake) {
        const io = parseNeedLineHook(allowEmpty, prompt, kind, wake);
        openProgramIo({
          title: formatLineModalTitle(io.prompt, io.kind),
          allowEmpty: io.allowEmpty,
          charMode: false,
          wake: io.wake,
        });
      },
      flatWasmOnNeedChar: function (prompt, wake) {
        const io = parseNeedCharHook(prompt, wake);
        openProgramIo({
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
    Module.flatWasmOnStderr = cfg.flatWasmOnStderr;
    Module.flatWasmOnNeedLine = cfg.flatWasmOnNeedLine;
    Module.flatWasmOnNeedChar = cfg.flatWasmOnNeedChar;
    Module.locateFile = cfg.locateFile;
    return Module;
  }

  function getWasmToolchainVersion() {
    if (!wasmModule) {
      return null;
    }
    try {
      return wasmModule.ccall('flat_wasm_toolchain_version', 'string', [], []);
    } catch (_e) {
      return null;
    }
  }

  async function loadWasm() {
    if (wasmLoadPromise) {
      return wasmLoadPromise;
    }
    wasmLoadPromise = (async () => {
      if (typeof FlatLModule !== 'function') {
        try {
          /* bust browser cache when wasm toolchain is rebuilt */
          await loadScript('wasm/flatl.js?v=mem-nested1');
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

  function applyWasmRunLimits(Module) {
    const p = FleditorSettings.getTerminalPrefs();
    const maxJumps = (p.maxJumps >>> 0) || 0;
    const maxSteps = (p.maxSteps >>> 0) || 0;
    const noJump = p.noJumpLimit ? 1 : 0;
    try {
      if (typeof Module._flat_wasm_set_run_limits === 'function') {
        Module._flat_wasm_set_run_limits(maxJumps, maxSteps, noJump);
      }
    } catch (_e) {
      /* older wasm without export or signature mismatch */
    }
  }

  async function runProjectEntry(Module, entryPath) {
    const errBuf = Module._malloc(4096);
    Module.setValue(errBuf, 0, 'i8');
    attachWasmHooks(Module);
    applyWasmRunLimits(Module);
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

    let termination = FLAT_TERM.UNKNOWN;
    try {
      termination = Module.ccall('flat_wasm_get_last_termination', 'number', [], []);
    } catch (_e) {
      /* older wasm without export */
    }

    let panicDetail = '';
    try {
      panicDetail = Module.ccall('flat_wasm_get_last_panic', 'string', [], []) || '';
    } catch (_e2) {
      /* older wasm without export */
    }

    const logicalFail =
      runRc === 2 &&
      (termination === FLAT_TERM.FAIL || termination === FLAT_TERM.UNKNOWN);

    let compileFailed = false;
    if (runRc !== 0 && !logicalFail) {
      const msg = Module.UTF8ToString(errBuf);
      if (isCompileErrorMessage(msg)) {
        compileFailed = true;
        term.write('\r\n[compile/link error] ' + msg + '\r\n');
      } else if (msg) {
        const tag =
          termination === FLAT_TERM.PANIC
            ? 'panic'
            : termination === FLAT_TERM.ERROR
              ? 'error'
              : 'run';
        term.write('\r\n[' + tag + '] ' + msg + '\r\n');
      } else if (panicDetail) {
        term.write('\r\n[panic] ' + panicDetail + '\r\n');
      } else {
        term.write('\r\n[error] program failed (exit ' + runRc + ')\r\n');
      }
    }
    Module._free(errBuf);
    return {
      exitCode: runRc,
      compileFailed: compileFailed,
      termination: termination,
      panicDetail: panicDetail,
    };
  }

  async function onRun() {
    if (running) return;
    if (!hasOpenFile()) {
      return;
    }
    if (ioPendingWake || (ioDialog && ioDialog.open)) {
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
    if (FleditorSettings.getTerminalPrefs().clearOnRun) {
      term.clear();
    }
    let runOutcome = {
      exitCode: 1,
      compileFailed: false,
      termination: FLAT_TERM.ERROR,
      panicDetail: '',
    };
    try {
      implicitSaveForRun();
      P.syncToMemfs(wasmModule, project.files);
      const memfsBeforeRun = P.snapshotMemfsBytes(wasmModule);
      runOutcome = await runProjectEntry(wasmModule, runEntryPath());
      const importReport = P.syncMemfsChangesToProject(wasmModule, project, memfsBeforeRun);
      const importMsg = P.formatMemfsImportReport(importReport);
      if (importMsg) {
        P.saveProject(project);
        renderFileTree();
        term.write('\r\n' + importMsg + '\r\n');
      }
      if (hasOpenFile()) {
        loadFileIntoEditor(currentOpenPath());
      }
    } catch (e) {
      term.write('\r\n[error] ' + e.message + '\r\n');
      runOutcome = {
        exitCode: 1,
        compileFailed: false,
        termination: FLAT_TERM.ERROR,
        panicDetail: '',
      };
      setStatus('failed', true);
    } finally {
      if (runOutcome.compileFailed) {
        writeTerminalEpilog('-- compile failed');
      } else {
        writeTerminalEpilog(
          runFinishedEpilog(
            runOutcome.exitCode,
            runOutcome.termination,
            runOutcome.panicDetail
          )
        );
      }
      running = false;
      updateRunSaveButtons();
      ioPendingWake = null;
      setStatus('idle');
    }
  }

  function initPanelResizers() {
    /** @type {null | (() => void)} */
    let cancelActiveDrag = null;

    function releasePointerSafe(el, pointerId) {
      if (!el || typeof el.hasPointerCapture !== 'function') {
        return;
      }
      try {
        if (el.hasPointerCapture(pointerId)) {
          el.releasePointerCapture(pointerId);
        }
      } catch (_e) {
        /* ignore */
      }
    }

    function stopActiveDrag() {
      if (cancelActiveDrag) {
        cancelActiveDrag();
        cancelActiveDrag = null;
      }
    }

    function startPointerDrag(e, captureEl, onMove, onEnd) {
      stopActiveDrag();
      const pointerId = e.pointerId;
      if (captureEl && captureEl.setPointerCapture) {
        try {
          captureEl.setPointerCapture(pointerId);
        } catch (_err) {
          /* ignore */
        }
      }
      document.body.classList.add('fleditor-resize-active');
      if (captureEl) {
        captureEl.classList.add('fleditor-splitter--active');
      }

      function cleanup() {
        document.removeEventListener('pointermove', onPointerMove);
        document.removeEventListener('pointerup', onPointerUp);
        document.removeEventListener('pointercancel', onPointerUp);
        window.removeEventListener('blur', onWindowBlur);
        document.body.classList.remove('fleditor-resize-active');
        if (captureEl) {
          captureEl.classList.remove('fleditor-splitter--active');
        }
        releasePointerSafe(captureEl, pointerId);
        cancelActiveDrag = null;
      }

      function onPointerMove(ev) {
        if (ev.pointerId !== pointerId) {
          return;
        }
        if (ev.pointerType === 'mouse' && ev.buttons === 0) {
          onPointerUp(ev);
          return;
        }
        onMove(ev.clientX, ev.clientY);
      }

      function onPointerUp(ev) {
        if (ev.pointerId !== pointerId) {
          return;
        }
        cleanup();
        if (onEnd) {
          onEnd();
        }
      }

      function onWindowBlur() {
        cleanup();
        if (onEnd) {
          onEnd();
        }
      }

      document.addEventListener('pointermove', onPointerMove);
      document.addEventListener('pointerup', onPointerUp);
      document.addEventListener('pointercancel', onPointerUp);
      window.addEventListener('blur', onWindowBlur);
      cancelActiveDrag = cleanup;
    }

    function isInteractiveTarget(el) {
      return !!(el && el.closest && el.closest('button, a, input, textarea, select'));
    }

    if (projectPanelResize && mainLayout && projectPanel) {
      projectPanelResize.addEventListener('pointerdown', function (e) {
        if (e.button !== 0 || isStackLayout() || !layoutState.projectOpen) {
          return;
        }
        e.preventDefault();
        const mainRect = mainLayout.getBoundingClientRect();
        const maxW = Math.min(560, Math.floor(mainRect.width * 0.55));
        startPointerDrag(
          e,
          projectPanelResize,
          function (clientX) {
            const w = L.clampProjectWidthPx(Math.min(maxW, clientX - mainRect.left));
            layoutState.projectWidthPx = w;
            projectPanel.style.width = w + 'px';
            scheduleLayoutRefresh();
          },
          function () {
            persistLayout();
          }
        );
      });
    }

    if (editorPanelResize && mainLayout) {
      editorPanelResize.addEventListener('pointerdown', function (e) {
        if (e.button !== 0 || !isStackLayout() || isInteractiveTarget(e.target)) {
          return;
        }
        e.preventDefault();
        if (layoutState.terminalOpen) {
          captureStackTerminalHeightFromDom();
        }
        const lockedTerminalPx = layoutState.terminalOpen ? stackTerminalHeightPx() : null;
        const mainRect = mainLayout.getBoundingClientRect();
        startPointerDrag(
          e,
          editorPanelResize,
          function (_x, clientY) {
            const frac = L.clampStackProjectFraction((clientY - mainRect.top) / mainRect.height);
            layoutState.stackProjectFraction = frac;
            const stackPct = Math.round(frac * 1000) / 10;
            if (projectPanel) {
              projectPanel.style.flexBasis = stackPct + '%';
            }
            if (lockedTerminalPx != null && terminalPanel) {
              terminalPanel.style.flexBasis = lockedTerminalPx + 'px';
              terminalPanel.style.flexGrow = '0';
              terminalPanel.style.flexShrink = '0';
            }
            scheduleLayoutRefresh();
          },
          function () {
            persistLayout();
          }
        );
      });
    }

    if (terminalPanelResize && workPanel && terminalPanel) {
      terminalPanelResize.addEventListener('pointerdown', function (e) {
        if (e.button !== 0 || !layoutState.terminalOpen || isInteractiveTarget(e.target)) {
          return;
        }
        e.preventDefault();
        const stack = isStackLayout();
        startPointerDrag(
          e,
          terminalPanelResize,
          function (_x, clientY) {
            const workRect = workPanel.getBoundingClientRect();
            if (stack) {
              const px = L.clampStackTerminalHeightPx(workRect.bottom - clientY, workRect.height);
              layoutState.stackTerminalHeightPx = px;
              terminalPanel.style.flexBasis = px + 'px';
            } else {
              const frac = L.clampFraction((workRect.bottom - clientY) / workRect.height);
              layoutState.terminalFraction = frac;
              const pct = Math.round(frac * 1000) / 10;
              terminalPanel.style.flexBasis = pct + '%';
            }
            scheduleLayoutRefresh();
          },
          function () {
            persistLayout();
          }
        );
      });
    }

    if (typeof stackLayoutMq.addEventListener === 'function') {
      stackLayoutMq.addEventListener('change', function () {
        const nowStack = isStackLayout();
        syncLayoutModeTerminalSizing(!nowStack, nowStack);
        applyLayout();
      });
    } else if (typeof stackLayoutMq.addListener === 'function') {
      stackLayoutMq.addListener(function () {
        const nowStack = isStackLayout();
        syncLayoutModeTerminalSizing(!nowStack, nowStack);
        applyLayout();
      });
    }
  }

  function initLayoutControls() {
    applyLayout();
    initPanelResizers();
    if (projectPanelToggle) {
      projectPanelToggle.addEventListener('click', () => setProjectOpen(false));
    }
    if (terminalPanelToggle) {
      terminalPanelToggle.addEventListener('click', () => setTerminalOpen(false));
    }
    if (dockRestoreProject) {
      dockRestoreProject.addEventListener('click', () => setProjectOpen(true));
    }
    if (dockRestoreTerminal) {
      dockRestoreTerminal.addEventListener('click', () => setTerminalOpen(true));
    }
    if (dockResetLayoutBtn) {
      dockResetLayoutBtn.addEventListener('click', resetLayout);
    }
  }

  function binaryGoToByte(index) {
    if (!binaryBuffer) {
      return;
    }
    let i = index;
    if (binaryBuffer.length === 0) {
      i = 0;
    } else {
      if (i < 0) {
        i = 0;
      }
      if (i >= binaryBuffer.length) {
        i = binaryBuffer.length - 1;
      }
    }
    binarySearchAnchor = i;
    binaryHighlightOffset = i;
    binarySearchMatchIndex = binarySearchMatches.indexOf(i);
    updateBinarySearchCountLabel();
    setBinaryPosFieldFromGrid(i);
    const page = Math.floor(i / H.BYTES_PER_PAGE);
    showBinaryPage(page);
  }

  function binarySearchStep(direction) {
    if (!binaryBuffer || !binarySearchInput) {
      return;
    }
    const q = binarySearchInput.value;
    if (!q.trim()) {
      showAlert('Enter a search pattern', 'Hex editor');
      return;
    }
    const pat = H.parseSearchPattern(q);
    if (!pat.ok) {
      showAlert(pat.error || 'Invalid search', 'Hex editor');
      return;
    }
    binarySearchMatches = H.findAllMatches(binaryBuffer, pat.bytes);
    const list = binarySearchMatches;
    if (!list.length) {
      binarySearchMatchIndex = -1;
      updateBinarySearchCountLabel();
      return;
    }
    let idx;
    const cur = list.indexOf(binarySearchAnchor);
    if (direction === 'next') {
      if (cur >= 0) {
        idx = (cur + 1) % list.length;
      } else {
        idx = list.findIndex(function (m) {
          return m > binarySearchAnchor;
        });
        if (idx < 0) {
          idx = 0;
        }
      }
    } else if (cur >= 0) {
      idx = (cur - 1 + list.length) % list.length;
    } else {
      idx = -1;
      for (let i = list.length - 1; i >= 0; i--) {
        if (list[i] < binarySearchAnchor) {
          idx = i;
          break;
        }
      }
      if (idx < 0) {
        idx = list.length - 1;
      }
    }
    binarySearchMatchIndex = idx;
    binaryGoToByte(list[idx]);
    updateBinarySearchCountLabel();
  }

  function initJsonlBar() {
    if (jsonlViewToggleBtn) {
      jsonlViewToggleBtn.addEventListener('click', function () {
        toggleJsonlView();
      });
    }
    if (jsonlFreeBtn) {
      jsonlFreeBtn.addEventListener('click', function () {
        setJsonlPanelMode('free');
      });
    }
    if (jsonlLockBtn) {
      jsonlLockBtn.addEventListener('click', function () {
        setJsonlPanelMode('lock');
      });
    }
    if (jsonlWordWrapBtn) {
      jsonlWordWrapBtn.addEventListener('click', function () {
        jsonlWordWrapEnabled = !jsonlWordWrapEnabled;
        syncJsonlWordWrapUi();
      });
      syncJsonlWordWrapUi();
    }
    if (jsonlApplyBtn) {
      jsonlApplyBtn.addEventListener('click', function () {
        if (!jsonlSession || jsonlSession.selected == null) return;
        if (jsonlSession.panelMode === 'free') {
          commitJsonlFreeDraftToLines(true);
        }
        renderJsonlRecords();
        recomputeJsonlDirty();
      });
    }
    if (jsonlRevertBtn) {
      jsonlRevertBtn.addEventListener('click', function () {
        if (!jsonlSession || jsonlSession.selected == null) return;
        loadJsonlSelectedIntoPanel();
        renderJsonlRecords();
        recomputeJsonlDirty();
      });
    }
    if (jsonlFreeTa) {
      jsonlFreeTa.addEventListener('input', function () {
        if (!jsonlSession || jsonlSession.panelMode !== 'free') return;
        jsonlSession.draftText = jsonlFreeTa.value;
        jsonlSession.draftDirty = true;
        const parsed = Jp.parseLine(jsonlFreeTa.value);
        if (parsed.empty) {
          if (jsonlFreeHint) {
            jsonlFreeHint.hidden = false;
            jsonlFreeHint.textContent = 'no json detected';
          }
          if (jsonlFreeErr) jsonlFreeErr.hidden = true;
          jsonlFreeTa.classList.remove('is-bad');
        } else if (!parsed.ok) {
          if (jsonlFreeHint) jsonlFreeHint.hidden = true;
          if (jsonlFreeErr) {
            jsonlFreeErr.hidden = false;
            jsonlFreeErr.textContent =
              parsed.error + (parsed.errorPos != null ? ' (position ' + parsed.errorPos + ')' : '');
          }
          jsonlFreeTa.classList.add('is-bad');
        } else {
          if (jsonlFreeHint) jsonlFreeHint.hidden = true;
          if (jsonlFreeErr) jsonlFreeErr.hidden = true;
          jsonlFreeTa.classList.remove('is-bad');
        }
        recomputeJsonlDirty();
      });
    }
    function jsonlSetPage(page) {
      if (!jsonlSession) return;
      confirmDiscardIfDirty(function () {
        commitJsonlFreeDraftToLines(false);
        recomputeJsonlSearchMatches();
        const pc = Jp.pageCount(Math.max(1, jsonlListItemCount()));
        jsonlSession.pageIndex = Math.max(0, Math.min(page, pc - 1));
        ensureJsonlSelectionOnPage();
        loadJsonlSelectedIntoPanel();
        renderJsonlRecords();
      });
    }
    const pagePrev = document.getElementById('jsonlPagePrev');
    const pageNext = document.getElementById('jsonlPageNext');
    if (pagePrev) {
      pagePrev.addEventListener('click', function () {
        if (!jsonlSession) return;
        jsonlSetPage(jsonlSession.pageIndex - 1);
      });
    }
    if (pageNext) {
      pageNext.addEventListener('click', function () {
        if (!jsonlSession) return;
        jsonlSetPage(jsonlSession.pageIndex + 1);
      });
    }
    if (jsonlPageInput) {
      function commitPageInput() {
        if (!jsonlSession) return;
        const raw = String(jsonlPageInput.value || '').trim();
        const page = Number(raw);
        recomputeJsonlSearchMatches();
        const pageCount = Jp.pageCount(Math.max(1, jsonlListItemCount()));
        if (!/^\d+$/.test(raw) || !Number.isSafeInteger(page) || page < 1 || page > pageCount) {
          updateJsonlBarMeta();
          return;
        }
        jsonlSetPage(page - 1);
      }
      jsonlPageInput.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter') {
          ev.preventDefault();
          commitPageInput();
          jsonlPageInput.blur();
        } else if (ev.key === 'Escape') {
          updateJsonlBarMeta();
          jsonlPageInput.blur();
        }
      });
      jsonlPageInput.addEventListener('change', commitPageInput);
      jsonlPageInput.addEventListener('blur', updateJsonlBarMeta);
    }

    // Splitter between JSONL line list and detail (col on desktop, row on mobile).
    const jsonlListResize = document.getElementById('jsonlListResize');
    if (jsonlListResize && jsonlRecordsHost) {
      let pointerId = null;
      let hostRect = { left: 0, top: 0, width: 0, height: 0 };
      let stackMode = false;
      function jsonlStackMode() {
        return !!(window.matchMedia && window.matchMedia('(max-width: 720px)').matches);
      }
      function syncJsonlResizeAria() {
        const stack = jsonlStackMode();
        jsonlListResize.setAttribute('aria-orientation', stack ? 'horizontal' : 'vertical');
        document.body.style.cursor = '';
      }
      function resizeJsonlListTo(clientX, clientY) {
        if (stackMode) {
          const minPx = 80;
          const maxPx = Math.max(minPx, hostRect.height - 150);
          const px = Math.max(minPx, Math.min(maxPx, clientY - hostRect.top));
          const pct = Math.max(15, Math.min(65, (px / hostRect.height) * 100));
          jsonlRecordsHost.style.setProperty('--fl-jsonl-list-height', pct.toFixed(2) + '%');
          return;
        }
        const minPx = 120;
        const maxPx = Math.max(minPx, hostRect.width - 220);
        const px = Math.max(minPx, Math.min(maxPx, clientX - hostRect.left));
        const pct = Math.max(18, Math.min(70, (px / hostRect.width) * 100));
        jsonlRecordsHost.style.setProperty('--fl-jsonl-list-width', pct.toFixed(2) + '%');
      }
      jsonlListResize.addEventListener('pointerdown', function (ev) {
        if (ev.button !== 0) return;
        const rect = jsonlRecordsHost.getBoundingClientRect();
        hostRect = { left: rect.left, top: rect.top, width: rect.width, height: rect.height };
        if (!hostRect.width || !hostRect.height) return;
        stackMode = jsonlStackMode();
        pointerId = ev.pointerId;
        ev.preventDefault();
        try { jsonlListResize.setPointerCapture(pointerId); } catch (_e) { /* ignore */ }
        document.body.classList.add('fleditor-resize-active');
        document.body.style.cursor = stackMode ? 'ns-resize' : 'col-resize';
      });
      jsonlListResize.addEventListener('pointermove', function (ev) {
        if (pointerId === null || ev.pointerId !== pointerId) return;
        resizeJsonlListTo(ev.clientX, ev.clientY);
      });
      function stopJsonlResize(ev) {
        if (pointerId === null || (ev && ev.pointerId !== pointerId)) return;
        try { jsonlListResize.releasePointerCapture(pointerId); } catch (_e) { /* ignore */ }
        pointerId = null;
        document.body.classList.remove('fleditor-resize-active');
        document.body.style.cursor = '';
      }
      jsonlListResize.addEventListener('pointerup', stopJsonlResize);
      jsonlListResize.addEventListener('pointercancel', stopJsonlResize);
      jsonlListResize.addEventListener('keydown', function (ev) {
        const stack = jsonlStackMode();
        const vertKeys = ev.key === 'ArrowUp' || ev.key === 'ArrowDown';
        const horizKeys = ev.key === 'ArrowLeft' || ev.key === 'ArrowRight';
        if (stack ? !vertKeys : !horizKeys) return;
        ev.preventDefault();
        const rect = jsonlRecordsHost.getBoundingClientRect();
        const listEl = jsonlListResize.previousElementSibling;
        if (!listEl) return;
        if (stack) {
          const current = listEl.getBoundingClientRect().height;
          const minPx = 80;
          const maxPx = Math.max(minPx, rect.height - 150);
          const delta = ev.key === 'ArrowUp' ? -24 : 24;
          const px = Math.max(minPx, Math.min(maxPx, current + delta));
          jsonlRecordsHost.style.setProperty(
            '--fl-jsonl-list-height',
            Math.max(15, Math.min(65, (px / rect.height) * 100)).toFixed(2) + '%'
          );
        } else {
          const current = listEl.getBoundingClientRect().width;
          const minPx = 120;
          const maxPx = Math.max(minPx, rect.width - 220);
          const delta = ev.key === 'ArrowLeft' ? -24 : 24;
          const px = Math.max(minPx, Math.min(maxPx, current + delta));
          jsonlRecordsHost.style.setProperty(
            '--fl-jsonl-list-width',
            Math.max(18, Math.min(70, (px / rect.width) * 100)).toFixed(2) + '%'
          );
        }
      });
      syncJsonlResizeAria();
      if (window.matchMedia) {
        const mql = window.matchMedia('(max-width: 720px)');
        const onChange = function () { syncJsonlResizeAria(); };
        if (mql.addEventListener) mql.addEventListener('change', onChange);
        else if (mql.addListener) mql.addListener(onChange);
      }
    }
    if (jsonlFindModeBtn) {
      jsonlFindModeBtn.addEventListener('click', function () {
        if (!jsonlSession) return;
        cycleJsonlFindMode();
        applyJsonlFindModeToSelection();
        syncJsonlFindModeUi();
        renderJsonlRecords();
      });
      syncJsonlFindModeUi();
    }
    if (jsonlSearchInput) {
      jsonlSearchInput.addEventListener('input', function () {
        recomputeJsonlSearchMatches();
        if ((jsonlFindMode === 'filter' || jsonlFindMode === 'advfilter') && jsonlSession) {
          jsonlSession.pageIndex = 0;
          if (jsonlListIsNarrowed() && jsonlSearchMatches.length) {
            if (jsonlSession.selected == null || jsonlSearchMatches.indexOf(jsonlSession.selected) < 0) {
              jsonlSession.selected = jsonlSearchMatches[0];
              loadJsonlSelectedIntoPanel();
            }
          }
          renderJsonlRecords();
          return;
        }
        if (jsonlSession && jsonlSession.panelMode === 'lock') renderJsonlLockPanel();
        updateJsonlSearchCount();
      });
    }
    const searchPrev = document.getElementById('jsonlSearchPrev');
    const searchNext = document.getElementById('jsonlSearchNext');
    if (searchPrev) searchPrev.addEventListener('click', function () { gotoJsonlSearchMatch(-1); });
    if (searchNext) searchNext.addEventListener('click', function () { gotoJsonlSearchMatch(1); });
    const helpDismiss = document.getElementById('jsonlHelpDismiss');
    if (helpDismiss) {
      helpDismiss.addEventListener('click', function () {
        if (jsonlHelpBanner) jsonlHelpBanner.hidden = true;
      });
    }
    if (jsonlTooLargeOpenRecords) {
      jsonlTooLargeOpenRecords.addEventListener('click', function () {
        openJsonlRecordsDespiteSize();
      });
    }
  }

  function initCsvBar() {
    if (csvViewToggleBtn) {
      csvViewToggleBtn.addEventListener('click', function () {
        toggleCsvView();
      });
    }
    if (csvFormatQuoteAllBtn) {
      csvFormatQuoteAllBtn.addEventListener('click', function () {
        const on = csvFormatQuoteAllBtn.getAttribute('aria-pressed') !== 'true';
        csvFormatQuoteAllBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
        csvFormatQuoteAllBtn.classList.toggle('fleditor-csv-header-btn--active', on);
      });
    }
    if (csvFormatAutoDelimiterBtn) {
      csvFormatAutoDelimiterBtn.addEventListener('click', function () {
        const on = csvFormatAutoDelimiterBtn.getAttribute('aria-pressed') !== 'true';
        csvFormatAutoDelimiterBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
        csvFormatAutoDelimiterBtn.classList.toggle('fleditor-csv-header-btn--active', on);
        if (csvFormatDelimiterSelect) {
          csvFormatDelimiterSelect.disabled = on;
        }
      });
    }
    if (csvFormatDelimiterSelect) {
      csvFormatDelimiterSelect.addEventListener('change', function () {
        if (!csvFormatAutoDelimiterBtn) return;
        csvFormatAutoDelimiterBtn.setAttribute('aria-pressed', 'false');
        csvFormatAutoDelimiterBtn.classList.remove('fleditor-csv-header-btn--active');
        csvFormatDelimiterSelect.disabled = false;
      });
    }
    if (csvFormatModalForm && csvFormatModal) {
      csvFormatModalForm.addEventListener('submit', function (e) {
        e.preventDefault();
        applyCsvFormatFromModal();
        csvFormatModal.close();
        updateCsvBarMeta();
      });
      csvFormatModal.addEventListener('cancel', function (e) {
        e.preventDefault();
        csvFormatModal.close();
      });
      const csvFormatCancel = document.getElementById('csvFormatModalCancel');
      if (csvFormatCancel) {
        csvFormatCancel.addEventListener('click', function () {
          csvFormatModal.close();
        });
      }
    }
    if (csvHeaderRowBtn) {
      csvHeaderRowBtn.addEventListener('click', function () {
        if (!csvSession) return;
        csvSession.headerRow = !csvSession.headerRow;
        if (editorViewMode === 'csv') renderCsvGrid();
        updateCsvBarMeta();
      });
    }
    const helpDismiss = document.getElementById('csvHelpDismiss');
    if (helpDismiss) {
      helpDismiss.addEventListener('click', function () {
        if (csvHelpBanner) csvHelpBanner.hidden = true;
      });
    }
    if (csvTooLargeOpenTable) {
      csvTooLargeOpenTable.addEventListener('click', function () {
        openCsvTableDespiteSize();
      });
    }
    if (csvSelectExtendBtn) {
      csvSelectExtendBtn.addEventListener('click', function () {
        csvSelectExtend = !csvSelectExtend;
        syncCsvSelectExtendBtn();
      });
    }
    if (csvCopyBtn) {
      csvCopyBtn.addEventListener('click', function () {
        if (!csvSession || editorViewMode !== 'csv') return;
        csvCopySelection();
      });
    }
    if (csvPasteBtn) {
      csvPasteBtn.addEventListener('click', function () {
        if (!csvSession || editorViewMode !== 'csv') return;
        void csvPasteFromBar();
      });
    }
    if (csvGoBtn && csvGoCell) {
      csvGoBtn.addEventListener('click', function () {
        if (!csvSession || editorViewMode !== 'csv') return;
        const p = CsvG.parseCellPos(csvGoCell.value, csvSession.rows);
        if (!p.ok) {
          showAlert(p.error || 'Invalid position', 'CSV editor');
          return;
        }
        csvGoToCell(p.r, p.c);
      });
    }
    if (csvSearchInput) {
      csvSearchInput.addEventListener('input', function () {
        recomputeCsvSearchMatches();
      });
    }
    if (csvSearchPrev) {
      csvSearchPrev.addEventListener('click', function () {
        csvSearchStep('prev');
      });
    }
    if (csvSearchNext) {
      csvSearchNext.addEventListener('click', function () {
        csvSearchStep('next');
      });
    }
    if (csvFormatBtn) {
      csvFormatBtn.addEventListener('click', function () {
        openCsvFormatDialog();
      });
    }
    if (textFormatBtn) {
      textFormatBtn.addEventListener('click', function () {
        openTextFormatDialog();
      });
    }
    if (textFormatModalForm && textFormatModal) {
      textFormatModalForm.addEventListener('submit', function (e) {
        e.preventDefault();
        applyTextFormatFromModal();
        textFormatModal.close();
        updateEditorHeader();
      });
      textFormatModal.addEventListener('cancel', function (e) {
        e.preventDefault();
        textFormatModal.close();
      });
      const textFormatCancel = document.getElementById('textFormatModalCancel');
      if (textFormatCancel) {
        textFormatCancel.addEventListener('click', function () {
          textFormatModal.close();
        });
      }
    }
    if (csvTableSizeBtn) {
      csvTableSizeBtn.addEventListener('click', function () {
        if (!csvSession || editorViewMode !== 'csv') return;
        openCsvTableSizeDialog();
      });
    }
    if (csvTableSizeModalForm && csvTableSizeModal) {
      csvTableSizeModalForm.addEventListener('submit', function (e) {
        e.preventDefault();
        void (async function () {
          const ok = await applyCsvTableSizeFromInputs();
          if (ok) csvTableSizeModal.close();
        })();
      });
      csvTableSizeModal.addEventListener('cancel', function (e) {
        e.preventDefault();
        csvTableSizeModal.close();
      });
      const csvTableSizeCancel = document.getElementById('csvTableSizeModalCancel');
      if (csvTableSizeCancel) {
        csvTableSizeCancel.addEventListener('click', function () {
          csvTableSizeModal.close();
        });
      }
    }
  }

  function initBinaryBar() {
    const helpDismiss = document.getElementById('binaryHelpDismiss');
    if (helpDismiss) {
      helpDismiss.addEventListener('click', function () {
        try {
          localStorage.setItem(BINARY_HELP_DISMISS_KEY, '1');
        } catch (_e) {
          /* ignore */
        }
        const banner = document.getElementById('binaryHelpBanner');
        if (banner) {
          banner.hidden = true;
        }
      });
    }
    const startBtn = document.getElementById('binaryPageStart');
    const prevBtn = document.getElementById('binaryPagePrev');
    const nextBtn = document.getElementById('binaryPageNext');
    const endBtn = document.getElementById('binaryPageEnd');
    const goBtn = document.getElementById('binaryGoBtn');
    const searchPrev = document.getElementById('binarySearchPrev');
    const searchNext = document.getElementById('binarySearchNext');
    const insertBtn = document.getElementById('binaryInsertBtn');
    const deleteBtn = document.getElementById('binaryDeleteBtn');
    const sizeBtn = document.getElementById('binarySizeBtn');
    const revertBtn = document.getElementById('binaryRevertBtn');
    const importBtn = document.getElementById('binaryImportBtn');
    const downloadBtn = document.getElementById('binaryDownloadBtn');

    function pageNav(target) {
      if (editorViewMode !== 'binary' || !binaryBuffer) {
        return;
      }
      const pages = H.pageCountForLength(binaryBuffer.length);
      if (target === 'start') {
        showBinaryPage(0);
      } else if (target === 'end') {
        showBinaryPage(pages - 1);
      } else if (target === 'prev') {
        showBinaryPage(binaryPage - 1);
      } else if (target === 'next') {
        showBinaryPage(binaryPage + 1);
      }
    }

    if (startBtn) {
      startBtn.addEventListener('click', function () {
        pageNav('start');
      });
    }
    if (prevBtn) {
      prevBtn.addEventListener('click', function () {
        pageNav('prev');
      });
    }
    if (nextBtn) {
      nextBtn.addEventListener('click', function () {
        pageNav('next');
      });
    }
    if (endBtn) {
      endBtn.addEventListener('click', function () {
        pageNav('end');
      });
    }
    if (goBtn && binaryGoOffset) {
      goBtn.addEventListener('click', function () {
        const p = H.parseByteOffset(binaryGoOffset.value);
        if (!p.ok) {
          showAlert(p.error, 'Hex editor');
          return;
        }
        if (!binaryBuffer || binaryBuffer.length === 0) {
          showAlert('Empty file', 'Hex editor');
          return;
        }
        if (p.value >= binaryBuffer.length) {
          showAlert('Offset out of range (0..' + (binaryBuffer.length - 1) + ')', 'Hex editor');
          return;
        }
        binaryGoToByte(p.value);
      });
    }
    if (searchPrev) {
      searchPrev.addEventListener('click', function () {
        binarySearchStep('prev');
      });
    }
    if (searchNext) {
      searchNext.addEventListener('click', function () {
        binarySearchStep('next');
      });
    }
    if (binarySearchInput) {
      binarySearchInput.addEventListener('input', function () {
        recomputeBinarySearchMatches();
      });
    }
    if (insertBtn) {
      insertBtn.addEventListener('click', async function () {
        if (editorViewMode !== 'binary' || !binaryBuffer) {
          return;
        }
        const offStr = await showPrompt({
          title: 'Insert bytes',
          message: 'Byte offset (decimal or 0x):',
          defaultValue: '0',
        });
        if (offStr == null) {
          return;
        }
        const offP = H.parseByteOffset(offStr);
        if (!offP.ok) {
          await showAlert(offP.error, 'Insert bytes');
          return;
        }
        const dataStr = await showPrompt({
          title: 'Insert bytes',
          message: 'Hex (e.g. 4865) or base64:',
          defaultValue: '',
        });
        if (dataStr == null) {
          return;
        }
        const payload = H.decodePayloadHexOrBase64(dataStr);
        if (!payload.ok) {
          await showAlert(payload.error, 'Insert bytes');
          return;
        }
        binaryBuffer = H.insertBytes(binaryBuffer, offP.value, payload.bytes);
        showBinaryPage(Math.floor(offP.value / H.BYTES_PER_PAGE));
        setDirty(!H.bytesEqual(binaryBuffer, binarySavedSnapshot));
        recomputeBinarySearchMatches();
        updateBinaryBarMeta();
      });
    }
    if (deleteBtn) {
      deleteBtn.addEventListener('click', async function () {
        if (editorViewMode !== 'binary' || !binaryBuffer) {
          return;
        }
        const offStr = await showPrompt({
          title: 'Delete bytes',
          message: 'Start offset:',
          defaultValue: '0',
        });
        if (offStr == null) {
          return;
        }
        const offP = H.parseByteOffset(offStr);
        if (!offP.ok) {
          await showAlert(offP.error, 'Delete bytes');
          return;
        }
        const countStr = await showPrompt({
          title: 'Delete bytes',
          message: 'Count:',
          defaultValue: '1',
        });
        if (countStr == null) {
          return;
        }
        const countP = H.parseByteOffset(countStr);
        if (!countP.ok) {
          await showAlert(countP.error, 'Delete bytes');
          return;
        }
        binaryBuffer = H.deleteBytes(binaryBuffer, offP.value, countP.value);
        showBinaryPage(binaryPage);
        setDirty(!H.bytesEqual(binaryBuffer, binarySavedSnapshot));
        recomputeBinarySearchMatches();
        updateBinaryBarMeta();
      });
    }
    if (sizeBtn) {
      sizeBtn.addEventListener('click', async function () {
        if (editorViewMode !== 'binary' || !binaryBuffer) {
          return;
        }
        const nStr = await showPrompt({
          title: 'Set file size',
          message: 'New size in bytes:',
          defaultValue: String(binaryBuffer.length),
        });
        if (nStr == null) {
          return;
        }
        const nP = H.parseByteOffset(nStr);
        if (!nP.ok) {
          await showAlert(nP.error, 'Set file size');
          return;
        }
        binaryBuffer = H.setFileSize(binaryBuffer, nP.value);
        showBinaryPage(0);
        setDirty(!H.bytesEqual(binaryBuffer, binarySavedSnapshot));
        recomputeBinarySearchMatches();
        updateBinaryBarMeta();
      });
    }
    if (revertBtn) {
      revertBtn.addEventListener('click', function () {
        if (editorViewMode !== 'binary' || !binarySavedSnapshot) {
          return;
        }
        binaryBuffer = H.copyBytes(binarySavedSnapshot);
        showBinaryPage(binaryPage);
        setDirty(false);
        recomputeBinarySearchMatches();
        updateBinaryBarMeta();
      });
    }
    if (importBtn && binaryImportFile) {
      importBtn.addEventListener('click', function () {
        binaryImportFile.click();
      });
      binaryImportFile.addEventListener('change', function () {
        const file = binaryImportFile.files && binaryImportFile.files[0];
        binaryImportFile.value = '';
        if (!file || editorViewMode !== 'binary') {
          return;
        }
        const reader = new FileReader();
        reader.onload = function () {
          const buf = new Uint8Array(reader.result);
          binaryBuffer = H.copyBytes(buf);
          showBinaryPage(0);
          setDirty(!H.bytesEqual(binaryBuffer, binarySavedSnapshot));
          updateBinaryBarMeta();
        };
        reader.readAsArrayBuffer(file);
      });
    }
    if (downloadBtn) {
      downloadBtn.addEventListener('click', function () {
        if (!binaryBuffer || !currentOpenPath()) {
          return;
        }
        const path = currentOpenPath();
        const name = path.split('/').pop() || 'download.bin';
        const blob = new Blob([binaryBuffer], { type: 'application/octet-stream' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = name;
        a.click();
        URL.revokeObjectURL(a.href);
      });
    }
  }

  async function boot() {
    initIoModal();
    initUnsavedModal();
    initDeleteModal();
    initResetProjectModal();
    initPromptModal();
    initCsvPasteModal();
    initAlertModal();
    initNewFileModal();
    initBinaryBar();
    initCsvBar();
    initJsonlBar();
    FleditorSettings.init({
      appEl: document.querySelector('.fleditor-app'),
      getCm: function () {
        return cm;
      },
      getTerm: function () {
        return term;
      },
      onRefreshLayout: scheduleLayoutRefresh,
      showPrompt: showPrompt,
      showAlert: showAlert,
      getWasmToolchainVersion: getWasmToolchainVersion,
    });
    await initEditor();
    FleditorSettings.applyEditorPrefs();
    initTerminal();
    FleditorSettings.applyTerminalTheme();
    initLayoutControls();
    window.addEventListener('resize', scheduleLayoutRefresh);
    clearBtn.addEventListener('click', () => term.clear());
    runBtn.addEventListener('click', () => onRun());
    if (codeMapBtn) {
      codeMapBtn.addEventListener('click', function () {
        if (editorViewMode !== 'flatl' || !hasOpenFile()) return;
        setCodeMapVisible(!codeMapVisible);
      });
    }
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
      if (m) {
        showBanner('');
        if (typeof FleditorSettings !== 'undefined' && FleditorSettings.refreshAboutPanel) {
          FleditorSettings.refreshAboutPanel();
        }
      }
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
