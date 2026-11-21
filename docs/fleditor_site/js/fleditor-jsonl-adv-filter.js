/* JSONL Adv.Filter query language for FLEditor — parse + eval, no DOM */
(function (root) {
  'use strict';

  function fail(msg, pos) {
    const err = { ok: false, error: String(msg || 'invalid query') };
    if (pos != null && pos >= 0) err.pos = pos;
    return err;
  }

  function kw(s) {
    return String(s || '').toLowerCase();
  }

  /* ---------- tokenize ---------- */

  function readString(s, start) {
    let i = start + 1;
    let out = '';
    while (i < s.length) {
      const c = s.charAt(i);
      if (c === '"') return { ok: true, value: out, end: i + 1 };
      if (c === '\\') {
        i++;
        if (i >= s.length) return { ok: false, error: 'unterminated string escape', pos: start };
        const e = s.charAt(i);
        if (e === '"' || e === '\\' || e === '/') out += e;
        else if (e === 'n') out += '\n';
        else if (e === 'r') out += '\r';
        else if (e === 't') out += '\t';
        else if (e === 'u') {
          const hex = s.slice(i + 1, i + 5);
          if (!/^[0-9a-fA-F]{4}$/.test(hex)) {
            return { ok: false, error: 'invalid unicode escape', pos: i };
          }
          out += String.fromCharCode(parseInt(hex, 16));
          i += 4;
        } else {
          return { ok: false, error: 'invalid escape', pos: i };
        }
        i++;
        continue;
      }
      if (c === '\n' || c === '\r') return { ok: false, error: 'unterminated string', pos: start };
      out += c;
      i++;
    }
    return { ok: false, error: 'unterminated string', pos: start };
  }

  function readNumber(s, start) {
    let i = start;
    if (s.charAt(i) === '-') i++;
    if (i >= s.length || s.charAt(i) < '0' || s.charAt(i) > '9') {
      return { ok: false, error: 'invalid number', pos: start };
    }
    while (i < s.length && s.charAt(i) >= '0' && s.charAt(i) <= '9') i++;
    if (s.charAt(i) === '.') {
      i++;
      if (i >= s.length || s.charAt(i) < '0' || s.charAt(i) > '9') {
        return { ok: false, error: 'invalid number', pos: start };
      }
      while (i < s.length && s.charAt(i) >= '0' && s.charAt(i) <= '9') i++;
    }
    if (s.charAt(i) === 'e' || s.charAt(i) === 'E') {
      i++;
      if (s.charAt(i) === '+' || s.charAt(i) === '-') i++;
      if (i >= s.length || s.charAt(i) < '0' || s.charAt(i) > '9') {
        return { ok: false, error: 'invalid number', pos: start };
      }
      while (i < s.length && s.charAt(i) >= '0' && s.charAt(i) <= '9') i++;
    }
    const n = Number(s.slice(start, i));
    if (!Number.isFinite(n)) return { ok: false, error: 'invalid number', pos: start };
    return { ok: true, value: n, end: i };
  }

  function tokenize(input) {
    const s = String(input == null ? '' : input);
    const tokens = [];
    let i = 0;
    while (i < s.length) {
      const c = s.charAt(i);
      if (c === ' ' || c === '\t' || c === '\n' || c === '\r') {
        i++;
        continue;
      }
      const start = i;
      if (c === '*') {
        i++;
        tokens.push({ kind: 'star', pos: start });
        continue;
      }
      if (c === '$') {
        i++;
        tokens.push({ kind: 'dollar', pos: start });
        continue;
      }
      if (c === '.') {
        i++;
        tokens.push({ kind: 'dot', pos: start });
        continue;
      }
      if (c === '[') {
        i++;
        tokens.push({ kind: 'lbracket', pos: start });
        continue;
      }
      if (c === ']') {
        i++;
        tokens.push({ kind: 'rbracket', pos: start });
        continue;
      }
      if (c === '(') {
        i++;
        tokens.push({ kind: 'lparen', pos: start });
        continue;
      }
      if (c === ')') {
        i++;
        tokens.push({ kind: 'rparen', pos: start });
        continue;
      }
      if (c === ',') {
        i++;
        tokens.push({ kind: 'comma', pos: start });
        continue;
      }
      if (c === '!' && s.charAt(i + 1) === '=') {
        i += 2;
        tokens.push({ kind: 'op', value: '!=', pos: start });
        continue;
      }
      if ((c === '<' || c === '>') && s.charAt(i + 1) === '=') {
        i += 2;
        tokens.push({ kind: 'op', value: c + '=', pos: start });
        continue;
      }
      if (c === '=' || c === '<' || c === '>') {
        i++;
        tokens.push({ kind: 'op', value: c, pos: start });
        continue;
      }
      if (c === '"') {
        const str = readString(s, i);
        if (!str.ok) return fail(str.error, str.pos);
        tokens.push({ kind: 'string', value: str.value, pos: start });
        i = str.end;
        continue;
      }
      if (c === '-' || (c >= '0' && c <= '9')) {
        const num = readNumber(s, i);
        if (!num.ok) return fail(num.error, num.pos);
        tokens.push({ kind: 'number', value: num.value, pos: start });
        i = num.end;
        continue;
      }
      if (/[A-Za-z_]/.test(c)) {
        let j = i + 1;
        while (j < s.length && /[A-Za-z0-9_]/.test(s.charAt(j))) j++;
        const raw = s.slice(i, j);
        tokens.push({ kind: 'ident', value: raw, lower: kw(raw), pos: start });
        i = j;
        continue;
      }
      return fail('unexpected character "' + c + '"', start);
    }
    tokens.push({ kind: 'eof', pos: s.length });
    return { ok: true, tokens: tokens };
  }

  /* ---------- parse ---------- */

  function parse(query) {
    const q = String(query == null ? '' : query).trim();
    if (!q) return fail('empty query', 0);
    const tokRes = tokenize(q);
    if (!tokRes.ok) return tokRes;
    const p = { tokens: tokRes.tokens, i: 0 };

    function cur() {
      return p.tokens[p.i];
    }
    function at(kind) {
      return cur().kind === kind;
    }
    function isIdent() {
      return cur().kind === 'ident';
    }
    function isKw() {
      if (!isIdent()) return false;
      for (let a = 0; a < arguments.length; a++) {
        if (cur().lower === arguments[a]) return true;
      }
      return false;
    }
    function eat(kind) {
      if (!at(kind)) throw { message: 'expected ' + kind, pos: cur().pos };
      const t = cur();
      p.i++;
      return t;
    }
    function eatKw() {
      if (!isIdent()) throw { message: 'expected keyword', pos: cur().pos };
      for (let a = 0; a < arguments.length; a++) {
        if (cur().lower === arguments[a]) {
          const t = cur();
          p.i++;
          return t;
        }
      }
      throw { message: 'expected keyword', pos: cur().pos };
    }
    function tryKw() {
      if (!isIdent()) return null;
      for (let a = 0; a < arguments.length; a++) {
        if (cur().lower === arguments[a]) {
          const t = cur();
          p.i++;
          return t;
        }
      }
      return null;
    }

    function parseExpr() {
      return parseOr();
    }
    function parseOr() {
      let left = parseAnd();
      while (tryKw('or')) left = { type: 'or', left: left, right: parseAnd() };
      return left;
    }
    function parseAnd() {
      let left = parseNot();
      while (tryKw('and')) left = { type: 'and', left: left, right: parseNot() };
      return left;
    }
    function parseNot() {
      if (tryKw('not')) return { type: 'not', expr: parseNot() };
      return parsePrimary();
    }
    function parsePrimary() {
      if (at('lparen')) {
        eat('lparen');
        const e = parseExpr();
        eat('rparen');
        return e;
      }
      return parsePredicate();
    }

    function parsePath() {
      eat('dollar');
      const segs = [];
      for (;;) {
        if (at('dot')) {
          eat('dot');
          if (at('string')) segs.push({ kind: 'key', name: eat('string').value });
          else if (isIdent()) segs.push({ kind: 'key', name: eat('ident').value });
          else throw { message: 'expected property name after "."', pos: cur().pos };
          continue;
        }
        if (at('lbracket')) {
          eat('lbracket');
          if (at('rbracket')) {
            eat('rbracket');
            segs.push({ kind: 'listMark' });
            continue;
          }
          if (at('number')) {
            const nTok = eat('number');
            const n = nTok.value;
            if (!Number.isInteger(n) || n < 0) {
              throw { message: 'list index must be a non-negative integer', pos: nTok.pos };
            }
            eat('rbracket');
            segs.push({ kind: 'index', i: n });
            continue;
          }
          if (at('star')) {
            eat('star');
            eat('rbracket');
            segs.push({ kind: 'star' });
            continue;
          }
          throw { message: 'expected index, "*", or "]" in brackets', pos: cur().pos };
        }
        break;
      }
      return { segs: segs };
    }

    function parseValue() {
      if (at('string')) return eat('string').value;
      if (at('number')) return eat('number').value;
      if (tryKw('true')) return true;
      if (tryKw('false')) return false;
      if (tryKw('null')) return null;
      if (at('lbracket')) return parseArrayLiteral();
      throw { message: 'expected value', pos: cur().pos };
    }

    function parseArrayLiteral() {
      eat('lbracket');
      const arr = [];
      if (at('rbracket')) {
        eat('rbracket');
        return arr;
      }
      for (;;) {
        arr.push(parseValue());
        if (at('comma')) {
          eat('comma');
          continue;
        }
        break;
      }
      eat('rbracket');
      return arr;
    }

    function parseCmpOp() {
      if (!at('op')) throw { message: 'expected comparison operator', pos: cur().pos };
      const t = eat('op');
      if (
        t.value !== '=' &&
        t.value !== '!=' &&
        t.value !== '<' &&
        t.value !== '>' &&
        t.value !== '<=' &&
        t.value !== '>='
      ) {
        throw { message: 'unsupported operator ' + t.value, pos: t.pos };
      }
      return t.value;
    }

    function parsePredicate() {
      if (!at('dollar')) throw { message: 'expected path starting with $', pos: cur().pos };
      const path = parsePath();

      if (at('eof') || at('rparen') || isKw('and') || isKw('or')) {
        return { type: 'exists', path: path };
      }

      let negated = false;
      if (isKw('not')) {
        const save = p.i;
        p.i++;
        if (
          isKw('in') ||
          isKw('has') ||
          isKw('contains') ||
          isKw('starts') ||
          isKw('ends') ||
          isKw('type')
        ) {
          negated = true;
        } else {
          p.i = save;
          throw {
            message: 'expected operator after path (or place "not" before the path)',
            pos: cur().pos,
          };
        }
      }

      if (tryKw('between')) {
        if (negated) throw { message: 'unexpected "not" before between', pos: cur().pos };
        if (!at('number')) throw { message: 'expected lower numeric bound after "between"', pos: cur().pos };
        const lower = eat('number').value;
        eatKw('to');
        if (!at('number')) throw { message: 'expected upper numeric bound after "to"', pos: cur().pos };
        const upper = eat('number').value;
        return { type: 'between', path: path, lower: lower, upper: upper };
      }

      if (at('op')) {
        if (negated) throw { message: 'unexpected "not" before comparison', pos: cur().pos };
        return { type: 'cmp', path: path, op: parseCmpOp(), value: parseValue() };
      }

      if (tryKw('is')) {
        if (negated) throw { message: 'use "is not null" form', pos: cur().pos };
        if (tryKw('not')) {
          eatKw('null');
          return { type: 'is', path: path, which: 'null', negated: true };
        }
        if (tryKw('null')) return { type: 'is', path: path, which: 'null', negated: false };
        if (tryKw('true')) return { type: 'is', path: path, which: 'true', negated: false };
        if (tryKw('false')) return { type: 'is', path: path, which: 'false', negated: false };
        throw { message: 'expected null, true, or false after "is"', pos: cur().pos };
      }

      if (tryKw('len')) {
        if (negated) throw { message: 'unexpected "not" before len', pos: cur().pos };
        const listLen = path.segs.length > 0 && path.segs[path.segs.length - 1].kind === 'listMark';
        const pathForLen = listLen ? { segs: path.segs.slice(0, -1) } : path;
        const op = parseCmpOp();
        if (!at('number')) throw { message: 'expected number after len', pos: cur().pos };
        return {
          type: 'len',
          path: pathForLen,
          kind: listLen ? 'list' : 'string',
          op: op,
          n: eat('number').value,
        };
      }

      if (tryKw('in')) {
        return { type: 'in', path: path, values: parseArrayLiteral(), negated: negated };
      }

      if (tryKw('has')) {
        const all = !!tryKw('all');
        const values = at('lbracket') ? parseArrayLiteral() : [parseValue()];
        return { type: 'has', path: path, values: values, all: all, negated: negated };
      }

      if (tryKw('contains')) {
        if (!at('string')) throw { message: 'expected string after contains', pos: cur().pos };
        return { type: 'contains', path: path, value: eat('string').value, negated: negated };
      }

      if (tryKw('starts')) {
        eatKw('with');
        if (!at('string')) throw { message: 'expected string after starts with', pos: cur().pos };
        return { type: 'startsWith', path: path, value: eat('string').value, negated: negated };
      }

      if (tryKw('ends')) {
        eatKw('with');
        if (!at('string')) throw { message: 'expected string after ends with', pos: cur().pos };
        return { type: 'endsWith', path: path, value: eat('string').value, negated: negated };
      }

      if (tryKw('type')) {
        if (!isIdent()) throw { message: 'expected type name', pos: cur().pos };
        const t = eat('ident');
        const allowed = {
          object: 'object',
          number: 'number',
          bool: 'bool',
          boolean: 'bool',
          list: 'list',
          array: 'list',
          string: 'string',
          null: 'null',
        };
        if (!allowed[t.lower]) throw { message: 'unknown type "' + t.value + '"', pos: t.pos };
        return { type: 'typecheck', path: path, typename: allowed[t.lower], negated: negated };
      }

      throw { message: 'expected predicate after path', pos: cur().pos };
    }

    try {
      const ast = parseExpr();
      if (!at('eof')) return fail('unexpected token after expression', cur().pos);
      return { ok: true, ast: ast };
    } catch (e) {
      if (e && e.message) return fail(e.message, e.pos);
      return fail(String(e));
    }
  }

  /* ---------- eval ---------- */

  function sameValue(a, b) {
    if (a === b) return true;
    if (typeof a === 'number' && typeof b === 'number' && Number.isNaN(a) && Number.isNaN(b)) {
      return true;
    }
    return false;
  }

  function isPlainObject(v) {
    return v !== null && typeof v === 'object' && !Array.isArray(v);
  }

  function resolvePath(root, path) {
    let current = [root];
    const segs = (path && path.segs) || [];
    for (let si = 0; si < segs.length; si++) {
      const seg = segs[si];
      const next = [];
      if (seg.kind === 'listMark') {
        for (let ci = 0; ci < current.length; ci++) {
          if (Array.isArray(current[ci])) next.push(current[ci]);
        }
        current = next;
        if (!current.length) return { missing: true };
        continue;
      }
      for (let ci = 0; ci < current.length; ci++) {
        const v = current[ci];
        if (seg.kind === 'key') {
          if (!isPlainObject(v) || !Object.prototype.hasOwnProperty.call(v, seg.name)) continue;
          next.push(v[seg.name]);
        } else if (seg.kind === 'index') {
          if (!Array.isArray(v) || seg.i < 0 || seg.i >= v.length) continue;
          next.push(v[seg.i]);
        } else if (seg.kind === 'star') {
          if (!Array.isArray(v)) continue;
          for (let j = 0; j < v.length; j++) next.push(v[j]);
        }
      }
      current = next;
      if (!current.length) return { missing: true };
    }
    return { values: current };
  }

  function cmpValues(op, left, right) {
    if (op === '=' || op === '!=') {
      const eq = sameValue(left, right);
      return op === '=' ? eq : !eq;
    }
    if (typeof left !== 'number' || typeof right !== 'number') return false;
    if (op === '<') return left < right;
    if (op === '>') return left > right;
    if (op === '<=') return left <= right;
    if (op === '>=') return left >= right;
    return false;
  }

  function applyLenOp(op, len, n) {
    if (op === '=') return len === n;
    if (op === '!=') return len !== n;
    if (op === '<') return len < n;
    if (op === '>') return len > n;
    if (op === '<=') return len <= n;
    if (op === '>=') return len >= n;
    return false;
  }

  function jsonTypeName(v) {
    if (v === null) return 'null';
    if (typeof v === 'boolean') return 'bool';
    if (typeof v === 'number') return 'number';
    if (typeof v === 'string') return 'string';
    if (Array.isArray(v)) return 'list';
    if (isPlainObject(v)) return 'object';
    return null;
  }

  function evalAny(values, pred) {
    for (let i = 0; i < values.length; i++) {
      if (pred(values[i])) return true;
    }
    return false;
  }

  function listContains(list, value) {
    for (let j = 0; j < list.length; j++) {
      if (sameValue(list[j], value)) return true;
    }
    return false;
  }

  function evaluate(ast, rootValue) {
    if (!ast) return false;
    switch (ast.type) {
      case 'or':
        return evaluate(ast.left, rootValue) || evaluate(ast.right, rootValue);
      case 'and':
        return evaluate(ast.left, rootValue) && evaluate(ast.right, rootValue);
      case 'not':
        return !evaluate(ast.expr, rootValue);
      case 'exists': {
        const r = resolvePath(rootValue, ast.path);
        return !r.missing;
      }
      case 'cmp': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        return evalAny(r.values, function (v) {
          return cmpValues(ast.op, v, ast.value);
        });
      }
      case 'between': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        return evalAny(r.values, function (v) {
          return typeof v === 'number' && v >= ast.lower && v <= ast.upper;
        });
      }
      case 'is': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        if (ast.which === 'null') {
          if (ast.negated) {
            return evalAny(r.values, function (v) {
              return v !== null;
            });
          }
          return evalAny(r.values, function (v) {
            return v === null;
          });
        }
        if (ast.which === 'true') {
          return evalAny(r.values, function (v) {
            return v === true;
          });
        }
        return evalAny(r.values, function (v) {
          return v === false;
        });
      }
      case 'len': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        return evalAny(r.values, function (v) {
          if (ast.kind === 'string') {
            if (typeof v !== 'string') return false;
            return applyLenOp(ast.op, v.length, ast.n);
          }
          if (!Array.isArray(v)) return false;
          return applyLenOp(ast.op, v.length, ast.n);
        });
      }
      case 'in': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        const hit = evalAny(r.values, function (v) {
          return listContains(ast.values, v);
        });
        return ast.negated ? !hit : hit;
      }
      case 'has': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        const hit = evalAny(r.values, function (list) {
          if (!Array.isArray(list)) return false;
          if (ast.all) {
            for (let i = 0; i < ast.values.length; i++) {
              if (!listContains(list, ast.values[i])) return false;
            }
            return true;
          }
          for (let i = 0; i < ast.values.length; i++) {
            if (listContains(list, ast.values[i])) return true;
          }
          return false;
        });
        return ast.negated ? !hit : hit;
      }
      case 'contains':
      case 'startsWith':
      case 'endsWith': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        const needle = ast.value;
        const hit = evalAny(r.values, function (v) {
          if (typeof v !== 'string') return false;
          if (ast.type === 'contains') return v.indexOf(needle) >= 0;
          if (ast.type === 'startsWith') return v.indexOf(needle) === 0;
          return v.length >= needle.length && v.slice(v.length - needle.length) === needle;
        });
        return ast.negated ? !hit : hit;
      }
      case 'typecheck': {
        const r = resolvePath(rootValue, ast.path);
        if (r.missing) return false;
        const hit = evalAny(r.values, function (v) {
          return jsonTypeName(v) === ast.typename;
        });
        return ast.negated ? !hit : hit;
      }
      default:
        return false;
    }
  }

  function match(query, rootValue) {
    const parsed = parse(query);
    if (!parsed.ok) return parsed;
    return { ok: true, match: evaluate(parsed.ast, rootValue) };
  }

  function filterValues(query, values) {
    const parsed = parse(query);
    if (!parsed.ok) return parsed;
    const out = [];
    const list = values || [];
    for (let i = 0; i < list.length; i++) {
      if (evaluate(parsed.ast, list[i])) out.push(i);
    }
    return { ok: true, indices: out };
  }

  root.FleditorJsonlAdvFilter = {
    parse: parse,
    eval: evaluate,
    evaluate: evaluate,
    match: match,
    filterValues: filterValues,
    resolvePath: resolvePath,
  };
})(typeof globalThis !== 'undefined' ? globalThis : window);
