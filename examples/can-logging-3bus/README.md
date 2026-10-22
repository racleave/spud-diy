# CAN send, receive and logging on three buses

This example tests all three CAN buses on the SPUD at once. It
regularly sends a test frame on each bus, and prints every frame it
receives on any bus to the serial monitor. You can save that output and
open it in SavvyCAN.

| Bus | Controller                 | Speed      |
|-----|----------------------------|------------|
| 0   | SPI CAN controller 0       | 500 kbit/s |
| 1   | SPI CAN controller 1       | 500 kbit/s |
| 2   | ESP32 built-in TWAI (CAN3) | 500 kbit/s |

## What it does

### Sending

About every 100 ms the program sends one 8-byte frame on each bus. All
three buses run at the same speed, so they can be connected together and
each one receives the other two.

| Bus | ID    | Contents                                                                 |
|-----|-------|--------------------------------------------------------------------------|
| 0   | 0x100 | Byte 3 is a ramping value (0–255). Byte 6 counts 0, 1, 2, 3 and repeats. |
| 1   | 0x651 | Fixed test values 280, -280 and 223 in bytes 0–5, plus a timer in bytes 6–7 that goes up by 1 every 10 ms. |
| 2   | 0x700 | The same data as the bus 0 frame.                                         |

After each send, the program prints a status line to the serial monitor:

    Sent CAN0 message, ID: 0x100
    Error sending CAN2 (TWAI) message! 2

### Receiving

The program checks all three buses continuously. Each frame it receives
is printed as one line:

    <time ms> <ID hex> 0 Rx <bus> <length> <data bytes hex>

For example, a frame with ID 0x100 received on bus 2:

    48213 100 0 Rx 2 8 00 00 00 7d 00 00 02 00

### Bus 2 recovery

If bus 2 gets too many errors, the TWAI controller shuts itself off
(called "bus-off"). The program checks for this about every 100 ms. It
prints `CAN bus 2 (TWAI) bus-off, recovering...` and restarts the
controller.

## How to test

A CAN frame only goes through if another node on the bus acknowledges
it. If a bus has nothing else on it, you'll see `Error sending ...`
lines for that bus, and bus 2 will go bus-off and recover over and over.
So test each bus against another node running at the same speed.

**Loopback test with all three buses.** Connect CAN0, CAN1 and CAN3
together on one pair: all the H wires together and all the L wires
together. Fit 120 Ω termination at each end of the cable; on a short
bench cable, one terminator is enough. CAN3 is isolated, so also run a
ground wire from its GND2 to the board's GND. Each bus then
acknowledges the others' frames. You should see:

- `Sent CAN0`, `Sent CAN1` and `Sent CAN2 (TWAI)` lines with no errors.
- Each frame received on the other two buses, six `Rx` lines per cycle:

  | ID    | Sent by | Received as      |
  |-------|---------|------------------|
  | `100` | bus 0   | `Rx 1` and `Rx 2` |
  | `651` | bus 1   | `Rx 0` and `Rx 2` |
  | `700` | bus 2   | `Rx 0` and `Rx 1` |

- Byte 3 changing between frames, and byte 6 counting 0–3.

To test just two buses, connect only those two. The third bus's frames
then fail with `Error sending ...`. To stop that, comment out its send
switch: `SEND_CAN1` for bus 1 or `SEND_CAN_TWAI` for bus 2.

**On a vehicle or other device**, set the bus speed to match the
network. Change `TWAI_TIMING_CONFIG_500KBITS()` in `twai_init()` for
bus 2, or the `CAN_500KBPS` values in `can_init()` for buses 0 and 1.
If you only want to listen, comment out `SEND_CAN` and `SEND_CAN_TWAI`
at the top of `src/main.cpp`. Otherwise the test frames go onto that
network.

If bus 2 goes bus-off or never receives, see
[TWAI-DIAGNOSTICS.md](TWAI-DIAGNOSTICS.md) for the fault-finding done
during bring-up.

## Logging to SavvyCAN

1. Save the serial output to a file:

       pio device monitor >& can-logging-test.log

2. Convert the log to SavvyCAN's GVRET CSV format. `-n 15` skips the
   startup lines at the top of the file. `-t 1` skips the last line,
   which may be cut off.

       python rev2gvret.py -n 15 -t 1 can-logging-test.log can-logging-test.csv

3. Open `can-logging-test.csv` in SavvyCAN. The "Sent ..." and error
   lines aren't filtered out, so they show up as junk rows. Ignore them,
   or delete them from the log before converting.
