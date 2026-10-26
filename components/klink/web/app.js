(function(){
'use strict';
/*KCOL*/
var API='/klink';
var TABS=['dashboard','program','settings'];
var state=null,offline=false,fetching=false,again=false;
var busy={},lastMode={},cards={},rows={},editing={},rowError={};

function byId(id){return document.getElementById(id);}
function qs(params){
  var parts=[],k;
  for(k in params){
    if(Object.prototype.hasOwnProperty.call(params,k)) parts.push(encodeURIComponent(k)+'='+encodeURIComponent(params[k]));
  }
  return parts.length?'?'+parts.join('&'):'';
}
function req(path,params,method){
  return fetch(path+qs(params||{}),{method:method||'POST',cache:'no-store'})
    .then(function(r){
      return r.text().then(function(text){
        var body={};
        if(text){try{body=JSON.parse(text);}catch(e){body={};}}
        if(!r.ok){throw new Error(body.error||('Request failed ('+r.status+')'));}
        return body;
      });
    });
}
function toast(msg,kind){
  var wrap=byId('toasts');
  var t=document.createElement('div');
  t.className=kind==='err'?'toast err':'toast';
  t.textContent=msg;
  wrap.appendChild(t);
  setTimeout(function(){if(t.parentNode)t.parentNode.removeChild(t);},kind==='err'?6000:3500);
}
function setText(el,text){if(el.textContent!==text)el.textContent=text;}
function updateBusy(){
  var els=document.querySelectorAll('[data-busy]'),i,v;
  for(i=0;i<els.length;i++){
    v=!!busy[els[i].getAttribute('data-busy')];
    if(els[i].disabled!==v) els[i].disabled=v;
  }
}
function act(key,path,params,okMsg,after){
  if(busy[key]) return;
  busy[key]=true;updateBusy();
  req(path,params).then(function(){
    busy[key]=false;updateBusy();
    if(okMsg) toast(okMsg,'ok');
    if(after) after(null);
    refresh();
  },function(err){
    busy[key]=false;updateBusy();
    if(after) after(err); else toast(err.message||'Request failed','err');
  });
}
function setWrites(on){
  act('writes',API+'/writes',{on:on?1:0},on?'Now sending to lights':'Stopped sending to lights',null);
}
function refresh(){
  if(fetching){again=true;return;}
  fetching=true;
  req(API+'/state',null,'GET').then(function(s){
    state=s;offline=false;
  },function(){
    offline=true;
  }).then(function(){
    try{render();}catch(err){if(window.console)console.error(err);}
    fetching=false;
    if(again){again=false;refresh();}
  });
}
function interp(slots,minute){
  if(minute<0) return null;
  var on=[],i;
  for(i=0;i<slots.length;i++) if(slots[i].enabled&&slots[i].minute<1440) on.push(slots[i]);
  if(!on.length) return null;
  on.sort(function(a,b){return a.minute-b.minute;});
  var sod=minute*60,prev=on.length-1,next,t0,t1,span,elapsed;
  for(i=0;i<on.length;i++) if(on[i].minute*60<=sod) prev=i;
  next=(prev+1)%on.length;
  t0=on[prev].minute*60;t1=on[next].minute*60;
  span=((t1-t0)%86400+86400)%86400;
  elapsed=((sod-t0)%86400+86400)%86400;
  var out=[];
  for(i=0;i<5;i++){
    var a=on[prev].ch[i],b=on[next].ch[i],v=a;
    if(span!==0){
      var d=(b-a)*elapsed;
      var step=d>=0?Math.floor(d/span):-Math.floor((-d+span-1)/span);
      v=a+step;
    }
    out.push(v);
  }
  return out;
}
function computeOutput(g,minute){
  var m=minute===undefined?g.now_minute:minute;
  var out=interp(g.slots,m);
  if(!out) return null;
  for(var i=0;i<5;i++){
    var max=g.max&&typeof g.max[i]==='number'?g.max[i]:255;
    if(out[i]<0)out[i]=0;
    if(out[i]>max)out[i]=max;
  }
  return out;
}
function outputText(g){
  if(g.mode!=='program') return g.status;
  var ch=computeOutput(g);
  if(!ch) return '—';
  var parts=[];
  for(var i=0;i<5;i++){
    var label=g.labels&&g.labels[i];
    if(!label) continue;
    var max=g.max&&typeof g.max[i]==='number'?g.max[i]:255;
    var pct=max===255?Math.round(ch[i]*100/255):ch[i];
    parts.push(label.toUpperCase()+' '+pct+'%');
  }
  return parts.join(' · ');
}
function modeOf(g){
  if(g.mode==='preview') return lastMode[g.gid]||'program';
  lastMode[g.gid]=g.mode;
  return g.mode;
}
function lampName(kid){
  if(!state||!state.lamps) return '';
  for(var i=0;i<state.lamps.length;i++) if(state.lamps[i].kid===kid) return state.lamps[i].name||'';
  return '';
}
function lampLabel(kid){
  var n=lampName(kid);
  if(n) return n;
  var s=String(kid);
  return 'Lamp '+s.slice(-4);
}
function makeButton(label,cls){
  var b=document.createElement('button');
  b.type='button';b.className=cls||'btn';b.textContent=label;
  return b;
}
function renderHeader(){
  var pill=byId('bus-pill');
  if(!state){pill.textContent='Connecting…';pill.className='pill';}
  else{
    var t=state.bus.state||'';
    var cls='pill';
    if(/conflict/i.test(t)) cls+=' bad';
    else if(/controlling/i.test(t)) cls+=' good';
    else if(/scanning|waiting|shadow/i.test(t)) cls+=' warn';
    pill.className=cls;
    setText(pill,t||'Unknown bus state');
    pill.setAttribute('title',t);
  }
  byId('offline').hidden=!offline;
  byId('writes-banner').hidden=!state||!!state.bus.writes;
  if(state){
    var w=!!state.bus.writes;
    setText(byId('writes-toggle'),w?'Turn off':'Turn on');
    byId('retry-btn').hidden=!/conflict/i.test(state.bus.state||'');
    setText(byId('bus-text'),state.bus.state||'');
    setText(byId('bus-count'),String(state.bus.known_lamps||0));
  }
}
function makeCard(g){
  var card=document.createElement('article');
  card.className='card';
  var head=document.createElement('div');head.className='card-head';
  var h2=document.createElement('h2');
  var badge=document.createElement('span');badge.className='badge';badge.textContent='Previewing';badge.hidden=true;
  head.appendChild(h2);head.appendChild(badge);
  var status=document.createElement('p');status.className='status';
  var seg=document.createElement('div');seg.className='seg';seg.setAttribute('role','group');seg.setAttribute('aria-label','Mode');
  var modes=[['program','Program'],['manual','Manual'],['off','Off']];
  var buttons={};
  modes.forEach(function(m){
    var b=makeButton(m[1],'');
    b.setAttribute('aria-pressed','false');
    b.setAttribute('data-busy','mode-'+g.gid);
    b.addEventListener('click',function(){act('mode-'+g.gid,API+'/mode',{gid:g.gid,mode:m[0]},null,null);});
    seg.appendChild(b);
    buttons[m[0]]=b;
  });
  var outLabel=document.createElement('p');outLabel.className='section-label';outLabel.textContent='Live output';
  var out=document.createElement('p');out.className='out';
  var outRow=document.createElement('div');outRow.className='outrow';
  var dot=document.createElement('span');dot.className='dot';dot.setAttribute('aria-hidden','true');dot.hidden=true;
  outRow.appendChild(dot);outRow.appendChild(out);
  var rowline=document.createElement('div');rowline.className='rowline';
  var find=makeButton('Find these lamps','btn');
  find.setAttribute('data-busy','find-'+g.gid);
  find.addEventListener('click',function(){
    act('find-'+g.gid,API+'/identify',{gid:g.gid},'Flashing the lamps in '+g.name,null);
  });
  var count=document.createElement('span');count.className='lamps';
  rowline.appendChild(find);rowline.appendChild(count);
  var chips=document.createElement('div');chips.className='chips';
  card.appendChild(head);card.appendChild(status);card.appendChild(seg);
  card.appendChild(outLabel);card.appendChild(outRow);card.appendChild(rowline);card.appendChild(chips);
  byId('groups').appendChild(card);
  return {el:card,name:h2,badge:badge,status:status,segButtons:buttons,out:out,dot:dot,find:find,count:count,chips:chips,chipEls:{}};
}
function updateCard(g){
  var c=cards[g.gid];
  if(!c){c=makeCard(g);cards[g.gid]=c;}
  var mode=modeOf(g);
  setText(c.name,g.name||('Group '+g.gid));
  c.badge.hidden=g.mode!=='preview';
  setText(c.status,g.status||'');
  ['program','manual','off'].forEach(function(m){
    var b=c.segButtons[m],on=m===mode;
    b.className=on?'on':'';
    b.setAttribute('aria-pressed',on?'true':'false');
  });
  if(g.mode==='program'){
    var t=outputText(g);
    setText(c.out,t);
    c.out.className='out';
  }else{
    setText(c.out,outputText(g));
    c.out.className='out muted';
  }
  var dcol='';
  if(g.mode==='program'||g.mode==='preview')dcol=swatchColor(g,computeOutput(g));
  c.dot.hidden=!dcol;
  if(dcol)c.dot.style.background=dcol;
  var finding=!!busy['find-'+g.gid];
  c.find.textContent='';
  if(finding){
    var sp=document.createElement('span');sp.className='spin';sp.setAttribute('aria-hidden','true');
    c.find.appendChild(sp);c.find.appendChild(document.createTextNode('Finding…'));
  }else{
    c.find.appendChild(document.createTextNode('Find these lamps'));
  }
  var n=g.lamps?g.lamps.length:0;
  setText(c.count,n===1?'1 lamp':n+' lamps');
  var seen={},i,kid,chip;
  if(g.lamps) for(i=0;i<g.lamps.length;i++){
    kid=g.lamps[i];seen[kid]=true;
    chip=c.chipEls[kid];
    if(!chip){
      chip=document.createElement('button');
      chip.type='button';chip.className='chip';
      chip.addEventListener('click',(function(k){return function(){
        act('id-'+k,API+'/identify',{kid:k},'Flashing '+lampLabel(k),null);
      };})(kid));
      c.chips.appendChild(chip);c.chipEls[kid]=chip;
    }
    setText(chip,lampLabel(kid));
    chip.setAttribute('aria-label','Flash '+lampLabel(kid));
  }
  for(kid in c.chipEls){
    if(!seen[kid]){
      c.chips.removeChild(c.chipEls[kid]);
      delete c.chipEls[kid];
    }
  }
}
function renderDashboard(){
  var gs=state.groups||[],seen={},i,g;
  var container=byId('groups');
  for(i=0;i<gs.length;i++){
    g=gs[i];seen[g.gid]=true;updateCard(g);
  }
  for(var gid in cards){
    if(!seen[gid]){
      cards[gid].el.parentNode.removeChild(cards[gid].el);
      delete cards[gid];
    }
  }
  var hint=container.querySelector('.hint');
  if(gs.length){
    if(hint) container.removeChild(hint);
  }else{
    if(!hint){
      hint=document.createElement('p');hint.className='hint';
      container.appendChild(hint);
    }
    setText(hint,'No groups configured.');
  }
}
function makeRow(lamp){
  var row=document.createElement('div');row.className='row';
  var main=document.createElement('div');main.className='row-main';
  var name=document.createElement('div');name.className='row-name';
  var meta=document.createElement('div');meta.className='row-meta';
  main.appendChild(name);main.appendChild(meta);
  var actions=document.createElement('div');actions.className='row-actions';
  var idBtn=makeButton('Identify','btn small');
  idBtn.setAttribute('data-busy','id-'+lamp.kid);
  idBtn.addEventListener('click',function(){
    act('id-'+lamp.kid,API+'/identify',{kid:lamp.kid},'Flashing '+lampLabel(lamp.kid),null);
  });
  var renameBtn=makeButton('Rename','btn small');
  actions.appendChild(idBtn);actions.appendChild(renameBtn);

  var edit=document.createElement('div');edit.className='row-edit';edit.hidden=true;
  var input=document.createElement('input');input.type='text';input.maxLength=24;
  input.setAttribute('aria-label','Lamp nickname');
  var saveBtn=makeButton('Save','btn small primary');saveBtn.setAttribute('data-busy','name-'+lamp.kid);
  var cancelBtn=makeButton('Cancel','btn small');
  edit.appendChild(input);edit.appendChild(saveBtn);edit.appendChild(cancelBtn);

  var select=document.createElement('select');
  select.setAttribute('aria-label','Group for '+lampLabel(lamp.kid));
  var moveBtn=makeButton('Move','btn small');
  moveBtn.setAttribute('data-busy','assign-'+lamp.kid);
  actions.appendChild(select);actions.appendChild(moveBtn);

  var err=document.createElement('div');err.className='row-error';err.hidden=true;
  err.setAttribute('role','status');

  row.appendChild(main);row.appendChild(actions);row.appendChild(edit);row.appendChild(err);

  var refs={el:row,name:name,meta:meta,idBtn:idBtn,renameBtn:renameBtn,edit:edit,input:input,
    saveBtn:saveBtn,cancelBtn:cancelBtn,select:select,moveBtn:moveBtn,err:err,
    sig:'',dirty:false,nickname:''};

  function openEdit(){
    editing[lamp.kid]=true;
    refs.nickname=lampName(lamp.kid);
    input.value=refs.nickname;
    edit.hidden=false;
    input.focus();
    input.select();
  }
  function closeEdit(){
    editing[lamp.kid]=false;
    edit.hidden=true;
  }
  function saveName(){
    var v=input.value;
    act('name-'+lamp.kid,API+'/name',{kid:lamp.kid,name:v},v?'Nickname saved':'Nickname cleared',function(e){
      if(e){setRowError(lamp.kid,e.message);}
      else{setRowError(lamp.kid,'');closeEdit();renameBtn.focus();}
    });
  }
  renameBtn.addEventListener('click',function(){
    if(editing[lamp.kid]) closeEdit(); else openEdit();
  });
  saveBtn.addEventListener('click',saveName);
  cancelBtn.addEventListener('click',function(){closeEdit();renameBtn.focus();});
  input.addEventListener('keydown',function(ev){
    if(ev.key==='Enter'){ev.preventDefault();saveName();}
    else if(ev.key==='Escape'){ev.preventDefault();closeEdit();renameBtn.focus();}
  });
  select.addEventListener('change',function(){refs.dirty=true;});
  moveBtn.addEventListener('click',function(){
    act('assign-'+lamp.kid,API+'/assign',{kid:lamp.kid,gid:parseInt(select.value,10)},null,function(e){
      if(e){setRowError(lamp.kid,e.message);}
      else{setRowError(lamp.kid,'');refs.dirty=false;toast('Lamp moved','ok');}
    });
  });
  return refs;
}
function setRowError(kid,msg){
  rowError[kid]=msg;
  var r=rows[kid];
  if(!r) return;
  r.err.hidden=!msg;
  setText(r.err,msg||'');
}
function fillGroups(select,lamp,refs){
  var gids=[],names={},i,g;
  if(state&&state.groups) for(i=0;i<state.groups.length;i++){
    g=state.groups[i];gids.push(g.gid);names[g.gid]=g.name||('Group '+g.gid);
  }
  for(i=3;i<=25;i++) if(gids.indexOf(i)<0){gids.push(i);}
  if(gids.indexOf(lamp.gid)<0) gids.push(lamp.gid);
  gids.sort(function(a,b){return a-b;});
  var sig=gids.join(',')+'#'+JSON.stringify(names);
  if(refs.sig!==sig){
    var keep=select.value;
    refs.sig=sig;
    select.textContent='';
    for(i=0;i<gids.length;i++){
      var o=document.createElement('option');
      o.value=String(gids[i]);
      o.textContent=names[gids[i]]?names[gids[i]]+' ('+gids[i]+')':'Group '+gids[i];
      select.appendChild(o);
    }
    if(refs.dirty&&keep) select.value=keep;
  }
  if(!refs.dirty&&document.activeElement!==select) select.value=String(lamp.gid);
}
function updateRow(lamp){
  var r=rows[lamp.kid];
  if(!r){r=makeRow(lamp);rows[lamp.kid]=r;byId('lamps').appendChild(r.el);}
  setText(r.name,lampLabel(lamp.kid));
  var gname='';
  if(state&&state.groups) for(var i=0;i<state.groups.length;i++) if(state.groups[i].gid===lamp.gid) gname=' · '+state.groups[i].name;
  setText(r.meta,'Type '+lamp.type+' · Group '+lamp.gid+gname);
  fillGroups(r.select,lamp,r);
  if(!editing[lamp.kid]) r.edit.hidden=true;
  r.err.hidden=!rowError[lamp.kid];
  setText(r.err,rowError[lamp.kid]||'');
}
function renderSettings(){
  var ls=state.lamps||[],seen={},i;
  var container=byId('lamps');
  for(i=0;i<ls.length;i++){seen[ls[i].kid]=true;updateRow(ls[i]);}
  for(var kid in rows){
    if(!seen[kid]){
      container.removeChild(rows[kid].el);
      delete rows[kid];
      delete editing[kid];
      delete rowError[kid];
    }
  }
  setText(byId('lamps-hint'),ls.length?'Known lamps on the bus.':'No lamps known yet. Find lamps runs a scan.');
}
function render(){
  renderHeader();
  if(state){renderDashboard();renderSettings();}
  renderProgram(state);
  updateBusy();
}
function showTab(name){
  if(TABS.indexOf(name)<0) name='dashboard';
  for(var i=0;i<TABS.length;i++){
    var t=TABS[i],on=t===name;
    var b=byId('tab-'+t+'-btn'),p=byId('tab-'+t);
    b.setAttribute('aria-selected',on?'true':'false');
    b.tabIndex=on?0:-1;
    p.hidden=!on;
  }
  document.body.setAttribute('data-tab',name);
}
function tabFromHash(){
  var h=location.hash.replace(/^#/,'');
  showTab(h||'dashboard');
}
function initTabs(){
  var tablist=document.querySelector('[role="tablist"]');
  var tabs=tablist.querySelectorAll('[role="tab"]');
  for(var i=0;i<tabs.length;i++){
    (function(btn){
      btn.addEventListener('click',function(){
        var n=btn.getAttribute('data-tab');
        if(location.hash!=='#'+n) location.hash=n; else showTab(n);
      });
      btn.addEventListener('keydown',function(ev){
        var idx=-1,j,next;
        for(j=0;j<tabs.length;j++) if(tabs[j]===btn) idx=j;
        if(ev.key==='ArrowRight') next=(idx+1)%tabs.length;
        else if(ev.key==='ArrowLeft') next=(idx+tabs.length-1)%tabs.length;
        else if(ev.key==='Home') next=0;
        else if(ev.key==='End') next=tabs.length-1;
        else return;
        ev.preventDefault();
        tabs[next].focus();
        tabs[next].click();
      });
    })(tabs[i]);
  }
  window.addEventListener('hashchange',tabFromHash);
  tabFromHash();
}
/* ---- Program tab ---- */
var NS='http://www.w3.org/2000/svg';
var DASH=['','6 3','2 4','9 3 2 3','1 3'];
var PRESETS=[
  ['Sunrise-sunset',[[450,0,0],[600,100,90],[1020,100,90],[1200,0,0]]],
  ['Daytime plateau',[[479,0,0],[480,100,90],[1079,100,90],[1080,0,0]]],
  ['Always on',[[0,100,90]]],
  ['Moonlight',[[0,20,0]]]
];
var P={gid:loadGid(),slots:[],sel:0,ch:0,scrub:-1,drag:false,dirty:0};
var pg=null,dragPid=null;
var pvT=0,pvBusy=false,pvPend=null;
function mk(tag,cls,parent,text){
  var e=document.createElement(tag);
  if(cls)e.className=cls;
  if(text!=null)e.textContent=text;
  if(parent)parent.appendChild(e);
  return e;
}
function se(tag,attrs,parent){
  var e=document.createElementNS(NS,tag),k;
  for(k in attrs)if(Object.prototype.hasOwnProperty.call(attrs,k))e.setAttribute(k,attrs[k]);
  if(parent)parent.appendChild(e);
  return e;
}
function cl(v,a,b){return v<a?a:v>b?b:v;}
function hhmm(m){return m<0?'--:--':('0'+Math.floor(m/60)).slice(-2)+':'+('0'+m%60).slice(-2);}
/* Each channel keeps its own colour: red is red, amber is amber, green is green, violet is violet. */
function chCol(g,c){var l=g.labels&&g.labels[c];return 'var(--k-'+(/^(int|clr|vio|r|am|grn)$/.test(l||'')?l:'int')+')';}
function hasCh(g,c){return !!(g.labels&&g.labels[c]);}
function hx(h){return [parseInt(h.slice(1,3),16),parseInt(h.slice(3,5),16),parseInt(h.slice(5,7),16)];}
/* Approximate lamp colour for raw channel values, from our own model (KCOL). */
function swatchColor(g,ch){
  if(!g||!ch)return '';
  var st=g.model==='tuna_sun'?KCOL.sun:g.model==='tuna_blue'?KCOL.blue:0;
  if(!st)return '';
  var L=g.labels||[],ci=L.indexOf('clr'),n=16;
  var f=(ci<0?128:(ch[ci]==null?128:cl(ch[ci],0,255)))/255*n,i=Math.floor(f),t=f-i;
  var a=hx(st[i]),b=hx(st[Math.min(n,i+1)]),o=[],k;
  for(k=0;k<3;k++)o[k]=a[k]+(b[k]-a[k])*t;
  var O=['r','am','vio','grn'];
  for(k=0;k<4;k++){
    var j=L.indexOf(O[k]);
    if(j<0||ch[j]==null)continue;
    var c=hx(KCOL.ov[O[k]]),m=.6*cl(ch[j],0,100)/100;
    for(var q=0;q<3;q++)o[q]+=(c[q]-o[q])*m;
  }
  return '#'+o.map(function(x){return('0'+Math.round(cl(x,0,255)).toString(16)).slice(-2);}).join('');
}
function cv(g,c,v){var mx=g.max&&typeof g.max[c]==='number'?g.max[c]:255;return mx===255?Math.round(v*100/255):v;}
function vc(g,c,p){var mx=g.max&&typeof g.max[c]==='number'?g.max[c]:255;return mx===255?Math.round(p*255/100):Math.round(p);}
function cloneSlots(a){var o=[],i;for(i=0;i<a.length;i++)o.push({minute:a[i].minute,ch:a[i].ch.slice(),enabled:!!a[i].enabled});return o;}
function lerp(a,b,e,s){var d=(b-a)*e;return a+(d>=0?Math.floor(d/s):-Math.floor((-d+s-1)/s));}
function loadGid(){try{return parseInt(localStorage.getItem('klink.prog.gid'),10)||0;}catch(e){return 0;}}
function saveGid(v){try{localStorage.setItem('klink.prog.gid',String(v));}catch(e){}}
function onList(){
  var a=[],i;
  for(i=0;i<P.slots.length;i++)if(P.slots[i].enabled&&P.slots[i].minute<1440)a.push({i:i,p:P.slots[i]});
  a.sort(function(x,y){return x.p.minute-y.p.minute||x.i-y.i;});
  return a;
}
function onCount(){var n=0,i;for(i=0;i<P.slots.length;i++)if(P.slots[i].enabled)n++;return n;}
function outText(g,m){
  var ch=interp(P.slots,m),parts=[],i;
  if(!ch)return '—';
  for(i=0;i<5;i++)if(hasCh(g,i))parts.push(g.labels[i].toUpperCase()+' '+cv(g,i,ch[i])+'%');
  return parts.join(' · ');
}
/* One request at a time, at most one per 120 ms, trailing call guaranteed. */
function pvSend(fn,now){
  pvPend=fn;
  if(now){
    if(pvT){clearTimeout(pvT);pvT=0;}
    pvRun();
  }else if(!pvT){
    pvT=setTimeout(function(){pvT=0;pvRun();},120);
  }
}
function pvRun(){
  if(pvBusy)return;
  var f=pvPend;
  pvPend=null;
  if(!f)return;
  pvBusy=true;
  f().then(pvDone,function(e){pvDone();if(!P.drag)toast(e.message||'Update failed','err');});
}
function pvDone(){
  pvBusy=false;
  if(pvPend&&!pvT)pvT=setTimeout(function(){pvT=0;pvRun();},120);
}
function pvPoint(slot,params,now){
  params.gid=P.gid;params.slot=slot+1;
  pvSend(function(){return req(API+'/point',params);},now);
}
function plotPt(ev){
  var r=pg.svg.getBoundingClientRect();
  var q=geom(pg.svg.clientWidth||r.width,pg.svg.clientHeight||r.height);
  return {q:q,x:ev.clientX-r.left,y:ev.clientY-r.top};
}
function geom(w,h){
  var q={w:w,h:h,l:32,r:10,t:10,b:18};
  q.pw=Math.max(1,w-q.l-q.r);q.ph=Math.max(1,h-q.t-q.b);
  return q;
}
function px(q,m){return q.l+m/1440*q.pw;}
function py(q,p){return q.t+(100-p)/100*q.ph;}
function chooseGroup(gid){
  if(gid===P.gid)return;
  P.gid=gid;saveGid(gid);P.sel=0;P.scrub=-1;P.dirty=0;P.drag=false;
  if(pg){pg.lsig='';pg.drawn='';}
  if(state)renderProgram(state);
}
function setTime(v){
  var m=/^(\d{1,2}):(\d{1,2})/.exec(v||''),s=P.slots[P.sel];
  if(!m||!s)return;
  s.minute=cl(parseInt(m[1],10)*60+parseInt(m[2],10),0,1439);
  P.dirty=Date.now();
  pvPoint(P.sel,{minute:s.minute});
  pg.drawn='';draw();updatePanel(pg.g);
}
function setChan(c,pct){
  var g=pg.g,s=P.slots[P.sel];
  if(!g||!s)return;
  pct=cl(isNaN(pct)?0:pct,0,100);
  s.ch[c]=vc(g,c,pct);
  P.dirty=Date.now();
  var params={minute:s.minute};
  params['c'+(c+1)]=s.ch[c];
  pvPoint(P.sel,params);
  pg.drawn='';draw();updatePanel(g);
}
function addPoint(i){
  var g=pg.g,s=P.slots[i];
  if(!g||!s)return;
  var m=g.now_minute>=0?g.now_minute:(s.minute||720);
  var v=interp(P.slots,m)||[0,0,0,0,0];
  s.minute=m;s.ch=v.slice();s.enabled=true;
  P.sel=i;P.dirty=Date.now();
  var params={minute:m,enabled:1},c;
  for(c=0;c<5;c++)if(hasCh(g,c))params['c'+(c+1)]=s.ch[c];
  pvPoint(i,params,true);
  pg.drawn='';draw();updatePanel(g);syncUnused();
}
function removePoint(i){
  var s=P.slots[i];
  if(!s||!s.enabled)return;
  if(onCount()<=1){toast('Keep at least one point in the program','err');return;}
  s.enabled=false;
  P.dirty=Date.now();
  pvPoint(i,{minute:s.minute,enabled:0},true);
  for(var k=1;k<=P.slots.length;k++){
    var j=(i+k)%P.slots.length;
    if(P.slots[j].enabled){P.sel=j;break;}
  }
  pg.drawn='';draw();updatePanel(pg.g);syncUnused();
}
function askPreset(pre){
  pg.pending=pre;
  setText(pg.confirmText,'Replace the whole program with "'+pre[0]+'"?');
  pg.confirm.hidden=false;
}
function applyPreset(pre){
  var g=pg.g;
  if(!g)return;
  var pts=pre[1],chain=Promise.resolve(),i;
  if(pts.length>P.slots.length){toast('This model has fewer points than the preset needs','err');return;}
  toast('Applying preset…','ok');
  for(i=0;i<P.slots.length;i++){
    (function(i){
      var p=pts[i];
      chain=chain.then(function(){
        var s=P.slots[i],params={gid:g.gid,slot:i+1,minute:p?p[0]:0,enabled:p?1:0},c;
        for(c=0;c<5;c++)if(hasCh(g,c))params['c'+(c+1)]=p?vc(g,c,c===0?p[1]:c===1?p[2]:0):0;
        return req(API+'/point',params).then(function(){
          s.minute=params.minute;s.enabled=!!p;
          for(c=0;c<5;c++)s.ch[c]=(p&&hasCh(g,c))?params['c'+(c+1)]:0;
        });
      });
    })(i);
  }
  chain.then(function(){
    toast('Program replaced','ok');
    P.dirty=Date.now();pg.drawn='';draw();refresh();
  },function(e){
    toast(e.message||'Could not apply the preset','err');
    refresh();
  });
}
function moveDrag(t){
  var g=pg.g,s=P.slots[P.sel];
  if(!g||!s)return;
  var q=t.q;
  var m=Math.round((t.x-q.l)/q.pw*1440/5)*5;
  s.minute=cl(m,0,1439);
  var pc=cl(Math.round((1-(t.y-q.t)/q.ph)*100),0,100);
  s.ch[P.ch]=vc(g,P.ch,pc);
  P.dirty=Date.now();
  var params={minute:s.minute};
  params['c'+(P.ch+1)]=s.ch[P.ch];
  pvPoint(P.sel,params);
  pg.drawn='';draw();updatePanel(g);
}
function onDown(ev){
  var g=pg.g;
  if(!g||ev.button>0)return;
  var t=plotPt(ev),q=t.q,on=onList(),best=-1,bd=26,i;
  for(i=0;i<on.length;i++){
    var dx=px(q,on[i].p.minute)-t.x,dy=py(q,cv(g,P.ch,on[i].p.ch[P.ch]))-t.y;
    var d=Math.sqrt(dx*dx+dy*dy);
    if(d<bd){bd=d;best=on[i].i;}
  }
  if(best<0||!hasCh(g,P.ch))return;
  ev.preventDefault();
  try{pg.svg.setPointerCapture(ev.pointerId);}catch(e){}
  dragPid=ev.pointerId;
  P.sel=best;P.drag=true;P.dirty=Date.now();
  pg.drawn='';draw();updatePanel(g);
  moveDrag(t);
}
function onMove(ev){
  if(!P.drag||ev.pointerId!==dragPid)return;
  ev.preventDefault();
  moveDrag(plotPt(ev));
}
function onUp(ev){
  if(!P.drag||ev.pointerId!==dragPid)return;
  P.drag=false;P.dirty=Date.now();
  dragPid=null;
  try{pg.svg.releasePointerCapture(ev.pointerId);}catch(e){}
  /* The queued trailing send already carries the final values; no duplicate request. */
  pg.drawn='';draw();
}
function onKey(ev){
  var t=ev.target,ds=t&&t.getAttribute?t.getAttribute('data-slot'):null;
  if(ds==null)return;
  var idx=parseInt(ds,10),s=P.slots[idx],g=pg.g;
  if(!s||!g)return;
  var chg=null;
  if(ev.key==='ArrowLeft'||ev.key==='ArrowRight'){
    var dm=ev.shiftKey?1:5;
    s.minute=cl(s.minute+(ev.key==='ArrowRight'?dm:-dm),0,1439);
    chg={minute:s.minute};
  }else if(ev.key==='ArrowUp'||ev.key==='ArrowDown'){
    if(!hasCh(g,P.ch))return;
    var dc=ev.shiftKey?5:1;
    var pc=cl(cv(g,P.ch,s.ch[P.ch])+(ev.key==='ArrowUp'?dc:-dc),0,100);
    s.ch[P.ch]=vc(g,P.ch,pc);
    chg={minute:s.minute};chg['c'+(P.ch+1)]=s.ch[P.ch];
  }else if(ev.key==='Delete'||ev.key==='Backspace'){
    ev.preventDefault();removePoint(idx);return;
  }else return;
  ev.preventDefault();
  P.sel=idx;P.dirty=Date.now();
  pvPoint(idx,chg);
  pg.drawn='';draw();updatePanel(g);
}
function buildProg(){
  var root=byId('prog-root');
  root.textContent='';
  pg={};
  pg.gseg=mk('div','seg',root);
  pg.gseg.setAttribute('role','group');
  pg.gseg.setAttribute('aria-label','Lamp group');
  pg.gseg.hidden=true;
  pg.preview=mk('div','previewbar',root);
  pg.preview.hidden=true;
  mk('p',null,pg.preview,'Previewing on the lamps');
  var pd=makeButton('Done','btn small');
  pd.addEventListener('click',function(){if(pg.g)act('pvd',API+'/preview/done',{gid:pg.g.gid},'Preview closed',null);});
  pg.preview.appendChild(pd);
  var wrap=mk('div','pwrap',root);
  var left=mk('div',null,wrap);
  var box=mk('div','chartbox',left);
  pg.chart=mk('div',null,box);
  pg.chart.id='prog-chart';
  pg.svg=document.createElementNS(NS,'svg');
  pg.svg.setAttribute('role','group');
  pg.svg.setAttribute('aria-label','Daily program curve');
  pg.chart.appendChild(pg.svg);
  pg.empty=mk('div','chart-empty',pg.chart,'No points yet: add one below');
  pg.legend=mk('div','legend',left);
  pg.legend.setAttribute('role','group');
  pg.legend.setAttribute('aria-label','Channels');
  pg.unusedLabel=mk('p','section-label',left,'Unused points');
  pg.unused=mk('div','unused',left);
  mk('p','section-label',left,'Scrub the day');
  pg.scrub=mk('input',null,left);
  pg.scrub.type='range';pg.scrub.min=0;pg.scrub.max=1439;pg.scrub.step=5;pg.scrub.value=0;
  pg.scrub.setAttribute('aria-label','Scrub the day');
  pg.scrubVal=mk('p','out',left);
  var sb=mk('div','pbtnrow',left);
  pg.nowBtn=makeButton('Back to now','btn small');
  sb.appendChild(pg.nowBtn);
  pg.clock=mk('p','hint',left,'The clock is not set, so the now line is hidden. You can still scrub a time to preview it.');
  var pan=mk('div','panel',wrap);
  pg.ptitle=mk('h3',null,pan,'No point selected');
  var tr=mk('div','pbtnrow',pan);
  mk('span',null,tr,'Time');
  pg.time=mk('input',null,tr);
  pg.time.type='time';pg.time.step=300;
  pg.time.setAttribute('aria-label','Point time');
  pg.chans=mk('div',null,pan);
  pg.chRows=[];
  pg.pick=mk('div','pick',pan);
  var pm=mk('div','pick-main',pg.pick);
  pg.strip=mk('div','cstrip',pm);
  pg.strip.setAttribute('role','slider');
  pg.strip.tabIndex=0;
  pg.strip.setAttribute('aria-label','Colour');
  pg.strip.setAttribute('aria-valuemin','0');
  pg.strip.setAttribute('aria-valuemax','100');
  pg.thumb=mk('div','cthumb',pg.strip);
  pg.thumb.setAttribute('aria-hidden','true');
  pg.clbl=mk('div','clbl',pm);
  pg.swatch=mk('div','cswatch',pg.pick);
  pg.swatch.setAttribute('aria-hidden','true');
  var sid=null;
  function stripPct(ev){
    var r=pg.strip.getBoundingClientRect();
    return cl(Math.round((ev.clientX-r.left)/Math.max(1,r.width)*100),0,100);
  }
  pg.strip.addEventListener('pointerdown',function(ev){
    var s=P.slots[P.sel];
    if(ev.button>0||pg.pick.hidden||!s||!s.enabled)return;
    ev.preventDefault();
    try{pg.strip.setPointerCapture(ev.pointerId);}catch(e){}
    sid=ev.pointerId;
    setChan(1,stripPct(ev));
  });
  pg.strip.addEventListener('pointermove',function(ev){
    if(sid!==null&&ev.pointerId===sid){ev.preventDefault();setChan(1,stripPct(ev));}
  });
  function stripEnd(ev){if(sid!==null&&ev.pointerId===sid)sid=null;}
  pg.strip.addEventListener('pointerup',stripEnd);
  pg.strip.addEventListener('pointercancel',stripEnd);
  pg.strip.addEventListener('keydown',function(ev){
    var s=P.slots[P.sel];
    if(pg.pick.hidden||!s||!s.enabled)return;
    var cur=cv(pg.g,1,s.ch[1]),d=ev.shiftKey?5:1,v;
    if(ev.key==='ArrowRight'||ev.key==='ArrowUp')v=cur+d;
    else if(ev.key==='ArrowLeft'||ev.key==='ArrowDown')v=cur-d;
    else if(ev.key==='Home')v=0;
    else if(ev.key==='End')v=100;
    else return;
    ev.preventDefault();
    setChan(1,cl(v,0,100));
  });
  var rb=mk('div','pbtnrow',pan);
  pg.remove=makeButton('Remove point','btn danger');
  rb.appendChild(pg.remove);
  mk('p','section-label',root,'Presets');
  var pr=mk('div','chips',root);
  PRESETS.forEach(function(pre){
    var b=makeButton(pre[0],'btn small');
    b.addEventListener('click',function(){askPreset(pre);});
    pr.appendChild(b);
  });
  pg.confirm=mk('div','confirm',root);
  pg.confirm.hidden=true;
  pg.confirmText=mk('span',null,pg.confirm);
  var yes=makeButton('Yes','btn small primary');
  var no=makeButton('Cancel','btn small');
  pg.confirm.appendChild(yes);pg.confirm.appendChild(no);
  pg.pending=null;
  yes.addEventListener('click',function(){
    var p=pg.pending;pg.confirm.hidden=true;pg.pending=null;
    if(p)applyPreset(p);
  });
  no.addEventListener('click',function(){pg.confirm.hidden=true;pg.pending=null;});
  pg.copy=mk('div','pbtnrow',root);
  pg.copy.hidden=true;
  mk('span',null,pg.copy,'Copy program to');
  pg.copySel=mk('select',null,pg.copy);
  pg.copySel.setAttribute('aria-label','Copy program to group');
  var cb=makeButton('Copy','btn small');
  pg.copy.appendChild(cb);
  cb.addEventListener('click',function(){
    if(pg.g)act('copy',API+'/copy',{from:pg.g.gid,to:parseInt(pg.copySel.value,10)},'Program copied',null);
  });
  pg.scrub.addEventListener('input',function(){
    var m=parseInt(pg.scrub.value,10)||0;
    P.scrub=m;P.dirty=Date.now();
    pvSend(function(){return req(API+'/preview',{gid:P.gid,minute:m});});
    pg.drawn='';updateScrub();draw();
  });
  pg.nowBtn.addEventListener('click',function(){
    if(!pg.g)return;
    P.scrub=-1;P.dirty=Date.now();
    pg.scrub.value=pg.g.now_minute>=0?pg.g.now_minute:0;
    req(API+'/preview/done',{gid:pg.g.gid}).then(refresh,function(){});
    pg.drawn='';updateScrub();draw();
  });
  pg.time.addEventListener('change',function(){setTime(pg.time.value);});
  pg.remove.addEventListener('click',function(){removePoint(P.sel);});
  pg.svg.addEventListener('pointerdown',onDown);
  pg.svg.addEventListener('pointermove',onMove);
  pg.svg.addEventListener('pointerup',onUp);
  pg.svg.addEventListener('pointercancel',onUp);
  pg.svg.addEventListener('keydown',onKey);
  if(window.ResizeObserver)new ResizeObserver(function(){pg.drawn='';draw();}).observe(pg.chart);
}
function syncGroups(gs,g){
  var sig=[],i;
  for(i=0;i<gs.length;i++)sig.push(gs[i].gid+'='+(gs[i].name||''));
  sig=sig.join('|');
  if(pg.gsig!==sig){
    pg.gsig=sig;
    pg.gseg.textContent='';
    gs.forEach(function(x){
      var b=makeButton(x.name||('Group '+x.gid),'');
      b.setAttribute('data-gid',String(x.gid));
      b.addEventListener('click',function(){chooseGroup(x.gid);});
      pg.gseg.appendChild(b);
    });
  }
  pg.gseg.hidden=gs.length<2;
  var bs=pg.gseg.children;
  for(i=0;i<bs.length;i++){
    var on=parseInt(bs[i].getAttribute('data-gid'),10)===g.gid;
    bs[i].className=on?'on':'';
    bs[i].setAttribute('aria-pressed',on?'true':'false');
  }
}
function markLegend(){
  var bs=pg.legend.children;
  for(var i=0;i<bs.length;i++)bs[i].setAttribute('aria-pressed',parseInt(bs[i].getAttribute('data-ch'),10)===P.ch?'true':'false');
}
function syncLegend(g){
  pg.legend.textContent='';
  for(var c=0;c<5;c++){
    if(!hasCh(g,c))continue;
    var b=makeButton('','lg');
    b.setAttribute('data-ch',c);
    var sp=document.createElementNS(NS,'svg');
    sp.setAttribute('width','22');sp.setAttribute('height','10');sp.setAttribute('aria-hidden','true');
    se('line',{x1:1,y1:5,x2:21,y2:5,stroke:chCol(g,c),'stroke-width':3,'stroke-dasharray':DASH[c],'stroke-linecap':'round'},sp);
    b.appendChild(sp);
    b.appendChild(document.createTextNode(g.labels[c].toUpperCase()));
    b.addEventListener('click',(function(cc){return function(){
      P.ch=cc;pg.drawn='';draw();updatePanel(pg.g);markLegend();
    };})(c));
    pg.legend.appendChild(b);
  }
}
function syncPanel(g){
  var sig=(g.labels||[]).join(',');
  if(pg.lsig===sig)return;
  pg.lsig=sig;
  var valid=[],c;
  for(c=0;c<5;c++)if(hasCh(g,c))valid.push(c);
  if(valid.indexOf(P.ch)<0)P.ch=valid.length?valid[0]:0;
  pg.chans.textContent='';
  pg.chRows=[];
  valid.forEach(function(c){
    var row=mk('div','chan',pg.chans);
    var top=mk('div','chan-top',row);
    mk('span',null,top,g.labels[c].toUpperCase());
    var num=mk('input',null,top);
    num.type='number';num.min=0;num.max=100;num.step=1;
    num.setAttribute('aria-label',g.labels[c]+' percent');
    var sl=mk('input',null,row);
    sl.type='range';sl.min=0;sl.max=100;sl.step=1;
    sl.setAttribute('aria-label',g.labels[c]+' percent slider');
    sl.addEventListener('input',function(){setChan(c,parseInt(sl.value,10));});
    num.addEventListener('change',function(){setChan(c,parseInt(num.value,10));});
    pg.chRows.push({c:c,sl:sl,num:num});
  });
  syncLegend(g);
}
function updatePanel(g){
  g=g||pg.g;
  if(!g)return;
  var s=P.slots[P.sel],on=s&&s.enabled,i;
  setText(pg.ptitle,on?'Point '+(P.sel+1)+' of '+P.slots.length:'No point selected');
  pg.time.disabled=!on;
  if(document.activeElement!==pg.time)pg.time.value=on?hhmm(s.minute):'';
  for(i=0;i<pg.chRows.length;i++){
    var r=pg.chRows[i],v=s?cv(g,r.c,s.ch[r.c]):0;
    if(document.activeElement!==r.sl){r.sl.value=v;r.sl.disabled=!on;}
    if(document.activeElement!==r.num){r.num.value=v;r.num.disabled=!on;}
  }
  var uc=(g.model==='tuna_sun'||g.model==='tuna_blue')&&hasCh(g,1);
  pg.pick.hidden=!uc;
  if(!uc)return;
  var v=s?cv(g,1,s.ch[1]):0,col=swatchColor(g,s?s.ch:null);
  pg.thumb.style.left='calc(10px + (100% - 20px) * '+(v/100)+')';
  pg.thumb.style.background=col;
  pg.swatch.style.background=col;
  pg.strip.setAttribute('aria-valuenow',v);
  pg.strip.setAttribute('aria-valuetext','CLR '+v+'%');
  pg.strip.setAttribute('aria-disabled',on?'false':'true');
  if(pg.psig!==g.model){
    pg.psig=g.model;
    pg.strip.style.background='linear-gradient(to right,'+(g.model==='tuna_sun'?KCOL.sun:KCOL.blue).join(',')+')';
    pg.clbl.textContent='';
    if(g.model==='tuna_sun'){mk('span',null,pg.clbl,'6,000 K');mk('span',null,pg.clbl,'9,000 K');}
    else mk('span',null,pg.clbl,'CLR');
  }
}
function syncUnused(){
  pg.unused.textContent='';
  for(var i=0;i<P.slots.length;i++){
    if(P.slots[i].enabled)continue;
    (function(slot){
      var b=makeButton('Point '+(slot+1),'btn small');
      b.setAttribute('aria-label','Add a point in slot '+(slot+1));
      b.addEventListener('click',function(){addPoint(slot);});
      pg.unused.appendChild(b);
    })(i);
  }
  pg.unused.hidden=!pg.unused.children.length;
  pg.unusedLabel.hidden=pg.unused.hidden;
}
function updateScrub(){
  var g=pg.g;
  if(!g)return;
  pg.clock.hidden=g.now_minute>=0;
  if(P.scrub<0){
    if(g.now_minute>=0&&document.activeElement!==pg.scrub)pg.scrub.value=g.now_minute;
    var m=g.now_minute>=0?g.now_minute:(parseInt(pg.scrub.value,10)||0);
    setText(pg.scrubVal,(g.now_minute>=0?'Now '+hhmm(g.now_minute):'Preview '+hhmm(m))+' · '+outText(g,m));
  }else{
    if(document.activeElement!==pg.scrub)pg.scrub.value=P.scrub;
    setText(pg.scrubVal,'Preview '+hhmm(P.scrub)+' · '+outText(g,P.scrub));
  }
}
function syncCopy(g){
  var targets=[],i;
  for(i=0;i<pg.groups.length;i++){
    var h=pg.groups[i];
    if(h.gid!==g.gid&&h.model===g.model&&h.slots&&h.slots.length===g.slots.length)targets.push(h);
  }
  pg.copy.hidden=!targets.length;
  if(!targets.length)return;
  var sig=[];
  for(i=0;i<targets.length;i++)sig.push(targets[i].gid+'='+(targets[i].name||''));
  sig=sig.join('|');
  if(pg.csig!==sig){
    pg.csig=sig;
    pg.copySel.textContent='';
    targets.forEach(function(t){
      var o=document.createElement('option');
      o.value=String(t.gid);o.textContent=t.name||('Group '+t.gid);
      pg.copySel.appendChild(o);
    });
  }
}
function draw(){
  var g=pg&&pg.g;
  if(!g)return;
  var w=pg.svg.clientWidth,h=pg.svg.clientHeight;
  if(w<80||h<60)return;
  var q=geom(w,h);
  var sig=JSON.stringify([P.slots,w,h,P.ch,g.now_minute,P.scrub,P.sel]);
  if(pg.drawn===sig)return;
  var focused=null,ae=document.activeElement;
  if(ae&&ae.getAttribute&&ae.getAttribute('data-slot')!=null)focused=ae.getAttribute('data-slot');
  pg.drawn=sig;
  var svg=pg.svg,i,c;
  svg.setAttribute('viewBox','0 0 '+w+' '+h);
  svg.textContent='';
  var on=onList(),n=on.length;
  pg.empty.hidden=n>0;
  for(i=0;i<=24;i+=3){
    var x=px(q,i*60);
    se('line',{x1:x,y1:q.t,x2:x,y2:q.t+q.ph,stroke:'var(--line)','stroke-width':1},svg);
    var xt=se('text',{x:Math.min(w-10,Math.max(10,x)),y:h-4,'text-anchor':'middle','font-size':9,fill:'var(--muted)'},svg);
    xt.textContent=('0'+i).slice(-2);
  }
  for(i=0;i<=100;i+=25){
    var y=py(q,i);
    se('line',{x1:q.l,y1:y,x2:q.l+q.pw,y2:y,stroke:'var(--line)','stroke-width':i%50===0?1:.6},svg);
    var yt=se('text',{x:q.l-4,y:y+3,'text-anchor':'end','font-size':9,fill:'var(--muted)'},svg);
    yt.textContent=i;
  }
  var order=[];
  for(c=0;c<5;c++)if(hasCh(g,c))order.push(c);
  order.sort(function(a,b){return (a===P.ch)-(b===P.ch);});
  for(var k=0;k<order.length;k++){
    c=order[k];
    var col=chCol(g,c),dash=DASH[c],wid=c===0?3:2,op=c===P.ch?1:.55,pts=[];
    for(i=0;i<n;i++)pts.push(px(q,on[i].p.minute)+','+py(q,cv(g,c,on[i].p.ch[c])));
    if(n===1){
      var yy=py(q,cv(g,c,on[0].p.ch[c]));
      se('line',{x1:q.l,y1:yy,x2:q.l+q.pw,y2:yy,stroke:col,'stroke-width':wid,'stroke-dasharray':dash,opacity:op},svg);
    }else if(n>1){
      se('polyline',{points:pts.join(' '),fill:'none',stroke:col,'stroke-width':wid,'stroke-dasharray':dash,'stroke-linejoin':'round','stroke-linecap':'round',opacity:op},svg);
      var A=on[n-1].p,B=on[0].p;
      var span=((B.minute-A.minute)%1440+1440)%1440;
      if(span===0){
        var ya=py(q,cv(g,c,A.ch[c]));
        se('line',{x1:q.l,y1:ya,x2:q.l+q.pw,y2:ya,stroke:col,'stroke-width':wid,'stroke-dasharray':dash,opacity:op},svg);
      }else{
        var vm=lerp(A.ch[c],B.ch[c],1440-A.minute,span);
        var ym=py(q,cv(g,c,vm));
        se('polyline',{points:px(q,A.minute)+','+py(q,cv(g,c,A.ch[c]))+' '+(q.l+q.pw)+','+ym,fill:'none',stroke:col,'stroke-width':wid,'stroke-dasharray':dash,opacity:op},svg);
        se('polyline',{points:q.l+','+ym+' '+px(q,B.minute)+','+py(q,cv(g,c,B.ch[c])),fill:'none',stroke:col,'stroke-width':wid,'stroke-dasharray':dash,opacity:op},svg);
      }
    }
  }
  if(g.now_minute>=0){
    var nx=px(q,g.now_minute);
    se('line',{x1:nx,y1:q.t,x2:nx,y2:q.t+q.ph,stroke:'var(--warn)','stroke-width':1.5,'stroke-dasharray':'4 3'},svg);
    var nt=se('text',{x:Math.min(w-16,Math.max(16,nx)),y:q.t+11,'text-anchor':'middle','font-size':9,fill:'var(--warn)'},svg);
    nt.textContent=hhmm(g.now_minute);
  }
  if(P.scrub>=0&&P.scrub!==g.now_minute){
    var sx=px(q,P.scrub);
    se('line',{x1:sx,y1:q.t,x2:sx,y2:q.t+q.ph,stroke:'var(--accent)','stroke-width':1.5},svg);
  }
  if(n&&hasCh(g,P.ch)){
    for(i=0;i<n;i++){
      var p=on[i].p,dx=px(q,p.minute),dy=py(q,cv(g,P.ch,p.ch[P.ch])),val=cv(g,P.ch,p.ch[P.ch]);
      if(on[i].i===P.sel)se('circle',{cx:dx,cy:dy,r:12,fill:'none',stroke:'var(--fg)','stroke-width':1.5,'pointer-events':'none'},svg);
      se('circle',{cx:dx,cy:dy,r:7,fill:chCol(g,P.ch),stroke:'var(--card)','stroke-width':2,'pointer-events':'none'},svg);
      se('circle',{cx:dx,cy:dy,r:22,fill:'none','pointer-events':'all',tabindex:'0',role:'slider','data-slot':on[i].i,
        'aria-label':'Point '+(on[i].i+1)+', '+hhmm(p.minute)+', '+g.labels[P.ch].toUpperCase()+' '+val+'%',
        'aria-valuemin':'0','aria-valuemax':'100','aria-valuenow':val,
        'aria-valuetext':g.labels[P.ch].toUpperCase()+' '+val+'%'},svg);
    }
  }
  if(focused!=null&&(!document.activeElement||!svg.contains(document.activeElement))){
    var fe=svg.querySelector('[data-slot="'+focused+'"]');
    if(fe)fe.focus();
  }
}
function renderProgram(s){
  if(!s)return;
  var root=byId('prog-root'),gs=s.groups||[],i,g=null;
  if(!gs.length){
    if(root.getAttribute('data-empty')!=='1'){
      root.textContent='';
      mk('p','hint',root,'No lamp groups found. Open Settings to find your lamps.');
      root.setAttribute('data-empty','1');
    }
    pg=null;
    return;
  }
  if(root.getAttribute('data-empty')==='1'){root.removeAttribute('data-empty');root.textContent='';}
  if(!pg)buildProg();
  for(i=0;i<gs.length;i++)if(gs[i].gid===P.gid)g=gs[i];
  if(!g){
    g=gs[0];P.gid=g.gid;saveGid(g.gid);P.sel=0;P.scrub=-1;P.dirty=0;P.drag=false;
    pg.lsig='';pg.drawn='';
  }
  pg.g=g;pg.groups=gs;
  if(!P.drag&&Date.now()-P.dirty>=1500)P.slots=cloneSlots(g.slots);
  if(P.sel<0||P.sel>=P.slots.length)P.sel=0;
  syncGroups(gs,g);
  syncPanel(g);
  updatePanel(g);
  markLegend();
  syncUnused();
  updateScrub();
  syncCopy(g);
  pg.preview.hidden=g.mode!=='preview';
  draw();
}
byId('writes-on').addEventListener('click',function(){setWrites(true);});
byId('writes-toggle').addEventListener('click',function(){
  setWrites(!(state&&state.bus.writes));
});
byId('scan-btn').addEventListener('click',function(){
  act('scan',API+'/scan',{},'Scan started',null);
});
byId('retry-btn').addEventListener('click',function(){
  act('retry',API+'/retry',{},'Retry requested',null);
});
document.addEventListener('visibilitychange',function(){if(!document.hidden) refresh();});
initTabs();
refresh();
setInterval(function(){if(!document.hidden) refresh();},2000);
})();
