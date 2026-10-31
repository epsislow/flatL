/* global window */
(function (global) {
  'use strict';

  const LAYOUT_KEY = 'flatl-fleditor-layout';

  const DEFAULT_LAYOUT = {
    projectOpen: true,
    terminalOpen: false,
    terminalFraction: 0.35,
  };

  function clampFraction(n) {
    if (typeof n !== 'number' || Number.isNaN(n)) {
      return DEFAULT_LAYOUT.terminalFraction;
    }
    return Math.min(0.6, Math.max(0.2, n));
  }

  function mergeLayout(raw) {
    const out = {
      projectOpen: DEFAULT_LAYOUT.projectOpen,
      terminalOpen: DEFAULT_LAYOUT.terminalOpen,
      terminalFraction: DEFAULT_LAYOUT.terminalFraction,
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
    mergeLayout,
    loadLayout,
    saveLayout,
    resetLayout,
  };
})(typeof window !== 'undefined' ? window : globalThis);
