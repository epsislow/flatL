/* global TextEncoder, TextDecoder, DecompressionStream */
(function (global) {
  'use strict';

  const MAX_FILES = 10000;
  const MAX_TOTAL_SIZE = 100 * 1024 * 1024;
  const MAX_FILE_SIZE = 50 * 1024 * 1024;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder('utf-8', { fatal: true });
  const crcTable = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    crcTable[n] = c >>> 0;
  }

  function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = crcTable[(c ^ bytes[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function safePath(path) {
    if (typeof path !== 'string' || !path || path.indexOf('\0') >= 0) return '';
    const p = path.replace(/\\/g, '/');
    if (p.startsWith('/') || /^[a-zA-Z]:/.test(p)) return '';
    const parts = p.split('/');
    if (parts.some(x => x === '..')) return '';
    const clean = parts.filter(x => x && x !== '.');
    return clean.join('/');
  }

  function dosDateTime(date) {
    const year = Math.max(1980, date.getFullYear());
    return {
      time: (date.getHours() << 11) | (date.getMinutes() << 5) | Math.floor(date.getSeconds() / 2),
      date: ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate()
    };
  }

  function createZip(entries) {
    const local = [];
    const central = [];
    let offset = 0;
    const stamp = dosDateTime(new Date());
    for (const entry of entries) {
      const name = safePath(entry.path);
      if (!name) throw new Error('Invalid archive path: ' + entry.path);
      const nameBytes = encoder.encode(name);
      const data = entry.bytes instanceof Uint8Array ? entry.bytes : new Uint8Array(entry.bytes || 0);
      if (data.length > MAX_FILE_SIZE) throw new Error('File too large: ' + name);
      const crc = crc32(data);
      const h = new Uint8Array(30 + nameBytes.length);
      const v = new DataView(h.buffer);
      v.setUint32(0, 0x04034b50, true);
      v.setUint16(4, 20, true);
      v.setUint16(6, 0x0800, true);
      v.setUint16(8, 0, true);
      v.setUint16(10, stamp.time, true);
      v.setUint16(12, stamp.date, true);
      v.setUint32(14, crc, true);
      v.setUint32(18, data.length, true);
      v.setUint32(22, data.length, true);
      v.setUint16(26, nameBytes.length, true);
      v.setUint16(28, 0, true);
      h.set(nameBytes, 30);
      local.push(h, data);

      const c = new Uint8Array(46 + nameBytes.length);
      const cv = new DataView(c.buffer);
      cv.setUint32(0, 0x02014b50, true);
      cv.setUint16(4, 20, true);
      cv.setUint16(6, 20, true);
      cv.setUint16(8, 0x0800, true);
      cv.setUint16(10, 0, true);
      cv.setUint16(12, stamp.time, true);
      cv.setUint16(14, stamp.date, true);
      cv.setUint32(16, crc, true);
      cv.setUint32(20, data.length, true);
      cv.setUint32(24, data.length, true);
      cv.setUint16(28, nameBytes.length, true);
      cv.setUint16(30, 0, true);
      cv.setUint16(32, 0, true);
      cv.setUint16(34, 0, true);
      cv.setUint16(36, 0, true);
      cv.setUint32(38, 0, true);
      cv.setUint32(42, offset, true);
      c.set(nameBytes, 46);
      central.push(c);
      offset += h.length + data.length;
    }
    const centralSize = central.reduce((n, x) => n + x.length, 0);
    const end = new Uint8Array(22);
    const ev = new DataView(end.buffer);
    ev.setUint32(0, 0x06054b50, true);
    ev.setUint16(4, 0, true);
    ev.setUint16(6, 0, true);
    ev.setUint16(8, entries.length, true);
    ev.setUint16(10, entries.length, true);
    ev.setUint32(12, centralSize, true);
    ev.setUint32(16, offset, true);
    ev.setUint16(20, 0, true);
    const total = offset + centralSize + end.length;
    const out = new Uint8Array(total);
    let pos = 0;
    for (const part of local.concat(central, [end])) { out.set(part, pos); pos += part.length; }
    return out;
  }

  async function inflateRaw(bytes, expectedSize) {
    if (typeof DecompressionStream !== 'function') {
      throw new Error('This browser cannot decompress ZIP entries. Try an uncompressed ZIP or update the browser.');
    }
    let stream;
    try {
      stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
    } catch (_e) {
      throw new Error('This browser does not support DEFLATE ZIP entries.');
    }
    const reader = stream.getReader();
    const chunks = [];
    let total = 0;
    while (true) {
      const part = await reader.read();
      if (part.done) break;
      total += part.value.length;
      if (total > expectedSize || total > MAX_FILE_SIZE) {
        await reader.cancel();
        throw new Error('ZIP entry expands beyond its declared or allowed size.');
      }
      chunks.push(part.value);
    }
    const out = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) { out.set(chunk, offset); offset += chunk.length; }
    return out;
  }

  async function readZip(arrayBuffer) {
    const bytes = new Uint8Array(arrayBuffer);
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const min = Math.max(0, bytes.length - 65557);
    let eocd = -1;
    for (let i = bytes.length - 22; i >= min; i--) {
      if (view.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error('Invalid ZIP: end record not found.');
    if (view.getUint16(eocd + 4, true) || view.getUint16(eocd + 6, true)) throw new Error('Multi-disk ZIP archives are not supported.');
    const count = view.getUint16(eocd + 10, true);
    const cdSize = view.getUint32(eocd + 12, true);
    let pos = view.getUint32(eocd + 16, true);
    if (count === 0xffff || cdSize === 0xffffffff || pos === 0xffffffff) throw new Error('ZIP64 archives are not supported yet.');
    if (count > MAX_FILES || pos + cdSize > bytes.length) throw new Error('Invalid or oversized ZIP directory.');
    const out = [];
    const seenPaths = new Set();
    let total = 0;
    for (let i = 0; i < count; i++) {
      if (pos + 46 > bytes.length || view.getUint32(pos, true) !== 0x02014b50) throw new Error('Invalid ZIP central directory.');
      const flags = view.getUint16(pos + 8, true);
      const method = view.getUint16(pos + 10, true);
      const crc = view.getUint32(pos + 16, true);
      const compSize = view.getUint32(pos + 20, true);
      const size = view.getUint32(pos + 24, true);
      const nameLen = view.getUint16(pos + 28, true);
      const extraLen = view.getUint16(pos + 30, true);
      const commentLen = view.getUint16(pos + 32, true);
      const localOffset = view.getUint32(pos + 42, true);
      if (flags & 1) throw new Error('Encrypted ZIP entries are not supported.');
      if (size === 0xffffffff || compSize === 0xffffffff || localOffset === 0xffffffff) throw new Error('ZIP64 entries are not supported yet.');
      if (pos + 46 + nameLen + extraLen + commentLen > bytes.length) throw new Error('Invalid ZIP entry metadata.');
      let name;
      try {
        const nameBytes = bytes.subarray(pos + 46, pos + 46 + nameLen);
        name = (flags & 0x0800) ? decoder.decode(nameBytes) : new TextDecoder('utf-8').decode(nameBytes);
      } catch (_e) { throw new Error('ZIP contains an invalid filename.'); }
      pos += 46 + nameLen + extraLen + commentLen;
      if (name.endsWith('/')) continue;
      const path = safePath(name);
      if (!path) throw new Error('Unsafe ZIP path rejected: ' + name);
      if (seenPaths.has(path)) throw new Error('Duplicate ZIP path rejected: ' + path);
      seenPaths.add(path);
      if (size > MAX_FILE_SIZE || (total += size) > MAX_TOTAL_SIZE) throw new Error('ZIP exceeds the import size limit (100 MB total; 50 MB per file).');
      if (localOffset + 30 > bytes.length || view.getUint32(localOffset, true) !== 0x04034b50) throw new Error('Invalid ZIP local header.');
      const localMethod = view.getUint16(localOffset + 8, true);
      const localNameLen = view.getUint16(localOffset + 26, true);
      const localExtraLen = view.getUint16(localOffset + 28, true);
      if (localMethod !== method || localOffset + 30 + localNameLen > bytes.length) throw new Error('ZIP local and central headers do not match: ' + path);
      const localName = new TextDecoder('utf-8').decode(bytes.subarray(localOffset + 30, localOffset + 30 + localNameLen));
      if (localName !== name) throw new Error('ZIP entry name mismatch: ' + path);
      const dataStart = localOffset + 30 + localNameLen + localExtraLen;
      if (dataStart + compSize > bytes.length) throw new Error('Truncated ZIP entry: ' + path);
      const compressed = bytes.subarray(dataStart, dataStart + compSize);
      let data;
      if (method === 0) data = new Uint8Array(compressed);
      else if (method === 8) data = await inflateRaw(compressed, size);
      else throw new Error('Unsupported ZIP compression method ' + method + ' for ' + path);
      if (data.length !== size || crc32(data) !== crc) throw new Error('ZIP integrity check failed for ' + path);
      out.push({ path, bytes: data });
    }
    return out;
  }

  global.FleditorProjectArchive = { createZip, readZip, safePath, MAX_FILES, MAX_TOTAL_SIZE };
})(typeof window !== 'undefined' ? window : globalThis);
