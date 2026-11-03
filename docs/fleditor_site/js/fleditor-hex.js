/* FlatL fleditor — hex dump parse/format (browser + Node via globalThis.FleditorHex) */
(function (global) {
  'use strict';

  const BYTES_PER_LINE = 16;
  const LINES_PER_PAGE = 40;
  const BYTES_PER_PAGE = BYTES_PER_LINE * LINES_PER_PAGE;
  /** New binary files start with this many zero bytes so the hex grid is visible. */
  const DEFAULT_NEW_BINARY_SIZE = 64;

  function copyBytes(u8) {
    return new Uint8Array(u8);
  }

  function bytesEqual(a, b) {
    if (!a || !b || a.length !== b.length) {
      return false;
    }
    for (let i = 0; i < a.length; i++) {
      if (a[i] !== b[i]) {
        return false;
      }
    }
    return true;
  }

  function pageCountForLength(byteLength) {
    if (byteLength <= 0) {
      return 1;
    }
    return Math.ceil(byteLength / BYTES_PER_PAGE);
  }

  function bytesOnPage(byteLength, pageIndex) {
    const off = pageIndex * BYTES_PER_PAGE;
    if (byteLength <= 0) {
      return 0;
    }
    return Math.min(BYTES_PER_PAGE, byteLength - off);
  }

  function pageByteOffset(pageIndex) {
    return pageIndex * BYTES_PER_PAGE;
  }

  function formatHexLine(absOffset, u8, start, end) {
    let hex = '';
    let ascii = '';
    for (let i = start; i < end; i++) {
      const b = u8[i];
      hex += b.toString(16).padStart(2, '0') + ' ';
      ascii += b >= 32 && b < 127 ? String.fromCharCode(b) : '.';
    }
    while (hex.length < BYTES_PER_LINE * 3) {
      hex += '   ';
    }
    return absOffset.toString(16).padStart(8, '0') + '  ' + hex + ' |' + ascii + '|';
  }

  function hexDumpPageFromBytes(u8, pageIndex) {
    const len = u8.length;
    const start = pageIndex * BYTES_PER_PAGE;
    if (len === 0 || start >= len) {
      return '';
    }
    const lines = [];
    const pageEnd = Math.min(len, start + BYTES_PER_PAGE);
    for (let off = start; off < pageEnd; off += BYTES_PER_LINE) {
      const end = Math.min(off + BYTES_PER_LINE, pageEnd);
      lines.push(formatHexLine(off, u8, off, end));
    }
    return lines.join('\n') + (lines.length ? '\n' : '');
  }

  /** Full file dump (legacy / export). */
  function hexDumpAllFromBytes(u8) {
    if (u8.length === 0) {
      return '';
    }
    const lines = [];
    for (let off = 0; off < u8.length; off += BYTES_PER_LINE) {
      const end = Math.min(off + BYTES_PER_LINE, u8.length);
      lines.push(formatHexLine(off, u8, off, end));
    }
    return lines.join('\n') + '\n';
  }

  function extractHexRegionFromLine(line) {
    const pipe = line.indexOf('|');
    let work = pipe >= 0 ? line.slice(0, pipe) : line;
    work = work.trim();
    const offMatch = work.match(/^([0-9a-fA-F]{8})\s+(.*)$/);
    if (offMatch) {
      return offMatch[2];
    }
    return work;
  }

  /**
   * Parse one page of hex dump text.
   * @param {number} expectedLen bytes expected for this page slice
   */
  function hexDumpPageToBytes(text, expectedLen) {
    if (expectedLen === 0) {
      const lines = (text || '').split('\n');
      for (let li = 0; li < lines.length; li++) {
        const line = lines[li].trim();
        if (!line || line.startsWith(';')) {
          continue;
        }
        return {
          ok: false,
          error: 'File has 0 bytes. Use Size… (e.g. 64) or Insert… — do not type plain text here.',
          line: li + 1,
        };
      }
      return { ok: true, bytes: new Uint8Array(0) };
    }
    const out = [];
    const lines = (text || '').split('\n');
    for (let li = 0; li < lines.length; li++) {
      const line = lines[li].trim();
      if (!line) {
        continue;
      }
      if (line.startsWith(';')) {
        continue;
      }
      const region = extractHexRegionFromLine(line);
      const tokens = region.match(/[0-9a-fA-F]{2}/g);
      if (!tokens) {
        if (region.trim().length > 0) {
          return { ok: false, error: 'invalid hex on line ' + (li + 1), line: li + 1 };
        }
        continue;
      }
      for (let ti = 0; ti < tokens.length; ti++) {
        out.push(parseInt(tokens[ti], 16));
      }
    }
    if (out.length !== expectedLen) {
      return {
        ok: false,
        error: 'expected ' + expectedLen + ' bytes on page, got ' + out.length,
      };
    }
    return { ok: true, bytes: new Uint8Array(out) };
  }

  function parseByteOffset(str) {
    const s = (str || '').trim();
    if (!s) {
      return { ok: false, error: 'empty offset' };
    }
    let n;
    if (/^0x[0-9a-fA-F]+$/i.test(s)) {
      n = parseInt(s, 16);
    } else if (/^\d+$/.test(s)) {
      n = parseInt(s, 10);
    } else {
      return { ok: false, error: 'use decimal or 0x hex' };
    }
    if (!Number.isFinite(n) || n < 0) {
      return { ok: false, error: 'invalid offset' };
    }
    return { ok: true, value: n };
  }

  function looksLikeBase64(s) {
    if (!s || s.length < 4) {
      return false;
    }
    return /^[A-Za-z0-9+/]+=*$/.test(s.replace(/\s/g, ''));
  }

  function parseHexPattern(s) {
    const compact = s.replace(/\s+/g, '');
    if (!compact || !/^[0-9a-fA-F]+$/.test(compact) || compact.length % 2 !== 0) {
      return null;
    }
    const out = new Uint8Array(compact.length / 2);
    for (let i = 0; i < out.length; i++) {
      out[i] = parseInt(compact.slice(i * 2, i * 2 + 2), 16);
    }
    return out;
  }

  function parseSearchPattern(str) {
    const raw = (str || '').trim();
    if (!raw) {
      return { ok: false, error: 'empty search' };
    }
    const hex = parseHexPattern(raw);
    if (hex) {
      return { ok: true, bytes: hex };
    }
    const b64 = raw.replace(/\s/g, '');
    if (looksLikeBase64(b64)) {
      try {
        const bin = atob(b64);
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) {
          out[i] = bin.charCodeAt(i);
        }
        return { ok: true, bytes: out };
      } catch (_e) {
        /* fall through to text */
      }
    }
    const enc = new TextEncoder();
    return { ok: true, bytes: enc.encode(raw) };
  }

  function bytesMatchAt(haystack, needle, i) {
    for (let j = 0; j < needle.length; j++) {
      if (haystack[i + j] !== needle[j]) {
        return false;
      }
    }
    return true;
  }

  function findAllMatches(haystack, needle) {
    const out = [];
    if (!needle.length || haystack.length < needle.length) {
      return out;
    }
    const max = haystack.length - needle.length;
    for (let i = 0; i <= max; i++) {
      if (bytesMatchAt(haystack, needle, i)) {
        out.push(i);
      }
    }
    return out;
  }

  /** Linear scan; does not wrap. */
  function findMatch(haystack, needle, fromIndex, direction) {
    if (!needle.length || haystack.length < needle.length) {
      return -1;
    }
    const max = haystack.length - needle.length;
    if (direction === 'next') {
      for (let i = Math.max(0, fromIndex); i <= max; i++) {
        if (bytesMatchAt(haystack, needle, i)) {
          return i;
        }
      }
      return -1;
    }
    for (let i = Math.min(fromIndex, max); i >= 0; i--) {
      if (bytesMatchAt(haystack, needle, i)) {
        return i;
      }
    }
    return -1;
  }

  /** Wraps to start/end when no match ahead/behind (strictly after/before fromIndex). */
  function findMatchWrap(haystack, needle, fromIndex, direction) {
    const all = findAllMatches(haystack, needle);
    if (!all.length) {
      return -1;
    }
    if (direction === 'next') {
      for (let k = 0; k < all.length; k++) {
        if (all[k] > fromIndex) {
          return all[k];
        }
      }
      return all[0];
    }
    for (let k = all.length - 1; k >= 0; k--) {
      if (all[k] < fromIndex) {
        return all[k];
      }
    }
    return all[all.length - 1];
  }

  function insertBytes(buffer, offset, payload) {
    const off = Math.min(Math.max(0, offset), buffer.length);
    const out = new Uint8Array(buffer.length + payload.length);
    out.set(buffer.subarray(0, off), 0);
    out.set(payload, off);
    out.set(buffer.subarray(off), off + payload.length);
    return out;
  }

  function deleteBytes(buffer, offset, count) {
    const off = Math.min(Math.max(0, offset), buffer.length);
    const n = Math.min(Math.max(0, count), buffer.length - off);
    const out = new Uint8Array(buffer.length - n);
    out.set(buffer.subarray(0, off), 0);
    out.set(buffer.subarray(off + n), off);
    return out;
  }

  function setFileSize(buffer, newSize) {
    const n = Math.max(0, newSize | 0);
    const out = new Uint8Array(n);
    out.set(buffer.subarray(0, Math.min(buffer.length, n)), 0);
    return out;
  }

  function decodePayloadHexOrBase64(str) {
    const raw = (str || '').trim();
    if (!raw) {
      return { ok: false, error: 'empty payload' };
    }
    const hex = parseHexPattern(raw.replace(/\s/g, ''));
    if (hex) {
      return { ok: true, bytes: hex };
    }
    const b64 = raw.replace(/\s/g, '');
    if (looksLikeBase64(b64)) {
      try {
        const bin = atob(b64);
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) {
          out[i] = bin.charCodeAt(i);
        }
        return { ok: true, bytes: out };
      } catch (_e) {
        return { ok: false, error: 'invalid base64' };
      }
    }
    return { ok: false, error: 'use hex pairs or base64' };
  }

  function newEmptyBinaryBuffer() {
    return new Uint8Array(DEFAULT_NEW_BINARY_SIZE);
  }

  global.FleditorHex = {
    BYTES_PER_LINE,
    LINES_PER_PAGE,
    BYTES_PER_PAGE,
    DEFAULT_NEW_BINARY_SIZE,
    newEmptyBinaryBuffer,
    copyBytes,
    bytesEqual,
    pageCountForLength,
    bytesOnPage,
    pageByteOffset,
    hexDumpPageFromBytes,
    hexDumpAllFromBytes,
    hexDumpPageToBytes,
    parseByteOffset,
    parseSearchPattern,
    findAllMatches,
    findMatch,
    findMatchWrap,
    insertBytes,
    deleteBytes,
    setFileSize,
    decodePayloadHexOrBase64,
  };
})(typeof window !== 'undefined' ? window : globalThis);
