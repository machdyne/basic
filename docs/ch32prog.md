# Programming LS10 modules from Werkzeug

**Status: implemented and tested against a simulated CH32V003; not yet run
on hardware.** This document fixes what the programmer must and must never
do, because a mistake here can make a module unusable.

## 1. Purpose

Werkzeug writes the Machdyne BASIC firmware to an LS10 (CH32V003), so that
users can restore or upgrade their modules without a WCH-LinkE. The
module's files (F-RAM) and its Sechs address are not touched: an upgraded
module keeps its programs and data.

## 2. Wiring

Only SWIO, power and ground are required. Everything goes to the top row of
Werkzeug's GPIO header, so female jumpers can be used:

| Werkzeug header | Signal | LS10A | Required |
|---|---|---|---|
| 1 (GPIO0) | drives SWIO through a 1k resistor | J3 pin 4 (SWDIO) | yes |
| 2 (GPIO1) | senses SWIO, on the module's side of the resistor | J3 pin 4 (SWDIO) | yes |
| 3 (GPIO2) | RESETN (open drain) | J3 pin 5 (RESETN) | no: for recovery |
| 9 | GND | GND (Sechs pin 5) | yes |
| 10 | 3V3 | 3V3 (Sechs pin 6) | yes |

The two programmer pins and the resistor make one SWIO signal: the drive pin
stays high between pulses, so the resistor is also the line's pull-up, and
the module can still pull the line low to answer a read; the sense pin
reads the line itself. The resistor also limits the current when both ends
drive.

RESETN is optional. Without it, the programmer reaches any module whose
firmware leaves SWIO on (Machdyne BASIC always does). With it, the
programmer restarts the module and stops it before its firmware gets far,
which recovers even firmware that turns SWIO off (rule 7).

J3 pin 6 (WPN, the F-RAM's write protect) stays unconnected. While it
programs, Werkzeug uses BASIC pins 9-11 (header GPIO0-2); the bridge
answers `busy` if a BASIC program has declared them.

## 3. The protocol

From WCH's *QingKeV2 Microprocessor Debug Manual*, chapter 2 (checked),
and Charles Lohr's MIT/BSD-licensed reference implementation
(`esp32s2-cookbook/ch32v003programmer/main/ch32v003_swio.h`), which has
been used to program these chips for years:

- **Packets:** a start bit (1), a 7-bit debug register address, a
  read/write bit (1 = write), 32 data bits, most significant first;
  optional even parity; then a stop (the line high).
- **Bits** are coded by the length of a low pulse, in units of the
  target's debug clock T. Normal mode (after reset): a 1 is low for T to
  4T, a 0 for 6T to 64T, each followed by high for T to 16T; a stop is high
  for at least 18T.
- **Reads:** the host pulls the line low briefly and releases it; the
  target holds it low for a 0. The host samples after about two pulse
  widths and waits for the line to return high.
- **Debug module:** RISC-V debug registers (DMCONTROL 0x10, DMSTATUS 0x11,
  abstract commands, program buffer), and WCH's CPBR/CFGR (0x7C/0x7D) to
  enable the target's output.
- **Flash:** the flash controller at 0x40022000 (KEYR, MODEKEYR, CTLR,
  STATR), 64-byte pages, programmed through its page buffer.

The pulses are timed in CPU cycles (8 ns at 125 MHz) by a routine that
runs from RAM with interrupts off for each packet (`targets/werkzeug/
swio.c`): a 1 is low for about 80 ns, a 0 for about 330 ns, each followed
by about 80 ns high, as in the reference. This keeps the structure of the
reference, which is proven on real chips; the bit routines are small and
can move to the PIO if hardware tests show a need.

## 4. Failsafe rules

These hold in every version of the programmer, and the tests check them.

1. **Main flash only.** The programmer erases and writes only
   0x08000000-0x08003FFF (16KB). It never writes the option bytes, the
   system bootloader area, or any other address.
2. **Never the option-byte key.** It never writes OBKEYR, so read and
   write protection cannot be changed, even by mistake. (The reference
   implementation unlocks it; this programmer does not.)
3. **The right chip.** Before erasing anything, it identifies the target
   through the debug module and continues only for a CH32V003.
4. **The right image, complete.** The whole image is received into
   Werkzeug's RAM and checked before the module is touched: at most
   16,384 bytes, its CRC32 as sent by the computer, and the Machdyne BASIC
   identity text inside it (unless forced). A transfer that breaks off
   erases nothing.
5. **Stopped before written.** The core is halted (through RESETN, before
   the old firmware runs, if wired) before any erase.
6. **Verified before released.** Every byte is read back and compared. The
   module is reset and released only after the whole image verifies; on a
   mismatch, the programmer erases and writes again (up to three times),
   then reports the failure and leaves the module halted.
7. **Always recoverable.** None of the above can leave the module unable
   to be programmed again: the debug interface does not depend on the
   flash contents, and with RESETN wired the programmer can stop any
   firmware (even one that turns off the debug pin) before it starts. A
   power cut or an unplugged cable at any point leaves a module that the
   next attempt can program.
8. **F-RAM untouched.** WPN is not connected, and nothing is sent to the
   F-RAM.

## 5. Use

```
sechsctl -d /dev/ttyACM1 flash ls10.bin
```

1. `sechsctl` sends the image with its CRC32 over the bridge port
   (`f`, `d`).
2. On `p`, Werkzeug checks it (rule 4), connects, stops the module
   (rule 5), identifies it (rule 3), then erases, writes and verifies every
   page of the main flash (rule 6), reporting progress.
3. On success the flash is locked again and the module restarts with the
   new firmware, its files and address intact. `force` skips the identity
   check of rule 4 (for other firmware).

## 6. Implementation and tests

| Part | Where |
|---|---|
| Programmer (hardware-independent) | `tools/sechs/ch32prog.c` |
| SWIO on Werkzeug | `targets/werkzeug/swio.c` |
| Bridge commands `f`, `d`, `p` | `tools/sechs/bridge.c`, `targets/werkzeug/usb.c` |
| `sechsctl flash` | `tools/sechs/sechs.c` |
| Simulated CH32V003 | `tools/sechs/test/ch32sim.h` |

Every write to the target passes one gate that allows only the main flash
and the five flash controller registers programming needs; anything else
is refused, counted, and fails the job (rules 1 and 2).

The simulated chip is stricter than the real one: NOR flash that only the
controller's documented sequences change, a debug module that runs commands
only while halted, and violations counted for the option-byte key, the
option bytes, direct flash writes and any other address. `make test` runs
(`ch32prog_test`, and through the bridge in the test suite):

- a normal write (about 139,000 debug transactions) and a full 16KB image;
- refusals before anything is touched: an image without the identity, an
  empty or oversized image, no chip, a different chip, a read-protected
  chip (never unlocked);
- firmware that turns SWIO off: not reachable without RESETN, recovered
  with it;
- a power cut at 400 points spread over the whole job (tearing erases and
  page programs in progress), each followed by a successful run;
- a corrupted bit in a random read, 300 times: never a reported success
  with wrong contents;
- `sechsctl flash` through the bridge to the simulated chip.

No test has ever seen a violation.

**Not yet verified on hardware:** the SWIO timing, the chip identification
values (`hartinfo` and the chip ID at 0x1FFFF7C4; a mismatch refuses, and
the message shows the ID read), and the halt-after-reset race with RESETN.

## 7. Bringing it up on hardware

1. A spare LS10 first, with RESETN wired.
2. `sechsctl flash` with the current `ls10.bin`: expect `ok written and
   verified`, and the module working as before (files kept).
3. If it reports a wrong chip, note the ID it shows; the identification may
   need adjusting (it refuses rather than guesses).
4. Then without RESETN, then several times in a row, then unplugging the
   USB cable in the middle once (the module must still program afterwards).
