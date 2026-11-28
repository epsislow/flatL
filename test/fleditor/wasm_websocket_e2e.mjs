#!/usr/bin/env node
/**
 * FlatL-level WebSocket E2E for the FLEditor WASM backend.
 * A local RFC6455 server talks to a real FlatL program running in the
 * Emscripten module. No npm WebSocket server dependency is required.
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

function makeServer() {
  let serverFailed = null;
  let clientSocket = null;
  const server = net.createServer((socket) => {
    clientSocket = socket;
    let input = Buffer.alloc(0);
    let upgraded = false;
    let clientMessages = 0;
    const fail = (message) => { serverFailed ??= new Error(message); socket.destroy(); };
    socket.on('error', (error) => { if (!serverFailed) serverFailed = error; });
    socket.on('data', (chunk) => {
      input = Buffer.concat([input, chunk]);
      if (!upgraded) {
        const end = input.indexOf('\r\n\r\n');
        if (end < 0) return;
        const request = input.subarray(0, end).toString('latin1');
        const key = /^sec-websocket-key:\s*(.+)$/im.exec(request)?.[1]?.trim();
        if (!key) return fail('missing Sec-WebSocket-Key');
        const accept = crypto.createHash('sha1')
          .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
        socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
        input = input.subarray(end + 4);
        upgraded = true;
        socket.write(frame(1, 'server-text'));
        socket.write(frame(2, Buffer.from('server-binary')));
      }
      while (upgraded && input.length >= 2) {
        const opcode = input[0] & 0x0f;
        const masked = (input[1] & 0x80) !== 0;
        let length = input[1] & 0x7f;
        let offset = 2;
        if (length === 126) { if (input.length < 4) return; length = input.readUInt16BE(2); offset = 4; }
        else if (length === 127) {
          if (input.length < 10) return;
          const n = input.readBigUInt64BE(2);
          if (n > 1_000_000n) return fail('oversized client frame');
          length = Number(n); offset = 10;
        }
        const maskLength = masked ? 4 : 0;
        if (input.length < offset + maskLength + length) return;
        const mask = masked ? input.subarray(offset, offset + 4) : null;
        offset += maskLength;
        const payload = Buffer.from(input.subarray(offset, offset + length));
        if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
        input = input.subarray(offset + length);
        if (opcode === 8) { socket.write(frame(8)); socket.end(); return; }
        if (opcode !== 1 && opcode !== 2) continue;
        clientMessages++;
        const expected = clientMessages === 1 ? 'client-text' : 'client-binary';
        if (payload.toString('utf8') !== expected || (clientMessages === 1 && opcode !== 1) ||
            (clientMessages === 2 && opcode !== 2)) return fail(`unexpected client frame ${clientMessages}: opcode=${opcode}, payload=${payload.toString('hex')}`);
        socket.write(frame(opcode, payload));
        if (clientMessages === 2) {
          /* Both FlatL sends have been echoed; now exercise peer-initiated close. */
          socket.write(frame(8));
          socket.end();
          return;
        }
      }
    });
  });
  return {
    server,
    get error() { return serverFailed; },
    async close() {
      if (clientSocket && !clientSocket.destroyed) clientSocket.destroy();
      await new Promise((resolve) => server.close(() => resolve()));
    },
  };
}

const testServer = makeServer();
await new Promise((resolve, reject) => {
  testServer.server.once('error', reject);
  testServer.server.listen(0, '127.0.0.1', resolve);
});
const address = testServer.server.address();
const url = `ws://127.0.0.1:${address.port}/flatl-e2e`;

try {
  const require = createRequire(import.meta.url);
  const FlatLModule = require(wasmJs);
  let stdout = '';
  let stderr = '';
  const M = await FlatLModule({
    locateFile: (name) => path.join(wasmDir, name),
    flatWasmOnStdout: (bytes) => { stdout += Buffer.from(bytes).toString('utf8'); },
    flatWasmOnStderr: (bytes) => { stderr += Buffer.from(bytes).toString('utf8'); },
  });

  const source = `use native sockets
use native task

#main
  socketId = sockets.wsConnect('${url}', 5000, 4096)
  openSub = task.spawnFor(socketId, 'socket', 'open', #onOpen, socketId)
  receiveSub = task.spawnFor(socketId, 'socket', 'receive', #onReceive, socketId)
  closeSub = task.spawnFor(socketId, 'socket', 'close', #onClose, socketId)
  io.outln('flat-root-registered')
  OK

#onOpen(event, socketId)
  io.outln('flat-open-event')
  sockets.wsSend(socketId, 'client-text')
  sockets.wsSendBinary(socketId, 'client-binary')
  OK

#onReceive(event, socketId)
  io.outln('flat-receive-event')
  io.outln(event.opcode)
  io.outln(event.data)
  OK

#onClose(event, socketId)
  io.outln('flat-close-event')
  OK
`;

  let eventLoopTick = false;
  setTimeout(() => { eventLoopTick = true; }, 0);
  const errPtr = M._malloc(4096);
  assert.ok(errPtr, 'WASM error buffer allocation');
  M.HEAPU8.fill(0, errPtr, errPtr + 4096);
  let rc = M.ccall('flat_wasm_run_source', 'number', ['string', 'number', 'number'],
    [source, errPtr, 4096], { async: true });
  if (rc && typeof rc.then === 'function') rc = await rc;
  const err = M.UTF8ToString(errPtr);
  const termination = M.ccall('flat_wasm_get_last_termination', 'number', [], []);
  M._free(errPtr);

  assert.equal(rc, 0, `FlatL WebSocket E2E failed: rc=${rc}, err=${err}, stderr=${stderr}, stdout=${stdout}`);
  assert.equal(termination, 1, `expected FLAT_TERM_OK, got ${termination}; stderr=${stderr}`);
  assert.equal(eventLoopTick, true, 'Node event loop did not progress while FlatL awaited WebSocket events');
  for (const fragment of [
    'flat-root-registered', 'flat-open-event', 'flat-close-event',
    'server-text', 'server-binary', 'client-text', 'client-binary',
  ]) {
    assert.ok(stdout.includes(fragment), `missing ${JSON.stringify(fragment)} in FlatL output: ${JSON.stringify(stdout)}`);
  }
  assert.ok((stdout.match(/\n1\n/g) || []).length >= 2, `expected text opcode events: ${stdout}`);
  assert.ok((stdout.match(/\n2\n/g) || []).length >= 2, `expected binary opcode events: ${stdout}`);
  assert.equal(testServer.error, null, `local WebSocket server failed: ${testServer.error}`);
  console.log('OK: FlatL WASM WebSocket E2E (open + text/binary send/receive + peer close + runtime cleanup)');
} finally {
  await testServer.close();
}
