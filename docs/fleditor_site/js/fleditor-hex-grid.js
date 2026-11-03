/* Hex byte grid (Variant A) — browser UI + testable cell helpers */
(function (global) {
  'use strict';

  const H = global.FleditorHex;
  const APPEND_LABEL = '+';

  function formatByteHex(b) {
    return b.toString(16).padStart(2, '0');
  }

  function asciiForByte(b) {
    return b >= 32 && b < 127 ? String.fromCharCode(b) : '.';
  }

  /**
   * Parse one cell value to byte. Empty → 0x00.
   * @returns {{ ok: true, byte: number } | { ok: false }}
   */
  function parseHexCellInput(str) {
    const s = (str || '').trim().toLowerCase();
    if (!s) {
      return { ok: true, byte: 0 };
    }
    if (!/^[0-9a-f]{1,2}$/.test(s)) {
      return { ok: false };
    }
    const padded = s.length === 1 ? '0' + s : s;
    return { ok: true, byte: parseInt(padded, 16) };
  }

  /** Page index that shows the EOF append slot. */
  function appendSlotPageIndex(byteLength) {
    if (!H) {
      return 0;
    }
    if (byteLength <= 0) {
      return 0;
    }
    return Math.floor(byteLength / H.BYTES_PER_PAGE);
  }

  function pageShowsAppendSlot(byteLength, pageIndex) {
    return pageIndex === appendSlotPageIndex(byteLength);
  }

  function gridRowCountForPage(byteLength, pageIndex) {
    const bytesOnPage = H.bytesOnPage(byteLength, pageIndex);
    let rows = rowCountForPageSlice(bytesOnPage);
    if (pageShowsAppendSlot(byteLength, pageIndex)) {
      const pageBase = H.pageByteOffset(pageIndex);
      const rel = byteLength - pageBase;
      const appendRow = Math.floor(rel / H.BYTES_PER_LINE);
      rows = Math.max(rows, appendRow + 1);
    }
    return rows;
  }

  function isCellDirty(absOffset, buffer, savedBuffer) {
    if (!savedBuffer || !buffer) {
      return false;
    }
    if (absOffset >= buffer.length) {
      return false;
    }
    if (absOffset >= savedBuffer.length) {
      return true;
    }
    return buffer[absOffset] !== savedBuffer[absOffset];
  }

  /** Rows needed for a page slice (max 40). */
  function rowCountForPageSlice(bytesOnPage) {
    if (bytesOnPage <= 0) {
      return 0;
    }
    return Math.min(H.LINES_PER_PAGE, Math.ceil(bytesOnPage / H.BYTES_PER_LINE));
  }

  function buildAsciiForRow(buffer, rowStart, count) {
    let s = '';
    for (let i = 0; i < count; i++) {
      s += asciiForByte(buffer[rowStart + i]);
    }
    return s;
  }

  function focusCellByOffset(container, offset) {
    if (!container) {
      return false;
    }
    const inp = container.querySelector('input.fleditor-hex-grid-cell[data-offset="' + offset + '"]');
    if (!inp) {
      return false;
    }
    inp.focus();
    inp.select();
    return true;
  }

  function focusAppendCell(container) {
    if (!container) {
      return false;
    }
    const inp = container.querySelector('input.fleditor-hex-grid-cell--append');
    if (!inp) {
      return false;
    }
    inp.focus();
    if (inp.value === APPEND_LABEL) {
      inp.value = '';
    }
    inp.select();
    return true;
  }

  function refreshRowAscii(buffer, asciiTd) {
    if (!asciiTd || !buffer || !H) {
      return;
    }
    const rs = parseInt(asciiTd.getAttribute('data-row-start'), 10);
    const cnt = Math.min(H.BYTES_PER_LINE, Math.max(0, buffer.length - rs));
    asciiTd.textContent = buildAsciiForRow(buffer, rs, cnt);
  }

  function applyCellValue(inp, buffer, abs, opts, asciiTd) {
    const oldB = buffer[abs];
    const parsed = parseHexCellInput(inp.value);
    if (!parsed.ok) {
      inp.value = formatByteHex(oldB);
      return oldB;
    }
    inp.value = formatByteHex(parsed.byte);
    if (parsed.byte !== oldB && opts.onCellChange) {
      opts.onCellChange(abs, parsed.byte);
    }
    refreshRowAscii(buffer, asciiTd);
    if (isCellDirty(abs, buffer, opts.savedBuffer)) {
      inp.classList.add('fleditor-hex-grid-cell--dirty');
    } else {
      inp.classList.remove('fleditor-hex-grid-cell--dirty');
    }
    return parsed.byte;
  }

  function commitAppendInput(inp, buffer, opts) {
    const raw = (inp.value || '').trim();
    if (!raw || raw === APPEND_LABEL) {
      inp.value = APPEND_LABEL;
      return false;
    }
    const parsed = parseHexCellInput(raw);
    if (!parsed.ok) {
      inp.value = APPEND_LABEL;
      return false;
    }
    if (opts.onAppendByte) {
      opts.onAppendByte(parsed.byte);
    }
    inp.value = APPEND_LABEL;
    return true;
  }

  function requestFocusAt(container, offset, buffer, opts) {
    if (offset < 0) {
      return;
    }
    if (offset < buffer.length) {
      if (focusCellByOffset(container, offset)) {
        if (opts.onCellFocus) {
          opts.onCellFocus(offset);
        }
        return;
      }
      if (opts.onRequestFocusOffset) {
        opts.onRequestFocusOffset(offset);
      }
      return;
    }
    if (offset === buffer.length) {
      if (focusAppendCell(container)) {
        if (opts.onCellFocus) {
          opts.onCellFocus(buffer.length);
        }
        return;
      }
      if (opts.onRequestFocusAppend) {
        opts.onRequestFocusAppend();
      }
    }
  }

  /** @returns {number | null} */
  function arrowTargetOffset(abs, key, bufferLength) {
    if (!H || bufferLength < 0) {
      return null;
    }
    if (key === 'ArrowLeft') {
      return abs > 0 ? abs - 1 : null;
    }
    if (key === 'ArrowRight') {
      return abs + 1 <= bufferLength ? abs + 1 : null;
    }
    if (key === 'ArrowUp') {
      const col = abs % H.BYTES_PER_LINE;
      const row = Math.floor(abs / H.BYTES_PER_LINE);
      if (row <= 0) {
        return null;
      }
      return (row - 1) * H.BYTES_PER_LINE + col;
    }
    if (key === 'ArrowDown') {
      const col = abs % H.BYTES_PER_LINE;
      const row = Math.floor(abs / H.BYTES_PER_LINE);
      const nextRowStart = (row + 1) * H.BYTES_PER_LINE;
      if (nextRowStart >= bufferLength) {
        return null;
      }
      return Math.min(nextRowStart + col, bufferLength - 1);
    }
    return null;
  }

  /** @returns {number | null} */
  function arrowTargetFromAppend(key, bufferLength) {
    if (!H || bufferLength < 0) {
      return null;
    }
    const appendCol = bufferLength % H.BYTES_PER_LINE;
    const appendRow = Math.floor(bufferLength / H.BYTES_PER_LINE);
    if (key === 'ArrowLeft') {
      return bufferLength > 0 ? bufferLength - 1 : null;
    }
    if (key === 'ArrowRight') {
      return null;
    }
    if (key === 'ArrowUp') {
      if (appendRow <= 0) {
        return bufferLength > 0 ? Math.min(appendCol, bufferLength - 1) : null;
      }
      const up = (appendRow - 1) * H.BYTES_PER_LINE + appendCol;
      return up < bufferLength ? up : bufferLength - 1;
    }
    if (key === 'ArrowDown') {
      return null;
    }
    return null;
  }

  function advanceToNextCell(container, abs, buffer, opts) {
    const next = abs + 1;
    if (next < buffer.length) {
      if (focusCellByOffset(container, next)) {
        if (opts.onCellFocus) {
          opts.onCellFocus(next);
        }
        return;
      }
      if (opts.onRequestFocusOffset) {
        opts.onRequestFocusOffset(next);
      }
      return;
    }
    if (next === buffer.length) {
      if (focusAppendCell(container)) {
        if (opts.onCellFocus) {
          opts.onCellFocus(buffer.length);
        }
        return;
      }
      if (opts.onRequestFocusAppend) {
        opts.onRequestFocusAppend();
      }
    }
  }

  function mountStandardCell(td, abs, buffer, opts, container, asciiTd) {
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.className = 'fleditor-hex-grid-cell';
    inp.maxLength = 2;
    inp.setAttribute('inputmode', 'latin');
    inp.autocomplete = 'off';
    inp.spellcheck = false;
    inp.dataset.offset = String(abs);
    inp.value = formatByteHex(buffer[abs]);
    if (opts.highlightOffset === abs) {
      inp.classList.add('fleditor-hex-grid-cell--hi');
    }
    if (isCellDirty(abs, buffer, opts.savedBuffer)) {
      inp.classList.add('fleditor-hex-grid-cell--dirty');
    }
    inp.addEventListener('focus', function () {
      inp.select();
      if (opts.onCellFocus) {
        opts.onCellFocus(abs);
      }
    });
    inp.addEventListener('input', function () {
      inp.value = inp.value.replace(/[^0-9a-fA-F]/g, '').toLowerCase();
      if (inp.value.length >= 2) {
        applyCellValue(inp, buffer, abs, opts, asciiTd);
        advanceToNextCell(container, abs, buffer, opts);
      }
    });
    inp.addEventListener('blur', function () {
      applyCellValue(inp, buffer, abs, opts, asciiTd);
    });
    inp.addEventListener('keydown', function (e) {
      if (
        e.key === 'ArrowLeft' ||
        e.key === 'ArrowRight' ||
        e.key === 'ArrowUp' ||
        e.key === 'ArrowDown'
      ) {
        const target = arrowTargetOffset(abs, e.key, buffer.length);
        if (target != null) {
          e.preventDefault();
          applyCellValue(inp, buffer, abs, opts, asciiTd);
          requestFocusAt(container, target, buffer, opts);
        }
        return;
      }
      if (e.key === 'Delete') {
        e.preventDefault();
        if (opts.onDeleteByteAt) {
          opts.onDeleteByteAt(abs);
        }
        return;
      }
      if (e.key === 'Backspace') {
        e.preventDefault();
        if (opts.onBackspaceByteAt) {
          opts.onBackspaceByteAt(abs);
        }
        return;
      }
      if (e.key === 'Tab' && !e.shiftKey) {
        e.preventDefault();
        applyCellValue(inp, buffer, abs, opts, asciiTd);
        advanceToNextCell(container, abs, buffer, opts);
      } else if (e.key === 'Tab' && e.shiftKey) {
        e.preventDefault();
        applyCellValue(inp, buffer, abs, opts, asciiTd);
        const prev = abs - 1;
        if (prev >= 0 && opts.onRequestFocusOffset) {
          opts.onRequestFocusOffset(prev);
        }
      }
    });
    td.appendChild(inp);
  }

  function mountAppendCell(td, buffer, opts, container) {
    const inp = document.createElement('input');
    inp.type = 'text';
    inp.className = 'fleditor-hex-grid-cell fleditor-hex-grid-cell--append';
    inp.maxLength = 2;
    inp.setAttribute('inputmode', 'latin');
    inp.autocomplete = 'off';
    inp.spellcheck = false;
    inp.setAttribute('aria-label', 'Append byte at end of file');
    inp.value = APPEND_LABEL;
    inp.addEventListener('focus', function () {
      if (inp.value === APPEND_LABEL) {
        inp.value = '';
      }
      inp.select();
      if (opts.onCellFocus) {
        opts.onCellFocus(buffer.length);
      }
    });
    inp.addEventListener('input', function () {
      inp.value = inp.value.replace(/[^0-9a-fA-F]/g, '').toLowerCase();
      if (inp.value.length >= 2) {
        commitAppendInput(inp, buffer, opts);
      }
    });
    inp.addEventListener('blur', function () {
      const raw = (inp.value || '').trim();
      if (!raw) {
        inp.value = APPEND_LABEL;
        return;
      }
      if (raw.length <= 2) {
        commitAppendInput(inp, buffer, opts);
      } else {
        inp.value = APPEND_LABEL;
      }
    });
    inp.addEventListener('keydown', function (e) {
      if (
        e.key === 'ArrowLeft' ||
        e.key === 'ArrowRight' ||
        e.key === 'ArrowUp' ||
        e.key === 'ArrowDown'
      ) {
        const target = arrowTargetFromAppend(e.key, buffer.length);
        if (target != null) {
          e.preventDefault();
          commitAppendInput(inp, buffer, opts);
          requestFocusAt(container, target, buffer, opts);
        }
        return;
      }
      if (e.key === 'Backspace') {
        e.preventDefault();
        if (buffer.length > 0 && opts.onBackspaceByteAt) {
          opts.onBackspaceByteAt(buffer.length);
        }
        return;
      }
      if (e.key === 'Delete') {
        e.preventDefault();
        return;
      }
      if (e.key === 'Enter') {
        e.preventDefault();
        commitAppendInput(inp, buffer, opts);
      }
    });
    td.appendChild(inp);
  }

  /**
   * @param {HTMLElement} container
   * @param {Uint8Array} buffer
   * @param {number} pageIndex
   * @param {{
   *   highlightOffset?: number,
   *   savedBuffer?: Uint8Array,
   *   onCellChange?: (offset: number, byte: number) => void,
   *   onAppendByte?: (byte: number) => void,
   *   onCellFocus?: (offset: number) => void,
   *   onRequestFocusOffset?: (offset: number) => void,
   *   onRequestFocusAppend?: () => void,
   * }} opts
   */
  function renderHexGrid(container, buffer, pageIndex, opts) {
    if (!container || !buffer || !H) {
      return;
    }
    opts = opts || {};
    const pageBase = H.pageByteOffset(pageIndex);
    const bytesOnPage = H.bytesOnPage(buffer.length, pageIndex);
    const showAppend = pageShowsAppendSlot(buffer.length, pageIndex);
    const rows = gridRowCountForPage(buffer.length, pageIndex);

    container.innerHTML = '';
    container.classList.add('fleditor-hex-grid-wrap');

    if (bytesOnPage === 0 && !showAppend) {
      const empty = document.createElement('p');
      empty.className = 'fleditor-hex-grid-empty';
      empty.textContent = 'No bytes on this page. Use page navigation or open the last page to append.';
      container.appendChild(empty);
      return;
    }

    const table = document.createElement('table');
    table.className = 'fleditor-hex-grid-table';
    table.setAttribute('role', 'grid');

    const colgroup = document.createElement('colgroup');
    const colOff = document.createElement('col');
    colOff.className = 'fleditor-hex-grid-col-offset';
    colgroup.appendChild(colOff);
    for (let ci = 0; ci < H.BYTES_PER_LINE; ci++) {
      const colByte = document.createElement('col');
      colByte.className = 'fleditor-hex-grid-col-byte';
      colgroup.appendChild(colByte);
    }
    const colAsc = document.createElement('col');
    colAsc.className = 'fleditor-hex-grid-col-ascii';
    colgroup.appendChild(colAsc);
    table.appendChild(colgroup);

    const thead = document.createElement('thead');
    const hr = document.createElement('tr');
    const corner = document.createElement('th');
    corner.scope = 'col';
    corner.textContent = 'Offset';
    hr.appendChild(corner);
    for (let c = 0; c < H.BYTES_PER_LINE; c++) {
      const th = document.createElement('th');
      th.scope = 'col';
      th.className = 'fleditor-hex-grid-col-h';
      th.textContent = c.toString(16).padStart(2, '0');
      hr.appendChild(th);
    }
    const ascH = document.createElement('th');
    ascH.scope = 'col';
    ascH.textContent = 'ASCII';
    hr.appendChild(ascH);
    thead.appendChild(hr);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    for (let row = 0; row < rows; row++) {
      const rowStart = pageBase + row * H.BYTES_PER_LINE;
      const tr = document.createElement('tr');

      const offTd = document.createElement('td');
      offTd.className = 'fleditor-hex-grid-offset';
      offTd.textContent = rowStart.toString(16).padStart(8, '0');
      tr.appendChild(offTd);

      const asciiTd = document.createElement('td');
      asciiTd.className = 'fleditor-hex-grid-ascii';
      asciiTd.setAttribute('data-row-start', String(rowStart));

      for (let col = 0; col < H.BYTES_PER_LINE; col++) {
        const td = document.createElement('td');
        td.className = 'fleditor-hex-grid-byte-td';
        const abs = rowStart + col;
        if (showAppend && abs === buffer.length) {
          mountAppendCell(td, buffer, opts, container);
          tr.appendChild(td);
          continue;
        }
        if (abs >= buffer.length) {
          td.classList.add('fleditor-hex-grid-pad');
          tr.appendChild(td);
          continue;
        }
        mountStandardCell(td, abs, buffer, opts, container, asciiTd);
        tr.appendChild(td);
      }

      const cellsInRow = Math.min(H.BYTES_PER_LINE, Math.max(0, buffer.length - rowStart));
      asciiTd.textContent = buildAsciiForRow(buffer, rowStart, cellsInRow);
      tr.appendChild(asciiTd);
      tbody.appendChild(tr);
    }
    table.appendChild(tbody);
    container.appendChild(table);

    const hi = container.querySelector('.fleditor-hex-grid-cell--hi');
    if (hi && typeof hi.scrollIntoView === 'function') {
      hi.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    }
  }

  global.FleditorHexGrid = {
    APPEND_LABEL,
    formatByteHex,
    parseHexCellInput,
    appendSlotPageIndex,
    pageShowsAppendSlot,
    gridRowCountForPage,
    isCellDirty,
    rowCountForPageSlice,
    buildAsciiForRow,
    focusCellByOffset,
    focusAppendCell,
    arrowTargetOffset,
    arrowTargetFromAppend,
    renderHexGrid,
  };
})(typeof window !== 'undefined' ? window : globalThis);
