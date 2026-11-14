/* global FleditorCodeMap */
(function (root) {
  'use strict';

  const NS = 'http://www.w3.org/2000/svg';
  const COLORS = { jump: '#60a5fa', call: '#4ade80', or: '#facc15', panic: '#f87171', for: '#c084fc', foreach: '#c084fc' };
  let displayMode = 'graph';
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

  function layoutKey(model) { return 'fleditor-code-map:' + (model.entryPath || 'project'); }
  function loadSavedPositions(model) {
    try { const raw = root.localStorage && root.localStorage.getItem(layoutKey(model)); return raw ? JSON.parse(raw) : null; }
    catch (_) { return null; }
  }
  function savePositions(model, positions) {
    try {
      const data = {};
      positions.forEach((p,id)=>{ data[id]={x:p.x,y:p.y}; });
      if (root.localStorage) root.localStorage.setItem(layoutKey(model), JSON.stringify(data));
    } catch (_) {}
  }
  function computePositions(nodes, edges, model, mode, forceArrange) {
    const positions = new Map(), nodeW=180, nodeH=28, margin=72, grid=20, gapX=260, gapY=100;
    const saved=!forceArrange?loadSavedPositions(model):null;
    if(saved) nodes.forEach(n=>{const p=saved[n.id];if(p&&Number.isFinite(p.x)&&Number.isFinite(p.y))positions.set(n.id,{x:p.x,y:p.y});});
    if(positions.size!==nodes.length) {
      const graph=getLayers(nodes,edges,model.entryPath||(model.files[0]&&model.files[0].path));
      const layers=orderLayers(graph.layers,edges,graph.depth);
      if(mode==='by-file') {
        const files=Array.from(new Set(nodes.map(n=>n.file))).sort();
        files.forEach((file,col)=>{
          const arr=nodes.filter(n=>n.file===file).sort(nodeCompare);
          const cols=Math.max(1,Math.ceil(Math.sqrt(arr.length)));
          arr.forEach((n,i)=>{if(!positions.has(n.id))positions.set(n.id,{x:margin+col*gapX+(i%cols)*120,y:margin+Math.floor(i/cols)*gapY});});
        });
      } else {
        layers.forEach((arr,layer)=>arr.forEach((n,i)=>{
          if(!positions.has(n.id)) positions.set(n.id,{x:margin+layer*gapX,y:margin+i*gapY});
        }));
      }
    }
    // Keep manually saved positions, but snap automatically-created positions to the routing grid.
    positions.forEach((p,id)=>{p.x=Math.round(p.x/grid)*grid;p.y=Math.round(p.y/grid)*grid;});
    const maxX=Math.max(500,...Array.from(positions.values(),p=>p.x+nodeW+150));
    const maxY=Math.max(420,...Array.from(positions.values(),p=>p.y+nodeH+100));
    return {positions,nodeW,nodeH,width:maxX,height:maxY,groups:Array.from(new Set(nodes.map(n=>n.file))).map(f=>[f,nodes.filter(n=>n.file===f)])};
  }
  function routeAll(nodes, edges, positions, nodeW, nodeH) {
    const step=20, radius=12, occupied=new Set(), usedSegments=new Set(), routes=[];
    const byId=new Map(nodes.map(n=>[n.id,n]));
    const center=id=>{const p=positions.get(id);return p?{x:p.x+radius,y:p.y+radius}:null;};
    const key=(x,y)=>x+','+y;
    const segKey=(a,b)=>[key(a.x,a.y),key(b.x,b.y)].sort().join('|');
    const nodeCells=new Set();
    nodes.forEach(n=>{const c=center(n.id);if(!c)return;for(let x=-2;x<=2;x++)for(let y=-2;y<=2;y++)if(x*x+y*y<=5)nodeCells.add(key(Math.round(c.x/step)+x,Math.round(c.y/step)+y));});
    const ordered=edges.map((e,i)=>({e,i})).sort((a,b)=>{
      const ca=center(a.e.from),cb=center(a.e.to),da=center(a.e.from),db=center(a.e.to);
      return (Math.hypot(db.x-da.x,db.y-da.y)-Math.hypot(cb.x-ca.x,cb.y-ca.y));
    });
    function port(from,to,edge,source) {
      const a=center(from),b=center(to);if(!a||!b)return null;
      const angle=Math.atan2(b.y-a.y,b.x-a.x);
      const c={x:a.x+Math.cos(angle)*(radius+step),y:a.y+Math.sin(angle)*(radius+step)};
      return {x:Math.round(c.x/step),y:Math.round(c.y/step)};
    }
    function astar(start,goal) {
      const open=[{x:start.x,y:start.y,g:0,f:Math.hypot(goal.x-start.x,goal.y-start.y)}], came=new Map(), costs=new Map([[key(start.x,start.y),0]]), closed=new Set();
      let found=null, iterations=0;
      while(open.length&&iterations++<30000){
        open.sort((a,b)=>a.f-b.f);const cur=open.shift(),ck=key(cur.x,cur.y);if(closed.has(ck))continue;closed.add(ck);
        if(cur.x===goal.x&&cur.y===goal.y){found=cur;break;}
        for(let dx=-1;dx<=1;dx++)for(let dy=-1;dy<=1;dy++){
          if(!dx&&!dy)continue;const nx=cur.x+dx,ny=cur.y+dy,nk=key(nx,ny);
          if(nodeCells.has(nk)&&!(nx===goal.x&&ny===goal.y))continue;
          if(occupied.has(nk)&&!(nx===goal.x&&ny===goal.y))continue;
          const sk=segKey(cur,{x:nx,y:ny});if(usedSegments.has(sk))continue;
          if(dx&&dy&&usedSegments.has(segKey({x:cur.x+dx,y:cur.y},{x:cur.x,y:cur.y+dy})))continue;
          // Prevent diagonal corner-cutting through occupied cells.
          if(dx&&dy&&(occupied.has(key(cur.x+dx,cur.y))||occupied.has(key(cur.x,cur.y+dy))))continue;
          const ng=cur.g+(dx&&dy?Math.SQRT2:1)+(dx&&dy?0.15:0);
          if(ng>=(costs.get(nk)??Infinity))continue;
          costs.set(nk,ng);came.set(nk,ck);open.push({x:nx,y:ny,g:ng,f:ng+Math.hypot(goal.x-nx,goal.y-ny)});
        }
      }
      if(!found)return null;const path=[];let k=key(found.x,found.y);
      while(k){const [x,y]=k.split(',').map(Number);path.push({x,y});k=came.get(k);}
      return path.reverse();
    }
    ordered.forEach(({e,i})=>{
      const a=port(e.from,e.to,e,true),b=port(e.to,e.from,e,false);if(!a||!b)return;
      // Ports are outside node keep-out zones; retry with expanding routing space.
      let path=astar(a,b);
      if(!path){const pad=8+i%5;const s={x:a.x+(a.x<b.x?-pad:pad),y:a.y+(a.y<b.y?-pad:pad)},t={x:b.x+(b.x<a.x?-pad:pad),y:b.y+(b.y<a.y?-pad:pad)};path=astar(s,t);if(path)path=[a,...path,b];}
      if(!path){routes[i]=null;return;}
      path.forEach((pt,j)=>{occupied.add(key(pt.x,pt.y));if(j)usedSegments.add(segKey(path[j-1],pt));});
      // Simplify collinear grid points while preserving 45-degree bends.
      const simple=path.filter((pt,j)=>j===0||j===path.length-1||((pt.x-path[j-1].x)*(path[j+1].y-pt.y)!==(pt.y-path[j-1].y)*(path[j+1].x-pt.x)));
      routes[i]='M '+simple.map(pt=>(pt.x*step)+' '+(pt.y*step)).join(' L ');
    });
    return routes;
  }

  function render(host, model, onNodeClick, onEdgeClick, forceArrange) {
    host.innerHTML = '';
    const toolbar=htmlElement('div','fl-code-map-toolbar');
    const summary=htmlElement('span','fl-code-map-summary',model.blocks.length+' blocks'+((model.dynamicNodes||[]).length?' · '+model.dynamicNodes.length+' dynamic targets':'')+' · '+model.edges.length+' transfers · '+model.files.length+' files');
    toolbar.appendChild(summary);
    const controls=htmlElement('div','fl-code-map-controls');
    [['graph','Graph'],['by-file','By file']].forEach(opt=>{
      const b=htmlElement('button','fl-code-map-mode'+(displayMode===opt[0]?' active':''),opt[1]); b.type='button'; b.setAttribute('aria-pressed',displayMode===opt[0]?'true':'false');
      b.addEventListener('click',()=>{displayMode=opt[0];render(host,model,onNodeClick,onEdgeClick);}); controls.appendChild(b);
    });
    const arrange=htmlElement('button','fl-code-map-mode',''); arrange.title='Arrange graph'; arrange.setAttribute('aria-label','Arrange graph'); arrange.appendChild(svgElement('svg',{width:16,height:16,viewBox:'0 0 24 24',fill:'none',stroke:'currentColor','stroke-width':2},null));
    const ai=arrange.querySelector('svg'); ai.appendChild(svgElement('circle',{cx:5,cy:5,r:2})); ai.appendChild(svgElement('circle',{cx:19,cy:5,r:2})); ai.appendChild(svgElement('circle',{cx:12,cy:19,r:2})); ai.appendChild(svgElement('path',{d:'M7 6.5 L10.5 16.5 M17 6.5 L13.5 16.5 M7 5 H17'}));
    arrange.addEventListener('click',()=>{savePositions(model,new Map());try{root.localStorage.removeItem(layoutKey(model));}catch(_){}render(host,model,onNodeClick,onEdgeClick,true);}); controls.appendChild(arrange);
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
    const layout=computePositions(nodes,model.edges,model,displayMode,!!forceArrange);
    const positions=layout.positions;
    manualPositions.forEach((p,id)=>{if(positions.has(id))Object.assign(positions.get(id),p);});
    const width=layout.width,height=layout.height;
    const routedPaths=routeAll(nodes,model.edges,positions,layout.nodeW,layout.nodeH);
    const svg=svgElement('svg',{class:'fl-code-map-svg',width:Math.ceil(width*zoom),height:Math.ceil(height*zoom),viewBox:`0 0 ${width} ${height}`,role:'img','aria-label':'FlatL block transfer map'});
    const defs=svgElement('defs');Object.keys(COLORS).forEach(type=>{const marker=svgElement('marker',{id:'fl-map-arrow-'+type,markerWidth:8,markerHeight:8,refX:6,refY:3,orient:'auto',markerUnits:'strokeWidth'});marker.appendChild(svgElement('path',{d:'M0,0 L0,6 L6,3 z',fill:COLORS[type]}));defs.appendChild(marker);});svg.appendChild(defs);
    if(displayMode==='by-file'){
      layout.groups.forEach(([file,arr])=>{if(!arr.length)return;const x=Math.min(...arr.map(n=>positions.get(n.id).x))-24;const y=Math.min(...arr.map(n=>positions.get(n.id).y))-38;const right=Math.max(...arr.map(n=>positions.get(n.id).x))+220;const bottom=Math.max(...arr.map(n=>positions.get(n.id).y))+48;
        svg.appendChild(svgElement('rect',{x,y,width:right-x,height:bottom-y,rx:10,class:'fl-map-file-bg'}));svg.appendChild(svgElement('text',{x:x+12,y:y+20,class:'fl-map-file-title'},file));});
    }
    // Draw every model edge exactly once; none are filtered by layout or direction.
    model.edges.forEach((edge,index)=>{
      const from=positions.get(edge.from),to=positions.get(edge.to);if(!from||!to)return;
      const d=routedPaths[index];if(!d)return;const color=COLORS[edge.type]||'#a1a1aa';
      const path=svgElement('path',{d,class:'fl-map-edge'+(edge.resolution==='hint'?' fl-map-edge-hint':edge.resolution==='dynamic'?' fl-map-edge-dynamic':''),stroke:color,'marker-end':'url(#fl-map-arrow-'+(COLORS[edge.type]?edge.type:'jump')+')','data-from':edge.from,'data-to':edge.to});
      path.appendChild(svgElement('title',{},edge.type+' · line '+edge.lines.join(', ')+(edge.resolution==='hint'?' · @map hint':edge.resolution==='dynamic'?' · dynamic target':'')));
      const hit=svgElement('path',{d,class:'fl-map-edge-hit'});hit.addEventListener('click',ev=>{ev.stopPropagation();if(onEdgeClick)onEdgeClick(edge);});
      path.addEventListener('click',ev=>{ev.stopPropagation();if(onEdgeClick)onEdgeClick(edge);});svg.appendChild(path);svg.appendChild(hit);
    });
    nodes.forEach(block=>{
      const p=positions.get(block.id);if(!p)return;const dynamic=block.kind==='dynamic';
      const g=svgElement('g',{class:'fl-map-node'+(dynamic?' fl-map-node-dynamic':'')+(selectedNodeId===block.id?' selected':''),tabindex:0,role:'button','data-node-id':block.id});
      g.appendChild(svgElement('circle',{cx:p.x+12,cy:p.y+12,r:10,class:'fl-map-node-bg'}));
      g.appendChild(svgElement('text',{x:p.x+30,y:p.y+16,class:'fl-map-node-name'},dynamic?'Dynamic target':block.name));
      g.appendChild(svgElement('title',{},block.file+' · line '+block.startLine));
      function activate(ev){if(ev.type==='keydown'&&ev.key!=='Enter'&&ev.key!==' ')return;if(ev.type==='keydown')ev.preventDefault();selectedNodeId=block.id;highlight();if(onNodeClick)onNodeClick(block);}
      g.addEventListener('click',activate);g.addEventListener('keydown',activate);
      g.addEventListener('pointerdown',ev=>{if(ev.button!==0)return;const startX=ev.clientX,startY=ev.clientY,orig={x:p.x,y:p.y};g.setPointerCapture(ev.pointerId);
        function move(e){const next={x:orig.x+(e.clientX-startX)/zoom,y:orig.y+(e.clientY-startY)/zoom};p.x=next.x;p.y=next.y;manualPositions.set(block.id,next);savePositions(model,positions);g.querySelector('circle').setAttribute('cx',p.x+12);g.querySelector('circle').setAttribute('cy',p.y+12);g.querySelector('text').setAttribute('x',p.x+30);g.querySelector('text').setAttribute('y',p.y+16);redrawEdges();}
        function up(){g.removeEventListener('pointermove',move);g.removeEventListener('pointerup',up);}g.addEventListener('pointermove',move);g.addEventListener('pointerup',up,{once:true});});
      svg.appendChild(g);
    });
    function highlight(){
      const connected=new Set([selectedNodeId]);model.edges.forEach(e=>{if(e.from===selectedNodeId)connected.add(e.to);if(e.to===selectedNodeId)connected.add(e.from);});
      svg.querySelectorAll('.fl-map-node').forEach(n=>{const id=n.getAttribute('data-node-id');n.classList.toggle('selected',id===selectedNodeId);n.classList.toggle('dimmed',selectedNodeId!==null&&!connected.has(id));});
      svg.querySelectorAll('.fl-map-edge').forEach(e=>{const active=e.getAttribute('data-from')===selectedNodeId||e.getAttribute('data-to')===selectedNodeId;e.classList.toggle('highlighted',active);e.classList.toggle('dimmed',selectedNodeId!==null&&!active);});
    }
    function redrawEdges(){const next=routeAll(nodes,model.edges,positions,layout.nodeW,layout.nodeH);const paths=svg.querySelectorAll('.fl-map-edge, .fl-map-edge-hit');let i=0;model.edges.forEach((edge,index)=>{const d=next[index];if(!d)return;paths[i++].setAttribute('d',d);paths[i++].setAttribute('d',d);});}
    svg.addEventListener('click',ev=>{if(ev.target===svg){selectedNodeId=null;highlight();}});
    highlight();viewport.appendChild(svg);
  }
  root.FleditorCodeMap={render:render};
}(typeof window!=='undefined'?window:this));
