/*! \file main.cpp
  \brief Test routine for sending OBD-II data using SPUD
  
*/

#include <Arduino.h>

#include "mcp_can.h"

#define SPUD_VERSION 1
#include "spud.h"

#define LOOP_TIME_US 1000 // Try this and 10ms.

#define LOOPS_PER_CAN_SLOW_UPDATE 10
#define LOOPS_PER_CAN_FAST_UPDATE 1
#define LOOPS_PER_OTHER_ACTION_TBC 100
#define LOOPS_PER_SERIAL_UPDATE 100

/** OBD2 frames:

    Standard request ID is 0x7DF, and we use 0x7E8 for the response. 

    Byte 0: Number of useful data bytes in the message (1-4)

    Byte 1: Mode (0x01-0x0A for requests, 0x40-0x4A for responses. So
    this gives 10 modes (0x01 and 0x41 are for real time data).

    Byte 2: PID

    Byte 3-6: Data

    Byte 7: Unused

*/
#define CAN_ID_OBD2_REQ 0x7DF
#define CAN_ID_OBD2_RES 0x7E8

#define PIDHV_VOLTAGE

MCP_CAN can0(CAN0_CS);
MCP_CAN can1(CAN1_CS);

unsigned long t = 0;
unsigned long tNMinus1 = 0;
uint16_t nLoops = 0;

bool sendSupportedPIDsFlag = 0;
uint8_t supportedPIDRange = 0;
bool sendStatusFlag = 0;
bool sendHV1Flag = 0;
bool sendSpeedOdoFlag = 0;
bool sendBattFlag = 0;

/// Can ID.
long unsigned int canID = 0;

/// Written when data is read from one of receive registers.
unsigned char canBufLen = 0;

/// Incoming messages are stored here.
unsigned char canBuf[8];

typedef enum {REVERSE, NEUTRAL, FORWARD, FORWARD2} GEARS;

int gear = 0;
int brake = 0;
int state = 0;

typedef void (*OBDEncodingFunc)(uint8_t *buf);

/** Encode status */
void EncodeStatus(uint8_t *buf) {
     buf[0] = 6;
     buf[1] = 0x41;
     buf[2] = 0x02;
     buf[3] = (state << 6)
          | (brake << 4)
          | ((gear == FORWARD2) << 3)
          | ((gear == FORWARD) << 2)
          | ((gear == NEUTRAL) << 1)
          | ((gear == REVERSE));     
     buf[4] = 0b11101000; // Relays: RSw, MC, Neg, Pre, Pos, EVSE_Conn, EVSE_On, Charging    
     buf[5] = 0x00;     
     buf[6] = 0x00;     
}

typedef struct OBDData_t {
     uint service;
     uint pid;
     OBDEncodingFunc func;
} OBDData_t;

const uint8_t nSuppportedPIDs = 1;

OBDData_t obdData[nSuppportedPIDs] = {
     {1, 2, EncodeStatus}
};

/*

  590 soh
  609 hvess current
  618 hvess voltage
*/

//! Mask used to tell OBD-II which PIDs are supported.
unsigned int pidMask = 0;

uint8_t can0MesgOut[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t can1MesgOut[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t obd2ReqMesg[8] = {0x02, 0x01, 0x0D, 0x55, 0x55, 0x55, 0x55, 0x55};
uint8_t obd2ResMesg[8] = {0x03, 0x41, 0x0D, 0x00, 0xAA, 0xAA, 0xAA, 0xAA};

/**
   Initialise the CAN controller.

   Sometimes the controller gets confused when it starts up while
   attached to a busy network. Therefore we run begin at least twice
   (side effect of a controller reset).

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
  
     SPI.begin(GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_15); // ok for dcc on both spuds).
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
          print_can_mesg(0, canID, canBufLen, canBuf);
          canID = 0;
     }
     if (CAN_MSGAVAIL == can1.checkReceive()) {         // check if data coming
          can1.readMsgBuf(&canID, &canBufLen, canBuf);
          if (canID == CAN_ID_OBD2_REQ) {
               // Check request and respond.
               uint8_t dataLen = canBuf[0];
               if (canBuf[1] == 0x01) { // Then this is a real time request
                    if ( (canBuf[2] & 0x0F) == 0x00) { // Supported PIDs
                         sendSupportedPIDsFlag = 1;
                         supportedPIDRange = canBuf[2] >> 4;
                    }
                    else if (canBuf[2] == 0x02) {
                         sendStatusFlag = 1;
                    }
               }
          }
          print_can_mesg(1, canID, canBufLen, canBuf);
          canID = 0;
     }
}

void setup() {
     Serial.begin(115200);
     Serial.println("Startup .............................................................");
     Serial.println("Rippletech SPUD CAN OBD2 example v1.0 Hardware v 1.x");

     Serial.println(ARDUINO_BOARD);
     Serial.print(F("CPU Frequency = ")); Serial.print(F_CPU / 1000000); Serial.println(F(" MHz"));
  
     tNMinus1 = millis();  // initial start time
 
     can_init(); 
}

void loop() {

     handle_incoming_can();

     gear = FORWARD;
     brake = 1;
     state = 1;
          
     t = micros();
     if (t - tNMinus1 > LOOP_TIME_US) {
          tNMinus1 = t;
          nLoops++;
    
          // Send CAN messages.
          if (nLoops % LOOPS_PER_CAN_SLOW_UPDATE == 0) {
          }
          if (nLoops % LOOPS_PER_CAN_FAST_UPDATE == 0) {
               if (sendSupportedPIDsFlag) {
                    obd2ResMesg[0] = 0x06;
                    obd2ResMesg[1] = 0x41;
                    obd2ResMesg[2] = supportedPIDRange << 4;
                    pidMask = 0;
                    for (int i = 0; i < nSuppportedPIDs; i++) {
                         if ( (obdData[i].pid > supportedPIDRange/2*32)
                              && (obdData[i].pid < (supportedPIDRange/2+1)*32) ) {
                              pidMask |= (0x01 << (32 - (obdData[i].pid - supportedPIDRange/2*32)));
                              //Serial.println(pidMask);
                         }
                    }
                    if  (obdData[nSuppportedPIDs-1].pid > (supportedPIDRange/2+1)*32) {
                         pidMask |= 0x01;
                         //Serial.println(pidMask);
                    }
                    obd2ResMesg[3] = pidMask >> 24;
                    obd2ResMesg[4] = pidMask >> 16;
                    obd2ResMesg[5] = pidMask >> 8;
                    obd2ResMesg[6] = pidMask;
                    obd2ResMesg[7] = 0x00;
                    if (int res = send_to_can_bus(can1, CAN_ID_OBD2_RES, obd2ResMesg, 8)) {
                         Serial.print("Error sending OBD-II response message! ");
                         Serial.println(res);
                    }
                    sendSupportedPIDsFlag = 0;
               }
               if (sendStatusFlag) {
                    EncodeStatus(obd2ResMesg);
                    if (int res = send_to_can_bus(can1, CAN_ID_OBD2_RES, obd2ResMesg, 8)) {
                         Serial.print("Error sending OBD-II response message! ");
                         Serial.println(res);
                    }
                    sendStatusFlag = 0;
               }
          }
     }
}

