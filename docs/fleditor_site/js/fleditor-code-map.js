/* global FleditorCodeMap */
(function (root) {
  'use strict';

  const NS = 'http://www.w3.org/2000/svg';
  const COLORS = { jump: '#60a5fa', call: '#4ade80', or: '#facc15', panic: '#f87171', for: '#c084fc', foreach: '#c084fc' };
  let displayMode = 'vertical';
  let zoom = 1;
  let selectedNodeId = null;
  let manualPositions = new Map();

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
  function nodeCompare(a, b) { return a.file.localeCompare(b.file) || a.startLine - b.startLine || a.name.localeCompare(b.name); }

  // Layer assignment follows every real transfer. Disconnected nodes remain visible.
  function getLayers(nodes, edges, entryPath) {
    const byId = new Map(nodes.map(n => [n.id, n]));
    const outgoing = new Map(nodes.map(n => [n.id, []]));
    edges.forEach(e => { if (outgoing.has(e.from) && byId.has(e.to)) outgoing.get(e.from).push(e.to); });
    const depth = new Map(), queue = [];
    nodes.filter(n => n.file === entryPath && n.kind !== 'dynamic').forEach(n => { depth.set(n.id, 0); queue.push(n.id); });
    for (let head = 0; head < queue.length; head++) {
      const id = queue[head];
      (outgoing.get(id) || []).forEach(to => { if (!depth.has(to)) { depth.set(to, depth.get(id) + 1); queue.push(to); } });
    }
    let max = 0; depth.forEach(v => { max = Math.max(max, v); });
    nodes.forEach(n => { if (!depth.has(n.id)) depth.set(n.id, max + 1); });
    const layers = new Map();
    nodes.forEach(n => { const d = depth.get(n.id); if (!layers.has(d)) layers.set(d, []); layers.get(d).push(n); });
    return { layers: Array.from(layers.entries()).sort((a,b)=>a[0]-b[0]).map(x=>x[1]), depth };
  }

  // Reorder nodes inside layers using repeated predecessor/successor barycentres to reduce crossings.
  function orderLayers(layers, edges, depth) {
    const order = new Map();
    layers.forEach((layer, i) => layer.forEach((n, j) => order.set(n.id, j)));
    const incoming = new Map(), outgoing = new Map();
    layers.flat().forEach(n => { incoming.set(n.id, []); outgoing.set(n.id, []); });
    edges.forEach(e => { if (incoming.has(e.to) && outgoing.has(e.from)) { outgoing.get(e.from).push(e.to); incoming.get(e.to).push(e.from); } });
    function sweep(indices, neighbors) {
      indices.forEach(i => {
        const layer = layers[i];
        layer.sort((a,b) => {
          function bary(n) { const ns=neighbors.get(n.id)||[]; return ns.length ? ns.reduce((s,id)=>s+(order.get(id)||0),0)/ns.length : order.get(n.id); }
          const diff=bary(a)-bary(b); return Math.abs(diff)>0.001 ? diff : nodeCompare(a,b);
        });
        layer.forEach((n,j)=>order.set(n.id,j));
      });
    }
    for (let pass=0; pass<4; pass++) { sweep(layers.map((_,i)=>i).slice(1), incoming); sweep(layers.map((_,i)=>i).slice(0,-1).reverse(), outgoing); }
    return layers;
  }

  function computePositions(nodes, edges, model, mode) {
    const positions = new Map();
    const nodeW = 252, nodeH = 54, xGap = 110, yGap = 34, margin = 44;
    if (mode === 'by-file') {
      const files = (model.files || []).map(f => f.path);
      const groups = new Map(files.map(f => [f, []]));
      nodes.forEach(n => { if (!groups.has(n.file)) groups.set(n.file, []); groups.get(n.file).push(n); });
      Array.from(groups.values()).forEach(a => a.sort(nodeCompare));
      Array.from(groups.entries()).forEach(([file, arr], col) => arr.forEach((n,row)=>positions.set(n.id,{x:margin+col*(nodeW+xGap),y:margin+70+row*(nodeH+yGap),col,row,file})));
      return { positions, nodeW, nodeH, columns: Array.from(groups.values()), groups: Array.from(groups.entries()), width: margin*2+groups.size*(nodeW+xGap), height: Math.max(300,...Array.from(groups.values()).map(a=>margin+90+a.length*(nodeH+yGap))) };
    }
    const graph = getLayers(nodes, edges, model.entryPath || (model.files[0] && model.files[0].path));
    const layers = orderLayers(graph.layers, edges, graph.depth);
    const maxCount = Math.max(1,...layers.map(a=>a.length));
    if (mode === 'horizontal') {
      layers.forEach((arr,col)=>arr.forEach((n,row)=>positions.set(n.id,{x:margin+col*(nodeW+xGap),y:margin+row*(nodeH+yGap),col,row})));
      return {positions,nodeW,nodeH,width:margin*2+layers.length*(nodeW+xGap),height:margin*2+maxCount*(nodeH+yGap),layers};
    }
    // Vertical: same dependency layers, rotated; each layer is a row of nodes.
    layers.forEach((arr,row)=>arr.forEach((n,col)=>positions.set(n.id,{x:margin+col*(nodeW+xGap),y:margin+row*(nodeH+yGap),col,row})));
    return {positions,nodeW,nodeH,width:margin*2+maxCount*(nodeW+xGap),height:margin*2+layers.length*(nodeH+yGap),layers};
  }

  function routeEdge(from, to, edge, index, mode, nodeW, nodeH) {
    const sameColumn = from.col === to.col;
    const sameRow = from.row === to.row;
    const offset = ((index % 7) - 3) * 7;
    if (mode === 'vertical' && !sameRow) {
      const x1=from.x+nodeW/2, y1=from.y+nodeH, x2=to.x+nodeW/2, y2=to.y;
      const lane=Math.max(y1,y2)+26+Math.abs(offset);
      return `M ${x1} ${y1} C ${x1+offset} ${lane}, ${x2+offset} ${lane}, ${x2} ${y2}`;
    }
    if (mode === 'horizontal' && !sameColumn) {
      const x1=from.x+nodeW, y1=from.y+nodeH/2, x2=to.x, y2=to.y+nodeH/2;
      const lane=Math.max(x1,x2)+26+Math.abs(offset);
      return `M ${x1} ${y1} C ${lane} ${y1+offset}, ${lane} ${y2+offset}, ${x2} ${y2}`;
    }
    if (mode === 'by-file') {
      const forward=to.x>=from.x;
      const x1=forward?from.x+nodeW:from.x, x2=forward?to.x:to.x+nodeW;
      const y1=from.y+nodeH/2, y2=to.y+nodeH/2;
      const lane=forward ? (x1+x2)/2+offset : Math.min(x1,x2)-42-Math.abs(offset);
      return `M ${x1} ${y1} C ${lane} ${y1+offset}, ${lane} ${y2+offset}, ${x2} ${y2}`;
    }
    // Back-edges and same-layer edges use an outer lane, keeping loops visible.
    const x1=from.x+nodeW/2, y1=from.y+nodeH/2, x2=to.x+nodeW/2, y2=to.y+nodeH/2;
    if (mode === 'vertical') {
      const lane=Math.min(from.y,to.y)-38-Math.abs(offset);
      return `M ${x1} ${y1} C ${x1+offset} ${lane}, ${x2+offset} ${lane}, ${x2} ${y2}`;
    }
    const lane=Math.min(from.x,to.x)-38-Math.abs(offset);
    return `M ${x1} ${y1} C ${lane} ${y1+offset}, ${lane} ${y2+offset}, ${x2} ${y2}`;
  }

  function render(host, model, onNodeClick, onEdgeClick) {
    host.innerHTML = '';
    const toolbar=htmlElement('div','fl-code-map-toolbar');
    const summary=htmlElement('span','fl-code-map-summary',model.blocks.length+' blocks'+((model.dynamicNodes||[]).length?' · '+model.dynamicNodes.length+' dynamic targets':'')+' · '+model.edges.length+' transfers · '+model.files.length+' files');
    toolbar.appendChild(summary);
    const controls=htmlElement('div','fl-code-map-controls'); controls.appendChild(htmlElement('span','fl-code-map-mode-label','Layout'));
    [['vertical','Vertical'],['horizontal','Horizontal'],['by-file','By file']].forEach(opt=>{
      const b=htmlElement('button','fl-code-map-mode'+(displayMode===opt[0]?' active':''),opt[1]); b.type='button'; b.setAttribute('aria-pressed',displayMode===opt[0]?'true':'false');
      b.addEventListener('click',()=>{displayMode=opt[0];manualPositions.clear();render(host,model,onNodeClick,onEdgeClick);}); controls.appendChild(b);
    });
    const zoomOut=htmlElement('button','fl-code-map-mode','−'); zoomOut.title='Zoom out'; zoomOut.addEventListener('click',()=>{zoom=Math.max(.45,zoom-.15);render(host,model,onNodeClick,onEdgeClick);});
    const zoomIn=htmlElement('button','fl-code-map-mode','+'); zoomIn.title='Zoom in'; zoomIn.addEventListener('click',()=>{zoom=Math.min(2,zoom+.15);render(host,model,onNodeClick,onEdgeClick);});
    const fit=htmlElement('button','fl-code-map-mode','Fit'); fit.title='Fit map'; fit.addEventListener('click',()=>{zoom=1;render(host,model,onNodeClick,onEdgeClick);});
    controls.appendChild(zoomOut);controls.appendChild(zoomIn);controls.appendChild(fit);toolbar.appendChild(controls);
    const legend=htmlElement('div','fl-code-map-legend');
    Object.keys(COLORS).forEach(type=>{const item=htmlElement('span','fl-code-map-legend-item');const sw=htmlElement('i','fl-code-map-swatch');sw.style.backgroundColor=COLORS[type];item.appendChild(sw);item.appendChild(document.createTextNode(type));legend.appendChild(item);});
    const hint=htmlElement('span','fl-code-map-legend-item');hint.appendChild(htmlElement('i','fl-code-map-swatch fl-code-map-swatch-hint'));hint.appendChild(document.createTextNode('@map hint'));legend.appendChild(hint);
    const dyn=htmlElement('span','fl-code-map-legend-item');dyn.appendChild(htmlElement('i','fl-code-map-swatch fl-code-map-swatch-dynamic'));dyn.appendChild(document.createTextNode('dynamic target'));legend.appendChild(dyn);toolbar.appendChild(legend);host.appendChild(toolbar);

    const viewport=htmlElement('div','fl-code-map-viewport');host.appendChild(viewport);
    const nodes=model.blocks.concat(model.dynamicNodes||[]);
    if(!nodes.length){viewport.appendChild(htmlElement('div','fl-code-map-empty','No FlatL blocks found in this file or its imported files.'));return;}
    const layout=computePositions(nodes,model.edges,model,displayMode);
    const positions=layout.positions;
    manualPositions.forEach((p,id)=>{if(positions.has(id))Object.assign(positions.get(id),p);});
    const width=layout.width,height=layout.height;
    const svg=svgElement('svg',{class:'fl-code-map-svg',width:Math.ceil(width*zoom),height:Math.ceil(height*zoom),viewBox:`0 0 ${width} ${height}`,role:'img','aria-label':'FlatL block transfer map'});
    const defs=svgElement('defs');Object.keys(COLORS).forEach(type=>{const marker=svgElement('marker',{id:'fl-map-arrow-'+type,markerWidth:8,markerHeight:8,refX:6,refY:3,orient:'auto',markerUnits:'strokeWidth'});marker.appendChild(svgElement('path',{d:'M0,0 L0,6 L6,3 z',fill:COLORS[type]}));defs.appendChild(marker);});svg.appendChild(defs);
    if(displayMode==='by-file'){
      layout.groups.forEach(([file,arr],col)=>{if(!arr.length)return;const x=Math.min(...arr.map(n=>positions.get(n.id).x))-14;const y=26;const bottom=Math.max(...arr.map(n=>positions.get(n.id).y+layout.nodeH))+16;
        svg.appendChild(svgElement('rect',{x,y,width:layout.nodeW+28,height:bottom-y,rx:10,class:'fl-map-file-bg'}));svg.appendChild(svgElement('text',{x:x+14,y:y+22,class:'fl-map-file-title'},file));});
    }
    // Draw every model edge exactly once; none are filtered by layout or direction.
    model.edges.forEach((edge,index)=>{
      const from=positions.get(edge.from),to=positions.get(edge.to);if(!from||!to)return;
      const d=routeEdge(from,to,edge,index,displayMode,layout.nodeW,layout.nodeH);const color=COLORS[edge.type]||'#a1a1aa';
      const path=svgElement('path',{d,class:'fl-map-edge'+(edge.resolution==='hint'?' fl-map-edge-hint':edge.resolution==='dynamic'?' fl-map-edge-dynamic':''),stroke:color,'marker-end':'url(#fl-map-arrow-'+(COLORS[edge.type]?edge.type:'jump')+')','data-from':edge.from,'data-to':edge.to});
      path.appendChild(svgElement('title',{},edge.type+' · line '+edge.lines.join(', ')+(edge.resolution==='hint'?' · @map hint':edge.resolution==='dynamic'?' · dynamic target':'')));
      const hit=svgElement('path',{d,class:'fl-map-edge-hit'});hit.addEventListener('click',ev=>{ev.stopPropagation();if(onEdgeClick)onEdgeClick(edge);});
      path.addEventListener('click',ev=>{ev.stopPropagation();if(onEdgeClick)onEdgeClick(edge);});svg.appendChild(path);svg.appendChild(hit);
    });
    nodes.forEach(block=>{
      const p=positions.get(block.id);if(!p)return;const dynamic=block.kind==='dynamic';const g=svgElement('g',{class:'fl-map-node'+(dynamic?' fl-map-node-dynamic':'')+(selectedNodeId===block.id?' selected':''),tabindex:0,role:'button','data-node-id':block.id});
      g.appendChild(svgElement('rect',{x:p.x,y:p.y,width:layout.nodeW,height:layout.nodeH,rx:7,class:'fl-map-node-bg'}));
      g.appendChild(svgElement('text',{x:p.x+12,y:p.y+22,class:'fl-map-node-name'},dynamic?'Dynamic target':block.name));
      g.appendChild(svgElement('text',{x:p.x+12,y:p.y+41,class:'fl-map-node-line'},dynamic?String(block.expression||'runtime value'):block.file+' · line '+block.startLine));
      function activate(ev){if(ev.type==='keydown'&&ev.key!=='Enter'&&ev.key!==' ')return;if(ev.type==='keydown')ev.preventDefault();selectedNodeId=block.id;highlight();if(onNodeClick)onNodeClick(block);}
      g.addEventListener('click',activate);g.addEventListener('keydown',activate);
      // Pointer drag: manual positions are retained when switching back to this layout.
      g.addEventListener('pointerdown',ev=>{if(ev.button!==0)return;const startX=ev.clientX,startY=ev.clientY,orig={x:p.x,y:p.y};g.setPointerCapture(ev.pointerId);
        function move(e){const scale=zoom;const next={x:orig.x+(e.clientX-startX)/scale,y:orig.y+(e.clientY-startY)/scale};p.x=next.x;p.y=next.y;manualPositions.set(block.id,next);g.querySelector('rect').setAttribute('x',p.x);g.querySelectorAll('text').forEach((t,i)=>t.setAttribute('x',p.x+12));g.querySelectorAll('text').forEach((t,i)=>t.setAttribute('y',p.y+(i===0?22:41)));redrawEdges();}
        function up(){g.removeEventListener('pointermove',move);g.removeEventListener('pointerup',up);}g.addEventListener('pointermove',move);g.addEventListener('pointerup',up,{once:true});});
      svg.appendChild(g);
    });
    function highlight(){
      const connected=new Set([selectedNodeId]);model.edges.forEach(e=>{if(e.from===selectedNodeId)connected.add(e.to);if(e.to===selectedNodeId)connected.add(e.from);});
      svg.querySelectorAll('.fl-map-node').forEach(n=>{const id=n.getAttribute('data-node-id');n.classList.toggle('selected',id===selectedNodeId);n.classList.toggle('dimmed',selectedNodeId!==null&&!connected.has(id));});
      svg.querySelectorAll('.fl-map-edge').forEach(e=>{const active=e.getAttribute('data-from')===selectedNodeId||e.getAttribute('data-to')===selectedNodeId;e.classList.toggle('highlighted',active);e.classList.toggle('dimmed',selectedNodeId!==null&&!active);});
    }
    function redrawEdges(){const paths=svg.querySelectorAll('.fl-map-edge, .fl-map-edge-hit');let i=0;model.edges.forEach((edge,index)=>{const a=positions.get(edge.from),b=positions.get(edge.to);if(!a||!b)return;const d=routeEdge(a,b,edge,index,displayMode,layout.nodeW,layout.nodeH);paths[i++].setAttribute('d',d);paths[i++].setAttribute('d',d);});}
    svg.addEventListener('click',ev=>{if(ev.target===svg){selectedNodeId=null;highlight();}});
    highlight();viewport.appendChild(svg);
  }
  root.FleditorCodeMap={render:render};
}(typeof window!=='undefined'?window:this));
