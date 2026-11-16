/* CSV table grid UI */
(function (root) {
  'use strict';

  const P = root.FleditorCsvParse;
  const APPEND = '+';

  function cellInputClass(dirty) {
    return 'fleditor-csv-grid-cell' + (dirty ? ' fleditor-csv-grid-cell--dirty' : '');
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
  }

  root.FleditorCsvGrid = {
    APPEND_LABEL: APPEND,
    renderGrid,
    focusCell,
    isCellDirty(val, saved) {
      return String(val) !== String(saved);
    },
  };
}(typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : this));