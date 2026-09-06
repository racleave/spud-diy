/*! \file main.cpp
  \brief Test routine for SPUD

  $ pio device monitor >& can-logging-test.log
  $ python rev2gvret.py -n 15 -t 1 can-logging-test.log can-logging-test.csv

  Then use SavvyCAN to inspect.

*/

#include <Arduino.h>

#define MCP2515 1
#define MCP251863 2
#define CAN_CHIP MCP2515
//#define CAN_CHIP MCP251863

#if CAN_CHIP == MCP2515
#include "mcp_can.h"
#elif CAN_CHIP == MCP251863
#include <ACAN2517FD.h>
#endif

#define SPUD_VERSION 1
#include "spud.h"

#define SEND_CAN

#define LOOP_TIME_US 1000 // Try this and 10ms.

#define LOOPS_PER_CAN_FAST_UPDATE 100 // Was 1
#define LOOPS_PER_OTHER_ACTION_TBC 100

const uint16_t nMessages = 5;
uint16_t idx = 0;
uint16_t CAN_ID_DUTY_A[nMessages] = {0x100, 0x101, 0x102, 0x303, 0x304};
uint16_t CAN_ID_DUTY_B[nMessages] = {0x651, 0x201, 0x202, 0x503, 0x504};

#if CAN_CHIP == MCP2515
MCP_CAN can0(CAN0_CS);
MCP_CAN can1(CAN1_CS);
#elif CAN_CHIP == MCP251863
#define ARB_BIT_RATE 1000000
#define DATA_RATE_MULTIPLIER DataBitRateFactor::x8
#define CAN_MODE ACAN2517FDSettings::NormalFD
//#define CAN_MODE ACAN2517FDSettings::Normal20B
ACAN2517FD can0(CS0, SPI, INT0);
ACAN2517FD can1(CS1, SPI, INT1);
#endif

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
  /*pinMode(CAN0_INT, INPUT_PULLUP); // Done by library
    pinMode(CAN1_INT, INPUT_PULLUP);
    pinMode(CAN0_CS, OUTPUT); // Also done in library, but doing it early may actually help with reset...
    pinMode(CAN1_CS, OUTPUT);*/

  SPI.begin(SCLK, SDI, SDO, CS0);
  delay(500);

#if CAN_CHIP == MCP2515
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

  while (CAN_OK != can1.begin(MCP_ANY, CAN_1000KBPS, MCP_8MHZ)) {
    Serial.println("CAN bus 1 init failed");
    Serial.println(" Trying again...");
    delay(100);
  }
  can1.setMode(MCP_NORMAL);
  Serial.println("CAN bus 1 init OK.");
  /*attachInterrupt(digitalPinToInterrupt(interruptPin),
    can_isr,
    FALLING);*/
#elif CAN_CHIP == MCP251863
  ACAN2517FDSettings settings0(ACAN2517FDSettings::OSC_40MHz, ARB_BIT_RATE,
                               DATA_RATE_MULTIPLIER);
  settings0.mRequestedMode = CAN_MODE;
  settings0.mArbitrationSJW = 1;
  settings0.mDataSJW = 1;
  uint32_t errorCode = 0;
  while ((errorCode = can0.begin(settings0, [] { can0.isr () ; })) != 0) {
    Serial.println("CAN bus 0 init failed");
    Serial.println(" Trying again...");
    delay(500);
  }
  Serial.println ("-------------CAN0-------------") ;
  Serial.print ("Bit Rate prescaler: ") ;
  Serial.println (settings0.mBitRatePrescaler) ;
  Serial.print ("Arbitration Phase segment 1: ") ;
  Serial.println (settings0.mArbitrationPhaseSegment1) ;
  Serial.print ("Arbitration Phase segment 2: ") ;
  Serial.println (settings0.mArbitrationPhaseSegment2) ;
  Serial.print ("Arbitration SJW:") ;
  Serial.println (settings0.mArbitrationSJW) ;
  Serial.print ("Actual Arbitration Bit Rate: ") ;
  Serial.print (settings0.actualArbitrationBitRate ()) ;
  Serial.println (" bit/s") ;
  Serial.print ("Exact Arbitration Bit Rate ? ") ;
  Serial.println (settings0.exactArbitrationBitRate () ? "yes" : "no") ;
  Serial.print ("Arbitration Sample point: ") ;
  Serial.print (settings0.arbitrationSamplePointFromBitStart ()) ;
  Serial.println ("%") ;
  Serial.print ("Data Phase segment 1: ") ;
  Serial.println (settings0.mDataPhaseSegment1) ;
  Serial.print ("Data Phase segment 2: ") ;
  Serial.println (settings0.mDataPhaseSegment2) ;
  Serial.print ("Data SJW:") ;
  Serial.println (settings0.mDataSJW) ;
  Serial.print ("TDCO:") ;
  Serial.println (settings0.mTDCO) ;
  Serial.print ("Even, error code 0 0x") ;
  Serial.println(errorCode, HEX);

  ACAN2517FDSettings settings1(ACAN2517FDSettings::OSC_40MHz, ARB_BIT_RATE,
                               DATA_RATE_MULTIPLIER);
  settings1.mRequestedMode = CAN_MODE;
  settings1.mArbitrationSJW = 1;
  settings1.mDataSJW = 1;
  uint32_t errorCode1 = 0;
  while ((errorCode1 = can1.begin(settings0, [] { can1.isr () ; })) != 0) {
    Serial.println("CAN bus 1 init failed");
    Serial.println(" Trying again...");
    delay(500);
  }
  Serial.println ("-------------CAN1-------------") ;
  Serial.print ("Bit Rate prescaler: ") ;
  Serial.println (settings1.mBitRatePrescaler) ;
  Serial.print ("Arbitration Phase segment 1: ") ;
  Serial.println (settings1.mArbitrationPhaseSegment1) ;
  Serial.print ("Arbitration Phase segment 2: ") ;
  Serial.println (settings1.mArbitrationPhaseSegment2) ;
  Serial.print ("Arbitration SJW:") ;
  Serial.println (settings1.mArbitrationSJW) ;
  Serial.print ("Actual Arbitration Bit Rate: ") ;
  Serial.print (settings1.actualArbitrationBitRate ()) ;
  Serial.println (" bit/s") ;
  Serial.print ("Exact Arbitration Bit Rate ? ") ;
  Serial.println (settings1.exactArbitrationBitRate () ? "yes" : "no") ;
  Serial.print ("Arbitration Sample point: ") ;
  Serial.print (settings1.arbitrationSamplePointFromBitStart ()) ;
  Serial.println ("%") ;
  Serial.print ("Data Phase segment 1: ") ;
  Serial.println (settings1.mDataPhaseSegment1) ;
  Serial.print ("Data Phase segment 2: ") ;
  Serial.println (settings1.mDataPhaseSegment2) ;
  Serial.print ("Data SJW:") ;
  Serial.println (settings1.mDataSJW) ;
  Serial.print ("TDCO:") ;
  Serial.println (settings1.mTDCO) ;
  Serial.print ("Even, error code 1 0x") ;
  Serial.println(errorCode1, HEX);
#endif
}

/**
   Send a byte array to can bus.

   The canId modifies what is sent.

*/
#if CAN_CHIP == MCP2515
int send_to_can_bus(MCP_CAN can, int canId, uint8_t *mesg, int len) {
  return can.sendMsgBuf(canId, 0, len, mesg);
}
#elif CAN_CHIP == MCP251863
int send_to_can_bus(ACAN2517FD& can, int canId, uint8_t *mesg, int len) {
  CANFDMessage frame;
  frame.id = canId;
  frame.len = len;
  frame.ext = false;
  //frame.type = CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH;

  for (uint8_t i = 0; i < frame.len; i++) {
    frame.data[i] = mesg[i];
  }
  bool res = can.tryToSend(frame);
  //Serial.println(res);
  if (res)
    return 0;
  else
    return 2;
}
#endif

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
#if CAN_CHIP == MCP2515
  if (CAN_MSGAVAIL == can0.checkReceive()) {         // check if data coming
    can0.readMsgBuf(&canID, &canBufLen, canBuf);
    /*if (canID == CAN_ID_DUTY_A) {
      pwmDutyA = canBuf[3];
      }
      else if (canID == CAN_ID_DUTY_B) {
      pwmDutyB = canBuf[3];
      }*/
    print_can_mesg(0, canID, canBufLen, canBuf);
    canID = 0;
  }
  if (CAN_MSGAVAIL == can1.checkReceive()) {         // check if data coming
    can1.readMsgBuf(&canID, &canBufLen, canBuf);
    /*if (canID == CAN_ID_DUTY_A) {
      pwmDutyA = canBuf[3];
      }
      else if (canID == CAN_ID_DUTY_B) {
      pwmDutyB = canBuf[3];
      }*/
    print_can_mesg(1, canID, canBufLen, canBuf);
    canID = 0;
  }
#elif CAN_CHIP == MCP251863
  CANFDMessage frame;
  if (can0.available()) {
    while (can0.receive(frame)) {
      //print_can_mesg(0, frame.id, frame.len, frame.data);
    }
  }

  if (can1.available()) {
    while (can1.receive(frame)) {
      //print_can_mesg(0, frame.id, frame.len, frame.data);
    }
  }
#endif
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
      if (idx >= 1/*nMessages*/)
        idx = 0;
      can0MesgOut[3] = pwmDutyADesired;
      if (can0MesgOut[6] >= 3) {
        can0MesgOut[6] = 0;
      } else {
        can0MesgOut[6]++;
      }
      if (int res = send_to_can_bus(can0, CAN_ID_DUTY_A[idx], can0MesgOut, 8)) {
        Serial.print("Error sending CAN0 message! "); Serial.println(res);
      } else {
        Serial.printf("Sent CAN0 message, ID: 0x");Serial.println(CAN_ID_DUTY_A[idx], HEX);
      }
      delay(5); // Helps a lot!
      can1MesgOut[1] = 280;
      can1MesgOut[0] = 280 >> 8;
      // can1MesgOut[1] = uint8_t(t/1000000 % 10000);
      // can1MesgOut[0] = uint8_t(t/1000000 % 10000 >> 8);
      // can1MesgOut[1] = 22;
      // can1MesgOut[3] = pwmDutyBDesired;
      can1MesgOut[3] = -280;
      can1MesgOut[2] = -280 >> 8;
      can1MesgOut[5] = 223;
      can1MesgOut[4] = 223 >> 8;
      can1MesgOut[7] = uint8_t(t/10000 % 10000);
      can1MesgOut[6] = uint8_t(t/10000 % 10000 >> 8);
      // if (can1MesgOut[6] >= 3) {
      //   can1MesgOut[6] = 0;
      // } else {
      //   can1MesgOut[6]++;
      // }
      // can1MesgOut[7] = 0xB5;
      if (int res = send_to_can_bus(can1, CAN_ID_DUTY_B[idx], can1MesgOut, 8)) {
        Serial.print("Error sending CAN1 message! 0x"); Serial.println(res);
      } else {
        Serial.print(" Sent CAN1 message, ID: 0x");Serial.println(CAN_ID_DUTY_B[idx], HEX);
        print_can_mesg(-1, CAN_ID_DUTY_B[idx], 8, can1MesgOut);
      }
      idx++;
    }
#endif
  }
}
