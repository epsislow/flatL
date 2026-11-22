#!/usr/bin/env node
/** Local block discovery and Code Map edge smoke tests. */
import { createRequire } from 'module';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const require = createRequire(import.meta.url);
const analyzerModule = require(path.join(repo, 'docs/fleditor_site/js/fleditor-block-analyzer.js'));
const { analyze } = analyzerModule.FleditorBlockAnalyzer;

const source = [
  '#main',
  "  for(2, i, '#:')",
  '    call #:',
  "      io.outln('nested')",
  '      return',
  '    next',
  '  foreach(items, item, \'#:\')',
  '    io.outln(item)',
  '    next',
  '  jump #:',
  "    io.outln('jump')",
  '    OK',
  '  false == true',
  "    io.outln('guard')",
  '    FAIL',
  '    false == true',
  "      io.outln('nested guard')",
  '      FAIL',
  "  str.to_num('bad') -> value  #.badNum",
  "    io.outln('panic')",
  '    FAIL',
  '  OK'
].join('\n');

const model = analyze({ 'main.fl': source }, 'main.fl');
const block = (name) => model.blocks.find((item) => item.file === 'main.fl' && item.name === name);
const edge = (from, to, type) => model.edges.find((item) => item.from === from.id && item.to === to.id && item.type === type);

const main = block('#main');
const forBlock = model.blocks.find((item) => item.displayName === '#local_1_for');
const callBlock = model.blocks.find((item) => item.displayName === '#local_2_call');
const foreachBlock = model.blocks.find((item) => item.displayName === '#local_3_foreach');
const jumpBlock = model.blocks.find((item) => item.displayName === '#local_4_jump');
const guardBlock = model.blocks.find((item) => item.displayName === '#local_5_guard');
const nestedGuardBlock = model.blocks.find((item) => item.displayName === '#local_6_guard');
const panicBlock = block('#panic.#main.badNum');

if (!main || !forBlock || !callBlock || !foreachBlock || !jumpBlock || !guardBlock || !nestedGuardBlock || !panicBlock) {
  throw new Error('local blocks were not represented in Code Map');
}
if (!edge(main, forBlock, 'for')) throw new Error('local for edge missing');
if (!edge(forBlock, callBlock, 'call')) throw new Error('nested local call edge missing');
if (!edge(main, foreachBlock, 'foreach')) throw new Error('local foreach edge missing');
if (!edge(main, jumpBlock, 'jump')) throw new Error('local jump edge missing');
if (!edge(main, guardBlock, 'or')) throw new Error('local guard #or edge missing');
if (!edge(guardBlock, nestedGuardBlock, 'or')) throw new Error('nested local guard #or edge missing');
if (!edge(main, panicBlock, 'panic')) throw new Error('local panic edge missing');
if (forBlock.startLine !== 3 || callBlock.startLine !== 4) throw new Error('local source line mapping is incorrect');
if (forBlock.kind !== 'local' || callBlock.kind !== 'local') throw new Error('local blocks are not marked synthetic');

process.stdout.write('OK: Code Map local block analysis\n');
