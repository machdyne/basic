/*
 * Host tests for the Sechs core with the interpreter and filesystem.
 *
 * A simulated I2C bus drives sechs.c exactly as a target's interrupt
 * handler does; a simulated main loop feeds the I2C console into the
 * interpreter, as on LS10.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sechs.h"
#include "../../basic.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

/* ---- simulated target -------------------------------------------------- */

static uint8_t fram[8192];
static uint8_t saved_addr;

int fs_media_read(uint32_t a, uint8_t *b, uint16_t n) {
    if (a + n > sizeof(fram)) return -1;
    memcpy(b, fram + a, n);
    return 0;
}
int fs_media_prog(uint32_t a, const uint8_t *b, uint16_t n) {
    if (a + n > sizeof(fram)) return -1;
    memcpy(fram + a, b, n);
    return 0;
}

static int on_bus = 1;
static int halt_seen;

void hw_putc(char c) { sechs_putc(c); }
int hw_break(void) {
    if (sechs.con_break) {
        sechs.con_break = 0;
        return 1;
    }
    if (sechs.cmd == CMD_HALT) {
        sechs.cmd = 0;
        halt_seen = 1;
        return 1;
    }
    return 0;
}
void hw_delay_ms(uint16_t ms) { (void)ms; }
int hw_pin_mode(uint8_t pin, uint8_t mode) {
    if (pin <= 2 && (mode == PM_OD || mode == PM_PP) && on_bus)
        return HW_ERR_BUS;
    return 0;
}
void hw_pin_write(uint8_t p, uint8_t l) { (void)p; (void)l; }
uint8_t hw_pin_read(uint8_t p) { (void)p; return 1; }
int16_t hw_adc(uint8_t p) { (void)p; return -1; }
void hw_led(uint8_t on) { (void)on; }
int hw_i2c(uint8_t a, const uint8_t *w, uint8_t wn, uint8_t *r, uint8_t rn) {
    (void)a; (void)w; (void)wn; (void)r; (void)rn;
    return -1;
}
int hw_fopen(const char *n, uint8_t m) { return fs_open(n, m); }
int hw_fread(uint8_t *b, uint16_t l) { return fs_read(b, l); }
int hw_fwrite(const uint8_t *b, uint16_t l) { return fs_write(b, l); }
int hw_fclose(void) { return fs_close(); }
void hw_fabort(void) { fs_abort(); }
int hw_fdelete(const char *n) { return fs_delete(n); }
int hw_fdir(fs_dir_cb cb) { return fs_dir(cb, 0); }
int hw_fformat(void) { return fs_format_default(sizeof(fram)); }

void sechs_set_addr(uint8_t a) { saved_addr = a; }
uint8_t *sechs_regs(void) { return basic_regs; }

/* what LS10's service() does */
static void update(void) {
    uint8_t fault = basic_prog_err == BASIC_E_BUS ? FAULT_BUS :
        basic_prog_err && basic_prog_err != BASIC_E_BREAK ? FAULT_PROGRAM : 0;
    sechs.r[SR_FAULT] = fault;
    sechs.r[SR_STATUS] = (sechs.networked ? ST_NETWORKED : 0) |
        (fault ? ST_FAULT : 0) | (fs_degraded() ? ST_DEGRADED : 0);
    sechs.r[SR_OK] = 0x01 | (fault ? 0 : 0x02) | (fs_degraded() ? 0 : 0x04) |
        0x08 | (basic_cmd_err ? 0 : 0x10);
}

/* INFO as LS10 builds it */
static const char info[] = "fw=Machdyne BASIC\nmod=TEST\nlang=basic\n";
uint8_t sechs_info(uint8_t i) {
    return i < strlen(info) ? info[i] : 0;
}

/* ---- simulated bus ------------------------------------------------------- */

/* a write transaction; returns 0 if nobody answered */
static int bus_write(uint8_t addr, const uint8_t *d, int n) {
    if (addr != 0 && addr != sechs.addr) return 0;
    sechs_start(addr == 0);
    for (int i = 0; i < n; i++) sechs_rx(d[i]);
    sechs_stop(0);
    return 1;
}

static int reg_write(uint8_t reg, const uint8_t *d, int n) {
    uint8_t buf[64];
    buf[0] = reg;
    memcpy(buf + 1, d, n);
    return bus_write(sechs.addr, buf, n + 1);
}

static void reg_read(uint8_t reg, uint8_t *out, int n) {
    sechs_start(0);
    sechs_rx(reg);
    sechs_start(0);     /* repeated start, read */
    /* like the hardware: one byte loaded ahead, never sent */
    for (int i = 0; i < n; i++) out[i] = sechs_tx();
    sechs_tx();
    sechs_stop(1);
}

static uint8_t reg1(uint8_t reg) {
    uint8_t v;
    reg_read(reg, &v, 1);
    return v;
}

/* main loop: console input from I2C into the interpreter */
static void main_loop(void) {
    static char line[128];
    static int n;
    int c;
    while ((c = sechs_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            line[n] = 0;
            n = 0;
            basic_yield((uint8_t *)line);
            update();
        } else if (n < 127) {
            line[n++] = c;
        }
    }
}

/* type text into the I2C console, honouring CIN, running the main loop */
static void con_type(const char *s) {
    while (*s) {
        uint8_t room = reg1(SR_CIN);
        int n = 0;
        while (s[n] && n < room && n < 16) n++;
        if (n) reg_write(SR_CDATA, (const uint8_t *)s, n);
        s += n;
        main_loop();
    }
}

/* Everything the console has printed. A master that reads while the
 * module waits for room (sechs_wait) collects it here too. */
static char con_out[8192];
static int con_len, reading = 1;

static void con_drain(void) {
    int n;
    while ((n = reg1(SR_COUT)) > 0) {
        uint8_t b[64];
        reg_read(SR_CDATA, b, n);
        for (int i = 0; i < n; i++)
            if (b[i] != '\r' && con_len < (int)sizeof(con_out) - 1) con_out[con_len++] = b[i];
    }
}

void sechs_wait(void) {
    if (reading) con_drain();
}

static char *con_read(void) {
    con_drain();
    con_out[con_len] = 0;
    con_len = 0;
    return con_out;
}

/* ---- tests ---------------------------------------------------------------- */

int main(void) {
    uint8_t b[64];

    memset(fram, 0xFF, sizeof(fram));
    fs_format_default(sizeof(fram));
    sechs_init(SECHS_DEFAULT_ADDR, CAP_FILES | CAP_UART_CON | CAP_I2C_CON);
    basic_yield((uint8_t *)"NEW");
    update();

    /* identity */
    reg_read(SR_SIG0, b, 4);
    CHECK(b[0] == 'S' && b[1] == '6', "signature %02x %02x", b[0], b[1]);
    CHECK(b[2] == SECHS_VERSION, "version %02x", b[2]);
    CHECK(b[3] & CAP_I2C_CON, "caps %02x", b[3]);
    update();
    CHECK(reg1(SR_STATUS) & ST_NETWORKED, "networked after being addressed");

    /* INFO */
    reg_read(SR_INFO, b, 60);
    b[59] = 0;
    CHECK(!strcmp((char *)b, "fw=Machdyne BASIC\nmod=TEST\nlang=basic\n"),
        "INFO: %s", b);

    /* console: enter a program, run it, read the output */
    con_type("10 PINS NET,NET,OD,AIN\r20 PRINT 6 * 7\rRUN\r");
    char *out = con_read();
    CHECK(strstr(out, "42\n"), "program output over I2C: [%s]", out);
    CHECK(reg1(SR_OK) & 0x10, "OK: last command succeeded");

    /* files through the console */
    con_type("SAVE BOOT\rNEW\rTYPE BOOT\r");
    out = con_read();
    CHECK(strstr(out, "10 PINS NET,NET,OD,AIN\n20 PRINT 6 * 7\n"),
        "TYPE over I2C: [%s]", out);
    con_type("LOAD NOFILE\r");
    out = con_read();
    CHECK(strstr(out, "NOT FOUND"), "error over I2C: [%s]", out);
    CHECK(!(reg1(SR_OK) & 0x10), "OK: last command failed");

    /* program registers are REG 0-15 */
    con_type("10 REG 2, 99: PRINT REG(3)\r");
    b[0] = 55;
    reg_write(SR_REG + 3, b, 1);
    con_type("RUN\r");
    out = con_read();
    CHECK(strstr(out, "55\n"), "REG(3) written by the master: [%s]", out);
    CHECK(reg1(SR_REG + 2) == 99, "REG 2 read by the master");

    /* a program error is a fault */
    con_type("10 PRINT 1 / 0\rRUN\r");
    con_read();
    CHECK(reg1(SR_FAULT) == FAULT_PROGRAM, "fault after an error");
    CHECK(!(reg1(SR_OK) & 0x02), "OK: fault bit");
    con_type("10 PRINT 1\rRUN\r");
    con_read();
    CHECK(reg1(SR_FAULT) == 0, "no fault after a good run");

    /* a program may not drive A/B on a bus */
    con_type("10 PINS NET,OD,-,-\r");
    out = con_read();
    CHECK(strstr(out, "BAD PINS"), "NET needs A and B: [%s]", out);
    con_type("10 PINS OD,OD,-,-\rRUN\r");
    out = con_read();
    CHECK(strstr(out, "ON A BUS IN 10"), "driving A/B on a bus: [%s]", out);
    CHECK(reg1(SR_FAULT) == FAULT_BUS, "bus fault");

    /* CONTROL and broadcasts */
    b[0] = CMD_RUN;
    reg_write(SR_CONTROL, b, 1);
    CHECK(sechs.cmd == CMD_RUN, "CONTROL RUN");
    sechs.cmd = 0;
    b[0] = CMD_IDENTIFY;
    reg_write(SR_CONTROL, b, 1);
    CHECK(sechs.cmd == 0, "IDENTIFY not implemented");
    uint8_t bc[2] = { SECHS_BROADCAST, CMD_HALT };
    bus_write(0, bc, 2);
    CHECK(sechs.cmd == 0, "broadcasts are not implemented");

    /* HALT stops a running program */
    con_type("10 GOTO 10\r");
    sechs.cmd = CMD_HALT;
    con_type("RUN\r");
    out = con_read();
    CHECK(halt_seen && strstr(out, "BREAK IN 10"), "HALT: [%s]", out);

    /* Ctrl-C on the I2C console */
    b[0] = 0x03;
    reg_write(SR_CDATA, b, 1);
    con_type("RUN\r");
    out = con_read();
    CHECK(strstr(out, "BREAK IN 10"), "Ctrl-C over I2C: [%s]", out);

    /* ADDR: new address and complement, applied at STOP */
    b[0] = 0x21;
    b[1] = 0x55;            /* wrong complement */
    reg_write(SR_ADDR, b, 2);
    CHECK(sechs.addr == SECHS_DEFAULT_ADDR, "bad complement ignored");
    b[1] = (uint8_t)~0x21;
    reg_write(SR_ADDR, b, 2);
    CHECK(sechs.addr == 0x21 && saved_addr == 0x21, "address changed");
    CHECK(!bus_write(SECHS_DEFAULT_ADDR, b, 1), "old address no longer answers");
    CHECK(reg1(SR_SIG0) == 'S', "new address answers");
    b[0] = 0x78;
    b[1] = (uint8_t)~0x78;
    reg_write(SR_ADDR, b, 2);
    CHECK(sechs.addr == 0x21, "reserved address refused");

    /* a byte loaded but not sent (read prefetch) stays in the buffer */
    con_type("10 PRINT \"AB\"\rRUN\r");
    sechs_start(0);
    sechs_rx(SR_CDATA);
    sechs_start(0);
    uint8_t x = sechs_tx(), y = sechs_tx();
    sechs_stop(1);              /* y was prefetched, never sent */
    out = con_read();
    CHECK(x == 'A' && y == 'B' && out[0] == 'B', "prefetch: [%c%c] [%s]", x, y, out);

    /* output that nobody reads is dropped, not blocking */
    reading = 0;
    con_type("10 FOR I = 1 TO 50: PRINT \"LINE\"; I: NEXT\rRUN\r");
    reading = 1;
    out = con_read();
    CHECK(strlen(out) <= SECHS_OUT_SIZE, "unread output limited to the buffer");

    /* and output that is read arrives complete */
    con_type("RUN\r");
    out = con_read();
    CHECK(strstr(out, "LINE1\n") && strstr(out, "LINE50\n"), "read output complete");

    /* long output through the 64-byte buffer, read while the module
     * waits; reads of exactly what is waiting (the hardware loads one byte
     * ahead) must not leave a byte behind */
    con_read();
    con_type("NEW\r");
    con_read();
    con_type("HELP\r");
    char *h = con_read();
    CHECK(strstr(h, "RUN LIST NEW SAVE") && strstr(h, "STEP THEN TO WAIT"),
        "HELP complete over I2C: %.80s", h);
    CHECK(strlen(h) > 200 && !strstr(h, "DDD") && !strstr(h, "   "),
        "HELP without repeated bytes (%d bytes)", (int)strlen(h));
    con_type("10 PRINT \"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijkl\"\r");
    con_type("LIST\r");
    h = con_read();
    CHECK(!strcmp(h, "10 PRINT \"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijkl\"\n"),
        "a long line listed intact: %s", h);

    /* a master that stops reading: the module goes on (output is
     * dropped), and output arrives again once it reads */
    reading = 0;
    con_type("HELP\r");
    reading = 1;
    con_read();
    con_type("LIST\r");
    h = con_read();
    CHECK(!strncmp(h, "10 PRINT", 8), "output again after a stall: %.60s", h);

    /* a stored address outside 0x08-0x77 is never used */
    {
        uint8_t keep = sechs.addr;
        sechs_init(0x00, CAP_FILES);
        CHECK(sechs.addr == SECHS_DEFAULT_ADDR, "address 0x00 replaced by the default");
        sechs_init(0x7E, CAP_FILES);
        CHECK(sechs.addr == SECHS_DEFAULT_ADDR, "address 0x7e replaced by the default");
        sechs_init(0x3F, CAP_FILES);
        CHECK(sechs.addr == 0x3F, "a valid address is kept");
        sechs.addr = keep;
    }

    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("all Sechs tests passed\n");
    return 0;
}
