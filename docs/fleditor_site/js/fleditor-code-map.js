/* global FleditorCodeMap */
(function (root) {
  'use strict';

  const COLORS = { jump:'#60a5fa', call:'#4ade80', or:'#facc15', panic:'#f87171', for:'#c084fc', foreach:'#c084fc' };
  let displayMode='graph', zoom=1, selectedNodeId=null, manualPositions=new Map();
  let activeView=null;

  function html(name, cls, text) { const e=document.createElement(name); if(cls)e.className=cls; if(text!=null)e.textContent=text; return e; }
  function key(model){return 'fleditor-code-map:v3:'+ (model.entryPath||'project');}
  function loadPositions(model){try{return JSON.parse(root.localStorage.getItem(key(model))||'null');}catch(_){return null;}}
  function savePositions(model, positions){try{const o={};positions.forEach((p,id)=>o[id]={x:p.x,y:p.y});root.localStorage.setItem(key(model),JSON.stringify(o));}catch(_){}}
  function compare(a,b){return a.file.localeCompare(b.file)||a.startLine-b.startLine||a.name.localeCompare(b.name);}
  function center(p){return {x:p.x,y:p.y};}
  function measure(ctx, node){
    ctx.font='600 12px ui-monospace, Menlo, Consolas, monospace';
    return Math.max(34,Math.ceil(ctx.measureText(node.kind==='dynamic'?'Dynamic target':node.name).width+4));
  }
  // Find the longest forward execution path from the entry block.
  // This becomes the visual spine; other nodes are placed as branches around it.
  function findMainPath(nodes, forward, roots) {
    const ids=new Set(nodes.map(n=>n.id)), outgoing=new Map(nodes.map(n=>[n.id,[]]));
    forward.forEach(e=>{if(ids.has(e.from)&&ids.has(e.to))outgoing.get(e.from).push(e.to);});
    const memo=new Map(), visiting=new Set();
    function best(id){
      if(memo.has(id))return memo.get(id);
      if(visiting.has(id))return {score:0,path:[id]};
      visiting.add(id);
      let choice={score:0,path:[id]};
      (outgoing.get(id)||[]).forEach(to=>{
        const next=best(to),candidate={score:1+next.score,path:[id,...next.path]};
        if(candidate.score>choice.score)choice=candidate;
      });
      visiting.delete(id);memo.set(id,choice);return choice;
    }
    let result={score:-1,path:[]};
    roots.forEach(n=>{const candidate=best(n.id);if(candidate.score>result.score)result=candidate;});
    if(!result.path.length&&nodes.length)result=best(nodes[0].id);
    return result.path;
  }
  function layoutGraph(nodes, edges, model, mode, force) {
    const pos=new Map(), rank=new Map(), outgoing=new Map(nodes.map(n=>[n.id,[]])), incoming=new Map(nodes.map(n=>[n.id,[]]));
    const saved=!force?loadPositions(model):null;
    if(saved) nodes.forEach(n=>{const p=saved[n.id];if(p&&Number.isFinite(p.x)&&Number.isFinite(p.y))pos.set(n.id,{x:p.x,y:p.y});});
    const entry=model.entryPath||(model.files[0]&&model.files[0].path);
    nodes.forEach(n=>rank.set(n.id,Infinity));
    const queue=[];
    edges.forEach(e=>{if(outgoing.has(e.from)&&incoming.has(e.to)){outgoing.get(e.from).push(e.to);incoming.get(e.to).push(e.from);}});
    const entryNodes=nodes.filter(n=>n.file===entry&&n.kind!=='dynamic');
    const mainNodes=entryNodes.filter(n=>String(n.name).replace(/^#/,'').toLowerCase()==='main');
    const roots=mainNodes.length?mainNodes:entryNodes.filter(n=>(incoming.get(n.id)||[]).length===0);
    (roots.length?roots:entryNodes.slice(0,1)).sort(compare).forEach(n=>{rank.set(n.id,0);queue.push(n.id);});
    for(let i=0;i<queue.length;i++){const id=queue[i];(outgoing.get(id)||[]).forEach(to=>{if(rank.get(to)===Infinity){rank.set(to,rank.get(id)+1);queue.push(to);}});}
    let max=0;rank.forEach(v=>{if(Number.isFinite(v))max=Math.max(max,v);});
    nodes.forEach(n=>{if(!Number.isFinite(rank.get(n.id)))rank.set(n.id,max+1);});
    // Only forward edges constrain the layout; backward/cyclic edges remain drawable.
    const forward=edges.filter(e=>rank.get(e.to)>rank.get(e.from));
    const layers=new Map();nodes.forEach(n=>{const r=rank.get(n.id);if(!layers.has(r))layers.set(r,[]);layers.get(r).push(n);});
    const ordered=Array.from(layers.entries()).sort((a,b)=>a[0]-b[0]).map(x=>x[1]);
    ordered.forEach(a=>a.sort(compare));
    const order=new Map();ordered.forEach((a,i)=>a.forEach((n,j)=>order.set(n.id,j)));
    const pred=new Map(nodes.map(n=>[n.id,[]])),succ=new Map(nodes.map(n=>[n.id,[]]));
    forward.forEach(e=>{pred.get(e.to).push(e.from);succ.get(e.from).push(e.to);});
    for(let pass=0;pass<6;pass++){
      const sweep=(arr,neighbors)=>arr.forEach(layer=>{layer.sort((a,b)=>{
        const bary=n=>{const ns=neighbors.get(n.id);return ns.length?ns.reduce((v,id)=>v+(order.get(id)||0),0)/ns.length:(order.get(n.id)||0);};
        return bary(a)-bary(b)||compare(a,b);
      });layer.forEach((n,i)=>order.set(n.id,i));});
      sweep(ordered.slice(1),pred);sweep(ordered.slice(0,-1).reverse(),succ);
    }
    const colGap=250,rowGap=76,margin=70;
    if(mode==='by-file'){
      const files=Array.from(new Set(nodes.map(n=>n.file))).sort();
      files.forEach((file,col)=>{const arr=nodes.filter(n=>n.file===file).sort(compare);const columns=Math.max(1,Math.ceil(Math.sqrt(arr.length/3)));
        arr.forEach((n,i)=>{if(!pos.has(n.id))pos.set(n.id,{x:margin+col*520+(i%columns)*220,y:margin+Math.floor(i/columns)*rowGap});});
      });
    } else {
      const spine=findMainPath(nodes,forward,roots.length?roots:entryNodes.slice(0,1));
      const spineIndex=new Map(spine.map((id,i)=>[id,i]));
      const parentMap=new Map(nodes.map(n=>[n.id,[]]));
      forward.forEach(e=>{if(parentMap.has(e.to))parentMap.get(e.to).push(e.from);});
      function spineAnchor(id){
        const seen=new Set(),q=[id];
        while(q.length){const current=q.shift();if(spineIndex.has(current))return spineIndex.get(current);if(seen.has(current))continue;seen.add(current);(parentMap.get(current)||[]).forEach(v=>q.push(v));}
        return -1;
      }
      const branchGroups=new Map();
      nodes.forEach(n=>{if(spineIndex.has(n.id))return;const anchor=spineAnchor(n.id);if(!branchGroups.has(anchor))branchGroups.set(anchor,[]);branchGroups.get(anchor).push(n);});
      // Keep the main path on one readable horizontal spine.
      const spineY=margin+Math.max(0,...Array.from(branchGroups.values(),a=>a.length))*rowGap*.35+rowGap;
      spine.forEach((id,i)=>{if(!pos.has(id))pos.set(id,{x:margin+i*colGap,y:spineY});});
      // Branches are clustered near the spine node they originate from.
      const branchByRank=new Map();
      branchGroups.forEach((arr,anchor)=>{
        arr.sort((a,b)=>rank.get(a.id)-rank.get(b.id)||compare(a,b));
        arr.forEach(n=>{
          const r=rank.get(n.id),anchorX=anchor>=0?anchor:0;
          if(!branchByRank.has(r))branchByRank.set(r,[]);
          branchByRank.get(r).push({node:n,anchor:anchorX});
        });
      });
      branchByRank.forEach((arr,r)=>{
        arr.sort((a,b)=>a.anchor-b.anchor||compare(a.node,b.node));
        arr.forEach((item,i)=>{
          if(pos.has(item.node.id))return;
          const side=(i%2===0?1:-1),slot=Math.floor(i/2)+1;
          pos.set(item.node.id,{x:margin+r*colGap,y:spineY+side*slot*rowGap});
        });
      });
      // Disconnected/import-only blocks remain in a compact side area.
      nodes.forEach((n,i)=>{if(!pos.has(n.id))pos.set(n.id,{x:margin+(i%4)*colGap,y:spineY+((i%2)?-1:1)*(Math.floor(i/4)+2)*rowGap});});
    }
    // Place any nodes added after a saved layout without moving existing nodes.
    nodes.forEach((n,i)=>{if(!pos.has(n.id))pos.set(n.id,{x:margin+(i%5)*colGap,y:margin+Math.floor(i/5)*rowGap});});
    const widths=new Map();nodes.forEach(n=>widths.set(n.id,Math.max(30,Math.min(220,n.name.length*7.2))));
    const maxX=Math.max(500,...Array.from(pos.values(),p=>p.x+300)),maxY=Math.max(400,...Array.from(pos.values(),p=>p.y+100));
    return {positions:pos,widths,width:maxX,height:maxY,files:Array.from(new Set(nodes.map(n=>n.file)))};
  }
  function route(a,b,edge,index){
    const dx=b.x-a.x,dy=b.y-a.y;
    if(Math.abs(dx)<28&&Math.abs(dy)<28)return [{x:a.x,y:a.y},{x:a.x+30,y:a.y-24-(index%3)*7},{x:b.x-30,y:b.y-24-(index%3)*7},{x:b.x,y:b.y}];
    const sign=dx>=0?1:-1, sx=a.x+sign*22, ex=b.x-sign*22;
    if(Math.abs(dx)>=Math.abs(dy)*.65){
      const mid=(sx+ex)/2;
      return [{x:a.x+sign*12,y:a.y},{x:sx,y:a.y},{x:mid,y:a.y},{x:mid,y:b.y},{x:ex,y:b.y},{x:b.x-sign*12,y:b.y}];
    }
    const sy=a.y+(dy>=0?14:-14),ey=b.y-(dy>=0?14:-14),mid=(sy+ey)/2;
    return [{x:a.x,y:sy},{x:a.x,y:mid},{x:b.x,y:mid},{x:b.x,y:ey}];
  }
  function drawArrow(ctx, pts, color){
    if(pts.length<2)return;const a=pts[pts.length-2],b=pts[pts.length-1],ang=Math.atan2(b.y-a.y,b.x-a.x);
    ctx.fillStyle=color;ctx.beginPath();ctx.moveTo(b.x,b.y);ctx.lineTo(b.x-9*Math.cos(ang-.48),b.y-9*Math.sin(ang-.48));ctx.lineTo(b.x-9*Math.cos(ang+.48),b.y-9*Math.sin(ang+.48));ctx.closePath();ctx.fill();
  }
  function render(host, model, onNodeClick, onEdgeClick, forceArrange){
    host.innerHTML='';
    const toolbar=html('div','fl-code-map-toolbar');
    const summary=html('span','fl-code-map-summary',model.blocks.length+' blocks'+((model.dynamicNodes||[]).length?' · '+model.dynamicNodes.length+' dynamic targets':'')+' · '+model.edges.length+' transfers · '+model.files.length+' files');
    toolbar.appendChild(summary);
    const controls=html('div','fl-code-map-controls');
    [['graph','Graph'],['by-file','By file']].forEach(([v,label])=>{const b=html('button','fl-code-map-mode'+(displayMode===v?' active':''),label);b.type='button';b.setAttribute('aria-pressed',displayMode===v?'true':'false');b.onclick=()=>{displayMode=v;render(host,model,onNodeClick,onEdgeClick);};controls.appendChild(b);});
    const arrange=html('button','fl-code-map-mode','↻');arrange.title='Arrange graph';arrange.setAttribute('aria-label','Arrange graph');arrange.onclick=()=>{manualPositions.clear();try{root.localStorage.removeItem(key(model));}catch(_){}render(host,model,onNodeClick,onEdgeClick,true);};controls.appendChild(arrange);
    [['−',()=>{zoom=Math.max(.4,zoom-.15);resize();}],['+',()=>{zoom=Math.min(2.5,zoom+.15);resize();}],['Fit',()=>{zoom=1;resize();}]].forEach(([label,fn])=>{const b=html('button','fl-code-map-mode',label);b.type='button';b.onclick=fn;controls.appendChild(b);});
    toolbar.appendChild(controls);
    const legend=html('div','fl-code-map-legend');
    Object.keys(COLORS).forEach(type=>{const item=html('span','fl-code-map-legend-item');const sw=html('i','fl-code-map-swatch');sw.style.backgroundColor=COLORS[type];item.append(sw,document.createTextNode(type));legend.appendChild(item);});
    const hint=html('span','fl-code-map-legend-item');hint.append(html('i','fl-code-map-swatch fl-code-map-swatch-hint'),document.createTextNode('@map hint'));legend.appendChild(hint);
    const dyn=html('span','fl-code-map-legend-item');dyn.append(html('i','fl-code-map-swatch fl-code-map-swatch-dynamic'),document.createTextNode('dynamic target'));legend.appendChild(dyn);toolbar.appendChild(legend);host.appendChild(toolbar);
    const viewport=html('div','fl-code-map-viewport');viewport.style.position='relative';viewport.style.overflow='hidden';viewport.style.touchAction='none';host.appendChild(viewport);
    const canvas=document.createElement('canvas');canvas.className='fl-code-map-canvas';canvas.style.display='block';canvas.style.width='100%';canvas.style.height='100%';canvas.style.touchAction='none';viewport.appendChild(canvas);
    const nodes=model.blocks.concat(model.dynamicNodes||[]);if(!nodes.length){viewport.appendChild(html('div','fl-code-map-empty','No FlatL blocks found in this file or its imported files.'));return;}
    const layout=layoutGraph(nodes,model.edges,model,displayMode,!!forceArrange),positions=layout.positions;
    manualPositions.forEach((p,id)=>{if(positions.has(id))Object.assign(positions.get(id),p);});
    let dpr=1,viewW=800,viewH=600,drag=null,pan=null,hover=null;
    const nodeById=new Map(nodes.map(n=>[n.id,n]));
    const edgePaths=model.edges.map((e,i)=>({edge:e,index:i,pts:null}));
    const bounds=()=>{let x=500,y=400;positions.forEach(p=>{x=Math.max(x,p.x+330);y=Math.max(y,p.y+110);});return {x,y};};
    function resize(){
      const r=viewport.getBoundingClientRect();viewW=Math.max(320,r.width);viewH=Math.max(280,r.height||600);dpr=root.devicePixelRatio||1;
      canvas.width=Math.round(viewW*dpr);canvas.height=Math.round(viewH*dpr);
      const b=bounds();canvas.style.minWidth=Math.max(viewW,b.x*zoom)+'px';canvas.style.minHeight=Math.max(viewH,b.y*zoom)+'px';
      draw();
    }
    function transformPoint(clientX,clientY){const r=canvas.getBoundingClientRect();return {x:(clientX-r.left)/zoom,y:(clientY-r.top)/zoom};}
    function draw(){
      const ctx=canvas.getContext('2d');if(!ctx)return;
      const r=canvas.getBoundingClientRect();ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,canvas.width/dpr,canvas.height/dpr);
      ctx.save();ctx.scale(zoom,zoom);
      // Subtle drafting grid.
      const b=bounds();ctx.fillStyle='rgba(130,145,160,.10)';
      for(let x=20;x<b.x;x+=24)for(let y=20;y<b.y;y+=24){ctx.fillRect(x,y,1,1);}
      // File boundaries in By file mode.
      if(displayMode==='by-file'){
        layout.files.forEach(file=>{const arr=nodes.filter(n=>n.file===file);if(!arr.length)return;const xs=arr.map(n=>positions.get(n.id).x),ys=arr.map(n=>positions.get(n.id).y);
          const x=Math.min(...xs)-35,y=Math.min(...ys)-38,w=Math.max(...xs)-Math.min(...xs)+300,h=Math.max(...ys)-Math.min(...ys)+78;
          ctx.fillStyle='rgba(120,140,160,.045)';ctx.strokeStyle='rgba(130,145,160,.25)';ctx.lineWidth=1;ctx.beginPath();ctx.roundRect?ctx.roundRect(x,y,w,h,9):ctx.rect(x,y,w,h);ctx.fill();ctx.stroke();
          ctx.fillStyle='#91a3ad';ctx.font='11px sans-serif';ctx.fillText(file,x+10,y+17);
        });
      }
      // Edges first, nodes last (same layering principle as mapp-canv).
      edgePaths.forEach(({edge,index},i)=>{
        const a=positions.get(edge.from),b=positions.get(edge.to);if(!a||!b)return;
        const pts=route(a,b,edge,index);edgePaths[i].pts=pts;
        const active=selectedNodeId===edge.from||selectedNodeId===edge.to;
        ctx.save();ctx.strokeStyle=COLORS[edge.type]||'#a1a1aa';ctx.lineWidth=selectedNodeId&&!active?1.2:1.8;ctx.globalAlpha=selectedNodeId&&!active?.18:.82;
        if(edge.resolution==='hint')ctx.setLineDash([6,4]);else if(edge.resolution==='dynamic')ctx.setLineDash([2,4]);
        ctx.beginPath();ctx.moveTo(pts[0].x,pts[0].y);for(let j=1;j<pts.length;j++)ctx.lineTo(pts[j].x,pts[j].y);ctx.stroke();ctx.setLineDash([]);
        if(active){ctx.lineWidth=4;ctx.globalAlpha=.22;ctx.stroke();}
        drawArrow(ctx,pts,COLORS[edge.type]||'#a1a1aa');ctx.restore();
      });
      nodes.forEach(n=>{
        const p=positions.get(n.id);if(!p)return;const dynamic=n.kind==='dynamic',selected=selectedNodeId===n.id,connected=!selectedNodeId||selected||model.edges.some(e=>(e.from===selectedNodeId&&e.to===n.id)||(e.to===selectedNodeId&&e.from===n.id));
        const label=dynamic?'Dynamic target':n.name;ctx.save();ctx.globalAlpha=connected?1:.2;
        ctx.beginPath();ctx.arc(p.x,p.y,10,0,Math.PI*2);ctx.fillStyle='#202631';ctx.fill();ctx.lineWidth=selected?3:1.8;ctx.strokeStyle=selected?'#facc15':dynamic?'#d97706':'#8b8b9c';ctx.setLineDash(dynamic?[3,2]:[]);ctx.stroke();ctx.setLineDash([]);
        ctx.font='600 12px ui-monospace, Menlo, Consolas, monospace';ctx.fillStyle='#e4e4e7';ctx.textBaseline='middle';ctx.fillText(label,p.x+18,p.y);
        ctx.restore();
      });
      ctx.restore();
    }
    function resizeCanvas(){resize();}
    function findNode(pt){let best=null,dist=Infinity;nodes.forEach(n=>{const p=positions.get(n.id),d=Math.hypot(pt.x-p.x,pt.y-p.y);if(d<16&&d<dist){best=n;dist=d;}});return best;}
    function findEdge(pt){let best=null,bestD=9;edgePaths.forEach(({edge,pts})=>{if(!pts)return;for(let i=1;i<pts.length;i++){const a=pts[i-1],b=pts[i],dx=b.x-a.x,dy=b.y-a.y,t=Math.max(0,Math.min(1,((pt.x-a.x)*dx+(pt.y-a.y)*dy)/(dx*dx+dy*dy||1))),d=Math.hypot(pt.x-(a.x+t*dx),pt.y-(a.y+t*dy));if(d<bestD){bestD=d;best=edge;}}});return best;}
    canvas.addEventListener('pointerdown',ev=>{
      if(ev.button!==0)return;const pt=transformPoint(ev.clientX,ev.clientY),node=findNode(pt);
      if(node){const p=positions.get(node.id);drag={id:node.id,start:pt,origin:{x:p.x,y:p.y},moved:false};canvas.setPointerCapture(ev.pointerId);}
      else{pan={x:ev.clientX,y:ev.clientY,left:viewport.scrollLeft,top:viewport.scrollTop};canvas.setPointerCapture(ev.pointerId);}
      ev.preventDefault();
    });
    canvas.addEventListener('pointermove',ev=>{
      const pt=transformPoint(ev.clientX,ev.clientY);
      if(drag){const p=positions.get(drag.id);p.x=drag.origin.x+(pt.x-drag.start.x);p.y=drag.origin.y+(pt.y-drag.start.y);drag.moved=true;manualPositions.set(drag.id,{x:p.x,y:p.y});draw();}
      else if(pan){viewport.scrollLeft=pan.left-(ev.clientX-pan.x);viewport.scrollTop=pan.top-(ev.clientY-pan.y);}
      else{hover=findNode(pt);canvas.style.cursor=hover?'grab':'default';}
    });
    function finish(ev){
      if(drag){if(drag.moved){savePositions(model,positions);draw();}else{const n=nodeById.get(drag.id);selectedNodeId=n.id;draw();if(onNodeClick)onNodeClick(n);}drag=null;}
      else if(!pan){const pt=transformPoint(ev.clientX,ev.clientY),edge=findEdge(pt);if(edge&&onEdgeClick)onEdgeClick(edge);}
      pan=null;
    }
    canvas.addEventListener('pointerup',finish);canvas.addEventListener('pointercancel',()=>{drag=null;pan=null;});
    canvas.addEventListener('dblclick',ev=>{const n=findNode(transformPoint(ev.clientX,ev.clientY));if(n&&onNodeClick)onNodeClick(n);});
    activeView={resize:resizeCanvas,draw};resize();
    if(typeof ResizeObserver!=='undefined'){const ro=new ResizeObserver(resize);ro.observe(viewport);}
  }
  root.FleditorCodeMap={render:render};
}(typeof window!=='undefined'?window:this));
