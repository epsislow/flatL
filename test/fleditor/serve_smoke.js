#!/usr/bin/env node
'use strict';

const http = require('http');
const { spawn } = require('child_process');
const path = require('path');

const repo = path.resolve(__dirname, '../..');
const serve = path.join(repo, 'docs/fleditor_site/node/serve.js');
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
  http.get(`http://127.0.0.1:${port}/fleditor_site/index.html`, (res) => {
    let body = '';
    res.on('data', (c) => (body += c));
    res.on('end', () => {
      if (res.statusCode !== 200) fail('status ' + res.statusCode);
      if (!body.includes('flatL fleditor')) fail('unexpected body');
      if (!body.includes('id="panelDock"')) fail('missing panel dock');
      if (!body.includes('id="dockResetLayoutBtn"')) fail('missing dock reset layout');
      if (!body.includes('fleditor-layout.js')) fail('missing layout script');
      if (!body.includes('fleditor-theme.js')) fail('missing theme script');
      if (!body.includes('fleditor-settings.js')) fail('missing settings script');
      if (!body.includes('id="settingsModal"')) fail('missing settings modal');
      if (!body.includes('id="settingsBtn"')) fail('missing settings button');
      if (!body.includes('id="settingsSectionAbout"')) fail('missing settings About section');
      if (!body.includes('fleditor-version.js')) fail('missing version script');
      if (!body.includes('>+file<')) fail('missing +file button');
      if (!body.includes('id="binaryHexGrid"')) fail('missing hex grid host');
      if (!body.includes('fleditor-hex-grid.js')) fail('missing hex grid script');
      if (!body.includes('id="resetProjectBtn"')) fail('missing reset project control');
      if (!body.includes('fleditor-editor-actions')) fail('missing editor actions bar');
      if (!body.includes('id="undoBtn"')) fail('missing undo in editor bar');
      if (!body.includes('fleditor-editor-actions') || !body.includes('id="runBtn"')) {
        fail('missing Run on editor bar');
      }
      if (!body.includes('fleditor-terminal-head') || !body.includes('id="clearBtn"')) {
        fail('missing terminal Clear control');
      }
      const editorBar = body.match(/fleditor-editor-actions[\s\S]*?<\/div>/);
      if (!editorBar || !editorBar[0].includes('id="saveBtn"')) {
        fail('missing Save on editor bar');
      }
      if (body.includes('id="chipProject"')) fail('legacy Project chip should be removed');
      if (!body.includes('href="../doc_site/index.html"')) fail('missing Docs link');
      http.get(`http://127.0.0.1:${port}/doc_site/index.html`, (rDoc) => {
        let docBody = '';
        rDoc.on('data', (c) => (docBody += c));
        rDoc.on('end', () => {
          if (rDoc.statusCode !== 200 || !docBody.includes('flatL documentation')) {
            fail('doc_site not served at /doc_site/');
          }
          http.get(`http://127.0.0.1:${port}/fleditor_site/examples/manifest.json`, (r2) => {
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
