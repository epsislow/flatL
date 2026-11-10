/* global window */
(function (global) {
  'use strict';

  /** Browser UI bundle; align with doc / release notes when bumped. */
  const FLEDITOR_UI_VERSION = '0.4.0';

  global.FleditorVersion = {
    FLEDITOR_UI_VERSION,
  };
})(typeof window !== 'undefined' ? window : globalThis);
