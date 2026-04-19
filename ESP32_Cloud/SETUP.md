# ESP32 Cloud Setup

## What changed

This version does 3 things at once:

- connects to your normal WiFi with internet
- sends sensor data to Arduino Cloud
- keeps the local webpage and CSV download

So this is no longer AP mode. The ESP32 will join your router WiFi instead.

---

## Files

- `ESP32_Cloud.ino` -> main sketch

---

## Pin choice

I changed the DHT11 pin from GPIO 33 to GPIO 4.

### New recommended pins

- DHT11 DATA -> GPIO 4
- MQ135 AO -> GPIO 32

Reason:
- GPIO 4 and GPIO 32 are physically less annoying to deal with than 33 and 32 together
- better for perfboard planning
- easier to separate wires when soldering

### Full wiring

#### DHT11
- VCC -> 3.3V
- GND -> GND
- DATA -> GPIO 4
- if bare DHT11, add 10k pull-up from DATA to VCC

#### MQ135
- VCC -> 5V preferred
- GND -> GND
- AO -> GPIO 32
- DO -> not used

## Important warning for MQ135

ESP32 GPIO is 3.3V only.

If your MQ135 module is powered from 5V, its AO may go too high for the ESP32.

### Safer choices

#### Option A, easier
- power MQ135 with 3.3V
- connect AO directly to GPIO 32

#### Option B, better practice
- power MQ135 with 5V
- use a voltage divider between AO and GPIO 32

Example divider:
- 20k from AO to GPIO32
- 10k from GPIO32 to GND

---

## Arduino Cloud setup

You said Arduino Cloud is not set up yet, so here is the clean path.

### 1. Make an Arduino account
Go to:
- https://cloud.arduino.cc

Sign in or create account.

### 2. Create a Thing
Inside Arduino Cloud:
- Create new Thing
- Name it something like `ESP32 Gas Monitor`

### 3. Add device
- Add device
- Choose **3rd party device**
- Choose **ESP32**
- Follow the device registration steps

Arduino Cloud will give you values such as:
- Device ID / login name
- Secret device key
- Thing ID

Put those into `ESP32_Cloud.ino` here:

```cpp
const char DEVICE_LOGIN_NAME[] = "YOUR_DEVICE_LOGIN_NAME";
const char DEVICE_KEY[]        = "YOUR_DEVICE_KEY";
const char THING_ID[]          = "YOUR_THING_ID";
```

### 4. Create cloud variables
Create these variables in the Thing:

- `cloudTemperature` -> float, Read Only
- `cloudHumidity` -> float, Read Only
- `cloudGas` -> int, Read Only

These names should match the code exactly.

### 5. Put your WiFi credentials in code
Replace:

```cpp
const char WIFI_SSID[] = "YOUR_WIFI_NAME";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
```

### 6. Install required libraries in Arduino IDE
Open Arduino IDE, then install these libraries if missing:

- `ArduinoIoTCloud`
- `Arduino_ConnectionHandler`
- `DHT sensor library`
- `Adafruit Unified Sensor`

`WiFi.h` and `WebServer.h` are already part of ESP32 board support.

### 7. Install ESP32 board package if needed
In Arduino IDE:
- Boards Manager
- install `esp32` by Espressif

### 8. Upload the code
- select your ESP32 board
- select COM port
- upload
- open Serial Monitor at **115200** baud

When connected, it should print something like:

- WiFi name
- local IP address

---

## How to open the webpage now

Since this is no longer AP mode:

- connect your laptop/phone to the same WiFi as the ESP32
- check Serial Monitor
- open the printed IP in browser

Example:
- `http://192.168.1.23/`

Routes still work:
- `/`
- `/data`
- `/history`
- `/download`

---

## Cloud + webpage behavior

This project now has two outputs:

### Local
- browser dashboard on ESP32 local IP
- CSV download from local device

### Cloud
- Arduino Cloud receives:
  - temperature
  - humidity
  - gas ADC

So if internet is available, you get cloud dashboard.
If local network is available, you still get your direct web interface.

---

## Important note about gas value

Right now `cloudGas` is raw ADC value, not calibrated PPM.

That is fine for now.
For a school project or prototype, raw ADC trend data is acceptable.
If later you want real gas concentration estimates, MQ135 calibration will need more work.

---

## Soldering concern

You are right to think ahead.

### Good news
GPIO 32 and GPIO 4 are easier to separate than 32 and 33.

### Better perfboard practice
- do not solder sensor modules directly flush against the ESP32 if avoidable
- use short color-coded wires
- keep power lines on one side, signal lines on another
- label the perfboard with marker before soldering
- test on breadboard first, then copy exactly

### My recommendation for enclosure build
- keep connectors or header pins if possible
- do not hard-solder the sensors permanently on first try
- make the sensor modules replaceable

That will save you pain when one sensor acts stupid later, which they often do.

---

## If upload fails or cloud does not connect

Check these first:

- wrong WiFi SSID/password
- wrong Thing ID / Device ID / Device Key
- missing libraries
- wrong ESP32 board selected
- Serial Monitor baud not 115200

---

## Next logical upgrade later

When you are ready, the next improvement should be:

- move WiFi and cloud secrets into a separate `secrets` file
- add reconnection status on webpage
- maybe store more than 120 samples
- maybe add calibration notes for MQ135

For now, this version is the right base.
