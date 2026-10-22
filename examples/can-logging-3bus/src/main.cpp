/*! \file main.cpp
  \brief Test routine for SPUD, logging three CAN buses.

  Bus 0 and bus 1 are the on-board SPI CAN controllers. Bus 2 (CAN3) is
  the ESP32's built-in TWAI controller on the analog input pins:
    AI0 (GPIO33) = TWAI TX
    AI5 (GPIO36) = TWAI RX

  $ pio device monitor >& can-logging-test.log
  $ python rev2gvret.py -n 15 -t 1 can-logging-test.log can-logging-test.csv

  Then use SavvyCAN to inspect.

*/

#include <Arduino.h>
#include "driver/twai.h"

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
#define SEND_CAN1 // Also send a test frame on bus 1.
#define SEND_CAN_TWAI // Also send a test frame on bus 2 (CAN3).

/// TWAI (ESP32 built-in CAN) pins, used as bus 2 (CAN3).
#define TWAI_TX AI0 // GPIO33
#define TWAI_RX AI5 // GPIO36, input only, so RX only.
#define TWAI_BUS 2
#define CAN_ID_TWAI_TEST 0x700

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

/**
   Initialise the SPI CAN controllers (bus 0 and bus 1).

   Both are 500k, the same as bus 2, so all three can share one bus.
*/
static void can_init() {
  delay(500);

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

  while (CAN_OK != can1.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)) {
    Serial.println("CAN bus 1 init failed");
    Serial.println(" Trying again...");
    delay(100);
  }
  can1.setMode(MCP_NORMAL);
  Serial.println("CAN bus 1 init OK.");
#elif CAN_CHIP == MCP251863
  ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_40MHz, ARB_BIT_RATE,
                              DATA_RATE_MULTIPLIER);
  settings.mRequestedMode = CAN_MODE;
  settings.mArbitrationSJW = 1;
  settings.mDataSJW = 1;

  while (can0.begin(settings, [] { can0.isr () ; }) != 0) {
    Serial.println("CAN bus 0 init failed");
    Serial.println(" Trying again...");
    delay(500);
  }
  Serial.println("CAN bus 0 init OK.");

  while (can1.begin(settings, [] { can1.isr () ; }) != 0) {
    Serial.println("CAN bus 1 init failed");
    Serial.println(" Trying again...");
    delay(500);
  }
  Serial.println("CAN bus 1 init OK.");
#endif
}

/**
   Initialise the ESP32 TWAI controller as bus 2 (CAN3).

   Baud rate is hardcoded to 500k, accepting all IDs.
*/
static void twai_init() {
  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX, TWAI_RX, TWAI_MODE_NORMAL);
  gConfig.rx_queue_len = 32;
  twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  while (twai_driver_install(&gConfig, &tConfig, &fConfig) != ESP_OK) {
    Serial.println("CAN bus 2 (TWAI) driver install failed");
    Serial.println(" Trying again...");
    delay(100);
  }
  while (twai_start() != ESP_OK) {
    Serial.println("CAN bus 2 (TWAI) start failed");
    Serial.println(" Trying again...");
    delay(100);
  }
  Serial.println("CAN bus 2 (TWAI) init OK.");
}

/**
   Send a byte array to can bus.

   Returns 0 on success.
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

  for (uint8_t i = 0; i < frame.len; i++) {
    frame.data[i] = mesg[i];
  }
  return can.tryToSend(frame) ? 0 : 2;
}
#endif

/**
   Send a byte array on the TWAI bus. Returns 0 on success, like
   send_to_can_bus().
*/
int send_to_twai(int canId, uint8_t *mesg, int len) {
  twai_message_t frame = {};
  frame.identifier = canId;
  frame.data_length_code = len;
  for (uint8_t i = 0; i < len; i++) {
    frame.data[i] = mesg[i];
  }
  return twai_transmit(&frame, 0) == ESP_OK ? 0 : 2;
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
#if CAN_CHIP == MCP2515
  if (CAN_MSGAVAIL == can0.checkReceive()) {         // check if data coming
    can0.readMsgBuf(&canID, &canBufLen, canBuf);
    print_can_mesg(0, canID, canBufLen, canBuf);
    canID = 0;
  }
  if (CAN_MSGAVAIL == can1.checkReceive()) {         // check if data coming
    can1.readMsgBuf(&canID, &canBufLen, canBuf);
    print_can_mesg(1, canID, canBufLen, canBuf);
    canID = 0;
  }
#elif CAN_CHIP == MCP251863
  CANFDMessage frame;
  while (can0.receive(frame)) {
    print_can_mesg(0, frame.id, frame.len, frame.data);
  }
  while (can1.receive(frame)) {
    print_can_mesg(1, frame.id, frame.len, frame.data);
  }
#endif

  twai_message_t twaiFrame;
  while (twai_receive(&twaiFrame, 0) == ESP_OK) {
    if (!twaiFrame.rtr) {
      print_can_mesg(TWAI_BUS, twaiFrame.identifier, twaiFrame.data_length_code, twaiFrame.data);
    }
  }
}

/**
   Recover the TWAI controller if it has gone bus-off (e.g. cable
   unplugged or wrong baud rate), otherwise it stays off until reset.
*/
void check_twai_status() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;
  if (status.state == TWAI_STATE_BUS_OFF) {
    Serial.println("CAN bus 2 (TWAI) bus-off, recovering...");
    twai_initiate_recovery();
  } else if (status.state == TWAI_STATE_STOPPED) {
    twai_start();
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
  twai_init();
}

void loop() {
  handle_incoming_can();

  t = micros();
  if (t - tNMinus1 > LOOP_TIME_US) {

    tNMinus1 = t;
    nLoops++;

    if (nLoops % LOOPS_PER_OTHER_ACTION_TBC == 0) {
      check_twai_status();
    }

    // Ramp the value sent in byte 3.
    if (pwmDutyADesired >= maxPWMDutyA) pwmDutyADesired = 0;
    else pwmDutyADesired += 5;

    // Send CAN messages.
#ifdef SEND_CAN
    if (nLoops % LOOPS_PER_CAN_FAST_UPDATE == 0) {
      can0MesgOut[3] = pwmDutyADesired;
      if (can0MesgOut[6] >= 3) {
        can0MesgOut[6] = 0;
      } else {
        can0MesgOut[6]++;
      }
      if (int res = send_to_can_bus(can0, CAN_ID_DUTY_A[idx], can0MesgOut, 8)) {
        Serial.print("Error sending CAN0 message! "); Serial.println(res);
      } else {
        Serial.print("Sent CAN0 message, ID: 0x"); Serial.println(CAN_ID_DUTY_A[idx], HEX);
      }
      delay(5); // Helps a lot!
#ifdef SEND_CAN1
      can1MesgOut[1] = 280;
      can1MesgOut[0] = 280 >> 8;
      can1MesgOut[3] = -280;
      can1MesgOut[2] = -280 >> 8;
      can1MesgOut[5] = 223;
      can1MesgOut[4] = 223 >> 8;
      can1MesgOut[7] = uint8_t(t/10000 % 10000);
      can1MesgOut[6] = uint8_t(t/10000 % 10000 >> 8);
      if (int res = send_to_can_bus(can1, CAN_ID_DUTY_B[idx], can1MesgOut, 8)) {
        Serial.print("Error sending CAN1 message! 0x"); Serial.println(res);
      } else {
        Serial.print("Sent CAN1 message, ID: 0x"); Serial.println(CAN_ID_DUTY_B[idx], HEX);
      }
      delay(5);
#endif
#ifdef SEND_CAN_TWAI
      if (int res = send_to_twai(CAN_ID_TWAI_TEST, can0MesgOut, 8)) {
        Serial.print("Error sending CAN2 (TWAI) message! "); Serial.println(res);
      } else {
        Serial.print("Sent CAN2 (TWAI) message, ID: 0x"); Serial.println(CAN_ID_TWAI_TEST, HEX);
      }
#endif
    }
#endif
  }
}
