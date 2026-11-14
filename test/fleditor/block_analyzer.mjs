#!/usr/bin/env node
/** Smoke tests for FlatL Code Map block/transfer analysis. */
import { createRequire } from 'module';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(__dirname, '../..');
const require = createRequire(import.meta.url);
const analyzerModule = require(path.join(repo, 'docs/fleditor_site/js/fleditor-block-analyzer.js'));
const { analyze } = analyzerModule.FleditorBlockAnalyzer;

const files = {
  'main.fl': [
    'use "lib/math.fl" as #math_',
    'use "lib/caller.fl" as #caller_',
    '',
    '#main',
    '  jump #start',
    '  call #math_add',
    "  loopLabel = '#loop'",
    '  for(3, i, loopLabel)',
    "  foreach(items, item, '#loop')",
    '  ready == true #.ready',
    '  ~ io.try_read() #.soft',
    '  io.read() -> value',
    '  PANIC',
    '  total = 10',
    '  PANIC #.manual',
    "  dynamicTarget = prefix + '_round'",
    '  -- @map target=#round,#retry',
    '  for(5, i, dynamicTarget)',
    '  --<',
    '      @map target=#round,#retry',
    '  -->',
    '  foreach(items, item, anotherDynamicTarget)',
    '  for(2, i, unresolvedTarget)',
    '',
    '#or.#main.ready',
    '  jump #done',
    '#or.#main.soft',
    '  return',
    '#panic.#main.9',
    '  jump #failed',
    '#panic.#main.manual',
    '  jump #failed',
    '#panic.any',
    '  jump #failed',
    '#start',
    '  jump #done',
    '#loop',
    '  next',
    '#round',
    '  next',
    '#retry',
    '  next',
    '#done',
    '  OK',
    '#failed',
    '  FAIL',
    '#orphan',
    '  OK'
  ].join('\n'),
  'lib/math.fl': [
    '#add',
    '  result = 1',
    '  return'
  ].join('\n'),
  'lib/caller.fl': [
    'use "nested/extra.fl" as #extra_',
    '#invoke',
    '  call #extra_target',
    '  return'
  ].join('\n'),
  'lib/nested/extra.fl': '#target\n  return',
  'unrelated/unused.fl': '#not_imported\n  OK'
};

const model = analyze(files, 'main.fl');
const block = (file, name) => model.blocks.find((b) => b.file === file && b.name === name);
const edge = (from, to, type) => model.edges.find((e) => e.from === from.id && e.to === to.id && e.type === type);

const main = block('main.fl', '#main');
const start = block('main.fl', '#start');
const add = block('lib/math.fl', '#add');
const invoke = block('lib/caller.fl', '#invoke');
const nestedTarget = block('lib/nested/extra.fl', '#target');
const loop = block('main.fl', '#loop');
const orphan = block('main.fl', '#orphan');
const orReady = block('main.fl', '#or.#main.ready');
const panic = block('main.fl', '#panic.#main.9');
const panicManual = block('main.fl', '#panic.#main.manual');
const panicAny = block('main.fl', '#panic.any');
const round = block('main.fl', '#round');
const retry = block('main.fl', '#retry');
if (block('unrelated/unused.fl', '#not_imported')) throw new Error('unimported project file leaked into the entry map');
if (model.files.some((file) => file.path === 'unrelated/unused.fl')) throw new Error('unimported file was included in the map file list');
if (model.entryPath !== 'main.fl') throw new Error('entry path was not retained in the model');

if (!main || !start || !add || !invoke || !nestedTarget || !loop || !orphan || !orReady || !panic || !panicManual || !panicAny || !round || !retry) {
  throw new Error('block declarations were not indexed');
}
if (add.qualifiedName !== '#math_add') throw new Error('module prefix was not applied: ' + add.qualifiedName);
if (invoke.qualifiedName !== '#caller_invoke') throw new Error('second module prefix was not applied: ' + invoke.qualifiedName);
if (nestedTarget.qualifiedName !== '#caller_extra_target') throw new Error('nested module prefix was not applied: ' + nestedTarget.qualifiedName);
if (!edge(invoke, nestedTarget, 'call')) throw new Error('nested imported cross-module call was not resolved');
if (!edge(main, start, 'jump')) throw new Error('jump edge missing');
if (!edge(main, add, 'call')) throw new Error('module call edge missing');
if (!edge(main, loop, 'for') || !edge(main, loop, 'foreach')) throw new Error('loop body edge missing');

if (!model.edges.some((e) => e.from === main.id && e.to === round.id && e.type === 'for' && e.resolution === 'hint')) {
  throw new Error('single-line @map target hint was not applied');
}
if (!model.edges.some((e) => e.from === main.id && e.to === retry.id && e.type === 'for' && e.resolution === 'hint')) {
  throw new Error('multiple targets from single-line @map hint were not resolved');
}
if (!model.edges.some((e) => e.from === main.id && e.to === round.id && e.type === 'foreach' && e.resolution === 'hint')) {
  throw new Error('block-comment @map target hint was not applied');
}
if (!model.edges.some((e) => e.from === main.id && e.to === retry.id && e.type === 'foreach' && e.resolution === 'hint')) {
  throw new Error('multiple targets from block-comment @map hint were not resolved');
}
if (!model.dynamicNodes || model.dynamicNodes.length !== 1) {
  throw new Error('unhinted dynamic loop target should create one dynamic node');
}
if (!model.edges.some((e) => e.to === model.dynamicNodes[0].id && e.resolution === 'dynamic')) {
  throw new Error('unhinted dynamic loop target edge missing');
}
if (!edge(main, orReady, 'or')) throw new Error('guard #or edge missing');
if (!model.edges.some((e) => e.from === main.id && e.to === panic.id && e.type === 'panic')) {
  throw new Error('indexed panic handler edge missing');
}
if (!model.edges.some((e) => e.from === main.id && e.to === panic.id && e.type === 'panic')) {
  throw new Error('indexed PANIC instruction edge missing');
}
if (!model.edges.some((e) => e.from === main.id && e.to === panicManual.id && e.type === 'panic')) {
  throw new Error('line-labelled PANIC instruction edge missing');
}
if (!model.edges.some((e) => e.from === main.id && e.to === panicAny.id && e.type === 'panic')) {
  throw new Error('panic.any fallback edge missing');
}
if (model.edges.some((e) => e.from === main.id && e.to === panic.id && e.lines.includes(8))) {
  throw new Error('automatic native panic must use #panic.any, not an indexed handler');
}
if (model.edges.some((e) => e.type === 'next' || e.type === 'return')) {
  throw new Error('next/return edges must not be emitted');
}
if (model.edges.some((e) => e.from === orphan.id || e.to === orphan.id)) {
  throw new Error('orphan block should remain disconnected');
}

console.log('OK: fleditor block analyzer smoke (' + model.blocks.length + ' blocks, ' + model.edges.length + ' transfers)');
