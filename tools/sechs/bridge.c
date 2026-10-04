/* USB bridge protocol for a Sechs programmer. See bridge.h. */

#include <string.h>
#include "bridge.h"

static char line[160];
static uint8_t len;

/* the image being received for programming (f, d, p) */
static uint8_t img[BRIDGE_IMAGE_MAX];
static uint32_t img_len, img_want, img_crc;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex byte at *p; -1 if not hex */
static int hexbyte(const char **p) {
    int h = hexval((*p)[0]), l = h < 0 ? -1 : hexval((*p)[1]);
    if (l < 0) return -1;
    *p += 2;
    return (h << 4) | l;
}

static const char *skip(const char *p) {
    while (*p == ' ') p++;
    return p;
}

static void answer(const char *prefix, const uint8_t *d, uint8_t n) {
    static const char hex[] = "0123456789abcdef";
    char out[2 * 64 + 8];
    uint8_t k = 0;
    while (*prefix) out[k++] = *prefix++;
    if (n) out[k++] = ' ';
    for (uint8_t i = 0; i < n; i++) {
        out[k++] = hex[d[i] >> 4];
        out[k++] = hex[d[i] & 15];
    }
    out[k++] = '\n';
    out[k] = 0;
    bridge_puts(out);
}

static void command(void) {
    const char *p = skip(line + 1);
    uint8_t d[64];
    int a, r = 0, n = 0;

    switch (line[0]) {
        case 'w':
            if ((a = hexbyte(&p)) < 0) break;
            p = skip(p);
            while (*p && n < 64) {
                if ((r = hexbyte(&p)) < 0) break;
                d[n++] = r;
            }
            if (*p) break;
            r = bridge_i2c_write(a, d, n);
            answer(r == -2 ? "busy" : r ? "nack" : "ok", 0, 0);
            return;
        case 'r':
            if ((a = hexbyte(&p)) < 0) break;
            p = skip(p);
            if ((r = hexbyte(&p)) < 0) break;
            p = skip(p);
            while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
            if (*p || n < 1 || n > 64) break;
            r = bridge_i2c_read(a, r, d, n);
            if (r) answer(r == -2 ? "busy" : "nack", 0, 0);
            else answer("ok", d, n);
            return;
        case 'u': {
            long baud = 0;
            while (*p >= '0' && *p <= '9') baud = baud * 10 + (*p++ - '0');
            if (*p || (baud != 9600 && baud != 115200)) break;
            if (bridge_uart(baud)) {
                answer("busy", 0, 0);
                return;
            }
            answer("ok", 0, 0);
            return;
        }
        case 'v':
            if (*p) break;
            bridge_puts("ok sechs-bridge 1\n");
            return;
        case 'f': {     /* f N CRC: an image of N bytes follows */
            uint32_t want = 0, crc = 0;
            while (*p >= '0' && *p <= '9') want = want * 10 + (*p++ - '0');
            p = skip(p);
            for (int k = 0; k < 4; k++) {
                if ((r = hexbyte(&p)) < 0) break;
                crc = (crc << 8) | r;
            }
            if (r < 0 || *p || !want || want > BRIDGE_IMAGE_MAX) break;
            img_want = want;
            img_crc = crc;
            img_len = 0;
            answer("ok", 0, 0);
            return;
        }
        case 'd':       /* d HEX: the next bytes of the image */
            while (*p && img_len < img_want) {
                if ((r = hexbyte(&p)) < 0) break;
                img[img_len++] = r;
            }
            if (r < 0 || *p) break;
            answer("ok", 0, 0);
            return;
        case 'i':       /* identify the module on the programming wires */
            if (*p) break;
            bridge_identify();
            return;
        case 't': {     /* t N: test the programming wires */
            uint32_t tn = 0;
            while (*p >= '0' && *p <= '9' && tn <= 100000) tn = tn * 10 + (*p++ - '0');
            if (*p || tn < 1 || tn > 100000) break;
            bridge_link_test(tn);
            return;
        }
        case 'c':       /* c 0 | c 1: the programming wire */
            if ((p[0] == '0' || p[0] == '1') && !p[1]) {
                bridge_swio_socket(p[0] == '1');
                return;
            }
            break;
        case 's': {     /* s [A B C D E F]: the SWIO timing */
            uint32_t v[6];
            int k = 0;
            while (*p && k < 6) {
                v[k] = 0;
                if (*p < '0' || *p > '9') break;
                while (*p >= '0' && *p <= '9') v[k] = v[k] * 10 + (*p++ - '0');
                k++;
                p = skip(p);
            }
            if (*p || (k != 0 && k != 6)) break;
            bridge_swio_timing(v, k);
            return;
        }
        case 'p': {     /* p [force]: program the module */
            int force = !strcmp(p, "force");
            if (*p && !force) break;
            if (!img_want || img_len != img_want || crc32(img, img_len) != img_crc) {
                bridge_puts("fail the image is incomplete or its CRC is wrong\n");
                return;
            }
            bridge_prog(img, img_len, force);   /* answers itself */
            img_want = 0;
            return;
        }
    }
    answer("error", 0, 0);
}

void bridge_char(char c) {
    if (c == '\r') return;
    if (c == '\n') {
        uint8_t n = len;
        line[len] = 0;
        len = 0;            /* first, so that a command can take input */
        if (n) command();
    } else if (len < sizeof(line) - 1) {
        line[len++] = c;
    }
}
