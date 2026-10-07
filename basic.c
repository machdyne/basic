/*
 * Machdyne BASIC
 * Copyright (c) 2025 Lone Dynamics Corporation. All rights reserved.
 *
 * The interpreter. Targets implement the hw_* functions in basic.h.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "basic.h"

#ifndef MAX_PROG
#define MAX_PROG  1024  /* bytes of program (a target may set more) */
#endif
#define MAX_TOK   64    /* tokens per line */
#define MAX_SRC   (BASIC_LINE - 1)  /* characters per line LOAD reads */
#define NUM_VARS  26
#define MAX_FOR   6     /* nested FOR loops */
#define MAX_GOSUB 8     /* nested GOSUBs */
#define NUM_REGS  16    /* program registers (Sechs 0x80-0x8F) */
#define ZONE      14    /* PRINT comma zone width */

/* Tokens. Append only: never renumber. */
enum {
    TOK_EOL = 0, TOK_LET, TOK_PRINT, TOK_GOTO, TOK_END, TOK_VAR, TOK_NUM,
    TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV, TOK_EQ, TOK_STR, TOK_IF,
    TOK_THEN, TOK_ELSE, TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_NE, TOK_EQEQ,
    TOK_INPUT, TOK_PEEK, TOK_POKE, TOK_SLEEP, TOK_LPAREN, TOK_RPAREN,
    TOK_COMMA, TOK_WAIT,
    /* Phase 2 */
    TOK_FOR, TOK_TO, TOK_STEP, TOK_NEXT, TOK_GOSUB, TOK_RETURN, TOK_REM,
    TOK_MOD, TOK_AND, TOK_OR, TOK_NOT, TOK_SEMI, TOK_COLON, TOK_HASH,
    TOK_OUT, TOK_IN, TOK_ADC, TOK_LED, TOK_PINS, TOK_I2C, TOK_I2CR,
    TOK_REG, TOK_STORE, TOK_STORED, TOK_OPEN, TOK_OUTPUT, TOK_APPEND,
    TOK_AS, TOK_CLOSE, TOK_EOF,
    TOK_PIN     /* extension: PIN n, mode (targets with more than 4 pins) */
    /* no longer used: TOK_PEEK, TOK_POKE, TOK_STORE, TOK_STORED */
};

/* extensions (BASIC_EXT, basic.h): tokens TOK_EXT + their index */
#define TOK_EXT 0xC0

/* Errors */
enum {
    E_NONE = 0, E_SYNTAX, E_LINE_TOO_LONG, E_NUMBER, E_UNDEF_LINE,
    E_DIV_ZERO, E_MEMORY, E_BAD_NAME, E_NOT_FOUND, E_DISK_FULL,
    E_DIR_FULL, E_IO, E_NOT_FORMATTED, E_DAMAGED, E_UNSUPPORTED, E_BREAK,
    E_NEXT, E_RETURN, E_NESTING, E_PIN, E_RANGE, E_FILE_NUM, E_FILE_OPEN,
    E_FILE_NOT_OPEN, E_EOF, E_I2C, E_BAD_PINS, E_BUS
};

/* the codes basic.h gives extensions must be these */
typedef char check_ext_errors[(E_SYNTAX == BASIC_E_SYNTAX && E_IO == BASIC_E_IO &&
    E_UNSUPPORTED == BASIC_E_UNSUPPORTED && E_BREAK == BASIC_E_BREAK &&
    E_RANGE == BASIC_E_RANGE && TOK_PIN < TOK_EXT) ? 1 : -1];

/* error messages in order, separated by NULs (no pointer table: flash
 * is scarce on small targets) */
static const char err_msg[] =
    "\0SYNTAX ERROR\0TOO LONG\0TOO BIG\0NO LINE\0DIV BY 0\0NO MEMORY\0"
    "BAD NAME\0NOT FOUND\0DISK FULL\0DIR FULL\0I/O ERROR\0NOT FORMATTED\0"
    "DAMAGED\0NOT SUPPORTED\0BREAK\0NO FOR\0NO GOSUB\0TOO DEEP\0"
    "PIN NOT DECLARED\0OUT OF RANGE\0BAD FILE #\0FILE OPEN\0NOT OPEN\0"
    "END OF FILE\0I2C ERROR\0BAD PINS\0ON A BUS";

/* Keywords in alphabetical order, except that a keyword comes before any
 * shorter keyword that is a prefix of it (I2CR, INPUT, OUTPUT), so
 * that the first match is the right one. HELP prints this list. */
static const char kw_names[] =
    "ADC AND APPEND AS CLOSE ELSE END EOF FOR GOSUB GOTO I2CR I2C IF "
    "INPUT IN LED LET MOD NEXT NOT OPEN OR OUTPUT OUT "
    "PINS "
#if HW_PINS > 4
    "PIN "      /* after PINS: words are matched by prefix */
#endif
    "PRINT REG REM RETURN SLEEP STEP THEN TO WAIT ";
static const uint8_t kw_toks[] = {
    TOK_ADC, TOK_AND, TOK_APPEND, TOK_AS, TOK_CLOSE, TOK_ELSE,
    TOK_END, TOK_EOF, TOK_FOR, TOK_GOSUB, TOK_GOTO, TOK_I2CR,
    TOK_I2C, TOK_IF, TOK_INPUT, TOK_IN, TOK_LED, TOK_LET, TOK_MOD,
    TOK_NEXT, TOK_NOT, TOK_OPEN, TOK_OR, TOK_OUTPUT, TOK_OUT,
    TOK_PINS,
#if HW_PINS > 4
    TOK_PIN,
#endif
    TOK_PRINT, TOK_REG, TOK_REM, TOK_RETURN, TOK_SLEEP, TOK_STEP,
    TOK_THEN, TOK_TO, TOK_WAIT
};

/* Commands, in the order of the switch in process_command() */
#ifdef HW_PROG_MODE    /* BOOT: restart in the module's programming mode */
static const char cmd_names[] = "RUN LIST NEW SAVE LOAD DIR DEL TYPE FORMAT HELP BOOT ";
#else
static const char cmd_names[] = "RUN LIST NEW SAVE LOAD DIR DEL TYPE FORMAT HELP ";
#endif

/* pin mode names for PINS, indexed by PM_* */
/* PINS mode names in PM_* order, separated by spaces */
const char basic_pin_modes[] = "- IN OD PP AIN I2C UART NET ";

static uint8_t program[MAX_PROG];
#ifdef BASIC_PROFILE
uint16_t basic_prog_max = MAX_PROG;
uint8_t basic_pins = HW_PINS;
#define PROG_MAX basic_prog_max
#define PINS_MAX basic_pins
#else
#define PROG_MAX MAX_PROG
#define PINS_MAX HW_PINS
#endif
static uint16_t prog_len;
static int16_t vars[NUM_VARS];

static uint8_t err;             /* current error, E_NONE if none */
static uint16_t cur_line;       /* line being run, 0 in immediate mode */

/* the running position: current line and position in it */
static uint8_t *line_p;
static uint8_t *ip;

/* FOR and GOSUB stacks; positions are offsets into program[] */
static struct {
    uint8_t var;
    int16_t limit, step;
    uint16_t line, pos;
} for_stack[MAX_FOR];
static uint8_t for_sp;

static struct {
    uint16_t line, pos;
} gosub_stack[MAX_GOSUB];
static uint8_t gosub_sp;

/* INPUT suspends the program until the next console line */
uint8_t basic_input;            /* a program waits at INPUT (basic.h) */
static uint8_t input_var;
static uint16_t resume_line, resume_pos;

static uint8_t pins[HW_PINS];   /* current PM_* mode of each pin */
uint8_t basic_regs[NUM_REGS];
uint8_t basic_running, basic_prog_err, basic_cmd_err;

/* the open data file (#1) */
static uint8_t file_mode;       /* 0, FS_READ, FS_WRITE or FS_APPEND */
static int16_t file_peek = -1;  /* look-ahead character, -1 if none */

static int16_t expr(void);
static void run(void);

/* ================= OUTPUT ================= */

/* Output goes to the console, to a file (SAVE, PRINT #), or is only
 * counted (to measure how long a listed line is). */
enum { OUT_CONSOLE, OUT_FILE, OUT_COUNT };
static uint8_t out_mode;
static uint16_t out_count;
static uint8_t out_failed;
static uint8_t col[2];          /* column on the console and in the file */

static void out_char(char c) {
    if (out_mode == OUT_COUNT) {
        out_count++;
        return;
    }
    if (out_mode == OUT_CONSOLE) {
        hw_putc(c);
    } else if (!out_failed && hw_fwrite((const uint8_t *)&c, 1)) {
        out_failed = 1;
    }
    col[out_mode] = (c == '\n' || c == '\r') ? 0 : col[out_mode] + 1;
}

static void out_str(const char *s) {
    while (*s) out_char(*s++);
}

static void out_unum(uint32_t u) {
    char buf[11];
    uint8_t i = 0;
    do {
        buf[i++] = '0' + u % 10;
        u /= 10;
    } while (u);
    while (i) out_char(buf[--i]);
}

static void out_num(int16_t v) {
    if (v < 0) out_char('-');
    out_unum(v < 0 ? (uint16_t)(-(int32_t)v) : (uint16_t)v);
}

static void out_nl(void) {
    if (out_mode == OUT_CONSOLE) out_char('\r');
    out_char('\n');
}

static void report(uint8_t e) {
    uint8_t m = out_mode;
    out_mode = OUT_CONSOLE;
    const char *msg = err_msg;
    if (col[OUT_CONSOLE]) out_nl();
    while (e--) msg += strlen(msg) + 1;
    out_str(msg);
    if (cur_line) {
        out_str(" IN ");
        out_unum(cur_line);
    }
    out_nl();
    out_mode = m;
}

/* map a file system status to an error */
static uint8_t fs_err(int r) {
    switch (r) {
        case FS_OK: return E_NONE;
        case FS_ERR_NOT_FOUND: return E_NOT_FOUND;
        case FS_ERR_FULL: return E_DISK_FULL;
        case FS_ERR_DIR_FULL: return E_DIR_FULL;
        case FS_ERR_INVALID: return E_BAD_NAME;
        case FS_ERR_CORRUPT: return E_DAMAGED;
        case FS_ERR_UNFORMATTED: return E_NOT_FORMATTED;
        case HW_ERR_UNSUPPORTED: return E_UNSUPPORTED;
        case HW_ERR_BUS: return E_BUS;
        default: return E_IO;
    }
}

/* ================= TEXT HELPERS ================= */

static char upper(char c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

static int is_alpha(char c) {
    c = upper(c);
    return c >= 'A' && c <= 'Z';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static char *skip_spaces(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int at_end(const char *s) {
    return !*s || *s == '\r' || *s == '\n';
}

/* Does s start with kw (any case)? */
static int starts_with(const char *s, const char *kw) {
    while (*kw) if (upper(*s++) != *kw++) return 0;
    return 1;
}

/* File name of len characters: 8.3, any case; ".BAS" if no extension.
 * out: 13 bytes. */
static uint8_t file_name(const char *s, uint8_t len, char *out) {
    uint8_t n = 0, dot = 0, ext = 0;
    while (len--) {
        char c = upper(*s++);
        if (c == '.') {
            if (dot || n == 0) return E_BAD_NAME;
            dot = 1;
        } else if (is_alpha(c) || is_digit(c) || c == '_' || c == '-') {
            if (dot ? ++ext > 3 : n >= 8) return E_BAD_NAME;
        } else {
            return E_BAD_NAME;
        }
        out[n++] = c;
    }
    if (n == 0) return E_BAD_NAME;
    if (!dot) {
        memcpy(out + n, ".BAS", 4);
        n += 4;
    }
    out[n] = 0;
    return E_NONE;
}

/* file name argument of a command, followed only by spaces */
static uint8_t arg_name(char *s, char *out) {
    uint8_t n = 0;
    s = skip_spaces(s);
    while (s[n] > ' ' && n < 13) n++;
    if (!at_end(skip_spaces(s + n))) return E_BAD_NAME;
    return file_name(s, n, out);
}

/* ================= TOKENIZER ================= */

/* two-character operators, in pairs, and their tokens (also used by LIST) */
static const char ops2[] = "<=>=<>==";
static const uint8_t op2_toks[] = { TOK_LE, TOK_GE, TOK_NE, TOK_EQEQ };

/* one-character operators and their tokens (also used by LIST) */
static const char ops[] = "+-*/=<>(),;:#";
static const uint8_t op_toks[] = {
    TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV, TOK_EQ, TOK_LT, TOK_GT,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_SEMI, TOK_COLON, TOK_HASH
};


/* Index of the first word in list (words separated by spaces) that s
 * starts with, in any case, with its length in *len; -1 if none. */
static int8_t find_word(const char *k, const char *s, uint8_t *len) {
    for (int8_t i = 0; *k; i++) {
        uint8_t n = 0;
        while (k[n] != ' ' && upper(s[n]) == k[n]) n++;
        if (k[n] == ' ') {
            *len = n;
            return i;
        }
        while (*k++ != ' ');
    }
    return -1;
}

/* Keyword at s: its token, with its length in *len; 0 if none. An
 * extension's name is taken if it matches more of s than a built-in
 * keyword does. */
static uint8_t find_keyword(const char *s, uint8_t *len) {
    int8_t i = find_word(kw_names, s, len);
    uint8_t t = i < 0 ? 0 : kw_toks[i];
#ifdef BASIC_EXT
    for (uint8_t e = 0; e < basic_ext_count; e++) {
        const char *n = basic_ext[e].name;
        uint8_t k = 0;
        while (n[k] && upper(s[k]) == n[k]) k++;
        if (!n[k] && (!t || k > *len)) {
            t = TOK_EXT + e;
            *len = k;
        }
    }
#endif
#if defined(BASIC_PROFILE) && HW_PINS > 4
    if (t == TOK_PIN && basic_pins <= 4) t = 0;     /* no PIN on this machine */
#endif
    return t;
}

/* Tokenize src into out (MAX_TOK bytes). Returns the length or 0 with
 * err set. */
static int tokenize(char *src, uint8_t *out) {
    uint8_t *p = out;
    uint8_t *end = out + MAX_TOK - 1;   /* room for TOK_EOL */
    uint8_t last = TOK_EOL;

#define EMIT(v) do { if (p >= end) { err = E_LINE_TOO_LONG; return 0; } \
                     *p++ = (v); } while (0)

    while (!at_end(src = skip_spaces(src))) {
        /* GOTO and GOSUB take a constant line number */
        if ((last == TOK_GOTO || last == TOK_GOSUB) && !is_digit(*src)) {
            err = E_SYNTAX;
            return 0;
        }
        if (*src == '"') {
            char *s = ++src;
            int len = 0;
            while (*src != '"' && !at_end(src)) {
                src++;
                len++;
            }
            if (*src != '"') {
                err = E_SYNTAX;
                return 0;
            }
            src++;
            if (p + 2 + len > end) {
                err = E_LINE_TOO_LONG;
                return 0;
            }
            *p++ = last = TOK_STR;
            *p++ = len;
            memcpy(p, s, len);
            p += len;
            continue;
        }
        if (is_digit(*src)) {
            int32_t v = 0;
            while (is_digit(*src)) {
                v = v * 10 + (*src++ - '0');
                if (v > 32767) {
                    err = E_NUMBER;
                    return 0;
                }
            }
            EMIT(TOK_NUM);
            EMIT(v & 0xff);
            EMIT(v >> 8);
            last = TOK_NUM;
            continue;
        }
        if (is_alpha(*src)) {
            uint8_t klen;
            uint8_t k = find_keyword(src, &klen);
            if (!k) {
                /* a variable is a single letter; a letter after it must
                 * start a keyword */
                char v = upper(*src++);
                if (is_alpha(*src) && !find_keyword(src, &klen)) {
                    err = E_SYNTAX;
                    return 0;
                }
                EMIT(TOK_VAR);
                EMIT(v - 'A');
                last = TOK_VAR;
                continue;
            }
            last = k;
            EMIT(last);
            src += klen;
            if (last == TOK_REM) {
                /* the rest of the line, kept as it is */
                src = skip_spaces(src);
                uint8_t len = 0;
                while (!at_end(src + len)) len++;
                if (p + 1 + len > end) {
                    err = E_LINE_TOO_LONG;
                    return 0;
                }
                *p++ = len;
                memcpy(p, src, len);
                p += len;
                break;
            }
            if (last == TOK_PINS) {
                /* four pin modes, stored as PM_* codes */
                uint8_t m[4];
                for (uint8_t i = 0; i < 4; i++) {
                    uint8_t l;
                    src = skip_spaces(src);
                    int8_t j = find_word(basic_pin_modes, src, &l);
                    if (j < 0 || is_alpha(src[l])) {
                        err = E_BAD_PINS;
                        return 0;
                    }
                    src = skip_spaces(src + l);
                    if (i < 3 && *src++ != ',') {
                        err = E_BAD_PINS;
                        return 0;
                    }
                    m[i] = j;
                }
                /* modes allowed on A/B and on C/D (bit = PM_*); NET on
                 * A and B together, I2C and UART on C and D together */
                if (!((0x8F >> m[0]) & (0x8F >> m[1]) & (0x7F >> m[2]) &
                      (0x7F >> m[3]) & 1) ||
                    (m[0] == PM_NET) != (m[1] == PM_NET) ||
                    (m[2] == PM_I2C) != (m[3] == PM_I2C) ||
                    (m[2] == PM_UART) != (m[3] == PM_UART)) {
                    err = E_BAD_PINS;
                    return 0;
                }
                for (uint8_t i = 0; i < 4; i++) EMIT(m[i]);
            }
#if HW_PINS > 4
            if (last == TOK_PIN) {
                /* extension: PIN n, mode for pins 5 and up; stored as the
                 * pin number and the PM_* code */
                uint8_t n = 0, l;
                src = skip_spaces(src);
                while (is_digit(*src) && n < 100) n = n * 10 + (*src++ - '0');
                src = skip_spaces(src);
                if (n < 5 || n > PINS_MAX) {
                    err = E_RANGE;
                    return 0;
                }
                if (*src++ != ',') {
                    err = E_BAD_PINS;
                    return 0;
                }
                src = skip_spaces(src);
                int8_t j = find_word(basic_pin_modes, src, &l);
                if (j < 0 || j > PM_AIN || is_alpha(src[l])) {
                    err = E_BAD_PINS;
                    return 0;
                }
                src += l;
                EMIT(n);
                EMIT(j);
            }
#endif
            continue;
        }

        char c = *src++;
        uint8_t t = 0;
        for (uint8_t i = 0; i < 4; i++)
            if (c == ops2[2 * i] && *src == ops2[2 * i + 1]) t = op2_toks[i];
        if (t) src++;
        else {
            uint8_t i = 0;
            while (ops[i] && ops[i] != c) i++;
            if (!ops[i]) {
                err = E_SYNTAX;
                return 0;
            }
            t = op_toks[i];
        }
        EMIT(t);
        last = t;
    }
    if (last == TOK_GOTO || last == TOK_GOSUB) {
        err = E_SYNTAX;
        return 0;
    }
    *p++ = TOK_EOL;
    return p - out;
#undef EMIT
}

/* ================= LIST ================= */

/* Print word n of a space-separated word list. */
static void out_word(const char *k, uint8_t n) {
    while (n--) while (*k++ != ' ');
    while (*k != ' ') out_char(*k++);
}

/* Print the keyword for tok; 0 if tok is not a keyword. */
static uint8_t out_keyword(uint8_t tok) {
#ifdef BASIC_EXT
    if (tok >= TOK_EXT && tok < TOK_EXT + basic_ext_count) {
        out_str(basic_ext[tok - TOK_EXT].name);
        return 1;
    }
#endif
    for (uint8_t i = 0; i < sizeof(kw_toks); i++) {
        if (kw_toks[i] == tok) {
            out_word(kw_names, i);
            return 1;
        }
    }
    return 0;
}


static int is_function(uint8_t t) {
#ifdef BASIC_EXT
    if (t >= TOK_EXT && t < TOK_EXT + basic_ext_count)
        return basic_ext[t - TOK_EXT].function && basic_ext[t - TOK_EXT].max_args;
#endif
    return t == TOK_IN || t == TOK_ADC || t == TOK_EOF ||
        t == TOK_I2CR || t == TOK_REG;
}

/* One line in its canonical form: tokens separated by single spaces,
 * except none after "(", "#", a unary minus or a function name before
 * "(", and none before ")", ",", ";" or ":". */
static void list_line(uint8_t *p) {
    uint8_t *q = p + 3;
    uint8_t space = 1;
    uint8_t prev = TOK_EOL;

    out_unum(p[0] | (p[1] << 8));
    while (*q != TOK_EOL) {
        uint8_t t = *q++;
        uint8_t unary = t == TOK_MINUS && prev != TOK_NUM &&
            prev != TOK_VAR && prev != TOK_RPAREN;
        if (t == TOK_LPAREN && is_function(prev)) space = 0;
        if (space && t != TOK_RPAREN && t != TOK_COMMA && t != TOK_SEMI &&
            t != TOK_COLON) out_char(' ');
        space = 1;
        prev = t;

        if (t == TOK_NUM) {
            out_num((int16_t)(q[0] | (q[1] << 8)));
            q += 2;
        } else if (t == TOK_VAR) {
            out_char('A' + *q++);
        } else if (t == TOK_STR) {
            uint8_t len = *q++;
            out_char('"');
            while (len--) out_char(*q++);
            out_char('"');
        } else if (out_keyword(t)) {
            if (t == TOK_REM) {
                uint8_t len = *q++;
                if (len) out_char(' ');
                while (len--) out_char(*q++);
            } else if (t == TOK_PINS) {
                for (uint8_t i = 0; i < 4; i++) {
                    out_char(i ? ',' : ' ');
                    out_word(basic_pin_modes, *q++);
                }
            }
#if HW_PINS > 4
            else if (t == TOK_PIN) {
                out_char(' ');
                out_unum(*q++);
                out_char(',');
                out_word(basic_pin_modes, *q++);
            }
#endif
        } else {
            for (uint8_t i = 0; i < sizeof(op_toks); i++)
                if (op_toks[i] == t) out_char(ops[i]);
            for (uint8_t i = 0; i < 4; i++) {
                if (op2_toks[i] == t) {
                    out_char(ops2[2 * i]);
                    out_char(ops2[2 * i + 1]);
                }
            }
            if (t == TOK_LPAREN || t == TOK_HASH || unary) space = 0;
        }
    }
    out_nl();
}

static void list_program(void) {
    uint8_t *p = program;
    while (p < program + prog_len && !out_failed) {
        list_line(p);
        p += 3 + p[2];
    }
}

/* ================= PROGRAM STORE ================= */

static uint8_t *find_line(uint16_t line) {
    uint8_t *p = program;
    while (p < program + prog_len) {
        if ((p[0] | (p[1] << 8)) == line) return p;
        p += 3 + p[2];
    }
    return NULL;
}

static void delete_line(uint16_t ln) {
    uint8_t *p = find_line(ln);
    if (!p) return;
    uint16_t total = 3 + p[2];
    memmove(p, p + total, (program + prog_len) - (p + total));
    prog_len -= total;
}

static void insert_line(uint16_t ln, uint8_t *buf, int len) {
    uint8_t *p = program;

    if (prog_len + 3 + len > PROG_MAX) {
        err = E_MEMORY;
        return;
    }
    while (p < program + prog_len && (p[0] | (p[1] << 8)) < ln)
        p += 3 + p[2];
    memmove(p + 3 + len, p, (program + prog_len) - p);
    p[0] = ln & 0xFF;
    p[1] = ln >> 8;
    p[2] = len;
    memcpy(p + 3, buf, len);
    prog_len += 3 + len;
}

/* A numbered line typed or loaded: replace, add or delete it. */
static void enter_line(char *line) {
    uint8_t tmp[3 + MAX_TOK];
    int32_t ln = 0;

    while (is_digit(*line)) {
        ln = ln * 10 + (*line++ - '0');
        if (ln > 32767) break;
    }
    if (ln < 1 || ln > 32767) {
        err = E_NUMBER;
        return;
    }
    line = skip_spaces(line);
    if (at_end(line)) {
        delete_line(ln);
        return;
    }
    int len = tokenize(line, tmp + 3);
    if (!len) return;


    delete_line(ln);
    insert_line(ln, tmp + 3, len);
}

/* ================= FILES (#1) ================= */

static void file_close(void) {
    if (!file_mode) return;
    file_mode = 0;
    file_peek = -1;
    if (!err) err = fs_err(hw_fclose());
    else hw_fclose();
}

/* "#n," in PRINT # and INPUT #: only file 1, open in the right mode */
static void file_number(uint8_t reading) {
    ip++;
    if (expr() != 1) {
        if (!err) err = E_FILE_NUM;
        return;
    }
    if (*ip++ != TOK_COMMA) err = E_SYNTAX;
    else if (!file_mode || (file_mode == FS_READ) != reading)
        err = E_FILE_NOT_OPEN;
}

/* next character of the file, -1 at the end */
static int16_t file_getc(void) {
    uint8_t c;
    if (file_peek >= 0) {
        int16_t r = file_peek;
        file_peek = -1;
        return r;
    }
    int r = hw_fread(&c, 1);
    if (r < 0) err = fs_err(r);
    return r == 1 ? c : -1;
}

static int16_t file_ungetc(int16_t c) {
    return file_peek = c;
}

static int file_eof(void) {
    return file_ungetc(file_getc()) < 0;
}

/* INPUT #: the next number, then one separator */
static int16_t file_number_value(void) {
    int16_t c;
    int32_t v = 0;
    uint8_t neg = 0, digits = 0;
    do {
        c = file_getc();
    } while (c == ' ' || c == '\r' || c == '\n' || c == ',');
    if (c < 0) {
        if (!err) err = E_EOF;
        return 0;
    }
    if (c == '-') {
        neg = 1;
        c = file_getc();
    }
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (v > 32768) v = 32768;
        digits++;
        c = file_getc();
    }
    while (c == ' ' || c == '\r') c = file_getc();
    if (c != ',' && c != '\n') file_ungetc(c);
    if (!digits || v > 32767 + neg) {
        err = E_SYNTAX;
        return 0;
    }
    return (int16_t)(neg ? -v : v);
}

/* ================= EXPRESSIONS ================= */

/* "(" expr { "," expr } ")" for functions; returns the number of
 * arguments, reading at most max into a */
static uint8_t args(int16_t *a, uint8_t max) {
    uint8_t n = 0;
    if (*ip++ != TOK_LPAREN) {
        err = E_SYNTAX;
        return 0;
    }
    for (;;) {
        int16_t v = expr();
        if (n < max) a[n] = v;
        n++;
        if (err || *ip != TOK_COMMA) break;
        ip++;
    }
    if (*ip++ != TOK_RPAREN) err = E_SYNTAX;
    return n;
}

static int8_t pin_arg(int16_t p) {
    if (p < 1 || p > PINS_MAX) {
        err = E_RANGE;
        return -1;
    }
    return p - 1;
}

static uint8_t i2c_ready(void) {
    if (pins[2] != PM_I2C) err = E_PIN;
    return !err;
}

#ifdef BASIC_EXT
static int stmt_end(void);

/* Call the extension of token t: parse its arguments (in parentheses for
 * a function that takes any, comma-separated for a statement), check
 * their number, run it. Returns a function's value. */
static int16_t ext_call(uint8_t t) {
    const basic_ext_t *x = &basic_ext[t - TOK_EXT];
    int16_t a[BASIC_EXT_ARGS];
    uint8_t n = 0, e = 0;
    if (x->function) {
        if (x->max_args) n = args(a, BASIC_EXT_ARGS);
    } else if (!stmt_end()) {
        for (;;) {
            int16_t v = expr();
            if (n < BASIC_EXT_ARGS) a[n] = v;
            n++;
            if (err || *ip != TOK_COMMA) break;
            ip++;
        }
    }
    if (err) return 0;
    if (n < x->min_args || n > x->max_args) {
        err = E_SYNTAX;
        return 0;
    }
    int16_t v = x->run(n, a, &e);
    if (e) err = e;
    return v;
}

#define IS_EXT(t, fn) ((t) >= TOK_EXT && (t) < TOK_EXT + basic_ext_count && \
                       basic_ext[(t) - TOK_EXT].function == (fn))
#endif

static int16_t factor(void) {
    int16_t v = 0, a[2];
    int8_t p = 0;
    uint8_t t = *ip++;

    /* functions with one argument: parse it first */
    if (t == TOK_IN || t == TOK_ADC || t == TOK_EOF || t == TOK_REG) {
        if (args(a, 1) != 1 && !err) err = E_SYNTAX;
        if (err) return 0;
        if (t == TOK_IN || t == TOK_ADC) {
            if ((p = pin_arg(a[0])) < 0) return 0;
        }
    }

    switch (t) {
        case TOK_NUM:
            v = ip[0] | (ip[1] << 8);
            ip += 2;
            break;
        case TOK_VAR:
            v = vars[*ip++];
            break;
        case TOK_MINUS:
            v = -factor();
            break;
        case TOK_LPAREN:
            v = expr();
            if (*ip++ != TOK_RPAREN) err = E_SYNTAX;
            break;
        case TOK_IN:
            if (pins[p] == PM_NONE || (pins[p] >= PM_AIN && pins[p] != PM_NET))
                err = E_PIN;
            else v = hw_pin_read(p + 1) ? 1 : 0;
            break;
        case TOK_ADC:
            if (pins[p] != PM_AIN) err = E_PIN;
            else if ((v = hw_adc(p + 1)) < 0) err = E_UNSUPPORTED;
            break;
        case TOK_EOF:
            if (a[0] != 1) err = E_FILE_NUM;
            else if (file_mode != FS_READ) err = E_FILE_NOT_OPEN;
            else v = file_eof() ? -1 : 0;
            break;
        case TOK_I2CR: {
            uint8_t n = args(a, 2), w, r;
            if ((n < 1 || n > 2) && !err) err = E_SYNTAX;
            if (err || !i2c_ready()) break;
            w = a[1];
            v = hw_i2c(a[0], &w, n - 1, &r, 1) ? -1 : r;
            break;
        }
        case TOK_REG:
            if (a[0] < 0 || a[0] >= NUM_REGS) err = E_RANGE;
            else v = basic_regs[a[0]];
            break;
        default:
#ifdef BASIC_EXT
            if (IS_EXT(t, 1)) {
                v = ext_call(t);
                break;
            }
#endif
            ip--;
            err = E_SYNTAX;
    }
    return v;
}

static int16_t term(void) {
    int16_t v = factor();
    while (*ip == TOK_MUL || *ip == TOK_DIV || *ip == TOK_MOD) {
        uint8_t op = *ip++;
        int16_t rhs = factor();
        if (op == TOK_MUL) v *= rhs;
        else if (rhs == 0) err = E_DIV_ZERO;
        else if (op == TOK_DIV) v /= rhs;
        else v %= rhs;
    }
    return v;
}

static int16_t sum(void) {
    int16_t v = term();
    while (*ip == TOK_PLUS || *ip == TOK_MINUS) {
        uint8_t op = *ip++;
        int16_t rhs = term();
        v = op == TOK_PLUS ? v + rhs : v - rhs;
    }
    return v;
}

/* one comparison; true is -1 */
static int16_t compare(void) {
    int16_t lhs = sum();
    uint8_t op = *ip;
    if (op != TOK_EQ && op != TOK_EQEQ && op != TOK_NE && op != TOK_LT &&
        op != TOK_GT && op != TOK_LE && op != TOK_GE) return lhs;
    ip++;
    int16_t rhs = sum();
    uint8_t r;
    switch (op) {
        case TOK_LT: r = lhs < rhs; break;
        case TOK_GT: r = lhs > rhs; break;
        case TOK_LE: r = lhs <= rhs; break;
        case TOK_GE: r = lhs >= rhs; break;
        case TOK_NE: r = lhs != rhs; break;
        default: r = lhs == rhs;
    }
    return r ? -1 : 0;
}

/* NOT is below comparisons: NOT A = B means NOT (A = B) */
static int16_t negation(void) {
    if (*ip != TOK_NOT) return compare();
    ip++;
    return ~negation();
}

static int16_t conjunction(void) {
    int16_t v = negation();
    while (*ip == TOK_AND) {
        ip++;
        v &= negation();
    }
    return v;
}

static int16_t expr(void) {
    int16_t v = conjunction();
    while (*ip == TOK_OR) {
        ip++;
        v |= conjunction();
    }
    return v;
}

/* ================= TIME ================= */

/* Wait in short steps so that Ctrl-C (and later Sechs HALT) is seen. */
static void wait_ms(int32_t ms) {
    while (ms > 0 && !err) {
        uint16_t n = ms > 100 ? 100 : (uint16_t)ms;
        hw_delay_ms(n);
        ms -= n;
        if (hw_break()) err = E_BREAK;
    }
}

/* ================= STATEMENTS ================= */

static uint8_t expect(uint8_t t) {
    if (*ip != t) {
        err = E_SYNTAX;
        return 0;
    }
    ip++;
    return 1;
}

/* end of a statement: end of line, ":" or ELSE */
static int stmt_end(void) {
    return *ip == TOK_EOL || *ip == TOK_COLON || *ip == TOK_ELSE;
}

/* Skip one token and its data. */
static void skip_token(uint8_t **q) {
    uint8_t t = *(*q)++;
    if (t == TOK_NUM) *q += 2;
    else if (t == TOK_VAR) *q += 1;
    else if (t == TOK_STR || t == TOK_REM) *q += 1 + **q;
    else if (t == TOK_PINS) *q += 4;
#if HW_PINS > 4
    else if (t == TOK_PIN) *q += 2;
#endif
}

static void jump(uint8_t *line, uint8_t *pos) {
    line_p = line;
    ip = pos ? pos : line + 3;
}

/* GOTO or GOSUB to the line number at ip */
static int go(uint8_t sub) {
    uint16_t ln = ip[1] | (ip[2] << 8);
    uint8_t *target = find_line(ln);
    ip += 3;
    if (!stmt_end()) err = E_SYNTAX;
    else if (!target) err = E_UNDEF_LINE;
    else if (sub && gosub_sp == MAX_GOSUB) err = E_NESTING;
    if (err) return -1;
    if (sub) {
        gosub_stack[gosub_sp].line = line_p - program;
        gosub_stack[gosub_sp++].pos = ip - program;
    }
    jump(target, NULL);
    return 1;
}

static void do_print(void) {
    uint8_t newline = 1;

    if (*ip == TOK_HASH) {
        file_number(0);
        if (err) return;
        out_mode = OUT_FILE;
        out_failed = 0;
    }
    while (!err && !stmt_end()) {
        if (*ip == TOK_SEMI || *ip == TOK_COMMA) {
            if (*ip++ == TOK_COMMA)
                do out_char(' '); while (col[out_mode] % ZONE);
            newline = 0;
            continue;
        }
        if (*ip == TOK_STR) {
            uint8_t len = *++ip;
            ip++;
            while (len--) out_char(*ip++);
        } else {
            int16_t v = expr();
            if (!err) out_num(v);
        }
        newline = 1;
    }
    if (!err && newline) out_nl();
    if (out_mode == OUT_FILE && out_failed && !err) err = E_DISK_FULL;
    out_mode = OUT_CONSOLE;
}

static void do_open(void) {
    char name[13];
    uint8_t mode;
    if (*ip++ != TOK_STR) {
        err = E_SYNTAX;
        return;
    }
    uint8_t len = *ip++;
    const char *s = (const char *)ip;
    ip += len;
    if (!expect(TOK_FOR)) return;
    if (*ip == TOK_INPUT) mode = FS_READ;
    else if (*ip == TOK_OUTPUT) mode = FS_WRITE;
    else if (*ip == TOK_APPEND) mode = FS_APPEND;
    else {
        err = E_SYNTAX;
        return;
    }
    ip++;
    if (!expect(TOK_AS)) return;
    if (*ip == TOK_HASH) ip++;
    if (expr() != 1) {
        if (!err) err = E_FILE_NUM;
        return;
    }
    if (file_mode) {
        err = E_FILE_OPEN;
        return;
    }
    if ((err = file_name(s, len, name))) return;
    if ((err = fs_err(hw_fopen(name, mode)))) return;
    file_mode = mode;
    file_peek = -1;
}

/* "a, b", or "(a, b)" when parenthesized */
static void two_args(int16_t *a) {
    if (*ip == TOK_LPAREN) {
        if (args(a, 2) != 2 && !err) err = E_SYNTAX;
        return;
    }
    a[0] = expr();
    if (expect(TOK_COMMA)) a[1] = expr();
}

static int set_pins(const uint8_t *m) {
    int r = 0;
    for (uint8_t i = 0; i < 4; i++) {
        int e = hw_pin_mode(i + 1, m[i]);
        if (e) r = e;
        else pins[i] = m[i];
    }
    return r;
}

/* Run one statement at ip. Returns 0 to continue with the next
 * statement, 1 if the position was changed (jump), -1 to stop. */
static int statement(void) {
    uint8_t tok = *ip++;
    int16_t a, b, ab[2];
    int8_t p;

    switch (tok) {
        case TOK_COLON:
            return 0;

        case TOK_REM:
            ip += 1 + *ip;
            break;

        case TOK_ELSE:
            /* reached the end of a THEN part: skip the ELSE part */
            while (*ip != TOK_EOL) skip_token(&ip);
            break;

        case TOK_LET:
            if (*ip++ != TOK_VAR) {
                err = E_SYNTAX;
                break;
            }
            /* fall through */
        case TOK_VAR: {
            uint8_t v = *ip++;
            if (!expect(TOK_EQ)) break;
            a = expr();
            if (!err) vars[v] = a;
            break;
        }

        case TOK_IF: {
            a = expr();
            if (err) break;
            if (*ip == TOK_THEN) ip++;
            if (!a) {
                /* skip to the ELSE that belongs to this IF */
                int depth = 0;
                while (*ip != TOK_EOL) {
                    if (*ip == TOK_IF) depth++;
                    else if (*ip == TOK_ELSE && depth-- == 0) {
                        ip++;
                        break;
                    }
                    skip_token(&ip);
                }
            }
            /* THEN 100 and ELSE 100 mean GOTO 100 */
            if (*ip == TOK_NUM) return go(0);
            return 0;   /* ip is at the start of the next statement */
        }

        case TOK_PRINT:
            do_print();
            break;

        case TOK_INPUT:
            if (*ip == TOK_HASH) {
                file_number(1);
                if (err) break;
                if (*ip++ != TOK_VAR) {
                    err = E_SYNTAX;
                    break;
                }
                uint8_t v = *ip++;
                a = file_number_value();
                if (!err) vars[v] = a;
                break;
            }
            if (*ip == TOK_STR) {
                uint8_t len = *++ip;
                ip++;
                while (len--) out_char(*ip++);
                if (*ip == TOK_COMMA || *ip == TOK_SEMI) ip++;
            }
            if (*ip++ != TOK_VAR) {
                err = E_SYNTAX;
                break;
            }
            input_var = *ip++;
            resume_line = line_p - program;
            resume_pos = ip - program;
            basic_input = 1;
            out_str("? ");
            return -1;

        case TOK_GOTO:
            return go(0);

        case TOK_GOSUB:
            return go(1);

        case TOK_RETURN:
            if (!gosub_sp) {
                err = E_RETURN;
                break;
            }
            gosub_sp--;
            jump(program + gosub_stack[gosub_sp].line,
                program + gosub_stack[gosub_sp].pos);
            return 1;

        case TOK_FOR: {
            if (*ip++ != TOK_VAR) {
                err = E_SYNTAX;
                break;
            }
            uint8_t v = *ip++;
            if (!expect(TOK_EQ)) break;
            a = expr();
            if (!expect(TOK_TO)) break;
            b = expr();
            int16_t step = 1;
            if (*ip == TOK_STEP) {
                ip++;
                step = expr();
            }
            if (err) break;
            vars[v] = a;
            /* a loop on the same variable replaces it and those inside */
            for (uint8_t i = 0; i < for_sp; i++) {
                if (for_stack[i].var == v) {
                    for_sp = i;
                    break;
                }
            }
            if (for_sp == MAX_FOR) {
                err = E_NESTING;
                break;
            }
            for_stack[for_sp].var = v;
            for_stack[for_sp].limit = b;
            for_stack[for_sp].step = step;
            for_stack[for_sp].line = line_p - program;
            for_stack[for_sp++].pos = ip - program;
            break;
        }

        case TOK_NEXT: {
            if (*ip == TOK_VAR) {
                uint8_t v = ip[1];
                ip += 2;
                while (for_sp && for_stack[for_sp - 1].var != v) for_sp--;
            }
            if (!for_sp) {
                err = E_NEXT;
                break;
            }
            uint8_t i = for_sp - 1;
            int32_t nv = (int32_t)vars[for_stack[i].var] + for_stack[i].step;
            if (for_stack[i].step >= 0 ? nv > for_stack[i].limit
                                       : nv < for_stack[i].limit) {
                for_sp--;
                break;
            }
            vars[for_stack[i].var] = (int16_t)nv;
            jump(program + for_stack[i].line, program + for_stack[i].pos);
            return 1;
        }

        case TOK_END:
            return -1;


        case TOK_SLEEP:
            wait_ms((int32_t)expr() * 1000);
            break;

        case TOK_WAIT:
            wait_ms(expr());
            break;

        case TOK_PINS:
            err = fs_err(set_pins(ip));
            ip += 4;
            break;
#if HW_PINS > 4
        case TOK_PIN: {
            int e = hw_pin_mode(ip[0], ip[1]);
            if (e) err = fs_err(e);
            else pins[ip[0] - 1] = ip[1];
            ip += 2;
            break;
        }
#endif

        case TOK_OUT:
            two_args(ab);
            if (err || (p = pin_arg(ab[0])) < 0) break;
            if (pins[p] != PM_OD && pins[p] != PM_PP) err = E_PIN;
            else hw_pin_write(p + 1, ab[1] != 0);
            break;

        case TOK_LED:
            a = expr();
            if (!err) hw_led(a != 0);
            break;

        case TOK_I2C: {
            uint8_t w[2], n = 0;
            a = expr();
            while (!err && *ip == TOK_COMMA && n < 2) {
                ip++;
                w[n++] = expr();
            }
            if (!n || !stmt_end()) err = E_SYNTAX;
            if (!err && i2c_ready() && hw_i2c(a, w, n, NULL, 0))
                err = E_I2C;
            break;
        }

        case TOK_REG:
            two_args(ab);
            if (err) break;
            if (ab[0] < 0 || ab[0] >= NUM_REGS) err = E_RANGE;
            else basic_regs[ab[0]] = ab[1];
            break;

        case TOK_OPEN:
            do_open();
            break;

        case TOK_CLOSE:
            if (*ip == TOK_HASH) ip++;
            if (expr() != 1) {
                if (!err) err = E_FILE_NUM;
                break;
            }
            if (!file_mode) err = E_FILE_NOT_OPEN;
            else file_close();
            break;

        default:
#ifdef BASIC_EXT
            if (IS_EXT(tok, 0)) {
                ext_call(tok);
                break;
            }
#endif
            ip--;
            err = E_SYNTAX;
    }

    if (!err && !stmt_end()) err = E_SYNTAX;
    return err ? -1 : 0;
}

/* ================= MAIN EXECUTION LOOP ================= */

/* Run from line_p/ip until the end, END, INPUT or an error. */
static void run(void) {
    basic_running = 1;
    basic_prog_err = 0;
    while (line_p < program + prog_len) {
        cur_line = line_p[0] | (line_p[1] << 8);
        if (hw_break()) err = E_BREAK;
        while (!err && *ip != TOK_EOL) {
            int r = statement();
            if (r < 0) goto stop;
            if (r > 0) goto next;
        }
        if (err) break;
        jump(line_p + 3 + line_p[2], NULL);
    next:;
    }
stop:
    basic_running = 0;
    basic_prog_err = err;
    if (err) report(err);
    if (!basic_input) {
        err = E_NONE;
        file_close();       /* a program's file is closed when it stops */
        if (err) report(err);
    }
    err = E_NONE;
    cur_line = 0;
}

static void clear(void) {
    static const uint8_t default_pins[4] = { PM_NET, PM_NET, 0, 0 };
    memset(vars, 0, sizeof(vars));
    for_sp = gosub_sp = 0;
    basic_input = 0;
    set_pins(default_pins);
#if HW_PINS > 4
    for (uint8_t i = 4; i < HW_PINS; i++) {     /* pins 5 and up: unused */
        hw_pin_mode(i + 1, PM_NONE);
        pins[i] = PM_NONE;
    }
#endif
}

static void new_program(void) {
    prog_len = 0;
    clear();
}

/* ================= COMMANDS ================= */

static void dir_entry(const char *name, void *ctx) {
    (void)ctx;
    out_str(name);
    out_nl();
}

static void cmd_dir(void) {
    err = fs_err(hw_fdir(dir_entry));
}

/* Open the file named by a command's argument. With mode 0 the file is
 * deleted instead. Returns 1 on success, 0 with err set. */
static uint8_t open_arg(char *arg, uint8_t mode) {
    char name[13];
    if ((err = arg_name(arg, name))) return 0;
    err = fs_err(mode ? hw_fopen(name, mode) : hw_fdelete(name));
    if (mode == FS_READ && !err) {
        file_mode = FS_READ;
        file_peek = -1;
    }
    return !err;
}

static void cmd_save(char *arg) {
    if (!open_arg(arg, FS_WRITE)) return;
    out_mode = OUT_FILE;
    out_failed = 0;
    list_program();
    out_mode = OUT_CONSOLE;
    if (out_failed) {
        hw_fabort();
        err = E_DISK_FULL;
        return;
    }
    err = fs_err(hw_fclose());
}

/* LOAD and TYPE: read the open file line by line */
char basic_line[BASIC_LINE];

static void cmd_read(char *arg, uint8_t load) {
    char *line = basic_line;    /* (arg may be in it: used up by open_arg) */
    int16_t c;
    uint8_t n = 0;
    if (!open_arg(arg, FS_READ)) return;
    if (load) new_program();
    do {
        c = file_getc();
        if (!load) {
            if (c == '\n') out_nl();
            else if (c >= 0) out_char(c);
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n' || c < 0) {
            line[n] = 0;
            n = 0;
            char *s = skip_spaces(line);
            if (*s) enter_line(s);
        } else if (n < MAX_SRC) {
            line[n++] = c;
        } else {
            err = E_LINE_TOO_LONG;
        }
    } while (c >= 0 && !err);
    file_close();
}

/* FORMAT YES: the YES is required, so that storage is never erased by a
 * mistyped command */
static void cmd_format(char *arg) {
    if (!starts_with(skip_spaces(arg), "YES")) err = E_SYNTAX;
    else err = fs_err(hw_fformat());
}

/* print a list of words */
static void help(const char *k) {
#if defined(BASIC_PROFILE) && HW_PINS > 4
    if (k == kw_names && basic_pins <= 4) {     /* the keywords, but PIN */
        for (const char *p = k; *p; ) {
            const char *e = p;
            while (*e != ' ') e++;
            if (e - p != 3 || p[0] != 'P' || p[1] != 'I' || p[2] != 'N')
                while (p <= e) out_char(*p++);
            p = e + 1;
        }
        out_nl();
        return;
    }
#endif
    out_str(k);
    out_nl();
}

static void process_command(char *line) {
    line = skip_spaces(line);
    if (at_end(line)) return;

    if (is_digit(*line)) {
        enter_line(line);
    } else {
        /* the command word; arg is what follows it */
        uint8_t n;
        int8_t i = find_word(cmd_names, line, &n);
        if (i < 0 || is_alpha(line[n]) || is_digit(line[n])) err = E_SYNTAX;
        char *arg = line + n;
        if (!err) switch (i) {
            case 0:     /* RUN */
                clear();
                jump(program, NULL);
                run();
                break;
            case 1:     /* LIST */
                list_program();
                break;
            case 2:     /* NEW */
                new_program();
                break;
            case 3:     /* SAVE */
                cmd_save(arg);
                break;
            case 4:     /* LOAD */
                cmd_read(arg, 1);
                break;
            case 5:     /* DIR */
                cmd_dir();
                break;
            case 6:     /* DEL */
                open_arg(arg, 0);
                break;
            case 7:     /* TYPE */
                cmd_read(arg, 0);
                break;
            case 8:     /* FORMAT */
                cmd_format(arg);
                break;
#ifdef HW_PROG_MODE
            case 10:    /* BOOT */
                hw_prog_mode();
                break;
#endif
            default:    /* HELP */
#ifndef NO_HELP
                help(cmd_names);
                help(kw_names);
#ifdef BASIC_EXT
                for (uint8_t e = 0; e < basic_ext_count; e++) {
                    out_str(basic_ext[e].name);
                    out_char(' ');
                }
                out_nl();
#endif
#endif
                break;
        }
    }

    basic_cmd_err = err;
    if (err) report(err);
    err = E_NONE;
}

/* ================= INPUT ROUTING ================= */

void basic_yield(uint8_t *line) {
    col[OUT_CONSOLE] = 0;   /* the user's Enter ended the line */
    if (basic_input) {
        basic_input = 0;
        /* a number, with optional spaces and sign */
        char *s = skip_spaces((char *)line);
        int16_t v = 0, neg = *s == '-';
        if (neg || *s == '+') s++;
        while (is_digit(*s)) v = v * 10 + (*s++ - '0');
        vars[input_var] = neg ? -v : v;
        jump(program + resume_line, program + resume_pos);
        run();
    } else {
        process_command((char *)line);
    }
}

int basic_boot(void) {
    if (hw_fopen("BOOT.BAS", FS_READ)) return 0;
    hw_fclose();
    process_command("LOAD BOOT.BAS");
    return !basic_cmd_err;
}


/* ================= LINUX TARGET ================= */

#ifdef TARGET_LINUX

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/* Files are ordinary files in the current directory, named in 8.3 form.
 * Like the files on a module, FS_WRITE replaces a file only when it is
 * closed. */

static volatile sig_atomic_t got_break;
static FILE *fp;
static uint8_t fmode;
static char fname[13];
static char tmpname[16];

static void on_sigint(int sig) {
    (void)sig;
    got_break = 1;
}

void hw_putc(char c) {
    putchar(c);
}

int hw_break(void) {
    if (!got_break) return 0;
    got_break = 0;
    return 1;
}

void hw_delay_ms(uint16_t ms) {
    fflush(stdout);
    usleep((useconds_t)ms * 1000);
}

/* Simulated hardware, for trying programs and for the tests:
 *   pins     an output's level reads back on IN(); inputs read 1
 *   ADC      pin 3 reads 300, pin 4 reads 400
 *   I2C      a 256-byte memory at address 0x50 (first byte written is
 *            the register address); no other devices answer */

static uint8_t sim_level[HW_PINS] = { 1, 1, 1, 1 };
static uint8_t sim_mem[256];
static uint8_t sim_reg;

int hw_pin_mode(uint8_t pin, uint8_t mode) {
    if (mode != PM_OD && mode != PM_PP) sim_level[pin - 1] = 1;
    return 0;
}

void hw_pin_write(uint8_t pin, uint8_t level) {
    sim_level[pin - 1] = level;
}

uint8_t hw_pin_read(uint8_t pin) {
    return sim_level[pin - 1];
}

int16_t hw_adc(uint8_t pin) {
    return pin == 3 ? 300 : 400;
}

void hw_led(uint8_t on) {
    (void)on;
}

int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r,
           uint8_t rn) {
    if (addr != 0x50) return -1;
    if (wn) sim_reg = w[0];
    for (uint8_t i = 1; i < wn; i++) sim_mem[sim_reg++] = w[i];
    for (uint8_t i = 0; i < rn; i++) r[i] = sim_mem[sim_reg++];
    return 0;
}


int hw_fopen(const char *name, uint8_t mode) {
    if (fp) return FS_ERR_BUSY;
    strcpy(fname, name);
    if (mode == FS_READ) {
        fp = fopen(name, "rb");
        if (!fp) return FS_ERR_NOT_FOUND;
    } else if (mode == FS_WRITE) {
        snprintf(tmpname, sizeof(tmpname), "%s~", name);
        fp = fopen(tmpname, "wb");
        if (!fp) return FS_ERR_IO;
    } else if (mode == FS_APPEND) {
        fp = fopen(name, "ab");
        if (!fp) return FS_ERR_IO;
    } else {
        return FS_ERR_INVALID;
    }
    fmode = mode;
    return FS_OK;
}

int hw_fread(uint8_t *buf, uint16_t len) {
    if (!fp || fmode != FS_READ) return FS_ERR_NOT_OPEN;
    return (int)fread(buf, 1, len, fp);
}

int hw_fwrite(const uint8_t *buf, uint16_t len) {
    if (!fp || fmode == FS_READ) return FS_ERR_NOT_OPEN;
    return fwrite(buf, 1, len, fp) == len ? FS_OK : FS_ERR_IO;
}

int hw_fclose(void) {
    if (!fp) return FS_ERR_NOT_OPEN;
    int r = fclose(fp) ? FS_ERR_IO : FS_OK;
    fp = NULL;
    if (fmode == FS_WRITE && r == FS_OK && rename(tmpname, fname))
        r = FS_ERR_IO;
    return r;
}

void hw_fabort(void) {
    if (!fp) return;
    fclose(fp);
    fp = NULL;
    if (fmode == FS_WRITE) remove(tmpname);
}

int hw_fdelete(const char *name) {
    return remove(name) ? FS_ERR_NOT_FOUND : FS_OK;
}

/* list the files in the current directory whose names are 8.3 */
int hw_fdir(fs_dir_cb cb) {
    DIR *d = opendir(".");
    struct dirent *e;
    struct stat st;
    if (!d) return FS_ERR_IO;
    while ((e = readdir(d))) {
        char norm[13];
        if (strlen(e->d_name) > 12 ||
            file_name(e->d_name, strlen(e->d_name), norm) ||
            strcmp(norm, e->d_name) || stat(e->d_name, &st) ||
            !S_ISREG(st.st_mode)) continue;
        cb(e->d_name, NULL);
    }
    closedir(d);
    return FS_OK;
}


int hw_fformat(void) {
    return HW_ERR_UNSUPPORTED;
}

int main(void) {
    char line[256];

#ifdef BASIC_PROFILE
    /* the machine to play, for the tests: BASIC_PROFILE=LS10 or LS11 */
    const char *prof = getenv("BASIC_PROFILE");
    if (prof && !strcmp(prof, "LS10")) {
        basic_prog_max = 1024;
        basic_pins = 4;
    } else if (prof && !strcmp(prof, "LS11")) {
        basic_prog_max = 4096;
        basic_pins = 7;
    }
#endif
    signal(SIGINT, on_sigint);
    puts("///");

    for (;;) {
        if (!basic_input) printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        basic_yield((uint8_t *)line);
    }
    return 0;
}

#endif
