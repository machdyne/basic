/*
 * sechsctl: a Sechs master for Linux.
 *
 *   sechsctl [-b BUS] scan                  list modules on the bus
 *   sechsctl [-b BUS] info ADDR             identity and status
 *   sechsctl [-b BUS] halt|run|reset|program ADDR   CONTROL (program: the
 *                                        module's programming mode, if CAPS
 *                                        bit 7 says it has one)
 *   sechsctl [-b BUS] addr ADDR NEW         change a module's address
 *   sechsctl [-b BUS] reg ADDR N [VALUE]    read or write program register N
 *   sechsctl [-b BUS] console ADDR          interactive I2C console
 *   sechsctl [-b BUS] send ADDR [FILE]      type FILE (or stdin) into the
 *                                        console, print the output, and
 *                                        check that the last command
 *                                        succeeded (exit status 1 if not)
 *   sechsctl -d DEV uart BAUD               (bridge only) the module's UART
 *                                        console at 9600 or 115200 baud
 *   sechsctl -d DEV flash FILE [force]      (bridge only) write the module's
 *                                        firmware through the programming
 *                                        wires (docs/ch32prog.md)
 *   sechsctl -d DEV swio-id                 (bridge only) identify the module
 *                                        on the programming wires (stops and
 *                                        restarts it; writes nothing)
 *   sechsctl -d DEV swio-test [N]           (bridge only) test the programming
 *                                        wires: N round trips (default 1000)
 *   sechsctl -d DEV -s ...                  (with flash, swio-id, swio-test) SWIO
 *                                        on the Sechs socket's pin A instead of
 *                                        the GPIO header's pin 1: for modules
 *                                        with SWIO on A (LS11), in programming
 *                                        mode (sechsctl program ADDR first)
 *   sechsctl -d DEV swio-timing [A B C D E F]  (bridge only) show or set
 *                                        the SWIO timing:
 *                                        1, 0, gap, sample (ns), pause (us),
 *                                        mode (0 driven, 1 released)
 *
 * BUS is the Linux I2C bus number (/dev/i2c-BUS, default 1). With
 * -d DEV the tool uses a USB bridge (Werkzeug's second USB serial port,
 * "Werkzeug Sechs bridge") instead. Addresses may be decimal
 * or 0x hex. Built with -DSECHS_SIM, the tool talks to a simulated module
 * instead (used by the tests).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include "../../sechs/sechs.h"
#include "sechsm.h"

/* ---- transport ------------------------------------------------------------ */

static const char *device;      /* -d: a USB bridge */
static int socket_wire;         /* -s: SWIO on the Sechs socket's pin A */

static int bus_open(int bus);
static int bus_write(uint8_t addr, const uint8_t *d, int n);
static int bus_read(uint8_t addr, uint8_t reg, uint8_t *d, int n);
static void bus_idle(void);     /* give a simulated module time to work */

#ifndef SECHS_SIM

#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int fd = -1;

/* ---- USB bridge (bridge.h): one command line, one answer line ---- */

static int line_timeout = 2;    /* seconds */
static long now_ms(void);

static int bridge_line(char *ans, int max) {
    int n = 0;
    while (n < max - 1) {
        fd_set f;
        struct timeval tv = { line_timeout, 0 };
        FD_ZERO(&f);
        FD_SET(fd, &f);
        if (select(fd + 1, &f, 0, 0, &tv) <= 0) return -1;
        char c;
        if (read(fd, &c, 1) != 1) return -1;
        if (c == '\r') continue;
        if (c == '\n') break;
        ans[n++] = c;
    }
    ans[n] = 0;
    return n;
}

static int bridge_cmd(const char *cmd, char *ans, int max) {
    size_t n = strlen(cmd);
    if (write(fd, cmd, n) != (ssize_t)n) return -1;
    return bridge_line(ans, max);
}

static int bridge_open(void) {
    struct termios t;
    char ans[64];
    fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(device);
        return -1;
    }
    if (!tcgetattr(fd, &t)) {
        cfmakeraw(&t);
        tcsetattr(fd, TCSANOW, &t);
    }
    tcflush(fd, TCIOFLUSH);
    /* resynchronize: end any partial line, ask for the version, and skip
     * stale answers until it arrives */
    if (write(fd, "\nv\n", 3) == 3) {
        for (int i = 0; i < 8 && bridge_line(ans, sizeof(ans)) >= 0; i++)
            if (!strcmp(ans, "ok sechs-bridge 1")) return 0;
    }
    fprintf(stderr, "%s: no Sechs bridge\n", device);
    return -1;
}

static void busy(const char *ans) {
    if (!strcmp(ans, "busy")) {
        fprintf(stderr, "the bridge's pins are in use (by a BASIC program "
            "on the Werkzeug)\n");
        exit(1);
    }
}

static int bridge_write(uint8_t addr, const uint8_t *d, int n) {
    char cmd[8 + 2 * 64], ans[16];
    int k = sprintf(cmd, "w %02x ", addr);
    for (int i = 0; i < n; i++) k += sprintf(cmd + k, "%02x", d[i]);
    strcpy(cmd + k, "\n");
    if (bridge_cmd(cmd, ans, sizeof(ans)) < 0) return -1;
    busy(ans);
    return strcmp(ans, "ok") ? -1 : 0;
}

static int bridge_read(uint8_t addr, uint8_t reg, uint8_t *d, int n) {
    char cmd[32], ans[8 + 2 * 64];
    sprintf(cmd, "r %02x %02x %d\n", addr, reg, n);
    if (bridge_cmd(cmd, ans, sizeof(ans)) < 0) return -1;
    busy(ans);
    if (strncmp(ans, "ok ", 3) ||
        (int)strlen(ans) != 3 + 2 * n) return -1;
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(ans + 3 + 2 * i, "%2x", &v) != 1) return -1;
        d[i] = v;
    }
    return 0;
}

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

/* Send a command for the programming wires and print what the bridge
 * reports ("status ..." lines, progress) until its final answer, with the
 * time it took. 0 if the answer was "ok". */
static int bridge_job(const char *cmd) {
    char ans[160];
    long t0 = now_ms();
    line_timeout = 60;              /* programming takes some seconds */
    if (strchr("itp", cmd[0])) {    /* jobs on the wire: choose it first */
        const char *c = socket_wire ? "c 1\n" : "c 0\n";
        if (write(fd, c, 4) < 0 || bridge_line(ans, sizeof(ans)) < 0) return 1;
        if (strncmp(ans, "ok", 2) && socket_wire) {
            fprintf(stderr, "this Werkzeug firmware cannot program through the socket"
                    " (update it)\n");
            return 1;
        }
    }
    if (write(fd, cmd, strlen(cmd)) < 0) return 1;
    for (;;) {
        if (bridge_line(ans, sizeof(ans)) < 0) {
            fprintf(stderr, "no answer from the bridge\n");
            return 1;
        }
        if (!strncmp(ans, "progress ", 9)) {
            fprintf(stderr, "\r  %s%%  ", ans + 9);
            continue;
        }
        if (!strncmp(ans, "status ", 7)) {
            fprintf(stderr, "\r  %s\n", ans + 7);
            continue;
        }
        busy(ans);
        printf("%s\n", ans);
        fprintf(stderr, "  (%.1f s)\n", (now_ms() - t0) / 1000.0);
        return strncmp(ans, "ok", 2) ? 1 : 0;
    }
}

/* send a firmware image, then program it; the bridge checks it before
 * touching the module */
static int bridge_flash(const char *path, int force) {
    static uint8_t img[32769];
    char cmd[160], ans[160];
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 2;
    }
    size_t n = fread(img, 1, sizeof(img), f);
    fclose(f);
    if (n == 0 || n > 32768) {
        fprintf(stderr, "%s: not a module firmware (%zu bytes; at most 32768)\n", path, n);
        return 2;
    }
    sprintf(cmd, "f %zu %08x\n", n, crc32(img, n));
    if (bridge_cmd(cmd, ans, sizeof(ans)) < 0 || strcmp(ans, "ok")) {
        fprintf(stderr, "the bridge does not take images (%s)\n", ans);
        return 1;
    }
    for (size_t i = 0; i < n; i += 64) {
        int k = sprintf(cmd, "d ");
        for (size_t j = i; j < n && j < i + 64; j++) k += sprintf(cmd + k, "%02x", img[j]);
        strcpy(cmd + k, "\n");
        if (bridge_cmd(cmd, ans, sizeof(ans)) < 0 || strcmp(ans, "ok")) {
            fprintf(stderr, "sending the image failed\n");
            return 1;
        }
    }
    return bridge_job(force ? "p force\n" : "p\n");
}

static int uart_relay(int tty);

/* the module's UART console through the bridge, until end of input */
static int bridge_uart(long baud) {
    char cmd[32], ans[16];
    sprintf(cmd, "u %ld\n", baud);
    if (bridge_cmd(cmd, ans, sizeof(ans)) < 0) return 1;
    busy(ans);
    if (strcmp(ans, "ok")) {
        fprintf(stderr, "the bridge refused %ld baud\n", baud);
        return 1;
    }
    fprintf(stderr, "sechsctl " __DATE__ " " __TIME__ "; UART console at %ld baud; press Enter to wake the "
        "module; Ctrl-C stops a program; end with Ctrl-D\r\n", baud);
    /* A terminal goes raw while relaying: the module echoes (so no local
     * echo), and Ctrl-C reaches it. Ctrl-D ends. */
    struct termios saved, raw;
    int tty = isatty(0) && !tcgetattr(0, &saved);
    if (tty) {
        raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~(ICRNL | IXON);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    int r = uart_relay(tty);
    if (tty) {
        tcsetattr(0, TCSANOW, &saved);
        fprintf(stderr, "\n");
    }
    return r;
}

static int uart_relay(int tty) {
    for (;;) {
        fd_set f;
        FD_ZERO(&f);
        FD_SET(0, &f);
        FD_SET(fd, &f);
        if (select(fd + 1, &f, 0, 0, 0) < 0) return 1;
        char b[256];
        if (FD_ISSET(fd, &f)) {
            ssize_t n = read(fd, b, sizeof(b));
            if (n <= 0) return 1;
            if (write(1, b, n) != n) return 1;
        }
        if (FD_ISSET(0, &f)) {
            ssize_t n = read(0, b, sizeof(b));
            if (n <= 0) return 0;
            for (ssize_t i = 0; i < n; i++) {
                if (tty && b[i] == 0x04) return 0;      /* Ctrl-D */
                if (b[i] == '\n') b[i] = '\r';
            }
            if (write(fd, b, n) != n) return 1;
        }
    }
}

/* ---- Linux I2C bus ---- */

static int bus_open(int bus) {
    char path[32];
    if (device) return bridge_open();
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    fd = open(path, O_RDWR);
    if (fd < 0) perror(path);
    return fd < 0 ? -1 : 0;
}

static int xfer(struct i2c_msg *m, int n) {
    struct i2c_rdwr_ioctl_data d = { m, n };
    return ioctl(fd, I2C_RDWR, &d) == n ? 0 : -1;
}

static int bus_write(uint8_t addr, const uint8_t *d, int n) {
    if (device) return bridge_write(addr, d, n);
    struct i2c_msg m = { addr, 0, (uint16_t)n, (uint8_t *)d };
    return xfer(&m, 1);
}

/* register read: write the register, repeated start, read */
static int bus_read(uint8_t addr, uint8_t reg, uint8_t *d, int n) {
    if (device) return bridge_read(addr, reg, d, n);
    struct i2c_msg m[2] = {
        { addr, 0, 1, &reg },
        { addr, I2C_M_RD, (uint16_t)n, d },
    };
    return xfer(m, 2);
}

static void bus_idle(void) {
    usleep(device ? 1000 : 2000);
}

#else
#include "sim.h"
#endif

/* ---- protocol: tools/sechs/sechsm.c, over this program's bus -------------- */

static int t_write(void *c, uint8_t addr, const uint8_t *d, int n) {
    (void)c;
    return bus_write(addr, d, n);
}

static int t_read(void *c, uint8_t addr, uint8_t reg, uint8_t *d, int n) {
    (void)c;
    return bus_read(addr, reg, d, n);
}

static void t_idle(void *c) {
    (void)c;
    bus_idle();
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static long t_ms(void *c) {
    (void)c;
    return now_ms();
}

static void t_out(void *c, char ch) {
    (void)c;
    fputc(ch, stdout);
    if (ch == '\n') fflush(stdout);
}

static const sm_bus B = { t_write, t_read, t_idle, t_ms, t_out, NULL };

static void print_status(int s) {
    for (int i = 0; i < 7; i++) if (s & (1 << i)) printf(" %s", sm_status_names[i]);
}

static int info(uint8_t addr) {
    sm_info_t i;
    if (sm_info(&B, addr, &i)) {
        fprintf(stderr, "no Sechs module at 0x%02x\n", addr);
        return 1;
    }
    printf("address 0x%02x\nversion %d.%d\ncaps    0x%02x\nstatus ",
        addr, i.version >> 4, i.version & 15, i.caps);
    print_status(i.status);
    printf("\nok      0x%02x\nfault   %d\n", i.ok, i.fault);
    fputs(i.info, stdout);
    return 0;
}

static int scan(void) {
    uint8_t found[112];
    int n = sm_scan(&B, found, sizeof(found));
    for (int k = 0; k < n; k++) {
        printf("0x%02x", found[k]);
        print_status(sm_reg(&B, found[k], SR_STATUS));
        printf("\n");
    }
    if (!n) printf("no modules found\n");
    return 0;
}

/* lines from in, typed into the console one at a time */
static int lines(uint8_t addr, FILE *in) {
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        if (sm_line(&B, addr, line, strcspn(line, "\r\n"))) {
            fprintf(stderr, "the module at 0x%02x stopped answering\n", addr);
            return 2;
        }
        fflush(stdout);
    }
    return 0;
}

static int send(uint8_t addr, FILE *in) {
    int r = lines(addr, in);
    if (r) return r;
    if (sm_last_ok(&B, addr) != 1) {
        fprintf(stderr, "the last command failed\n");
        return 1;
    }
    return 0;
}

static int console(uint8_t addr) {
    fprintf(stderr, "sechsctl " __DATE__ " " __TIME__ "; console on 0x%02x; end with Ctrl-D\n", addr);
    return lines(addr, stdin);
}

static long num(const char *s) {
    char *e;
    long v = strtol(s, &e, 0);
    if (*e) {
        fprintf(stderr, "not a number: %s\n", s);
        exit(2);
    }
    return v;
}

static int usage(void) {
    fprintf(stderr,
        "sechsctl " __DATE__ " " __TIME__ "\n"
        "usage: sechsctl [-b BUS | -d DEV [-s]] scan | info ADDR | halt|run|reset|program ADDR |\n"
        "                addr ADDR NEW | reg ADDR N [VALUE] | console ADDR |\n"
        "                send ADDR [FILE] | uart BAUD (with -d) |\n"
        "                flash FILE [force] | swio-id | swio-test [N] (with -d)\n");
    return 2;
}

int main(int argc, char **argv) {
    int bus = 1, i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-s")) {   /* SWIO on the socket's pin A */
            socket_wire = 1;
            i++;
            continue;
        }
        if (i + 1 >= argc) return usage();
        if (!strcmp(argv[i], "-b")) bus = num(argv[i + 1]);
        else if (!strcmp(argv[i], "-d")) device = argv[i + 1];
        else return usage();
        i += 2;
    }
    if (i >= argc) return usage();
    const char *cmd = argv[i++];
    if (bus_open(bus)) return 2;
    if (!strcmp(cmd, "scan")) return scan();
#ifndef SECHS_SIM
    if (!strcmp(cmd, "uart") && device && i < argc) return bridge_uart(num(argv[i]));
    if (!strcmp(cmd, "flash") && device && i < argc)
        return bridge_flash(argv[i], i + 1 < argc && !strcmp(argv[i + 1], "force"));
    if (!strcmp(cmd, "swio-id") && device) return bridge_job("i\n");
    if (!strcmp(cmd, "swio-timing") && device) {
        char c[96] = "s";
        for (int k = 0; k < 6 && i + k < argc; k++)
            snprintf(c + strlen(c), sizeof(c) - strlen(c), " %ld", num(argv[i + k]));
        strcat(c, "\n");
        return bridge_job(c);
    }
    if (!strcmp(cmd, "swio-test") && device) {
        char c[32];
        snprintf(c, sizeof(c), "t %ld\n", i < argc ? num(argv[i]) : 1000L);
        return bridge_job(c);
    }
#endif
    if (i >= argc) return usage();
    uint8_t addr = num(argv[i++]);

    if (!strcmp(cmd, "info")) return info(addr);
    if (!strcmp(cmd, "halt") || !strcmp(cmd, "run") || !strcmp(cmd, "reset") ||
        !strcmp(cmd, "program")) {
        uint8_t c = cmd[0] == 'h' ? CMD_HALT : cmd[0] == 'p' ? CMD_PROGRAM :
            cmd[1] == 'u' ? CMD_RUN : CMD_RESET;
        int r = sm_control(&B, addr, c);
        if (r == -2) fprintf(stderr, "the module at 0x%02x has no programming mode\n", addr);
        return r ? 1 : 0;
    }
    if (!strcmp(cmd, "addr") && i < argc) {
        long n = num(argv[i]);
        if (n < 0x08 || n > 0x77) {
            fprintf(stderr, "addresses are 0x08-0x77\n");
            return 2;
        }
        if (sm_set_address(&B, addr, n)) {
            fprintf(stderr, "address not changed\n");
            return 1;
        }
        return 0;
    }
    if (!strcmp(cmd, "reg") && i < argc) {
        long n = num(argv[i++]);
        if (n < 0 || n > 15) return usage();
        if (i < argc) {
            uint8_t v = num(argv[i]);
            return sm_reg_write(&B, addr, SR_REG + n, &v, 1) ? 1 : 0;
        }
        int v = sm_reg(&B, addr, SR_REG + n);
        if (v < 0) return 1;
        printf("%d\n", v);
        return 0;
    }
    if (!strcmp(cmd, "console")) return console(addr);
    if (!strcmp(cmd, "send")) {
        FILE *in = i < argc ? fopen(argv[i], "r") : stdin;
        if (!in) {
            perror(argv[i]);
            return 2;
        }
        return send(addr, in);
    }
    return usage();
}
