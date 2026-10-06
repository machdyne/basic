/*
 * Machdyne BASIC
 *
 * Interface between the interpreter (basic.c) and a target. Each target
 * implements the hw_* functions and calls basic_yield() with every line
 * typed at the console.
 */

#ifndef BASIC_H
#define BASIC_H

#include <stdint.h>
#include "fs/fs.h"      /* file modes and status codes */

/* status for targets without file storage */
#define HW_ERR_UNSUPPORTED -20
/* hw_pin_mode: A/B cannot be driven, the module is on a Sechs bus */
#define HW_ERR_BUS -21

/* ---- implemented by basic.c --------------------------------------------- */

void basic_yield(uint8_t *line);   /* one line typed at the console */
int basic_boot(void);              /* load BOOT.BAS; 1 if loaded */

/* state for Sechs (read by the target) */
extern uint8_t basic_regs[16];     /* REG 0-15: Sechs registers 0x80-0x8F */
extern uint8_t basic_running;      /* 1 while a program runs */
extern uint8_t basic_input;        /* 1 while a program waits at INPUT: the
                                      next line is its answer */
extern uint8_t basic_prog_err;     /* error that stopped the last program */
extern uint8_t basic_cmd_err;      /* error of the last command, 0 if none */
extern const char basic_pin_modes[];   /* "- IN OD PP AIN I2C UART NET " */
#define BASIC_E_BREAK 15           /* basic_prog_err after Ctrl-C/HALT */
#define BASIC_E_BUS   27           /* basic_prog_err: drove A/B on a bus */

/* ---- implemented by the target ------------------------------------------ */

/* console */
void hw_putc(char c);
int hw_break(void);                /* 1 if the user pressed Ctrl-C */

/* time */
void hw_delay_ms(uint16_t ms);     /* ms <= 1000 */


/* One line of text: LOAD reads into it. A target with little RAM may use
 * it as its console line buffer too (LS10 does): LOAD takes its file name
 * from the console line before it reads into the buffer. */
#define BASIC_LINE 128
extern char basic_line[BASIC_LINE];

/* ---- pins: Sechs pins 1-4 (A, B, C, D) and their modes (PINS) ---- */

/* Pins a program can use: 1-4 in BASIC 1; a target with more pins defines
 * HW_PINS and gets the PIN statement (an extension, not BASIC 1). */
#ifndef HW_PINS
#define HW_PINS 4
#endif

#define PM_NONE  0      /* not used: high impedance */
#define PM_IN    1      /* digital input */
#define PM_OD    2      /* open-drain output */
#define PM_PP    3      /* push-pull output */
#define PM_AIN   4      /* analog input */
#define PM_I2C   5      /* local I2C master (C = SCL, D = SDA) */
#define PM_UART  6      /* UART to a peripheral (C = RX, D = TX) */
#define PM_NET   7      /* Sechs I2C target (A and B) */

int hw_pin_mode(uint8_t pin, uint8_t mode);     /* HW_ERR_UNSUPPORTED */
void hw_pin_write(uint8_t pin, uint8_t level);
uint8_t hw_pin_read(uint8_t pin);
int16_t hw_adc(uint8_t pin);                    /* 0-1023, -1 if none */
void hw_led(uint8_t on);
#ifdef HW_PROG_MODE
void hw_prog_mode(void);    /* BOOT: restart in programming mode */
#endif

/* local I2C bus on C/D: write wn bytes, then read rn bytes (with a
 * repeated start). Returns 0, or -1 if the device does not answer. */
int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r, uint8_t rn);


/* files: names are already in 8.3 form ("NAME.EXT", upper case);
 * modes and return codes are those of fs.h; one file is open at a time */
#ifdef HW_FILES_FS
/* the target's files are the module filesystem (fs.c): no wrappers */
#define hw_fopen    fs_open
#define hw_fread    fs_read
#define hw_fwrite   fs_write
#define hw_fclose   fs_close
#define hw_fabort   fs_abort
#define hw_fdelete  fs_delete
#define hw_fdir(cb) fs_dir(cb, 0)
#else
/* Each returns FS_OK or a negative FS_ERR_* code, except hw_fread,
 * which returns the bytes read (0 at the end). hw_fwrite returns FS_OK
 * once all len bytes are written: not a byte count. */
int hw_fopen(const char *name, uint8_t mode);
int hw_fread(uint8_t *buf, uint16_t len);       /* bytes, 0 at end */
int hw_fwrite(const uint8_t *buf, uint16_t len);    /* FS_OK or error */
int hw_fclose(void);
void hw_fabort(void);                           /* discard FS_WRITE */
int hw_fdelete(const char *name);
int hw_fdir(fs_dir_cb cb);                      /* cb(name, NULL) per file */
#endif
int hw_fformat(void);

/* ---- Extensions (BASIC_EXT) ----
 *
 * Statements and functions a target adds to BASIC 1 (graphics on Zeitlos,
 * for example), documented by the target. The target provides basic_ext[]
 * and basic_ext_count.
 *
 * A name is two or more upper-case letters or digits. Keywords match by
 * prefix (spaces are optional in BASIC), and the longest match wins, so a
 * name may begin with a built-in keyword. The interpreter parses the
 * arguments, all numeric expressions:
 *   a statement:  NAME            NAME a, b, ...
 *   a function:   NAME            if it takes none (max_args 0)
 *                 NAME(a, b, ...) otherwise (min_args at least 1)
 * A wrong number of arguments is a SYNTAX ERROR. run() gets them in argv;
 * a function returns its value. On a problem run() sets *error to one of
 * the BASIC_E_* codes, and the line stops with that message. */
#ifdef BASIC_EXT
#define BASIC_EXT_ARGS  6   /* the most arguments an extension can take */

typedef struct {
    const char *name;
    uint8_t function;               /* 1: a function, used in expressions */
    uint8_t min_args, max_args;
    int16_t (*run)(uint8_t argc, const int16_t *argv, uint8_t *error);
} basic_ext_t;

extern const basic_ext_t basic_ext[];
extern const uint8_t basic_ext_count;   /* at most 64 */
#endif

/* errors an extension may report (the same messages as BASIC 1's) */
#define BASIC_E_SYNTAX       1   /* SYNTAX ERROR */
#define BASIC_E_IO          11   /* I/O ERROR */
#define BASIC_E_UNSUPPORTED 14   /* NOT SUPPORTED */
/*      BASIC_E_BREAK       15      BREAK (above) */
#define BASIC_E_RANGE       20   /* OUT OF RANGE */

#endif /* BASIC_H */
