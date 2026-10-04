# Machdyne BASIC targets

The language is [Machdyne BASIC 1](basic1.md) everywhere. This document
describes what each machine adds to it: its pins, its storage, how it
starts, and how it is reached.

| Target | Pins | Analog inputs | Files | Runs `BOOT.BAS` at start-up |
|---|---|---|---|---|
| LS10, LS11 (Sechs modules) | 1-4 (LS11 1-7) | 3, 4 (LS11 3-7) | 8KB F-RAM, EEPROM | yes |
| Werkzeug | 1-24 | 21-24 | flash (2MB on V3C) | yes |
| Blaustahl | none | none | 8KB F-RAM | yes |
| Linux | simulated 1-4 | simulated | the current directory | no |

## LS10 (Sechs module)

A CH32V003 (16KB flash, 2KB RAM) with 8KB of F-RAM, on a
[Sechs](sechs.md) connector. The firmware is a Sechs
module: pins 1 and 2 are its I2C slave, through which a master finds,
controls and programs it.

**Pins.** 1 (A) and 2 (B) are the Sechs bus; 3 (C) and 4 (D) are local
I/O: `AIN` on both (ADC channels 6 and 5), `I2C` (3 = SCL, 4 = SDA), and the
UART console (the module receives on 3 and transmits on 4). The LED is
`LED`. A program that declares pins 1 and 2 as outputs stops with `ON A
BUS` once a master has addressed the module.

**Start-up.** Nothing is driven at power-on. For 600 ms (the boot window) a
master can halt the module, and Enter on the UART wakes the console.
Otherwise `BOOT.BAS` runs.

**Consoles.** The UART console runs at 115200 baud and answers a wake with
`///`. Declaring pins 3 or 4 for anything else turns it off until the next
`RUN` or `NEW`. The I2C console (registers CIN, COUT, CDATA) carries the
same session, so every command works over I2C too.

**Sechs registers.**

| Sechs | Machdyne BASIC |
|---|---|
| address | 0x0c until changed; changed with the ADDR register (`sechsctl addr`), kept in F-RAM |
| 0x80-0x8F | `REG 0` to `REG 15` |
| INFO | `fw=Machdyne BASIC`, `mod=LS10A`, `lang=basic` |
| FAULT | 1: the program drove pins 1/2 on a bus (`ON A BUS`); 3: the program stopped with an error |
| CONTROL | HALT stops the program (as Ctrl-C); RUN runs it; RESET restarts the module |
| CAPS | files on the consoles, UART console, I2C console |
| STATUS | boot window, halted, running, console active, networked, fault, degraded (storage repaired a fault) |
| OK bit 4 | the last console command succeeded |

Not implemented (optional in Sechs): broadcasts, HOLD, IDENTIFY, `pins` in
INFO, the writable label, the stand-alone pull-up probe.

**Files.** 8,176 bytes of the F-RAM (the last 16 hold the address), 16
files. Power-loss safe ([fs.md](fs.md)). Firmware upgrades do not touch
them.

**Building.** `targets/ls10` (the board's `board.h` and F-RAM driver; the
module code is shared with LS11 in `targets/ls1x`), with ch32fun; the
firmware fills the 16KB flash almost exactly.

## LS11 (Sechs module)

A CH32V005 (32KB flash, 6KB RAM) with an 8KB I2C EEPROM, on the Zwölf
footprint. The same firmware as LS10 (`targets/ls1x`), with more room:
programs of up to 4,096 bytes, three more pins, and programming through
pin A. **Not yet tested on hardware.**

**Pins.**

| Pin | Where | Uses |
|---|---|---|
| 1 (A) | PD1 | the Sechs bus (SCL); SWIO in programming mode |
| 2 (B) | PD0 | the Sechs bus (SDA) |
| 3 (C) | PD6 | `AIN` (ADC 6), `I2C` SCL, the UART console (receives) |
| 4 (D) | PD5 | `AIN` (ADC 5), `I2C` SDA, the UART console (transmits) |
| 5 (E) | PD2, rear pin 7 | `IN`, `PP`, `OD`, `AIN` (ADC 3) |
| 6 (F) | PD3, rear pin 8 | `IN`, `PP`, `OD`, `AIN` (ADC 4) |
| 7 (G) | PD4, rear pin 9 | `IN`, `PP`, `OD`, `AIN` (ADC 7) |

Pins 5-7 are the `PIN` extension (`PIN 5, AIN`); BASIC 1 programs use 1-4
as on LS10. E and F are also a second UART (USART2), not used by BASIC
yet. Rear pin 10 is the same signal as pin 1, rear pin 11 is RESETN and
rear pin 12 is GND. The ADC has 12 bits; `ADC()` reads 0-1023 as on every
target. E, F and G go straight to the microcontroller: anything driving
them must be current-limited (about 1 kΩ). So must anything driving C and
D: as on LS10, they have no series resistors on the board.

**Programming mode.** Pin A is also SWIO, the CH32V005's programming pin,
but only in programming mode: otherwise it is the Sechs bus. The module
enters programming mode for 10 seconds (the LED blinks) when

- it is reset through RESETN (rear pin 11), but not at power-on;
- `BOOT` is typed at a console (LS11 only: it restarts the module);
- a master sends the Sechs `PROGRAM` command (CONTROL 0x06, CAPS bit 7).

A programmer connected to pin A then stops it and writes new firmware;
otherwise it restarts normally after 10 seconds.

**Start-up, consoles, Sechs registers.** As LS10, except: INFO says
`mod=LS11A`; CAPS also has bit 7 (programming mode); CONTROL also accepts
0x06 (PROGRAM).

**Files.** 8,176 bytes of the EEPROM (the last 16 hold the address and the
programming-mode request), 16 files. The EEPROM is bit-banged on its own
pins (PC4 SDA, PC5 SCL), never on the Sechs bus; its write-protect pin (PC6) is
held high except while writing. Writes take up to 5 ms per 32-byte page.

**To confirm on hardware:** the LED's polarity (PA1; assumed lit by a low
pin, as LS10); that `SWCFG` 0b100 turns SWIO off on the CH32V005, as on the
CH32V003; that PD7's reset function is enabled by default; the EEPROM size
(`EE_SIZE` in `board.h`: 8192 for an AT24C64, 4096 for an AT24C32); and the
filesystem on EEPROM under power cuts (a power cut can damage a whole
page being written, not just one byte).

**Building.** `targets/ls11` (`board.h`, `eeprom.c`), with LS10's
ch32fun checkout (`targets/ls10/ch32fun`). Flashing: a WCH-LinkE
(`make flash`), or Werkzeug through the socket with no wires:
`sechsctl -d ... program 0x0c`, then within 10 seconds
`sechsctl -d ... -s flash ls11.bin` ([ch32prog.md](ch32prog.md)).

## Werkzeug

An RP2040 board with a PMOD socket and a GPIO header. One firmware gives two
USB serial ports:

| Port | Interface name | Use |
|---|---|---|
| first (`...-if00`) | Werkzeug BASIC | the BASIC console |
| second (`...-if02`) | Werkzeug Sechs bridge | `sechsctl -d` for a module in the PMOD socket |

**Pins.** An extension of BASIC 1: pins 1-4 are declared with `PINS` as
everywhere; pins 5 to 24 with `PIN n, mode` (modes `-`, `IN`, `OD`, `PP`,
and `AIN` on 21-24). `RUN` makes pins 5 and up unused again. Programs that
use `PIN` run only on targets with those pins.

| Pins | Where | RP2040 |
|---|---|---|
| 1-4 | PMOD top row (the Sechs socket): A, B, C, D | GPIO19, 17, 15, 13 |
| 5-8 | PMOD bottom row | GPIO18, 16, 14, 12 |
| 9-20 | GPIO header | GPIO0-11 |
| 21-24 | GPIO header, analog inputs | GPIO26-29 (ADC0-3) |

Werkzeug is the module's carrier: pins 1-4 have the RP2040's internal
pull-ups whenever Werkzeug does not drive them, from power-on, so the
module's bus and UART lines never float. (Floating, they picked up the
UART traffic on pin 3 as I2C writes.) For long wires, add 2.2-4.7k
pull-ups on pins 1 and 2.

`OD` and `I2C` are emulated open-drain outputs with the internal pull-ups.
`I2C` is on pins 3 (SCL) and 4 (SDA), bit-banged, about 50 kHz. `ADC` reads
0-1023 (the 12-bit converter, scaled). `LED` is the green LED.

**The bridge and BASIC share the Sechs socket.** The bridge answers `busy`
if a BASIC program has declared pins 1/2 (for I2C) or 3/4 (for its UART
mode) for itself, and while the bridge's UART mode is on (until its port is
closed), a program cannot declare pins 3/4 (`ON A BUS`). The bridge's UART
transmits with the RP2040's PIO, so its bit timing is exact.

**Files.** The upper half of the flash, at its end, at most 2MB. The size
is read from the flash chip (its JEDEC ID) at start-up:

| Flash | Werkzeug | Files |
|---|---|---|
| 1MB | earlier boards | 512KB |
| 4MB | V3C | 2MB |
| 8MB or more | | 2MB (the last 2MB) |

A chip that does not identify itself counts as 1MB. Type `FORMAT YES` the
first time. Firmware updates do not touch the files.

**Start-up.** `BOOT.BAS` runs at power-on, with or without a computer. A
terminal that connects to the BASIC port gets `///`.

**Programming modules.** Werkzeug writes the firmware of an LS10 through
one jumper wire from GPIO header pin 1 (GPIO0) straight to the module's
SWIO, no resistor (optional RESETN on GPIO2):
`sechsctl -d ... flash ls10.bin`. The module's files and address are kept.
See [ch32prog.md](ch32prog.md).

**Building.** `targets/werkzeug`, with the Pico SDK
(`cmake -DPICO_BOARD=machdyne_werkzeug ..`).

## Blaustahl

An RP2040 USB stick with 8KB of F-RAM. Files are kept on the F-RAM, below
Blaustahl's own 512-byte metadata area. It has no pins for `PINS` (`NOT
SUPPORTED`); `LED` is its LED. `BOOT.BAS` runs at start-up.

## Linux

The reference and test platform. Files are ordinary files in the current
directory. Pins 1-4 are simulated: outputs read back on `IN`, `ADC(3)` is
300 and `ADC(4)` 400, and a 256-byte I2C memory answers at address 80. It
does not run `BOOT.BAS` by itself.
