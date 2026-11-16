/* CSV table grid UI */
(function (root) {
  'use strict';

  const P = root.FleditorCsvParse;
  const APPEND = '+';

  function cellInputClass(dirty) {
    return 'fleditor-csv-grid-cell' + (dirty ? ' fleditor-csv-grid-cell--dirty' : '');
  }

  function normalizeSelection(anchor, focus) {
    if (!anchor || !focus) return null;
    return {
      r0: Math.min(anchor.r, focus.r),
      r1: Math.max(anchor.r, focus.r),
      c0: Math.min(anchor.c, focus.c),
      c1: Math.max(anchor.c, focus.c),
    };
  }

  function isMultiCellSelection(anchor, focus) {
    const b = normalizeSelection(anchor, focus);
    if (!b) return false;
    return b.r0 !== b.r1 || b.c0 !== b.c1;
  }

  function copySelectionToTsv(rows, anchor, focus) {
    const b = normalizeSelection(anchor, focus);
    if (!b || !rows.length) return '';
    const lines = [];
    for (let r = b.r0; r <= b.r1; r++) {
      const line = rows[r] || [];
      const fields = [];
      for (let c = b.c0; c <= b.c1; c++) {
        fields.push(line[c] != null ? String(line[c]) : '');
      }
      lines.push(fields.join('\t'));
    }
    return lines.join('\n');
  }

  function applySelectionHighlights(container, anchor, focus) {
    container.querySelectorAll('input.fleditor-csv-grid-cell').forEach((inp) => {
      inp.classList.remove('fleditor-csv-grid-cell--selected');
    });
    const b = normalizeSelection(anchor, focus);
    if (!b) return;
    for (let r = b.r0; r <= b.r1; r++) {
      for (let c = b.c0; c <= b.c1; c++) {
        const inp = container.querySelector(
          'input.fleditor-csv-grid-cell[data-row="' + r + '"][data-col="' + c + '"]'
        );
        if (inp) inp.classList.add('fleditor-csv-grid-cell--selected');
      }
    }
  }

  function focusCell(container, r, c, kind) {
    const sel = kind === 'append-col'
      ? 'input.fleditor-csv-grid-cell--append-col[data-row="' + r + '"]'
      : kind === 'append-row'
        ? 'input.fleditor-csv-grid-cell--append-row[data-col="' + c + '"]'
        : 'input.fleditor-csv-grid-cell[data-row="' + r + '"][data-col="' + c + '"]';
    const inp = container.querySelector(sel);
    if (inp) { inp.focus(); if (inp.select) inp.select(); }
  }

  function mountDataCell(td, value, r, c, savedValue, opts) {
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.className = cellInputClass(value !== savedValue);
    inp.value = value;
    inp.setAttribute('data-row', String(r));
    inp.setAttribute('data-col', String(c));
    inp.spellcheck = false;
    inp.autocomplete = 'off';
    inp.setAttribute('aria-label', 'Cell row ' + (r + 1) + ' column ' + (c + 1));
    inp.addEventListener('mousedown', function (e) {
      if (e.shiftKey && opts.onCellExtendSelect) {
        opts.onCellExtendSelect(r, c);
      } else if (opts.onCellSelect) {
        opts.onCellSelect(r, c);
      }
    });
    inp.addEventListener('focus', function () {
      if (opts.onCellFocus) opts.onCellFocus(r, c);
    });
    inp.addEventListener('input', function () {
      if (opts.onCellChange) opts.onCellChange(r, c, inp.value);
      inp.className = cellInputClass(inp.value !== savedValue);
    });
    inp.addEventListener('keydown', function (e) {
      const cols = opts.colCount();
      const rows = opts.rowCount();
      if (e.key === 'ArrowRight') {
        e.preventDefault();
        if (c + 1 < cols) focusCell(opts.container, r, c + 1);
        else if (c + 1 === cols) focusCell(opts.container, r, 0, 'append-col');
      } else if (e.key === 'ArrowLeft') {
        e.preventDefault();
        if (c > 0) focusCell(opts.container, r, c - 1);
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (r + 1 < rows) focusCell(opts.container, r + 1, c);
        else focusCell(opts.container, 0, c, 'append-row');
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (r > 0) focusCell(opts.container, r - 1, c);
      } else if (e.key === 'Enter') {
        e.preventDefault();
        if (r + 1 < rows) focusCell(opts.container, r + 1, c);
        else if (opts.onGrowRow) { opts.onGrowRow(); window.requestAnimationFrame(function () { focusCell(opts.container, r + 1, c); }); }
      } else if (e.key === 'Tab' && !e.shiftKey) {
        e.preventDefault();
        if (c + 1 < cols) focusCell(opts.container, r, c + 1);
        else if (c + 1 === cols) {
          if (opts.onGrowCol) opts.onGrowCol();
          window.requestAnimationFrame(function () { focusCell(opts.container, r, c + 1); });
        }
      } else if (e.key === 'Tab' && e.shiftKey) {
        e.preventDefault();
        if (c > 0) focusCell(opts.container, r, c - 1);
      } else if (e.key === 'Backspace' && inp.value === '' && inp.selectionStart === 0 && c > 0) {
        e.preventDefault();
        focusCell(opts.container, r, c - 1);
      } else if (e.key === 'Delete' && inp.selectionStart === 0 && inp.selectionEnd === inp.value.length) {
        e.preventDefault();
        inp.value = '';
        if (opts.onCellChange) opts.onCellChange(r, c, '');
        inp.className = cellInputClass(true);
      } else if (e.shiftKey && (e.key === 'ArrowRight' || e.key === 'ArrowLeft' || e.key === 'ArrowDown' || e.key === 'ArrowUp')) {
        let nr = r;
        let nc = c;
        if (e.key === 'ArrowRight' && c + 1 < cols) nc = c + 1;
        else if (e.key === 'ArrowLeft' && c > 0) nc = c - 1;
        else if (e.key === 'ArrowDown' && r + 1 < rows) nr = r + 1;
        else if (e.key === 'ArrowUp' && r > 0) nr = r - 1;
        if (nr !== r || nc !== c) {
          e.preventDefault();
          if (opts.onCellExtendSelect) opts.onCellExtendSelect(nr, nc);
          focusCell(opts.container, nr, nc);
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === 'c' && opts.onCopy) {
        const multi = opts.isMultiCellSelection && opts.isMultiCellSelection();
        const allSelected =
          inp.selectionStart === 0 && inp.selectionEnd === inp.value.length;
        if (multi || allSelected) {
          e.preventDefault();
          opts.onCopy();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === 'v' && opts.onPaste) {
        e.preventDefault();
        opts.onPaste(r, c);
      }
    });
    td.appendChild(inp);
    return inp;
  }

  function mountAppendColCell(td, r, opts) {
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.className = 'fleditor-csv-grid-cell fleditor-csv-grid-cell--append-col';
    inp.value = APPEND;
    inp.setAttribute('data-row', String(r));
    inp.readOnly = true;
    inp.setAttribute('tabindex', '-1');
    inp.addEventListener('focus', function () {
      if (opts.onGrowCol) opts.onGrowCol();
      window.requestAnimationFrame(function () {
        const c = opts.colCount() - 1;
        focusCell(opts.container, r, c);
      });
    });
    td.appendChild(inp);
  }

  function mountAppendRowCell(td, c, opts) {
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.className = 'fleditor-csv-grid-cell fleditor-csv-grid-cell--append-row';
    inp.value = APPEND;
    inp.setAttribute('data-col', String(c));
    inp.readOnly = true;
    inp.setAttribute('tabindex', '-1');
    inp.addEventListener('focus', function () {
      if (opts.onGrowRow) opts.onGrowRow();
      window.requestAnimationFrame(function () {
        const r = opts.rowCount() - 1;
        focusCell(opts.container, r, c);
      });
    });
    td.appendChild(inp);
  }

  function renderGrid(container, rows, savedRows, opts) {
    const options = opts || {};
    const headerStyle = !!options.headerRow;
    container.innerHTML = '';
    const table = document.createElement('table');
    table.className = 'fleditor-csv-grid-table';
    const thead = document.createElement('thead');
    const hr = document.createElement('tr');
    const corner = document.createElement('th');
    corner.className = 'fleditor-csv-grid-corner';
    hr.appendChild(corner);
    const cols = rows[0] ? rows[0].length : 1;
    for (let c = 0; c < cols; c++) {
      const th = document.createElement('th');
      th.className = 'fleditor-csv-grid-col-h';
      th.textContent = String(c + 1);
      hr.appendChild(th);
    }
    const thPlus = document.createElement('th');
    thPlus.className = 'fleditor-csv-grid-col-h fleditor-csv-grid-plus-h';
    thPlus.textContent = APPEND;
    hr.appendChild(thPlus);
    thead.appendChild(hr);
    table.appendChild(thead);
    const tbody = document.createElement('tbody');
    const gridOpts = {
      container,
      colCount: function () { return rows[0] ? rows[0].length : 1; },
      rowCount: function () { return rows.length; },
      onCellChange: options.onCellChange,
      onGrowRow: options.onGrowRow,
      onGrowCol: options.onGrowCol,
    };
    for (let r = 0; r < rows.length; r++) {
      const tr = document.createElement('tr');
      if (headerStyle && r === 0) tr.className = 'fleditor-csv-grid-row--header';
      const rowLabel = document.createElement('th');
      rowLabel.className = 'fleditor-csv-grid-row-h';
      rowLabel.textContent = String(r + 1);
      tr.appendChild(rowLabel);
      const line = rows[r];
      for (let c = 0; c < cols; c++) {
        const td = document.createElement('td');
        const val = line[c] != null ? String(line[c]) : '';
        const saved = savedRows[r] && savedRows[r][c] != null ? String(savedRows[r][c]) : '';
        mountDataCell(td, val, r, c, saved, gridOpts);
        tr.appendChild(td);
      }
      const tdA = document.createElement('td');
      mountAppendColCell(tdA, r, gridOpts);
      tr.appendChild(tdA);
      tbody.appendChild(tr);
    }
    const trPlus = document.createElement('tr');
    trPlus.className = 'fleditor-csv-grid-row--append';
    const thR = document.createElement('th');
    thR.className = 'fleditor-csv-grid-row-h';
    thR.textContent = APPEND;
    trPlus.appendChild(thR);
    for (let c = 0; c < cols; c++) {
      const td = document.createElement('td');
      mountAppendRowCell(td, c, gridOpts);
      trPlus.appendChild(td);
    }
    const tdCorner = document.createElement('td');
    tdCorner.className = 'fleditor-csv-grid-pad';
    trPlus.appendChild(tdCorner);
    tbody.appendChild(trPlus);
    table.appendChild(tbody);
    container.appendChild(table);
    if (options.getSelection) {
      const sel = options.getSelection();
      applySelectionHighlights(container, sel && sel.anchor, sel && sel.focus);
    }
  }

  function cellLinearIndex(r, c, colCount) {
    return r * colCount + c;
  }

  function findCellMatches(rows, query) {
    const q = String(query || '').trim();
    if (!q) return [];
    const low = q.toLowerCase();
    const out = [];
    for (let r = 0; r < rows.length; r++) {
      const line = rows[r] || [];
      for (let c = 0; c < line.length; c++) {
        if (String(line[c]).toLowerCase().indexOf(low) >= 0) out.push({ r: r, c: c });
      }
    }
    return out;
  }

  function parseCellPos(text, rows) {
    const s = String(text || '').trim();
    const m = s.match(/^(\d+)\s*[,:\s]\s*(\d+)$/);
    if (!m) return { ok: false, error: 'Use row,column (1-based), e.g. 2,3' };
    const r = parseInt(m[1], 10) - 1;
    const c = parseInt(m[2], 10) - 1;
    if (!rows.length) return { ok: false, error: 'Empty table' };
    const cols = rows[0] ? rows[0].length : 0;
    if (r < 0 || c < 0 || r >= rows.length || c >= cols) {
      return { ok: false, error: 'Cell out of range' };
    }
    return { ok: true, r: r, c: c };
  }

  function matchIndexForAnchor(matches, anchorR, anchorC, colCount) {
    const a = cellLinearIndex(anchorR, anchorC, colCount);
    for (let i = 0; i < matches.length; i++) {
      const m = matches[i];
      if (cellLinearIndex(m.r, m.c, colCount) === a) return i;
    }
    return -1;
  }

  function applySearchHighlights(container, matches, currentIndex) {
    container.querySelectorAll('input.fleditor-csv-grid-cell').forEach((inp) => {
      inp.classList.remove('fleditor-csv-grid-cell--search', 'fleditor-csv-grid-cell--search-current');
    });
    matches.forEach((m, i) => {
      const inp = container.querySelector(
        'input.fleditor-csv-grid-cell[data-row="' + m.r + '"][data-col="' + m.c + '"]'
      );
      if (!inp) return;
      inp.classList.add('fleditor-csv-grid-cell--search');
      if (i === currentIndex) inp.classList.add('fleditor-csv-grid-cell--search-current');
    });
  }

  root.FleditorCsvGrid = {
    APPEND_LABEL: APPEND,
    renderGrid,
    focusCell,
    normalizeSelection,
    isMultiCellSelection,
    copySelectionToTsv,
    applySelectionHighlights,
    cellLinearIndex,
    findCellMatches,
    parseCellPos,
    matchIndexForAnchor,
    applySearchHighlights,
    isCellDirty(val, saved) {
      return String(val) !== String(saved);
    },
  };
}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));