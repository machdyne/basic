# Machdyne BASIC

A lightweight BASIC interpreter for embedded systems.

## Features

- **Tokenized execution** - Programs are compiled to bytecode for efficient execution
- **26 variables** (A-Z)
- **Control flow** - IF/THEN/ELSE, GOTO
- **I/O** - PRINT, INPUT
- **Memory access** - PEEK/POKE for direct memory manipulation
- **Arithmetic** - Addition, subtraction, multiplication, division
- **Comparisons** - <, >, <=, >=, <>, ==

## Targets

Linux and [Zwölf LS10A](https://github.com/machdyne/zwolf).

## Building

### For LS10
```bash
$ cd targets/ls10
$ make
```

### For Linux
```bash
make
./basic
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

#### PEEK/POKE (not yet implemented)
Direct memory access:
```basic
10 POKE 100, 42
20 LET A = PEEK(100)
30 PRINT A
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

## License

The contents of this repo are released under the [Lone Dynamics Open License](LICENSE.md).

