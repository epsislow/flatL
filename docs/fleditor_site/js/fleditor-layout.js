/* global window */
(function (global) {
  'use strict';

  const LAYOUT_KEY = 'flatl-fleditor-layout';

  const DEFAULT_LAYOUT = {
    projectOpen: true,
    terminalOpen: false,
    terminalFraction: 0.35,
    projectWidthPx: 240,
    stackProjectFraction: 0.28,
  };

  function clampFraction(n) {
    if (typeof n !== 'number' || Number.isNaN(n)) {
      return DEFAULT_LAYOUT.terminalFraction;
    }
    return Math.min(0.6, Math.max(0.2, n));
  }

  function clampProjectWidthPx(n) {
    if (typeof n !== 'number' || Number.isNaN(n)) {
      return DEFAULT_LAYOUT.projectWidthPx;
    }
    return Math.min(560, Math.max(140, Math.round(n)));
  }

  function clampStackProjectFraction(n) {
    if (typeof n !== 'number' || Number.isNaN(n)) {
      return DEFAULT_LAYOUT.stackProjectFraction;
    }
    return Math.min(0.55, Math.max(0.12, n));
  }

  /** @param {number} px @param {number} workHeight */
  function clampStackTerminalHeightPx(px, workHeight) {
    const work = typeof workHeight === 'number' && workHeight > 0 ? workHeight : 480;
    const minH = 72;
    const maxH = Math.max(minH, Math.floor(work * 0.65));
    if (typeof px !== 'number' || Number.isNaN(px)) {
      return Math.round(Math.min(maxH, Math.max(minH, work * DEFAULT_LAYOUT.terminalFraction)));
    }
    return Math.min(maxH, Math.max(minH, Math.round(px)));
  }

  function mergeLayout(raw) {
    const out = {
      projectOpen: DEFAULT_LAYOUT.projectOpen,
      terminalOpen: DEFAULT_LAYOUT.terminalOpen,
      terminalFraction: DEFAULT_LAYOUT.terminalFraction,
      projectWidthPx: DEFAULT_LAYOUT.projectWidthPx,
      stackProjectFraction: DEFAULT_LAYOUT.stackProjectFraction,
      stackTerminalHeightPx: null,
    };
    if (!raw || typeof raw !== 'object') {
      return out;
    }
    if (typeof raw.projectOpen === 'boolean') {
      out.projectOpen = raw.projectOpen;
    }
    if (typeof raw.terminalOpen === 'boolean') {
      out.terminalOpen = raw.terminalOpen;
    }
    out.terminalFraction = clampFraction(raw.terminalFraction);
    out.projectWidthPx = clampProjectWidthPx(raw.projectWidthPx);
    out.stackProjectFraction = clampStackProjectFraction(raw.stackProjectFraction);
    if (typeof raw.stackTerminalHeightPx === 'number' && !Number.isNaN(raw.stackTerminalHeightPx)) {
      out.stackTerminalHeightPx = clampStackTerminalHeightPx(raw.stackTerminalHeightPx, 480);
    }
    return out;
  }

  function loadLayout(storage) {
    const store = storage || global.localStorage;
    try {
      const raw = store.getItem(LAYOUT_KEY);
      if (!raw) {
        return mergeLayout(null);
      }
      return mergeLayout(JSON.parse(raw));
    } catch (_e) {
      return mergeLayout(null);
    }
  }

  function saveLayout(storage, layout) {
    const store = storage || global.localStorage;
    const merged = mergeLayout(layout);
    store.setItem(LAYOUT_KEY, JSON.stringify(merged));
    return merged;
  }

  function resetLayout(storage) {
    const store = storage || global.localStorage;
    store.removeItem(LAYOUT_KEY);
    return mergeLayout(null);
  }

  global.FleditorLayout = {
    LAYOUT_KEY,
    DEFAULT_LAYOUT,
    clampFraction,
    clampProjectWidthPx,
    clampStackProjectFraction,
    clampStackTerminalHeightPx,
    mergeLayout,
    loadLayout,
    saveLayout,
    resetLayout,
  };
})(typeof window !== 'undefined' ? window : globalThis);
