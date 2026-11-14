/* global FleditorCodeMap */
(function (root) {
  'use strict';

  const NS = 'http://www.w3.org/2000/svg';
  const COLORS = {
    jump: '#60a5fa',
    call: '#4ade80',
    or: '#facc15',
    panic: '#f87171',
    for: '#c084fc',
    foreach: '#c084fc'
  };
  let displayMode = 'connected';

  function svgElement(name, attrs, text) {
    const el = document.createElementNS(NS, name);
    Object.keys(attrs || {}).forEach(function (key) { el.setAttribute(key, attrs[key]); });
    if (text != null) el.textContent = text;
    return el;
  }

  function htmlElement(name, className, text) {
    const el = document.createElement(name);
    if (className) el.className = className;
    if (text != null) el.textContent = text;
    return el;
  }

  function connectedColumns(nodeEntries, edges, entryPath) {
    const byId = new Map();
    const outgoing = new Map();
    nodeEntries.forEach(function (node) {
      byId.set(node.id, node);
      outgoing.set(node.id, []);
    });
    edges.forEach(function (edge) {
      if (outgoing.has(edge.from) && byId.has(edge.to)) outgoing.get(edge.from).push(edge.to);
    });

    const depth = new Map();
    const queue = [];
    nodeEntries.filter(function (node) { return node.file === entryPath && node.kind !== 'dynamic'; })
      .forEach(function (node) {
        if (!depth.has(node.id)) {
          depth.set(node.id, 0);
          queue.push(node.id);
        }
      });
    while (queue.length) {
      const id = queue.shift();
      const nextDepth = depth.get(id) + 1;
      (outgoing.get(id) || []).forEach(function (targetId) {
        if (!depth.has(targetId)) {
          depth.set(targetId, nextDepth);
          queue.push(targetId);
        }
      });
    }

    let maxDepth = 0;
    depth.forEach(function (value) { maxDepth = Math.max(maxDepth, value); });
    nodeEntries.forEach(function (node) {
      if (!depth.has(node.id)) {
        // Keep disconnected/import-only blocks visible after the connected portion.
        depth.set(node.id, maxDepth + 1);
      }
    });
    const columns = new Map();
    nodeEntries.forEach(function (node) {
      const col = depth.get(node.id);
      if (!columns.has(col)) columns.set(col, []);
      columns.get(col).push(node);
    });
    return Array.from(columns.entries()).sort(function (a, b) { return a[0] - b[0]; }).map(function (entry) {
      return entry[1].sort(function (a, b) {
        return a.file.localeCompare(b.file) || a.startLine - b.startLine;
      });
    });
  }

  function render(host, model, onNodeClick, onEdgeClick) {
    host.innerHTML = '';
    const toolbar = htmlElement('div', 'fl-code-map-toolbar');
    const summary = htmlElement('span', 'fl-code-map-summary',
      model.blocks.length + ' blocks' + ((model.dynamicNodes || []).length ? ' · ' + model.dynamicNodes.length + ' dynamic targets' : '') + ' · ' + model.edges.length + ' transfers · ' + model.files.length + ' files');
    toolbar.appendChild(summary);

    const controls = htmlElement('div', 'fl-code-map-controls');
    const modeLabel = htmlElement('span', 'fl-code-map-mode-label', 'Layout');
    controls.appendChild(modeLabel);
    [['connected', 'Connected'], ['file', 'By file']].forEach(function (option) {
      const button = htmlElement('button', 'fl-code-map-mode' + (displayMode === option[0] ? ' active' : ''), option[1]);
      button.type = 'button';
      button.setAttribute('aria-pressed', displayMode === option[0] ? 'true' : 'false');
      button.addEventListener('click', function () {
        if (displayMode === option[0]) return;
        displayMode = option[0];
        render(host, model, onNodeClick, onEdgeClick);
      });
      controls.appendChild(button);
    });
    toolbar.appendChild(controls);

    const legend = htmlElement('div', 'fl-code-map-legend');
    ['jump', 'call', 'or', 'panic', 'for', 'foreach'].forEach(function (type) {
      const item = htmlElement('span', 'fl-code-map-legend-item');
      const swatch = htmlElement('i', 'fl-code-map-swatch');
      swatch.style.backgroundColor = COLORS[type];
      item.appendChild(swatch);
      item.appendChild(document.createTextNode(type));
      legend.appendChild(item);
    });
    const hintLegend = htmlElement('span', 'fl-code-map-legend-item');
    hintLegend.appendChild(htmlElement('i', 'fl-code-map-swatch fl-code-map-swatch-hint'));
    hintLegend.appendChild(document.createTextNode('@map hint'));
    legend.appendChild(hintLegend);
    const dynamicLegend = htmlElement('span', 'fl-code-map-legend-item');
    dynamicLegend.appendChild(htmlElement('i', 'fl-code-map-swatch fl-code-map-swatch-dynamic'));
    dynamicLegend.appendChild(document.createTextNode('dynamic target'));
    legend.appendChild(dynamicLegend);
    toolbar.appendChild(legend);
    host.appendChild(toolbar);

    const viewport = htmlElement('div', 'fl-code-map-viewport');
    host.appendChild(viewport);
    const nodeEntries = model.blocks.concat(model.dynamicNodes || []);
    if (!nodeEntries.length) {
      viewport.appendChild(htmlElement('div', 'fl-code-map-empty', 'No FlatL blocks found in this file or its imported files.'));
      return;
    }

    let columns;
    if (displayMode === 'file') {
      const byFile = new Map();
      model.files.forEach(function (file) { byFile.set(file.path, []); });
      nodeEntries.forEach(function (node) {
        if (!byFile.has(node.file)) byFile.set(node.file, []);
        byFile.get(node.file).push(node);
      });
      columns = Array.from(byFile.values()).filter(function (items) { return items.length > 0; });
      columns.forEach(function (items) { items.sort(function (a, b) { return a.startLine - b.startLine; }); });
    } else {
      columns = connectedColumns(nodeEntries, model.edges, model.entryPath || (model.files[0] && model.files[0].path));
    }

    const colWidth = 300;
    const colGap = 56;
    const nodeWidth = 252;
    const nodeHeight = 52;
    const rowGap = 28;
    const top = displayMode === 'file' ? 78 : 34;
    const left = 24;
    const positions = new Map();
    let maxRows = 1;
    columns.forEach(function (items, col) {
      maxRows = Math.max(maxRows, items.length);
      items.forEach(function (node, row) {
        positions.set(node.id, { x: left + col * (colWidth + colGap) + 22, y: top + row * (nodeHeight + rowGap), col: col, row: row });
      });
    });
    const width = Math.max(640, left * 2 + columns.length * colWidth + Math.max(0, columns.length - 1) * colGap);
    const height = Math.max(240, top + maxRows * (nodeHeight + rowGap) + 36);
    const svg = svgElement('svg', { class: 'fl-code-map-svg', width: width, height: height, viewBox: '0 0 ' + width + ' ' + height, role: 'img', 'aria-label': 'FlatL block transfer map' });
    const defs = svgElement('defs');
    Object.keys(COLORS).forEach(function (type) {
      const marker = svgElement('marker', { id: 'fl-map-arrow-' + type, markerWidth: 8, markerHeight: 8, refX: 6, refY: 3, orient: 'auto', markerUnits: 'strokeWidth' });
      marker.appendChild(svgElement('path', { d: 'M0,0 L0,6 L6,3 z', fill: COLORS[type] }));
      defs.appendChild(marker);
    });
    svg.appendChild(defs);

    if (displayMode === 'file') {
      columns.forEach(function (items, col) {
        const x = left + col * (colWidth + colGap);
        const file = items[0].file;
        const blockCount = items.filter(function (node) { return node.kind !== 'dynamic'; }).length;
        const dynamicCount = items.length - blockCount;
        const groupHeight = Math.max(150, 100 + items.length * (nodeHeight + rowGap));
        svg.appendChild(svgElement('rect', { x: x, y: 12, width: colWidth, height: groupHeight, rx: 10, class: 'fl-map-file-bg' }));
        svg.appendChild(svgElement('text', { x: x + 14, y: 36, class: 'fl-map-file-title' }, file));
        svg.appendChild(svgElement('text', { x: x + 14, y: 54, class: 'fl-map-file-meta' }, blockCount + ' blocks' + (dynamicCount ? ' · ' + dynamicCount + ' dynamic' : '')));
      });
    }

    model.edges.forEach(function (edge) {
      const from = positions.get(edge.from);
      const to = positions.get(edge.to);
      if (!from || !to) return;
      const y1 = from.y + nodeHeight / 2;
      const y2 = to.y + nodeHeight / 2;
      let x1;
      let x2;
      let d;
      if (from.col === to.col) {
        x1 = from.x + nodeWidth;
        x2 = to.x + nodeWidth;
        const laneX = left + from.col * (colWidth + colGap) + colWidth + 18;
        d = 'M ' + x1 + ' ' + y1 + ' C ' + laneX + ' ' + y1 + ', ' + laneX + ' ' + y2 + ', ' + x2 + ' ' + y2;
      } else if (to.col > from.col) {
        x1 = from.x + nodeWidth;
        x2 = to.x;
        const bend = Math.max(42, Math.abs(x2 - x1) * 0.42);
        d = 'M ' + x1 + ' ' + y1 + ' C ' + (x1 + bend) + ' ' + y1 + ', ' + (x2 - bend) + ' ' + y2 + ', ' + x2 + ' ' + y2;
      } else {
        x1 = from.x;
        x2 = to.x + nodeWidth;
        const bend = Math.max(42, Math.abs(x2 - x1) * 0.42);
        d = 'M ' + x1 + ' ' + y1 + ' C ' + (x1 - bend) + ' ' + y1 + ', ' + (x2 + bend) + ' ' + y2 + ', ' + x2 + ' ' + y2;
      }
      const color = COLORS[edge.type] || '#a1a1aa';
      const path = svgElement('path', { d: d, class: 'fl-map-edge' + (edge.resolution === 'hint' ? ' fl-map-edge-hint' : edge.resolution === 'dynamic' ? ' fl-map-edge-dynamic' : ''), stroke: color, 'marker-end': 'url(#fl-map-arrow-' + (COLORS[edge.type] ? edge.type : 'jump') + ')' });
      path.appendChild(svgElement('title', {}, edge.type + ' · line ' + edge.lines.join(', ') + (edge.resolution === 'hint' ? ' · target declared by @map' : edge.resolution === 'dynamic' ? ' · runtime target' : '')));
      path.addEventListener('click', function (event) { event.stopPropagation(); if (onEdgeClick) onEdgeClick(edge); });
      const hit = svgElement('path', { d: d, class: 'fl-map-edge-hit' });
      hit.addEventListener('click', function (event) { event.stopPropagation(); if (onEdgeClick) onEdgeClick(edge); });
      svg.appendChild(path);
      svg.appendChild(hit);
    });

    nodeEntries.forEach(function (block) {
      const pos = positions.get(block.id);
      if (!pos) return;
      const isDynamic = block.kind === 'dynamic';
      const nodeLabel = isDynamic ? 'Dynamic target' : block.name;
      const nodeDetail = isDynamic ? String(block.expression || 'runtime value') : block.file + ' · line ' + block.startLine;
      const group = svgElement('g', { class: 'fl-map-node' + (isDynamic ? ' fl-map-node-dynamic' : ''), tabindex: 0, role: 'button', 'aria-label': 'Open ' + nodeLabel + ' in ' + block.file + ' at line ' + block.startLine });
      group.appendChild(svgElement('rect', { x: pos.x, y: pos.y, width: nodeWidth, height: nodeHeight, rx: 7, class: 'fl-map-node-bg' }));
      group.appendChild(svgElement('text', { x: pos.x + 12, y: pos.y + 21, class: 'fl-map-node-name' }, nodeLabel));
      group.appendChild(svgElement('text', { x: pos.x + 12, y: pos.y + 39, class: 'fl-map-node-line' }, nodeDetail));
      group.appendChild(svgElement('title', {}, block.file + ':' + block.startLine + (isDynamic ? ' · dynamic target expression: ' + nodeDetail : '')));
      function activate(event) {
        if (event.type === 'keydown' && event.key !== 'Enter' && event.key !== ' ') return;
        if (event.type === 'keydown') event.preventDefault();
        if (onNodeClick) onNodeClick(block);
      }
      group.addEventListener('click', activate);
      group.addEventListener('keydown', activate);
      svg.appendChild(group);
    });
    viewport.appendChild(svg);
  }

  root.FleditorCodeMap = { render: render };
}(typeof window !== 'undefined' ? window : this));
