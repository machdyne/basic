# Embedding Machdyne BASIC

Machdyne BASIC is one C file (`basic.c`, with `basic.h`) that runs on a
16 KB microcontroller and on a desktop alike. A system that wants BASIC
gives it a console and whatever hardware it has; the interpreter does the
rest. This document is for that system's programmer: how to embed the
interpreter, and how to add statements and functions of your own.

The language itself is [basic1.md](basic1.md). Each existing embedding is
described in [targets.md](targets.md).

## 1. The model: a line in, characters out

```
   the system                                   basic.c
   ----------                                   -------
   collects a typed line  --- basic_yield() --> runs it: a command, a
                                                program line, or RUN
   shows characters      <------ hw_putc() ---  everything BASIC prints
   says "stop!"          <------ hw_break() ---  asked at every program
                                                line, and every 100 ms of
                                                WAIT or SLEEP
```

- **`basic_yield(line)`** is the only way in. The system calls it with
  each line typed at its console (without the line end). The line is a
  command (`RUN`, `LIST`, `SAVE` ...) or a program line (`10 PRINT 1`); it
  is carried out before `basic_yield` returns. **`RUN` runs the program
  inside that call**, until it ends, stops on an error, is broken, or
  reaches `INPUT`.
- **`INPUT`** returns early: the program waits, with `? ` printed. The
  next line the system passes to `basic_yield` is the answer, and the
  program continues inside that call. The system does nothing special: it
  just goes on collecting lines.
- **`hw_putc(c)`** receives everything BASIC prints. A line ends with a
  carriage return and a line feed.
- **`hw_break()`** is asked at the start of every program line and at
  least every 100 ms while a program waits. It returns 1 to stop the
  program (`BREAK IN n`). **It is also the system's chance to stay alive
  while a program runs**: a system with windows, messages or a watchdog
  services them here, since a program may run for a long time.
- **The prompt and any banner are the system's.** BASIC prints none.
- **`basic_boot()`** loads `BOOT.BAS` (1 if there is one); the system then
  calls `basic_yield("RUN")` if it wants it run, typically at start-up.

There are no threads, no callbacks into the system other than the `hw_*`
functions, and no dynamic memory.

## 2. What the system provides

Every `hw_*` function below must exist. Hardware the system does not have
is answered with "not supported", and BASIC reports `NOT SUPPORTED` to the
program, as BASIC 1 requires.

| Function | Does | Without the hardware |
|---|---|---|
| `void hw_putc(char c)` | console output | (required) |
| `int hw_break(void)` | 1 if the user asked to stop (Ctrl-C, Esc) | return 0 |
| `void hw_delay_ms(uint16_t ms)` | wait `ms` (at most 1000) milliseconds | (required) |
| `int hw_pin_mode(pin, mode)` | set pin 1..`HW_PINS` to a `PM_*` mode (`PINS`, `PIN`); 0 if done | 0 for `PM_NONE`, `HW_ERR_UNSUPPORTED` for the rest |
| `void hw_pin_write(pin, level)` | `OUT` | (never called if `hw_pin_mode` refuses) |
| `uint8_t hw_pin_read(pin)` | `IN()` | " |
| `int16_t hw_adc(pin)` | `ADC()`: 0-1023 | -1 |
| `void hw_led(uint8_t on)` | `LED` | do nothing |
| `int hw_i2c(addr, w, wn, r, rn)` | `I2C`, `I2CR`: write then read, repeated start; 0 or -1 | -1 (pin 3/4 `I2C` mode is refused anyway) |
| files (below) | `SAVE`, `LOAD`, `DIR`, `DEL`, `TYPE`, data files | `HW_ERR_UNSUPPORTED` from each |
| `int hw_fformat(void)` | `FORMAT YES` | `HW_ERR_UNSUPPORTED` |

Pins are numbered from 1. Pins 1-4 are BASIC 1's (A, B, C and D on a Sechs
module); a system with more defines `HW_PINS` and its programs use the
`PIN` statement for pins 5 and up. `RUN` sets every pin to `PM_NONE`
(unused) first. A system without pins accepts `PM_NONE`, which needs
nothing done, and refuses every other mode: `PINS -, -, -, -` then works,
and any real use of a pin reports `NOT SUPPORTED`.

### Files

Names reach the system already checked and in 8.3 form (`NAME.EXT`, upper
case), with no paths. One file is open at a time. Modes and return codes
are those of `fs/fs.h`.

- **Your own storage:** implement `hw_fopen(name, mode)` (`FS_READ`,
  `FS_WRITE`, `FS_APPEND`), `hw_fread`, `hw_fwrite`, `hw_fclose`,
  `hw_fabort` (close, discarding a file opened with `FS_WRITE`),
  `hw_fdelete` and `hw_fdir(callback)`, which calls `callback(name, NULL)`
  for each file. **Each returns `FS_OK` (0) or a negative `FS_ERR_*` code,
  except `hw_fread`, which returns the bytes read (0 at the end).** In
  particular `hw_fwrite` returns `FS_OK` once all the bytes are written,
  not a byte count: returning the count makes every `SAVE` fail. `FS_WRITE` should replace a file
  only when it is closed, so that a failed `SAVE` keeps the old one. The
  Linux build in `basic.c` (`TARGET_LINUX`) maps them to a directory.
- **Raw storage (F-RAM, EEPROM, NOR flash):** build with `HW_FILES_FS` and
  `fs/fs.c`, and provide only `fs_media_read` and `fs_media_prog`
  ([fs.md](fs.md)). The filesystem survives a power cut at any moment.

Keeping a program's files in one place (a directory, a volume) of their
own is a good idea on a system that has other files: BASIC has no paths,
so a program cannot reach anything else.

## 3. Build options

| Define | Default | Meaning |
|---|---|---|
| `HW_PINS` | 4 | pins a program can use; above 4 adds the `PIN` statement |
| `MAX_PROG` | 1024 | bytes of tokenized program (BASIC 1 promises at least 1024; at most 65535) |
| `HW_FILES_FS` | off | files on the module filesystem (`fs/fs.c`) |
| `BASIC_EXT` | off | extensions (section 4) |
| `HW_PROG_MODE` | off | the `BOOT` command, calling `hw_prog_mode()` |
| `NO_HELP` | off | leaves out `HELP` (to save space) |
| `TARGET_LINUX` | off | the Linux build, with its own `main` (in `basic.c`) |

**Size.** Compiled for RV32IM with `-Os`, the interpreter is about 14.5 KB
of code with extensions on. Its RAM is `MAX_PROG` plus about 330 bytes,
plus the stack: the deepest paths (an expression inside a `FOR` inside a
`GOSUB`) need a few hundred bytes. On a 2 KB microcontroller that margin
had to be measured (targets.md, LS10); elsewhere it does not matter.

**State a system may read:** `basic_running` (1 while a program runs),
`basic_input` (1 while a program waits at `INPUT`: a system with a prompt
of its own shows it only when this is 0),
`basic_prog_err` (what stopped the last program), `basic_cmd_err` (the last
command's error), and `basic_regs[16]`, the program registers `REG 0-15`,
which BASIC 1 calls "shared with the environment": on a Sechs module they
are the registers a master reads. The system may read and write them at
any time.

## 4. Extensions

A system can add statements and functions to the language: graphics,
sound, its own hardware. Build with `BASIC_EXT` and provide a table:

```c
#include "basic.h"

static int16_t plot(uint8_t argc, const int16_t *argv, uint8_t *error) {
    if (argv[0] < 0 || argv[0] > 319 || argv[1] < 0 || argv[1] > 239)
        *error = BASIC_E_RANGE;            /* OUT OF RANGE IN n */
    else
        screen_set(argv[0], argv[1]);      /* the system's own drawing */
    return 0;
}

static int16_t point(uint8_t argc, const int16_t *argv, uint8_t *error) {
    return screen_get(argv[0], argv[1]);   /* a function's value */
}

const basic_ext_t basic_ext[] = {
    /* name     function  min max  run */
    { "PLOT",   0,        2,  2,   plot  },    /* PLOT x, y      */
    { "POINT",  1,        2,  2,   point },    /* POINT(x, y)    */
};
const uint8_t basic_ext_count = sizeof(basic_ext) / sizeof(basic_ext[0]);
```

The interpreter treats each one like its own keywords:

- **Names** are two or more upper-case letters or digits; programs may
  type them in any case. Keywords are matched by prefix, because spaces
  are optional in BASIC (`FORI=1TO10`); **the longest match wins**, so a
  name may begin with a built-in keyword (`ORBIT` is not `OR`+`BIT`).
- **Arguments** are numeric expressions, parsed by the interpreter:
  - a statement takes them comma-separated: `PLOT 10, 20`, or none;
  - a function with arguments takes them in parentheses: `POINT(10, 20)`;
  - a function without arguments (`max` 0) takes no parentheses: `KEY`.

  At most `BASIC_EXT_ARGS` (6). A different number than `min`..`max` is a
  `SYNTAX ERROR`, before `run` is called.
- **`run`** gets the values. A function returns its value; a statement's
  return value is ignored. To report a problem, set `*error` to a
  `BASIC_E_*` code: the line stops with that message, as for BASIC's own
  errors (`OUT OF RANGE IN 40`). A long-running extension should call
  `hw_break()` itself and report `BASIC_E_BREAK`.
- **`LIST`, `SAVE` and `LOAD`** keep extensions as their names, in
  canonical form. **`HELP`** lists them on a line of their own.
- **Tokens:** extensions are stored as one byte each, 0xC0 plus their
  index, so up to 64 of them. **Never reorder the table** of a system
  whose programs are kept in tokenized form; programs saved as text (all
  of BASIC's files) do not depend on it.

Rules for a system that extends the language:

1. **Add, never change.** Everything in [basic1.md](basic1.md) behaves as
   specified, so a BASIC 1 program runs the same everywhere, and programs
   written for one system's extensions stay valid BASIC 1 where they do
   not use them.
2. **Document them** with the system ([targets.md](targets.md) or the
   system's own documentation): they are not BASIC 1.
3. **Test them** the way `test/ext_table.c` is tested: build the
   interpreter with the table and run programs through it
   (`testsuite.sh`, section "Extensions", `BASIC_BIN=./basic_ext`).

## 5. Existing embeddings

| System | Where | Notes |
|---|---|---|
| Linux | `basic.c` (`TARGET_LINUX`) | files in the current directory; the reference for the tests |
| LS10, LS11 (Sechs modules) | `targets/ls1x` | the module filesystem, Sechs consoles and registers |
| Werkzeug, Blaustahl | `targets/werkzeug`, `targets/blaustahl` | 24 pins (`PIN`), files in flash |
| Zeitlos (planned) | Zeitlos's `sw/apps/basic` | a 320x240 BASIC computer, with graphics extensions |

## 6. A Sechs master

A system that should talk to Sechs modules (rather than be one) embeds
`tools/sechs/sechsm.c` instead: the master side of the protocol, with no
operating system or allocation. It reaches the bus through five functions
the system provides (`sm_bus` in `sechsm.h`: write, register read, a short
pause, a millisecond clock and console output), and offers scan, info,
registers, address changes, HALT/RUN/RESET/PROGRAM and the I2C console.
`tools/sechs/sechs.c` (sechsctl) is its user on Linux.
