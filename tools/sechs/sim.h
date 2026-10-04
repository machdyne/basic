/*
 * A simulated Sechs module for the sechs tool (built with -DSECHS_SIM):
 * the real interpreter, filesystem and Sechs core, with an 8KB F-RAM kept
 * in the file named by SECHS_SIM_IMAGE (default ZZSIM.IMG), so that it
 * persists between runs like a real module.
 */

#include <fcntl.h>
#include "../../basic.h"

static int img = -1;
static uint8_t addr_saved;

/* Set by a host that serves the bus while a program runs (bridge_host):
 * called between statements, as the I2C interrupt runs on a module. */
static void (*sim_poll)(void);

int fs_media_read(uint32_t a, uint8_t *b, uint16_t n) {
    return pread(img, b, n, a) == n ? 0 : -1;
}
int fs_media_prog(uint32_t a, const uint8_t *b, uint16_t n) {
    return pwrite(img, b, n, a) == n ? 0 : -1;
}

void hw_putc(char c) { sechs_putc(c); }
int hw_break(void) {
    if (sim_poll) sim_poll();
    if (sechs.cmd == CMD_RESET) return 1;   /* stop; the reset follows */
    if (sechs.con_break || sechs.cmd == CMD_HALT) {
        sechs.con_break = 0;
        sechs.cmd = 0;
        return 1;
    }
    return 0;
}
void hw_delay_ms(uint16_t ms) { (void)ms; }

/* the console output is full: a host that serves the bus (bridge_host)
 * lets the master read now, as the I2C interrupt does on a module */
void sechs_wait(void) {
    if (sim_poll) sim_poll();
    usleep(2000);       /* as long as on LS10 */
}
/* like LS10: pins 1 and 2 cannot be outputs once a master has
 * addressed the module */
int hw_pin_mode(uint8_t p, uint8_t m) {
    if (p <= 2 && (m == PM_OD || m == PM_PP) && sechs.networked)
        return HW_ERR_BUS;
    return 0;
}
void hw_pin_write(uint8_t p, uint8_t l) { (void)p; (void)l; }
uint8_t hw_pin_read(uint8_t p) { (void)p; return 1; }
int16_t hw_adc(uint8_t p) { (void)p; return 512; }
void hw_led(uint8_t on) { (void)on; }
int hw_i2c(uint8_t a, const uint8_t *w, uint8_t wn, uint8_t *r, uint8_t rn) {
    (void)a; (void)w; (void)wn; (void)r; (void)rn;
    return -1;
}
int hw_fformat(void) { return fs_format_default(8192 - 16); }

uint8_t *sechs_regs(void) { return basic_regs; }
static const char sim_info[] = "fw=Machdyne BASIC\nmod=SIM\nlang=basic\n";
uint8_t sechs_info(uint8_t i) { return i < sizeof(sim_info) - 1 ? sim_info[i] : 0; }

/* the address lives in the image's last bytes, as on LS10 */
void sechs_set_addr(uint8_t a) {
    uint8_t c[3] = { 0xA5, a, (uint8_t)~a };
    fs_media_prog(8192 - 16, c, 3);
    addr_saved = a;
}

/* power-on: the address from the image, an empty program, REG all 0 */
static void sim_start(void) {
    uint8_t c[3];
    fs_media_read(8192 - 16, c, 3);
    sechs_init((c[0] == 0xA5 && c[2] == (uint8_t)~c[1]) ? c[1] : SECHS_DEFAULT_ADDR,
        CAP_FILES | CAP_UART_CON | CAP_I2C_CON);
    if (fs_mount(8192 - 16) == FS_ERR_UNFORMATTED) fs_format_default(8192 - 16);
    basic_yield((uint8_t *)"NEW");
    memset(basic_regs, 0, 16);
    sechs.con_active = 1;
}

static int bus_open(int bus) {
    (void)bus;
    const char *path = getenv("SECHS_SIM_IMAGE");
    if (!path) path = "ZZSIM.IMG";
    int fresh = access(path, F_OK) != 0;
    img = open(path, O_RDWR | O_CREAT, 0644);
    if (img < 0) return -1;
    if (fresh) {
        uint8_t z[8192];
        memset(z, 0xFF, sizeof(z));
        if (write(img, z, sizeof(z)) != (ssize_t)sizeof(z)) return -1;
    }
    sim_start();
    return 0;
}

/* STATUS, FAULT and OK, as LS10's service() keeps them */
static void sim_status(void) {
    uint8_t fault = basic_prog_err == BASIC_E_BUS ? FAULT_BUS :
        basic_prog_err && basic_prog_err != BASIC_E_BREAK ? FAULT_PROGRAM : 0;
    sechs.r[SR_FAULT] = fault;
    sechs.r[SR_STATUS] = (sechs.networked ? ST_NETWORKED : 0) |
        (basic_running ? ST_RUNNING : 0) | (fault ? ST_FAULT : 0) | ST_CONSOLE;
    uint8_t s = sechs.r[SR_STATUS];
    sechs.r[SR_OK] = ~(((s >> 4) & 0x06) | ((s << 2) & 0x08) |
        (basic_cmd_err ? 0x10 : 0)) & 0x1F;
}

/* the module's main loop, as on LS10: console lines and CONTROL commands;
 * this is where programs run */
static void bus_idle(void) {
    static char line[128];
    static int n;
    int c;
    while (!basic_running && (c = sechs_getc()) >= 0) {
        hw_putc(c);
        if (c == '\r' || c == '\n') {
            hw_putc('\n');
            line[n] = 0;
            n = 0;
            basic_yield((uint8_t *)line);
        } else if (n < 127) {
            line[n++] = c;
        }
    }
    sim_status();
    if (basic_running) return;
    if (sechs.cmd == CMD_HALT) {
        sechs.cmd = 0;      /* nothing running: nothing to stop */
    } else if (sechs.cmd == CMD_RUN) {
        sechs.cmd = 0;
        basic_yield((uint8_t *)"RUN");
    } else if (sechs.cmd == CMD_RESET) {
        /* restart: RAM cleared, then BOOT.BAS as at power-on */
        sechs.cmd = 0;
        sim_start();
        if (basic_boot()) basic_yield((uint8_t *)"RUN");
    }
    sim_status();
}

/* A register access only touches registers when the host runs the main
 * loop itself (bridge_host); otherwise the main loop runs here. */
static void bus_after(void) {
    if (sim_poll) sim_status();
    else bus_idle();
}

static int bus_write(uint8_t a, const uint8_t *d, int n) {
    if (a != sechs.addr) return -1;
    sechs_start(0);
    for (int i = 0; i < n; i++) sechs_rx(d[i]);
    sechs_stop(0);
    bus_after();
    return 0;
}

static int bus_read(uint8_t a, uint8_t reg, uint8_t *d, int n) {
    if (a != sechs.addr) return -1;
    bus_after();
    sechs_start(0);
    sechs_rx(reg);
    sechs_start(0);
    /* like the hardware: one byte is always loaded ahead, and the last one
     * loaded is never sent */
    for (int i = 0; i < n; i++) d[i] = sechs_tx();
    sechs_tx();
    sechs_stop(1);
    return 0;
}
