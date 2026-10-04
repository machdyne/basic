# Machdyne BASIC

A lightweight BASIC implementation for embedded systems.

## Features

- **Tokenized execution** - Lines are tokenized when entered and checked for errors
- **26 variables** (A-Z)
- **Control flow** - IF/THEN/ELSE, GOTO, GOSUB/RETURN, FOR/NEXT, `:` between statements
- **I/O** - PRINT, INPUT, data files (OPEN, PRINT #, INPUT #)
- **Pins** - PINS, OUT, IN, ADC, LED, and I2C devices on the local bus
- **Sechs** - on modules: an I2C slave with a second console, so files and programs can be reached over I2C as well as the UART
- **Time** - SLEEP (seconds), WAIT (milliseconds)
- **Arithmetic** - Addition, subtraction, multiplication, division
- **Comparisons** - <, >, <=, >=, <>, ==
- **Files** - Programs are saved as plain text, on a power-loss-safe filesystem on modules
- **Errors** - Reported with the line number; Ctrl-C stops a running program

Planned work is described in [docs/plan.md](docs/plan.md).

## Targets

  * Linux
  * [Werkzeug](https://github.com/machdyne/werkzeug)
  * [Blaustahl](https://github.com/machdyne/blaustahl)
  * [Zwölf LS10A](https://machdyne.com/product/zwolf-ls10/)
  * Zwölf LS11A (in development)

## Building

### Linux
```bash
$ make
$ ./basic
```

On Linux, files are ordinary files in the current directory.

### Tests
```bash
$ make test         # filesystem tests and the language test suite
$ make test-quick   # the same with fewer random operations
```

`make test` also builds `basic_fs`, the interpreter with the module
filesystem on a simulated 8KB F-RAM, so that the file handling used on
modules is tested on Linux.

### LS10 and LS11
```bash
$ cd targets/ls10
$ git clone https://github.com/cnlohr/ch32fun
$ make                  # ls10.bin, and flashes it with a WCH-LinkE
$ make -C ../ls11       # ls11.bin (uses the same ch32fun checkout)
```

Both modules share their firmware (`targets/ls1x`); each board's directory
has a `board.h` with its pins, storage and features.

### RP2040 (Werkzeug / Blaustahl)

You will need [pico-sdk](https://github.com/raspberrypi/pico-sdk) (with its
TinyUSB submodule) and `PICO_SDK_PATH` set. The build produces a UF2 file;
drag it onto the device in bootloader mode.

#### Werkzeug

```bash
$ cd targets/werkzeug
$ mkdir build
$ cd build
$ cmake -DPICO_BOARD=machdyne_werkzeug ..
$ make
```

One firmware with two USB serial ports: the BASIC console, and the Sechs
bridge used by `sechsctl`. 24 pins (the Sechs socket, the PMOD's bottom
row and the GPIO header, with four analog inputs), files in flash. See
[docs/targets.md](docs/targets.md).

#### Blaustahl

```bash
$ cd targets/blaustahl
$ mkdir build
$ cd build
$ cmake ..
$ make
```

On Blaustahl, files are stored on its F-RAM; it has no pins for `PINS`,
and `LED` is its LED.

### One-page reference

`make poster` (or `python3 tools/poster/poster.py --pdf poster.pdf`)
generates a reference sheet, one A4 sheet printed on
both sides (the language on the front, Sechs on the back), from
[docs/basic1.md](docs/basic1.md), [docs/targets.md](docs/targets.md) and
[docs/sechs.md](docs/sechs.md).
The documents are the source: the script only lays them out, and stops
with a message if a section it needs is missing. The PDF needs WeasyPrint
(`pip install weasyprint`).

### Sechs master tool

`tools/sechs` is a command-line master for Sechs modules, on a Linux
I2C bus or through Werkzeug as a USB bridge; see
[tools/sechs/README.md](tools/sechs/README.md). Hardware testing:
[docs/hwtest.md](docs/hwtest.md).

## Downloads

Every push is built and tested on GitHub (`.github/workflows/build.yml`).
Each release (a tag such as `v1.0`) has the firmware (`ls10.bin`,
`werkzeug.uf2`, `blaustahl.uf2`), `sechsctl` for Linux, the reference sheet
and the getting-started guide attached. Builds of other commits are under
the workflow run's artifacts.

## Documentation

| Document | Contents |
|---|---|
| [docs/guide.md](docs/guide.md) | getting started with Werkzeug and an LS10A (`make guide` prints it) |
| [docs/basic1.md](docs/basic1.md) | Machdyne BASIC 1, the language |
| [docs/targets.md](docs/targets.md) | each machine: pins, storage, start-up, LS10's Sechs binding |
| [docs/sechs.md](docs/sechs.md) | Sechs, the six-pin module interface (specification) |
| [docs/fs.md](docs/fs.md) | the power-loss safe filesystem |
| [docs/ch32prog.md](docs/ch32prog.md) | programming LS10 modules from Werkzeug |
| [docs/hwtest.md](docs/hwtest.md) | hardware test plan |
| [docs/plan.md](docs/plan.md) | development plan and history |

## BASIC Language Reference

This is a guide. The exact definition of the language, Machdyne BASIC 1,
is [docs/basic1.md](docs/basic1.md); what each machine adds (pins,
storage, start-up) is in [docs/targets.md](docs/targets.md).

### Program Structure
Programs consist of numbered lines (1 to 32767). Keywords may be typed in
upper or lower case; `LIST` shows them in upper case. Variables are single
letters.
```basic
10 PRINT "HELLO"
20 LET A = 5
30 PRINT A
```

### Commands

#### LET
Assign a value to a variable:
```basic
LET A = 10
LET B = A + 5
```

#### PRINT
Output text or expressions:
```basic
PRINT "HELLO"
PRINT 42
PRINT A + B
```

#### INPUT
Read a value from the user:
```basic
INPUT A
INPUT "ENTER YOUR AGE: ", A
```

#### IF/THEN/ELSE
Conditional execution:
```basic
10 INPUT A
20 IF A > 10 THEN PRINT "BIG"
30 IF A < 5 THEN PRINT "SMALL" ELSE PRINT "MEDIUM"
```

#### GOTO
Jump to a line number:
```basic
10 LET A = 0
20 PRINT A
30 LET A = A + 1
40 IF A < 10 THEN GOTO 20
50 PRINT "DONE"
```

#### SLEEP
Sleep for a number of seconds (up to 32767, about 9 hours):
```basic
10 PRINT "SLEEPING"
20 SLEEP 3
30 PRINT "AWAKE"
```

#### WAIT
Wait for a number of milliseconds (up to 32767):
```basic
10 PINS NET,NET,PP,-
20 OUT 3, 1
30 WAIT 250
40 OUT 3, 0
```

Ctrl-C stops a program during `SLEEP` and `WAIT` too.

#### Several statements on a line
Separate them with `:`. After `THEN`, everything up to `ELSE` or the end of
the line is the THEN part:
```basic
10 IF A > 10 THEN PRINT "BIG": B = 1 ELSE PRINT "SMALL"
20 IF A = 0 THEN 100
```
A line number after `THEN` or `ELSE` means `GOTO`.

#### FOR and NEXT
```basic
10 FOR I = 1 TO 10 STEP 2: PRINT I: NEXT I
```
`STEP` is optional (1 by default) and may be negative. The loop body always
runs at least once. Loops can be nested 6 deep.

#### GOSUB and RETURN
```basic
10 GOSUB 100: PRINT "BACK": END
100 PRINT "IN THE SUBROUTINE": RETURN
```
`GOTO` and `GOSUB` take a line number, not an expression. Subroutines can
be nested 8 deep.

#### REM
A comment; the rest of the line is kept as typed.

#### PRINT
Items are separated by `;` (printed together) or `,` (next 14-column
zone). A `;` or `,` at the end keeps the cursor on the line:
```basic
10 PRINT "A = "; A; " B = "; B
20 PRINT 1, 2, 3
```

### Pins

The pins are numbered as on the Sechs/Zwölf connector: 1 (A), 2 (B),
3 (C) and 4 (D). A program declares how it uses them with `PINS`, which is
also the module's Sechs pin declaration. Without `PINS`, a program has
`PINS NET,NET,-,-`.

| Mode | Meaning | Pins |
|---|---|---|
| `-` | not used | any |
| `IN` | digital input | any |
| `OD` | open-drain output | any |
| `PP` | push-pull output | any |
| `AIN` | analog input | 3, 4 |
| `I2C` | I2C master (3 = SCL, 4 = SDA) | 3 and 4 together |
| `UART` | reserved | 3 and 4 together |
| `NET` | Sechs network (I2C slave) | 1 and 2 together |

```basic
10 PINS NET,NET,OD,AIN
20 OUT 3, 0              : REM pin 3 low
30 PRINT ADC(4)          : REM 0-1023 on every target
40 PRINT IN(3)           : REM 0 or 1
50 LED 1                 : REM the module's LED, if it has one
```

Using a pin in a way it was not declared stops the program with
`PIN NOT DECLARED`.

On LS10, pins 3 and 4 are also the UART console. Declaring them for
anything else turns the UART console off (without a message, to save
flash) until the next `RUN` or `NEW`; the I2C console is not affected.

#### I2C devices on pins 3 and 4
```basic
10 PINS NET,NET,I2C,I2C
20 I2C 72, 1, 96         : REM device 72: register 1 = 96
30 PRINT I2CR(72, 0)     : REM read register 0 of device 72
```
`I2C a, v` writes one byte; `I2C a, r, v` writes register `r`.
`I2CR(a)` reads one byte; `I2CR(a, r)` reads register `r`. Addresses are
7-bit. `I2CR` returns -1 if the device does not answer, so a program can
check for it; `I2C` stops with `I2C ERROR`.

### Program registers

`REG n, v` and `REG(n)` (n = 0-15) are program registers that a Sechs
master can read and write over I2C (registers 0x80-0x8F). They are not
cleared by `RUN`, so a master can set them before a program starts.

### Data files
```basic
10 OPEN "LOG.DAT" FOR APPEND AS #1
20 PRINT #1, T; ","; ADC(4)
30 CLOSE #1
```
```basic
10 OPEN "LOG.DAT" FOR INPUT AS #1
20 IF EOF(1) THEN 50
30 INPUT #1, A: PRINT A
40 GOTO 20
50 CLOSE #1
```
Modes are `INPUT`, `OUTPUT` (replace) and `APPEND`. One file (#1) can be
open at a time. `PRINT #` writes exactly what `PRINT` would show, so data
files are plain text. `INPUT #` reads the next number; numbers may be
separated by commas or new lines. `EOF(1)` is -1 at the end of the file.
A program's file is closed when the program stops.

On Linux, pins and I2C are simulated (outputs read back on `IN`, `ADC(3)`
is 300 and `ADC(4)` 400, and a 256-byte I2C memory answers at address 80).

### Sechs (modules)

On a Sechs module (LS10), pins 1 and 2 are an I2C slave that a master
uses to identify, control and program the module. See the
[Sechs specification](docs/sechs.md) and
[sechs/README.md](sechs/README.md).

- At power-on nothing is driven. For 600 ms a master can halt the
  module, and pressing Enter on the UART wakes the console (115200 baud;
  it answers with `///`). Otherwise `BOOT.BAS` runs. The console stays
  asleep (pin 4 not driven) until it is woken.
- The I2C console carries the same session as the UART, so every command,
  including `SAVE`, `LOAD`, `TYPE` and `DIR`, works over I2C too.
- A program that declares pins 1 and 2 as outputs stops with `ON A BUS` when
  the module is on an I2C bus.

### Operators

**Arithmetic**: `+`, `-`, `*`, `/`, `MOD`, unary `-`

**Comparison**: `<`, `>`, `<=`, `>=`, `<>` (not equal), `=` or `==` (equal).
A comparison is -1 when true and 0 when false.

**Logic**: `AND`, `OR`, `NOT` (bitwise, so they combine comparisons)

Precedence, highest first: unary `-`; `*` `/` `MOD`; `+` `-`;
comparisons; `NOT`; `AND`; `OR`.

**Parentheses**: `(` and `)` for grouping

### Immediate Commands

#### RUN
Execute the stored program:
```basic
> RUN
```

#### LIST
Display the stored program:
```basic
> LIST
10 PRINT "HELLO"
20 INPUT A
30 PRINT A
```

#### Delete a line
Type just the line number:
```basic
> 20
```

#### NEW
Clear the program and variables.

#### HELP
List the commands, then the statements and functions (keywords) that the
target understands:
```basic
> HELP
RUN LIST NEW SAVE LOAD DIR DEL FORMAT HELP
ADC AND APPEND AS CLOSE ELSE END EOF FOR GOSUB GOTO I2CR I2C IF INPUT IN ...
```
Hardware is reached through `PINS`, `OUT`, `IN`, `ADC`, `LED` and the I2C
statements; `PEEK` and `POKE` no longer exist.

#### Stop a program
Press Ctrl-C:
```basic
> RUN
BREAK IN 20
```

### Files

File names are 8.3 (up to 8 characters, a dot and up to 3 characters),
letters, digits, `_` and `-`, in any case. A name without an extension
gets `.BAS`, so `SAVE HELLO` saves `HELLO.BAS`.

Programs are saved as plain text, exactly as `LIST` shows them, so they
can be read and edited on any computer.

| Command | Meaning |
|---|---|
| `SAVE name` | save the program |
| `LOAD name` | replace the program with a saved one |
| `DIR` | list the files |
| `DEL name` | delete a file |
| `TYPE name` | print a file (for example a log) |
| `FORMAT YES` | erase all files (modules only); `YES` is required |

`BOOT.BAS` is loaded and run automatically when a module starts, unless a
key is pressed first.

On modules, saving never destroys the old version of a file until the new
one is complete, even if power is lost. See [docs/fs.md](docs/fs.md).

### Errors

Messages are kept short to save flash on small modules. Errors are
reported with the line number when a program is running:

```basic
> 10 PRINT 1 / 0
> RUN
DIV BY 0 IN 10
```

| Error | Cause |
|---|---|
| `SYNTAX ERROR` | the line or command is not valid BASIC |
| `TOO LONG` | the line has too many tokens or lists too long |
| `TOO BIG` | a number or line number above 32767 |
| `NO LINE` | `GOTO` or `GOSUB` to a line that does not exist |
| `DIV BY 0` | division or `MOD` by zero |
| `NO MEMORY` | the program does not fit |
| `BAD NAME` | not an 8.3 file name |
| `NOT FOUND` | no such file |
| `DISK FULL` / `DIR FULL` | no room for the file |
| `NOT FORMATTED` | the module's storage needs `FORMAT YES` |
| `NO FOR`, `NO GOSUB` | `NEXT` without `FOR`, `RETURN` without `GOSUB` |
| `TOO DEEP` | too many nested `FOR` loops or `GOSUB`s |
| `PIN NOT DECLARED` | the pin is not declared for this use in `PINS` |
| `BAD PINS` | a `PINS` declaration that is not allowed |
| `OUT OF RANGE` | a pin or register number |
| `BAD FILE #`, `NOT OPEN`, `FILE OPEN`, `END OF FILE` | data files |
| `I2C ERROR` | an I2C device did not answer |
| `ON A BUS` | pins 1 and 2 cannot be outputs: the module is on a Sechs bus |
| `DAMAGED`, `I/O ERROR` | storage problem |
| `NOT SUPPORTED` | the target cannot do this (for example `ADC` on Werkzeug) |
| `BREAK` | Ctrl-C |

## Example Programs

### Hello World
```basic
10 PRINT "HELLO WORLD"
```

### Count to 10
```basic
10 LET A = 1
20 PRINT A
30 LET A = A + 1
40 IF A <= 10 THEN GOTO 20
50 PRINT "DONE"
```

### Age Calculator
```basic
10 PRINT "WHAT IS YOUR BIRTH YEAR?"
20 INPUT Y
30 LET A = 2025 - Y
40 PRINT "YOU ARE "
50 PRINT A
60 PRINT " YEARS OLD"
```

## Technical Details

### Memory Layout
- **Program storage**: 1024 bytes (tokenized; 4096 on LS11)
- **Variables**: 26 signed 16-bit integers (A-Z)

### Token Format
Lines are tokenized when they are entered. Tokens are internal only: files
contain text. Token numbers are never changed; new tokens are added at the
end.
- **Single-byte tokens**: Keywords, operators
- **Multi-byte tokens**: 
  - Numbers: `TOK_NUM` + 2 bytes (little-endian)
  - Strings: `TOK_STR` + length + data
  - Variables: `TOK_VAR` + index (0-25)

### Line Format
Each program line:
```
[line# low] [line# high] [length] [tokens...] [TOK_EOL]
```

## Limitations

- Maximum 1024 bytes total program storage (tokenized; 4096 on LS11)
- 64 tokens per line
- 26 variables (A-Z only)
- 16-bit signed integers only (-32768 to 32767)
- No floating point
- No arrays
- No string variables (only string literals in PRINT)
- One data file open at a time

### LLM-generated code

To the extent that there is LLM-generated code in this repo, it should be space indented. Any space indented code should be carefully audited and then converted to tabs (eventually). 

## License

The contents of this repo are released under the [Lone Dynamics Open License](LICENSE.md).
