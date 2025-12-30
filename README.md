# Machdyne BASIC

A lightweight BASIC implementation for embedded systems.

## Features

- **Tokenized execution** - Programs are compiled to bytecode for efficient execution
- **26 variables** (A-Z)
- **Control flow** - IF/THEN/ELSE, GOTO
- **I/O** - PRINT, INPUT
- **Hardware access** - PEEK/POKE for access to hardware
- **Arithmetic** - Addition, subtraction, multiplication, division
- **Comparisons** - <, >, <=, >=, <>, ==

## Targets

  * Linux
  * [Werkzeug](https://github.com/machdyne/werkzeug)
  * [Zwölf LS10A](https://machdyne.com/product/zwolf-ls10/)

## Building

### Linux
```bash
$ make
$ ./basic
```

### LS10
```bash
$ cd targets/ls10
$ git clone https://github.com/cnlohr/ch32fun
$ make
```

### Werkzeug

You will need to have [pico-sdk](https://github.com/raspberrypi/pico-sdk) installed in your path.

```bash
$ cd targets/werkzeug
$ mkdir build
$ cd build
$ cmake -DPICO_BOARD=machdyne_werkzeug ..
$ make
```

## BASIC Language Reference

### Program Structure
Programs consist of numbered lines:
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

#### PEEK/POKE
Turn an LED on or off on LS10:
```basic
10 POKE 16, 128
20 INPUT "LED 0 or 1:", L
30 IF L == 1 THEN POKE(17, 128) ELSE POKE(17, 0)
```

Blink LEDs on Werkzeug:
```basic
10 POKE(18,255)
20 LET A = 0
30 IF A == 0 THEN LET A = 255 ELSE LET A = 0
40 POKE(23,A)
50 SLEEP 1
60 GOTO 30
```

#### SLEEP
Sleep for a number of seconds:
```basic
10 PRINT "SLEEPING"
20 SLEEP 3
30 PRINT "AWAKE"
```

### Operators

**Arithmetic**: `+`, `-`, `*`, `/`

**Comparison**: `<`, `>`, `<=`, `>=`, `<>` (not equal), `==` (equal)

**Parentheses**: `(` and `)` for grouping

### Immediate Commands

#### RUN
Execute the stored program:
```
> RUN
```

#### LIST
Display the stored program:
```
> LIST
10 PRINT "HELLO"
20 INPUT A
30 PRINT A
```

#### Delete a line
Type just the line number:
```
> 20
```

#### Save a program
```basic
> SAVE HELLO.BAS
```

#### Load a program
```basic
> LOAD HELLO.BAS
```

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
- **Program storage**: 1024 bytes
- **Variables**: 26 signed 16-bit integers (A-Z)
- **PEEK/POKE memory**: 256 bytes

### Token Format
Programs are tokenized into bytecode:
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

- Maximum 1024 bytes total program storage
- 26 variables (A-Z only)
- 16-bit signed integers only (-32768 to 32767)
- No floating point
- No arrays
- No string variables (only string literals in PRINT)
- No FOR/NEXT loops (use GOTO)
- No subroutines/GOSUB

### LLM-generated code

To the extent that there is LLM-generated code in this repo, it should be space indented. Any space indented code should be carefully audited and then converted to tabs (eventually). 

## License

The contents of this repo are released under the [Lone Dynamics Open License](LICENSE.md).

