/* Line endings and text encoding helpers for FLEditor */
(function (root) {
  'use strict';

  const TEXT_STORE_PREFIX = 'FLEDITOR::TEXT::';
  const LINE_ENDING_KEYS = ['lf', 'crlf', 'cr'];

  function lineEndingFromKey(key) {
    const k = String(key || 'lf').toLowerCase();
    if (k === 'crlf') return '\r\n';
    if (k === 'cr') return '\r';
    return '\n';
  }

  function lineEndingKeyFromValue(le) {
    if (le === '\r\n') return 'crlf';
    if (le === '\r') return 'cr';
    return 'lf';
  }

  function lineEndingLabel(key) {
    const k = lineEndingKeyFromValue(lineEndingFromKey(key));
    if (k === 'crlf') return 'Windows CRLF';
    if (k === 'cr') return 'Mac CR';
    return 'Unix LF';
  }

  function detectLineEnding(text) {
    const s = String(text || '');
    let crlf = 0;
    let lf = 0;
    let cr = 0;
    for (let i = 0; i < s.length; i++) {
      if (s[i] === '\r' && s[i + 1] === '\n') {
        crlf++;
        i++;
        continue;
      }
      if (s[i] === '\n') lf++;
      else if (s[i] === '\r') cr++;
    }
    if (crlf >= lf && crlf >= cr && crlf > 0) return '\r\n';
    if (cr >= lf && cr > 0) return '\r';
    return '\n';
  }

  function splitTextLines(text) {
    const lines = [];
    const s = String(text);
    let start = 0;
    for (let i = 0; i < s.length; i++) {
      if (s[i] === '\r' && s[i + 1] === '\n') {
        lines.push(s.slice(start, i));
        start = i + 2;
        i++;
        continue;
      }
      if (s[i] === '\n' || s[i] === '\r') {
        lines.push(s.slice(start, i));
        start = i + 1;
      }
    }
    lines.push(s.slice(start));
    return lines;
  }

  function convertLineEnding(text, targetLe) {
    const le = targetLe === '\r\n' || targetLe === '\r' ? targetLe : '\n';
    return splitTextLines(text).join(le);
  }

  function textToCmNewlines(text) {
    return convertLineEnding(text, '\n');
  }

  function textFromCmNewlines(cmText, lineEnding) {
    const le = lineEnding || '\n';
    return String(cmText || '').replace(/\n/g, le);
  }

  function csvParse() {
    return typeof root !== 'undefined' && root.FleditorCsvParse;
  }

  function normalizeEncoding(enc) {
    const C = csvParse();
    if (C && C.normalizeEncoding) return C.normalizeEncoding(enc);
    return 'utf-8';
  }

  function ENCODINGS() {
    const C = csvParse();
    return C && C.ENCODINGS ? C.ENCODINGS.slice() : ['utf-8', 'utf-8-bom', 'utf-16le', 'utf-16be'];
  }

  function decodeTextBytes(u8) {
    const C = csvParse();
    if (C && C.decodeTabularBytes) return C.decodeTabularBytes(u8);
    return { ok: false };
  }

  function encodeTextBytes(text, encoding) {
    const C = csvParse();
    if (C && C.encodeTabularBytes) return C.encodeTabularBytes(text, encoding);
    return new TextEncoder().encode(String(text));
  }

  function isTextStoredValue(content) {
    return typeof content === 'string' && content.startsWith(TEXT_STORE_PREFIX);
  }

  function unwrapProjectText(content) {
    if (typeof content !== 'string') return null;
    if (!content.startsWith(TEXT_STORE_PREFIX)) {
      return { text: content, encoding: 'utf-8', wrapped: false };
    }
    const rest = content.slice(TEXT_STORE_PREFIX.length);
    const nl = rest.indexOf('\n');
    if (nl < 0) return null;
    const tag = rest.slice(0, nl);
    const enc = tag.endsWith('::') ? tag.slice(0, -2) : tag;
    if (ENCODINGS().indexOf(enc) < 0 || enc === 'utf-8') return null;
    return { text: rest.slice(nl + 1), encoding: enc, wrapped: true };
  }

  function wrapProjectText(text, encoding) {
    const enc = normalizeEncoding(encoding);
    if (enc === 'utf-8') return String(text);
    return TEXT_STORE_PREFIX + enc + '::\n' + String(text);
  }

  function encodingFromProjectContent(content) {
    const un = unwrapProjectText(content);
    if (un && un.wrapped) return un.encoding;
    if (typeof content === 'string' && content.charCodeAt(0) === 0xfeff) return 'utf-8-bom';
    return 'utf-8';
  }

  function projectTextFromContent(content) {
    const un = unwrapProjectText(content);
    if (un) {
      const C = csvParse();
      const t = un.text;
      return C && C.stripBom ? C.stripBom(t) : t;
    }
    const C = csvParse();
    const s = String(content || '');
    return C && C.stripBom ? C.stripBom(s) : s;
  }

  root.FleditorTextFormat = {
    TEXT_STORE_PREFIX,
    LINE_ENDING_KEYS,
    lineEndingFromKey,
    lineEndingKeyFromValue,
    lineEndingLabel,
    detectLineEnding,
    splitTextLines,
    convertLineEnding,
    textToCmNewlines,
    textFromCmNewlines,
    ENCODINGS,
    normalizeEncoding,
    decodeTextBytes,
    encodeTextBytes,
    isTextStoredValue,
    unwrapProjectText,
    wrapProjectText,
    encodingFromProjectContent,
    projectTextFromContent,
  };
}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));