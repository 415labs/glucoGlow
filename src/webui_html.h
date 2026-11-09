#pragma once
#include <pgmspace.h>

// Single-page settings app served by the device. No external assets, works offline.
// The form is generated from the SECTIONS table below; field keys match the JSON API (settings_store.cpp).
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>glucoGlow</title>
<style>
:root{color-scheme:light dark;--bd:#8884;--ac:#2a7;--crit:#d32f2f;--caut:#ef8a00;--ok:#2e9e4f;--stale:#3b6fd6;--unk:#888}
body{font:15px/1.4 system-ui,sans-serif;max-width:640px;margin:0 auto;padding:12px}
h1{font-size:20px;margin:6px 0 10px}
fieldset{border:1px solid var(--bd);border-radius:8px;margin:12px 0;padding:6px 12px 10px}
legend{font-weight:600;padding:0 6px}
label{display:flex;justify-content:space-between;align-items:center;gap:10px;margin:7px 0}
label span{flex:1}
small{display:block;opacity:.65;font-size:12px}
input,select,button{font:inherit;padding:5px 7px;border-radius:6px;border:1px solid var(--bd);background:transparent;color:inherit}
input:not([type=checkbox]),select{width:52%;box-sizing:border-box}
.rg{display:flex;align-items:center;gap:6px;width:52%}
.rg input{width:auto!important;flex:1;padding:0}
.rg output{min-width:2.4em;text-align:right}
button{cursor:pointer;background:var(--ac);color:#fff;border:0;padding:8px 14px;margin:3px 4px 3px 0}
button.s{background:#8885;color:inherit}
button.d{background:var(--crit)}
#st{display:grid;grid-template-columns:1fr 1fr;gap:2px 12px}
#st b{font-size:28px}
.chip{display:inline-block;color:#fff;border-radius:10px;padding:1px 10px;font-size:13px}
.chip.crit,.seg.crit{background:var(--crit)}.chip.caut,.seg.caut{background:var(--caut)}
.chip.ok,.seg.ok{background:var(--ok)}.chip.stale{background:var(--stale)}.chip.unk{background:var(--unk)}
#bar{display:flex;margin-top:8px;border-radius:6px;overflow:hidden}
.seg{color:#fff;font-size:10px;text-align:center;padding:6px 2px;overflow:hidden;white-space:nowrap}
#banner{display:none;background:var(--caut);color:#fff;border-radius:8px;padding:8px 12px;margin:8px 0;font-weight:600}
#msg{min-height:1.4em;font-weight:600}
.err{color:var(--crit)}.good{color:var(--ok)}
</style></head><body>
<h1>glucoGlow <small id="ver" style="display:inline"></small></h1>
<div id="banner"></div>
<fieldset id="statusBox"><legend>Status</legend><div id="st"></div></fieldset>
<fieldset id="chartBox" style="display:none"><legend>Last 12 hours</legend><canvas id="ch" style="width:100%;height:170px"></canvas><small id="chNote"></small></fieldset>
<form id="f" onsubmit="return false"></form>
<fieldset id="previewBox"><legend>Preview on the display</legend>
<label><span>State<small>Shows this state on the device for 20 s</small></span><select id="pvState">
<option value="normal">Normal (green)</option>
<option value="caution_low">Caution low (orange)</option><option value="caution_high">Caution high (orange)</option>
<option value="critical_low">Critical low (red)</option><option value="critical_high">Critical high (red)</option>
<option value="stale">Stale / no data (blue)</option></select></label>
<button class="s" onclick="preview()">Show for 20 s</button></fieldset>
<div id="msg"></div>
<button onclick="save()">Save</button>
<button class="s" onclick="testLogin()">Test LibreLinkUp login</button>
<button class="s" onclick="post('/api/reboot','Rebooting...')">Reboot</button>
<button class="d" onclick="resetAll()">Factory reset</button>
<script>
const MODES=[[0,'Steady'],[1,'Blink slow'],[2,'Blink fast'],[3,'Slow pulse'],[5,'Fast pulse'],[4,'3 blinks + pause']];
// t: text|password|number|check|select|time|range. bar: redraw the range bar on change.
// grp: brightness group ('day'|'night'): moving the slider previews it on the device.
const SECTIONS=[
{n:'Connection',f:[
 {k:'wifiSsid',l:'WiFi network (SSID)',t:'text'},
 {k:'wifiPass',l:'WiFi password',t:'password'},
 {k:'lluEmail',l:'LibreLinkUp email',t:'text'},
 {k:'lluPass',l:'LibreLinkUp password',t:'password'},
 {k:'lluRegion',l:'Region override',t:'text',h:'Leave empty for auto (e.g. us, eu, de, fr, jp, ae, ap, au)'},
 {k:'lluVersion',l:'LibreLinkUp app version',t:'text',h:'Sent as the "version" header. Raise it if login is rejected as outdated'},
 {k:'pollSec',l:'Poll interval (seconds)',t:'number',min:30,max:900,h:'30-900, default 60'},
 {k:'tlsInsecure',l:'Skip certificate check',t:'check',h:'Only if HTTPS fails; weakens security'}]},
{n:'Glucose ranges',bar:1,intro:'Values in mg/dL. Critical = red, Caution = orange (between critical and normal), Normal = green.',f:[
 {k:'units',l:'Display units',t:'select',o:[[0,'mg/dL'],[1,'mmol/L']]},
 {k:'criticalLow',l:'Critical low: below',t:'number',min:30,max:100,bar:1},
 {k:'normalLow',l:'Normal range starts at',t:'number',min:31,max:200,bar:1},
 {k:'normalHigh',l:'Normal range ends at',t:'number',min:31,max:350,bar:1},
 {k:'criticalHigh',l:'Critical high: above',t:'number',min:100,max:450,bar:1},
 {k:'staleMin',l:'No new data after (minutes)',t:'number',min:5,max:120,h:'Shown as stale (blue)'}]},
{n:'Alert patterns',intro:'Only one element animates, never the whole screen, and the number stays steady: the trend arrow for critical, the LOW / HIGH label for caution, the STALE label for no / stale data. On the OLED a pulse is a blink that stays on most of the time.',f:[
 {k:'criticalLowMode',l:'Critical low (red): arrow',t:'select',o:MODES},
 {k:'criticalHighMode',l:'Critical high (red): arrow',t:'select',o:MODES},
 {k:'cautionMode',l:'Caution, low or high (orange): label',t:'select',o:MODES},
 {k:'staleMode',l:'No / stale data (blue): STALE text',t:'select',o:MODES}]},
{n:'Brightness',intro:'The display is never switched off: night is dim but still visible. Move a slider to preview it on the device.',f:[
 {k:'dayStartMin',l:'Day starts',t:'time'},
 {k:'nightStartMin',l:'Night starts',t:'time'},
 {k:'dayContrast',l:'Day: display brightness',t:'range',min:1,max:255,grp:'day'},
 {k:'nightContrast',l:'Night: display brightness',t:'range',min:1,max:255,grp:'night'},
 {k:'tz',l:'Timezone (POSIX TZ)',t:'text',h:'US Pacific: PST8PDT,M3.2.0,M11.1.0'}]},
{n:'Display',f:[
 {k:'showArrow',l:'Show trend arrow',t:'check'},
 {k:'showAge',l:'Show age of reading',t:'check'},
 {k:'pixelShift',l:'Pixel shift (OLED burn-in)',t:'check'},
 {k:'showChart',l:'Show 12-hour chart',t:'check',h:'On this page and as a chart that appears 4 s out of every 12 s on the display (normal/caution only). After a reboot the last 12 hours are fetched from the LibreLink server'},
 {k:'rotation',l:'Screen rotation',t:'select',o:[[0,'0 (default)'],[90,'90 clockwise'],[180,'180 (upside down)'],[270,'270 clockwise']],h:'Pick the one that reads upright on your shelf. Applies when you press Save. 90 and 270 use a tall layout'}]},
{n:'Device',f:[
 {k:'hostname',l:'Hostname (http://name.local)',t:'text'},
 {k:'apPass',l:'Setup WiFi password',t:'password',h:'8-32 characters (default: glow + 4 digits). Shown on the device screen in setup mode. Applies next time setup mode starts'},
 {k:'adminPass',l:'Admin password (user: admin)',t:'password',h:'Protects this page on your home WiFi. Empty = none'},
 {k:'clearAdminPass',l:'Remove admin password',t:'check'}]}
];
const ZC={critical_low:'crit',critical_high:'crit',caution_low:'caut',caution_high:'caut',normal:'ok',stale:'stale',unknown:'unk'};
const ZN={critical_low:'Critical low',critical_high:'Critical high',caution_low:'Caution (low)',caution_high:'Caution (high)',normal:'Normal',stale:'No fresh data',unknown:'Waiting for data'};
const $=s=>document.querySelector(s);
let cur={},pvT=0;
function msg(t,c){const m=$('#msg');m.textContent=t;m.className=c||''}
function bar(){
 const b=$('#bar');if(!b)return;
 const g=k=>parseInt(($('#k_'+k)||{}).value,10)||0;
 const cl=g('criticalLow'),nl=g('normalLow'),nh=g('normalHigh'),ch=g('criticalHigh'),mx=Math.max(ch+80,320);
 const seg=[[cl,'crit','&lt;'+cl],[nl-cl,'caut','Caution'],[nh-nl+1,'ok','Normal '+nl+'-'+nh],[ch-nh,'caut','Caution'],[mx-ch,'crit','&gt;'+ch]];
 b.innerHTML=seg.map(s=>'<div class="seg '+s[1]+'" style="flex:'+Math.max(s[0],1)+'">'+s[2]+'</div>').join('');
}
function previewBright(g){
 const c=+$('#k_'+g+'Contrast').value;
 clearTimeout(pvT);
 pvT=setTimeout(()=>api('/api/preview',J({state:'keep',contrast:c,seconds:10})).catch(()=>{}),250);
}
function build(){
 const f=$('#f');f.innerHTML='';
 for(const s of SECTIONS){
  const fs=document.createElement('fieldset');
  fs.innerHTML='<legend>'+s.n+'</legend>'+(s.intro?'<small>'+s.intro+'</small>':'');
  for(const x of s.f){
   const lb=document.createElement('label');
   lb.innerHTML='<span>'+x.l+(x.h?'<small>'+x.h+'</small>':'')+'</span>';
   let i;
   if(x.t==='select'){
    i=document.createElement('select');
    for(const o of x.o){const op=document.createElement('option');op.value=o[0];op.textContent=o[1];i.appendChild(op)}
   }else{
    i=document.createElement('input');
    i.type=x.t==='check'?'checkbox':x.t;
    if(x.min!=null)i.min=x.min;
    if(x.max!=null)i.max=x.max;
    if(x.t==='password')i.autocomplete='new-password';
   }
   i.id='k_'+x.k;
   if(x.bar)i.addEventListener('input',bar);
   if(x.t==='range'){
    const o=document.createElement('output');o.id='o_'+x.k;
    i.addEventListener('input',()=>{o.textContent=i.value;if(x.grp)previewBright(x.grp)});
    const w=document.createElement('div');w.className='rg';w.append(i,o);lb.appendChild(w);
   }else lb.appendChild(i);
   fs.appendChild(lb);
  }
  if(s.bar){const b=document.createElement('div');b.id='bar';fs.appendChild(b)}
  f.appendChild(fs);
 }
}
async function chart(){
 const box=$('#chartBox');
 box.style.display=cur.showChart?'':'none';
 if(!cur.showChart)return;
 let p=[];try{p=(await api('/api/history')).p}catch(e){return}
 const c=$('#ch'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
 c.width=W*dpr;c.height=H*dpr;
 const g=c.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,W,H);
 const u=cur.units==1,cv=x=>u?x/18.016:x,fm=x=>u?cv(x).toFixed(1):Math.round(x);
 let lo=Math.min(40,...p.map(a=>a[1])),hi=Math.max(240,cur.criticalHigh+10,...p.map(a=>a[1]));
 const L=34,B=16,pw=W-L-4,ph=H-B-4,X=m=>L+pw-(m/720)*pw,Y=v=>4+ph-(v-lo)/(hi-lo)*ph;
 const band=(a,b,col)=>{g.fillStyle=col;g.fillRect(L,Y(b),pw,Y(a)-Y(b))};
 const cs=getComputedStyle(document.body);
 band(lo,cur.criticalLow,'#d32f2f22');band(cur.criticalLow,cur.normalLow,'#ef8a0022');
 band(cur.normalLow,cur.normalHigh,'#2e9e4f22');band(cur.normalHigh,cur.criticalHigh,'#ef8a0022');band(cur.criticalHigh,hi,'#d32f2f22');
 g.fillStyle=cs.color;g.strokeStyle='#8886';g.font='11px system-ui';g.textAlign='right';
 for(const v of [cur.criticalLow,cur.normalLow,cur.normalHigh,cur.criticalHigh]){g.beginPath();g.moveTo(L,Y(v));g.lineTo(L+pw,Y(v));g.stroke();g.fillText(fm(v),L-4,Y(v)+4)}
 g.textAlign='center';for(let h=0;h<=12;h+=3){g.fillText(h?'-'+h+'h':'now',X(h*60),H-3)}
 if(p.length<2){g.textAlign='left';g.fillText('Collecting data...',L+8,H/2);$('#chNote').textContent='Loading the last 12 hours from the LibreLink server...';return}
 $('#chNote').textContent=p.length+' points (older ones come from the LibreLink server after a reboot).';
 const col=v=>v<cur.criticalLow||v>cur.criticalHigh?'#d32f2f':(v<cur.normalLow||v>cur.normalHigh?'#ef8a00':'#2e9e4f');
 g.strokeStyle=cs.color;g.lineWidth=1.5;g.beginPath();
 p.forEach((a,i)=>{i?g.lineTo(X(a[0]),Y(a[1])):g.moveTo(X(a[0]),Y(a[1]))});g.stroke();
 p.forEach(a=>{g.fillStyle=col(a[1]);g.beginPath();g.arc(X(a[0]),Y(a[1]),2.5,0,7);g.fill()});
}
const hm12=m=>{const h=Math.floor(m/60)%24;return ((h%12)||12)+':'+String(m%60).padStart(2,'0')+' '+(h<12?'AM':'PM')};
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
function fill(){
 for(const s of SECTIONS)for(const x of s.f){
  const i=$('#k_'+x.k);if(!i)continue;
  const v=cur[x.k];
  if(x.t==='check')i.checked=!!v;
  else if(x.t==='time')i.value=hm(v||0);
  else if(x.t==='password'){i.value='';i.placeholder=cur[x.k+'Set']?'(set - leave empty to keep)':'(not set)'}
  else if(v!==undefined)i.value=v;
  if(x.t==='range'){const o=$('#o_'+x.k);if(o)o.textContent=i.value}
 }
 bar();
}
function collect(){
 const o={};
 for(const s of SECTIONS)for(const x of s.f){
  const i=$('#k_'+x.k);if(!i)continue;
  if(x.t==='check')o[x.k]=i.checked;
  else if(x.t==='time'){const p=i.value.split(':');o[x.k]=(+p[0])*60+(+p[1]||0)}
  else if(x.t==='number'||x.t==='select'||x.t==='range')o[x.k]=parseInt(i.value,10);
  else o[x.k]=i.value;
 }
 return o;
}
async function api(path,opt){
 const r=await fetch(path,opt);
 if(!r.ok&&r.status!==400)throw new Error('HTTP '+r.status);
 return r.json();
}
const J=b=>({method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
async function save(){
 try{
  const r=await api('/api/settings',J(collect()));
  if(r.error){msg(r.error,'err');return}
  msg(r.rebootRequired?'Saved. WiFi/hostname changes need a reboot.':'Saved and applied.','good');
  await load();
 }catch(e){msg('Save failed: '+e.message,'err')}
}
async function testLogin(){
 msg('Testing...');
 const o=collect();
 try{
  const r=await api('/api/test-login',J({lluEmail:o.lluEmail,lluPass:o.lluPass,lluRegion:o.lluRegion,lluVersion:o.lluVersion,tlsInsecure:o.tlsInsecure}));
  msg(r.ok?('Login OK. Latest reading: '+r.mgdl+' mg/dL'):('Login failed: '+r.error),r.ok?'good':'err');
 }catch(e){msg('Test failed: '+e.message,'err')}
}
async function preview(){
 try{await api('/api/preview',J({state:$('#pvState').value,contrast:+$('#k_dayContrast').value,seconds:20}));msg('Previewing on the device for 20 s','good')}
 catch(e){msg('Preview failed: '+e.message,'err')}
}
async function post(p,t){try{await api(p,{method:'POST'});msg(t,'good')}catch(e){msg(t,'good')}}
function resetAll(){if(confirm('Erase ALL settings including WiFi and credentials?'))post('/api/factory-reset','Resetting... the device restarts in setup mode.')}
async function load(){cur=await api('/api/settings');fill();chart()}
async function status(){
 try{
  const s=await api('/api/status');
  $('#ver').textContent='v'+s.version+' - '+s.ip;
  const v=s.haveValue?(cur.units==1?(s.mgdl/18.016).toFixed(1):s.mgdl):'--';
  const t=['↓↓','↘','→','↗','↑↑'][s.trendDir+2]||'';
  $('#st').innerHTML='<div><b>'+v+'</b> '+(s.trendKnown?t:'')+'</div>'+
   '<div><span class="chip '+(ZC[s.state]||'unk')+'">'+(ZN[s.state]||s.state)+'</span></div>'+
   '<div>Device time: '+(s.localMin>=0?hm12(s.localMin)+' - <b>'+(s.night?'night':'day')+'</b> brightness':'clock not synced yet - <b>night</b> brightness')+'</div>'+
   '<div>Age: '+(s.ageKnown?s.ageMin+' min':'-')+'</div>'+
   '<div>WiFi: '+(s.provisioning?'not connected':s.rssi+' dBm')+'</div>'+
   '<div>Last poll: '+(s.pollEver?(s.lastOk?'<span class="good">ok</span>':'<span class="err">'+s.lastErr+'</span>')+' ('+s.lastPollAgoS+' s ago)':'-')+'</div>'+
   '<div>Heap: '+Math.round(s.freeHeap/1024)+' kB, up '+Math.round(s.uptimeS/60)+' min</div>';
  const bn=$('#banner');
  if(s.setupAp){
   bn.style.display='block';
   bn.textContent=s.provisioning
    ?'First-time setup: enter your WiFi and LibreLinkUp details, press Save, then Reboot.'
    :'Setup WiFi is ON ('+s.apSsid+'). It switches off after 15 min without clients, or hold BOOT 3 s on the device.';
  }else bn.style.display='none';
 }catch(e){}
}
(async()=>{build();await load();await status();setInterval(status,5000);setInterval(chart,30000)})();
</script></body></html>)HTML";
