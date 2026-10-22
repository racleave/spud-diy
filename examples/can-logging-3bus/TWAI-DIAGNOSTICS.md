# CAN3 (TWAI) bring-up diagnostics

Notes from getting bus 2 (CAN3, the ESP32's built-in TWAI) working on the
SPUD board, October 2026.

## Status: resolved

The board has been reworked so that GPIO33 goes to TXD and GPIO36 to
RXD. CAN3 now works in normal mode with bus 0 at 500k: each bus
receives the other's frames, with matching data (test 13).

## Conclusion

**The board was built with A0 and A5 swapped at the transceiver.** A5
(GPIO36) goes to TXD and A0 (GPIO33) goes to RXD. That explains why
IO36 read 3.2 V: it was sitting on TXD, pulled up by R82. It also
explains why RX never seemed to respond: the firmware was reading the
wrong pin. U6 is fine (test 12).

With this wiring the ESP32 can only receive on CAN3, because GPIO36 is
input only and can't drive TXD. The fix is to rework the board so that
GPIO33 goes to TXD and GPIO36 to RXD, then repeat the retests in step 4
below.

The earlier conclusion, now known to be wrong, was that U6's receiver
was holding RX high.

## Hardware

| Signal | ESP32 | Net | ISO1042 (U6) |
|---|---|---|---|
| TWAI TX | GPIO33 (AI0) | A0 | Pin 2 TXD (R82 10k pull-up to 3V3) |
| TWAI RX | GPIO36 (AI5, input only) | A5 | Pin 3 RXD (through R1 1k) |

- U6 ISO1042BQDWVRQ1 is an isolated CAN transceiver.
  - Side 1 is powered by 3V3 (U2 XC6206) and GND.
  - Side 2 is powered by VCC2 (5 V) and GND2, from the isolated supply
    U3 SN6505B → U5 760390014 → U1 TPS76350.
- Termination is R42 120 Ω via jumper P5. ESD protection is U4
  ESD2CAN24.
- R2 1 MΩ (GND–GND2) is a static bleed only and is not fitted. It isn't
  needed for CAN to work.

### Changes made to the analog input circuit

A0 and A5 are shared with the AI0/AI5 analog input circuit. To use them
as TWAI pins, these parts were removed:

- **A0:** R4 (22k), R59 (33k), C4 (33 nF)
- **A5:** R38 (47k), R79 (10k), C15 (33 nF)

R1002, R10026, R10050 and R10051 are not fitted. So A0IN/A5IN are
disconnected from the ESP32 side. The only parts left on A0 and A5 are
the ISO1042, R1, the clamp diodes (D4/D23, D16/D19) and the ESP32.

## Symptom

Test setup: bus 0 (MCP2515, 500k) connected to CAN3 with CANH/CANL only,
300 mm cable, 120 Ω termination at one end.

```
Error sending CAN0 message! 6          <- CAN_GETTXBFTIMEOUT: all TX buffers stuck, never ACKed
Error sending CAN2 (TWAI) message! 2
CAN bus 2 (TWAI) bus-off, recovering...
TWAI state 2 TEC 128 REC 0 bus errors 355 tx failed 20 arb lost 0 rx missed 0
```

- Bus-off with REC 0 means bus 2 has transmit errors but has never
  received a bit. Bus 0 was retrying its frame continuously, yet bus 2
  still saw nothing on RX.
- The self-test (`TWAI_SELF_TEST`, no ACK needed, bus 2 receives its own
  frames) failed the same way. So the fault is on bus 2's side, not
  bus 0's.

## Tests and results

| # | Test | Result | Meaning |
|---|---|---|---|
| 1 | Normal mode, bus 0 ↔ bus 2 | Both fail, TWAI bus-off | Neither bus is ACKing the other |
| 2 | `TWAI_SELF_TEST` | Bus-off, bus errors climbing, no `Rx 2` | Fault is between the ESP32 and the bus, on bus 2's side |
| 3 | `TWAI_PIN_TEST` (toggle TX every 10 µs, read RX) | `TX low -> RX low 0/1000   TX high -> RX high 1000/1000` | RX stuck high |
| 4 | Meter during pin test: IO33, TXD | 1.6 V each (toggling) | TX path OK |
| 5 | Meter: CANH − CANL | 1.55 V | Transceiver is driving the bus |
| 6 | Meter: RXD pin 3, IO36 | 3.2 V each | RX stuck high |
| 7 | VCC2 / VCC1 | 4.95–5.0 V / 3.3 V | Transceiver powered |
| 8 | GND1 (pin 4) to ESP32 GND | First measured open, then rechecked as connected | Ground OK. The first reading was likely taken against the wrong point. |
| 9 | TX/RX pin assignment | Code, `spud.h` and schematic agree | Wrong: the code and schematic agree, but the built board has A0/A5 swapped (see test 12). |
| 10 | Dev board unplugged, same firmware | IO33 = 1.65 V, IO36 = 0.15 V | ESP32 is fine and doesn't pull IO36 high. The high comes from the SPUD board. |
| 12 | `TWAI_CAN0_RX_TEST`: bus 0 retransmits continuously, GPIO33 sampled directly | `low 127929/200000  edges 18741` | The receiver works and its output is on GPIO33, so the board is swapped. |
| 13 | After the rework: `TWAI_PIN_TEST`, then normal mode bus 0 ↔ bus 2 | 1000/1000 on both counts, then `Rx 2` ID `100` and `Rx 0` ID `700` with matching data | CAN3 working |
| 11 | `TWAI_RX_MONITOR` (TX held recessive) with a 1.5 V cell across CANH/CANL, both polarities | RX stays at 3139 mV either way | Receiver doesn't respond to a dominant bus. Either U6's receiver is faulty, or the voltage isn't reaching pins 6/7. |

## Still to do (superseded: the fault was the A0/A5 swap, see above)

1. **Battery test of the receiver**: done, test 11. RX didn't respond.
2. **With the battery connected, measure pin 7 to pin 6 directly on the
   chip's legs.** This is the deciding check.
   - About 1.5 V: the voltage reaches the receiver and RXD still doesn't
     respond, so U6 is faulty.
   - About 0 V: the connection between the bus connector and the chip
     is broken (U4, the tracks, or the pin 6/7 joints).

   Also check pins 6/7 at the chip's legs against GND2.
   - Idle, each should be about 2.5 V.
   - Toggling, CANH should be about 3.0 V and CANL about 2.0 V.

   This confirms the receiver's inputs actually see the bus.
3. **Inspect U6.**
   - Marking reads ISO1042.
   - Pin 1 orientation is correct.
   - Joints on pins 3, 6 and 7 are good.
   - The chip isn't running hot.
4. **If the receiver is faulty, replace U6**, then retest in this order:
   1. `TWAI_PIN_TEST`: expect 1000/1000 on both counts.
   2. `TWAI_SELF_TEST`: expect `Rx 2` lines with ID `700` and bus errors
      at 0.
   3. Normal mode, bus 0 ↔ bus 2, with a ground wire GND2 ↔ bus 0 GND:
      expect `Rx 2` ID `100` and `Rx 0` ID `700`.

## Firmware test switches (removed)

These switches were used during bring-up and have since been removed
from `src/main.cpp` to keep the example simple. The table records what
each one did.

| Define | Default | Purpose |
|---|---|---|
| `SEND_CAN1` | off | Send test frames on bus 1 (needs a 1 Mbit/s node) |
| `SEND_CAN_TWAI` | on | Send test frame ID 0x700 on bus 2 |
| `TWAI_SELF_TEST` | off | Bus 2 receives its own frames, no ACK needed |
| `TWAI_DEBUG` | on | Print TWAI state and error counters every second |
| `TWAI_PIN_TEST` | off | No CAN at all: toggle TX, check RX follows through the transceiver |
| `TWAI_PINS_SWAPPED` | off | For a board with A0/A5 swapped: RX on GPIO33, bus 2 listen-only. Needs another node on the bus to ACK. |
| `TWAI_CAN0_RX_TEST` | off | Bus 0 sends, TWAI not started: sample the bus 2 RX pin and count edges |
| `TWAI_RX_MONITOR` | off | No CAN at all: hold TX recessive, print RX voltage. Apply 1.5 V across CANH(+)/CANL(−) to test the receiver. Takes priority over `TWAI_PIN_TEST`. |

When bus 2 works, comment out `TWAI_RX_MONITOR`, then `TWAI_SELF_TEST`,
then `TWAI_DEBUG`.
