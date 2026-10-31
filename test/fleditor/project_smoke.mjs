#!/usr/bin/env node
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const require = createRequire(import.meta.url);
require(path.join(repo, 'fleditor_site/js/fleditor-project.js'));

const P = globalThis.FleditorProject;

let proj = P.defaultProject();
proj.files['lib/a.fl'] = 'x';
proj.files['lib/b.fl'] = 'y';
P.ensureParentDirs(proj, 'lib/a.fl');

const under = P.filesUnderFolder(proj, 'lib');
if (under.length !== 2) {
  console.error('project_smoke: filesUnderFolder', under);
  process.exit(1);
}

P.removeFile(proj, 'lib/a.fl');
if (P.listFilePaths(proj).length !== 2) {
  console.error('project_smoke: removeFile');
  process.exit(1);
}

const n = P.removeFolder(proj, 'lib');
if (n !== 1 || P.listFilePaths(proj).length !== 1) {
  console.error('project_smoke: removeFolder', n, proj.files);
  process.exit(1);
}

proj.dirs = ['empty'];
P.removeEmptyFolder(proj, 'empty');
if ((proj.dirs || []).includes('empty')) {
  console.error('project_smoke: removeEmptyFolder');
  process.exit(1);
}

const reset = P.defaultProject();
if (!reset.files[P.DEFAULT_MAIN] || P.listFilePaths(reset).length !== 1) {
  console.error('project_smoke: default project');
  process.exit(1);
}

console.log('OK: fleditor project smoke');
