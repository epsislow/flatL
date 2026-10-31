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
      if (res.statusCode !== 200) fail('status ' + res.statusCode);
      if (!body.includes('flatL fleditor')) fail('unexpected body');
      if (!body.includes('id="chipProject"')) fail('missing Project chip');
      if (!body.includes('Reset layout')) fail('missing Reset layout');
      if (!body.includes('fleditor-layout.js')) fail('missing layout script');
      if (!body.includes('>+fl<')) fail('missing +fl button');
      if (!body.includes('Reset project')) fail('missing Reset project');
      if (!body.includes('href="/doc_site/index.html"')) fail('missing Docs link');
      http.get(`http://127.0.0.1:${port}/doc_site/index.html`, (rDoc) => {
        let docBody = '';
        rDoc.on('data', (c) => (docBody += c));
        rDoc.on('end', () => {
          if (rDoc.statusCode !== 200 || !docBody.includes('flatL documentation')) {
            fail('doc_site not served at /doc_site/');
          }
          http.get(`http://127.0.0.1:${port}/examples/manifest.json`, (r2) => {
        let b2 = '';
        r2.on('data', (c) => (b2 += c));
        r2.on('end', () => {
          child.kill();
          if (r2.statusCode !== 200 || !b2.includes('hello.fl')) {
            fail('examples manifest not served');
          }
          process.stdout.write('OK: fleditor serve smoke\n');
          process.exit(0);
        });
      }).on('error', (e) => fail(e.message));
        });
      }).on('error', (e) => fail(e.message));
    });
  }).on('error', (e) => fail(e.message));
}, 400);
