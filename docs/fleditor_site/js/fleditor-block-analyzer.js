/* global FleditorBlockAnalyzer */
(function (root) {
  'use strict';

  function normalizePath(path) {
    const parts = [];
    String(path || '').replace(/\\/g, '/').split('/').forEach(function (part) {
      if (!part || part === '.') return;
      if (part === '..') {
        if (parts.length) parts.pop();
      } else {
        parts.push(part);
      }
    });
    return parts.join('/');
  }

  function dirname(path) {
    const p = normalizePath(path).split('/');
    p.pop();
    return p.join('/');
  }

  function parseMapDirective(text) {
    const match = String(text || '').trim().match(/^@map\s+target\s*=\s*(.+?)\s*$/i);
    if (!match) return null;
    const targets = match[1].split(',').map(function (item) {
      return item.trim();
    }).filter(function (item) {
      return /^#[A-Za-z][A-Za-z0-9_.:-]*$/.test(item);
    });
    return targets.length ? Array.from(new Set(targets)) : null;
  }

  function collectLines(source) {
    const raw = String(source || '').replace(/\r\n?/g, '\n').split('\n');
    const out = [];
    let inBlockComment = false;
    let blockCommentLines = [];
    let pendingMapTargets = null;

    function collectHint(commentLines) {
      commentLines.forEach(function (commentLine) {
        const targets = parseMapDirective(commentLine.replace(/^\s*--\s?/, '').trim());
        if (targets) pendingMapTargets = targets;
      });
    }

    raw.forEach(function (line, index) {
      const trimmed = line.trim();
      if (inBlockComment) {
        if (trimmed.startsWith('-->')) {
          collectHint(blockCommentLines);
          blockCommentLines = [];
          inBlockComment = false;
        } else {
          blockCommentLines.push(trimmed);
        }
        out.push({ line: index + 1, text: '', ignored: true });
        return;
      }
      if (trimmed.startsWith('--<')) {
        inBlockComment = true;
        blockCommentLines = [];
        out.push({ line: index + 1, text: '', ignored: true });
        return;
      }
      if (trimmed.startsWith('--')) {
        collectHint([trimmed]);
        out.push({ line: index + 1, text: '', ignored: true });
        return;
      }
      if (!trimmed) {
        out.push({ line: index + 1, text: '', ignored: true });
        return;
      }
      out.push({
        line: index + 1,
        text: line,
        ignored: false,
        mapTargets: pendingMapTargets
      });
      pendingMapTargets = null;
    });
    return out;
  }

  function parseBlockDeclaration(text) {
    if (!text || text.charAt(0) !== '#') return null;
    const m = text.match(/^#([^\s(]+)(?:\([^\n]*\))?\s*$/);
    if (!m || !/^[A-Za-z]/.test(m[1])) return null;
    return '#' + m[1];
  }

  function hasGuardOperator(text) {
    let quote = '';
    let escaped = false;
    for (let i = 0; i < text.length; i += 1) {
      const ch = text[i];
      if (quote) {
        if (escaped) escaped = false;
        else if (ch === '\\') escaped = true;
        else if (ch === quote) quote = '';
        continue;
      }
      if (ch === '\"' || ch === "'") {
        quote = ch;
        continue;
      }
      if (ch === '=' && text[i + 1] === '=') return true;
      if (ch === '<' || ch === '>') {
        if (ch === '>' && text[i - 1] === '-') continue;
        if (text[i + 1] === '=') return true;
        return true;
      }
    }
    return false;
  }

  function splitArguments(text) {
    const args = [];
    let start = 0;
    let depth = 0;
    let quote = '';
    let escaped = false;
    for (let i = 0; i < text.length; i += 1) {
      const ch = text[i];
      if (quote) {
        if (escaped) escaped = false;
        else if (ch === '\\') escaped = true;
        else if (ch === quote) quote = '';
        continue;
      }
      if (ch === '"' || ch === "'") quote = ch;
      else if (ch === '(' || ch === '[' || ch === '{') depth += 1;
      else if (ch === ')' || ch === ']' || ch === '}') depth -= 1;
      else if (ch === ',' && depth === 0) {
        args.push(text.slice(start, i).trim());
        start = i + 1;
      }
    }
    args.push(text.slice(start).trim());
    return args;
  }

  function callArguments(line, keyword) {
    const m = line.match(new RegExp('^\\s*' + keyword + '\\s*\\('));
    if (!m) return null;
    const open = line.indexOf('(', m[0].length - 1);
    let depth = 0;
    let quote = '';
    let escaped = false;
    for (let i = open; i < line.length; i += 1) {
      const ch = line[i];
      if (quote) {
        if (escaped) escaped = false;
        else if (ch === '\\') escaped = true;
        else if (ch === quote) quote = '';
        continue;
      }
      if (ch === '"' || ch === "'") quote = ch;
      else if (ch === '(') depth += 1;
      else if (ch === ')') {
        depth -= 1;
        if (depth === 0) return splitArguments(line.slice(open + 1, i));
      }
    }
    return null;
  }

  function qualifyBlock(name, prefix, forcePrefix) {
    if (!name || !prefix) return name;
    if (name.startsWith('#or.#')) {
      const local = name.slice(5);
      return forcePrefix || !local.startsWith(prefix) ? '#or.#' + prefix + local : name;
    }
    if (name.startsWith('#panic.#')) {
      const local = name.slice(8);
      return forcePrefix || !local.startsWith(prefix) ? '#panic.#' + prefix + local : name;
    }
    if (name.startsWith('#or.') || name.startsWith('#panic.')) return name;
    if (name.charAt(0) === '#') {
      return forcePrefix || !name.startsWith('#' + prefix) ? '#' + prefix + name.slice(1) : name;
    }
    return name;
  }

  function unquoteBlock(value) {
    const v = String(value || '').trim();
    const m = v.match(/^(['"])(#[^'"]+)\1$/);
    if (m) return m[2];
    return /^#[A-Za-z][A-Za-z0-9_.:-]*$/.test(v) ? v : null;
  }

  function addEdge(edges, edgeMap, edge) {
    const key = edge.from + '\u0000' + edge.to + '\u0000' + edge.type + '\u0000' + (edge.resolution || 'static');
    let existing = edgeMap[key];
    if (!existing) {
      existing = Object.assign({}, edge, { lines: [] });
      edgeMap[key] = existing;
      edges.push(existing);
    }
    if (edge.line && !existing.lines.includes(edge.line)) existing.lines.push(edge.line);
  }

  function analyze(files, entryPath) {
    const paths = Object.keys(files || {}).filter(function (p) {
      return /\.fl$/i.test(p) && typeof files[p] === 'string';
    }).map(normalizePath).sort();
    const fileRecords = {};
    paths.forEach(function (path) {
      fileRecords[path] = { path: path, prefix: '', imports: [], blocks: [] };
      const lines = collectLines(files[path]);
      let current = null;
      let instructionIndex = 0;
      lines.forEach(function (item) {
        if (item.ignored) return;
        const text = item.text;
        if (text === text.trim()) {
          const declared = parseBlockDeclaration(text.trim());
          if (declared) {
            current = { name: declared, file: path, startLine: item.line, endLine: item.line, instructions: [] };
            fileRecords[path].blocks.push(current);
            instructionIndex = 0;
            return;
          }
        }
        if (!current) {
          const useMatch = text.match(/^\s*use\s+(['"])(.*?)\1\s+as\s+(#[A-Za-z][A-Za-z0-9_-]*)\s*$/);
          if (useMatch) {
            const imported = normalizePath(dirname(path) + '/' + useMatch[2]);
            fileRecords[path].imports.push({ path: imported, prefix: useMatch[3].slice(1) });
          }
          return;
        }
        current.endLine = item.line;
        if (!text.trim()) return;
        instructionIndex += 1;
        current.instructions.push({ line: item.line, index: instructionIndex, text: text.trim(), mapTargets: item.mapTargets || null });
      });
    });

    const rootPath = normalizePath(entryPath || 'main.fl');
    const queue = [{ path: rootPath, prefix: '' }];
    const visited = Object.create(null);
    const reachablePaths = [];
    while (queue.length) {
      const item = queue.shift();
      const record = fileRecords[item.path];
      if (!record) continue;
      const visitKey = item.path + '\u0000' + item.prefix;
      if (visited[visitKey]) continue;
      visited[visitKey] = true;
      if (reachablePaths.indexOf(item.path) === -1) reachablePaths.push(item.path);
      if (!record.prefix) record.prefix = item.prefix;
      record.imports.forEach(function (imp) {
        queue.push({ path: imp.path, prefix: item.prefix + imp.prefix });
      });
    }

    // The map is scoped to the entry file and its transitive `use` dependencies.
    const pathsInContext = reachablePaths;
    const blocks = [];
    const blockIndex = {};
    pathsInContext.forEach(function (path) {
      const record = fileRecords[path];
      record.blocks.forEach(function (block) {
        block.qualifiedName = qualifyBlock(block.name, record.prefix, true);
        block.id = path + '::' + block.name;
        blocks.push(block);
        if (!blockIndex[block.qualifiedName]) blockIndex[block.qualifiedName] = [];
        blockIndex[block.qualifiedName].push(block);
      });
    });

    const edges = [];
    const dynamicNodes = [];
    const edgeMap = Object.create(null);
    function findBlock(name, record) {
      if (!name) return null;
      const exact = blockIndex[name] || [];
      const localName = qualifyBlock(name, record.prefix, false);
      const local = blockIndex[localName] || [];
      const candidates = local.concat(exact).filter(function (block, index, all) {
        return all.indexOf(block) === index;
      });
      if (!candidates.length) return null;
      return candidates.find(function (block) { return block.file === record.path; }) || candidates[0];
    }
    function handlerFor(record, block, instruction, kind) {
      const prefix = kind === 'or' ? '#or.' : '#panic.';
      const lineLabelMatch = instruction.text.match(/#\.([A-Za-z][A-Za-z0-9_]*)\s*$/);
      const candidates = [];
      if (kind === 'or') {
        candidates.push(prefix + block.name + '.' + instruction.index);
        if (lineLabelMatch) candidates.push(prefix + block.name + '.' + lineLabelMatch[1]);
        if (block.name.startsWith('#or.') && lineLabelMatch) candidates.push('#or.' + lineLabelMatch[1]);
      } else {
        candidates.push(prefix + block.name + '.' + instruction.index);
        if (lineLabelMatch) candidates.push(prefix + block.name + '.' + lineLabelMatch[1]);
        candidates.push('#panic.any');
      }
      for (const candidate of candidates) {
        const target = findBlock(candidate, record);
        if (target) return target;
      }
      return null;
    }

    blocks.forEach(function (block) {
      const record = fileRecords[block.file];
      const staticLabels = Object.create(null);
      block.instructions.forEach(function (instruction) {
        const text = instruction.text;
        const labelAssignment = text.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(['"])(#[^'"]+)\2\s*$/);
        if (labelAssignment) staticLabels[labelAssignment[1]] = labelAssignment[3];
        let m = text.match(/^jump\s+(#[A-Za-z][A-Za-z0-9_.:-]*)\s*$/);
        if (m) {
          const target = findBlock(m[1], record);
          if (target) addEdge(edges, edgeMap, { from: block.id, to: target.id, type: 'jump', line: instruction.line });
          return;
        }
        m = text.match(/^call\s+(#[A-Za-z][A-Za-z0-9_.:-]*)\b/);
        if (m) {
          const target = findBlock(m[1], record);
          if (target) addEdge(edges, edgeMap, { from: block.id, to: target.id, type: 'call', line: instruction.line });
          return;
        }

        ['for', 'foreach'].some(function (keyword) {
          const args = callArguments(text, keyword);
          if (!args || args.length < 3) return false;
          const targetExpression = args[2];
          const label = unquoteBlock(targetExpression) || staticLabels[targetExpression] || null;
          if (label) {
            const target = findBlock(label, record);
            if (target) addEdge(edges, edgeMap, { from: block.id, to: target.id, type: keyword, line: instruction.line, resolution: 'static' });
            return true;
          }
          const hintedTargets = instruction.mapTargets || null;
          if (hintedTargets && hintedTargets.length) {
            hintedTargets.forEach(function (hintedName) {
              const hintedTarget = findBlock(hintedName, record);
              if (hintedTarget) {
                addEdge(edges, edgeMap, {
                  from: block.id,
                  to: hintedTarget.id,
                  type: keyword,
                  line: instruction.line,
                  resolution: 'hint',
                  targetExpression: targetExpression
                });
              }
            });
            return true;
          }
          const dynamicId = record.path + '::@dynamic:' + instruction.line + ':' + keyword;
          dynamicNodes.push({
            id: dynamicId,
            name: 'Dynamic target',
            file: record.path,
            startLine: instruction.line,
            endLine: instruction.line,
            kind: 'dynamic',
            expression: targetExpression
          });
          addEdge(edges, edgeMap, {
            from: block.id,
            to: dynamicId,
            type: keyword,
            line: instruction.line,
            resolution: 'dynamic',
            targetExpression: targetExpression
          });
          return true;
        });

        const isGuard = hasGuardOperator(text) && !/^\s*(?:jump|call)\b/.test(text);
        const isLogicalNativeFailure = /^~\s*/.test(text);
        if (isGuard || isLogicalNativeFailure) {
          const target = handlerFor(record, block, instruction, 'or');
          if (target) addEdge(edges, edgeMap, { from: block.id, to: target.id, type: 'or', line: instruction.line });
        }

        // PANIC handlers are reachable from operations that can report infrastructure errors.
        // Keep one edge per source/target/type and retain all source lines on that edge.
        const withoutModifier = text.replace(/^[?~]\s*/, '');
        const instructionBody = withoutModifier.replace(/\s+#\.[A-Za-z][A-Za-z0-9_]*\s*$/, '').trim();
        const explicitPanic = instructionBody === 'PANIC';
        if (explicitPanic) {
          const target = handlerFor(record, block, instruction, 'panic');
          if (target) addEdge(edges, edgeMap, { from: block.id, to: target.id, type: 'panic', line: instruction.line });
          return;
        }

        // Automatic VM panics (native/assignment/loop/jump/call-block failures)
        // route to #panic.any. Only the explicit PANIC instruction uses indexed handlers.
        const nativeCall = /(?:^|=|->)\s*[A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_.]*\s*\(/.test(instructionBody);
        const assignment = /^[A-Za-z_][A-Za-z0-9_.]*\s*=(?!=|>)/.test(instructionBody);
        const loopCall = /^(?:for|foreach)\s*\(/.test(instructionBody);
        const jumpCall = /^jump\s+/.test(instructionBody);
        const callBlock = /^call\s+block\b/.test(instructionBody);
        const hasModifier = /^[?~]/.test(text);
        const mayPanic = (!hasModifier && (nativeCall || assignment)) || loopCall || jumpCall || callBlock || isGuard;
        if (mayPanic) {
          const target = findBlock('#panic.any', record);
          if (target) addEdge(edges, edgeMap, { from: block.id, to: target.id, type: 'panic', line: instruction.line });
        }
      });
    });

    edges.forEach(function (edge) { edge.lines.sort(function (a, b) { return a - b; }); });
    return {
      blocks: blocks,
      dynamicNodes: dynamicNodes,
      edges: edges,
      entryPath: rootPath,
      files: pathsInContext.map(function (path) {
        return { path: path, prefix: fileRecords[path].prefix, blockCount: fileRecords[path].blocks.length };
      }),
      unresolved: []
    };
  }

  root.FleditorBlockAnalyzer = { analyze: analyze };
}(typeof window !== 'undefined' ? window : this));
