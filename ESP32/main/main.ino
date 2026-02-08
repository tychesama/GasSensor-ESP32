#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ===== AP SETTINGS =====
const char* AP_SSID = "ESP32-SENSORS";
const char* AP_PASS = "12345678";

WebServer server(80);

// ===== LCD (I2C) =====
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ===== DHT11 =====
#define DHTPIN 33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== MQ135 (Analog) =====
#define GAS_PIN 32

// ===== BUTTONS =====
#define BTN_NEXT 25
#define BTN_PREV 26

bool lastNextState = HIGH;
bool lastPrevState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 40; // ms

byte mode = 0; // 0=Temp, 1=Humidity, 2=Gas

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

unsigned long lastRefresh = 0;
const unsigned long refreshInterval = 500;

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

  // I2C for LCD (change pins here if needed)
  Wire.begin(27, 14);
  lcd.init();
  lcd.backlight();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  dht.begin();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AP:");
  lcd.print(AP_SSID);
  lcd.setCursor(0, 1);
  lcd.print(ip);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/history", handleHistory);
  server.begin();

  lastSample = millis() - sampleInterval;
  showMode(latestT, latestH, latestG);
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // ---- Buttons (edge + debounce) ----
  bool nextState = digitalRead(BTN_NEXT);
  bool prevState = digitalRead(BTN_PREV);

  // NEXT
  if (lastNextState == HIGH && nextState == LOW) {
    if (now - lastDebounceTime > debounceDelay) {
      mode = (mode + 1) % 3;
      showMode(latestT, latestH, latestG);
      lastDebounceTime = now;
    }
  }

  // PREV
  if (lastPrevState == HIGH && prevState == LOW) {
    if (now - lastDebounceTime > debounceDelay) {
      mode = (mode == 0) ? 2 : (mode - 1);
      showMode(latestT, latestH, latestG);
      lastDebounceTime = now;
    }
  }

  lastNextState = nextState;
  lastPrevState = prevState;

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

  // ---- Refresh LCD values (no mode change) ----
  if (now - lastRefresh >= refreshInterval) {
    lastRefresh = now;
    showMode(latestT, latestH, latestG);
  }
}
