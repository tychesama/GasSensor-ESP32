#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ===== AP SETTINGS =====
const char* AP_SSID = "ESP32-SENSORS";
const char* AP_PASS = "12345678"; // min 8 chars

WebServer server(80);

// ===== LCD (I2C) =====
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ===== DHT11 =====
#define DHTPIN 16
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== MQ135 (Analog) =====
#define GAS_PIN 34

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
const int HISTORY_N = 120; // ~120 seconds if sampling every 1s
Sample hist[HISTORY_N];
int histIdx = 0;
bool histFull = false;

// Timing
unsigned long lastSample = 0;
const unsigned long sampleInterval = 1000;

unsigned long lastRefresh = 0;
const unsigned long refreshInterval = 500;

unsigned long lastModeSwitch = 0;
const unsigned long modeInterval = 3000;

byte mode = 0;

void showMode(float t, float h, int gas) {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (mode == 0) {
    lcd.print("Temp:");
    lcd.setCursor(0, 1);
    if (isnan(t)) lcd.print("DHT error");
    else { lcd.print(t, 1); lcd.print((char)223); lcd.print("C"); }
  } else if (mode == 1) {
    lcd.print("Humidity:");
    lcd.setCursor(0, 1);
    if (isnan(h)) lcd.print("DHT error");
    else { lcd.print(h, 0); lcd.print("%"); }
  } else {
    lcd.print("Gas Level:");
    lcd.setCursor(0, 1);
    lcd.print(gas);
  }
}

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

  // output oldest->newest
  int start = histFull ? histIdx : 0;
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
  // Minimal page (no external files needed)
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

  // I2C for LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting...");

  dht.begin();

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Start Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP(); // usually 192.168.4.1
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AP:");
  lcd.print(AP_SSID);
  lcd.setCursor(0, 1);
  lcd.print(ip);

  // Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.begin();

  // First sample immediately
  lastSample = millis() - sampleInterval;
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Sample sensors every 1s
  if (now - lastSample >= sampleInterval) {
    lastSample = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int gas = analogRead(GAS_PIN);

    latestT = t;
    latestH = h;
    latestG = gas;

    // store history
    hist[histIdx] = { now, t, h, gas };
    histIdx = (histIdx + 1) % HISTORY_N;
    if (histIdx == 0) histFull = true;

    Serial.print("T="); Serial.print(t); Serial.print(" H=");
    Serial.print(h); Serial.print(" G=");
    Serial.println(gas);
  }

  // Auto cycle LCD
  if (now - lastModeSwitch >= modeInterval) {
    lastModeSwitch = now;
    mode = (mode + 1) % 3;
  }

  // Refresh LCD
  if (now - lastRefresh >= refreshInterval) {
    lastRefresh = now;
    showMode(latestT, latestH, latestG);
  }
}
