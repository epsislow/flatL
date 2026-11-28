#!/usr/bin/env node
/**
 * Headless browser-WebSocket smoke for the Emscripten build.
 * Node 22+ supplies the browser-compatible WebSocket client. A tiny RFC6455
 * echo server is implemented with net so this test has no npm dependency.
 * This verifies the browser API, asynchronous event-loop behavior, text and
 * binary frames, and the FlatL runtime capability probe. It does not replace
 * a real browser UI test of DOM event dispatch.
 */
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import crypto from 'node:crypto';
import net from 'node:net';
import fs from 'node:fs';
import path from 'node:path';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const wasmDir = path.join(repo, 'docs/fleditor_site/wasm');
const wasmJs = path.join(wasmDir, 'flatl.js');
assert.ok(fs.existsSync(wasmJs), `missing ${wasmJs}; run make fleditor-wasm`);
assert.equal(typeof globalThis.WebSocket, 'function', 'Node.js 22+ WebSocket global is required');

function frame(opcode, payload = Buffer.alloc(0)) {
  payload = Buffer.from(payload);
  let head;
  if (payload.length < 126) head = Buffer.from([0x80 | opcode, payload.length]);
  else if (payload.length <= 0xffff) {
    head = Buffer.alloc(4); head[0] = 0x80 | opcode; head[1] = 126; head.writeUInt16BE(payload.length, 2);
  } else {
    head = Buffer.alloc(10); head[0] = 0x80 | opcode; head[1] = 127; head.writeBigUInt64BE(BigInt(payload.length), 2);
  }
  return Buffer.concat([head, payload]);
}

const server = net.createServer((socket) => {
  let input = Buffer.alloc(0);
  let upgraded = false;
  socket.on('data', (chunk) => {
    input = Buffer.concat([input, chunk]);
    if (!upgraded) {
      const end = input.indexOf('\r\n\r\n');
      if (end < 0) return;
      const request = input.subarray(0, end).toString('latin1');
      const key = /^sec-websocket-key:\s*(.+)$/im.exec(request)?.[1]?.trim();
      if (!key) { socket.destroy(); return; }
      const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
      socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
      input = input.subarray(end + 4);
      upgraded = true;
      socket.write(frame(1, 'server-text'));
      socket.write(frame(2, Buffer.from([0x00, 0xff, 0x7f])));
    }
    while (upgraded && input.length >= 2) {
      const opcode = input[0] & 0x0f;
      const masked = (input[1] & 0x80) !== 0;
      let len = input[1] & 0x7f, offset = 2;
      if (len === 126) { if (input.length < 4) return; len = input.readUInt16BE(2); offset = 4; }
      else if (len === 127) { if (input.length < 10) return; const n = input.readBigUInt64BE(2); if (n > 1_000_000n) { socket.destroy(); return; } len = Number(n); offset = 10; }
      const maskBytes = masked ? 4 : 0;
      if (input.length < offset + maskBytes + len) return;
      const mask = masked ? input.subarray(offset, offset + 4) : null;
      offset += maskBytes;
      const payload = Buffer.from(input.subarray(offset, offset + len));
      if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      input = input.subarray(offset + len);
      if (opcode === 8) { socket.write(frame(8)); socket.end(); return; }
      if (opcode === 1 || opcode === 2) socket.write(frame(opcode, payload));
    }
  });
});

await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
const address = server.address();
const url = `ws://127.0.0.1:${address.port}`;
try {
  const require = createRequire(import.meta.url);
  const FlatLModule = require(wasmJs);
  let stdout = '';
  const M = await FlatLModule({
    locateFile: (name) => path.join(wasmDir, name),
    flatWasmOnStdout: (bytes) => { stdout += Buffer.from(bytes).toString('utf8'); },
  });
  const source = `use native sockets\n#main\n  io.outln(sockets.wsSupported())\n  OK\n`;
  const errPtr = M._malloc(4096);
  assert.ok(errPtr);
  M.HEAPU8.fill(0, errPtr, errPtr + 4096);
  let rc = M.ccall('flat_wasm_run_source', 'number', ['string', 'number', 'number'], [source, errPtr, 4096], { async: true });
  if (rc && typeof rc.then === 'function') rc = await rc;
  const err = M.UTF8ToString(errPtr);
  M._free(errPtr);
  assert.equal(rc, 0, `FlatL sockets capability probe failed: ${err}`);
  assert.match(stdout, /true/i, `FlatL sockets.wsSupported() did not report true: ${stdout}`);

  let timerFired = false;
  setTimeout(() => { timerFired = true; }, 0);
  const ws = new WebSocket(url);
  ws.binaryType = 'arraybuffer';
  const received = [];
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('WebSocket echo timed out')), 5000);
    ws.onerror = () => { clearTimeout(timeout); reject(new Error('WebSocket connection failed')); };
    ws.onmessage = (event) => {
      received.push(event.data);
      if (received.length === 2) {
        ws.send('client-text');
        ws.send(new Uint8Array([0x00, 0x7f, 0xff]).buffer);
      }
      if (received.length === 4) { clearTimeout(timeout); resolve(); }
    };
  });
  assert.equal(timerFired, true, 'Node event loop was blocked while WebSocket was pending');
  assert.equal(received[0], 'server-text');
  assert.deepEqual([...new Uint8Array(received[1])], [0x00, 0xff, 0x7f]);
  assert.equal(received[2], 'client-text');
  assert.deepEqual([...new Uint8Array(received[3])], [0x00, 0x7f, 0xff]);
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('WebSocket close timed out')), 3000);
    ws.onclose = () => { clearTimeout(timeout); resolve(); };
    ws.close(1000, 'test complete');
  });
  console.log('OK: wasm WebSocket capability + Node event loop + text/binary echo + close');
} finally {
  await new Promise((resolve) => server.close(resolve));
}
