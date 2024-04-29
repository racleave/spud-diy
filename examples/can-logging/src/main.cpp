/*! \file main.cpp
  \brief Test routine for SPUD

$ pio device monitor >& can-logging-test.log
$ python rev2gvret.py -n 15 -t 1 can-logging-test.log can-logging-test.csv

Then use SavvyCAN to inspect.

*/

#include <Arduino.h>

#include "mcp_can.h"

#define SPUD_VERSION 1
#include "spud.h"

#define SEND_CAN

#define LOOP_TIME_US 1000 // Try this and 10ms.

#define LOOPS_PER_CAN_FAST_UPDATE 1
#define LOOPS_PER_OTHER_ACTION_TBC 100
#define LOOPS_PER_SERIAL_UPDATE 100

#define CAN_ID_DUTY_A 0x100
#define CAN_ID_DUTY_B 0x101
#define CAN_ID_INV_STATUS 0x1DA //!< Sent by Inverter 10ms

MCP_CAN can0(CAN0_CS);
MCP_CAN can1(CAN1_CS);

unsigned long t = 0;
unsigned long tNMinus1 = 0;
uint16_t nLoops = 0;

/// Can ID.
long unsigned int canID = 0;

/// Written when data is read from one of receive registers.
unsigned char canBufLen = 0;

/// Incoming messages are stored here.
unsigned char canBuf[8];

uint8_t can0MesgOut[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t can1MesgOut[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const int maxPWMDutyA = 256; // Should be 2**pwmRes.
unsigned int pwmDutyADesired = 0; // This gets sent out on CAN bus.
unsigned int pwmDutyA = 0; // This gets written by handle_incoming_can.

const int maxPWMDutyB = 256; // Should be 2**pwmRes.
unsigned int pwmDutyBDesired = 0; // This gets sent out on CAN bus.
unsigned int pwmDutyB = 0; // This gets written by handle_incoming_can.

/**
   Initialise the CAN controller.

   Baud rate is hardcoded to 500k.
*/
static void can_init() {
  // Looks like this helps...
  //can.begin(CAN_500KBPS);
  delay(500);
	    
  /** Start interrupt to let us know when CAN messages have arrived
      (pre filtered). Must be set on FALLING edge, as that is when MCP2515
      has added a filtered message.*/
  pinMode(CAN0_INT, INPUT);
  pinMode(CAN1_INT, INPUT);
  
  SPI.begin(SCLK, SDI, SDO, CS0);
  delay(500);

  while (CAN_OK != can0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)) {
    Serial.println("CAN bus 0 init failed");
    Serial.println(" Trying again...");
    delay(100);
  }
  can0.setMode(MCP_NORMAL);
  Serial.println("CAN bus 0 init OK.");
  /*attachInterrupt(digitalPinToInterrupt(interruptPin),
    can_isr,
    FALLING);*/
  
  while (CAN_OK != can1.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)) {
    Serial.println("CAN bus 1 init failed");
    Serial.println(" Trying again...");
    delay(100);
  }
  can1.setMode(MCP_NORMAL);
  Serial.println("CAN bus 1 init OK.");
  /*attachInterrupt(digitalPinToInterrupt(interruptPin),
    can_isr,
    FALLING);*/
  
}

/**
   Send a byte array to can bus.

   The canId modifies what is sent.

*/
int send_to_can_bus(MCP_CAN can, int canId, uint8_t *mesg, int len) {
  return can.sendMsgBuf(canId, 0, len, mesg);
}


/** This is close to a GVRET file (SavvyCAN's native format). To get
 * an actual GVRET file the spaces need to be replaced by commas, and
 * any extra lines at the start and/or half lines at the end need to
 * be removed. There is a python file to help with this.
 *
 *   @param bus Which bus (0,1,2) the message was received on
 *   @param id The CAN id of the message
 *   @param len The data length of the message
 *   @param buf A pointer to the message data
*/
void print_can_mesg(uint8_t bus, long unsigned int id, uint8_t len, uint8_t *buf) {

     long int tNow = millis();
     Serial.printf("%d %0x 0 Rx %d %d",  tNow, id, bus, len);
     for (int i = 0; i < len; i++) {
          Serial.print(" ");
          Serial.printf("%02x", buf[i]);
     }
     Serial.print("\n");
}

void handle_incoming_can() {
  if (CAN_MSGAVAIL == can0.checkReceive()) {         // check if data coming
    can0.readMsgBuf(&canID, &canBufLen, canBuf);
    if (canID == CAN_ID_DUTY_A) {
      pwmDutyA = canBuf[3];
    }
    else if (canID == CAN_ID_DUTY_B) {
      pwmDutyB = canBuf[3];
    }
    print_can_mesg(0, canID, canBufLen, canBuf);
    canID = 0;
  }
  if (CAN_MSGAVAIL == can1.checkReceive()) {         // check if data coming
    can1.readMsgBuf(&canID, &canBufLen, canBuf);
    if (canID == CAN_ID_DUTY_A) {
      pwmDutyA = canBuf[3];
    }
    else if (canID == CAN_ID_DUTY_B) {
      pwmDutyB = canBuf[3];
    }
    print_can_mesg(1, canID, canBufLen, canBuf);
    canID = 0;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Startup .............................................................");
  Serial.println("Rippletech SPUD test software v1.0 Hardware v 1.1");

  Serial.println(ARDUINO_BOARD);
  Serial.print(F("CPU Frequency = ")); Serial.print(F_CPU / 1000000); Serial.println(F(" MHz"));

  tNMinus1 = millis();  // initial start time
 
  can_init(); 
}

void loop() {

  handle_incoming_can();
  
  t = micros();
  if (t - tNMinus1 > LOOP_TIME_US) {
    tNMinus1 = t;
    nLoops++;
    
    // Increase desired PWM duties at different rates.
    if (pwmDutyADesired >= maxPWMDutyA) pwmDutyADesired = 0;
    else pwmDutyADesired += 5;
    //pwmDutyADesired = 128;
    if (pwmDutyBDesired >= maxPWMDutyB) pwmDutyBDesired = 0;
    else pwmDutyBDesired += 10;
    
    // Send CAN messages.
#ifdef SEND_CAN    
    if (nLoops % LOOPS_PER_CAN_FAST_UPDATE == 0) {
      can0MesgOut[3] = pwmDutyADesired;
      if (int res = send_to_can_bus(can0, CAN_ID_DUTY_A, can0MesgOut, 8)) {
        Serial.print("Error sending CAN0 message! "); Serial.println(res);
      }  
      can1MesgOut[3] = pwmDutyBDesired;
      if (int res = send_to_can_bus(can1, CAN_ID_DUTY_B, can1MesgOut, 8)) {
        Serial.print("Error sending CAN1 message! "); Serial.println(res);
      }
    }
#endif
  }
}
