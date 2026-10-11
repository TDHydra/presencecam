// The dashboard served at "/". Plain HTML + JS, no external files, so it also works on
// the setup access point with no internet.
#pragma once
#include <pgmspace.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PresenceCam</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#667085;--line:#e3e6ec;--acc:#2563eb;--ok:#14894a;--bad:#c2341d;--on:#f59e0b}
@media (prefers-color-scheme:dark){:root{--bg:#0f1218;--card:#181c24;--fg:#e6e9ef;--mut:#8b93a3;--line:#2a303c;--acc:#5b8dff;--ok:#3fbf7a;--bad:#ff6b52;--on:#fbbf24}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:760px;margin:0 auto;padding:16px}
h1{font-size:1.3rem;margin:4px 0 2px}h2{font-size:1rem;margin:0 0 10px}
.mut{color:var(--mut)}.mono{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.85em}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:14px 0}
.hero{display:flex;gap:18px;align-items:center;flex-wrap:wrap}
.dot{width:84px;height:84px;border-radius:50%;background:var(--line);display:grid;place-items:center;transition:background .3s;flex:none}
.dot.on{background:var(--on);box-shadow:0 0 0 8px color-mix(in srgb,var(--on) 25%,transparent)}
.dot svg{width:44px;height:44px;fill:var(--card)}
.state{font-size:2rem;font-weight:700;line-height:1.1}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:14px}
.kv{background:var(--bg);border-radius:8px;padding:8px 10px}.kv b{display:block;font-size:1.1rem}
.bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;background:var(--acc);width:0;transition:width .3s}
table{width:100%;border-collapse:collapse;font-size:.9rem}td,th{padding:6px 4px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}
.badge{display:inline-block;padding:2px 9px;border-radius:99px;font-weight:600;font-size:.8rem;color:#fff}.badge.ok{background:var(--ok)}.badge.bad{background:var(--bad)}
label{display:block;margin:8px 0 3px;font-weight:600;font-size:.9rem}
input,select{width:100%;padding:8px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button{background:var(--acc);color:#fff;border:0;border-radius:8px;padding:9px 16px;font:inherit;font-weight:600;cursor:pointer;margin-top:12px}
button.sec{background:var(--line);color:var(--fg)}
.tw{overflow-x:auto}pre{white-space:pre-wrap;margin:8px 0 0}
details summary{cursor:pointer;font-weight:600}
@media (max-width:520px){.row{grid-template-columns:1fr}.state{font-size:1.6rem}}
</style></head><body><main>
<header><h1 id="name">PresenceCam</h1><div class="mut" id="sub">connecting…</div></header>

<section class="card"><div class="hero">
 <div class="dot" id="dot"><svg viewBox="0 0 24 24"><circle cx="12" cy="7" r="4"/><path d="M4 21c0-4.4 3.6-8 8-8s8 3.6 8 8z"/></svg></div>
 <div><div class="state" id="state">–</div><div class="mut" id="for"></div></div>
</div>
<div class="grid" id="readings"></div>
<p class="mut" id="sens" style="margin:12px 0 0"></p>
</section>

<section class="card"><h2>Event log <span id="vbadge"></span></h2>
<div id="vreport" class="mut"></div>
<div class="tw"><table><thead><tr><th>#</th><th>When</th><th>Event</th><th>Detail</th></tr></thead><tbody id="log"></tbody></table></div>
<button class="sec" onclick="loadLog()">Refresh</button> <button class="sec" onclick="location='/api/log.txt'">Download log</button>
<details style="margin-top:12px"><summary>How the log is protected</summary><p class="mut">Each line carries an HMAC-SHA256 signature made with a secret key that never leaves the device, plus the signature of the line before it. Editing, deleting or reordering lines breaks the chain, and the check above says where. The line counter is kept separately, so deleting the whole log is noticed too. Older lines rotate out after about 48 KB.</p></details>
</section>

<section class="card"><h2>Hardware</h2>
<p class="mut" id="hwsum"></p>
<div class="tw"><table><thead><tr><th>Connection</th><th>GPIO</th></tr></thead><tbody id="pins"></tbody></table></div>
<p class="mut">To change the board, sensors, display or layout, open the web flasher, choose them there and press <em>Send setup to board</em>. No reflash needed.</p>
<details><summary>Config JSON</summary><pre class="mono" id="hwjson"></pre></details>
</section>

<section class="card"><h2>Settings</h2><form id="sf">
<label for="sname">Device name</label><input id="sname" name="name" maxlength="32">
<div class="row"><div><label for="hold">Hold time (seconds)</label><input id="hold" name="hold" type="number" min="1" max="3600"></div>
<div><label for="maxcm">Max distance (cm, LD2410 only)</label><input id="maxcm" name="maxcm" type="number" min="30" max="800"></div></div>
<div class="row"><div><label for="screenflip">Screen orientation</label><select id="screenflip" name="screenflip"><option value="0">Normal</option><option value="1">Upside down</option></select></div>
<div><label for="screensleep">Screen off after nobody is present for (s, 0 = never)</label><input id="screensleep" name="screensleep" type="number" min="0"></div></div>
<button>Save settings</button> <span id="smsg" class="mut"></span></form></section>

<section class="card"><h2>Wi-Fi</h2><p class="mut" id="wstat"></p><form id="wf">
<label for="ssid">Network</label><input id="ssid" name="ssid" list="nets" autocomplete="off"><datalist id="nets"></datalist>
<label for="pass">Password</label><input id="pass" name="pass" type="password">
<button>Connect</button> <button type="button" class="sec" onclick="scan()">Scan</button> <span id="wmsg" class="mut"></span></form></section>

<section class="card"><h2>Firmware</h2><p class="mut" id="fw"></p>
<form id="uf"><label for="bin">Update with a .bin file (presencecam-ota.bin from the releases page)</label><input id="bin" type="file" accept=".bin">
<button>Upload</button> <button type="button" class="sec" onclick="reboot()">Restart</button> <span id="umsg" class="mut"></span></form></section>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const dur=s=>s<60?s+' s':s<3600?Math.floor(s/60)+' min '+(s%60)+' s':Math.floor(s/3600)+' h '+Math.floor(s%3600/60)+' min';
let S=null,formFilled=false;
async function status(){
 try{S=await (await fetch('/api/status')).json()}catch(e){$('sub').textContent='device not reachable, retrying…';return}
 $('name').textContent=S.name;document.title=S.name;
 $('sub').textContent=(S.wifi.connected?S.wifi.ip+' · '+S.wifi.host:'setup mode')+' · up '+dur(S.uptime)+(S.safeMode?' · SAFE MODE':'');
 $('dot').classList.toggle('on',S.present);
 $('state').textContent=!S.presenceSensors?'Sensor readings':S.present?'Someone is here':'Nobody here';
 $('for').textContent=S.presenceSensors?'for '+dur(S.stateFor)+' · '+S.visitsToday+' visit(s) today':'';
 $('readings').innerHTML=S.readings.map(r=>{const v=!r.valid?'—':typeof r.value=='boolean'?(r.value?'yes':'no'):(Math.round(r.value*10)/10)+' '+esc(r.unit);
  const bar=r.max?`<div class="bar"><i style="width:${r.valid?Math.min(100,r.value/r.max*100):0}%"></i></div>`:'';
  return `<div class="kv"><span class="mut">${esc(r.label)}</span><b>${v}</b>${bar}</div>`}).join('')||'<p class="mut">No sensors set up yet.</p>';
 $('sens').textContent=S.sensors.map(x=>x.title+': '+x.status).join(' · ')+' · display: '+S.display;
 $('wstat').textContent=S.wifi.connected?'Connected to '+S.wifi.ssid+' ('+S.wifi.rssi+' dBm). Also reachable as http://'+S.wifi.host:(S.wifi.ssid?'Trying to join '+S.wifi.ssid+'…':'Not set up yet.');
 $('fw').textContent='Version '+S.fw+' · free memory '+Math.round(S.heap/1024)+' KB';
 if(!formFilled){formFilled=true;const st=S.settings;$('sname').value=S.name;$('hold').value=st.hold;$('maxcm').value=st.maxcm;
  $('screenflip').value=st.screenflip?1:0;$('screensleep').value=st.screensleep;$('ssid').value=S.wifi.ssid}
}
function when(t,up){return t>1700000000?new Date(t*1000).toLocaleString():'uptime '+dur(+up)}
async function loadLog(){
 const txt=await (await fetch('/api/log?n=40')).text();
 const rows=txt.trim().split('\n').filter(Boolean).reverse().map(l=>{const f=l.split('|');
  return `<tr><td class="mono">${esc(f[0])}</td><td>${esc(when(+f[1],f[2]))}</td><td>${esc(f[3])}</td><td>${esc(f[4])}</td></tr>`});
 $('log').innerHTML=rows.join('')||'<tr><td colspan="4" class="mut">No events yet.</td></tr>';
 const v=await (await fetch('/api/verify')).json();
 $('vbadge').innerHTML=v.ok?'<span class="badge ok">Verified</span>':'<span class="badge bad">'+v.issues+' problem'+(v.issues>1?'s':'')+'</span>';
 $('vreport').innerHTML=(v.lines?`${v.lines} lines checked (${v.first}–${v.last})`+(v.startsMidChain?', older lines rotated out':''):'')+(v.report?'<pre class="mono">'+esc(v.report)+'</pre>':'');
}
$('sf').onsubmit=async e=>{e.preventDefault();const b=new URLSearchParams(new FormData($('sf')));
 const r=await fetch('/api/settings',{method:'POST',body:b});$('smsg').textContent=r.ok?'Saved.':'Failed.';formFilled=false;status();loadLog()};
$('wf').onsubmit=async e=>{e.preventDefault();$('wmsg').textContent='Joining… if this page stops responding, find the device on the new network (its IP shows on the OLED).';
 await fetch('/api/wifi',{method:'POST',body:new URLSearchParams(new FormData($('wf')))}).catch(()=>{})};
async function scan(){$('wmsg').textContent='Scanning…';const n=await (await fetch('/api/scan')).json();
 $('nets').innerHTML=n.sort((a,b)=>b.rssi-a.rssi).map(x=>`<option value="${esc(x.ssid)}">${x.rssi} dBm${x.open?' open':''}</option>`).join('');$('wmsg').textContent=n.length+' networks found, pick one in the Network box.'}
$('uf').onsubmit=e=>{e.preventDefault();const f=$('bin').files[0];if(!f)return;const fd=new FormData();fd.append('firmware',f,f.name);
 const x=new XMLHttpRequest();x.upload.onprogress=p=>$('umsg').textContent='Uploading '+Math.round(p.loaded*100/p.total)+'%';
 x.onload=()=>{$('umsg').textContent=x.responseText+(x.status==200?'. Reloading in 10 s…':'');if(x.status==200)setTimeout(()=>location.reload(),10000)};
 x.open('POST','/update');x.send(fd)};
async function reboot(){if(confirm('Restart the device?')){await fetch('/api/reboot',{method:'POST'}).catch(()=>{});$('umsg').textContent='Restarting…';setTimeout(()=>location.reload(),8000)}}
async function hardware(){const h=await (await fetch('/api/hardware')).json();const c=h.config;
 $('hwsum').textContent='Board '+c.board+' · display '+h.display+' · layout '+c.layout+' · '+(c.sensors.length?c.sensors.map(s=>s.type).join(', '):'no sensors')+(h.safeMode?' · SAFE MODE (stored config ignored)':'')+' · config '+h.hash;
 $('pins').innerHTML=h.pins.map(p=>`<tr><td>${esc(p.what)}</td><td class="mono">GPIO${p.gpio}</td></tr>`).join('');$('hwjson').textContent=JSON.stringify(c,null,1)}
status();loadLog();hardware();setInterval(status,1000);setInterval(loadLog,15000);
</script></body></html>)HTML";
