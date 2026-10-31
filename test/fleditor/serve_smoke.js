#!/usr/bin/env node
'use strict';

const http = require('http');
const { spawn } = require('child_process');
const path = require('path');

const repo = path.resolve(__dirname, '../..');
const serve = path.join(repo, 'fleditor_site/node/serve.js');
const port = 18765;

const child = spawn(process.execPath, [serve, String(port)], {
  cwd: repo,
  stdio: ['ignore', 'pipe', 'pipe'],
});

function fail(msg) {
  process.stderr.write('serve_smoke: ' + msg + '\n');
  child.kill();
  process.exit(1);
}

child.on('error', (e) => fail(e.message));

setTimeout(() => {
  http.get(`http://127.0.0.1:${port}/index.html`, (res) => {
    let body = '';
    res.on('data', (c) => (body += c));
    res.on('end', () => {
      child.kill();
      if (res.statusCode !== 200) fail('status ' + res.statusCode);
      if (!body.includes('flatL fleditor')) fail('unexpected body');
      process.stdout.write('OK: fleditor serve smoke\n');
      process.exit(0);
    });
  }).on('error', (e) => fail(e.message));
}, 400);
