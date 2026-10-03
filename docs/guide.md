# Getting started

This guide uses a **Werkzeug**, an **LS10A** module, a **Wolfszahn**
adapter and a USB cable. Every program in it can be typed as shown.

## 1. Werkzeug's console

Connect Werkzeug to the computer. It appears as two serial ports: the
first is the BASIC console, the second the Sechs bridge.

| System | BASIC console | Sechs bridge |
|---|---|---|
| Linux | `/dev/ttyACM0` | `/dev/ttyACM1` |
| macOS | the first `/dev/cu.usbmodem...` | the second |
| Windows | the first of the two COM ports | the second |

Open the BASIC console in a terminal program (for example `tio
/dev/ttyACM0` on Linux, PuTTY on Windows; the speed setting does not
matter). Werkzeug answers with `///`. If it also says `NOT FORMATTED`, type
`FORMAT YES` once to prepare its storage.

## 2. A first program

Type these lines, then `RUN`. The green LED blinks; Ctrl-C stops it.

```basic
10 LED 1: WAIT 500
20 LED 0: WAIT 500
30 GOTO 10
```

`LIST` shows the program, `SAVE BLINK` keeps it, `NEW` clears it and
`LOAD BLINK` brings it back. `HELP` lists every command and keyword.

## 3. The module

Put the LS10A into the Wolfszahn and the Wolfszahn into Werkzeug's PMOD
socket, top row, with the module's pin 1 at the socket's pin 1. Werkzeug
powers the module.

The module is reached through the Sechs bridge with `sechsctl` (from the
release downloads, or `make sechsctl` in the source):

```
sechsctl -d /dev/ttyACM1 scan
sechsctl -d /dev/ttyACM1 console 0x0c
```

`scan` lists the modules it finds (a new module answers at 0x0c).
`console` opens the module's own BASIC console over the bus. Everything
works as on Werkzeug; end the console with Ctrl-D.

## 4. A program that starts by itself

In the module's console:

```basic
NEW
10 LED 1: WAIT 200
20 LED 0: WAIT 800
30 GOTO 10
```

Then `SAVE BOOT` and `RUN`. The module's LED blinks. To stop it, run
`sechsctl -d /dev/ttyACM1 halt 0x0c` in a second terminal. A program saved
as `BOOT` runs by itself when the module is powered up, with or without a
computer.

## 5. Talking to a running program

The 16 program registers are shared between a running program and the
controller. This program counts in register 0:

```basic
NEW
10 REG 0, REG(0) + 1
20 WAIT 1000
30 GOTO 10
```

`RUN` it, then read the count from the computer while it runs:
`sechsctl -d /dev/ttyACM1 reg 0x0c 0`. Writing a register
(`reg 0x0c 1 5`) is how a controller passes a value in.

## 6. Keeping data

Files survive power loss. A program can log to one:

```basic
NEW
10 OPEN "LOG.TXT" FOR APPEND AS #1
20 PRINT #1, REG(0)
30 CLOSE #1
40 WAIT 5000
50 GOTO 10
```

`TYPE LOG.TXT` shows the file, `DIR` lists the files, `DEL LOG.TXT`
deletes it.

## 7. Werkzeug's pins

On Werkzeug, pins 1-4 are the module socket, 5-8 the PMOD's bottom row and
9-24 the GPIO header (21-24 are analog inputs). Pins from 5 up are declared
one at a time with `PIN`:

```basic
10 PIN 9,PP: PIN 21,AIN
20 OUT 9, 1
30 PRINT ADC(21)
```

Pin 9 is the header's GPIO0; `ADC` reads 0 to 1023.

## 8. Next

- The reference sheet: the whole language on one side, Sechs on the other.
- `docs/` in the source: the language (`basic1.md`), the machines
  (`targets.md`) and Sechs (`sechs.md`).
- A module's firmware can be restored or upgraded from Werkzeug with
  `sechsctl flash` and three wires (`docs/ch32prog.md`).
