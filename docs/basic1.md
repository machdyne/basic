# Machdyne BASIC 1

**Language reference. Status: frozen for implementation 1 (2026-10-02).**

This document defines the Machdyne BASIC language. A program written to it
means the same thing wherever it runs. What each machine provides (its pins,
its storage, how it starts) is described with that machine, not here; see
the targets document of the implementation.
The [README](../README.md) is the friendlier guide. Where they disagree,
this document is right and the README is wrong.

The behaviour described here is checked by the test suite (`make test`).

## 1. Programs and lines

A program is a set of numbered lines. A line is a line number followed by
one or more statements separated by `:`.

```basic
10 PRINT "HELLO": GOTO 10
```

- **Line numbers** are 1 to 32767. Lines run in ascending order.
- **Entering** a line with a new number adds it; with an existing number,
  replaces it. A line number alone deletes that line.
- **Text without a line number** is a command (section 9). There is no
  immediate mode: `PRINT 1` without a line number is a `SYNTAX ERROR`.
- **Case:** keywords and variable names may be typed in any case and are
  stored in upper case. Text in quotes keeps its case.
- **Spaces** between elements are optional; `LIST` shows every line in one
  canonical spacing.
- **`REM`** makes the rest of the line a comment, including any `:`.
- A line must fit in 64 tokens (about 64 keywords, numbers, variables and
  operators; `TOO LONG`). `LOAD` reads lines of up to 127 characters, the
  longest line the console accepts.

Programs are stored and transferred as text: the form `LIST` shows.

## 2. Values

The only type is the **16-bit signed integer**, -32768 to 32767.

- **Literals** are decimal, 0 to 32767. There are no hexadecimal, floating
  point or string values. `-5` is unary minus applied to 5, so the literal
  -32768 cannot be written (`-32767 - 1` is -32768).
- **Arithmetic wraps** around 16 bits: `32767 + 1` is -32768, `300 * 300`
  is 24464, `-(-32768)` is -32768.
- **Truth:** a condition is true if it is not 0. Comparisons give -1 (true)
  or 0 (false), so the bitwise `AND`, `OR` and `NOT` combine them.
- **Strings** exist only as quoted text in `PRINT`, `INPUT` prompts and
  file names. A string anywhere else is a `SYNTAX ERROR` when the line runs.

## 3. Variables

There are 26 variables, `A` to `Z`, each one value. They start at 0 and
`RUN` sets them all to 0. `LET` is optional: `LET A = 1` and `A = 1` are the
same.

Program registers (`REG`, section 7) are separate from variables and are not
cleared by `RUN`.

## 4. Expressions

| Precedence (highest first) | Operators |
|---|---|
| 1 | unary `-` |
| 2 | `*` `/` `MOD` |
| 3 | `+` `-` |
| 4 | `=` `==` `<>` `<` `>` `<=` `>=` |
| 5 | `NOT` |
| 6 | `AND` |
| 7 | `OR` |

Operators of the same precedence associate to the left. Parentheses group.

- **`/`** divides and truncates toward zero: `-7 / 2` is -3.
- **`MOD`** is the remainder, with the sign of the left operand: `-7 MOD 2`
  is -1, `7 MOD -2` is 1. `/` and `MOD` by zero stop the program with
  `DIV BY 0`. `-32768 / -1` is -32768 and `-32768 MOD -1` is 0.
- **Comparisons** do not chain: `3 > 2 > 1` is a `SYNTAX ERROR`; write
  `(3 > 2) > 1` if that is really meant. `=` and `==` are the same.
- **`NOT`** binds more loosely than comparisons: `NOT A = B` is
  `NOT (A = B)`.
- **`AND`, `OR`, `NOT`** are bitwise on the 16-bit value: `NOT 0` is -1,
  `NOT -1` is 0, `6 AND 3` is 2.

**Functions** (each takes its arguments in parentheses):

| Function | Value |
|---|---|
| `IN(p)` | pin `p` read as 0 or 1 (section 8) |
| `ADC(p)` | analog input on pin `p`, 0 to 1023 |
| `I2CR(a)`, `I2CR(a, r)` | one byte (0-255) from I2C device `a`, or from its register `r`; -1 if the device does not answer |
| `REG(n)` | program register `n` (0-15), 0-255 |
| `EOF(1)` | -1 at the end of the open input file, else 0 |

## 5. Statements

| Statement | Does |
|---|---|
| `LET v = e`, `v = e` | assign |
| `PRINT items` | print values and text; `;` joins, `,` moves to the next zone |
| `INPUT v`, `INPUT "prompt"; v` | read a number from the console |
| `IF e THEN ... ELSE ...` | run the rest of the line if `e` is not 0 |
| `GOTO n` | continue at line `n` |
| `GOSUB n`, `RETURN` | call and return from a subroutine |
| `FOR v = a TO b STEP s`, `NEXT v` | loop; the body runs at least once |
| `END` | stop the program |
| `REM text` | comment |
| `SLEEP e`, `WAIT e` | wait seconds, wait milliseconds |
| `PINS m,m,m,m` | declare the use of pins 1-4 |
| `OUT p, e` | set an output pin |
| `LED e` | the LED on or off |
| `I2C a, v`, `I2C a, r, v` | write to an I2C device |
| `REG n, e` | set a program register |
| `OPEN "f" FOR m AS #1` | open a data file (`INPUT`, `OUTPUT`, `APPEND`) |
| `PRINT #1, items`, `INPUT #1, v` | write, read a data file |
| `CLOSE #1` | close the data file |

In the forms below, `e` is any expression, `n` a literal line number, `v`
a variable and `"..."` quoted text.

**`LET v = e`**, **`v = e`**: assignment.

**`PRINT`** items separated by `;` or `,`:

- An item is an expression or quoted text. Numbers are printed with no
  added spaces (`PRINT -5; 3` shows `-53`).
- `;` prints the next item directly after; `,` moves to the next print
  zone (zones are 14 columns wide). Two items with no separator act as `;`.
- A trailing `;` or `,` keeps the cursor on the line; otherwise `PRINT`
  ends the line. `PRINT` alone prints an empty line.

**`INPUT v`**, **`INPUT "prompt"; v`**: prints the prompt (if any) and `? `,
and reads a line. The value is an optional sign and the digits that follow;
anything after them is ignored, and a line with no digits gives 0. Values
outside -32768 to 32767 wrap as arithmetic does.

**`IF e THEN` statements [`ELSE` statements]**: if `e` is true, the
statements after `THEN` run, up to `ELSE` or the end of the line;
otherwise the statements after `ELSE` run, if any. `THEN n` and `ELSE n`
are short for `GOTO n`. An `IF` takes the rest of its line: statements
after `ELSE` belong to the `ELSE`.

**`GOTO n`**: continue at line `n`. The target is a literal line number;
there are no computed jumps. A missing line stops the program with
`NO LINE`.

**`GOSUB n`** / **`RETURN`**: call the subroutine at line `n`; `RETURN`
continues after the `GOSUB`. At most 8 calls can be active (`TOO DEEP`).
`RETURN` without a call is `NO GOSUB`.

**`FOR v = e1 TO e2 [STEP e3]`** ... **`NEXT [v]`**:

- `v` is set to `e1`; `e2` and `e3` (default 1) are evaluated once.
- The body **always runs at least once**. At `NEXT`, `v` is increased by
  the step; if it has not passed `e2` (above it for a positive step, below
  it for a negative one), the body runs again. After the loop, `v` holds
  the last value used: `FOR I = 1 TO 3 ... NEXT` leaves `I` at 3.
- `STEP 0` loops until stopped.
- `NEXT v` must name the innermost loop's variable (`NO FOR` otherwise);
  `NEXT` alone continues the innermost loop.
- A `FOR` with the variable of a loop that is already active replaces that
  loop and any loops inside it. At most 6 loops can be active (`TOO DEEP`).

**`END`**: stop the program.

**`REM` text**: a comment (section 1).

**`SLEEP e`**: wait `e` seconds. **`WAIT e`**: wait `e` milliseconds.
Negative values do not wait. Ctrl-C (or a stop request from outside the
program) stops a program while it waits.

**Pins** (section 8): **`PINS m1,m2,m3,m4`**, **`OUT p, e`** (low if `e`
is 0, high otherwise), **`LED e`** (on if `e` is not 0; ignored where there
is no LED).

**I2C** on pins 3 and 4: **`I2C a, v`** writes byte `v` to device `a`;
**`I2C a, r, v`** writes `v` to its register `r`. A device that does not
answer stops the program with `I2C ERROR` (use `I2CR` to check first).
Addresses are 7-bit.

**`REG n, e`**: set program register `n` (0-15) to `e` modulo 256.

**Data files** (section 10): **`OPEN "name" FOR INPUT|OUTPUT|APPEND AS #1`**,
**`PRINT #1,`** items, **`INPUT #1, v`**, **`CLOSE #1`**. The `#` is
optional in `OPEN` and `CLOSE`.

## 6. Running

`RUN` sets the variables to 0, closes any open file, takes the default pin
declaration (`PINS NET,NET,-,-`) and starts at the lowest line. A program
ends after its last line, at `END`, on an error, or when stopped (Ctrl-C:
`BREAK`).

Errors in a line's form (bad tokens, numbers above 32767, a bad `PINS`) are
reported when the line is entered. Everything else is reported when the line
runs, as `message IN line`.

## 7. Program registers

`REG` 0-15 hold one byte each. They are shared with whatever the program
runs inside, which can read and write them while the program runs, so they
are how a program exchanges values with the outside. They keep their values
across `RUN` and `NEW`, so they can be set before a program starts. They
are 0 at start-up.

## 8. Pins

A program has four pins, numbered 1 to 4. It declares their use with
`PINS`; the declaration in effect when a
pin is used decides what is allowed. `PINS` may appear more than once; each
replaces the previous declaration.

| Mode | Meaning | Allowed on |
|---|---|---|
| `-` | not used | any pin |
| `IN` | digital input | any pin |
| `OD` | open-drain output | any pin |
| `PP` | push-pull output | any pin |
| `AIN` | analog input | 3, 4 |
| `I2C` | I2C controller (3 = SCL, 4 = SDA) | 3 and 4 together |
| `UART` | reserved for a later version | 3 and 4 together |
| `NET` | left to the system (for example a bus it is connected by) | 1 and 2 together |

- A declaration that breaks these rules is `BAD PINS` when entered.
- `OUT` needs `OD` or `PP`; `IN` works on `IN`, `OD`, `PP` and `NET` pins;
  `ADC` needs `AIN`; `I2C` and `I2CR` need `I2C,I2C` on pins 3 and 4.
  Anything else is `PIN NOT DECLARED`. A pin number other than 1-4 is
  `OUT OF RANGE`.
- A use the system cannot allow at the moment (for example an output on
  pins that carry a bus) stops with `ON A BUS`.

## 9. Commands

| Command | Action |
|---|---|
| `RUN` | run the program (section 6) |
| `LIST` | list the program |
| `NEW` | delete the program |
| `SAVE name` | save the program as text |
| `LOAD name` | replace the program with a saved one |
| `TYPE name` | print any file |
| `DIR` | list the files |
| `DEL name` | delete a file |
| `FORMAT YES` | erase all files (`FORMAT` alone is a `SYNTAX ERROR`) |
| `HELP` | list the commands and keywords |
| Ctrl-C | stop a running program |

Anything after `RUN`, `LIST`, `NEW` or `HELP` is ignored.

**File names** are 1 to 8 letters, digits, `_` or `-`, optionally followed
by a dot and 1 to 3 more. They may be typed in any case and are stored in
upper case. A name without an extension gets `.BAS`, wherever a name is
used (commands and `OPEN`). Anything else is `BAD NAME`. A system that starts a program by
itself runs `BOOT.BAS`.

## 10. Data files

One file, number 1, can be open at a time (`BAD FILE #` for another number,
`FILE OPEN` if one is already open).

- `OUTPUT` creates or replaces the file; the old file stays until `CLOSE`.
  `APPEND` adds to the end, creating the file if needed; what is appended
  is kept as it is written. `INPUT` reads (`NOT FOUND` if there is no such
  file).
- `PRINT #1,` writes exactly what `PRINT` would show, so data files are
  plain text.
- `INPUT #1, v` reads the next number. Spaces, commas and line ends
  before it are skipped; the number is an optional `-` and digits, and
  must be in range. Anything else in its place (text, a number out of
  range) is a `SYNTAX ERROR`: unlike `INPUT` from the console, data files
  are read strictly. Reading past the last number is `END OF FILE`;
  `EOF(1)` tells in advance.
- The open file is closed when the program stops for any reason.

## 11. Conformance

An implementation provides everything in this document. Where it lacks the
hardware for something (pins, an analog input, file storage), using it
stops with `NOT SUPPORTED`; `LED` is ignored where there is no LED.

An implementation may offer more (more pins, more statements) as documented
extensions. They are not part of BASIC 1, and a program that uses them runs
only where they exist.

## 12. Limits

| Limit | At least |
|---|---|
| Program memory | 1,024 bytes (tokenized) |
| Tokens per line | 64 |
| Line length read by `LOAD` | 127 characters (as typed at the console) |
| Active `FOR` loops | 6 |
| Active `GOSUB`s | 8 |
| Variables | 26 (`A`-`Z`) |
| Program registers | 16 |
| Open files | 1 |

An implementation may provide more, but not less.

## 13. Errors

| Message | Cause |
|---|---|
| `SYNTAX ERROR` | the line or command is not valid BASIC |
| `TOO LONG` | too many tokens, or the listed line is too long |
| `TOO BIG` | a number or line number above 32767 |
| `NO LINE` | `GOTO`/`GOSUB`/`THEN`/`ELSE` to a missing line |
| `DIV BY 0` | `/` or `MOD` by zero |
| `NO MEMORY` | the program does not fit |
| `NO FOR`, `NO GOSUB` | `NEXT` without its `FOR`, `RETURN` without `GOSUB` |
| `TOO DEEP` | more than 6 loops or 8 calls active |
| `PIN NOT DECLARED`, `BAD PINS`, `ON A BUS` | pins (section 8) |
| `OUT OF RANGE` | a pin or register number |
| `I2C ERROR` | an I2C device did not answer `I2C` |
| `BAD NAME`, `NOT FOUND`, `DISK FULL`, `DIR FULL` | files |
| `BAD FILE #`, `FILE OPEN`, `NOT OPEN`, `END OF FILE` | data files |
| `NOT FORMATTED`, `DAMAGED`, `I/O ERROR` | storage |
| `NOT SUPPORTED` | the hardware for it is not there |
| `BREAK` | stopped by Ctrl-C |

## 14. Reserved words

`ADC AND APPEND AS CLOSE ELSE END EOF FOR GOSUB GOTO I2C I2CR IF IN INPUT
LED LET MOD NEXT NOT OPEN OR OUT OUTPUT PINS PRINT REG REM RETURN SLEEP
STEP THEN TO WAIT`, and the commands of section 9. `PEEK`, `POKE`,
`STORE` and `STORED` from earlier versions are not part of BASIC 1; lines
using them are rejected with `SYNTAX ERROR` when entered.

## 15. Later versions

Machdyne BASIC 1 is frozen. A later version may add statements, functions,
pin modes (such as `UART`) and immediate mode, and may raise limits, but
every BASIC 1 program must keep its meaning. Programs are stored as text,
so no stored program depends on internal token numbers.
