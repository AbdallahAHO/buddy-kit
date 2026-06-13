// Single-page fleet dashboard. Server sends the shell; the page asks for the
// admin key once (kept in localStorage) and drives the JSON API with it.
export function dashboardHtml(): string {
  return `<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>buddy fleet</title><style>
:root{color-scheme:dark}
body{font-family:-apple-system,system-ui,sans-serif;background:#0d0d0f;color:#e7e7ea;margin:0;padding:24px}
h1{font-size:20px;margin:0 0 2px}.sub{color:#888;font-size:13px;margin:0 0 20px}
table{width:100%;border-collapse:collapse;font-size:13px}
th{text-align:left;color:#888;font-weight:500;padding:8px 10px;border-bottom:1px solid #222}
td{padding:10px;border-bottom:1px solid #1a1a1d}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
.on{background:#5fbf77}.off{background:#555}
.mono{font-family:ui-monospace,monospace;color:#aaa}
.bar{display:flex;gap:8px;align-items:center;margin:18px 0;flex-wrap:wrap}
input,button,select{background:#1c1c1f;color:#e7e7ea;border:1px solid #333;border-radius:7px;padding:8px 10px;font-size:13px}
button{background:#d97757;border:0;font-weight:600;cursor:pointer}
button.ghost{background:#1c1c1f;border:1px solid #333;font-weight:400}
#msg{font-size:13px;color:#888;min-height:18px}
</style></head><body>
<h1>buddy fleet</h1><p class=sub>registered devices · push firmware over the air</p>
<div class=bar>
  <select id=fw></select>
  <button onclick=broadcast()>push update to all</button>
  <button class=ghost onclick=load()>refresh</button>
  <button class=ghost onclick=setKey()>set admin key</button>
  <span id=msg></span>
</div>
<table><thead><tr><th>device</th><th>model</th><th>fw</th><th>last seen</th><th>ip</th><th></th></tr></thead>
<tbody id=rows></tbody></table>
<script>
const K=()=>localStorage.getItem('bk')||'';
function setKey(){const k=prompt('admin key');if(k)localStorage.setItem('bk',k);load();}
const hdr=()=>({Authorization:'Bearer '+K()});
const ago=t=>{if(!t)return '—';const s=(Date.now()-t)/1000;return s<60?Math.round(s)+'s':s<3600?Math.round(s/60)+'m':Math.round(s/3600)+'h';};
// Device-supplied fields (id/model/fw/ip come from headers an authed device sets)
// must be escaped before going into innerHTML — otherwise a compromised device
// could XSS the admin and steal the admin key from localStorage.
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function load(){
  const m=document.getElementById('msg');m.textContent='';
  try{
    const d=await(await fetch('/v1/devices',{headers:hdr()})).json();
    document.getElementById('rows').innerHTML=(d.devices||[]).map(x=>{
      const online=x.last_seen&&Date.now()-x.last_seen<20000;
      return '<tr><td><span class="dot '+(online?'on':'off')+'"></span>'+esc(x.id)+
        '</td><td>'+esc(x.model||'—')+'</td><td class=mono>'+esc(x.fw_version||'—')+
        '</td><td>'+ago(x.last_seen)+'</td><td class=mono>'+esc(x.last_ip||'—')+
        '</td><td><button class=ghost data-id="'+esc(x.id)+'" onclick="reboot(this.dataset.id)">reboot</button></td></tr>';
    }).join('')||'<tr><td colspan=6 style=color:#666>no devices yet</td></tr>';
    const f=await(await fetch('/v1/firmware',{headers:hdr()})).json();
    document.getElementById('fw').innerHTML=(f.firmware||[]).map(v=>'<option>'+v.version+'</option>').join('')||'<option disabled>no firmware</option>';
  }catch(e){m.textContent='auth? click "set admin key"';}
}
async function broadcast(){
  const version=document.getElementById('fw').value;if(!version)return;
  if(!confirm('push '+version+' to ALL devices?'))return;
  const r=await(await fetch('/v1/ota/broadcast',{method:'POST',headers:{...hdr(),'content-type':'application/json'},body:JSON.stringify({version})})).json();
  document.getElementById('msg').textContent='queued OTA '+version+' to '+r.queued+' device(s)';
}
async function reboot(id){
  await fetch('/v1/devices/'+encodeURIComponent(id)+'/cmd',{method:'POST',headers:hdr(),body:JSON.stringify({cmd:'reboot'})});
  document.getElementById('msg').textContent='reboot queued for '+id;
}
load();setInterval(load,5000);
</script></body></html>`;
}
