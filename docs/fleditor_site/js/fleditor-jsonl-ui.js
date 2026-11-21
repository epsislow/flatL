/* JSONL Records UI: line list + Lock tree + Free panel */
(function (root) {
  'use strict';

  function P() {
    return root.FleditorJsonlParse;
  }

  function pathKey(path) {
    return (path || []).join('\u0001');
  }

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text != null) node.textContent = text;
    return node;
  }

  function truncDisplay(str) {
    const Jp = P();
    return JSON.stringify(str);
  }

  function renderScalarToken(value) {
    if (value === null) return el('span', 'fleditor-jsonl-tok--null', 'null');
    const t = typeof value;
    if (t === 'boolean') return el('span', 'fleditor-jsonl-tok--bool', String(value));
    if (t === 'number') return el('span', 'fleditor-jsonl-tok--num', String(value));
    if (t === 'string') return el('span', 'fleditor-jsonl-tok--str', truncDisplay(value));
    return el('span', null, String(value));
  }

  /**
   * @param {HTMLElement} container
   * @param {*} value
   * @param {{ expand: Record<string, boolean>, onToggle: (path: string[]) => void }} state
   */
  function renderLockTree(container, value, state) {
    container.textContent = '';
    const expand = state.expand || {};
    const onToggle = state.onToggle || function () {};

    function isOpen(path) {
      return !!expand[pathKey(path)];
    }

    function row(indent, togglePath, bodyBuild, bare) {
      const r = el('div', 'fleditor-jsonl-tree-row');
      if (!bare) r.appendChild(el('span', 'fleditor-jsonl-tree-indent', indent));
      const tog = bare ? null : el('button', 'fleditor-jsonl-tree-tog');
      if (tog) tog.type = 'button';
      if (tog && togglePath) {
        const open = isOpen(togglePath);
        tog.textContent = open ? '[-]' : '[+]';
        tog.setAttribute('aria-expanded', open ? 'true' : 'false');
        tog.setAttribute('aria-label', open ? 'Collapse' : 'Expand');
        tog.addEventListener('click', function (ev) {
          ev.preventDefault();
          onToggle(togglePath.slice());
        });
      } else if (tog) {
        tog.className = 'fleditor-jsonl-tree-tog fleditor-jsonl-tree-tog--spacer';
        tog.tabIndex = -1;
        tog.setAttribute('aria-hidden', 'true');
        tog.textContent = '';
      }
      if (tog) r.appendChild(tog);
      const body = el('span', 'fleditor-jsonl-tree-body');
      bodyBuild(body);
      r.appendChild(body);
      container.appendChild(r);
    }

    function renderValue(val, path, indent) {
      const t = typeof val;
      if (val === null || t === 'boolean' || t === 'number' || t === 'string') {
        row(indent, null, function (body) {
          body.appendChild(renderScalarToken(val));
        }, path.length === 1);
        return;
      }
      if (Array.isArray(val)) {
        const open = isOpen(path);
        if (!open) {
          row(indent, path, function (body) {
            body.appendChild(document.createTextNode('[…]'));
          });
          return;
        }
        row(indent, path, function (body) {
          body.appendChild(document.createTextNode('['));
        });
        for (let i = 0; i < val.length; i++) {
          const child = val[i];
          const childPath = path.concat([String(i)]);
          const isCont = child !== null && typeof child === 'object';
          if (isCont && !isOpen(childPath)) {
            row(indent + '  ', childPath, function (body) {
              body.appendChild(document.createTextNode(Array.isArray(child) ? '[…],' : '{…},'));
            });
          } else if (isCont) {
            renderValue(child, childPath, indent + '  ');
            const last = container.lastElementChild;
            if (last) {
              const b = last.querySelector('.fleditor-jsonl-tree-body');
              if (b && !/,\s*$/.test(b.textContent)) b.appendChild(document.createTextNode(','));
            }
          } else {
            row(indent + '  ', null, function (body) {
              body.appendChild(renderScalarToken(child));
              body.appendChild(document.createTextNode(','));
            });
          }
        }
        row(indent, null, function (body) {
          body.appendChild(document.createTextNode(']'));
        });
        return;
      }
      if (t === 'object') {
        const open = isOpen(path);
        if (!open) {
          row(indent, path, function (body) {
            body.appendChild(document.createTextNode('{…}'));
          });
          return;
        }
        row(indent, path, function (body) {
          body.appendChild(document.createTextNode('{'));
        });
        const keys = Object.keys(val);
        for (let ki = 0; ki < keys.length; ki++) {
          const k = keys[ki];
          const child = val[k];
          const childPath = path.concat([k]);
          const isCont = child !== null && typeof child === 'object';
          if (isCont && !isOpen(childPath)) {
            row(indent + '  ', childPath, function (body) {
              body.appendChild(el('span', 'fleditor-jsonl-tok--key', JSON.stringify(k)));
              body.appendChild(document.createTextNode(': '));
              body.appendChild(document.createTextNode(Array.isArray(child) ? '[…],' : '{…},'));
            });
          } else if (isCont) {
            row(indent + '  ', childPath, function (body) {
              body.appendChild(el('span', 'fleditor-jsonl-tok--key', JSON.stringify(k)));
              body.appendChild(document.createTextNode(': '));
              body.appendChild(document.createTextNode(Array.isArray(child) ? '[' : '{'));
            });
            if (Array.isArray(child)) {
              for (let i = 0; i < child.length; i++) {
                const item = child[i];
                const ip = childPath.concat([String(i)]);
                if (item !== null && typeof item === 'object') {
                  if (!isOpen(ip)) {
                    row(indent + '    ', ip, function (body) {
                      body.appendChild(document.createTextNode(Array.isArray(item) ? '[…],' : '{…},'));
                    });
                  } else {
                    renderValue(item, ip, indent + '    ');
                    const last = container.lastElementChild;
                    if (last) {
                      const b = last.querySelector('.fleditor-jsonl-tree-body');
                      if (b && !/,\s*$/.test(b.textContent)) b.appendChild(document.createTextNode(','));
                    }
                  }
                } else {
                  row(indent + '    ', null, function (body) {
                    body.appendChild(renderScalarToken(item));
                    body.appendChild(document.createTextNode(','));
                  });
                }
              }
            } else {
              const ckeys = Object.keys(child);
              for (let ci = 0; ci < ckeys.length; ci++) {
                renderPair(ckeys[ci], child[ckeys[ci]], childPath.concat([ckeys[ci]]), indent + '    ');
              }
            }
            row(indent + '  ', null, function (body) {
              body.appendChild(document.createTextNode(Array.isArray(child) ? '],' : '},'));
            });
          } else {
            renderPair(k, child, childPath, indent + '  ');
          }
        }
        row(indent, null, function (body) {
          body.appendChild(document.createTextNode('}'));
        });
      }
    }

    function renderPair(k, child, childPath, indent) {
      row(indent, null, function (body) {
        body.appendChild(el('span', 'fleditor-jsonl-tok--key', JSON.stringify(k)));
        body.appendChild(document.createTextNode(': '));
        body.appendChild(renderScalarToken(child));
        body.appendChild(document.createTextNode(','));
      });
    }

    const rootPath = ['$'];
    const rootKey = pathKey(rootPath);
    if (!Object.prototype.hasOwnProperty.call(expand, rootKey)) expand[rootKey] = true;
    renderValue(value, rootPath, '');
  }

  /**
   * @param {HTMLElement} container
   * @param {{
   *   lines: string[],
   *   pageStart?: number,
   *   pageEnd?: number,
   *   lineIndices?: number[],
   *   selected: number|null,
   *   onSelect: (index: number) => void,
   *   onDelete: (index: number) => void,
   *   onAdd: () => void
   * }} opts
   */
  function renderLineList(container, opts) {
    container.textContent = '';
    const addRow = el('div', 'fleditor-jsonl-list-add');
    const addBtn = el('button', 'fleditor-jsonl-add-btn', '+');
    addBtn.type = 'button';
    addBtn.title = 'Add empty line';
    addBtn.setAttribute('aria-label', 'Add empty line');
    addBtn.addEventListener('click', function () {
      opts.onAdd();
    });
    addRow.appendChild(addBtn);
    if (opts.selected != null) {
      const duplicateBtn = el('button', 'fleditor-jsonl-tool-btn', '×2');
      duplicateBtn.type = 'button';
      duplicateBtn.title = 'Duplicate selected line';
      duplicateBtn.setAttribute('aria-label', 'Duplicate selected line');
      duplicateBtn.addEventListener('click', function () { opts.onDuplicate(opts.selected); });
      addRow.appendChild(duplicateBtn);
      if (opts.lines.length > 1) {
        const upBtn = el('button', 'fleditor-jsonl-tool-btn', '↑');
        upBtn.type = 'button';
        upBtn.title = 'Move line up';
        upBtn.setAttribute('aria-label', 'Move line up');
        upBtn.disabled = opts.selected <= 0;
        upBtn.addEventListener('click', function () { opts.onMove(opts.selected, -1); });
        addRow.appendChild(upBtn);

        const downBtn = el('button', 'fleditor-jsonl-tool-btn', '↓');
        downBtn.type = 'button';
        downBtn.title = 'Move line down';
        downBtn.setAttribute('aria-label', 'Move line down');
        downBtn.disabled = opts.selected >= opts.lines.length - 1;
        downBtn.addEventListener('click', function () { opts.onMove(opts.selected, 1); });
        addRow.appendChild(downBtn);
      }
    }
    container.appendChild(addRow);

    const listScroll = el('div', 'fleditor-jsonl-list-scroll');
    const list = el('ul', 'fleditor-jsonl-list');
    list.setAttribute('aria-label', 'JSONL lines');
    const Jp = P();
    let indices = opts.lineIndices;
    if (!indices) {
      indices = [];
      for (let i = opts.pageStart; i < opts.pageEnd; i++) indices.push(i);
    }
    for (let k = 0; k < indices.length; k++) {
      const i = indices[k];
      const li = el('li', 'fleditor-jsonl-list-item');
      if (opts.selected === i) li.classList.add('fleditor-jsonl-list-item--sel');
      li.setAttribute('role', 'button');
      li.tabIndex = 0;
      const ln = el('span', 'fleditor-jsonl-list-ln', String(i + 1));
      const prev = el('span', 'fleditor-jsonl-list-prev', Jp.previewLine(opts.lines[i]));
      const x = el('button', 'fleditor-jsonl-list-x', '\u00d7');
      x.type = 'button';
      x.title = 'Remove line';
      x.setAttribute('aria-label', 'Remove line ' + (i + 1));
      (function (index) {
        x.addEventListener('click', function (ev) {
          ev.stopPropagation();
          opts.onDelete(index);
        });
        li.addEventListener('click', function () {
          opts.onSelect(index);
        });
        li.addEventListener('keydown', function (ev) {
          if (ev.key === 'Enter' || ev.key === ' ') {
            ev.preventDefault();
            opts.onSelect(index);
          }
        });
      })(i);
      li.appendChild(ln);
      li.appendChild(prev);
      li.appendChild(x);
      list.appendChild(li);
    }
    listScroll.appendChild(list);
    container.appendChild(listScroll);
  }

  /**
   * Highlight error range in a textarea when possible.
   */
  function applyFreeErrorHighlight(textarea, errorPos) {
    if (!textarea || errorPos == null || errorPos < 0) return;
    const pos = Math.min(errorPos, textarea.value.length);
    try {
      textarea.focus();
      textarea.setSelectionRange(pos, Math.min(pos + 1, textarea.value.length));
    } catch (_e) {
      /* ignore */
    }
  }

  function highlightText(container, query) {
    if (!container || !query) return;
    const doc = container.ownerDocument || document;
    function visit(node) {
      if (node.nodeType === 3) {
        const value = node.nodeValue || '';
        const at = value.indexOf(query);
        if (at < 0) return;
        const frag = doc.createDocumentFragment();
        let from = 0;
        let found = at;
        while (found >= 0) {
          if (found > from) frag.appendChild(doc.createTextNode(value.slice(from, found)));
          const mark = el('mark', 'fleditor-jsonl-search-highlight', value.slice(found, found + query.length));
          frag.appendChild(mark);
          from = found + query.length;
          found = value.indexOf(query, from);
        }
        if (from < value.length) frag.appendChild(doc.createTextNode(value.slice(from)));
        node.parentNode.replaceChild(frag, node);
        return;
      }
      if (node.nodeType !== 1 || node.tagName === 'MARK') return;
      const children = Array.prototype.slice.call(node.childNodes || []);
      children.forEach(visit);
    }
    visit(container);
  }

  root.FleditorJsonlUi = {
    renderLockTree: renderLockTree,
    highlightText: highlightText,
    renderLineList: renderLineList,
    applyFreeErrorHighlight: applyFreeErrorHighlight,
    pathKey: pathKey,
  };
})(typeof globalThis !== 'undefined' ? globalThis : window);
