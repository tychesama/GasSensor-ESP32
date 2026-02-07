#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ===== LCD (I2C) =====
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ===== DHT11 =====
#define DHTPIN 16
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== MQ135 (Analog) =====
#define GAS_PIN 34

byte mode = 0;  // 0=Temp, 1=Humidity, 2=Gas

// Timing
unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;

unsigned long lastRefresh = 0;
const unsigned long refreshInterval = 500;

unsigned long lastModeSwitch = 0;
const unsigned long modeInterval = 3000;

void showMode(float t, float h, int gas);

void setup() {
  Serial.begin(2400);

  // I2C pins for ESP32 DevKit V1
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  analogReadResolution(12);        // 0..4095
  analogSetAttenuation(ADC_11db);  // ~0..3.3V range

  lcd.setCursor(0, 0);
  lcd.print("Starting...");
  delay(800);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  // Read sensors (DHT is slow; this is fine with our intervals)
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int gas = analogRead(GAS_PIN);

  // Auto switch mode every 3 seconds
  if (now - lastModeSwitch >= modeInterval) {
    lastModeSwitch = now;
    mode = (mode + 1) % 3;
  }

  // Refresh LCD
  if (now - lastRefresh >= refreshInterval) {
    lastRefresh = now;
    showMode(t, h, gas);
  }

  // in loop(), replace the Serial output block with this:
  if (now - lastSend >= sendInterval) {
    lastSend = now;

    // If DHT fails, send -1 placeholders but KEEP the format
    float outT = isnan(t) ? -1.0f : t;
    int outH = isnan(h) ? -1 : (int)roundf(h);

    Serial.print("T,");
    Serial.print(outT, 1);
    Serial.print(",H,");
    Serial.print(outH);
    Serial.print(",G,");
    Serial.println(gas);
  }
}

void showMode(float t, float h, int gas) {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (mode == 0) {
    lcd.print("Temp:");
    lcd.setCursor(0, 1);
    if (isnan(t)) lcd.print("DHT error");
    else {
      lcd.print(t, 1);
      lcd.print((char)223);
      lcd.print("C");
    }

  } else if (mode == 1) {
    lcd.print("Humidity:");
    lcd.setCursor(0, 1);
    if (isnan(h)) lcd.print("DHT error");
    else {
      lcd.print(h, 0);
      lcd.print("%");
    }

  } else {
    lcd.print("Gas Level:");
    lcd.setCursor(0, 1);
    lcd.print(gas);
  }
}
