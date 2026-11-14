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

  function render(host, model, onNodeClick, onEdgeClick) {
    host.innerHTML = '';
    const toolbar = htmlElement('div', 'fl-code-map-toolbar');
    const summary = htmlElement('span', 'fl-code-map-summary',
      model.blocks.length + ' blocks' + ((model.dynamicNodes || []).length ? ' · ' + model.dynamicNodes.length + ' dynamic targets' : '') + ' · ' + model.edges.length + ' transfers · ' + model.files.length + ' files');
    toolbar.appendChild(summary);
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
    const hintSwatch = htmlElement('i', 'fl-code-map-swatch fl-code-map-swatch-hint');
    hintLegend.appendChild(hintSwatch);
    hintLegend.appendChild(document.createTextNode('@map hint'));
    legend.appendChild(hintLegend);
    const dynamicLegend = htmlElement('span', 'fl-code-map-legend-item');
    const dynamicSwatch = htmlElement('i', 'fl-code-map-swatch fl-code-map-swatch-dynamic');
    dynamicLegend.appendChild(dynamicSwatch);
    dynamicLegend.appendChild(document.createTextNode('dynamic target'));
    legend.appendChild(dynamicLegend);
    toolbar.appendChild(legend);
    host.appendChild(toolbar);

    const viewport = htmlElement('div', 'fl-code-map-viewport');
    host.appendChild(viewport);
    if (!model.blocks.length) {
      viewport.appendChild(htmlElement('div', 'fl-code-map-empty', 'No FlatL blocks found in this project.'));
      return;
    }

    const byFile = new Map();
    model.files.forEach(function (file) { byFile.set(file.path, []); });
    const nodeEntries = model.blocks.concat(model.dynamicNodes || []);
    nodeEntries.forEach(function (node) {
      if (!byFile.has(node.file)) byFile.set(node.file, []);
      byFile.get(node.file).push(node);
    });
    const fileGroups = Array.from(byFile.entries()).filter(function (entry) { return entry[1].length > 0; });
    const colWidth = 300;
    const colGap = 56;
    const nodeWidth = 238;
    const nodeHeight = 48;
    const rowGap = 28;
    const top = 78;
    const left = 24;
    const positions = new Map();
    let maxRows = 1;
    fileGroups.forEach(function (entry, col) {
      entry[1].sort(function (a, b) { return a.startLine - b.startLine; });
      maxRows = Math.max(maxRows, entry[1].length);
      entry[1].forEach(function (block, row) {
        positions.set(block.id, {
          x: left + col * (colWidth + colGap) + 22,
          y: top + row * (nodeHeight + rowGap),
          col: col,
          row: row
        });
      });
    });
    const width = Math.max(640, left * 2 + fileGroups.length * colWidth + Math.max(0, fileGroups.length - 1) * colGap);
    const height = Math.max(260, top + maxRows * (nodeHeight + rowGap) + 46);
    const svg = svgElement('svg', { class: 'fl-code-map-svg', width: width, height: height, viewBox: '0 0 ' + width + ' ' + height, role: 'img', 'aria-label': 'FlatL block transfer map' });
    const defs = svgElement('defs');
    Object.keys(COLORS).forEach(function (type) {
      const marker = svgElement('marker', { id: 'fl-map-arrow-' + type, markerWidth: 8, markerHeight: 8, refX: 6, refY: 3, orient: 'auto', markerUnits: 'strokeWidth' });
      marker.appendChild(svgElement('path', { d: 'M0,0 L0,6 L6,3 z', fill: COLORS[type] }));
      defs.appendChild(marker);
    });
    svg.appendChild(defs);

    fileGroups.forEach(function (entry, col) {
      const x = left + col * (colWidth + colGap);
      const groupHeight = Math.max(150, 100 + entry[1].length * (nodeHeight + rowGap));
      svg.appendChild(svgElement('rect', { x: x, y: 12, width: colWidth, height: groupHeight, rx: 10, class: 'fl-map-file-bg' }));
      svg.appendChild(svgElement('text', { x: x + 14, y: 36, class: 'fl-map-file-title' }, entry[0]));
      svg.appendChild(svgElement('text', { x: x + 14, y: 54, class: 'fl-map-file-meta' }, entry[1].filter(function (node) { return node.kind !== 'dynamic'; }).length + ' blocks' + (entry[1].some(function (node) { return node.kind === 'dynamic'; }) ? ' · ' + entry[1].filter(function (node) { return node.kind === 'dynamic'; }).length + ' dynamic' : '')));
    });

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
      const titleText = edge.type + ' · line ' + edge.lines.join(', ') + (edge.resolution === 'hint' ? ' · target declared by @map' : edge.resolution === 'dynamic' ? ' · runtime target' : '');
      const title = svgElement('title', {}, titleText);
      path.appendChild(title);
      path.addEventListener('click', function (event) {
        event.stopPropagation();
        if (onEdgeClick) onEdgeClick(edge);
      });
      const hit = svgElement('path', { d: d, class: 'fl-map-edge-hit' });
      hit.addEventListener('click', function (event) {
        event.stopPropagation();
        if (onEdgeClick) onEdgeClick(edge);
      });
      svg.appendChild(path);
      svg.appendChild(hit);
    });

    nodeEntries.forEach(function (block) {
      const pos = positions.get(block.id);
      if (!pos) return;
      const isDynamic = block.kind === 'dynamic';
      const nodeLabel = isDynamic ? 'Dynamic target' : block.name;
      const nodeDetail = isDynamic ? String(block.expression || 'runtime value') : 'line ' + block.startLine;
      const group = svgElement('g', { class: 'fl-map-node' + (isDynamic ? ' fl-map-node-dynamic' : ''), tabindex: 0, role: 'button', 'aria-label': 'Open ' + nodeLabel + ' at line ' + block.startLine });
      group.appendChild(svgElement('rect', { x: pos.x, y: pos.y, width: nodeWidth, height: nodeHeight, rx: 7, class: 'fl-map-node-bg' }));
      group.appendChild(svgElement('text', { x: pos.x + 12, y: pos.y + 20, class: 'fl-map-node-name' }, nodeLabel));
      group.appendChild(svgElement('text', { x: pos.x + 12, y: pos.y + 37, class: 'fl-map-node-line' }, nodeDetail));
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
