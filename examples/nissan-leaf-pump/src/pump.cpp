/** @file pump.cpp
    @brief Test nissan leaf pump
*/

#include "Arduino.h"

#include "spud.h"

#define LOOP_TIME_MS 10

#define WATERPUMP_CONTROL_PIN DO1

#define WATERPUMP_PWM_PERIOD_MS 500

unsigned long t = 0;
unsigned long tNMinus1 = 0;

//! Time in milliseconds of start of water pump PWM period.
unsigned long tWaterPumpZero = 0;
int waterPumpCommand = 10;

/** The code to run a Nissan Leaf water pump at a reasonable speed.
  
    Can't seem to make PWM frequency low enough, so doing this via a
    timer.

    @todo This is nissan leaf specific, so it should maybe go into
    leaf.cpp.

    @todo No dead band here.

    @return 0 is OK.

*/
int setWaterPumpSpeed(void) {
     
     int waterPumpCommandOut = waterPumpCommand*WATERPUMP_PWM_PERIOD_MS;
     waterPumpCommandOut /= 100;
     if (t - tWaterPumpZero < WATERPUMP_PWM_PERIOD_MS) {
          if ( (t - tWaterPumpZero > waterPumpCommandOut)
               && (digitalRead(WATERPUMP_CONTROL_PIN)) )
               digitalWrite(WATERPUMP_CONTROL_PIN, LOW);
     }
     else {
          tWaterPumpZero = t;
          digitalWrite(WATERPUMP_CONTROL_PIN, HIGH);
     }
     return 0;
}

/** Read single chars from the keyboard and change mode (testing
    only).

*/
void readSerial()
{
     if (Serial.available() > 0) {
          uint8_t readByte = Serial.read();
          if (readByte == 'p') {
               if (waterPumpCommand > 10)
                    waterPumpCommand--;
          }
          else if (readByte == 'P') {
               if (waterPumpCommand < 90)
                    waterPumpCommand++;
          }
          Serial.println(waterPumpCommand);
     }
}

/** The setup function.


*/
void setup() {

     Serial.printf("Nissan Leaf pump example\n");
     
     Serial.begin(115200);
     Serial.println("Serial output setup.");

     pinMode(WATERPUMP_CONTROL_PIN, OUTPUT);
     digitalWrite(WATERPUMP_CONTROL_PIN, LOW);


}

/** Main loop function.

*/
void loop() {

     t = millis();
     if (t - tNMinus1 > LOOP_TIME_MS) {
          readSerial();
          setWaterPumpSpeed();
     }
}
 
