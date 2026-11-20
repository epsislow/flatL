/* JSONL / NDJSON / JSON-lines parse helpers for FLEditor */
(function (root) {
  'use strict';

  const LINES_PER_PAGE = 40;
  const MAX_LINES_FOR_RECORDS = 100000;
  const PREVIEW_CHARS = 80;
  const UI_STRING_TRUNCATE = 300;

  const JSONL_EXT_RE = /\.(jsonl|ndjson|json|jsl)$/i;

  function isJsonlPath(path) {
    return JSONL_EXT_RE.test(String(path || ''));
  }

  function detectLineEnding(text) {
    const T = typeof globalThis !== 'undefined' && globalThis.FleditorTextFormat;
    if (T && T.detectLineEnding) return T.detectLineEnding(text);
    return String(text).indexOf('\r\n') >= 0 ? '\r\n' : '\n';
  }

  /**
   * Split into lines preserving a trailing empty line after a final newline.
   * Empty file → [].
   */
  function splitLines(text) {
    const s = String(text == null ? '' : text);
    if (s === '') return [];
    const T = typeof globalThis !== 'undefined' && globalThis.FleditorTextFormat;
    let lines;
    if (T && T.splitTextLines) {
      lines = T.splitTextLines(s);
    } else {
      lines = s.split(/\r\n|\n|\r/);
    }
    if (s.endsWith('\n') || s.endsWith('\r')) {
      if (!lines.length || lines[lines.length - 1] !== '') lines.push('');
    }
    return lines;
  }

  function joinLines(lines, lineEnding) {
    const le = lineEnding === '\r\n' ? '\r\n' : '\n';
    return (lines || []).join(le);
  }

  function extractErrorPos(message) {
    const m = String(message || '');
    let pm = m.match(/position\s+(\d+)/i);
    if (pm) return Number(pm[1]);
    pm = m.match(/at position\s+(\d+)/i);
    if (pm) return Number(pm[1]);
    pm = m.match(/column\s+(\d+)/i);
    if (pm) return Number(pm[1]) - 1;
    return null;
  }

  /**
   * Parse one line. Does not trim before parse (caller may pass raw line).
   * Whitespace-only → empty.
   */
  function parseLine(lineText) {
    const raw = lineText == null ? '' : String(lineText);
    if (!raw.replace(/[\s\uFEFF]/g, '').length) {
      return { ok: false, empty: true, value: undefined, error: null, errorPos: null };
    }
    try {
      return { ok: true, empty: false, value: JSON.parse(raw), error: null, errorPos: null };
    } catch (e) {
      const msg = e && e.message ? String(e.message) : String(e);
      return {
        ok: false,
        empty: false,
        value: undefined,
        error: msg,
        errorPos: extractErrorPos(msg),
      };
    }
  }

  function serializeLine(value) {
    return JSON.stringify(value);
  }

  function prettyLine(value) {
    return JSON.stringify(value, null, 2);
  }

  /**
   * Text from Free panel → one physical JSONL line.
   * Valid JSON → compact stringify. Invalid → collapse newlines so the file
   * never gains extra records from pretty-print.
   */
  function storeLineFromFreeText(text) {
    const raw = text == null ? '' : String(text);
    const parsed = parseLine(raw);
    if (parsed.ok) return serializeLine(parsed.value);
    if (parsed.empty) return '';
    return raw.replace(/\r\n|\n|\r/g, ' ');
  }

  function previewLine(lineText, maxChars) {
    const n = maxChars == null ? PREVIEW_CHARS : maxChars;
    const s = lineText == null ? '' : String(lineText);
    if (!s.replace(/[\s\uFEFF]/g, '').length) return '(empty)';
    if (s.length <= n) return s;
    return s.slice(0, n) + '\u2026';
  }

  function isTooLarge(lineCount) {
    return (lineCount | 0) > MAX_LINES_FOR_RECORDS;
  }

  function tooLargeMessage(lineCount) {
    return 'Too many lines (' + (lineCount | 0) + '). Open as text.';
  }

  function pageCount(lineCount) {
    const n = lineCount | 0;
    if (n <= 0) return 1;
    return Math.max(1, Math.ceil(n / LINES_PER_PAGE));
  }

  function pageRange(pageIndex, lineCount) {
    const n = lineCount | 0;
    if (n <= 0) return { start: 0, end: 0 };
    const p = Math.max(0, Math.min(pageIndex | 0, pageCount(n) - 1));
    const start = p * LINES_PER_PAGE;
    const end = Math.min(n, start + LINES_PER_PAGE);
    return { start: start, end: end };
  }

  function defaultPanelModeForLine(lineText) {
    const r = parseLine(lineText);
    return r.ok ? 'lock' : 'free';
  }

  function truncateUiString(s, maxLen) {
    const max = maxLen == null ? UI_STRING_TRUNCATE : maxLen;
    const str = String(s);
    if (str.length <= max) return { text: str, truncated: false, length: str.length };
    return { text: str.slice(0, max) + '\u2026', truncated: true, length: str.length };
  }

  root.FleditorJsonlParse = {
    LINES_PER_PAGE: LINES_PER_PAGE,
    MAX_LINES_FOR_RECORDS: MAX_LINES_FOR_RECORDS,
    PREVIEW_CHARS: PREVIEW_CHARS,
    UI_STRING_TRUNCATE: UI_STRING_TRUNCATE,
    isJsonlPath: isJsonlPath,
    detectLineEnding: detectLineEnding,
    splitLines: splitLines,
    joinLines: joinLines,
    parseLine: parseLine,
    serializeLine: serializeLine,
    prettyLine: prettyLine,
    storeLineFromFreeText: storeLineFromFreeText,
    previewLine: previewLine,
    isTooLarge: isTooLarge,
    tooLargeMessage: tooLargeMessage,
    pageCount: pageCount,
    pageRange: pageRange,
    defaultPanelModeForLine: defaultPanelModeForLine,
    truncateUiString: truncateUiString,
    extractErrorPos: extractErrorPos,
  };
})(typeof globalThis !== 'undefined' ? globalThis : window);
