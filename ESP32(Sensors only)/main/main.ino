#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ===== AP SETTINGS =====
const char* AP_SSID = "ESP32-SENSORS";
const char* AP_PASS = "12345678";

WebServer server(80);

// ===== DHT11 =====
#define DHTPIN 33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== MQ135 (Analog) =====
#define GAS_PIN 32

// Latest readings
volatile float latestT = NAN;
volatile float latestH = NAN;
volatile int   latestG = 0;

// Simple history buffer
struct Sample {
  unsigned long ms;
  float t;
  float h;
  int g;
};
const int HISTORY_N = 120;
Sample hist[HISTORY_N];
int histIdx = 0;
bool histFull = false;

// Timing
unsigned long lastSample = 0;
const unsigned long sampleInterval = 1000;

String jsonFloat(float v, int decimals) {
  if (isnan(v)) return "null";
  return String(v, decimals);
}

void handleData() {
  String out = "{";
  out += "\"temp\":" + jsonFloat(latestT, 1) + ",";
  out += "\"hum\":"  + jsonFloat(latestH, 0) + ",";
  out += "\"gas\":"  + String(latestG);
  out += "}";
  server.send(200, "application/json", out);
}

void handleHistory() {
  String out = "[";
  int count = histFull ? HISTORY_N : histIdx;

  int start = histFull ? histIdx : 0; // oldest -> newest
  for (int i = 0; i < count; i++) {
    int j = (start + i) % HISTORY_N;
    out += "{";
    out += "\"ms\":" + String(hist[j].ms) + ",";
    out += "\"temp\":" + jsonFloat(hist[j].t, 1) + ",";
    out += "\"hum\":"  + jsonFloat(hist[j].h, 0) + ",";
    out += "\"gas\":"  + String(hist[j].g);
    out += "}";
    if (i != count - 1) out += ",";
  }
  out += "]";
  server.send(200, "application/json", out);
}

void handleRoot() {
  String page =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 Sensors</title></head><body>"
    "<h2>ESP32 Sensors</h2>"
    "<pre id='p'>Loading...</pre>"
    "<script>"
    "async function tick(){"
    "  const r=await fetch('/data');"
    "  const d=await r.json();"
    "  document.getElementById('p').textContent="
    "    'Temp: '+d.temp+' C\\nHum: '+d.hum+' %\\nGas: '+d.gas;"
    "}"
    "tick(); setInterval(tick,1000);"
    "</script></body></html>";

  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);

  dht.begin();

  // ADC config
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP SSID: "); Serial.println(AP_SSID);
  Serial.print("AP IP: ");   Serial.println(ip);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.begin();

  lastSample = millis() - sampleInterval; // take first sample immediately
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // ---- Sample sensors every 1s ----
  if (now - lastSample >= sampleInterval) {
    lastSample = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int gas = analogRead(GAS_PIN);

    latestT = t;
    latestH = h;
    latestG = gas;

    hist[histIdx] = { now, t, h, gas };
    histIdx = (histIdx + 1) % HISTORY_N;
    if (histIdx == 0) histFull = true;

    Serial.print("T="); Serial.print(t);
    Serial.print(" H="); Serial.print(h);
    Serial.print(" G="); Serial.println(gas);
  }
}
