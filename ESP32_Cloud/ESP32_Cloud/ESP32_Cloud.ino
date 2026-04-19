/*
  ESP32 Gas Sensor Monitor - Arduino Cloud + Local Dashboard

  Features:
  - Connects to normal WiFi internet (STA mode)
  - Publishes Temperature, Humidity, Gas ADC to Arduino Cloud
  - Keeps a local dashboard webpage
  - Keeps CSV download
  - Safer pin spacing for perfboard soldering

  Recommended wiring:
  - DHT11 DATA -> GPIO 4
  - MQ135 AO   -> GPIO 32

  Notes:
  - Create Arduino Cloud variables first, then paste the generated IDs below
  - This sketch intentionally keeps placeholders so setup is easier to follow
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <time.h>
#include "thingProperties.h"

// ============================================================
// Sensor pins
// ============================================================
#define DHTPIN   4
#define DHTTYPE  DHT11
#define GAS_PIN  32

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// ============================================================
// Arduino Cloud variables
// Must match your Thing variable names/types in Arduino Cloud
// ============================================================
// Cloud variables are declared in thingProperties.h

// ============================================================
// Local latest readings
// ============================================================
float latestT = 0;
float latestH = 0;
int   latestG = 0;
bool  haveTemp = false;
bool  haveHum  = false;
bool  haveGas  = false;

// ============================================================
// Circular history buffer
// ============================================================
struct Sample {
  unsigned long ms;
  float t;
  float h;
  int g;
};

const int HIST_N = 120;
Sample hist[HIST_N];
int histIdx = 0;
bool histFull = false;

// ============================================================
// Timing
// ============================================================
unsigned long lastSample = 0;
unsigned long lastCloudPush = 0;
const unsigned long SAMPLE_MS = 2000;
const unsigned long CLOUD_PUSH_MS = 5000;

String sessionId = "";
String sessionName = "";
bool clockReady = false;
unsigned long long sessionStartEpochMs = 0;
unsigned long sessionStartUptimeMs = 0;

String jf(float v, int d = 1) {
  return String(v, d);
}

String jsonEscape(const String &s) {
  String out = s;
  out.replace("\\", "\\\\");
  out.replace("\"", "\\\"");
  return out;
}

String formatEpochMsManila(unsigned long long epochMs, const char *fmt) {
  time_t seconds = (time_t)(epochMs / 1000ULL);
  seconds += 8 * 3600;
  struct tm timeinfo;
  gmtime_r(&seconds, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), fmt, &timeinfo);
  return String(buf);
}

String formatNow(const char *fmt) {
  time_t nowUtc = time(nullptr);
  if (nowUtc <= 1000) return "";
  unsigned long long nowMs = (unsigned long long)nowUtc * 1000ULL;
  return formatEpochMsManila(nowMs, fmt);
}

void initClock() {
  struct tm timeinfo;
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&timeinfo)) {
      clockReady = true;
      return;
    }
    delay(300);
  }
  clockReady = false;
}

void initSessionIdentity() {
  if (clockReady) {
    time_t nowUtc = time(nullptr);
    if (nowUtc > 1000) {
      sessionStartEpochMs = (unsigned long long)nowUtc * 1000ULL;
      sessionStartUptimeMs = millis();
      sessionId = formatEpochMsManila(sessionStartEpochMs, "%Y-%m-%d_%H-%M-%S");
      sessionName = formatEpochMsManila(sessionStartEpochMs, "%Y-%m-%d %H:%M:%S");
    }
  }

  if (sessionId.length() == 0) {
    sessionStartUptimeMs = millis();
    sessionId = "session_" + String(sessionStartUptimeMs);
    sessionName = sessionId;
  }
}

void postReadingToBackend() {
  if (WiFi.status() != WL_CONNECTED || sessionId.length() == 0) return;

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(SECRET_BACKEND_URL);
  http.addHeader("Content-Type", "application/json");

  String timestamp;
  if (clockReady && sessionStartEpochMs > 0) {
    unsigned long elapsedMs = millis() - sessionStartUptimeMs;
    timestamp = formatEpochMsManila(sessionStartEpochMs + (unsigned long long)elapsedMs, "%Y-%m-%d %H:%M:%S");
  } else {
    timestamp = String(millis());
  }
  String payload = "{";
  payload += "\"action\":\"log_reading\",";
  payload += "\"session_id\":\"" + jsonEscape(sessionId) + "\",";
  payload += "\"session_name\":\"" + jsonEscape(sessionName) + "\",";
  payload += "\"timestamp\":\"" + jsonEscape(timestamp) + "\",";
  payload += "\"uptime_ms\":" + String(millis()) + ",";
  payload += "\"temperature_c\":" + jf(latestT, 1) + ",";
  payload += "\"humidity_pct\":" + jf(latestH, 0) + ",";
  payload += "\"gas_adc\":" + String(latestG) + ",";
  payload += "\"device_label\":\"ESP32 Gas Monitor\"";
  payload += "}";

  int code = http.POST(payload);
  if (code > 0) {
    Serial.printf("Backend POST OK, code=%d\n", code);
  } else {
    Serial.printf("Backend POST failed, err=%s\n", http.errorToString(code).c_str());
  }
  http.end();
}

void handleData() {
  String out = "{";
  out += "\"temp\":"   + jf(latestT, 1) + ",";
  out += "\"hum\":"    + jf(latestH, 0) + ",";
  out += "\"gas\":"    + String(latestG) + ",";
  out += "\"uptime\":" + String(millis()) + ",";
  out += "\"wifi\":"   + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
  out += "\"session\":\"" + jsonEscape(sessionName) + "\",";
  out += "\"session_id\":\"" + jsonEscape(sessionId) + "\",";
  out += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  out += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleHistory() {
  int count = histFull ? HIST_N : histIdx;
  int start = histFull ? histIdx : 0;
  String out = "[";

  for (int i = 0; i < count; i++) {
    int j = (start + i) % HIST_N;
    if (i) out += ",";
    out += "{\"ms\":" + String(hist[j].ms) + ","
           "\"temp\":" + jf(hist[j].t, 1) + ","
           "\"hum\":" + jf(hist[j].h, 0) + ","
           "\"gas\":" + String(hist[j].g) + "}";
  }

  out += "]";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleDownload() {
  int count = histFull ? HIST_N : histIdx;
  int start = histFull ? histIdx : 0;
  String out = "[";

  for (int i = 0; i < count; i++) {
    int j = (start + i) % HIST_N;
    if (i) out += ",";
    out += "{\"ms\":" + String(hist[j].ms) + ","
           "\"temp\":" + jf(hist[j].t, 1) + ","
           "\"hum\":" + jf(hist[j].h, 0) + ","
           "\"gas\":" + String(hist[j].g) + "}";
  }

  out += "]";
  server.sendHeader("X-Uptime", String(millis()));
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleBackendSessions() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"wifi_disconnected\"}");
    return;
  }

  HTTPClient http;
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(String(SECRET_BACKEND_URL) + "?action=sessions");
  int code = http.GET();
  String body = code > 0 ? http.getString() : "{\"ok\":false,\"error\":\"backend_sessions_failed\"}";
  Serial.printf("Backend sessions GET code=%d\n", code);
  if (code <= 0) Serial.printf("Backend sessions error=%s\n", http.errorToString(code).c_str());
  http.end();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code > 0 ? 200 : 502, "application/json", body);
}

void handleBackendReadings() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"wifi_disconnected\"}");
    return;
  }

  if (!server.hasArg("session_id")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"session_id_required\"}");
    return;
  }

  String sessionArg = server.arg("session_id");
  String encodedArg = sessionArg;
  encodedArg.replace(" ", "%20");
  encodedArg.replace(":", "%3A");
  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(String(SECRET_BACKEND_URL) + "?action=readings&session_id=" + encodedArg);
  int code = http.GET();
  String body = code > 0 ? http.getString() : "{\"ok\":false,\"error\":\"backend_readings_failed\"}";
  Serial.printf("Backend readings GET code=%d\n", code);
  if (code <= 0) Serial.printf("Backend readings error=%s\n", http.errorToString(code).c_str());
  http.end();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code > 0 ? 200 : 502, "application/json", body);
}

void handleBackendDeleteSession() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"wifi_disconnected\"}");
    return;
  }

  if (!server.hasArg("session_id")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"session_id_required\"}");
    return;
  }

  String sessionArg = server.arg("session_id");
  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(String(SECRET_BACKEND_URL));
  http.addHeader("Content-Type", "application/json");
  String payload = "{";
  payload += "\"action\":\"delete_session\",";
  payload += "\"session_id\":\"" + jsonEscape(sessionArg) + "\"";
  payload += "}";
  int code = http.POST(payload);
  String body = code > 0 ? http.getString() : "{\"ok\":false,\"error\":\"backend_delete_failed\"}";
  Serial.printf("Backend delete POST code=%d\n", code);
  if (code <= 0) Serial.printf("Backend delete error=%s\n", http.errorToString(code).c_str());
  http.end();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code > 0 ? 200 : 502, "application/json", body);
}

const char INDEX_HTML[] PROGMEM = R"HTMLEOF(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Cloud Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3.0.0/dist/chartjs-adapter-date-fns.bundle.min.js"></script>
<style>
@import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Rajdhani:wght@500;600;700&display=swap');
:root{
  --bg:#09111c;--surf:#101a28;--surf2:#0c1420;--bdr:#22364d;
  --blue:#00c8ff;--green:#00ff9d;--yellow:#ffd700;--orange:#ff7b00;--red:#ff2e5b;
  --text:#edf7ff;--text-soft:#cfe4f5;--muted:#94afc4;
  --mono:'Share Tech Mono',monospace;--ui:'Rajdhani',sans-serif;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:var(--ui);overflow-x:hidden}
body::before{
  content:'';position:fixed;inset:0;pointer-events:none;z-index:0;
  background:repeating-linear-gradient(0deg,transparent,transparent 3px,rgba(0,200,255,0.018) 3px,rgba(0,200,255,0.018) 4px);
}
header{
  position:relative;z-index:2;display:flex;align-items:flex-start;justify-content:space-between;gap:16px;
  padding:14px 16px;border-bottom:1px solid var(--bdr);
  background:linear-gradient(90deg,rgba(0,200,255,0.08) 0%,transparent 72%);
}
.logo{display:flex;align-items:flex-start;gap:10px}
.logo-icon{
  width:34px;height:34px;border:2px solid var(--blue);border-radius:8px;display:grid;place-items:center;
  font-size:16px;box-shadow:0 0 14px rgba(0,200,255,0.45);animation:glow 2.5s ease-in-out infinite;
}
@keyframes glow{0%,100%{box-shadow:0 0 10px rgba(0,200,255,0.35)}50%{box-shadow:0 0 20px rgba(0,200,255,0.85)}}
h1{font-size:18px;font-weight:700;letter-spacing:2px;text-transform:uppercase;color:var(--blue)}
.title-row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.sub{font-size:13px;color:var(--text-soft);margin-top:4px}
.hdr-r{display:flex;align-items:center;gap:10px;flex-wrap:wrap;justify-content:flex-end}
.sig{display:flex;align-items:center;gap:7px;padding:8px 10px;background:rgba(255,255,255,0.03);border:1px solid var(--bdr);border-radius:999px}
.sig-dot{width:8px;height:8px;border-radius:50%;animation:blink 1.2s ease-in-out infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.2}}
.sig-lbl{font-size:11px;letter-spacing:2px;font-weight:700}
.meta{font-family:var(--mono);font-size:11px;color:var(--text-soft);padding:8px 10px;background:rgba(255,255,255,0.03);border:1px solid var(--bdr);border-radius:999px}
.dl-btn{
  font-family:var(--mono);font-size:11px;letter-spacing:1px;padding:9px 14px;background:transparent;color:var(--blue);
  border:1px solid var(--blue);border-radius:8px;cursor:pointer;transition:all .2s;-webkit-tap-highlight-color:transparent;
}
.dl-btn:hover,.dl-btn:active{background:rgba(0,200,255,0.14)}
.live-controls{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:14px;justify-content:flex-start}
.live-select{font-family:var(--mono);font-size:11px;letter-spacing:1px;padding:9px 36px 9px 12px;background:rgba(255,255,255,0.03);color:var(--text-soft);border:1px solid var(--blue);border-radius:8px;outline:none}
.saved-row{display:flex;justify-content:space-between;align-items:flex-start;gap:10px}
.delete-btn{font-family:var(--mono);font-size:10px;letter-spacing:1px;padding:6px 10px;background:transparent;color:#ff9eb6;border:1px solid rgba(255,46,91,0.55);border-radius:8px;cursor:pointer;flex-shrink:0}
.delete-btn:hover{background:rgba(255,46,91,0.12)}
.main{position:relative;z-index:1;padding:16px;max-width:1600px;margin:0 auto}
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px;align-items:stretch}
.grid.vertical{grid-template-columns:1fr;max-width:1100px;margin:0 auto}
.grid.vertical .chart-wrap{height:360px}
.grid.vertical + .history-panel{max-width:1100px;margin:16px auto 0}
.panel{
  background:linear-gradient(180deg,var(--surf) 0%,var(--surf2) 100%);border:1px solid var(--bdr);border-radius:14px;overflow:hidden;
  box-shadow:0 8px 30px rgba(0,0,0,0.18);min-width:0;height:100%;
}
.panel-head{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:14px 16px 10px;border-bottom:1px solid rgba(255,255,255,0.05)}
.panel-title{font-size:19px;font-weight:700;color:var(--text)}
.panel-sub{font-family:var(--mono);font-size:11px;color:var(--muted);margin-top:2px}
.badge{
  display:inline-flex;align-items:center;gap:5px;padding:7px 12px;border-radius:999px;border:1px solid currentColor;
  font-size:11px;letter-spacing:1.5px;font-weight:700;text-transform:uppercase;white-space:nowrap;
}
.chart-wrap{position:relative;height:260px;padding:12px 14px 8px}
.ov{
  position:absolute;inset:12px 14px 8px;display:flex;flex-direction:column;align-items:center;justify-content:center;
  background:rgba(9,17,28,0.84);gap:10px;font-family:var(--mono);font-size:12px;color:var(--text-soft);letter-spacing:1px;
  pointer-events:none;transition:opacity .6s;z-index:10;border-radius:10px;
}
.ov.gone{opacity:0;pointer-events:none}
.spinner{width:28px;height:28px;border:2px solid var(--bdr);border-top-color:var(--blue);border-radius:50%;animation:spin 0.9s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.info-sec{display:flex;flex-direction:column;gap:12px;padding:12px 16px 18px}
.metrics{display:grid;grid-template-columns:1fr auto;gap:12px;align-items:end}
.reading-lbl{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin-bottom:4px}
.reading-val{font-family:var(--mono);font-size:38px;line-height:1;color:var(--text)}
.reading-unit{font-size:16px;color:var(--text-soft)}
.reading-time{font-family:var(--mono);font-size:11px;color:var(--muted);margin-top:6px}
.advice{
  min-height:154px;background:rgba(0,0,0,0.22);border-left:4px solid var(--blue);border-radius:0 10px 10px 0;
  padding:12px 14px;color:var(--text);line-height:1.5;font-size:15px;
}
.advice-title{font-weight:700;font-size:18px;margin-bottom:8px;color:var(--text)}
.advice-line{color:var(--text-soft);margin-top:6px}
.guide{border:1px solid rgba(255,255,255,0.06);border-radius:10px;padding:12px;background:rgba(255,255,255,0.02)}
.guide-title{font-size:12px;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin-bottom:8px}
.guide ul{list-style:none;display:flex;flex-direction:column;gap:6px}
.guide li{font-size:13px;color:var(--text-soft);line-height:1.45}
.guide strong{color:var(--text)}
@media(max-width:1180px){
  .grid{grid-template-columns:1fr;}
  .chart-wrap{height:280px}
  .advice{min-height:0}
}
.history-panel{margin-top:16px;background:linear-gradient(180deg,var(--surf) 0%,var(--surf2) 100%);border:1px solid var(--bdr);border-radius:14px;overflow:hidden;box-shadow:0 8px 30px rgba(0,0,0,0.18)}
.history-head{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:14px 16px 10px;border-bottom:1px solid rgba(255,255,255,0.05)}
.history-title{font-size:18px;font-weight:700;color:var(--text)}
.history-sub{font-family:var(--mono);font-size:11px;color:var(--muted);margin-top:2px}
.history-wrap{overflow-x:auto}
.history-table{width:100%;border-collapse:collapse;font-size:13px}
.history-table th,.history-table td{padding:10px 12px;border-bottom:1px solid rgba(255,255,255,0.06);text-align:left;color:var(--text-soft)}
.history-table th{font-size:11px;letter-spacing:1.5px;text-transform:uppercase;color:var(--muted);background:rgba(255,255,255,0.03)}
.history-table td:first-child,.history-table th:first-child{color:var(--text)}
.saved-grid{display:grid;grid-template-columns:430px minmax(0,1fr);gap:18px;padding:18px 18px 28px}
.saved-list{border:1px solid rgba(255,255,255,0.06);border-radius:12px;overflow:auto;background:rgba(255,255,255,0.02);max-height:720px}
.saved-item{padding:14px 16px;border-bottom:1px solid rgba(255,255,255,0.06);cursor:pointer;transition:background .18s, opacity .18s}
.saved-item:last-child{border-bottom:none}
.saved-item:hover,.saved-item.active{background:rgba(0,200,255,0.08)}
.saved-item.disabled{pointer-events:none;opacity:.45}
.saved-name{font-weight:700;color:var(--text)}
.saved-meta{font-size:12px;color:var(--muted);margin-top:4px}
.saved-empty{padding:16px;color:var(--muted)}
.saved-detail{position:relative;border:1px solid rgba(255,255,255,0.06);border-radius:12px;overflow:hidden;background:rgba(255,255,255,0.02);min-height:640px}
.saved-summary{padding:14px 16px;border-bottom:1px solid rgba(255,255,255,0.06)}
.saved-summary-title{font-size:16px;font-weight:700;color:var(--text)}
.saved-summary-meta{font-size:12px;color:var(--muted);margin-top:6px}
.saved-chart-wrap{height:320px;padding:12px 16px 4px}
.saved-table-wrap{max-height:320px;overflow:auto;padding:0 16px 18px}
.saved-table{width:100%;border-collapse:collapse;font-size:12px}
.saved-table th,.saved-table td{padding:8px 10px;border-bottom:1px solid rgba(255,255,255,0.06);text-align:left;color:var(--text-soft)}
.saved-table th{font-size:11px;letter-spacing:1.5px;text-transform:uppercase;color:var(--muted);background:transparent;position:sticky;top:0}
.saved-loading{position:absolute;inset:0;display:none;flex-direction:column;align-items:center;justify-content:center;gap:12px;background:rgba(9,17,28,0.9);z-index:30;color:var(--text-soft);font-family:var(--mono);letter-spacing:1px}
.saved-loading.show{display:flex}
.saved-spinner{width:38px;height:38px;border:3px solid rgba(255,255,255,0.12);border-top-color:var(--blue);border-radius:50%;animation:spin .9s linear infinite}
@media(max-width:680px){
  header{padding:12px 12px 10px;flex-direction:column;align-items:stretch;gap:10px}
  h1{font-size:15px;letter-spacing:1px}
  .title-row{align-items:flex-start;gap:6px}
  .sub{font-size:12px;margin-top:2px}
  .hdr-r{justify-content:flex-start;align-items:center;gap:6px}
  .meta{font-size:9px;padding:6px 7px}
  .sig{padding:6px 8px;gap:5px}
  .sig-lbl{font-size:9px;letter-spacing:1px}
  .dl-btn{font-size:9px;padding:8px 10px}
  .title-row .meta{font-size:9px;padding:6px 7px}
  #layoutBtn{display:none}
  .live-controls{gap:6px;margin-bottom:10px;justify-content:space-between}
  .live-select{font-size:10px;padding:8px 28px 8px 10px;min-width:128px}
  .main{padding:10px}
  .panel-head{padding:12px 12px 8px}
  .chart-wrap{height:205px;padding:8px 8px 4px}
  .ov{inset:8px 8px 4px}
  .info-sec{padding:10px 12px 14px}
  .reading-val{font-size:32px}
  .advice{font-size:14px}
  .history-head{padding:12px 12px 8px}
  .history-table th,.history-table td{padding:9px 10px;font-size:12px}
  .saved-grid{grid-template-columns:1fr;padding:12px 12px 20px}
  .saved-list{max-height:320px}
  .saved-detail{min-height:0}
  .saved-chart-wrap{height:210px;padding:8px 10px 4px}
  .saved-table-wrap{padding:0 12px 12px}
  .grid.vertical + .history-panel{max-width:none;margin-top:16px}
}
</style>
</head>
<body>
<header>
  <div class="logo">
    <div class="logo-icon">⬡</div>
    <div>
      <div class="title-row">
        <h1>ESP32 Cloud Monitor</h1>
      </div>
      <div class="sub">Temperature, humidity, and gas data in one view</div>
    </div>
  </div>
  <div class="hdr-r">
    <div class="meta">POWERED BY ARDUINO CLOUD</div>
    <div class="meta" id="ipLine">IP: waiting...</div>
    <button class="dl-btn" id="layoutBtn" onclick="toggleLayout()">VERTICAL VIEW</button>
    <button class="dl-btn" onclick="dlCSV()">CSV DOWNLOAD</button>
    <div class="sig" id="sigEl">
      <div class="sig-dot" id="sigDot" style="background:var(--orange);box-shadow:0 0 7px var(--orange)"></div>
      <span class="sig-lbl" id="sigLbl" style="color:var(--orange)">LIVE</span>
    </div>
  </div>
</header>
<div class="main">
  <div class="live-controls">
    <div class="meta">LIVE RANGE</div>
    <select class="live-select" id="liveRangeSelect" onchange="setLiveWindow(this.value)">
      <option value="60000">1 Minute</option>
      <option value="180000" selected>3 Minutes</option>
      <option value="300000">5 Minutes</option>
      <option value="600000">10 Minutes</option>
      <option value="1800000">30 Minutes</option>
      <option value="3600000">1 Hour</option>
    </select>
  </div>
  <div class="grid">
    <section class="panel">
      <div class="panel-head">
        <div>
          <div class="panel-title">Temperature</div>
          <div class="panel-sub">Target graph range: 0 to 100 °C</div>
        </div>
        <div id="tB" class="badge" style="color:var(--muted);border-color:var(--muted)">INIT</div>
      </div>
      <div class="chart-wrap">
        <canvas id="tC"></canvas>
        <div class="ov" id="tOv"><div class="spinner"></div><span>WAITING FOR SENSOR</span></div>
      </div>
      <div class="info-sec">
        <div class="metrics">
          <div>
            <div class="reading-lbl">Current Temperature</div>
            <div class="reading-val" id="tV">--<span class="reading-unit"> °C</span></div>
            <div class="reading-time" id="tT">waiting...</div>
          </div>
        </div>
        <div class="advice" id="tA">Connecting to sensor...</div>
        <div class="guide">
          <div class="guide-title">Ranges</div>
          <ul>
            <li><strong>Below 10 C:</strong> very cold</li>
            <li><strong>10 to 33 C:</strong> stable</li>
            <li><strong>34 to 42 C:</strong> warm</li>
            <li><strong>43 to 50 C:</strong> danger</li>
            <li><strong>Above 50 C:</strong> extreme danger</li>
          </ul>
        </div>
      </div>
    </section>

    <section class="panel">
      <div class="panel-head">
        <div>
          <div class="panel-title">Humidity</div>
          <div class="panel-sub">Target graph range: 0 to 100 %</div>
        </div>
        <div id="hB" class="badge" style="color:var(--muted);border-color:var(--muted)">INIT</div>
      </div>
      <div class="chart-wrap">
        <canvas id="hC"></canvas>
        <div class="ov" id="hOv"><div class="spinner"></div><span>WAITING FOR SENSOR</span></div>
      </div>
      <div class="info-sec">
        <div class="metrics">
          <div>
            <div class="reading-lbl">Current Humidity</div>
            <div class="reading-val" id="hV">--<span class="reading-unit"> %</span></div>
            <div class="reading-time" id="hT">waiting...</div>
          </div>
        </div>
        <div class="advice" id="hA">Connecting to sensor...</div>
        <div class="guide">
          <div class="guide-title">Ranges</div>
          <ul>
            <li><strong>Below 30%:</strong> dry</li>
            <li><strong>30 to 70%:</strong> stable</li>
            <li><strong>71 to 85%:</strong> humid</li>
            <li><strong>Above 85%:</strong> very humid</li>
          </ul>
        </div>
      </div>
    </section>

    <section class="panel">
      <div class="panel-head">
        <div>
          <div class="panel-title">Gas</div>
          <div class="panel-sub">Target graph range: 0 to 1224 ADC</div>
        </div>
        <div id="gB" class="badge" style="color:var(--muted);border-color:var(--muted)">INIT</div>
      </div>
      <div class="chart-wrap">
        <canvas id="gC"></canvas>
        <div class="ov" id="gOv"><div class="spinner"></div><span>WAITING FOR SENSOR</span></div>
      </div>
      <div class="info-sec">
        <div class="metrics">
          <div>
            <div class="reading-lbl">Current Gas Level</div>
            <div class="reading-val" id="gV">--</div>
            <div class="reading-time" id="gT">waiting...</div>
          </div>
        </div>
        <div class="advice" id="gA">Connecting to sensor...</div>
        <div class="guide">
          <div class="guide-title">Ranges</div>
          <ul>
            <li><strong>Below 600:</strong> stable</li>
            <li><strong>600 to 799:</strong> caution</li>
            <li><strong>800 to 899:</strong> danger</li>
            <li><strong>900 and above:</strong> extreme danger</li>
          </ul>
        </div>
      </div>
    </section>
  </div>

  <section class="history-panel">
    <div class="history-head">
      <div>
        <div class="history-title">Saved Sessions</div>
        <div class="history-sub">Google Sheets backed sessions, click one to inspect static data</div>
      </div>
    </div>
    <div class="saved-grid">
      <div class="saved-list" id="savedSessionsList">
        <div class="saved-empty">Loading saved sessions...</div>
      </div>
      <div class="saved-detail">
        <div class="saved-summary">
          <div class="saved-summary-title" id="savedTitle">Select a saved session</div>
          <div class="saved-summary-meta" id="savedMeta">Saved readings will appear here.</div>
        </div>
        <div class="saved-loading" id="savedLoading"><div class="saved-spinner"></div><div>LOADING SAVED SESSION</div></div>
        <div class="saved-chart-wrap"><canvas id="savedSessionChart"></canvas></div>
        <div class="saved-table-wrap">
          <table class="saved-table">
            <thead>
              <tr><th>Date, Time</th><th>Temperature</th><th>Humidity</th><th>Gas</th></tr>
            </thead>
            <tbody id="savedTableBody">
              <tr><td colspan="4">No saved session selected.</td></tr>
            </tbody>
          </table>
        </div>
      </div>
    </div>
  </section>
</div>
<script>
function setSig(mode){
  const dot=document.getElementById('sigDot'), lbl=document.getElementById('sigLbl');
  if(mode==='live'){dot.style.background='var(--green)';dot.style.boxShadow='0 0 7px var(--green)';lbl.style.color='var(--green)';lbl.textContent='LIVE';}
  else if(mode==='conn'){dot.style.background='var(--orange)';dot.style.boxShadow='0 0 7px var(--orange)';lbl.style.color='var(--orange)';lbl.textContent='CONNECTING';}
  else{dot.style.background='var(--red)';dot.style.boxShadow='0 0 7px var(--red)';lbl.style.color='var(--red)';lbl.textContent='NO SIGNAL';}
}
const KEYS={t:'temp',h:'hum',g:'gas'};
const colors={t:'#00c8ff',h:'#00ff9d',g:'#ff7b00'};
const units={t:' °C',h:' %',g:''};
const limits={t:{min:0,max:100},h:{min:0,max:100},g:{min:0,max:1224}};
const bands={
  t:[
    {from:0,to:10,color:'rgba(68,136,255,0.16)'},
    {from:10,to:33,color:'rgba(0,255,157,0.12)'},
    {from:33,to:42,color:'rgba(255,215,0,0.12)'},
    {from:42,to:50,color:'rgba(255,123,0,0.12)'},
    {from:50,to:100,color:'rgba(255,46,91,0.14)'}
  ],
  h:[
    {from:0,to:30,color:'rgba(255,215,0,0.12)'},
    {from:30,to:70,color:'rgba(0,255,157,0.12)'},
    {from:70,to:85,color:'rgba(255,123,0,0.12)'},
    {from:85,to:100,color:'rgba(255,46,91,0.14)'}
  ],
  g:[
    {from:0,to:600,color:'rgba(0,255,157,0.12)'},
    {from:600,to:800,color:'rgba(255,215,0,0.12)'},
    {from:800,to:900,color:'rgba(255,123,0,0.12)'},
    {from:900,to:1224,color:'rgba(255,46,91,0.14)'}
  ]
};
let liveWindowMs=3*60*1000;
let savedSessionChart;
let currentSessionId='';
let savedLoading=false;
let currentSessionLoaded=false;
function addPoint(ds, point){
  const exists=ds.some(p=>p.x===point.x);
  if(!exists) ds.push(point);
  ds.sort((a,b)=>a.x-b.x);
}
function toggleLayout(){
  const grid=document.querySelector('.grid');
  const btn=document.getElementById('layoutBtn');
  const vertical=grid.classList.toggle('vertical');
  btn.textContent=vertical ? 'HORIZONTAL VIEW' : 'VERTICAL VIEW';
  setTimeout(()=>{Object.values(charts).forEach(c=>c.resize()); if(savedSessionChart) savedSessionChart.resize();},120);
}
function updateLiveAxes(){
  const now=Date.now();
  Object.values(charts).forEach(c=>{
    const ds=c.data.datasets[0].data;
    const firstX=ds.length ? ds[0].x : now-liveWindowMs;
    c.options.scales.x.min=Math.max(now-liveWindowMs, Math.min(firstX, now));
    c.options.scales.x.max=now;
    c.update('none');
  });
}
function setLiveWindow(ms){
  liveWindowMs=Number(ms);
  const select=document.getElementById('liveRangeSelect');
  if(select && String(select.value)!==String(ms)) select.value=String(ms);
  updateLiveAxes();
}
function setSavedLoading(on){
  savedLoading=on;
  const overlay=document.getElementById('savedLoading');
  const list=document.getElementById('savedSessionsList');
  if(overlay) overlay.classList.toggle('show', on);
  if(list) list.querySelectorAll('.saved-item').forEach(item=>item.classList.toggle('disabled', on));
}
function parseBackendTime(ts){
  if(!ts) return null;
  const m=String(ts).match(/^(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})$/);
  if(m){
    return new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]), Number(m[4]), Number(m[5]), Number(m[6]));
  }
  const d=new Date(String(ts));
  return isNaN(d.getTime()) ? null : d;
}
function formatReadableDate(ts){
  const d=parseBackendTime(ts);
  if(!d) return String(ts || '');
  return d.toLocaleDateString()+' '+d.toLocaleTimeString();
}
function loadCurrentSessionHistory(sessionId){
  if(!sessionId || currentSessionLoaded) return;
  fetch('/backend/readings?session_id='+encodeURIComponent(sessionId)).then(r=>r.json()).then(data=>{
    const rows=(data.readings||[]).slice().sort((a,b)=>(Number(a.uptime_ms)||0) - (Number(b.uptime_ms)||0));
    if(!rows.length) return;
    const lastUptime=Number(rows[rows.length-1].uptime_ms || 0);
    const now=Date.now();
    rows.forEach(r=>{
      const x=now-(lastUptime-Number(r.uptime_ms||0));
      addPoint(charts.t.data.datasets[0].data,{x:x,y:Number(r.temperature_c||0)});
      addPoint(charts.h.data.datasets[0].data,{x:x,y:Number(r.humidity_pct||0)});
      addPoint(charts.g.data.datasets[0].data,{x:x,y:Number(r.gas_adc||0)});
    });
    Object.values(charts).forEach(c=>c.update('none'));
    currentSessionLoaded=true;
  }).catch(()=>{});
}
const rangeBandPlugin={
  id:'rangeBandPlugin',
  beforeDraw(chart,args,pluginOptions){
    const {ctx,chartArea,scales}=chart;
    if(!chartArea || !scales.y) return;
    const key=pluginOptions.key;
    const defs=bands[key] || [];
    ctx.save();
    defs.forEach(b=>{
      const top=scales.y.getPixelForValue(b.to);
      const bottom=scales.y.getPixelForValue(b.from);
      ctx.fillStyle=b.color;
      ctx.fillRect(chartArea.left, top, chartArea.right-chartArea.left, bottom-top);
    });
    ctx.restore();
  }
};
function mkChart(id,key,label){
  return new Chart(document.getElementById(id),{
    type:'line',
    data:{datasets:[{label,data:[],borderColor:colors[key],backgroundColor:colors[key],pointRadius:0,tension:.25,borderWidth:2,fill:false}]},
    options:{
      responsive:true,maintainAspectRatio:false,animation:false,
      plugins:{legend:{display:false},rangeBandPlugin:{key:key}},
      scales:{
        x:{type:'time',ticks:{color:'#b8d0e0',maxTicksLimit:6},grid:{color:'rgba(255,255,255,0.06)'}},
        y:{min:limits[key].min,max:limits[key].max,ticks:{color:'#b8d0e0'},grid:{color:'rgba(255,255,255,0.06)'}}
      }
    },
    plugins:[rangeBandPlugin]
  });
}
const charts={
  t:mkChart('tC','t','Temperature'),
  h:mkChart('hC','h','Humidity'),
  g:mkChart('gC','g','Gas')
};
savedSessionChart=new Chart(document.getElementById('savedSessionChart'),{
  type:'line',
  data:{datasets:[
    {label:'Temperature',data:[],borderColor:'#00c8ff',pointRadius:0,tension:.2,borderWidth:2},
    {label:'Humidity',data:[],borderColor:'#00ff9d',pointRadius:0,tension:.2,borderWidth:2},
    {label:'Gas',data:[],borderColor:'#ff7b00',pointRadius:0,tension:.2,borderWidth:2,yAxisID:'y1'}
  ]},
  options:{
    responsive:true,maintainAspectRatio:false,animation:false,
    plugins:{legend:{labels:{color:'#cfe4f5'}}},
    scales:{
      x:{type:'time',ticks:{color:'#b8d0e0'},grid:{color:'rgba(255,255,255,0.06)'}},
      y:{min:0,max:100,ticks:{color:'#b8d0e0'},grid:{color:'rgba(255,255,255,0.06)'}},
      y1:{position:'right',min:0,max:1224,ticks:{color:'#ffb27a'},grid:{drawOnChartArea:false}}
    }
  }
});
function stamp(){ return new Date().toLocaleTimeString(); }
function adviceHtml(title, nowLine, avoidLine, dangerLine){
  return '<div class="advice-title">'+title+'</div>'+
         '<div class="advice-line">'+nowLine+'</div>'+
         '<div class="advice-line"><strong>Avoid issues:</strong> '+avoidLine+'</div>'+
         '<div class="advice-line"><strong>If in danger:</strong> '+dangerLine+'</div>';
}
function getProfile(kind,val){
  if(kind==='t'){
    if(val<10) return {badge:'TOO COLD',color:'#4488ff',html:adviceHtml('Temperature Low','Temperature is too low right now.','Keep the unit away from cold drafts and sudden exposure.','Move to a safer area and reduce cold exposure immediately.')};
    if(val<=33) return {badge:'STABLE',color:'var(--green)',html:adviceHtml('Temperature is stable','Temperature is stable right now.','Keep airflow normal and avoid placing the device near direct heat.','No immediate danger. Keep monitoring.')};
    if(val<=42) return {badge:'WARM',color:'var(--yellow)',html:adviceHtml('Temperature High','Temperature is getting warm.','Improve airflow and keep heat sources away from the enclosure.','Pause nearby heat-producing activity and cool the area down.')};
    if(val<=50) return {badge:'DANGER',color:'var(--orange)',html:adviceHtml('Temperature High','Temperature is in the danger range.','Use ventilation, reduce load, and keep the enclosure out of direct sunlight.','Stop operation if needed, ventilate fast, and move people away from the heat source.')};
    return {badge:'EXTREME',color:'var(--red)',html:adviceHtml('Temperature High','Temperature is extremely high.','Prevent buildup by keeping the area ventilated and away from heat sources.','Shut down the setup, move away from the source, and cool the area immediately.')};
  }
  if(kind==='h'){
    if(val<30) return {badge:'DRY',color:'var(--yellow)',html:adviceHtml('Humidity Low','Humidity is lower than ideal.','Keep the setup away from very dry airflow and harsh direct heat.','Move the setup to a more stable area and limit prolonged dry exposure.')};
    if(val<=70) return {badge:'STABLE',color:'var(--green)',html:adviceHtml('Humidity is stable','Humidity is stable right now.','Keep the enclosure dry and maintain normal ventilation.','No immediate danger. Continue monitoring.')};
    if(val<=85) return {badge:'HUMID',color:'var(--orange)',html:adviceHtml('Humidity High','Humidity is getting high.','Improve ventilation and avoid trapping moisture inside the enclosure.','Dry the area, improve airflow, and check for condensation.')};
    return {badge:'DANGER',color:'var(--red)',html:adviceHtml('Humidity High','Humidity is in a dangerous range.','Seal exposed metal properly and avoid damp environments.','Power down if condensation appears, dry the unit, and ventilate the area.')};
  }
  if(val<600) return {badge:'STABLE',color:'var(--green)',html:adviceHtml('Gas level is stable','Gas concentration appears stable.','Keep the area ventilated and avoid enclosed buildup.','No immediate danger. Continue monitoring.')};
  if(val<800) return {badge:'CAUTION',color:'var(--yellow)',html:adviceHtml('Gas level elevated','Gas reading is above the normal range.','Improve ventilation and keep possible gas sources away from the setup.','Ventilate the area and prepare to step back if the value keeps rising.')};
  if(val<900) return {badge:'DANGER',color:'var(--orange)',html:adviceHtml('Gas level dangerous','Gas concentration is in the danger range.','Avoid confined spaces and keep fresh air moving through the area.','Ventilate immediately, reduce exposure, and move people away from the source.')};
  return {badge:'EXTREME',color:'var(--red)',html:adviceHtml('Gas level critical','Gas concentration is extremely high.','Prevent buildup by monitoring in a well-ventilated area.','Evacuate the area, ventilate aggressively, and seek assistance if needed.')};
}
function updateUI(p,val,overlay){
  document.getElementById(p+'V').innerHTML = val + (units[p] ? '<span class="reading-unit">'+units[p]+'</span>' : '');
  document.getElementById(p+'T').textContent = 'updated ' + stamp();
  overlay.classList.add('gone');
  const profile=getProfile(p,val);
  const b=document.getElementById(p+'B');
  const a=document.getElementById(p+'A');
  b.textContent=profile.badge;
  b.style.color=profile.color;
  b.style.borderColor=profile.color;
  a.innerHTML=profile.html;
  a.style.borderLeftColor=profile.color;
}
fetch('/history').then(r=>r.json()).then(rows=>{
  if(!rows.length) return;
  const last=rows[rows.length-1].ms, now=Date.now();
  rows.forEach(d=>{
    const x=now-(last-d.ms);
    Object.keys(charts).forEach(p=>{
      const y=d[KEYS[p]];
      addPoint(charts[p].data.datasets[0].data,{x,y});
    });
  });
  Object.values(charts).forEach(c=>c.update('none'));
}).catch(()=>{});
function dlCSV(){
  fetch('/download').then(r=>{
    const uptime=parseInt(r.headers.get('X-Uptime')||'0');
    const bootWall=Date.now()-uptime;
    return r.json().then(rows=>({rows,bootWall}));
  }).then(({rows,bootWall})=>{
    const lines=['datetime,temp_C,hum_pct,gas_adc'];
    rows.forEach(d=>{
      const dt=new Date(bootWall+d.ms);
      const stamp=dt.getFullYear()+'-'+String(dt.getMonth()+1).padStart(2,'0')+'-'+String(dt.getDate()).padStart(2,'0')+' '+String(dt.getHours()).padStart(2,'0')+':'+String(dt.getMinutes()).padStart(2,'0')+':'+String(dt.getSeconds()).padStart(2,'0');
      lines.push(stamp+','+d.temp+','+d.hum+','+d.gas);
    });
    const blob=new Blob([lines.join('\r\n')],{type:'text/csv'});
    const a=document.createElement('a');
    a.href=URL.createObjectURL(blob);
    a.download='sensor_log.csv';
    a.click();
    URL.revokeObjectURL(a.href);
  }).catch(()=>alert('Download failed'));
}
let fails=0;
function poll(){
  fetch('/data').then(r=>{ if(!r.ok) throw 0; return r.json(); }).then(d=>{
    fails=0;
    setSig(d.wifi ? 'live' : 'conn');
    document.getElementById('ipLine').textContent='IP: '+(d.ip || 'unknown');
    if(d.session_id && currentSessionId !== d.session_id){
      const changed = currentSessionId.length > 0;
      currentSessionId = d.session_id;
      currentSessionLoaded = false;
      loadCurrentSessionHistory(currentSessionId);
      if(changed) loadSavedSessions();
    } else if (d.session_id && !currentSessionLoaded) {
      loadCurrentSessionHistory(d.session_id);
    }
    const now=Date.now(), winStart=now-liveWindowMs;
    Object.keys(charts).forEach(p=>{
      const val=parseFloat(d[KEYS[p]]);
      if(isNaN(val) || val===0) return;
      const ds=charts[p].data.datasets[0].data;
      addPoint(ds,{x:now,y:val});
      while(ds.length && ds[0].x < winStart-5000) ds.shift();
      updateLiveAxes();
      updateUI(p,val,document.getElementById(p+'Ov'));
    });
  }).catch(()=>{ if(++fails>=4) setSig('nosig'); else setSig('conn'); });
}
function renderSavedSessions(items){
  const list=document.getElementById('savedSessionsList');
  const filtered=(items||[]).filter(s=>!currentSessionId || s.session_id !== currentSessionId);
  if(!filtered.length){
    list.innerHTML='<div class="saved-empty">No saved sessions yet.</div>';
    document.getElementById('savedTitle').textContent='No saved session available';
    document.getElementById('savedMeta').textContent='The current live session is excluded from this list.';
    document.getElementById('savedTableBody').innerHTML='<tr><td colspan="4">No saved session selected.</td></tr>';
    savedSessionChart.data.datasets.forEach(ds=>ds.data=[]);
    savedSessionChart.update('none');
    return;
  }
  list.innerHTML=filtered.map((s,i)=>'<div class="saved-item'+(i===0?' active':'')+'" data-session="'+s.session_id+'">'+
    '<div class="saved-row">'+
      '<div>'+
        '<div class="saved-name">'+formatReadableDate(s.started_at)+'</div>'+
        '<div class="saved-meta">ID: '+s.session_id+' | '+s.sample_count+' samples</div>'+
      '</div>'+
      '<button class="delete-btn" data-delete="'+s.session_id+'">DELETE</button>'+
    '</div>'+
  '</div>').join('');
  list.querySelectorAll('.saved-item').forEach(el=>el.onclick=ev=>{ if(savedLoading) return; if(ev.target && ev.target.dataset && ev.target.dataset.delete) return; loadSavedSession(el.dataset.session,el); });
  list.querySelectorAll('.delete-btn').forEach(btn=>btn.onclick=ev=>{ ev.stopPropagation(); deleteSavedSession(btn.dataset.delete); });
  loadSavedSession(filtered[0].session_id,list.querySelector('.saved-item'));
}
function loadSavedSessions(){
  fetch('/backend/sessions').then(r=>r.json()).then(data=>renderSavedSessions(data.sessions||[])).catch(()=>{
    document.getElementById('savedSessionsList').innerHTML='<div class="saved-empty">Failed to load saved sessions.</div>';
  });
}
function deleteSavedSession(sessionId){
  if(savedLoading) return;
  if(!confirm('Delete session '+sessionId+'?')) return;
  setSavedLoading(true);
  fetch('/backend/delete-session?session_id='+encodeURIComponent(sessionId), { method:'POST' })
    .then(r=>r.json())
    .then(data=>{
      if(!data.ok) throw new Error('delete failed');
      loadSavedSessions();
    })
    .catch(()=>{ alert('Failed to delete session'); })
    .finally(()=>setSavedLoading(false));
}
function loadSavedSession(sessionId,el){
  if(savedLoading) return;
  document.querySelectorAll('.saved-item').forEach(item=>item.classList.remove('active'));
  if(el) el.classList.add('active');
  setSavedLoading(true);
  fetch('/backend/readings?session_id='+encodeURIComponent(sessionId)).then(r=>r.json()).then(data=>{
    const rows=(data.readings||[]).slice().sort((a,b)=>{
      const ax=parseBackendTime(a.timestamp); const bx=parseBackendTime(b.timestamp);
      return (ax?ax.getTime():0) - (bx?bx.getTime():0);
    });
    document.getElementById('savedTitle').textContent=sessionId;
    document.getElementById('savedMeta').textContent='Saved samples: '+rows.length;
    document.getElementById('savedTableBody').innerHTML=rows.length ? rows.slice().reverse().map(r=>'<tr><td>'+formatReadableDate(r.timestamp)+'</td><td>'+Number(r.temperature_c).toFixed(1)+' °C</td><td>'+Number(r.humidity_pct).toFixed(0)+' %</td><td>'+Number(r.gas_adc).toFixed(0)+'</td></tr>').join('') : '<tr><td colspan="4">No rows for this session.</td></tr>';
    const tData=rows.map(r=>{ const d=parseBackendTime(r.timestamp); return d ? {x:d.getTime(),y:Number(r.temperature_c)} : null; }).filter(Boolean);
    const hData=rows.map(r=>{ const d=parseBackendTime(r.timestamp); return d ? {x:d.getTime(),y:Number(r.humidity_pct)} : null; }).filter(Boolean);
    const gData=rows.map(r=>{ const d=parseBackendTime(r.timestamp); return d ? {x:d.getTime(),y:Number(r.gas_adc)} : null; }).filter(Boolean);
    savedSessionChart.data.datasets[0].data=tData;
    savedSessionChart.data.datasets[1].data=hData;
    savedSessionChart.data.datasets[2].data=gData;
    const allX=tData.map(p=>p.x);
    if(allX.length){
      savedSessionChart.options.scales.x.min=Math.min.apply(null, allX);
      savedSessionChart.options.scales.x.max=Math.max.apply(null, allX);
    } else {
      delete savedSessionChart.options.scales.x.min;
      delete savedSessionChart.options.scales.x.max;
    }
    savedSessionChart.update('none');
  }).catch(()=>{
    document.getElementById('savedMeta').textContent='Failed to load saved session.';
  }).finally(()=>setSavedLoading(false));
}
poll();
loadSavedSessions();
setInterval(poll,2000);
</script>
</body>
</html>
)HTMLEOF";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void sampleSensors() {
  latestG = analogRead(GAS_PIN);
  haveGas = true;

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(t) && t > 0) {
    latestT = t;
    haveTemp = true;
  }

  if (!isnan(h) && h > 0) {
    latestH = h;
    haveHum = true;
  }

  hist[histIdx] = { millis(), latestT, latestH, latestG };
  histIdx = (histIdx + 1) % HIST_N;
  if (histIdx == 0) histFull = true;

  Serial.printf("T=%.1f C  H=%.0f %%  G=%d\n", latestT, latestH, latestG);
}

void pushToCloud() {
  if (haveTemp) cloudTemperature = latestT;
  if (haveHum)  cloudHumidity = latestH;
  if (haveGas)  cloudGas = (float)latestG;
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.on("/download", handleDownload);
  server.on("/backend/sessions", handleBackendSessions);
  server.on("/backend/readings", handleBackendReadings);
  server.on("/backend/delete-session", handleBackendDeleteSession);
  server.begin();
}

void printNetworkInfo() {
  Serial.println("========================");
  Serial.print("WiFi : ");
  Serial.println(SSID);
  Serial.print("IP   : http://");
  Serial.println(WiFi.localIP());
  Serial.println("========================");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  dht.begin();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  delay(2000);

  initProperties();

  Serial.println("Connecting WiFi first...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    printNetworkInfo();
    initClock();
    initSessionIdentity();
    Serial.print("Session: ");
    Serial.println(sessionName);
    Serial.println("Starting Arduino Cloud...");
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    setDebugMessageLevel(2);
    ArduinoCloud.printDebugInfo();
  } else {
    initSessionIdentity();
    Serial.println("WiFi connection failed, local web server only.");
  }

  setupWebServer();

  Serial.println("Booting ESP32 Cloud Monitor...");
  lastSample = millis() - SAMPLE_MS;
  lastCloudPush = millis() - CLOUD_PUSH_MS;
}

void loop() {
  static bool cloudStarted = (WiFi.status() == WL_CONNECTED);

  if (!cloudStarted && WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi recovered. Starting Arduino Cloud...");
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    setDebugMessageLevel(2);
    ArduinoCloud.printDebugInfo();
    cloudStarted = true;
  }

  if (cloudStarted) {
    ArduinoCloud.update();
  }

  server.handleClient();

  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();
    sampleSensors();
  }

  if (millis() - lastCloudPush >= CLOUD_PUSH_MS) {
    lastCloudPush = millis();
    pushToCloud();
    postReadingToBackend();
  }
}
