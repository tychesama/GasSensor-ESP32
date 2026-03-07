/*
  ESP32 Sensor Monitor — DHT11 + MQ135
  AP mode — creates its own WiFi network
  Routes: / (dashboard) | /data | /history | /download (CSV)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ============================================================
// ▶  EDIT THESE
// ============================================================
const char* AP_SSID = "ESP32-SENSORS";
const char* AP_PASS = "12345678";        // min 8 chars; "" = open network
// ============================================================

WebServer server(80);

// ── DHT11 ────────────────────────────────────────────────
#define DHTPIN  33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ── MQ135 ────────────────────────────────────────────────
#define GAS_PIN 32

// ── Latest readings ───────────────────────────────────────
float latestT = 0;
float latestH = 0;
int   latestG = 0;

// ── Circular history buffer ───────────────────────────────
struct Sample { unsigned long ms; float t; float h; int g; };
const int  HIST_N = 120;
Sample     hist[HIST_N];
int        histIdx  = 0;
bool       histFull = false;

// ── Timing ───────────────────────────────────────────────
unsigned long lastSample = 0;
const unsigned long SAMPLE_MS = 2000;   // DHT11 min reliable interval

// ── Helpers ──────────────────────────────────────────────
String jf(float v, int d = 1) { return String(v, d); }

// ── /data ────────────────────────────────────────────────
void handleData() {
  String out = "{";
  out += "\"temp\":"   + jf(latestT, 1) + ",";
  out += "\"hum\":"    + jf(latestH, 0) + ",";
  out += "\"gas\":"    + String(latestG) + ",";
  out += "\"uptime\":" + String(millis());
  out += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

// ── /history ─────────────────────────────────────────────
void handleHistory() {
  int count = histFull ? HIST_N : histIdx;
  int start = histFull ? histIdx : 0;
  String out = "[";
  for (int i = 0; i < count; i++) {
    int j = (start + i) % HIST_N;
    if (i) out += ",";
    out += "{\"ms\":"   + String(hist[j].ms)   + ","
           "\"temp\":"  + jf(hist[j].t, 1)     + ","
           "\"hum\":"   + jf(hist[j].h, 0)     + ","
           "\"gas\":"   + String(hist[j].g)     + "}";
  }
  out += "]";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

// ── /download  sends millis; browser converts to real datetime ──
void handleDownload() {
  // Send JSON array so the browser can stamp real wall-clock times
  int count = histFull ? HIST_N : histIdx;
  int start = histFull ? histIdx : 0;
  String out = "[";
  for (int i = 0; i < count; i++) {
    int j = (start + i) % HIST_N;
    if (i) out += ",";
    out += "{\"ms\":"  + String(hist[j].ms)  + ","
           "\"temp\":" + jf(hist[j].t, 1)    + ","
           "\"hum\":"  + jf(hist[j].h, 0)    + ","
           "\"gas\":"  + String(hist[j].g)    + "}";
  }
  out += "]";
  // Pass current uptime so browser can compute offset
  server.sendHeader("X-Uptime", String(millis()));
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

// ── / (dashboard HTML) ───────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"HTMLEOF(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3.0.0/dist/chartjs-adapter-date-fns.bundle.min.js"></script>
<style>
@import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Rajdhani:wght@400;600;700&display=swap');
:root{
  --bg:#0a0e1a;--surf:#111827;--bdr:#1e2d40;
  --blue:#00c8ff;--green:#00ff9d;--orange:#ff7b00;--red:#ff2e5b;
  --text:#d4e6f1;--muted:#5a7a8a;
  --mono:'Share Tech Mono',monospace;--ui:'Rajdhani',sans-serif;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:var(--ui);overflow-x:hidden}
body::before{
  content:'';position:fixed;inset:0;pointer-events:none;z-index:0;
  background:repeating-linear-gradient(0deg,transparent,transparent 3px,
    rgba(0,200,255,0.012) 3px,rgba(0,200,255,0.012) 4px);
}

/* HEADER */
header{
  position:relative;z-index:2;
  display:flex;align-items:center;justify-content:space-between;
  padding:11px 14px;border-bottom:1px solid var(--bdr);
  background:linear-gradient(90deg,rgba(0,200,255,0.07) 0%,transparent 70%);
}
.logo{display:flex;align-items:center;gap:9px}
.logo-icon{
  width:30px;height:30px;border:2px solid var(--blue);border-radius:6px;
  display:grid;place-items:center;font-size:15px;
  box-shadow:0 0 10px rgba(0,200,255,0.45);
  animation:glow 2.5s ease-in-out infinite;
}
@keyframes glow{0%,100%{box-shadow:0 0 10px rgba(0,200,255,0.4)}50%{box-shadow:0 0 20px rgba(0,200,255,0.8)}}
h1{font-size:15px;font-weight:700;letter-spacing:3px;text-transform:uppercase;color:var(--blue)}
.hdr-r{display:flex;align-items:center;gap:9px}

/* status indicator — swaps between LIVE / WARMING UP / NO SIGNAL */
.sig{display:flex;align-items:center;gap:6px}
.sig-dot{width:7px;height:7px;border-radius:50%;animation:blink 1.2s ease-in-out infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.15}}
.sig-lbl{font-size:10px;letter-spacing:2px}

.dl-btn{
  font-family:var(--mono);font-size:11px;letter-spacing:1px;
  padding:7px 12px;background:transparent;color:var(--blue);
  border:1px solid var(--blue);border-radius:4px;cursor:pointer;transition:all .2s;
  -webkit-tap-highlight-color:transparent;
}
.dl-btn:active{background:rgba(0,200,255,0.15);transform:scale(0.96)}

/* TABS — stretch full width */
.tabs{
  position:relative;z-index:2;
  display:flex;padding:12px 14px 0;gap:3px;
}
.tab-btn{
  flex:1;font-family:var(--ui);font-size:12px;font-weight:700;
  letter-spacing:1px;text-transform:uppercase;
  padding:10px 4px;background:transparent;color:var(--muted);
  border:1px solid transparent;border-bottom:none;
  border-radius:6px 6px 0 0;cursor:pointer;transition:all .2s;
  white-space:nowrap;text-align:center;
  -webkit-tap-highlight-color:transparent;
}
.tab-btn:active{opacity:0.7}
.tab-btn.active{
  color:var(--blue);border-color:var(--bdr);
  background:var(--surf);border-bottom:1px solid var(--surf);
  margin-bottom:-1px;
}

/* PANEL */
.panel{
  display:none;position:relative;z-index:1;
  flex-direction:column;
  margin:0 14px 20px;
  background:var(--surf);border:1px solid var(--bdr);
  border-radius:0 0 8px 8px;overflow:hidden;
}
.panel.active{display:flex}

/* CHART */
.chart-wrap{
  width:100%;height:240px;
  padding:14px 14px 6px;
  position:relative;flex-shrink:0;
}

/* WARMUP OVERLAY — shown until first valid reading */
.ov{
  position:absolute;inset:0;
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  background:rgba(10,14,26,0.82);gap:10px;
  font-family:var(--mono);font-size:12px;color:var(--muted);letter-spacing:1px;
  pointer-events:none;transition:opacity .6s;z-index:10;
}
.ov.gone{opacity:0;pointer-events:none}
.spinner{
  width:26px;height:26px;border:2px solid var(--bdr);
  border-top-color:var(--blue);border-radius:50%;
  animation:spin 0.9s linear infinite;
}
@keyframes spin{to{transform:rotate(360deg)}}

/* INFO SECTION */
.info-sec{
  display:flex;flex-direction:column;gap:10px;
  padding:12px 14px 18px;border-top:1px solid var(--bdr);
}
.info-top{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}
.reading-lbl{font-size:9px;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin-bottom:2px}
.reading-val{font-family:var(--mono);font-size:36px;color:var(--blue);line-height:1;transition:color .35s}
.reading-unit{font-size:14px;color:var(--muted)}
.reading-time{font-family:var(--mono);font-size:9px;color:var(--muted);margin-top:3px}
.badge{
  display:inline-flex;align-items:center;gap:5px;
  padding:6px 12px;border-radius:20px;border:1px solid currentColor;
  font-size:10px;letter-spacing:2px;font-weight:700;text-transform:uppercase;
  transition:all .35s;white-space:nowrap;
}
.advice{
  background:rgba(0,0,0,0.25);
  border-left:3px solid var(--blue);border-radius:0 5px 5px 0;
  padding:9px 12px;font-size:12px;line-height:1.6;color:var(--text);
  transition:border-color .35s;
}
.legend{
  display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));
  gap:3px 10px;border:1px solid var(--bdr);border-radius:6px;padding:10px 12px;
}
.legend strong{
  grid-column:1/-1;font-size:9px;letter-spacing:2px;
  text-transform:uppercase;color:var(--muted);margin-bottom:3px;
}
.lg{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--muted)}
.lgd{width:8px;height:8px;border-radius:2px;flex-shrink:0}

/* DESKTOP */
@media(min-width:680px){
  header{padding:14px 26px} h1{font-size:19px}
  .tabs{padding:16px 26px 0}
  .panel{margin:0 26px 28px;flex-direction:row!important}
  .chart-wrap{flex:1 1 0;height:400px;padding:18px}
  .info-sec{
    width:260px;flex-shrink:0;border-top:none;
    border-left:1px solid var(--bdr);padding:18px 16px;overflow-y:auto;
  }
  .reading-val{font-size:42px}
}
</style>
</head>
<body>

<header>
  <div class="logo">
    <div class="logo-icon">⬡</div>
    <h1>ESP32 Monitor</h1>
  </div>
  <div class="hdr-r">
    <div class="sig" id="sigEl">
      <div class="sig-dot" id="sigDot" style="background:var(--orange);box-shadow:0 0 7px var(--orange)"></div>
      <span class="sig-lbl" id="sigLbl" style="color:var(--orange)">CONNECTING</span>
    </div>
    <button class="dl-btn" onclick="dlCSV()">⬇ CSV</button>
  </div>
</header>

<div class="tabs">
  <button class="tab-btn active" data-tab="tP">🌡 Temp</button>
  <button class="tab-btn"        data-tab="hP">💧 Humidity</button>
  <button class="tab-btn"        data-tab="gP">💨 Gas</button>
</div>

<!-- TEMP -->
<div class="panel active" id="tP">
  <div class="chart-wrap">
    <canvas id="tC"></canvas>
    <div class="ov" id="tOv"><div class="spinner"></div><span>WAITING FOR SENSOR</span></div>
  </div>
  <div class="info-sec">
    <div class="info-top">
      <div>
        <div class="reading-lbl">Temperature</div>
        <div class="reading-val" id="tV">--<span class="reading-unit">°C</span></div>
        <div class="reading-time" id="tT">waiting…</div>
      </div>
      <div id="tB" class="badge" style="color:var(--muted);border-color:var(--muted)">— INIT</div>
    </div>
    <div class="advice" id="tA">Connecting to sensor…</div>
    <div class="legend">
      <strong>Ranges</strong>
      <div class="lg"><span class="lgd" style="background:rgba(68,136,255,.7)"></span>&lt;10°C Very Cold</div>
      <div class="lg"><span class="lgd" style="background:rgba(0,200,80,.7)"></span>10–33°C Normal</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,215,0,.7)"></span>34–42°C Warm</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,120,0,.7)"></span>43–50°C Danger</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,46,91,.7)"></span>&gt;50°C Extreme</div>
    </div>
  </div>
</div>

<!-- HUM -->
<div class="panel" id="hP">
  <div class="chart-wrap">
    <canvas id="hC"></canvas>
    <div class="ov" id="hOv"><div class="spinner"></div><span>WAITING FOR SENSOR</span></div>
  </div>
  <div class="info-sec">
    <div class="info-top">
      <div>
        <div class="reading-lbl">Humidity</div>
        <div class="reading-val" id="hV">--<span class="reading-unit">%</span></div>
        <div class="reading-time" id="hT">waiting…</div>
      </div>
      <div id="hB" class="badge" style="color:var(--muted);border-color:var(--muted)">— INIT</div>
    </div>
    <div class="advice" id="hA">Connecting to sensor…</div>
    <div class="legend">
      <strong>Ranges</strong>
      <div class="lg"><span class="lgd" style="background:rgba(255,46,91,.7)"></span>&lt;10% Ext. Dry</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,120,0,.7)"></span>10–18% Very Dry</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,215,0,.7)"></span>19–34% Dry</div>
      <div class="lg"><span class="lgd" style="background:rgba(0,200,80,.7)"></span>35–60% Normal</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,215,0,.7)"></span>61–75% Humid</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,120,0,.7)"></span>76–85% Very Humid</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,46,91,.7)"></span>&gt;85% Extreme</div>
    </div>
  </div>
</div>

<!-- GAS -->
<div class="panel" id="gP">
  <div class="chart-wrap">
    <canvas id="gC"></canvas>
    <div class="ov" id="gOv"><div class="spinner"></div><span>WAITING FOR SENSOR</span></div>
  </div>
  <div class="info-sec">
    <div class="info-top">
      <div>
        <div class="reading-lbl">Gas Level</div>
        <div class="reading-val" id="gV">--<span class="reading-unit" style="font-size:12px"> ADC</span></div>
        <div class="reading-time" id="gT">waiting…</div>
      </div>
      <div id="gB" class="badge" style="color:var(--muted);border-color:var(--muted)">— INIT</div>
    </div>
    <div class="advice" id="gA">Connecting to sensor…</div>
    <div class="legend">
      <strong>Ranges</strong>
      <div class="lg"><span class="lgd" style="background:rgba(0,200,80,.7)"></span>&lt;600 Normal</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,215,0,.7)"></span>600–799 Caution</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,120,0,.7)"></span>800–899 Danger</div>
      <div class="lg"><span class="lgd" style="background:rgba(255,46,91,.7)"></span>&gt;=900 Extreme</div>
    </div>
  </div>
</div>

<script>
// ── TABS ─────────────────────────────────────────────────
document.querySelectorAll('.tab-btn').forEach(b=>{
  b.addEventListener('click',()=>{
    document.querySelectorAll('.tab-btn').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.panel').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    document.getElementById(b.dataset.tab).classList.add('active');
  });
});
function dlCSV(){const a=document.createElement('a');a.href='/download';a.download='sensor_log.csv';a.click();}

// ── STATUS FNS ───────────────────────────────────────────
const SF={
  t:v=>{
    if(v<10)  return['TOO COLD','#4488ff','Very low temperature. Risk of hypothermia. Wear heavy layers.'];
    if(v<=33) return['NORMAL','#00ff9d','Comfortable temperature. No action needed.'];
    if(v<=42) return['CAUTION','#ffd700','Warm. Stay hydrated and avoid prolonged sun exposure.'];
    if(v<=50) return['DANGER','#ff7b00','High heat. Risk of heat stroke. Minimize outdoor activity.'];
    return      ['EXTREME','#ff2e5b','Extreme heat. Stay indoors and hydrate immediately.'];
  },
  h:v=>{
    if(v<10)  return['EXT. DRY','#ff2e5b','Very dry air. Use humidifier and drink water.'];
    if(v<18)  return['VERY DRY','#ff7b00','Dry air. Skin and throat may feel irritated.'];
    if(v<35)  return['DRY','#ffd700','Mild discomfort possible. Stay hydrated.'];
    if(v<=60) return['NORMAL','#00ff9d','Comfortable humidity. No action needed.'];
    if(v<=75) return['HUMID','#ffd700','Slightly sticky. Ensure ventilation.'];
    if(v<=85) return['VERY HUMID','#ff7b00','High humidity. Ventilate rooms.'];
    return      ['EXT. HUMID','#ff2e5b','Risk of mold. Use dehumidifier immediately.'];
  },
  g:v=>{
    if(v<600) return['NORMAL','#00ff9d','Gas levels safe. No action needed.'];
    if(v<800) return['CAUTION','#ffd700','Elevated gas. Ventilate and monitor closely.'];
    if(v<900) return['DANGER','#ff7b00','High gas. Ventilate the area immediately.'];
    return      ['EXTREME','#ff2e5b','Evacuate! Extremely high gas concentration.'];
  }
};

// ── UPDATE UI ────────────────────────────────────────────
function updateUI(p,val,ovId){
  const[lbl,col,adv]=SF[p](val);
  document.getElementById(ovId).classList.add('gone');
  const ve=document.getElementById(p+'V');
  ve.firstChild.textContent=val.toFixed(p==='t'?1:0);
  ve.style.color=col;
  document.getElementById(p+'T').textContent=new Date().toLocaleTimeString();
  const b=document.getElementById(p+'B');
  b.textContent='● '+lbl; b.style.color=col; b.style.borderColor=col;
  b.style.boxShadow='0 0 10px '+col+'44';
  const a=document.getElementById(p+'A');
  a.textContent=adv; a.style.borderColor=col;
}

// ── HEADER SIGNAL STATE ──────────────────────────────────
let sigState='';
function setSig(state){
  if(state===sigState)return; sigState=state;
  const dot=document.getElementById('sigDot');
  const lbl=document.getElementById('sigLbl');
  if(state==='live'){
    dot.style.background='var(--green)'; dot.style.boxShadow='0 0 7px var(--green)';
    lbl.style.color='var(--green)'; lbl.textContent='LIVE';
    dot.style.animationDuration='1.2s';
  } else if(state==='conn'){
    dot.style.background='var(--orange)'; dot.style.boxShadow='0 0 7px var(--orange)';
    lbl.style.color='var(--orange)'; lbl.textContent='CONNECTING';
    dot.style.animationDuration='0.8s';
  } else {
    dot.style.background='var(--red)'; dot.style.boxShadow='0 0 7px var(--red)';
    lbl.style.color='var(--red)'; lbl.textContent='NO SIGNAL';
    dot.style.animationDuration='0.4s';
  }
}

// ── ZONE PLUGIN ──────────────────────────────────────────
function zonePlugin(cid,zones){
  return{id:'z'+cid,beforeDraw(ch){
    if(ch.canvas.id!==cid)return;
    const{ctx,chartArea:ca,scales}=ch; if(!ca)return;
    zones.forEach(z=>{
      ctx.fillStyle=z.c;
      ctx.fillRect(ca.left,scales.y.getPixelForValue(z.hi),
        ca.right-ca.left,scales.y.getPixelForValue(z.lo)-scales.y.getPixelForValue(z.hi));
    });
  }};
}
const TZ=[{lo:50,hi:110,c:'rgba(255,46,91,.13)'},{lo:42,hi:50,c:'rgba(255,123,0,.13)'},
          {lo:33,hi:42,c:'rgba(255,215,0,.11)'},{lo:10,hi:33,c:'rgba(0,255,157,.07)'},
          {lo:0, hi:10,c:'rgba(68,136,255,.13)'}];
const HZ=[{lo:85,hi:100,c:'rgba(255,46,91,.13)'},{lo:75,hi:85,c:'rgba(255,123,0,.13)'},
          {lo:60,hi:75, c:'rgba(255,215,0,.11)'},{lo:35,hi:60,c:'rgba(0,255,157,.07)'},
          {lo:18,hi:35, c:'rgba(255,215,0,.11)'},{lo:10,hi:18,c:'rgba(255,123,0,.13)'},
          {lo:0, hi:10, c:'rgba(255,46,91,.13)'}];
const GZ=[{lo:900,hi:1023,c:'rgba(255,46,91,.13)'},{lo:800,hi:900,c:'rgba(255,123,0,.13)'},
          {lo:600,hi:800, c:'rgba(255,215,0,.11)'},{lo:0,  hi:600,c:'rgba(0,255,157,.07)'}];

// ── CHART FACTORY ────────────────────────────────────────
function mkChart(id,label,yMax,zones,clr){
  return new Chart(document.getElementById(id),{
    type:'line',
    data:{datasets:[{label,borderColor:clr,borderWidth:2,pointRadius:0,
      backgroundColor:clr.replace('rgb(','rgba(').replace(')',',0.07)'),
      fill:true,tension:0.35,data:[]}]},
    options:{
      animation:false,responsive:true,maintainAspectRatio:false,
      plugins:{legend:{display:false},tooltip:{backgroundColor:'#111827',
        borderColor:clr,borderWidth:1,titleColor:clr,bodyColor:'#d4e6f1'}},
      scales:{
        x:{type:'time',
           time:{unit:'second',displayFormats:{second:'h:mm:ss a'},tooltipFormat:'h:mm:ss a'},
           grid:{color:'rgba(255,255,255,0.04)'},
           ticks:{color:'#5a7a8a',font:{family:"'Share Tech Mono'",size:9},maxTicksLimit:3,maxRotation:0}},
        y:{min:0,max:yMax,
           grid:{color:'rgba(255,255,255,0.04)'},
           ticks:{color:'#5a7a8a',font:{family:"'Share Tech Mono'"}},
           title:{display:true,text:label,color:'#5a7a8a',font:{size:11}}}
      }
    },
    plugins:[zonePlugin(id,zones)]
  });
}

const charts={
  t:mkChart('tC','Temperature (°C)', 110, TZ,'rgb(0,200,255)'),
  h:mkChart('hC','Humidity (%)',     100,HZ,'rgb(0,255,157)'),
  g:mkChart('gC','Gas Level (ADC)', 1023,GZ,'rgb(255,123,0)')
};
const KEYS={t:'temp',h:'hum',g:'gas'};
const OVS ={t:'tOv', h:'hOv',g:'gOv'};
const WIN  = 90000;

// ── LOAD HISTORY ─────────────────────────────────────────
fetch('/history').then(r=>r.json()).then(rows=>{
  if(!rows.length)return;
  const last=rows[rows.length-1].ms, now=Date.now();
  rows.forEach(d=>{
    const x=now-(last-d.ms);
    Object.keys(charts).forEach(p=>charts[p].data.datasets[0].data.push({x,y:d[KEYS[p]]}));
  });
  Object.values(charts).forEach(c=>c.update('none'));
}).catch(()=>{});

// ── CSV DOWNLOAD — browser builds real datetime from millis offset ──
function dlCSV(){
  fetch('/download').then(r=>{
    const uptime=parseInt(r.headers.get('X-Uptime')||'0');
    const now=Date.now();
    // offset: wall-clock time that corresponds to ESP millis=0
    const bootWall=now-uptime;
    return r.json().then(rows=>({rows,bootWall}));
  }).then(({rows,bootWall})=>{
    const lines=['datetime,temp_C,hum_pct,gas_adc'];
    rows.forEach(d=>{
      const dt=new Date(bootWall+d.ms);
      const stamp=
        dt.getFullYear()+'-'+
        String(dt.getMonth()+1).padStart(2,'0')+'-'+
        String(dt.getDate()).padStart(2,'0')+' '+
        String(dt.getHours()).padStart(2,'0')+':'+
        String(dt.getMinutes()).padStart(2,'0')+':'+
        String(dt.getSeconds()).padStart(2,'0');
      lines.push(stamp+','+d.temp+','+d.hum+','+d.gas);
    });
    const blob=new Blob([lines.join('\r\n')],{type:'text/csv'});
    const a=document.createElement('a');
    a.href=URL.createObjectURL(blob);
    a.download='sensor_log.csv'; a.click();
    URL.revokeObjectURL(a.href);
  }).catch(()=>alert('Download failed'));
}

// ── POLL — one fetch every 2 s feeds all three charts ────
let fails=0;
function poll(){
  fetch('/data').then(r=>{ if(!r.ok)throw 0; return r.json(); })
  .then(d=>{
    fails=0;
    setSig('live');
    const now=Date.now(), winStart=now-WIN;
    Object.keys(charts).forEach(p=>{
      const val=parseFloat(d[KEYS[p]]);
      if(isNaN(val)||val===0)return; // skip zero placeholder (DHT not ready yet)
      const ds=charts[p].data.datasets[0].data;
      ds.push({x:now,y:val});
      while(ds.length && ds[0].x < winStart-5000) ds.shift();
      charts[p].options.scales.x.min=winStart;
      charts[p].options.scales.x.max=now;
      charts[p].update('none');
      updateUI(p,val,OVS[p]);
    });
  })
  .catch(()=>{ if(++fails>=4) setSig('nosig'); else setSig('conn'); });
}

poll();
setInterval(poll, 2000);
</script>
</body>
</html>
)HTMLEOF";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// ── Sensor sampling ──────────────────────────────────────
void sampleSensors() {
  latestG = analogRead(GAS_PIN);

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(t) && t > 0) latestT = t;
  if (!isnan(h) && h > 0) latestH = h;

  hist[histIdx] = { millis(), latestT, latestH, latestG };
  histIdx = (histIdx + 1) % HIST_N;
  if (histIdx == 0) histFull = true;

  Serial.printf("T=%.1f C  H=%.0f %%  G=%d\n", latestT, latestH, latestG);
}

// ── setup / loop ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  dht.begin();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // DHT11 needs ~1s after power — give it 2s then just proceed;
  // the loop will keep retrying every 2s until valid reads come in
  delay(2000);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.println("========================");
  Serial.print  ("SSID : "); Serial.println(AP_SSID);
  Serial.print  ("URL  : http://"); Serial.println(ip);
  Serial.println("========================");

  server.on("/",         handleRoot);
  server.on("/data",     handleData);
  server.on("/history",  handleHistory);
  server.on("/download", handleDownload);
  server.begin();
  Serial.println("HTTP server started");

  lastSample = millis() - SAMPLE_MS;
}

void loop() {
  server.handleClient();
  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();
    sampleSensors();
    server.handleClient();  // yield after blocking DHT read
  }
}
