#!/usr/bin/env node
/**
 * Static server for GitHub Pages layout under docs/ (127.0.0.1 only).
 *   /              → docs/index.html (redirect to fleditor)
 *   /fleditor_site → editor
 *   /doc_site      → offline HTML docs
 * Usage: node docs/fleditor_site/node/serve.js [port]
 * Default port: 8765
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const pagesRoot = path.resolve(__dirname, '..', '..');
const fleditorRoot = path.join(pagesRoot, 'fleditor_site');
const docRoot = path.join(pagesRoot, 'doc_site');
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

function safePathInRoot(root, relPath) {
  let rel = String(relPath || '').replace(/^\/+/, '');
  if (rel === '' || rel.endsWith('/')) {
    rel = rel.replace(/\/+$/, '');
    rel = rel ? rel + '/index.html' : 'index.html';
  }
  const abs = path.resolve(root, rel);
  if (!abs.startsWith(root + path.sep) && abs !== root) {
    return null;
  }
  return abs;
}

function resolveFilePath(urlPath) {
  const decoded = decodeURIComponent(urlPath.split('?')[0]);
  if (decoded === '/' || decoded === '') {
    return safePathInRoot(pagesRoot, 'index.html');
  }
  if (decoded === '/fleditor_site' || decoded.startsWith('/fleditor_site/')) {
    const sub = decoded.slice('/fleditor_site'.length).replace(/^\//, '') || 'index.html';
    return safePathInRoot(fleditorRoot, sub);
  }
  if (decoded === '/doc_site' || decoded.startsWith('/doc_site/')) {
    const sub = decoded.slice('/doc_site'.length).replace(/^\//, '') || 'index.html';
    return safePathInRoot(docRoot, sub);
  }
  let rel = decoded.replace(/^\//, '');
  if (rel === '') {
    rel = 'index.html';
  }
  return safePathInRoot(pagesRoot, rel);
}

const server = http.createServer((req, res) => {
  const filePath = resolveFilePath(req.url || '/');
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
  process.stdout.write(`fleditor: http://127.0.0.1:${port}/fleditor_site/\n`);
  if (fs.existsSync(docRoot)) {
    process.stdout.write(`fleditor: http://127.0.0.1:${port}/doc_site/index.html\n`);
  }
});
