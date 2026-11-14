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
    if(mode!=='by-file' && force) optimizeLayout(nodes,edges,pos);
    const widths=new Map();nodes.forEach(n=>widths.set(n.id,Math.max(30,Math.min(220,n.name.length*7.2))));
    const maxX=Math.max(500,...Array.from(pos.values(),p=>p.x+300)),maxY=Math.max(400,...Array.from(pos.values(),p=>p.y+100));
    return {positions:pos,widths,width:maxX,height:maxY,files:Array.from(new Set(nodes.map(n=>n.file)))};
  }

  const PORT_ANGLES=[-90,-60,-30,0,30,60,90,120,150,180,210,240];
  function choosePorts(a,b,edge,index,edges,positions){
    const angle=Math.atan2(b.y-a.y,b.x-a.x);
    const sourceOrder=PORT_ANGLES.map((deg,i)=>({i,delta:Math.abs(Math.atan2(Math.sin(deg*Math.PI/180-angle),Math.cos(deg*Math.PI/180-angle)))})).sort((x,y)=>x.delta-y.delta);
    const reverse=angle+Math.PI;
    const targetOrder=PORT_ANGLES.map((deg,i)=>({i,delta:Math.abs(Math.atan2(Math.sin(deg*Math.PI/180-reverse),Math.cos(deg*Math.PI/180-reverse)))})).sort((x,y)=>x.delta-y.delta);
    const usedOut=new Map(),usedIn=new Map();
    edges.forEach((e,j)=>{if(j>=index)return;if(e.from===edge.from)usedOut.set(e._portOut,(usedOut.get(e._portOut)||0)+1);if(e.to===edge.to)usedIn.set(e._portIn,(usedIn.get(e._portIn)||0)+1);});
    const out=sourceOrder.find(x=>!usedOut.has(x.i))||sourceOrder[0],inn=targetOrder.find(x=>!usedIn.has(x.i))||targetOrder[0];
    edge._portOut=out.i;edge._portIn=inn.i;
    const point=(p,i)=>{const r=11,rad=PORT_ANGLES[i]*Math.PI/180;return {x:p.x+Math.cos(rad)*r,y:p.y+Math.sin(rad)*r};};
    return [point(a,out.i),point(b,inn.i)];
  }
  function route(a,b,edge,index,edges){
    const [s,e]=choosePorts(a,b,edge,index,edges);
    const dx=e.x-s.x,dy=e.y-s.y;
    if(Math.hypot(dx,dy)<22)return [s,e];
    // Prefer a compact 45-degree dogleg; use a short lead from each port.
    const sx=s.x+Math.sign(dx||1)*5,sy=s.y+Math.sign(dy||1)*5;
    const ex=e.x-Math.sign(dx||1)*5,ey=e.y-Math.sign(dy||1)*5;
    const adx=ex-sx,ady=ey-sy,diag=Math.min(Math.abs(adx),Math.abs(ady));
    if(diag<10)return [s,{x:sx,y:sy},{x:ex,y:ey},e];
    const signX=Math.sign(adx),signY=Math.sign(ady);
    const bend1={x:sx+signX*diag,y:sy+signY*diag};
    const bend2={x:ex-signX*diag,y:ey-signY*diag};
    const pts=[s,{x:sx,y:sy},bend1];
    if(Math.hypot(bend2.x-bend1.x,bend2.y-bend1.y)>8)pts.push(bend2);
    pts.push({x:ex,y:ey},e);
    return pts;
  }
  function segmentCross(a,b,c,d){
    const cross=(p,q,r)=>(q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x);
    const x1=cross(a,b,c),x2=cross(a,b,d),x3=cross(c,d,a),x4=cross(c,d,b);
    return x1*x2<-.01&&x3*x4<-.01;
  }
  function layoutCost(nodes,edges,pos){
    let cost=0;
    const ids=new Map(nodes.map(n=>[n.id,n]));
    const paths=edges.map(e=>{const a=pos.get(e.from),b=pos.get(e.to);return a&&b?route(a,b,{...e},0,[]):null;});
    paths.forEach((p,i)=>{if(!p)return;for(let j=1;j<p.length;j++)cost+=Math.hypot(p[j].x-p[j-1].x,p[j].y-p[j-1].y)*.012;});
    for(let i=0;i<paths.length;i++)if(paths[i])for(let j=i+1;j<paths.length;j++)if(paths[j]){
      const a=paths[i],b=paths[j];let crossings=0;
      for(let x=1;x<a.length;x++)for(let y=1;y<b.length;y++)if(segmentCross(a[x-1],a[x],b[y-1],b[y]))crossings++;
      cost+=crossings*95;
    }
    for(let i=0;i<nodes.length;i++)for(let j=i+1;j<nodes.length;j++){
      const a=pos.get(nodes[i].id),b=pos.get(nodes[j].id);if(!a||!b)continue;
      const dx=Math.abs(a.x-b.x),dy=Math.abs(a.y-b.y);
      const minX=Math.max(44,Math.min(190,(nodes[i].name||'').length*3.8))+Math.max(44,Math.min(190,(nodes[j].name||'').length*3.8));
      if(dx<minX&&dy<30)cost+=(minX-dx)*2+(30-dy)*3;
    }
    return cost;
  }
  function optimizeLayout(nodes,edges,positions){
    const movable=nodes.filter(n=>positions.has(n.id));
    if(movable.length<3)return;
    // Coordinate descent on a coarse grid: cheap for editor-sized graphs and deterministic.
    let best=layoutCost(nodes,edges,positions),step=36;
    for(let pass=0;pass<5;pass++){
      let changed=false;
      for(const n of movable){
        const p=positions.get(n.id),origin={x:p.x,y:p.y},candidates=[
          [0,0],[-step,0],[step,0],[0,-step],[0,step],[-step,-step],[step,-step],[-step,step],[step,step],
          [-step*2,0],[step*2,0],[0,-step*2],[0,step*2]
        ];
        let local=best,choice=origin;
        candidates.forEach(([dx,dy])=>{p.x=origin.x+dx;p.y=origin.y+dy;const score=layoutCost(nodes,edges,positions);if(score<local-.01){local=score;choice={x:p.x,y:p.y};}});
        p.x=choice.x;p.y=choice.y;if(local<best-.01){best=local;changed=true;}
      }
      step=Math.max(8,step*.62);
      if(!changed&&pass>1)break;
    }
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
    [['−',()=>{zoomAt(zoom/1.2);}],['+',()=>{zoomAt(zoom*1.2);}],['Fit',()=>{const b=bounds();zoom=Math.max(.25,Math.min(1.5,Math.min((viewW-32)/b.x,(viewH-32)/b.y)));setCanvasSize();draw();viewport.scrollLeft=0;viewport.scrollTop=0;}]].forEach(([label,fn])=>{const b=html('button','fl-code-map-mode',label);b.type='button';b.onclick=fn;controls.appendChild(b);});
    toolbar.appendChild(controls);
    const legend=html('div','fl-code-map-legend');
    Object.keys(COLORS).forEach(type=>{const item=html('span','fl-code-map-legend-item');const sw=html('i','fl-code-map-swatch');sw.style.backgroundColor=COLORS[type];item.append(sw,document.createTextNode(type));legend.appendChild(item);});
    const hint=html('span','fl-code-map-legend-item');hint.append(html('i','fl-code-map-swatch fl-code-map-swatch-hint'),document.createTextNode('@map hint'));legend.appendChild(hint);
    const dyn=html('span','fl-code-map-legend-item');dyn.append(html('i','fl-code-map-swatch fl-code-map-swatch-dynamic'),document.createTextNode('dynamic target'));legend.appendChild(dyn);toolbar.appendChild(legend);host.appendChild(toolbar);
    const viewport=html('div','fl-code-map-viewport');viewport.style.position='relative';viewport.style.overflow='auto';viewport.style.touchAction='pan-x pan-y';host.appendChild(viewport);
    const canvas=document.createElement('canvas');canvas.className='fl-code-map-canvas';canvas.style.display='block';canvas.style.touchAction='none';canvas.style.maxWidth='none';viewport.appendChild(canvas);
    const nodes=model.blocks.concat(model.dynamicNodes||[]);if(!nodes.length){viewport.appendChild(html('div','fl-code-map-empty','No FlatL blocks found in this file or its imported files.'));return;}
    const layout=layoutGraph(nodes,model.edges,model,displayMode,!!forceArrange),positions=layout.positions;
    manualPositions.forEach((p,id)=>{if(positions.has(id))Object.assign(positions.get(id),p);});
    let dpr=1,viewW=800,viewH=600,drag=null,pan=null,hover=null;
    const nodeById=new Map(nodes.map(n=>[n.id,n]));
    const edgePaths=model.edges.map((e,i)=>({edge:e,index:i,pts:null}));
    const bounds=()=>{let x=500,y=400;positions.forEach(p=>{x=Math.max(x,p.x+Math.max(260,(nodeById.get([...positions.keys()].find(id=>positions.get(id)===p))||{}).name?.length*7||0)+80);y=Math.max(y,p.y+100);});return {x,y};};
    function setCanvasSize(){
      const b=bounds(),cssW=Math.max(viewW,b.x*zoom),cssH=Math.max(viewH,b.y*zoom);
      dpr=Math.max(1,root.devicePixelRatio||1);
      canvas.style.width=cssW+'px';canvas.style.height=cssH+'px';
      canvas.width=Math.ceil(cssW*dpr);canvas.height=Math.ceil(cssH*dpr);
    }
    function resize(){
      const r=viewport.getBoundingClientRect();viewW=Math.max(240,r.width);viewH=Math.max(220,r.height);
      setCanvasSize();draw();
    }
    function transformPoint(clientX,clientY){const r=canvas.getBoundingClientRect();return {x:(clientX-r.left)/zoom,y:(clientY-r.top)/zoom};}
    function draw(){
      const ctx=canvas.getContext('2d');if(!ctx)return;
      ctx.setTransform(dpr*zoom,0,0,dpr*zoom,0,0);ctx.clearRect(0,0,canvas.width/(dpr*zoom),canvas.height/(dpr*zoom));
      ctx.save();
      const b=bounds();ctx.fillStyle='rgba(130,145,160,.10)';
      for(let x=20;x<b.x;x+=24)for(let y=20;y<b.y;y+=24){ctx.fillRect(x,y,1,1);}
      if(displayMode==='by-file'){
        layout.files.forEach(file=>{const arr=nodes.filter(n=>n.file===file);if(!arr.length)return;const xs=arr.map(n=>positions.get(n.id).x),ys=arr.map(n=>positions.get(n.id).y);
          const x=Math.min(...xs)-35,y=Math.min(...ys)-38,w=Math.max(...xs)-Math.min(...xs)+300,h=Math.max(...ys)-Math.min(...ys)+78;
          ctx.fillStyle='rgba(120,140,160,.045)';ctx.strokeStyle='rgba(130,145,160,.25)';ctx.lineWidth=1;ctx.beginPath();ctx.roundRect?ctx.roundRect(x,y,w,h,9):ctx.rect(x,y,w,h);ctx.fill();ctx.stroke();
          ctx.fillStyle='#91a3ad';ctx.font='11px sans-serif';ctx.fillText(file,x+10,y+17);
        });
      }
      edgePaths.forEach(({edge,index},i)=>{
        const a=positions.get(edge.from),b=positions.get(edge.to);if(!a||!b)return;
        const pts=route(a,b,edge,index,model.edges);edgePaths[i].pts=pts;
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
        ctx.font='600 12px ui-monospace, Menlo, Consolas, monospace';ctx.fillStyle='#e4e4e7';ctx.textBaseline='middle';ctx.fillText(label,p.x+18,p.y+2);
        ctx.restore();
      });
      ctx.restore();
    }
    function resizeCanvas(){resize();}
    function findNode(pt){
      let best=null,dist=Infinity;
      nodes.forEach(n=>{const p=positions.get(n.id),d=Math.hypot(pt.x-p.x,pt.y-p.y),label=(n.kind==='dynamic'?'Dynamic target':n.name)||'';
        const inLabel=pt.x>=p.x+12&&pt.x<=p.x+24+label.length*7.5&&Math.abs(pt.y-p.y)<=13;
        if((d<19||inLabel)&&d<dist){best=n;dist=d;}
      });return best;
    }
    function findEdge(pt){let best=null,bestD=9;edgePaths.forEach(({edge,pts})=>{if(!pts)return;for(let i=1;i<pts.length;i++){const a=pts[i-1],b=pts[i],dx=b.x-a.x,dy=b.y-a.y,t=Math.max(0,Math.min(1,((pt.x-a.x)*dx+(pt.y-a.y)*dy)/(dx*dx+dy*dy||1))),d=Math.hypot(pt.x-(a.x+t*dx),pt.y-(a.y+t*dy));if(d<bestD){bestD=d;best=edge;}}});return best;}
    canvas.addEventListener('pointerdown',ev=>{
      if(ev.button!==0&&ev.pointerType!=='touch')return;
      const pt=transformPoint(ev.clientX,ev.clientY),node=findNode(pt);
      if(node){const p=positions.get(node.id);drag={id:node.id,start:pt,origin:{x:p.x,y:p.y},moved:false,pointerId:ev.pointerId};canvas.setPointerCapture(ev.pointerId);canvas.style.cursor='grabbing';}
      else{pan={x:ev.clientX,y:ev.clientY,left:viewport.scrollLeft,top:viewport.scrollTop,pointerId:ev.pointerId};canvas.setPointerCapture(ev.pointerId);}
      ev.preventDefault();
    });
    canvas.addEventListener('pointermove',ev=>{
      const pt=transformPoint(ev.clientX,ev.clientY);
      if(drag){const p=positions.get(drag.id),dx=pt.x-drag.start.x,dy=pt.y-drag.start.y;if(Math.abs(dx)+Math.abs(dy)>2)drag.moved=true;p.x=drag.origin.x+dx;p.y=drag.origin.y+dy;manualPositions.set(drag.id,{x:p.x,y:p.y});setCanvasSize();draw();}
      else if(pan){viewport.scrollLeft=pan.left-(ev.clientX-pan.x);viewport.scrollTop=pan.top-(ev.clientY-pan.y);}
      else{hover=findNode(pt);canvas.style.cursor=hover?'grab':'default';}
    });
    function finish(ev){
      if(drag){const current=drag;if(current.moved){savePositions(model,positions);setCanvasSize();draw();}else{const n=nodeById.get(current.id);selectedNodeId=n.id;draw();if(onNodeClick)onNodeClick(n);}drag=null;canvas.style.cursor='default';}
      else if(!pan){const pt=transformPoint(ev.clientX,ev.clientY),edge=findEdge(pt);if(edge&&onEdgeClick)onEdgeClick(edge);}
      pan=null;
    }
    canvas.addEventListener('pointerup',finish);canvas.addEventListener('pointercancel',()=>{drag=null;pan=null;canvas.style.cursor='default';});
    canvas.addEventListener('dblclick',ev=>{const n=findNode(transformPoint(ev.clientX,ev.clientY));if(n&&onNodeClick)onNodeClick(n);});
    function zoomAt(nextZoom){
      const old=zoom,cx=viewport.scrollLeft+viewW/2,cy=viewport.scrollTop+viewH/2;
      zoom=Math.max(.25,Math.min(2.5,nextZoom));setCanvasSize();draw();
      viewport.scrollLeft=(cx/old)*zoom-viewW/2;viewport.scrollTop=(cy/old)*zoom-viewH/2;
    }
    activeView={resize:resizeCanvas,draw};resize();
    if(typeof ResizeObserver!=='undefined'){const ro=new ResizeObserver(resize);ro.observe(viewport);}
  }
  root.FleditorCodeMap={render:render};
}(typeof window!=='undefined'?window:this));
