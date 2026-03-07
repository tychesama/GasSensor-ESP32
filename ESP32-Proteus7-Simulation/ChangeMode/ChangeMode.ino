#include <LiquidCrystal.h>

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

// Buttons
#define BTN_NEXT 8
#define BTN_PREV 9
bool lastNextState = HIGH;
bool lastPrevState = HIGH;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 40;  // ms

// Send Data
static unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;  // 1 second

// Sensor pins
#define TEMP_PIN A0
#define HUMIDITY_PIN A1
#define GAS_PIN A2

byte mode = 0;
// 0 = Temperature
// 1 = Humidity
// 2 = Gas

void setup() {
  // LCD setup
  lcd.begin(16, 2);
  lcd.clear();

  // Button setup
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  // Serial for virtual terminal
  Serial.begin(2400);

  showMode();
}

void loop() {
  bool nextState = digitalRead(BTN_NEXT);
  bool prevState = digitalRead(BTN_PREV);

  unsigned long now = millis();

  // NEXT button
  if (lastNextState == HIGH && nextState == LOW) {
    if (now - lastDebounceTime > debounceDelay) {
      mode++;
      if (mode > 2) mode = 0;
      showMode();
      lastDebounceTime = now;
    }
  }

  // PREVIOUS button
  if (lastPrevState == HIGH && prevState == LOW) {
    if (now - lastDebounceTime > debounceDelay) {
      if (mode == 0) mode = 2;
      else mode--;
      showMode();
      lastDebounceTime = now;
    }
  }

  lastNextState = nextState;
  lastPrevState = prevState;

  // Periodic refresh only
  static unsigned long lastRefresh = 0;
  if (now - lastRefresh >= 500) {
    showMode();
    lastRefresh = now;
  }

  // Send Data
  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();

    float tempC = analogRead(TEMP_PIN) * (3.3 / 1023.0) * 100.0;

    int humVal = analogRead(HUMIDITY_PIN);
    float humPercent = humVal * 100.0 / 1023.0;

    int gasVal = analogRead(GAS_PIN);

    Serial.print("T,");
    Serial.print(tempC, 1);
    Serial.print(",H,");
    Serial.print(humPercent, 0);
    Serial.print(",G,");
    Serial.println(gasVal);
  }
}


// Function to read sensor values, display on LCD
void showMode() {
  lcd.clear();
  lcd.setCursor(0, 0);

  if (mode == 0) {
    lcd.print("Temp: ");
    float tempC = analogRead(TEMP_PIN) * (3.3 / 1023.0) * 100.0;
    lcd.setCursor(0, 1);
    lcd.print(tempC, 1);
    lcd.print((char)223);
    lcd.print("C");
  } else if (mode == 1)  // Humidity
  {
    lcd.print("Humidity:");
    int humVal = analogRead(HUMIDITY_PIN);
    float humPercent = humVal * 100.0 / 1023.0;
    lcd.setCursor(0, 1);
    lcd.print(humPercent, 0);
    lcd.print("%");
  } else if (mode == 2)  // Gas
  {
    lcd.print("Gas Level:");
    int gasVal = analogRead(GAS_PIN);
    lcd.setCursor(0, 1);
    lcd.print(gasVal);
  }
}
