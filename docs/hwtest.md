# Hardware tests

Nothing below has been run on hardware yet. Everything has been tested on
the host (`make test`), with simulated media, pins, I2C and modules. This
is the order in which to test it on real hardware, and what to expect.
Note anything that differs, with the firmware's commit.

## 1. Bench setup

- **Werkzeug** with `werkzeug.uf2` (`targets/werkzeug`): it appears as two
  serial ports, the BASIC console (`...-if00`) and the Sechs bridge
  (`...-if02`). Below, `/dev/ttyACM1` stands for the bridge port.
- **LS10A** in a Wolfszahn on Werkzeug's PMOD (top row: module pin 1 at
  PMOD 1). Werkzeug's 3.3V powers it.
- `make sechsctl` on the computer.

## 2. Bring-up (LS10, before anything else)

| # | Do | Expect |
|---|---|---|
| 2.1 | Flash `ls10.bin`, power the module | nothing driven on pins 1-4 during the first 600 ms (scope on pin 4: no UART idle level until woken) |
| 2.2 | `sechsctl -d /dev/ttyACM1 scan` | `0x0c ...` |
| 2.3 | `sechsctl -d /dev/ttyACM1 info 0x0c` | version 0.5, caps 0x07, `fw=Machdyne BASIC`, `mod=LS10A` |
| 2.4 | `echo "FORMAT YES" \| sechsctl -d /dev/ttyACM1 send 0x0c` (new module only) | no error |

If 2.2 finds nothing: check the pull-ups on pins 1 and 2 (the RP2040's
internal ones are weak; add 2.2-4.7k), and that the module is the right way
round in the Wolfszahn.

## 3. Automated test

```
tools/sechs/hwtest.sh -d /dev/ttyACM1
```

18 checks: identity, the I2C console, `SAVE`/`LOAD`/`TYPE`/`DEL`, data
files, register exchange with a running program, `HALT`, `FAULT`,
`ON A BUS`, address change and back, `RESET` with `BOOT.BAS` and storage
across the reset. It keeps an existing `BOOT.BAS`. Expect
`18 passed, 0 failed`.

Then the UART console (the bridge's UART mode ends when the port closes):

```
tools/sechs/hwtest.sh -d /dev/ttyACM1 --uart
```

Expect the extra check "UART console wakes and answers (///)".

## 4. Manual tests on LS10

Type these on the console (`sechsctl -d /dev/ttyACM1 console 0x0c`).

| # | Program | Fixture | Expect |
|---|---|---|---|
| 4.1 | `10 LED 1: WAIT 500: LED 0: WAIT 500: GOTO 10` | none | the LED blinks once a second (`LED 1` = on; the schematic has it active low on PD4) |
| 4.2 | `10 PINS NET,NET,PP,-` / `20 OUT 3, 1: WAIT 500: OUT 3, 0: WAIT 500: GOTO 10` | LED and resistor, or a meter, on pin 3 | toggles every 500 ms. The UART console goes quiet (pin 3 is taken); the I2C console still works |
| 4.3 | `10 PINS NET,NET,-,AIN` / `20 PRINT ADC(4): WAIT 200: GOTO 10` | potentiometer on pin 4 | 0 at GND, 1023 at 3.3V, smooth in between |
| 4.4 | `10 PINS NET,NET,I2C,I2C` / `20 PRINT I2CR(72, 0)` | an I2C sensor on pins 3/4 with pull-ups (address adjusted) | a plausible value; -1 without the sensor |
| 4.5 | `10 PRINT "A": SLEEP 400: PRINT "B"` | a stopwatch | B 400 s after A (the old bug: wrong above 357 s) |
| 4.6 | `10 PRINT "T": WAIT 10000: PRINT "U"` | a stopwatch | 10 s |
| 4.7 | Ctrl-C on the UART console during 4.5 | | `BREAK IN 10` at once |
| 4.8 | Save a program as `BOOT`, power off and on | | it runs 600 ms after power-on |
| 4.9 | Power off during a running data logger (`OPEN ... APPEND`, `PRINT #`, in a loop), 10 times | | the file is readable with `TYPE` every time; at most the last line is incomplete |
| 4.10 | Same as 4.2 with the module on a bus where a master has addressed it | | `ON A BUS` |
| 4.11 | Run a data logger, and change the address from the master while it logs (`sechsctl addr`), several times | | the log stays readable, and the new address survives a power cycle (the address is saved to F-RAM in the main loop, never during a file write) |

## 5. Werkzeug with Machdyne BASIC

Open the BASIC port (`...-if00`) in a terminal.

| # | Do | Expect |
|---|---|---|
| 5.1 | Plug in, open the BASIC port | `///` (and `NOT FORMATTED` the first time) |
| 5.2 | `FORMAT YES`, then `10 PRINT 1`, `SAVE T`, `DIR` | `T.BAS` |
| 5.3 | Unplug and replug, `LOAD T`, `LIST` | the program is still there |
| 5.4 | Reflash `werkzeug.uf2`, `DIR` | files survive a firmware update (they are in the upper half of the flash) |
| 5.5 | On a Werkzeug with 1MB flash | `FORMAT YES`, then save programs until `DISK FULL`: about 500KB fit; the firmware still starts afterwards |
| 5.6 | `10 LED 1` / `RUN` | the green LED lights |
| 5.7 | `10 PINS -,-,PP,-: OUT 3, 1` | PMOD pin 3 (GPIO15) high |
| 5.8 | `10 PIN 9, PP: OUT 9, 1` | header GPIO0 high; `PIN 9, PP` lists as `PIN 9,PP` |
| 5.9 | `10 PIN 21, AIN: PRINT ADC(21)`, potentiometer on GPIO26 | 0 at GND, 1023 at 3.3V |
| 5.10 | Save `10 LED 1: WAIT 300: LED 0: WAIT 300: GOTO 10` as `BOOT`, power Werkzeug from a charger (no computer) | it blinks: `BOOT.BAS` runs without a terminal |
| 5.11 | Log 2,000 lines with `APPEND`, unplug in the middle a few times | `TYPE` always works; at most one line damaged per unplug |
| 5.12 | `SAVE` a long program while typing in the terminal | nothing lost; USB pauses briefly while a flash sector is erased (about 45 ms) |
| 5.13 | Run `10 PINS NET,NET,PP,-: GOTO 10`, then `sechsctl -d /dev/ttyACM1 uart 115200` | `busy` (the program has pin 3); after Ctrl-C and `NEW`, it works |
| 5.14 | While a program runs on the Werkzeug, `sechsctl -d /dev/ttyACM1 scan` | the bridge answers (it is served between statements) |

## 5a. Programming an LS10 from Werkzeug

Wiring and rules: [ch32prog.md](ch32prog.md). The LS10 sits in the
Wolfszahn on the PMOD as usual; one jumper wire goes from Werkzeug's GPIO
header **pin 1 (GPIO0)** to the LS10's rear **pin 10 (SWIO)**. No resistor.
Each step only goes on if the previous one passed.

| # | Do | Expect | Result |
|---|---|---|---|
| 5a.1 | `sechsctl -d /dev/ttyACM1 swio-test 10000` | `ok 10000 errors 0` (the wire only; the module keeps running) | passed 2026-10-04 (both line modes) |
| 5a.2 | `sechsctl -d /dev/ttyACM1 swio-id` | `ok chip 003xxxxx hartinfo ...`; the module restarts, nothing written | passed: `00310510`, `002120f4` |
| 5a.3 | `sechsctl -d /dev/ttyACM1 flash ls10.bin` | status lines, `verified 16384 bytes`, `ok written and verified`, a time | passed: 13.2 s |
| 5a.4 | `sechsctl -d /dev/ttyACM1 scan`, then `info` | the module answers, `fw=Machdyne BASIC`, its files still there | passed |
| 5a.5 | 5a.3 ten times in a row | `ok` every time | |
| 5a.6 | A file that is not Machdyne BASIC | `fail not a Machdyne BASIC firmware`, the module untouched | |
| 5a.7 | The jumper removed | `fail no chip answers on SWIO (read ffffffff)`, nothing written | |
| 5a.8 | Unplug Werkzeug's USB in the middle of writing, plug in, flash again | `ok`: the module programs again | |
| 5a.9 | RESETN wired (header pin 3 to rear pin 11), a firmware that turns SWIO off | flashing recovers it | |

If 5a.1 shows errors, nothing has been written: `swio-timing` shows the
timing, and the answer shows what was read back (ch32prog.md, section 5).

## 5b. SWIO sharing pin A (LS11 rehearsal)

For modules with SWIO on pin A. On a breadboard, the LS10's SWIO joins the
Sechs bus's pin A line, with pull-ups and a second module on the bus.

| # | Do | Expect |
|---|---|---|
| 5b.1 | `swio-timing 180 900 150 150 4 1` (line released between bits), then 5a.1-5a.4 | the same results |
| 5b.2 | `info` on the second module before and after | unchanged: same address, no fault |
| 5b.3 | Power-cycle the programmed module 50 times while a master polls the bus | it always starts normally, never halted |

## 6. Blaustahl

| # | Do | Expect |
|---|---|---|
| 6.1 | `FORMAT YES`, `SAVE`, `DIR`, `LOAD` | works; files kept after unplugging |
| 6.2 | `10 LED 1` / `RUN` | its LED lights |
| 6.3 | Save a program as `BOOT`, replug | it runs |

## 7. What to report

For each failure: the test number, what happened, and for LS10 the output
of `sechsctl info`. For 4.3-4.6, the measured values.
