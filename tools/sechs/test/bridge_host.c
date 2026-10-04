/*
 * The Sechs bridge (../bridge.c) on the host, for tests: it serves a
 * pseudo-terminal, prints its name, and answers as a bridge with a
 * simulated module behind it (../sim.h). In UART mode it echoes what it
 * receives.
 */

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include "../../../sechs/sechs.h"
#include "../sim.h"
#include "../bridge.h"
#include "ch32sim.h"    /* a CH32V003 on the programming wires */

static int master;
static int uart_mode;

/* serve the bus while a program runs: whatever has arrived, no waiting */
static void poll_bus(void) {
    char c;
    while (read(master, &c, 1) == 1) bridge_char(c);
}

int bridge_i2c_write(uint8_t addr, const uint8_t *d, uint8_t n) {
    return bus_write(addr, d, n);
}

int bridge_i2c_read(uint8_t addr, uint8_t reg, uint8_t *d, uint8_t n) {
    return bus_read(addr, reg, d, n);
}

int bridge_uart(uint32_t baud) {
    (void)baud;
    uart_mode = 1;
    return 0;
}

static void prog_progress(int pct) {
    static int last = -1;
    if (pct / 25 == last) return;
    last = pct / 25;
    char b[32];
    sprintf(b, "progress %d\n", pct);
    bridge_puts(b);
}

static void prog_status(const char *msg) {
    char b[100];
    snprintf(b, sizeof(b), "status %s\n", msg);
    bridge_puts(b);
}

static void chip_ready(void) {
    static int made;
    if (!made) {
        chip_new(0x5a);
        made = 1;
    }
    ch32_status = prog_status;
}

void bridge_identify(void) {
    char b[100];
    chip_ready();
    dmi_n = 0;
    int r = ch32_identify();
    if (r) snprintf(b, sizeof(b), "fail %s\n", ch32_message(r));
    else snprintf(b, sizeof(b), "ok chip %08lx hartinfo %08lx\n",
                  (unsigned long)ch32_chip_id, (unsigned long)ch32_hartinfo);
    bridge_puts(b);
}

void bridge_link_test(uint32_t n) {
    char b[100];
    uint32_t errors;
    chip_ready();
    int r = ch32_link_test(n, &errors);
    if (r == CH32_NO_CHIP)
        snprintf(b, sizeof(b), "fail %s (read %08lx%s)\n", ch32_message(r),
                 (unsigned long)ch32_last_read,
                 ch32_last_read_ok ? "" : ", the line stayed low");
    else if (r) snprintf(b, sizeof(b), "fail %s\n", ch32_message(r));
    else snprintf(b, sizeof(b), "ok %lu errors %lu\n", (unsigned long)n,
                  (unsigned long)errors);
    bridge_puts(b);
}

void bridge_swio_timing(const uint32_t *v, int n) {
    (void)v;
    (void)n;
    bridge_puts("ok 180 900 150 150 4 0\n");      /* (no timing to set) */
}

void bridge_prog(const uint8_t *img, uint32_t len, int force) {
    char b[120];
    chip_ready();
    dmi_n = 0;
    int r = ch32_flash(img, len, force, prog_progress);
    /* (the simulated chip's own checks: what is in its flash, violations) */
    if (!r && (memcmp(chip.flash, img, len) || violations)) {
        bridge_puts("fail the simulated chip disagrees\n");
        return;
    }
    sprintf(b, "%s %s\n", r ? "fail" : "ok", ch32_message(r));
    bridge_puts(b);
}

void bridge_puts(const char *s) {
    if (write(master, s, strlen(s)) < 0) exit(1);
}

int main(void) {
    struct termios t;
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master)) return 1;
    const char *name = ptsname(master);
    /* raw, and kept open so the terminal survives between clients */
    int slave = open(name, O_RDWR | O_NOCTTY);
    if (slave < 0 || tcgetattr(slave, &t)) return 1;
    cfmakeraw(&t);
    tcsetattr(slave, TCSANOW, &t);
    if (bus_open(0)) return 1;
    sim_poll = poll_bus;
    fcntl(master, F_SETFL, O_NONBLOCK);
    printf("%s\n", name);
    fflush(stdout);
    for (;;) {
        char c;
        ssize_t r = read(master, &c, 1);
        if (r < 0) {
            bus_idle();     /* the module's main loop (programs run here) */
            usleep(1000);
            continue;
        }
        if (r != 1) return 0;
        if (uart_mode) {
            if (write(master, &c, 1) != 1) return 1;
        } else {
            bridge_char(c);
        }
    }
}
