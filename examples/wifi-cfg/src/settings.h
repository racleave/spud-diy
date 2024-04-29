
#ifndef SETTINGS_H
#define SETTINGS_H

// Need this for strings - might be a smaller lib to include.
#include <Arduino.h>

typedef String (*SettingsFunc)(String x);

String PassThrough(String x) {
  return x;
}

// These should really be defined in library.

enum VarType {TEXT = 0, NUMBER = 1, FUNCTION = 2};

typedef struct WiFiCfgParam_t {
  String name;
  String text;
  String value;
  VarType varType;
  String units;
  uint8_t nBytes;
  SettingsFunc func;
};

const char settingsName[] = "spud_settings";
  
const int nWiFiCfgParams = 4;

WiFiCfgParam_t wifiCfgParams[nWiFiCfgParams] = {
  {"ssid", "SSID", "SPUD", TEXT, "", 8, PassThrough}, // Max of eight characters
  {"password", "Password", "spud1234", TEXT, "", 8, PassThrough}, // Must be eight characters
  {"onDuration", "Duration LED is on", "100", NUMBER, "ms", 1, PassThrough},
  {"offDuration", "Duration LED is off", "900", NUMBER, "ms", 1, PassThrough}
};

#endif
