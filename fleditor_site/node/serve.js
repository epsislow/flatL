#!/usr/bin/env node
/**
 * Static server for fleditor_site/ (127.0.0.1 only).
 * Usage: node fleditor_site/node/serve.js [port]
 * Default port: 8765
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const siteRoot = path.resolve(__dirname, '..');
const port = parseInt(process.argv[2] || '8765', 10);

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.md': 'text/plain; charset=utf-8',
};

function safePath(urlPath) {
  const decoded = decodeURIComponent(urlPath.split('?')[0]);
  let rel = decoded.replace(/^\/+/, '');
  if (rel === '') rel = 'index.html';
  const abs = path.resolve(siteRoot, rel);
  if (!abs.startsWith(siteRoot + path.sep) && abs !== siteRoot) {
    return null;
  }
  return abs;
}

const server = http.createServer((req, res) => {
  const filePath = safePath(req.url || '/');
  if (!filePath) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }
  fs.stat(filePath, (err, st) => {
    if (err || !st.isFile()) {
      res.writeHead(404);
      res.end('Not found');
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    const headers = { 'Content-Type': MIME[ext] || 'application/octet-stream' };
    if (ext === '.js' || ext === '.html' || ext === '.wasm') {
      headers['Cache-Control'] = 'no-cache';
    }
    res.writeHead(200, headers);
    fs.createReadStream(filePath).pipe(res);
  });
});

server.on('error', (err) => {
  if (err && err.code === 'EADDRINUSE') {
    process.stderr.write(
      `fleditor: port ${port} already in use (another serve.js running?).\n` +
        `  Stop it: fuser -k ${port}/tcp   or use another port: node serve.js 8766\n`
    );
    process.exit(1);
  }
  throw err;
});

server.listen(port, '127.0.0.1', () => {
  process.stdout.write(`fleditor: http://127.0.0.1:${port}/\n`);
});
