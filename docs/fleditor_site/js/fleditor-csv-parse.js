/* CSV/TSV parse and serialize for FLEditor */
(function (root) {
  'use strict';

  const MAX_ROWS = 2000;
  const MAX_COLS = 100;
  const QUOTE = '"';

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

  function countUnquoted(line, ch) {
    let n = 0;
    let q = false;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (q) {
        if (c === QUOTE) {
          if (line[i + 1] === QUOTE) { i++; continue; }
          q = false;
        }
        continue;
      }
      if (c === QUOTE) { q = true; continue; }
      if (c === ch) n++;
    }
    return n;
  }

  function scoreDelimiter(sampleLines, delim) {
    if (!sampleLines.length) return { score: -1, cols: 0 };
    const counts = sampleLines.map((ln) => countUnquoted(ln, delim) + 1);
    const mode = counts.reduce((a, b) => a + b, 0) / counts.length;
    let variance = 0;
    counts.forEach((c) => { variance += Math.abs(c - mode); });
    const consistent = counts.every((c) => Math.abs(c - counts[0]) <= 1);
    return { score: consistent ? counts.length * 10 - variance : -1, cols: counts[0] || 0 };
  }

  function detectDelimiter(text, preferTab) {
    const lines = splitPhysicalLines(text).filter((ln) => ln.trim() !== '').slice(0, 20);
    const candidates = preferTab ? ['\t', ';', ','] : [';', ',', '\t'];
    let best = ',';
    let bestScore = -1;
    candidates.forEach((d) => {
      const s = scoreDelimiter(lines, d);
      if (s.score > bestScore) { bestScore = s.score; best = d; }
    });
    return best;
  }

  function parseCsvText(text, delimiter) {
    const rows = [];
    let row = [];
    let field = '';
    let i = 0;
    const s = text;
    let inQuotes = false;
    while (i < s.length) {
      const c = s[i];
      if (inQuotes) {
        if (c === QUOTE) {
          if (s[i + 1] === QUOTE) { field += QUOTE; i += 2; continue; }
          inQuotes = false;
          i++;
          continue;
        }
        field += c;
        i++;
        continue;
      }
      if (c === QUOTE) { inQuotes = true; i++; continue; }
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

  function needsQuote(field, delimiter) {
    if (field.indexOf(QUOTE) >= 0) return true;
    if (field.indexOf('\n') >= 0 || field.indexOf('\r') >= 0) return true;
    if (field.indexOf(delimiter) >= 0) return true;
    return false;
  }

  function escapeField(field) {
    return String(field).replace(/"/g, '""');
  }

  function serializeField(field, delimiter) {
    const s = String(field);
    if (!needsQuote(s, delimiter)) return s;
    return QUOTE + escapeField(s) + QUOTE;
  }

  function serializeCsv(rows, delimiter, lineEnding) {
    const norm = normalizeRowWidths(rows);
    if (isGridEmpty(norm)) return '';
    const le = lineEnding || '\n';
    return norm.map((r) => r.map((c) => serializeField(c, delimiter)).join(delimiter)).join(le);
  }

  function parseFile(text, options) {
    const opts = options || {};
    if (isWhitespaceOnly(text)) {
      return {
        rows: emptyGrid(),
        delimiter: opts.preferTab ? '\t' : ',',
        lineEnding: '\n',
        empty: true,
      };
    }
    const raw = stripBom(String(text));
    const lineEnding = detectLineEnding(raw);
    const delimiter = opts.delimiter || detectDelimiter(raw, !!opts.preferTab);
    let rows = parseCsvText(raw, delimiter);
    while (rows.length > 1 && rows[rows.length - 1].every((c) => c === '')) rows.pop();
    rows = normalizeRowWidths(rows);
    if (!rows.length) rows = emptyGrid();
    return { rows, delimiter, lineEnding, empty: false };
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

  root.FleditorCsvParse = {
    MAX_ROWS,
    MAX_COLS,
    stripBom,
    isWhitespaceOnly,
    detectLineEnding,
    detectDelimiter,
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
    tooLargeMessage(rows) {
      const t = tableSize(rows);
      return 'Too large (' + t.rows + ' rows, ' + t.cols + ' columns). Open as text.';
    },
  };
}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));