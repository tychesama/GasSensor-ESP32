// Code generated/adapted for local Arduino IDE upload.
// Based on Arduino IoT Cloud generated Thing properties.

#pragma once

#include "arduino_secrets.h"
#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>

const char DEVICE_LOGIN_NAME[] = "76b00853-305d-47a0-bd99-9a5267a1dd3e";
const char THING_ID[]          = "a7c424eb-8d7c-4def-a104-4e204a953e32";

const char SSID[] = SECRET_SSID;
const char PASS[] = SECRET_OPTIONAL_PASS;
const char DEVICE_KEY[] = SECRET_DEVICE_KEY;

float cloudGas;
float cloudHumidity;
float cloudTemperature;

void initProperties() {
  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setThingId(THING_ID);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);

  ArduinoCloud.addProperty(cloudGas, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudHumidity, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudTemperature, READ, ON_CHANGE, NULL);
}

WiFiConnectionHandler ArduinoIoTPreferredConnection(SSID, PASS);
