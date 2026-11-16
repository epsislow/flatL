/* CSV/TSV parse and serialize for FLEditor */
(function (root) {
  'use strict';

  const MAX_ROWS = 2000;
  const MAX_COLS = 100;
  const CSV_STORE_PREFIX = 'FLEDITOR::CSV::';

  function stripBom(text) {
    return text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
  }

  function isWhitespaceOnly(text) {
    return !text || !String(text).replace(/[\s\uFEFF]/g, '').length;
  }

  function detectLineEnding(text) {
    return text.indexOf('\r\n') >= 0 ? '\r\n' : '\n';
  }

  function splitPhysicalLines(text) {
    const lines = [];
    let start = 0;
    for (let i = 0; i < text.length; i++) {
      if (text[i] === '\n') {
        let end = i;
        if (end > start && text[end - 1] === '\r') end--;
        lines.push(text.slice(start, end));
        start = i + 1;
      }
    }
    if (start < text.length) lines.push(text.slice(start));
    else if (text.endsWith('\n') || text.endsWith('\r\n')) lines.push('');
    return lines;
  }

  function countUnquoted(line, delim, quoteChar) {
    const Q = quoteChar || '"';
    let n = 0;
    let q = false;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (q) {
        if (c === Q) {
          if (line[i + 1] === Q) { i++; continue; }
          q = false;
        }
        continue;
      }
      if (c === Q) { q = true; continue; }
      if (c === delim) n++;
    }
    return n;
  }

  function scoreDelimiter(sampleLines, delim, quoteChar) {
    if (!sampleLines.length) return { score: -1, cols: 0 };
    const counts = sampleLines.map((ln) => countUnquoted(ln, delim, quoteChar) + 1);
    const mode = counts.reduce((a, b) => a + b, 0) / counts.length;
    let variance = 0;
    counts.forEach((c) => { variance += Math.abs(c - mode); });
    const consistent = counts.every((c) => Math.abs(c - counts[0]) <= 1);
    return { score: consistent ? counts.length * 10 - variance : -1, cols: counts[0] || 0 };
  }

  function detectDelimiter(text, preferTab, quoteChar) {
    const lines = splitPhysicalLines(text).filter((ln) => ln.trim() !== '').slice(0, 20);
    const candidates = preferTab ? ['\t', ';', ','] : [';', ',', '\t'];
    let best = ',';
    let bestScore = -1;
    candidates.forEach((d) => {
      const s = scoreDelimiter(lines, d, quoteChar);
      if (s.score > bestScore) { bestScore = s.score; best = d; }
    });
    return best;
  }

  function parseDelimiterInput(str) {
    const s = String(str || '').trim().toLowerCase();
    if (s === 'tab' || s === '\\t') return '\t';
    if (s === 'comma' || s === ',') return ',';
    if (s === 'semicolon' || s === ';') return ';';
    if (s.length === 1) return s;
    return null;
  }

  function parseCsvText(text, delimiter, quoteChar) {
    const Q = quoteChar || '"';
    const rows = [];
    let row = [];
    let field = '';
    let i = 0;
    const s = text;
    let inQuotes = false;
    while (i < s.length) {
      const c = s[i];
      if (inQuotes) {
        if (c === Q) {
          if (s[i + 1] === Q) { field += Q; i += 2; continue; }
          inQuotes = false;
          i++;
          continue;
        }
        field += c;
        i++;
        continue;
      }
      if (c === Q) { inQuotes = true; i++; continue; }
      if (c === delimiter) { row.push(field); field = ''; i++; continue; }
      if (c === '\r') {
        i++;
        if (s[i] === '\n') i++;
        row.push(field);
        field = '';
        rows.push(row);
        row = [];
        continue;
      }
      if (c === '\n') {
        row.push(field);
        field = '';
        rows.push(row);
        row = [];
        i++;
        continue;
      }
      field += c;
      i++;
    }
    if (field !== '' || row.length > 0) {
      row.push(field);
      rows.push(row);
    }
    return rows;
  }

  function normalizeRowWidths(rows) {
    let w = 0;
    rows.forEach((r) => { if (r.length > w) w = r.length; });
    if (!rows.length) return [['']];
    return rows.map((r) => {
      const copy = r.slice();
      while (copy.length < w) copy.push('');
      return copy;
    });
  }

  function emptyGrid() {
    return [['']];
  }

  function isGridEmpty(rows) {
    if (rows.length !== 1 || rows[0].length !== 1) return false;
    return rows[0][0] === '';
  }

  function needsQuote(field, delimiter, quoteChar) {
    const Q = quoteChar || '"';
    if (field.indexOf(Q) >= 0) return true;
    if (field.indexOf('\n') >= 0 || field.indexOf('\r') >= 0) return true;
    if (field.indexOf(delimiter) >= 0) return true;
    return false;
  }

  function escapeField(field, quoteChar) {
    const Q = quoteChar || '"';
    return String(field).split(Q).join(Q + Q);
  }

  function serializeField(field, delimiter, quoteChar) {
    const Q = quoteChar || '"';
    const s = String(field);
    if (!needsQuote(s, delimiter, Q)) return s;
    return Q + escapeField(s, Q) + Q;
  }

  function serializeCsv(rows, delimiter, lineEnding, quoteChar) {
    const norm = normalizeRowWidths(rows);
    if (isGridEmpty(norm)) return '';
    const le = lineEnding || '\n';
    const Q = quoteChar || '"';
    return norm.map((r) => r.map((c) => serializeField(c, delimiter, Q)).join(delimiter)).join(le);
  }

  const ENCODINGS = ['utf-8', 'utf-8-bom', 'utf-16le', 'utf-16be'];

  function normalizeEncoding(enc) {
    const e = String(enc || 'utf-8').toLowerCase();
    return ENCODINGS.indexOf(e) >= 0 ? e : 'utf-8';
  }

  function detectEncodingFromBytes(u8) {
    if (u8.length >= 2 && u8[0] === 0xff && u8[1] === 0xfe) return 'utf-16le';
    if (u8.length >= 2 && u8[0] === 0xfe && u8[1] === 0xff) return 'utf-16be';
    if (u8.length >= 3 && u8[0] === 0xef && u8[1] === 0xbb && u8[2] === 0xbf) return 'utf-8-bom';
    return 'utf-8';
  }

  function decodeTabularBytes(u8) {
    if (!u8 || !u8.length) {
      return { ok: true, text: '', encoding: 'utf-8' };
    }
    const encoding = detectEncodingFromBytes(u8);
    try {
      let text = '';
      if (encoding === 'utf-16le' || encoding === 'utf-16be') {
        text = new TextDecoder(encoding).decode(u8.subarray(2));
      } else if (encoding === 'utf-8-bom') {
        text = new TextDecoder('utf-8').decode(u8.subarray(3));
      } else {
        text = new TextDecoder('utf-8', { fatal: true }).decode(u8);
      }
      if (text.indexOf('\0') >= 0) return { ok: false };
      return { ok: true, text: text, encoding: encoding };
    } catch (_e) {
      return { ok: false };
    }
  }

  function encodeTabularBytes(text, encoding) {
    const enc = normalizeEncoding(encoding);
    const body = String(text);
    if (enc === 'utf-16le' || enc === 'utf-16be') {
      return encodeTabularBytesUtf16(body, enc);
    }
    const utf8 = new TextEncoder().encode(body);
    if (enc === 'utf-8-bom') {
      const out = new Uint8Array(3 + utf8.length);
      out[0] = 0xef; out[1] = 0xbb; out[2] = 0xbf;
      out.set(utf8, 3);
      return out;
    }
    return utf8;
  }

  function encodeTabularBytesUtf16(body, enc) {
    const le = enc === 'utf-16le';
    const bom = le ? [0xff, 0xfe] : [0xfe, 0xff];
    const u8 = new Uint8Array(2 + body.length * 2);
    u8[0] = bom[0]; u8[1] = bom[1];
    const view = new DataView(u8.buffer);
    for (let i = 0; i < body.length; i++) {
      view.setUint16(2 + i * 2, body.charCodeAt(i), le);
    }
    return u8;
  }

  function isCsvStoredValue(content) {
    return typeof content === 'string' && content.startsWith(CSV_STORE_PREFIX);
  }

  function unwrapProjectCsv(content) {
    if (typeof content !== 'string') return null;
    if (!content.startsWith(CSV_STORE_PREFIX)) {
      return { text: content, encoding: 'utf-8', wrapped: false };
    }
    const rest = content.slice(CSV_STORE_PREFIX.length);
    const nl = rest.indexOf('\n');
    if (nl < 0) return null;
    const tag = rest.slice(0, nl);
    const enc = tag.endsWith('::') ? tag.slice(0, -2) : tag;
    if (ENCODINGS.indexOf(enc) < 0 || enc === 'utf-8') return null;
    return { text: rest.slice(nl + 1), encoding: enc, wrapped: true };
  }

  function wrapProjectCsv(text, encoding) {
    const enc = normalizeEncoding(encoding);
    if (enc === 'utf-8') return String(text);
    return CSV_STORE_PREFIX + enc + '::\n' + String(text);
  }

  function parseFile(text, options) {
    const opts = options || {};
    const quoteChar = opts.quoteChar != null && String(opts.quoteChar).length ? String(opts.quoteChar)[0] : '"';
    const preferTab = !!opts.preferTab;
    const autoDetect = opts.autoDetect !== false;
    if (isWhitespaceOnly(text)) {
      return {
        rows: emptyGrid(),
        delimiter: opts.delimiter != null ? opts.delimiter : (preferTab ? '\t' : ','),
        lineEnding: '\n',
        quoteChar: quoteChar,
        empty: true,
      };
    }
    const raw = stripBom(String(text));
    const lineEnding = detectLineEnding(raw);
    let delimiter = opts.delimiter;
    if (delimiter == null) {
      delimiter = detectDelimiter(raw, preferTab, quoteChar);
    }
    let rows = parseCsvText(raw, delimiter, quoteChar);
    while (rows.length > 1 && rows[rows.length - 1].every((c) => c === '')) rows.pop();
    rows = normalizeRowWidths(rows);
    if (!rows.length) rows = emptyGrid();
    return { rows, delimiter, lineEnding, quoteChar, empty: false };
  }

  function tableSize(rows) {
    const rowCount = rows.length;
    let colCount = 0;
    rows.forEach((r) => { if (r.length > colCount) colCount = r.length; });
    return { rows: rowCount, cols: colCount };
  }

  function isTooLarge(rows) {
    const t = tableSize(rows);
    return t.rows > MAX_ROWS || t.cols > MAX_COLS;
  }

  function resizeGrid(rows, newRows, newCols) {
    const r = Math.max(1, newRows | 0);
    const c = Math.max(1, newCols | 0);
    const out = [];
    for (let y = 0; y < r; y++) {
      const src = rows[y] || [];
      const line = [];
      for (let x = 0; x < c; x++) line.push(src[x] != null ? String(src[x]) : '');
      out.push(line);
    }
    return out;
  }

  function ensureMinGrid(rows) {
    if (!rows.length) return emptyGrid();
    return normalizeRowWidths(rows);
  }

  function delimiterLabel(d) {
    if (d === '\t') return 'tab';
    if (d === ';') return ';';
    return ',';
  }

  function encodingFromProjectContent(content) {
    const un = unwrapProjectCsv(content);
    if (un && un.wrapped) return un.encoding;
    if (typeof content === 'string' && content.charCodeAt(0) === 0xfeff) return 'utf-8-bom';
    return 'utf-8';
  }

  function projectTextFromContent(content) {
    const un = unwrapProjectCsv(content);
    if (un) return stripBom(un.text);
    return stripBom(String(content || ''));
  }

  root.FleditorCsvParse = {
    MAX_ROWS,
    MAX_COLS,
    ENCODINGS,
    CSV_STORE_PREFIX,
    stripBom,
    isWhitespaceOnly,
    detectLineEnding,
    detectDelimiter,
    parseDelimiterInput,
    parseCsvText,
    parseFile,
    serializeCsv,
    normalizeRowWidths,
    emptyGrid,
    isGridEmpty,
    tableSize,
    isTooLarge,
    resizeGrid,
    ensureMinGrid,
    delimiterLabel,
    decodeTabularBytes,
    encodeTabularBytes,
    isCsvStoredValue,
    unwrapProjectCsv,
    wrapProjectCsv,
    encodingFromProjectContent,
    projectTextFromContent,
    normalizeEncoding,
    tooLargeMessage(rows) {
      const t = tableSize(rows);
      return 'Too large (' + t.rows + ' rows, ' + t.cols + ' columns). Open as text.';
    },
  };
}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));