# GasSensor-ESP32

GasSensor-ESP32 is a completed academic IoT project for monitoring temperature, humidity, and gas sensor readings using ESP32-based hardware, a local web dashboard, a Google Sheets backend, and a Proteus 7 simulation version.

The project contains three main implementations:

1. **Proteus 7 simulation** — a simulated sensor/LCD setup for demonstration and testing.
2. **Actual ESP32 sensors-only build** — runs on real ESP32 hardware with a local dashboard and CSV/history features, but no external backend.
3. **ESP32 cloud/backend build** — runs on real ESP32 hardware with local dashboard support plus cloud/backend logging.

---

## Project status

Status: **completed**

This repository is kept as the final project archive, including source code, setup notes, backend script, and user manual files.

---

## What the system does

The system monitors environmental readings from connected sensors and displays/logs them through different project versions.

Main readings:

- Temperature
- Humidity
- Gas level / gas ADC value

Main outputs depending on version:

- LCD display in the Proteus simulation
- Local ESP32-hosted web dashboard
- JSON data routes
- History view
- CSV/download support
- Arduino Cloud variables
- Google Sheets backend logging

---

## Repository structure

```text
GasSensor-ESP32/
  ESP32-Proteus7-Simulation/
    ChangeMode/
      ChangeMode.ino
    Potentiometer.DSN
    Potentiometer.PWI
    bridge.py
    sensor.html
    sensor_data.csv

  ESP32_Sensors_only/
    main/
      main.ino

  ESP32_Cloud/
    SETUP.md
    ESP32_Cloud/
      ESP32_Cloud.ino
      arduino_secrets.h
      thingProperties.h

  GoogleSheets-Backend/
    SETUP.md
    Code.gs

  USER_MANUAL.md
  USER_MANUAL.html
  USER_MANUAL.pdf
  USER_MANUAL_BROCHURE.pdf
```

---

## Version 1: Proteus 7 simulation

Folder:

```text
ESP32-Proteus7-Simulation/
```

This version is for Proteus 7 simulation/demonstration.

It includes:

- Proteus design/project files
- Arduino sketch for changing display modes
- LCD display support
- Simulated temperature, humidity, and gas inputs
- Button controls for switching modes
- Serial output for sending sensor data
- Supporting bridge/dashboard files

Main sketch:

```text
ESP32-Proteus7-Simulation/ChangeMode/ChangeMode.ino
```

The simulation sketch cycles through:

- Temperature
- Humidity
- Gas level

It uses analog inputs and displays readings on a 16x2 LCD.

---

## Version 2: Actual ESP32 sensors-only build

Folder:

```text
ESP32_Sensors_only/main/
```

This version is for the actual ESP32 hardware build, but it does **not** use an external backend.

It includes:

- ESP32 access point/local network dashboard
- DHT11 temperature and humidity readings
- MQ135 gas sensor readings
- Local `/data` JSON route
- Local `/history` route
- Local `/download` route for saved readings
- Browser-based dashboard served directly from the ESP32

Main sketch:

```text
ESP32_Sensors_only/main/main.ino
```

Important note:

This version is useful when you only need the device itself and a nearby browser. It does not send readings to Google Sheets or a cloud backend.

---

## Version 3: ESP32 cloud/backend build

Folder:

```text
ESP32_Cloud/
```

This version is for the actual ESP32 hardware build with backend/cloud features.

It includes:

- Normal WiFi connection
- Local ESP32 dashboard
- Local history and CSV/download support
- Arduino Cloud variable updates
- Google Apps Script / Google Sheets backend logging
- Session-based reading storage

Main sketch:

```text
ESP32_Cloud/ESP32_Cloud/ESP32_Cloud.ino
```

Setup guide:

```text
ESP32_Cloud/SETUP.md
```

Cloud/backend behavior:

- Arduino Cloud receives temperature, humidity, and gas ADC values.
- Google Apps Script acts as the backend API.
- Google Sheets stores sessions and readings.
- Each ESP32 run can become a separate session.

---

## Google Sheets backend

Folder:

```text
GoogleSheets-Backend/
```

The backend uses:

- Google Sheets as the saved data store
- Google Apps Script as the API endpoint

Files:

```text
GoogleSheets-Backend/Code.gs
GoogleSheets-Backend/SETUP.md
```

Main backend features:

- `GET` health/status response
- `GET ?action=sessions`
- `GET ?action=readings&session_id=...`
- `POST` reading logs with `log_reading`
- Session tracking
- Reading history storage
- Optional session deletion logic

The script automatically creates/uses these sheet tabs:

- `sessions`
- `readings`

See the backend setup guide for deployment steps:

```text
GoogleSheets-Backend/SETUP.md
```

---

## Hardware notes

Typical actual ESP32 hardware version uses:

- ESP32 board
- DHT11 temperature/humidity sensor
- MQ135 gas sensor module
- USB power/data cable
- WiFi network for the cloud version

Recommended wiring from the cloud setup notes:

| Component | Connection |
| --- | --- |
| DHT11 VCC | 3.3V |
| DHT11 GND | GND |
| DHT11 DATA | GPIO 4 |
| MQ135 VCC | 5V preferred, or 3.3V for safer direct ADC wiring |
| MQ135 GND | GND |
| MQ135 AO | GPIO 32 |

Important safety note:

ESP32 GPIO pins are 3.3V only. If the MQ135 module is powered from 5V, use a voltage divider before connecting the analog output to ESP32 GPIO 32.

---

## Local web routes

The ESP32 dashboard versions expose routes such as:

| Route | Purpose |
| --- | --- |
| `/` | Main dashboard |
| `/data` | Latest sensor values as JSON |
| `/history` | Saved local history readings |
| `/download` | Download/export readings |

For the sensors-only build, open the ESP32 network/IP shown by the sketch.

For the cloud build, connect your phone/laptop to the same WiFi as the ESP32, then open the local IP address printed in Serial Monitor.

---

## Setup overview

### Proteus 7 simulation

1. Open the Proteus project file inside `ESP32-Proteus7-Simulation/`.
2. Use the sketch in `ESP32-Proteus7-Simulation/ChangeMode/`.
3. Run the simulation.
4. Use the buttons to switch between temperature, humidity, and gas modes.

### Actual ESP32 without backend

1. Open `ESP32_Sensors_only/main/main.ino` in Arduino IDE.
2. Install the needed ESP32 board support and libraries.
3. Upload to the ESP32.
4. Open Serial Monitor.
5. Open the local dashboard address shown by the device.

### ESP32 with cloud/backend

1. Follow `GoogleSheets-Backend/SETUP.md` to deploy the Apps Script backend.
2. Follow `ESP32_Cloud/SETUP.md` to configure Arduino Cloud and WiFi/backend secrets.
3. Upload `ESP32_Cloud/ESP32_Cloud/ESP32_Cloud.ino` to the ESP32.
4. Open Serial Monitor at 115200 baud.
5. Use the local dashboard and verify backend/cloud updates.

---

## Documentation files

The repository also includes generated user manual files:

- `USER_MANUAL.md`
- `USER_MANUAL.html`
- `USER_MANUAL.pdf`
- `USER_MANUAL_BROCHURE.pdf`

These are intended for final project documentation and presentation use.

---

## Notes

- The project is for academic/prototype use.
- Gas values are raw ADC readings unless separately calibrated.
- MQ135 calibration is required if accurate PPM-style gas concentration values are needed.
- Google Apps Script and Google Sheets are suitable for school/demo scale logging, not high-throughput production telemetry.

---

## Summary

This repository contains a complete gas monitoring project with:

- A Proteus 7 simulation version
- A real ESP32 local-dashboard version without backend
- A real ESP32 cloud/backend version
- Google Sheets backend logging
- User manual and documentation outputs
