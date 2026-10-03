/*
 * sechsctl: a Sechs controller for Linux.
 *
 *   sechsctl [-b BUS] scan                  list modules on the bus
 *   sechsctl [-b BUS] info ADDR             identity and status
 *   sechsctl [-b BUS] halt|run|reset ADDR   CONTROL
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

/* ---- transport ------------------------------------------------------------ */

static const char *device;      /* -d: a USB bridge */

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

/* send a firmware image, then program it; the bridge checks it before
 * touching the module */
static int bridge_flash(const char *path, int force) {
    static uint8_t img[16385];
    char cmd[160], ans[160];
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 2;
    }
    size_t n = fread(img, 1, sizeof(img), f);
    fclose(f);
    if (n == 0 || n > 16384) {
        fprintf(stderr, "%s: not a CH32V003 firmware (%zu bytes)\n", path, n);
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
    line_timeout = 60;              /* programming takes some seconds */
    if (write(fd, force ? "p force\n" : "p\n", force ? 8 : 2) < 0) return 1;
    for (;;) {
        if (bridge_line(ans, sizeof(ans)) < 0) {
            fprintf(stderr, "no answer from the bridge\n");
            return 1;
        }
        if (!strncmp(ans, "progress ", 9)) {
            fprintf(stderr, "\r%s%%", ans + 9);
            continue;
        }
        fprintf(stderr, "\r");
        busy(ans);
        printf("%s\n", ans);
        return strncmp(ans, "ok", 2) ? 1 : 0;
    }
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

/* ---- protocol ---------------------------------------------------------------- */

static int reg_write(uint8_t addr, uint8_t reg, const uint8_t *d, int n) {
    uint8_t b[64];
    b[0] = reg;
    memcpy(b + 1, d, n);
    return bus_write(addr, b, n + 1);
}

static int reg1(uint8_t addr, uint8_t reg) {
    uint8_t v;
    return bus_read(addr, reg, &v, 1) ? -1 : v;
}

static int is_module(uint8_t addr) {
    uint8_t s[2];
    return !bus_read(addr, SR_SIG0, s, 2) && s[0] == 'S' && s[1] == '6';
}

static void print_status(int s) {
    static const char *bits[] = {
        "boot", "halted", "running", "console", "networked", "fault",
        "degraded"
    };
    for (int i = 0; i < 7; i++) if (s & (1 << i)) printf(" %s", bits[i]);
}

static int info(uint8_t addr) {
    uint8_t r[7];
    char text[128];
    int n = 0;
    if (bus_read(addr, SR_SIG0, r, 7) || r[0] != 'S' || r[1] != '6') {
        fprintf(stderr, "no Sechs module at 0x%02x\n", addr);
        return 1;
    }
    printf("address 0x%02x\nversion %d.%d\ncaps    0x%02x\nstatus ",
        addr, r[2] >> 4, r[2] & 15, r[3]);
    print_status(r[4]);
    printf("\nok      0x%02x\nfault   %d\n", r[5], r[6]);
    /* INFO: one read (64 bytes, the most a bridge reads at once), up to
     * its terminating zero */
    if (bus_read(addr, SR_INFO, (uint8_t *)text, 64)) n = 0;
    else while (n < (int)sizeof(text) - 1 && text[n]) n++;
    fputs(text, stdout);
    return 0;
}

static int scan(void) {
    int found = 0;
    for (int a = 0x08; a <= 0x77; a++) {
        if (!is_module(a)) continue;
        int s = reg1(a, SR_STATUS);
        printf("0x%02x", a);
        print_status(s);
        printf("\n");
        found++;
    }
    if (!found) printf("no modules found\n");
    return 0;
}

/* everything the console has printed so far */
static int drain(uint8_t addr, FILE *out) {
    int total = 0, n;
    while ((n = reg1(addr, SR_COUT)) > 0) {
        uint8_t b[64];
        if (n > 64) n = 64;
        if (bus_read(addr, SR_CDATA, b, n)) return -1;
        for (int i = 0; i < n; i++) if (b[i] != '\r') fputc(b[i], out);
        total += n;
    }
    fflush(out);
    return total;
}

/* type text into the console, never more than the module can take */
static int type(uint8_t addr, const char *s, int len, FILE *out) {
    while (len > 0) {
        int room = reg1(addr, SR_CIN);
        if (room < 0) return -1;
        int n = room < len ? room : len;
        if (n > 32) n = 32;
        if (n && reg_write(addr, SR_CDATA, (const uint8_t *)s, n)) return -1;
        s += n;
        len -= n;
        bus_idle();
        drain(addr, out);
    }
    return 0;
}

/* wait until the module has printed nothing for 300 ms (a module may
 * take a moment to start answering: a LOAD, a program that waits) */
static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static void settle(uint8_t addr, FILE *out) {
    long quiet_since = now_ms();
    while (now_ms() - quiet_since < 300) {
        bus_idle();
        if (drain(addr, out) > 0) quiet_since = now_ms();
    }
}

static int send(uint8_t addr, FILE *in) {
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        size_t n = strcspn(line, "\r\n");
        line[n++] = '\r';
        if (type(addr, line, n, stdout)) {
            fprintf(stderr, "the module at 0x%02x stopped answering\n", addr);
            return 2;
        }
        settle(addr, stdout);
    }
    int ok = reg1(addr, SR_OK);
    if (ok < 0 || !(ok & 0x10)) {
        fprintf(stderr, "the last command failed\n");
        return 1;
    }
    return 0;
}

static int console(uint8_t addr) {
    char line[256];
    fprintf(stderr, "sechsctl " __DATE__ " " __TIME__ "; console on 0x%02x; end with Ctrl-D\n", addr);
    while (fgets(line, sizeof(line), stdin)) {
        size_t n = strcspn(line, "\r\n");
        line[n++] = '\r';
        if (type(addr, line, n, stdout)) {
            fprintf(stderr, "the module at 0x%02x stopped answering\n", addr);
            return 2;
        }
        settle(addr, stdout);
    }
    return 0;
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
        "usage: sechsctl [-b BUS | -d DEV] scan | info ADDR | halt|run|reset ADDR |\n"
        "                addr ADDR NEW | reg ADDR N [VALUE] | console ADDR |\n"
        "                send ADDR [FILE] | uart BAUD (with -d) |\n"
        "                flash FILE [force] (with -d)\n");
    return 2;
}

int main(int argc, char **argv) {
    int bus = 1, i = 1;
    while (i + 1 < argc && argv[i][0] == '-') {
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
#endif
    if (i >= argc) return usage();
    uint8_t addr = num(argv[i++]);

    if (!strcmp(cmd, "info")) return info(addr);
    if (!strcmp(cmd, "halt") || !strcmp(cmd, "run") || !strcmp(cmd, "reset")) {
        uint8_t c = cmd[0] == 'h' ? CMD_HALT : cmd[1] == 'u' ? CMD_RUN : CMD_RESET;
        return reg_write(addr, SR_CONTROL, &c, 1) ? 1 : 0;
    }
    if (!strcmp(cmd, "addr") && i < argc) {
        long n = num(argv[i]);
        if (n < 0x08 || n > 0x77) {
            fprintf(stderr, "addresses are 0x08-0x77\n");
            return 2;
        }
        uint8_t b[2] = { (uint8_t)n, (uint8_t)~n };
        if (reg_write(addr, SR_ADDR, b, 2) || !is_module(n)) {
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
            return reg_write(addr, SR_REG + n, &v, 1) ? 1 : 0;
        }
        int v = reg1(addr, SR_REG + n);
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
