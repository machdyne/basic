# Programming modules from Werkzeug (LS10, LS11)

**Status: CH32V003 (LS10): tested in simulation and on hardware (an LS10A,
2026-10-04: one pin, no resistor, a complete flash with read-back in
13.2 s). CH32V005 (LS11): tested in simulation only.** This document fixes what the programmer must and must never do,
because a mistake here can make a module unusable.

## 1. Purpose

Werkzeug writes the Machdyne BASIC firmware to a module's microcontroller,
so that users can restore or upgrade their modules without a WCH-LinkE.
The module's files and its Sechs address are not touched: an upgraded
module keeps its programs and data.

| Module | Chip | Flash | Pages | SWIO |
|---|---|---|---|---|
| LS10 | CH32V003 | 16 KB | 64 bytes | rear pin 10: a jumper from Werkzeug's GPIO header pin 1 |
| LS11 | CH32V005 | 32 KB | 256 bytes | pin A, in programming mode: through the socket (`-s`) |

## 2. Wiring

One jumper wire: Werkzeug's GPIO header **pin 1 (GPIO0)** to the module's
**SWIO**, directly, with no resistor. The module gets power and ground as
usual (in the Wolfszahn on the PMOD, for example), or from header pins 9
(GND) and 10 (3V3).

| Werkzeug header | Signal | LS10A | Required |
|---|---|---|---|
| 1 (GPIO0) | SWIO | rear pin 10 (J3 pin 4, SWDIO) | yes |
| 3 (GPIO2) | RESETN (open drain) | rear pin 11 (J3 pin 5, RESETN) | no: for recovery |
| 9, 10 | GND, 3V3 | Sechs pins 5, 6 | if not powered otherwise |

One pin carries SWIO in both directions, as in the reference programmers:
the line is driven high between bits, pulled low for each bit, and
released only inside a read bit, so the chip can answer by holding it low.
The chip only drives the line during a read bit, so nothing can fight; the
RP2040's internal pull-up keeps the line high while it is released. (A
second line mode, released between bits, also works: for SWIO on a line
shared with other devices.)

RESETN is optional. Without it, the programmer reaches any module whose
firmware leaves SWIO on (Machdyne BASIC always does). With it, the
programmer restarts the module and stops it before its firmware gets far,
which recovers even firmware that turns SWIO off (rule 7).

The module's WPN (F-RAM write protect) stays unconnected. While it
programs, Werkzeug uses BASIC pins 9 and 11 (header GPIO0 and GPIO2); the
bridge answers `busy` if a BASIC program has declared them.

### Timing, measured on a CH32V003 (LS10A, 2026-10-04)

The chip's debug clock period T is about 83 ns, and every edge of the
working window falls where WCH's rules put it:

| | Rule | Measured |
|---|---|---|
| a 1 | T to 4T (83-333 ns) | works 120-300 ns, fails at 110 and 350 |
| a 0 | at least 6T (500 ns) | fails at 450, works 500-2000 ns (the longest tried) |
| read sample delay | | anything from 50 to 250 ns |
| pause after a transaction | | fails at 1 us, works from 2 us |
| line mode | | both |

The defaults sit in the middle of each window: a 1 is 180 ns low, a 0
900 ns, each followed by 150 ns high; reads sample 150 ns after release; a
transaction is followed by 4 us. With them: 10,000 link round trips without
an error in either line mode; chip ID 0x00310510, hartinfo 0x002120f4; the
whole 16 KB erased, written, verified page by page and read back in 13.2 s;
the module restarted running the new firmware. `sechsctl swio-timing`
shows or changes the timing until Werkzeug restarts (for other chips, or
other wiring).

The time is about 50 us per debug transaction, so a 32 KB chip (CH32V005)
would take about 26 s. Auto-execution, a flash loop in the program buffer
and a cheaper read-back would bring that down to a few seconds: a later
optimisation.

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
  STATR), programmed a page at a time through its page buffer: 64-byte
  pages on the CH32V003, 256-byte pages on the CH32V005, with the same
  sequence (as ch32fun's minichlink).
- **Identification:** the debug module's chip ID register (0x7F), read
  without halting, as minichlink does: 0x003..5.. is a CH32V003,
  0x005..... a CH32V005.

The pulses are timed in CPU cycles (8 ns at 125 MHz) by a routine that
runs from RAM with interrupts off for each packet (`targets/werkzeug/
swio.c`), with the measured timing of section 2. The bit routines are
small and could move to the PIO if that is ever needed.

## 4. Failsafe rules

These hold in every version of the programmer, and the tests check them.

1. **Main flash only.** The programmer erases and writes only the
   identified chip's main flash: 0x08000000-0x08003FFF (16KB) on a
   CH32V003, 0x08000000-0x08007FFF (32KB) on a CH32V005; before the chip
   is identified, nothing. It never writes the option bytes, the system
   bootloader area, or any other address.
2. **Never the option-byte key.** It never writes OBKEYR, so read and
   write protection cannot be changed, even by mistake. (The reference
   implementation unlocks it; this programmer does not.)
3. **The right chip.** Before erasing anything, it identifies the target
   through the debug module and continues only for a CH32V003 or a
   CH32V005. A CH32V003 must also pass the check proven on hardware
   (hartinfo, and the chip ID word in memory).
4. **The right image, complete.** The whole image is received into
   Werkzeug's RAM and checked before the module is touched: at most
   32,768 bytes, its CRC32 as sent by the computer, and the Machdyne BASIC
   identity text inside it (unless forced). A transfer that breaks off
   erases nothing. Once the chip is identified, and still before anything
   is written: the image must fit its flash, and the module it was built
   for (`mod=LS10A` or `mod=LS11A`) must match the chip, since their pins
   differ (unless forced). A refused image leaves the chip running as
   before.
5. **Stopped before written.** The core is halted (through RESETN, before
   the old firmware runs, if wired) before any erase.
6. **Verified before released.** Every page is read back and compared as
   it is written; on a mismatch, the programmer erases and writes it again
   (up to three times). Then the whole flash is read back a second time.
   The module is reset and released only after both pass; otherwise the
   programmer reports the failure and leaves the module halted.
7. **Always recoverable.** None of the above can leave the module unable
   to be programmed again: the debug interface does not depend on the
   flash contents, and with RESETN wired the programmer can stop any
   firmware (even one that turns off the debug pin) before it starts. A
   power cut or an unplugged cable at any point leaves a module that the
   next attempt can program.
8. **Files untouched.** Nothing is sent to the module's F-RAM or
   EEPROM, whose write-protect pins are not connected to the programmer.

## 5. Use

```
sechsctl -d /dev/ttyACM1 swio-test 1000     # the wire alone: write and read
                                            # back a debug register 1000 times;
                                            # the module keeps running
sechsctl -d /dev/ttyACM1 swio-id            # stop the module, read its chip
                                            # ID, restart it; writes nothing
sechsctl -d /dev/ttyACM1 flash ls10.bin     # program it
sechsctl -d /dev/ttyACM1 swio-timing        # show (or set) the bit timing
```

**LS11, through the socket.** No wires: the module sits in the Wolfszahn
on the PMOD as usual. Put it in programming mode, then within its 10
seconds program it with `-s` (SWIO on the socket's pin A):

```
sechsctl -d /dev/ttyACM1 program 0x0c       # (or BOOT at its console, or
                                            # a pulse on RESETN)
sechsctl -d /dev/ttyACM1 -s flash ls11.bin
```

While the module is in programming mode, pin A is not the Sechs bus: the
bridge sends nothing on it but SWIO. `-s` also works with `swio-id` and
`swio-test`.

1. `sechsctl` sends the image with its CRC32 over the bridge port
   (`f`, `d`).
2. On `p`, Werkzeug checks it (rule 4), connects, stops the module
   (rule 5), identifies it (rule 3), then erases, writes and verifies every
   page of the main flash, and reads it all back again (rule 6). It reports
   each step (`status` lines: connecting, the chip ID, unlocking, writing,
   verifying) and the progress; `sechsctl` shows them and the time taken.
3. On success the flash is locked again and the module restarts with the
   new firmware, its files and address intact. `force` skips the identity
   check of rule 4 (for other firmware).

If `swio-test` fails, its answer shows what was read back: `ffffffff`
means the chip does not answer (wiring, or timing for another chip), 0 or
"the line stayed low" means something holds the line low.

## 6. Implementation and tests

| Part | Where |
|---|---|
| Programmer (hardware-independent) | `tools/sechs/ch32prog.c` |
| SWIO on Werkzeug | `targets/werkzeug/swio.c` |
| Bridge commands `f`, `d`, `p`, `i`, `t`, `s`, `c` | `tools/sechs/bridge.c`, `targets/werkzeug/usb.c` |
| `sechsctl flash`, `swio-id`, `swio-test`, `swio-timing`, `-s` | `tools/sechs/sechs.c` |
| Simulated CH32V003 and CH32V005 | `tools/sechs/test/ch32sim.h` |

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
- the CH32V005: a 20KB and a full 32KB image, 150 power cuts and 100
  corrupted reads; firmware for the other module refused both ways
  (nothing written, the chip runs on); a CH32V004 refused;
- `sechsctl flash`, `swio-id` and `swio-test` through the bridge to the
  simulated chips: an LS10 on the header wire, an LS11 in the socket.

No test has ever seen a violation.

**Verified on hardware** (LS10A, 2026-10-04): the SWIO timing on one pin
without a resistor, the chip identification values, a complete flash with
read-back, and the module running the new firmware afterwards.

**Not yet verified on hardware:** everything on the CH32V005 (its chip ID
register, 256-byte page programming, and whether its debug clock gives the
same timing window: `swio-timing` can adjust it); the halt-after-reset race with RESETN
(recovering firmware that turns SWIO off), a cable unplugged in the middle
of a job, and SWIO on a line shared with the Sechs bus (planned for LS11,
where SWIO is on pin A).

## 7. Hardware tests

Done (docs/hwtest.md 5): the wire (`swio-test`, both line modes), the
identification (`swio-id`) and a complete flash on an LS10A.

Still to do:

1. Several flashes in a row, and unplugging the USB cable in the middle of
   one: the module must still program afterwards.
2. With RESETN wired: a module whose firmware turns SWIO off, recovered.
3. SWIO sharing pin A with the Sechs bus (LS11): programming with the
   line released between bits (`swio-timing ... 1`), another module on the
   bus undisturbed.
