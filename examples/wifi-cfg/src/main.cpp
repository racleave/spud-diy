
#include "Arduino.h"
#include "SPI.h"
#include "settings.h"
#include "wifi_cfg.hpp"

#define LED GPIO_NUM_4 // DIO0

unsigned long timer; 
unsigned long timeStateChange = 0;
bool ledState = false;

/** An example of a check function. Here a value (the torque) is
 * constrained to be within two limits.
 */
String CheckTorque(String x) {
  int xInt = x.toInt();
  if (xInt < 0) xInt = 0;
  else if (xInt > 280) xInt = 280;
  return String(xInt);
}

void setup() {
  Serial.begin(921600);
  settings_server(0);
  timer = millis();
  ReadEepromValues();
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  wifiCfgParams[4].func = CheckTorque;
  
}

void loop() {
  if (millis() >= timer) {
    ReadEepromValues();
    timer =  millis() + 10000;
  }

  if (ledState) {
    if (millis() - timeStateChange > getIntVal("onDuration")) {
      ledState = LOW;
      digitalWrite(LED, ledState);
      Serial.println("Off");
      timeStateChange = millis();
    }
  }
  else {
    if (millis() - timeStateChange > getIntVal("offDuration")) {
      ledState = HIGH;
      digitalWrite(LED, ledState);
      timeStateChange = millis();
      Serial.print("On");
    }
  } 

}
