/** @file bosch_temp.cpp
    @brief Bosch Engine Coolant Temperature Sensor (ECT Sensor) test
*/

#include "Arduino.h"
#include "math.h"

#include "spud.h"

#define LOOP_TIME_MS 1000

#define TEMP_PIN AI3
float val = 0.;

unsigned long t = 0;
unsigned long tNMinus1 = 0;

// Circuit constants
// 5V ───────── [ NTC Thermistor ]
//                     │
//              [ Resistor 1: 680 Ω ]
//                     │
//                     ├─── Analog Input to 3.3V MCU
//                     │
//              [ Resistor 2: 1.2 kΩ ]
//                     │
//                    GND
//
const float V_5V = 5.0;       // Voltage driving the top of the NTC loop
const float V_MCU = 3.3;      // ESP32 ADC full-scale reference voltage
const float R1_VALUE = 680.0;  // Resistor 1 (in Ohms)
const float R2_VALUE = 1200.0; // Resistor 2 (in Ohms)
const int ADC_MAX = 4095;     // ESP32 uses a 12-bit ADC (0 - 4095)

// --- Thermistor Calibration (Derived for 0°C to 60°C range) ---
const float T_0 = 273.15;      // Reference temperature in Kelvin (0 °C)
const float R_0 = 5896.0;      // NTC Resistance at T_0 (0 °C from Bosch table)
const float BETA = 3476.0; // Calculated Beta coefficient for the 0-60°C span

float read_coolant_temperature() {
    // 1. Take an average of multiple readings to eliminate ESP32 ADC noise
    long adcAccumulator = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) {
        adcAccumulator += analogRead(TEMP_PIN);
        delayMicroseconds(50);
    }
    float rawAdc = (float)adcAccumulator / samples;

    // 2. Convert raw ADC steps into actual measured voltage
    float vOut = (rawAdc / (float)ADC_MAX) * V_MCU;

    // Avoid division-by-zero if the pin floats completely to 0V
    if (vOut <= 0.05) return -999.0;

    // 3. Reverse-engineer the NTC resistance based on your exact topology:
    // Formula derived from: Vout = 5V * R2 / (R_ntc + R1 + R2)
    float rNtc = ((V_5V * R2_VALUE) / vOut) - R1_VALUE - R2_VALUE;

    // Sanity check to avoid log errors if math goes out of bounds
    if (rNtc <= 0) return -999.0;

    // 4. Apply Beta parameter equation to find temperature in Kelvin
    float steinhart;
    steinhart = log(rNtc / R_0);         // ln(R/R0)
    steinhart /= BETA;                  // 1/B * ln(R/R0)
    steinhart += (1.0 / T_0);            // + (1/T0)
    steinhart = 1.0 / steinhart;        // Invert to get Kelvin

    // 5. Convert Kelvin to Celsius
    float temperatureC = steinhart - 273.15;

    return temperatureC;
}


/** The setup function.


*/
void setup() {

     Serial.printf("Bosch temp probe example\n");

     Serial.begin(115200);
     Serial.println("Serial output setup.");

     pinMode(TEMP_PIN, INPUT);

}

/** Main loop function.

*/
void loop() {

     t = millis();
     if (t - tNMinus1 > LOOP_TIME_MS) {
       tNMinus1 = t;
       val = read_coolant_temperature();
       Serial.printf("%lu %.2f\n", t, val);
     }
}
