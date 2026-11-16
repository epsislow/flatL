/* global FleditorCodeMap */
(function (root) {
  'use strict';

  const COLORS = { jump:'#60a5fa', call:'#4ade80', or:'#facc15', panic:'#f87171', for:'#c084fc', foreach:'#c084fc' };
  let displayMode='graph', zoom=1, selectedNodeId=null, manualPositions=new Map();
  let activeView=null, activeOrientation='horizontal';

  function setSelectedNode(id, focus){
    selectedNodeId=id||null;
    if(activeView&&typeof activeView.draw==='function'){
      activeView.draw();
      if(focus&&selectedNodeId&&typeof activeView.focus==='function')activeView.focus(selectedNodeId);
    }
  }

  function html(name, cls, text) { const e=document.createElement(name); if(cls)e.className=cls; if(text!=null)e.textContent=text; return e; }
  function key(model){return 'fleditor-code-map:v3:'+ (model.entryPath||'project');}
  function orientationKey(model){return key(model)+':orientation';}
  function loadOrientation(model){try{return root.localStorage.getItem(orientationKey(model))==='vertical'?'vertical':'horizontal';}catch(_){return 'horizontal';}}
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
    // Branches and the auto-arrange pass can place nodes above or left of the
    // origin. The canvas cannot scroll below zero, so pull the whole graph in.
    containPositions(pos,function(_id,p){return {left:p.x-NODE_R,top:p.y-NODE_R};});
    const widths=new Map();nodes.forEach(n=>widths.set(n.id,Math.max(30,Math.min(220,n.name.length*7.2))));
    const maxX=Math.max(500,...Array.from(pos.values(),p=>p.x+300)),maxY=Math.max(400,...Array.from(pos.values(),p=>p.y+100));
    return {positions:pos,widths,width:maxX,height:maxY,files:Array.from(new Set(nodes.map(n=>n.file)))};
  }

  const PORT_ANGLES=[-90,-60,-30,0,30,60,90,120,150,180,210,240];
  const PORT_GAP=20,NODE_R=10,PORT_R=11,CANVAS_PAD=48;
  let nodeRadius=new Map();
  function containPositions(pos,edgeOf){
    let minL=Infinity,minT=Infinity;
    pos.forEach((p,id)=>{const e=edgeOf(id,p);minL=Math.min(minL,e.left);minT=Math.min(minT,e.top);});
    const dx=Number.isFinite(minL)&&minL<CANVAS_PAD?CANVAS_PAD-minL:0;
    const dy=Number.isFinite(minT)&&minT<CANVAS_PAD?CANVAS_PAD-minT:0;
    if(dx||dy)pos.forEach(p=>{p.x+=dx;p.y+=dy;});
    return {dx:dx,dy:dy};
  }
  function portDelta(deg,target){return Math.abs(Math.atan2(Math.sin(deg*Math.PI/180-target),Math.cos(deg*Math.PI/180-target)));}
  function ringFor(count){
    if(count<=PORT_ANGLES.length)return {node:NODE_R,port:PORT_R};
    // Match the chord of the 12-point ring (30° on the standard radius), not the
    // tighter 20° pack. The circle grows as soon as a 13th end is added.
    const gap=Math.min(PORT_GAP*Math.PI/180,2*Math.PI/count);
    const scale=Math.sin(Math.PI/12)/Math.sin(gap/2);
    return {node:NODE_R*scale,port:PORT_R*scale};
  }
  function portPoint(p,deg,radius){const r=radius||PORT_R,rad=deg*Math.PI/180;return {x:p.x+Math.cos(rad)*r,y:p.y+Math.sin(rad)*r};}
  function nearestPort(target){let best=0,delta=Infinity;PORT_ANGLES.forEach((deg,i)=>{const d=portDelta(deg,target);if(d<delta){delta=d;best=i;}});return best;}
  function normRad(a){const t=Math.PI*2;a%=t;return a<0?a+t:a;}
  function toDeg(rad){let deg=normRad(rad)*180/Math.PI;if(deg>180)deg-=360;return deg;}
  // One shared ring per node, for both incoming and outgoing ends.
  // Up to 12 ends use the fixed 30° points on the standard circle. Past that,
  // the circle grows so neighboring lines stay as far apart as on that 12-point ring.
  function assignGrid(stubs){
    const used=new Set(),pending=stubs.map((stub,i)=>({stub,i})),degs=new Array(stubs.length);
    while(pending.length){
      let best=null;
      pending.forEach((item,pi)=>{
        PORT_ANGLES.forEach((deg,p)=>{
          if(used.has(p))return;
          const delta=portDelta(deg,item.stub.angle);
          const closer=!best||delta<best.delta-1e-9||(Math.abs(delta-best.delta)<=1e-9&&(item.stub.index<best.item.stub.index||(item.stub.index===best.item.stub.index&&item.stub.side<best.item.stub.side)||(item.stub.index===best.item.stub.index&&item.stub.side===best.item.stub.side&&p<best.p)));
          if(closer)best={delta,item,pi,p};
        });
      });
      used.add(best.p);degs[best.item.i]=PORT_ANGLES[best.p];pending.splice(best.pi,1);
    }
    return degs;
  }
  function assignDense(stubs){
    const n=stubs.length,full=Math.PI*2,minGap=Math.min(PORT_GAP*Math.PI/180,full/n);
    const order=stubs.map((stub,i)=>({i,a:normRad(stub.angle)})).sort((p,q)=>p.a-q.a||p.i-q.i);
    const want=order.map(o=>o.a);
    let x=want.slice();
    for(let iter=0;iter<80;iter++){
      let changed=false;
      for(let i=0;i<n;i++){
        const j=(i+1)%n,gap=(j===0?x[0]+full:x[j])-x[i];
        if(gap<minGap-1e-8){const push=(minGap-gap)/2;x[i]-=push;x[j]+=push;changed=true;}
      }
      let shift=0;
      for(let i=0;i<n;i++){let d=x[i]-want[i];while(d>Math.PI)d-=full;while(d<-Math.PI)d+=full;shift+=d;}
      shift/=n;
      if(Math.abs(shift)>1e-8){for(let i=0;i<n;i++)x[i]-=shift;changed=true;}
      if(!changed)break;
    }
    let u=x.map(normRad);
    for(let i=1;i<n;i++)if(u[i]<u[i-1]-1e-6)u[i]+=full;
    for(let i=1;i<n;i++)u[i]=Math.max(u[i],u[i-1]+minGap);
    if(u[n-1]>u[0]+full-minGap+1e-5){
      const gap=full/n;let sx=0,sy=0;order.forEach(o=>{sx+=Math.cos(o.a);sy+=Math.sin(o.a);});
      const start=Math.atan2(sy,sx)-(n-1)*gap/2;
      for(let i=0;i<n;i++)u[i]=start+i*gap;
    }
    const degs=new Array(n);
    order.forEach((o,k)=>{degs[o.i]=toDeg(u[k]);});
    return degs;
  }
  function assignPorts(edges,positions){
    edges.forEach(e=>{delete e._portOut;delete e._portIn;delete e._rOut;delete e._rIn;});
    const groups=new Map(),radii=new Map();
    function add(id,edge,side,angle,index){if(!groups.has(id))groups.set(id,[]);groups.get(id).push({edge,side,angle,index});}
    edges.forEach((edge,index)=>{
      const from=positions.get(edge.from),to=positions.get(edge.to);if(!from||!to)return;
      const outAngle=Math.atan2(to.y-from.y,to.x-from.x);
      let inAngle=Math.atan2(from.y-to.y,from.x-to.x);
      if(edge.from===edge.to)inAngle+=Math.PI;
      add(edge.from,edge,'out',outAngle,index);
      add(edge.to,edge,'in',inAngle,index);
    });
    groups.forEach((stubs,id)=>{
      const ring=ringFor(stubs.length),degs=stubs.length<=PORT_ANGLES.length?assignGrid(stubs):assignDense(stubs);
      radii.set(id,ring);
      stubs.forEach((stub,i)=>{
        if(stub.side==='out'){stub.edge._portOut=degs[i];stub.edge._rOut=ring.port;}
        else{stub.edge._portIn=degs[i];stub.edge._rIn=ring.port;}
      });
    });
    nodeRadius=radii;
  }
  function choosePorts(a,b,edge){
    const angle=Math.atan2(b.y-a.y,b.x-a.x);
    if(typeof edge._portOut!=='number')edge._portOut=PORT_ANGLES[nearestPort(angle)];
    if(typeof edge._portIn!=='number')edge._portIn=PORT_ANGLES[nearestPort(angle+Math.PI)];
    return [portPoint(a,edge._portOut,edge._rOut),portPoint(b,edge._portIn,edge._rIn)];
  }
  function route(a,b,edge){
    const [s,e]=choosePorts(a,b,edge);
    const dx=e.x-s.x,dy=e.y-s.y;
    if(edge.from===edge.to){
      const side=Math.cos(edge._portOut*Math.PI/180)>=0?1:-1,reach=Math.max(34,(edge._rOut||PORT_R)+24);
      return [s,{x:s.x+side*reach,y:s.y-reach},{x:e.x-side*reach,y:e.y-reach},e];
    }
    if(Math.hypot(dx,dy)<22)return [s,e];
    // At most one 45-degree segment followed by one straight segment.
    // This removes the multi-bend Z routes produced by the previous router.
    const diagonal=Math.min(Math.abs(dx),Math.abs(dy));
    if(diagonal<8)return [s,e];
    const bend={x:s.x+Math.sign(dx)*diagonal,y:s.y+Math.sign(dy)*diagonal};
    if(Math.hypot(e.x-bend.x,e.y-bend.y)<3)return [s,e];
    return [s,bend,e];
  }
  function segmentCross(a,b,c,d){
    const cross=(p,q,r)=>(q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x);
    const x1=cross(a,b,c),x2=cross(a,b,d),x3=cross(c,d,a),x4=cross(c,d,b);
    return x1*x2<-.01&&x3*x4<-.01;
  }
  function layoutCost(nodes,edges,pos){
    let cost=0;
    const ids=new Map(nodes.map(n=>[n.id,n]));
    const paths=edges.map(e=>{const a=pos.get(e.from),b=pos.get(e.to);return a&&b?route(a,b,Object.assign({},e,{_portOut:undefined,_portIn:undefined})):null;});
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
  function defaultFocusId(nodes, model){
    const entry=model.entryPath||(model.files[0]&&model.files[0].path);
    const entryBlocks=nodes.filter(n=>n.file===entry&&n.kind!=='dynamic');
    const main=entryBlocks.find(n=>String(n.name).replace(/^#/,'').toLowerCase()==='main');
    if(main)return main.id;
    const incoming=new Map(nodes.map(n=>[n.id,0]));
    (model.edges||[]).forEach(e=>{if(incoming.has(e.to))incoming.set(e.to,incoming.get(e.to)+1);});
    const roots=entryBlocks.filter(n=>!incoming.get(n.id)).sort(compare);
    if(roots.length)return roots[0].id;
    if(entryBlocks.length)return entryBlocks.sort(compare)[0].id;
    const any=nodes.filter(n=>n.kind!=='dynamic').sort(compare);
    return any.length?any[0].id:(nodes[0]&&nodes[0].id)||null;
  }
  function render(host, model, onNodeClick, onEdgeClick, forceArrange,centerView){
    const hadSavedLayout=!forceArrange&&!!loadPositions(model);
    const shouldCenter=!!centerView||!hadSavedLayout;
    host.innerHTML='';
    const toolbar=html('div','fl-code-map-toolbar');
    const summary=html('span','fl-code-map-summary',model.blocks.length+' blocks'+((model.dynamicNodes||[]).length?' · '+model.dynamicNodes.length+' dynamic targets':'')+' · '+model.edges.length+' transfers · '+model.files.length+' files');
    toolbar.appendChild(summary);
    const controls=html('div','fl-code-map-controls');
    activeOrientation=loadOrientation(model);
    [['graph','Graph'],['by-file','By file']].forEach(([v,label])=>{const b=html('button','fl-code-map-mode'+(displayMode===v?' active':''),label);b.type='button';b.title=label;b.setAttribute('aria-label',label);b.setAttribute('aria-pressed',displayMode===v?'true':'false');b.onclick=()=>{displayMode=v;render(host,model,onNodeClick,onEdgeClick);};controls.appendChild(b);});
    const arrange=html('button','fl-code-map-mode','↻');arrange.type='button';arrange.title='Arrange graph';arrange.setAttribute('aria-label','Arrange graph');arrange.onclick=()=>{manualPositions.clear();try{root.localStorage.removeItem(key(model));}catch(_){}render(host,model,onNodeClick,onEdgeClick,true);};controls.appendChild(arrange);
    const clearPos=html('button','fl-code-map-mode','🗑');clearPos.type='button';clearPos.title='Clear saved positions (first layout)';clearPos.setAttribute('aria-label','Clear saved positions');clearPos.onclick=()=>{manualPositions.clear();try{root.localStorage.removeItem(key(model));}catch(_){}render(host,model,onNodeClick,onEdgeClick,false,true);};controls.appendChild(clearPos);
    const rotate=html('button','fl-code-map-mode','⤾');rotate.type='button';rotate.title='Rotate graph 90°';rotate.setAttribute('aria-label','Rotate graph 90 degrees');rotate.setAttribute('aria-pressed',activeOrientation==='vertical'?'true':'false');rotate.onclick=()=>{activeOrientation=activeOrientation==='horizontal'?'vertical':'horizontal';try{root.localStorage.setItem(orientationKey(model),activeOrientation);}catch(_){}render(host,model,onNodeClick,onEdgeClick);if(selectedNodeId&&activeView)activeView.focus(selectedNodeId);};controls.appendChild(rotate);
    const focus=html('button','fl-code-map-mode','⌖');focus.type='button';focus.title='Focus selected block';focus.setAttribute('aria-label','Focus selected block');focus.onclick=()=>{if(activeView&&selectedNodeId)activeView.focus(selectedNodeId);};controls.appendChild(focus);
    [['−',()=>{zoomAt(zoom/1.2);}],['+',()=>{zoomAt(zoom*1.2);}],['Fit',()=>{const b=bounds();const w=activeOrientation==='vertical'?b.y:b.x,h=activeOrientation==='vertical'?b.x:b.y;zoom=Math.max(.25,Math.min(1.5,Math.min((viewW-32)/w,(viewH-32)/h)));setCanvasSize();draw();viewport.scrollLeft=0;viewport.scrollTop=0;}]].forEach(([label,fn])=>{const b=html('button','fl-code-map-mode',label);b.type='button';b.title=label==='Fit'?'Fit graph':'Zoom '+(label==='+'?'in':'out');b.setAttribute('aria-label',b.title);b.onclick=fn;controls.appendChild(b);});
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
    function nodeBox(id,p){
      const ring=(nodeRadius.get(id)||{node:NODE_R}).node,node=nodeById.get(id);
      const label=!node?'':(node.kind==='dynamic'?'Dynamic target':(node.name||''));
      const text=Math.max(28,label.length*7.4),labelAt=ring+8;
      if(activeOrientation==='vertical')return {left:p.x-ring,right:p.x+ring,top:p.y-ring,bottom:p.y+text};
      return {left:p.x-ring,right:p.x+labelAt+text,top:p.y-ring,bottom:p.y+ring};
    }
    const bounds=()=>{let x=500,y=400;positions.forEach((p,id)=>{const box=nodeBox(id,p);x=Math.max(x,box.right+CANVAS_PAD);y=Math.max(y,box.bottom+CANVAS_PAD);});return {x,y};};
    function containLive(){
      assignPorts(model.edges,positions);
      const shift=containPositions(positions,nodeBox);
      if(!shift.dx&&!shift.dy)return;
      manualPositions.forEach((mp,id)=>{if(positions.has(id)){mp.x+=shift.dx;mp.y+=shift.dy;}});
      // While dragging, scroll by the same shift so the node stays under the cursor
      // and the canvas grows instead of letting the node leave the scrollable area.
      if(drag){viewport.scrollLeft+=shift.dx*zoom;viewport.scrollTop+=shift.dy*zoom;}
    }
    function setCanvasSize(){
      containLive();
      const b=bounds(),worldW=activeOrientation==='vertical'?b.y:b.x,worldH=activeOrientation==='vertical'?b.x:b.y,cssW=Math.max(viewW,worldW*zoom),cssH=Math.max(viewH,worldH*zoom);
      dpr=Math.max(1,root.devicePixelRatio||1);
      canvas.style.width=cssW+'px';canvas.style.height=cssH+'px';
      canvas.width=Math.ceil(cssW*dpr);canvas.height=Math.ceil(cssH*dpr);
    }
    function resize(){
      const r=viewport.getBoundingClientRect();viewW=Math.max(240,r.width);viewH=Math.max(220,r.height);
      setCanvasSize();draw();
      if(pendingCenter&&viewW>80&&viewH>80){pendingCenter=false;centerOnDefault();}
    }
    function transformPoint(clientX,clientY){const r=canvas.getBoundingClientRect(),x=(clientX-r.left)/zoom,y=(clientY-r.top)/zoom;if(activeOrientation==='vertical'){const b=bounds();return {x:b.x-y,y:x};}return {x,y};}
    function displayPoint(p){if(activeOrientation==='vertical'){const b=bounds();return {x:p.y,y:b.x-p.x};}return {x:p.x,y:p.y};}
    function focusNode(id){const p=positions.get(id);if(!p)return;const q=displayPoint(p),left=q.x*zoom-viewW/2,top=q.y*zoom-viewH/2,maxL=Math.max(0,canvas.clientWidth-viewW),maxT=Math.max(0,canvas.clientHeight-viewH);viewport.scrollLeft=Math.min(maxL,Math.max(0,left));viewport.scrollTop=Math.min(maxT,Math.max(0,top));}
    function centerOnDefault(){const id=defaultFocusId(nodes,model);if(!id)return;focusNode(id);}
    let pendingCenter=shouldCenter;
    function draw(){
      const ctx=canvas.getContext('2d');if(!ctx)return;
      ctx.setTransform(dpr*zoom,0,0,dpr*zoom,0,0);ctx.clearRect(0,0,canvas.width/(dpr*zoom),canvas.height/(dpr*zoom));
      if(activeOrientation==='vertical'){const b=bounds();ctx.setTransform(0,-dpr*zoom,dpr*zoom,0,0,b.x*dpr*zoom);}else{ctx.setTransform(dpr*zoom,0,0,dpr*zoom,0,0);}
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
      assignPorts(model.edges,positions);
      edgePaths.forEach(({edge},i)=>{
        const a=positions.get(edge.from),b=positions.get(edge.to);if(!a||!b)return;
        const pts=route(a,b,edge);edgePaths[i].pts=pts;
        const active=selectedNodeId===edge.from||selectedNodeId===edge.to;
        ctx.save();ctx.strokeStyle=COLORS[edge.type]||'#a1a1aa';ctx.lineWidth=selectedNodeId&&!active?1.2:1.8;ctx.globalAlpha=selectedNodeId&&!active?.42:.82;
        if(edge.resolution==='hint')ctx.setLineDash([6,4]);else if(edge.resolution==='dynamic')ctx.setLineDash([2,4]);
        ctx.beginPath();ctx.moveTo(pts[0].x,pts[0].y);for(let j=1;j<pts.length;j++)ctx.lineTo(pts[j].x,pts[j].y);ctx.stroke();ctx.setLineDash([]);
        if(active){ctx.lineWidth=4;ctx.globalAlpha=.22;ctx.stroke();}
        drawArrow(ctx,pts,COLORS[edge.type]||'#a1a1aa');ctx.restore();
      });
      nodes.forEach(n=>{
        const p=positions.get(n.id);if(!p)return;const dynamic=n.kind==='dynamic',selected=selectedNodeId===n.id,connected=!selectedNodeId||selected||model.edges.some(e=>(e.from===selectedNodeId&&e.to===n.id)||(e.to===selectedNodeId&&e.from===n.id));
        const label=dynamic?'Dynamic target':n.name,ring=nodeRadius.get(n.id)||{node:NODE_R},labelAt=ring.node+8;ctx.save();ctx.globalAlpha=connected?1:.62;
        ctx.beginPath();ctx.arc(p.x,p.y,ring.node,0,Math.PI*2);ctx.fillStyle='#202631';ctx.fill();ctx.lineWidth=selected?3:1.8;ctx.strokeStyle=selected?'#facc15':dynamic?'#d97706':'#8b8b9c';ctx.setLineDash(dynamic?[3,2]:[]);ctx.stroke();ctx.setLineDash([]);
        ctx.font='600 12px ui-monospace, Menlo, Consolas, monospace';ctx.fillStyle='#e4e4e7';ctx.textBaseline='middle';if(activeOrientation==='vertical'){ctx.save();ctx.translate(p.x+labelAt,p.y+2);ctx.rotate(Math.PI/2);ctx.fillText(label,0,0);ctx.restore();}else ctx.fillText(label,p.x+labelAt,p.y+2);
        ctx.restore();
      });
      ctx.restore();
    }
    function resizeCanvas(){resize();}
    function findNode(pt){
      let best=null,dist=Infinity;
      nodes.forEach(n=>{const p=positions.get(n.id),d=Math.hypot(pt.x-p.x,pt.y-p.y),label=(n.kind==='dynamic'?'Dynamic target':n.name)||'',ring=nodeRadius.get(n.id)||{node:NODE_R},labelAt=ring.node+8;
        const inLabel=activeOrientation==='vertical'?(Math.abs(pt.x-(p.x+labelAt))<=13&&pt.y>=p.y-8&&pt.y<=p.y+8+label.length*7.5):(pt.x>=p.x+ring.node+2&&pt.x<=p.x+labelAt+6+label.length*7.5&&Math.abs(pt.y-p.y)<=13);
        if((d<ring.node+9||inLabel)&&d<dist){best=n;dist=d;}
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
    activeView={resize:resizeCanvas,draw,focus:focusNode};resize();
    if(typeof ResizeObserver!=='undefined'){const ro=new ResizeObserver(resize);ro.observe(viewport);}
  }
  root.FleditorCodeMap={render:render,setSelectedNode:setSelectedNode};
}(typeof window!=='undefined'?window:this));
