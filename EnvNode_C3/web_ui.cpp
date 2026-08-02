#include "web_ui.h"
#include "net_ota.h"
#include "settings.h"
#include "sd_logger.h"
#include "rtc_time.h"
#include "comfort.h"

// ---------------------------------------------------------------------
//  Dashboard (PROGMEM: sta in flash, non in RAM). Niente CDN/librerie
//  esterne: grafici canvas fatti a mano in vanilla JS, come da convenzione
//  del resto del repo (vedi net_ota.cpp/web_ui.cpp di WSOLED_XIAO).
//
//  I dati grezzi (ts,T,H) arrivano da /api/giorno; comfort score/etichetta/
//  in-range si ricalcolano NEL BROWSER contro /api/config corrente (stessa
//  formula di comfort.h, duplicata qui in JS): cambiare le soglie da web
//  ricolora anche lo storico gia' scaricato, senza un nuovo giro di rete e
//  senza costo aggiuntivo sul device.
// ---------------------------------------------------------------------
static const char DASHBOARD_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EnvNode-C3</title><style>
 :root{color-scheme:dark}
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;display:flex;justify-content:center}
 .wrap{max-width:820px;width:100%}
 h1{font-size:1.1rem;margin:0 0 .8rem}
 .card{background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1rem;margin-bottom:1rem}
 .card h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.05em;color:#9aa;margin:0 0 .7rem}
 .tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:.7rem}
 .tile{background:#191919;border-radius:10px;padding:.7rem .9rem}
 .tile .lbl{font-size:.72rem;color:#9aa;text-transform:uppercase;letter-spacing:.04em}
 .tile .val{font-size:1.5rem;margin-top:.2rem}
 .tile .sub{font-size:.72rem;color:#8a8a8a;margin-top:.15rem}
 canvas{width:100%;height:180px;display:block;background:#161615;border-radius:8px}
 select,input{background:#111;color:#eee;border:1px solid #444;border-radius:6px;padding:.4rem;width:100%}
 label{display:block;font-size:.78rem;color:#9aa;margin:.5rem 0 .2rem}
 .row{display:flex;gap:.8rem;flex-wrap:wrap}
 .row>div{flex:1 1 110px}
 button{padding:.55rem .9rem;border:0;border-radius:8px;background:#3987e5;color:#fff;font-size:.9rem;cursor:pointer;margin:.6rem .4rem 0 0}
 button.sec{background:#374151}
 button.dan{background:#b91c1c}
 .muted{color:#8a8a8a;font-size:.78rem;line-height:1.4}
 .legend{font-size:.75rem;color:#9aa;margin-top:.4rem}
 .dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:.3rem;vertical-align:middle}
 a{color:#3987e5}
 #tooltip{position:fixed;pointer-events:none;background:#000;border:1px solid #444;border-radius:6px;padding:.3rem .5rem;font-size:.72rem;color:#eee;display:none;white-space:nowrap;z-index:5}
</style></head><body><div class="wrap">
<h1 id="titolo">EnvNode-C3</h1>

<div class="card"><h2>Stato</h2>
 <div class="tiles" id="tiles"></div>
</div>

<div class="card">
 <h2>Giorno</h2>
 <select id="giorno"></select>
 <button id="baggiorna" class="sec">Aggiorna</button>
 <button id="bscarica" class="sec">Scarica CSV</button>
 <button id="belimina" class="dan">Elimina giorno</button>
 <p class="muted" id="eliminaMsg"></p>
</div>

<div class="card"><h2>Temperatura</h2><canvas id="cTemp" width="760" height="180"></canvas></div>
<div class="card"><h2>Umidita'</h2><canvas id="cHum" width="760" height="180"></canvas></div>
<div class="card"><h2>Comfort (scatter temp/umidita')</h2><canvas id="cScatter" width="760" height="220"></canvas>
 <div class="legend"><span class="dot" style="background:#0ca30c"></span>dentro range
  <span class="dot" style="background:#d03b3b;margin-left:.8rem"></span>fuori range</div>
</div>
<div class="card"><h2>Comfort score nel tempo</h2><canvas id="cScore" width="760" height="180"></canvas></div>

<div class="card"><h2>Impostazioni</h2>
 <div class="row">
  <div><label>Nome nodo</label><input id="cfgNodo" maxlength="23"></div>
  <div><label>Intervallo log (s)</label><input id="cfgLogInt" type="number" min="5" max="3600"></div>
  <div><label>Rotazione pagine OLED (s)</label><input id="cfgPageSec" type="number" min="2" max="30"></div>
 </div>
 <div class="row">
  <div><label>Temp min comfort (C)</label><input id="cfgTMin" type="number" step="0.5"></div>
  <div><label>Temp max comfort (C)</label><input id="cfgTMax" type="number" step="0.5"></div>
  <div><label>Umid. min comfort (%)</label><input id="cfgHMin" type="number" step="1"></div>
  <div><label>Umid. max comfort (%)</label><input id="cfgHMax" type="number" step="1"></div>
 </div>
 <div class="row"><div><label>Fuso orario (stringa POSIX TZ)</label><input id="cfgTz"></div></div>
 <button id="bsalva">Applica</button>
 <a href="/update" style="margin-left:.6rem">Aggiorna firmware</a>
 <p class="muted" id="cfgMsg"></p>
</div>

</div><div id="tooltip"></div>
<script>
const $=id=>document.getElementById(id);
const tip=$('tooltip');
let cfg=null;

function fmtTime(ts){
  const d=new Date(ts*1000);
  return d.toLocaleString('it-IT',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'});
}

// Stessa formula di comfort.h (vedi il commento in testa a questo file):
// duplicata qui per ricalcolare lo storico nel browser senza round-trip.
function comfortEval(t,h,c){
  const distT = t<c.t_min ? c.t_min-t : (t>c.t_max ? t-c.t_max : 0);
  const distH = h<c.h_min ? c.h_min-h : (h>c.h_max ? h-c.h_max : 0);
  const pT = Math.min(distT/6*50, 50);
  const pH = Math.min(distH/30*50, 50);
  const score = Math.max(0, Math.min(100, Math.round(100-pT-pH)));
  const inRange = t>=c.t_min && t<=c.t_max && h>=c.h_min && h<=c.h_max;
  let label;
  if (score>=80) label='Confortevole';
  else if (score>=60) label='Accettabile';
  else {
    const tOff=pT>0, hOff=pH>0;
    if (tOff && hOff) label='Scomodo';
    else if (tOff) label = t<c.t_min ? 'Troppo freddo' : 'Troppo caldo';
    else label = h<c.h_min ? 'Troppo secco' : 'Troppo umido';
  }
  return {score,label,inRange};
}

function tile(lbl,val,sub){
  return '<div class="tile"><div class="lbl">'+lbl+'</div><div class="val">'+val+'</div>'+
         (sub?('<div class="sub">'+sub+'</div>'):'')+'</div>';
}

function refreshStato(){
  fetch('/api/stato').then(r=>r.json()).then(s=>{
    $('titolo').textContent = s.nodo+' · fw '+s.fw;
    const t = s.temp!=null ? s.temp.toFixed(1)+'°C' : '--';
    const h = s.hum!=null ? s.hum.toFixed(0)+'%' : '--';
    const comfort = s.comfort_label!=null ? (s.comfort_label+' ('+s.comfort_score+')') : '--';
    const sd = s.sd ? (s.sd_liberi_mb+'/'+s.sd_totali_mb+' MB') : (s.sd_errore||'assente');
    $('tiles').innerHTML =
      tile('Temperatura', t, s.temp_max!=null?('min '+s.temp_min.toFixed(1)+' · max '+s.temp_max.toFixed(1)):'') +
      tile('Umidita\'', h) +
      tile('Comfort', comfort) +
      tile('SD', sd, s.record_oggi+' oggi · '+s.record_totali+' tot') +
      tile('Ora', s.ora, s.ora_fonte) +
      tile('Rete', s.wifi ? (s.ip+' ('+s.rssi+' dBm)') : 'non connesso');
  }).catch(()=>{});
}

function refreshConfig(){
  return fetch('/api/config').then(r=>r.json()).then(c=>{
    cfg=c;
    $('cfgNodo').value=c.nodo; $('cfgLogInt').value=c.log_int_s; $('cfgPageSec').value=c.page_sec;
    $('cfgTMin').value=c.t_min; $('cfgTMax').value=c.t_max;
    $('cfgHMin').value=c.h_min; $('cfgHMax').value=c.h_max;
    $('cfgTz').value=c.tz;
  });
}

function niceMinMax(vals){
  let mn=Math.min(...vals), mx=Math.max(...vals);
  if (mn===mx){ mn-=1; mx+=1; }
  const pad=(mx-mn)*0.12;
  return [mn-pad, mx+pad];
}

function drawLineChart(canvas, points, color, unit){
  const ctx=canvas.getContext('2d');
  const W=canvas.width, H=canvas.height;
  ctx.clearRect(0,0,W,H);
  const pad={l:38,r:12,t:10,b:10};
  if (!points.length){
    ctx.fillStyle='#8a8785'; ctx.font='12px system-ui';
    ctx.fillText('nessun dato per questo giorno', pad.l, H/2);
    canvas.onmousemove=null; canvas.onmouseleave=null;
    return;
  }
  const xs=points.map(p=>p.x), ys=points.map(p=>p.y);
  const xMin=Math.min(...xs), xMax=Math.max(...xs);
  const [y0,y1]=niceMinMax(ys);
  const xToPx=x=> pad.l + (W-pad.l-pad.r) * (xMax>xMin ? (x-xMin)/(xMax-xMin) : 0.5);
  const yToPx=y=> H-pad.b - (H-pad.t-pad.b) * ((y-y0)/(y1-y0));

  ctx.strokeStyle='#2c2c2a'; ctx.fillStyle='#898781'; ctx.font='10px system-ui'; ctx.lineWidth=1;
  for(let i=0;i<=3;i++){
    const gy=pad.t+(H-pad.t-pad.b)*i/3;
    ctx.beginPath(); ctx.moveTo(pad.l,gy); ctx.lineTo(W-pad.r,gy); ctx.stroke();
    ctx.fillText((y1-(y1-y0)*i/3).toFixed(1), 2, gy+3);
  }

  ctx.strokeStyle=color; ctx.lineWidth=2; ctx.beginPath();
  points.forEach((p,i)=>{ const px=xToPx(p.x), py=yToPx(p.y); if(i===0) ctx.moveTo(px,py); else ctx.lineTo(px,py); });
  ctx.stroke();

  canvas.onmousemove=(ev)=>{
    const rect=canvas.getBoundingClientRect();
    const mx=(ev.clientX-rect.left)*(canvas.width/rect.width);
    let best=points[0], bestD=Infinity;
    points.forEach(p=>{ const d=Math.abs(xToPx(p.x)-mx); if(d<bestD){bestD=d;best=p;} });
    tip.style.display='block';
    tip.style.left=(ev.clientX+12)+'px'; tip.style.top=(ev.clientY-10)+'px';
    tip.textContent = fmtTime(best.x)+'  '+best.y.toFixed(1)+unit;
  };
  canvas.onmouseleave=()=>{ tip.style.display='none'; };
}

function drawScatter(canvas, points, band){
  const ctx=canvas.getContext('2d');
  const W=canvas.width, H=canvas.height;
  ctx.clearRect(0,0,W,H);
  const pad={l:38,r:12,t:10,b:22};
  if (!points.length){
    ctx.fillStyle='#8a8785'; ctx.font='12px system-ui';
    ctx.fillText('nessun dato per questo giorno', pad.l, H/2);
    canvas.onmousemove=null; canvas.onmouseleave=null;
    return;
  }
  const xs=points.map(p=>p.t), ys=points.map(p=>p.h);
  const [x0,x1]=niceMinMax(xs.concat([band.t_min,band.t_max]));
  const [y0,y1]=niceMinMax(ys.concat([band.h_min,band.h_max]));
  const xToPx=x=> pad.l + (W-pad.l-pad.r) * ((x-x0)/(x1-x0));
  const yToPx=y=> H-pad.b - (H-pad.t-pad.b) * ((y-y0)/(y1-y0));

  ctx.strokeStyle='#2c2c2a'; ctx.fillStyle='#898781'; ctx.font='10px system-ui'; ctx.lineWidth=1;
  for(let i=0;i<=3;i++){
    const gy=pad.t+(H-pad.t-pad.b)*i/3;
    ctx.beginPath(); ctx.moveTo(pad.l,gy); ctx.lineTo(W-pad.r,gy); ctx.stroke();
    ctx.fillText((y1-(y1-y0)*i/3).toFixed(0), 2, gy+3);
  }

  ctx.setLineDash([4,3]); ctx.strokeStyle='#9aa'; ctx.lineWidth=1;
  ctx.strokeRect(xToPx(band.t_min), yToPx(band.h_max),
                 xToPx(band.t_max)-xToPx(band.t_min), yToPx(band.h_min)-yToPx(band.h_max));
  ctx.setLineDash([]);

  points.forEach(p=>{
    const inRange = p.t>=band.t_min && p.t<=band.t_max && p.h>=band.h_min && p.h<=band.h_max;
    ctx.fillStyle = inRange ? '#0ca30c' : '#d03b3b';
    ctx.beginPath(); ctx.arc(xToPx(p.t), yToPx(p.h), 4, 0, 2*Math.PI); ctx.fill();
  });

  canvas.onmousemove=(ev)=>{
    const rect=canvas.getBoundingClientRect();
    const mx=(ev.clientX-rect.left)*(canvas.width/rect.width);
    const my=(ev.clientY-rect.top)*(canvas.height/rect.height);
    let best=points[0], bestD=Infinity;
    points.forEach(p=>{ const dx=xToPx(p.t)-mx, dy=yToPx(p.h)-my, d=dx*dx+dy*dy; if(d<bestD){bestD=d;best=p;} });
    tip.style.display='block';
    tip.style.left=(ev.clientX+12)+'px'; tip.style.top=(ev.clientY-10)+'px';
    tip.textContent = fmtTime(best.ts)+'  '+best.t.toFixed(1)+'°C, '+best.h.toFixed(0)+'%';
  };
  canvas.onmouseleave=()=>{ tip.style.display='none'; };
}

function loadGiorno(date){
  if(!date || !cfg) return;
  fetch('/api/giorno?d='+encodeURIComponent(date)).then(r=>r.json()).then(rows=>{
    // Il CSV puo' avere righe fuori ordine cronologico (es. un riavvio che
    // riparte da una stima di orario prima che l'NTP la corregga): un grafico
    // a linee deve comunque avanzare nel tempo, altrimenti la linea "torna
    // indietro" ogni volta che incontra una riga con timestamp minore della
    // precedente. Si ordina qui, una volta, invece di fidarsi dell'ordine del file.
    rows = rows.slice().sort((a,b)=>a[0]-b[0]);
    const tempPts    = rows.map(r=>({x:r[0],y:r[1]}));
    const humPts     = rows.map(r=>({x:r[0],y:r[2]}));
    const scatterPts = rows.map(r=>({ts:r[0],t:r[1],h:r[2]}));
    const scorePts   = rows.map(r=>{ const c=comfortEval(r[1],r[2],cfg); return {x:r[0],y:c.score}; });
    drawLineChart($('cTemp'), tempPts, '#d95926', '°C');
    drawLineChart($('cHum'), humPts, '#3987e5', '%');
    drawScatter($('cScatter'), scatterPts, cfg);
    drawLineChart($('cScore'), scorePts, '#199e70', '');
  });
}

function loadGiorni(){
  return fetch('/api/giorni').then(r=>r.json()).then(list=>{
    const sel=$('giorno');
    sel.innerHTML = list.map(d=>'<option value="'+d+'">'+d+'</option>').join('');
    if (list.length){ sel.value=list[list.length-1]; loadGiorno(sel.value); }
  });
}

$('giorno').addEventListener('change', ()=>loadGiorno($('giorno').value));
$('baggiorna').addEventListener('click', ()=>loadGiorno($('giorno').value));

$('bscarica').addEventListener('click', ()=>{
  const d = $('giorno').value;
  if (!d) return;
  window.location = '/api/scarica?d='+encodeURIComponent(d);
});

$('belimina').addEventListener('click', ()=>{
  const d = $('giorno').value;
  if (!d) return;
  if (!confirm('Eliminare definitivamente il log di '+d+'? Non si puo\' annullare.')) return;
  fetch('/api/elimina?d='+encodeURIComponent(d), {method:'POST'}).then(r=>r.json()).then(res=>{
    $('eliminaMsg').textContent = res.ok ? (d+' eliminato.') : ('Errore: '+res.errore);
    if (res.ok) { refreshStato(); loadGiorni(); }
  });
});

$('bsalva').addEventListener('click', ()=>{
  const p = new URLSearchParams({
    nodo: $('cfgNodo').value, log_int_s: $('cfgLogInt').value, page_sec: $('cfgPageSec').value,
    t_min: $('cfgTMin').value, t_max: $('cfgTMax').value,
    h_min: $('cfgHMin').value, h_max: $('cfgHMax').value, tz: $('cfgTz').value
  });
  fetch('/api/config?'+p.toString(), {method:'POST'}).then(r=>r.json()).then(res=>{
    $('cfgMsg').textContent = res.ok ? 'Salvato.' : ('Errore: '+res.errore);
    if (res.ok) refreshConfig().then(()=>{ if ($('giorno').value) loadGiorno($('giorno').value); });
  });
});

refreshStato();
setInterval(refreshStato, 5000);
refreshConfig().then(loadGiorni);
</script>
</body></html>
)HTML";

// =======================================================================
//  Helper: JSON minimale a mano (stesso stile "niente librerie extra" del
//  resto del repo). Solo escaping di virgolette/backslash/controlli: le
//  stringhe che ci finiscono sono nomi nodo/etichette/errori, mai input
//  binario.
// =======================================================================
static void appendJsonString(String& out, const char* s) {
  out += '"';
  if (s) {
    // Limite di lunghezza indipendente dalla terminazione: se mai un
    // chiamante passasse un buffer non terminato (vedi il commento su
    // rtctime_format in rtc_time.cpp), questo evita comunque una lettura
    // indefinita oltre il buffer invece di bloccare il web server.
    for (size_t i = 0; i < 256 && s[i] != '\0'; i++) {
      char c = s[i];
      if (c == '"' || c == '\\') { out += '\\'; out += c; }
      else if ((unsigned char)c >= 0x20) out += c;
    }
  }
  out += '"';
}

// ---------------------------------------------------------------------
//  GET /
// ---------------------------------------------------------------------
static void handleRoot() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", DASHBOARD_PAGE);
}

// ---------------------------------------------------------------------
//  GET /api/stato
// ---------------------------------------------------------------------
static void handleApiStato() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const AppSettings& cfg = settings_get();
  char oraBuf[24] = "--";
  rtctime_format(rtctime_now(), "%Y-%m-%d %H:%M:%S", oraBuf, sizeof(oraBuf));

  String json;
  json.reserve(620);
  json += '{';
  json += "\"nodo\":";     appendJsonString(json, cfg.nodeName);       json += ',';
  json += "\"fw\":";       appendJsonString(json, app_fw_version());  json += ',';
  json += "\"wifi\":";     json += (net_isConnected() ? "true" : "false"); json += ',';
  json += "\"ip\":";       appendJsonString(json, net_isConnected() ? net_ip().c_str() : ""); json += ',';
  json += "\"rssi\":" + String(net_isConnected() ? net_rssi() : 0) + ",";
  json += "\"ora\":";      appendJsonString(json, oraBuf);            json += ',';
  json += "\"ora_fonte\":"; appendJsonString(json, rtctime_source()); json += ',';

  if (app_has_reading()) {
    float t = app_temp_now(), h = app_hum_now();
    ComfortResult c = comfort_eval(t, h, cfg.comfort);
    char buf[24];

    json += "\"temp\":" + String(t, 1) + ",";
    json += "\"hum\":"  + String(h, 1) + ",";

    json += "\"temp_min\":" + String(app_temp_min(), 1) + ",";
    rtctime_format(app_temp_min_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"temp_min_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"temp_max\":" + String(app_temp_max(), 1) + ",";
    rtctime_format(app_temp_max_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"temp_max_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"hum_min\":" + String(app_hum_min(), 1) + ",";
    rtctime_format(app_hum_min_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"hum_min_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"hum_max\":" + String(app_hum_max(), 1) + ",";
    rtctime_format(app_hum_max_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"hum_max_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"comfort_score\":" + String(c.score) + ",";
    json += "\"comfort_label\":"; appendJsonString(json, c.label); json += ',';
    json += "\"comfort_in_range\":"; json += (c.inRange ? "true" : "false"); json += ',';
  } else {
    json += "\"temp\":null,\"hum\":null,"
            "\"temp_min\":null,\"temp_min_ora\":null,"
            "\"temp_max\":null,\"temp_max_ora\":null,"
            "\"hum_min\":null,\"hum_min_ora\":null,"
            "\"hum_max\":null,\"hum_max_ora\":null,"
            "\"comfort_score\":null,\"comfort_label\":null,\"comfort_in_range\":null,";
  }

  json += "\"sd\":"; json += (sd_mounted() ? "true" : "false"); json += ',';
  json += "\"sd_errore\":"; appendJsonString(json, sd_mounted() ? "" : sd_last_error()); json += ',';
  json += "\"sd_liberi_mb\":"  + String((unsigned long)sd_free_mb()) + ",";
  json += "\"sd_totali_mb\":"  + String((unsigned long)sd_total_mb()) + ",";
  json += "\"record_oggi\":"   + String(sd_record_count_today()) + ",";
  json += "\"record_totali\":" + String(sd_record_count_total()) + ",";
  json += "\"dht_errori\":"    + String(app_dht_errors()) + ",";
  json += "\"uptime\":"        + String(millis() / 1000) + ",";
  json += "\"heap\":"          + String(ESP.getFreeHeap());
  json += '}';

  net_server().send(200, "application/json", json);
}

// ---------------------------------------------------------------------
//  GET /api/giorni
// ---------------------------------------------------------------------
static void appendDayCb(const char* isoDate, size_t /*fileSizeBytes*/, void* arg) {
  String* out = (String*)arg;
  if (out->length() > 1) *out += ',';
  appendJsonString(*out, isoDate);
}

static void handleApiGiorni() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  String json = "[";
  sd_list_days(appendDayCb, &json, 0);
  json += ']';
  net_server().send(200, "application/json", json);
}

// ---------------------------------------------------------------------
//  GET /api/giorno?d=YYYY-MM-DD — risposta in streaming, mai l'intero
//  file in RAM (vedi sd_read_day in sd_logger.cpp).
// ---------------------------------------------------------------------
struct GiornoStreamCtx {
  WebServer* server;
  bool       first;
};

static void streamRowCb(time_t ts, float t, float h, void* arg) {
  GiornoStreamCtx* ctx = (GiornoStreamCtx*)arg;
  char chunk[48];
  int n = snprintf(chunk, sizeof(chunk), "%s[%lu,%.1f,%.1f]",
                    ctx->first ? "" : ",", (unsigned long)ts, t, h);
  ctx->first = false;
  ctx->server->sendContent(chunk, n);
}

static void handleApiGiorno() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("d") || !sd_name_is_safe(srv.arg("d").c_str())) {
    srv.send(400, "application/json", "[]");
    return;
  }
  String date = srv.arg("d");

  srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv.send(200, "application/json", "");
  srv.sendContent("[");
  GiornoStreamCtx ctx{ &srv, true };
  sd_read_day(date.c_str(), streamRowCb, &ctx);
  srv.sendContent("]");
}

// ---------------------------------------------------------------------
//  GET /api/scarica?d=YYYY-MM-DD — scarica il CSV grezzo di un giorno.
// ---------------------------------------------------------------------
static void handleApiScarica() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("d") || !sd_name_is_safe(srv.arg("d").c_str())) {
    srv.send(400, "text/plain", "data non valida");
    return;
  }
  String date = srv.arg("d");

  File f = sd_open_day(date.c_str());
  if (!f) {
    srv.send(404, "text/plain", "file non trovato");
    return;
  }

  char header[40];
  snprintf(header, sizeof(header), "attachment; filename=\"%s.csv\"", date.c_str());
  srv.sendHeader("Content-Disposition", header);
  srv.streamFile(f, "text/csv");
  f.close();
}

// ---------------------------------------------------------------------
//  POST /api/elimina?d=YYYY-MM-DD — elimina il file di log di un giorno.
// ---------------------------------------------------------------------
static void handleApiElimina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("d") || !sd_name_is_safe(srv.arg("d").c_str())) {
    srv.send(400, "application/json", "{\"ok\":false,\"errore\":\"data non valida\"}");
    return;
  }

  bool ok = sd_delete_day(srv.arg("d").c_str());
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  if (!ok) { json += ",\"errore\":"; appendJsonString(json, sd_last_error()); }
  json += '}';
  srv.send(ok ? 200 : 400, "application/json", json);
}

// ---------------------------------------------------------------------
//  GET /api/config
// ---------------------------------------------------------------------
static void handleApiConfigGet() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  const AppSettings& cfg = settings_get();

  String json = "{";
  json += "\"nodo\":"; appendJsonString(json, cfg.nodeName); json += ',';
  json += "\"log_int_s\":" + String(cfg.logIntervalS) + ",";
  json += "\"page_sec\":"  + String(cfg.pageSeconds) + ",";
  json += "\"t_min\":" + String(cfg.comfort.tMin, 1) + ",";
  json += "\"t_max\":" + String(cfg.comfort.tMax, 1) + ",";
  json += "\"h_min\":" + String(cfg.comfort.hMin, 1) + ",";
  json += "\"h_max\":" + String(cfg.comfort.hMax, 1) + ",";
  json += "\"tz\":"; appendJsonString(json, cfg.tz);
  json += '}';

  net_server().send(200, "application/json", json);
}

// ---------------------------------------------------------------------
//  POST /api/config — ogni campo e' opzionale/indipendente (stesso
//  pattern di handleConfig() in WSOLED_XIAO/web_ui.cpp).
// ---------------------------------------------------------------------
static void handleApiConfigPost() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  String err;

  if (srv.hasArg("nodo") && !settings_set_node_name(srv.arg("nodo").c_str())) {
    err = "nome nodo non valido";
  }
  if (err.length() == 0 && srv.hasArg("log_int_s") &&
      !settings_set_log_interval_s((uint32_t)srv.arg("log_int_s").toInt())) {
    err = "intervallo di log fuori range (5-3600s)";
  }
  if (err.length() == 0 && srv.hasArg("page_sec") &&
      !settings_set_page_seconds((uint32_t)srv.arg("page_sec").toInt())) {
    err = "rotazione pagine fuori range (2-30s)";
  }
  if (err.length() == 0 &&
      (srv.hasArg("t_min") || srv.hasArg("t_max") || srv.hasArg("h_min") || srv.hasArg("h_max"))) {
    ComfortConfig band = settings_get().comfort;
    if (srv.hasArg("t_min")) band.tMin = srv.arg("t_min").toFloat();
    if (srv.hasArg("t_max")) band.tMax = srv.arg("t_max").toFloat();
    if (srv.hasArg("h_min")) band.hMin = srv.arg("h_min").toFloat();
    if (srv.hasArg("h_max")) band.hMax = srv.arg("h_max").toFloat();
    if (!settings_set_comfort(band)) err = "banda di comfort non valida";
  }
  if (err.length() == 0 && srv.hasArg("tz")) {
    if (!settings_set_tz(srv.arg("tz").c_str())) {
      err = "fuso orario non valido";
    } else {
      rtctime_begin(settings_get().tz);   // riapplica subito, non solo al prossimo riavvio
    }
  }

  String json = "{\"ok\":";
  json += (err.length() == 0) ? "true" : "false";
  if (err.length() > 0) { json += ",\"errore\":"; appendJsonString(json, err.c_str()); }
  json += '}';

  srv.send(err.length() == 0 ? 200 : 400, "application/json", json);
}

// ---------------------------------------------------------------------
void web_ui_begin() {
  WebServer& srv = net_server();
  srv.on("/", HTTP_GET, handleRoot);
  srv.on("/api/stato", HTTP_GET, handleApiStato);
  srv.on("/api/giorni", HTTP_GET, handleApiGiorni);
  srv.on("/api/giorno", HTTP_GET, handleApiGiorno);
  srv.on("/api/scarica", HTTP_GET, handleApiScarica);
  srv.on("/api/elimina", HTTP_POST, handleApiElimina);
  srv.on("/api/config", HTTP_GET, handleApiConfigGet);
  srv.on("/api/config", HTTP_POST, handleApiConfigPost);
}
