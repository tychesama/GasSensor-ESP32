# User Manual
## ESP32 Temperature, Humidity, and Gas Monitoring System

Course: Architecture and Organization, 2nd Semester 2025  
Prepared for: Christopher Aris Alviola  
Project Owner / Support: Jose Emmanuel R. Idpan

---

## 1. Introduction

This project is an ESP32-based monitoring system that measures temperature, humidity, and gas levels using connected sensors. The readings can be viewed through a local web dashboard and can also be saved for later review.

### Purpose
- Monitor environmental conditions in real time
- View readings on a local website using the ESP32 device IP address
- Save session data for later checking
- Provide a simple academic prototype for monitoring and logging

### Benefits
- Real-time monitoring of temperature, humidity, and gas
- Browser-based access on devices connected to the same WiFi
- Saved readings for later review
- Simple setup for school project demonstration

---

## 2. Getting Started

Before using the device, prepare the following:

- A laptop or computer
- A proper power source for the ESP32, such as a laptop USB port or safe USB adapter
- A WiFi network
- A Google account if you want to save and review data using Google Sheets

### Important notes
- The ESP32 and the viewing device must be connected to the same WiFi network
- The device should be used in a dry, ventilated area
- This system is for educational and prototype use

---

## 3. Device Setup and Access

This manual assumes the hardware is already assembled, soldered, and placed inside its enclosure.

### Step-by-step setup
1. Connect the ESP32 device to a laptop using USB.
2. Power the device using a safe USB power source.
3. Make sure the ESP32 is using the configured WiFi network.
4. Connect the laptop or phone you will use for viewing to the same WiFi network.
5. Open the Serial Monitor on the laptop.
6. After the ESP32 connects to WiFi, it should show the local IP address.
7. Open that IP address in a browser to access the monitoring website.

### Optional Google Sheets setup
If you want saved session data:

1. Use a Google account.
2. Open the backend setup in `D:\repos\GasSensor-ESP32\GoogleSheets-Backend`
3. Deploy the Apps Script backend as a Web App.
4. Put the generated Web App URL in the ESP32 project settings.

Repository reference: https://github.com/tychesama/GasSensor-ESP32

---

## 4. Using the Website

Once the website is open, the user can view the available monitoring features.

### Main functions
- View live temperature, humidity, and gas readings
- Review saved session data
- Change the display between horizontal and vertical layouts
- Check session-based history from the website
- Download or review saved data if available in the current implementation

### Usage flow
1. Power the device.
2. Wait for WiFi connection.
3. Open the IP address shown in Serial Monitor.
4. Use the website to monitor current readings.
5. Open saved data or history sections when needed.

---

## 5. Troubleshooting

### Common issues
- No power: check the USB cable and power source
- No IP address shown: check WiFi settings and restart the device
- Website not opening: make sure both devices are on the same WiFi network
- No sensor readings: check wiring and sensor connections
- Saved data not working: recheck the Google Sheets backend setup

---

## 6. Maintenance

To keep the device working properly:

- Keep it away from dust and water
- Keep ventilation areas clear
- Do not expose it to rough handling
- Keep it away from children
- Check wires and sensor connections if readings become unstable

---

## 7. Warranty and Support

This project is an academic prototype and has no formal commercial warranty.

For support or questions, contact:
- Website: joemidpan.com
- Email: jridpan1225@gmail.com

---

## Summary

This monitoring system allows the user to:
- monitor temperature, humidity, and gas values
- access the system through a browser on the same WiFi
- review saved data when backend logging is enabled
- use a simple and practical ESP32-based monitoring setup for academic purposes
