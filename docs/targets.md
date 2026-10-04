# Machdyne BASIC targets

The language is [Machdyne BASIC 1](basic1.md) everywhere. This document
describes what each machine adds to it: its pins, its storage, how it
starts, and how it is reached.

| Target | Pins | Analog inputs | Files | Runs `BOOT.BAS` at start-up |
|---|---|---|---|---|
| LS10 (Sechs module) | 1-4 | 3, 4 | 8KB F-RAM | yes |
| Werkzeug | 1-24 | 21-24 | flash (2MB on V3C) | yes |
| Blaustahl | none | none | 8KB F-RAM | yes |
| Linux | simulated 1-4 | simulated | the current directory | no |

## LS10 (Sechs module)

A CH32V003 (16KB flash, 2KB RAM) with 8KB of F-RAM, on a
[Sechs](sechs.md) connector. The firmware is a Sechs
module: pins 1 and 2 are its I2C target, through which a controller finds,
controls and programs it.

**Pins.** 1 (A) and 2 (B) are the Sechs bus; 3 (C) and 4 (D) are local
I/O: `AIN` on both (ADC channels 6 and 5), `I2C` (3 = SCL, 4 = SDA), and the
UART console (the module receives on 3 and transmits on 4). The LED is
`LED`. A program that declares pins 1 and 2 as outputs stops with `ON A
BUS` once a controller has addressed the module.

**Start-up.** Nothing is driven at power-on. For 600 ms (the boot window) a
controller can halt the module, and Enter on the UART wakes the console.
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

**Building.** `targets/ls10`, with ch32fun; the firmware fills the 16KB
flash exactly.

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
