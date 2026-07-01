R"HTML(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<title>sc 程序结构依赖图</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  html,body { margin:0; height:100%; background:#0d1117; color:#c9d1d9;
    font:13px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; }
  #app { display:flex; height:100%; }
  #side { width:270px; flex:0 0 270px; padding:12px 14px; overflow:auto;
    background:#161b22; border-right:1px solid #21262d; }
  #side h1 { font-size:14px; margin:0 0 4px; }
  #side .sub { color:#8b949e; font-size:11px; margin-bottom:12px; }
  #cv { flex:1; display:block; cursor:grab; }
  #cv:active { cursor:grabbing; }
  .grp { margin:10px 0; }
  .grp h2 { font-size:11px; text-transform:uppercase; letter-spacing:.05em;
    color:#8b949e; margin:0 0 6px; }
  label.row { display:flex; align-items:center; gap:6px; padding:2px 0; cursor:pointer; }
  label.row .sw { width:20px; height:3px; border-radius:2px; flex:0 0 20px; }
  label.row .dot { width:9px; height:9px; border-radius:50%; flex:0 0 9px; }
  .stats { font-size:11px; color:#8b949e; }
  .stats b { color:#c9d1d9; }
  #search { width:100%; padding:5px 8px; background:#0d1117; border:1px solid #30363d;
    border-radius:6px; color:#c9d1d9; }
  #info { margin-top:10px; padding:8px; background:#0d1117; border:1px solid #30363d;
    border-radius:6px; font-size:12px; min-height:40px; word-break:break-all; }
  #info .k { color:#8b949e; }
  #info .t { font-weight:600; font-size:13px; }
  .toggle { display:flex; gap:6px; align-items:center; margin:4px 0; }
</style>
</head>
<body>
<div id="app">
  <div id="side">
    <h1>程序结构依赖图</h1>
    <div class="sub" id="meta"></div>
    <input id="search" placeholder="搜索节点…" autocomplete="off">
    <div id="info">点击节点查看详情</div>
    <div class="grp">
      <h2>视图</h2>
      <label class="toggle"><input type="checkbox" id="activeOnly"> 仅显示激活</label>
      <label class="toggle"><input type="checkbox" id="showModule" checked> 显示模块容器</label>
      <label class="toggle"><input type="checkbox" id="dimDead" checked> 淡化未激活</label>
    </div>
    <div class="grp"><h2>边种类</h2><div id="edgeFilters"></div></div>
    <div class="grp"><h2>节点种类</h2><div id="kindLegend"></div></div>
    <div class="grp"><h2>统计</h2><div class="stats" id="stats"></div></div>
  </div>
  <canvas id="cv"></canvas>
</div>
<script>
const DATA = /*__GRAPH_DATA__*/;

const EDGE_COLORS = {
  call:"#58a6ff", type:"#d29922", read:"#3fb950", write:"#f85149",
  method:"#bc8cff", construct:"#39c5cf", macro:"#db61a2", tokdep:"#e3b341",
  import:"#6e7681"
};
const KIND_COLORS = {
  fnc:"#58a6ff", rpc:"#79c0ff", method:"#bc8cff", dim:"#a371f7",
  struct:"#d29922", union:"#e3b341", enum:"#f0883e", alias:"#db6d28", functype:"#f8c555",
  var:"#3fb950", let:"#56d364", tls:"#2ea043", tok:"#e3b341", dep:"#e3b341",
  test:"#8b949e", macro:"#db61a2", module:"#484f58"
};

const cv = document.getElementById('cv');
const ctx = cv.getContext('2d');
let W=0,H=0, DPR=window.devicePixelRatio||1;
function resize(){ W=cv.clientWidth; H=cv.clientHeight; cv.width=W*DPR; cv.height=H*DPR;
  ctx.setTransform(DPR,0,0,DPR,0,0); }
window.addEventListener('resize', resize);

// ---- 状态 ----
const enabledEdge = {}; Object.keys(EDGE_COLORS).forEach(k=>enabledEdge[k]=k!=='import');
let activeOnly=false, showModule=true, dimDead=true;
let selected=null, hoverId=null;
const view={x:0,y:0,scale:1};

// ---- 构建节点/边索引 ----
const nodeById={}; DATA.nodes.forEach(n=>{ n.x=(Math.random()-.5)*600; n.y=(Math.random()-.5)*600; n.vx=0; n.vy=0; nodeById[n.id]=n; });
const neighbors={}; DATA.nodes.forEach(n=>neighbors[n.id]=new Set());
DATA.edges.forEach(e=>{ if(nodeById[e.from]&&nodeById[e.to]){ neighbors[e.from].add(e.to); neighbors[e.to].add(e.from); }});

function visibleNode(n){
  if(n.kind==='module') return showModule;
  if(activeOnly && !n.active) return false;
  return true;
}
function visibleEdge(e){
  if(!enabledEdge[e.kind]) return false;
  const a=nodeById[e.from], b=nodeById[e.to];
  if(!a||!b||!visibleNode(a)||!visibleNode(b)) return false;
  return true;
}

// ---- 力导向仿真 ----
function step(){
  const nodes=DATA.nodes.filter(visibleNode);
  const K=0.008, REP=1400, CENTER=0.002;
  for(const n of nodes){ n.fx=0; n.fy=0; }
  for(let i=0;i<nodes.length;i++){
    const a=nodes[i];
    for(let j=i+1;j<nodes.length;j++){
      const b=nodes[j];
      let dx=a.x-b.x, dy=a.y-b.y, d2=dx*dx+dy*dy+0.01;
      let f=REP/d2; let d=Math.sqrt(d2);
      a.fx+=f*dx/d; a.fy+=f*dy/d; b.fx-=f*dx/d; b.fy-=f*dy/d;
    }
    a.fx-=a.x*CENTER; a.fy-=a.y*CENTER;
  }
  for(const e of DATA.edges){
    if(!visibleEdge(e)) continue;
    const a=nodeById[e.from], b=nodeById[e.to];
    let dx=b.x-a.x, dy=b.y-a.y, d=Math.sqrt(dx*dx+dy*dy)+0.01;
    let f=K*(d-90);
    a.fx+=f*dx/d; a.fy+=f*dy/d; b.fx-=f*dx/d; b.fy-=f*dy/d;
  }
  for(const n of nodes){
    if(n===dragging) continue;
    n.vx=(n.vx+n.fx)*0.82; n.vy=(n.vy+n.fy)*0.82;
    n.x+=n.vx; n.y+=n.vy;
  }
}

function radius(n){ return n.kind==='module'?4:Math.max(4, 4+Math.sqrt(n.fanIn+n.fanOut)); }

function draw(){
  ctx.clearRect(0,0,W,H);
  ctx.save();
  ctx.translate(W/2+view.x, H/2+view.y); ctx.scale(view.scale,view.scale);
  const hi = selected? new Set([selected, ...neighbors[selected]]) : null;
  // edges
  for(const e of DATA.edges){
    if(!visibleEdge(e)) continue;
    const a=nodeById[e.from], b=nodeById[e.to];
    const on = !hi || (hi.has(e.from)&&hi.has(e.to));
    ctx.strokeStyle=EDGE_COLORS[e.kind]||'#6e7681';
    ctx.globalAlpha=on?0.55:0.06;
    ctx.lineWidth=(e.kind==='import'?1.4:1)/view.scale + (on?0.4:0);
    ctx.beginPath(); ctx.moveTo(a.x,a.y); ctx.lineTo(b.x,b.y); ctx.stroke();
    if(on){ // arrow head
      const ang=Math.atan2(b.y-a.y,b.x-a.x); const r=radius(b)+2;
      const hx=b.x-Math.cos(ang)*r, hy=b.y-Math.sin(ang)*r, s=5/view.scale;
      ctx.globalAlpha=on?0.6:0.06; ctx.fillStyle=EDGE_COLORS[e.kind]||'#6e7681';
      ctx.beginPath(); ctx.moveTo(hx,hy);
      ctx.lineTo(hx-Math.cos(ang-0.4)*s, hy-Math.sin(ang-0.4)*s);
      ctx.lineTo(hx-Math.cos(ang+0.4)*s, hy-Math.sin(ang+0.4)*s);
      ctx.closePath(); ctx.fill();
    }
  }
  // nodes
  ctx.globalAlpha=1;
  for(const n of DATA.nodes){
    if(!visibleNode(n)) continue;
    const on = !hi || hi.has(n.id);
    const r=radius(n);
    let col=KIND_COLORS[n.kind]||'#8b949e';
    ctx.globalAlpha = on ? (dimDead && !n.active && n.kind!=='module' ? 0.35 : 1) : 0.12;
    ctx.fillStyle=col;
    ctx.beginPath(); ctx.arc(n.x,n.y,r,0,7); ctx.fill();
    if(n.id===selected){ ctx.strokeStyle='#fff'; ctx.lineWidth=2/view.scale; ctx.stroke(); }
    else if(n.exported){ ctx.strokeStyle='#fff'; ctx.lineWidth=1.2/view.scale; ctx.globalAlpha=on?0.7:0.1; ctx.stroke(); }
    if(view.scale>0.75 && on && n.kind!=='module'){
      ctx.globalAlpha=on?0.9:0.1; ctx.fillStyle='#c9d1d9';
      ctx.font=(11/view.scale)+'px sans-serif';
      ctx.fillText(n.name, n.x+r+2, n.y+3);
    }
  }
  ctx.restore();
}

function loop(){ step(); draw(); requestAnimationFrame(loop); }

// ---- 交互：缩放/平移/拖拽/点击 ----
let dragging=null, panning=false, last=null;
function worldPos(ev){ const r=cv.getBoundingClientRect();
  return {x:(ev.clientX-r.left-W/2-view.x)/view.scale, y:(ev.clientY-r.top-H/2-view.y)/view.scale}; }
function hit(ev){ const p=worldPos(ev); let best=null,bd=1e9;
  for(const n of DATA.nodes){ if(!visibleNode(n))continue; const dx=n.x-p.x,dy=n.y-p.y,d=dx*dx+dy*dy;
    const rr=(radius(n)+4)*(radius(n)+4); if(d<rr&&d<bd){bd=d;best=n;} } return best; }
cv.addEventListener('mousedown',ev=>{ const n=hit(ev);
  if(n){ dragging=n; selected=n.id; showInfo(n); } else { panning=true; }
  last={x:ev.clientX,y:ev.clientY}; });
window.addEventListener('mousemove',ev=>{
  if(dragging){ const p=worldPos(ev); dragging.x=p.x; dragging.y=p.y; dragging.vx=0; dragging.vy=0; }
  else if(panning&&last){ view.x+=ev.clientX-last.x; view.y+=ev.clientY-last.y; last={x:ev.clientX,y:ev.clientY}; }
});
window.addEventListener('mouseup',()=>{ dragging=null; panning=false; last=null; });
cv.addEventListener('wheel',ev=>{ ev.preventDefault(); const f=ev.deltaY<0?1.1:0.9;
  view.scale=Math.max(0.15,Math.min(4,view.scale*f)); },{passive:false});

function showInfo(n){
  const io=DATA.edges.filter(e=>e.from===n.id).length;
  const el=document.getElementById('info');
  el.innerHTML=`<div class="t">${n.name}</div>
    <div><span class="k">kind</span> ${n.kind}${n.exported?' · @导出':''}${n.external?' · 外部':''}</div>
    <div><span class="k">module</span> ${n.module||'-'}</div>
    <div><span class="k">位置</span> ${(n.file||'').split('/').pop()||'-'}:${n.line||'?'}</div>
    <div><span class="k">激活</span> ${n.active?('是（深度 '+n.depth+'）'):'否'}</div>
    <div><span class="k">依赖/被依赖</span> 出 ${n.fanOut} · 入 ${n.fanIn}</div>`;
}

// ---- 侧栏 UI ----
document.getElementById('meta').textContent =
  `${DATA.root.split('/').pop()} · ${DATA.mode==='whole'?'整程序':'单元'} · 入口 ${DATA.entry.kind}`;
const s=DATA.stats;
document.getElementById('stats').innerHTML =
  `节点 <b>${s.nodes}</b> · 边 <b>${s.edges}</b><br>激活 <b>${s.active}</b> · 未激活 <b>${s.dead}</b><br>依赖环 <b>${s.cycles.length}</b>`;

const ef=document.getElementById('edgeFilters');
for(const k of Object.keys(EDGE_COLORS)){
  const l=document.createElement('label'); l.className='row';
  l.innerHTML=`<input type="checkbox" ${enabledEdge[k]?'checked':''}><span class="sw" style="background:${EDGE_COLORS[k]}"></span>${k}`;
  l.querySelector('input').onchange=e=>enabledEdge[k]=e.target.checked;
  ef.appendChild(l);
}
const kl=document.getElementById('kindLegend');
const usedKinds=[...new Set(DATA.nodes.map(n=>n.kind))];
for(const k of usedKinds){
  const l=document.createElement('div'); l.className='row';
  l.innerHTML=`<span class="dot" style="background:${KIND_COLORS[k]||'#8b949e'}"></span>${k}`;
  kl.appendChild(l);
}
document.getElementById('activeOnly').onchange=e=>activeOnly=e.target.checked;
document.getElementById('showModule').onchange=e=>showModule=e.target.checked;
document.getElementById('dimDead').onchange=e=>dimDead=e.target.checked;
document.getElementById('search').oninput=e=>{
  const q=e.target.value.trim().toLowerCase(); if(!q){selected=null;return;}
  const n=DATA.nodes.find(n=>n.name.toLowerCase().includes(q));
  if(n){ selected=n.id; showInfo(n); view.x=-n.x*view.scale; view.y=-n.y*view.scale; }
};

resize(); loop();
</script>
</body>
</html>
)HTML"
