/*
 * sechsm: the master side of Sechs, independent of how the bus is reached
 * (sechsm.h). The protocol is docs/sechs.md.
 */

#include <string.h>
#include "sechsm.h"

const char *const sm_status_names[7] = {
    "boot", "halted", "running", "console", "networked", "fault", "degraded"
};

int sm_reg_write(const sm_bus *b, uint8_t addr, uint8_t reg, const uint8_t *d, int n) {
    uint8_t buf[65];
    if (n < 0 || n > 64) return -1;
    buf[0] = reg;
    memcpy(buf + 1, d, n);
    return b->write(b->ctx, addr, buf, n + 1);
}

int sm_reg(const sm_bus *b, uint8_t addr, uint8_t reg) {
    uint8_t v;
    return b->read(b->ctx, addr, reg, &v, 1) ? -1 : v;
}

int sm_is_module(const sm_bus *b, uint8_t addr) {
    uint8_t s[2];
    return !b->read(b->ctx, addr, SR_SIG0, s, 2) && s[0] == 'S' && s[1] == '6';
}

int sm_info(const sm_bus *b, uint8_t addr, sm_info_t *i) {
    uint8_t r[7];
    int n = 0;
    if (b->read(b->ctx, addr, SR_SIG0, r, 7) || r[0] != 'S' || r[1] != '6') return -1;
    i->version = r[2];
    i->caps = r[3];
    i->status = r[4];
    i->ok = r[5];
    i->fault = r[6];
    /* INFO: one read of 64 bytes (the most a bridge reads at once), up to
     * its terminating zero */
    if (!b->read(b->ctx, addr, SR_INFO, (uint8_t *)i->info, SM_INFO_MAX))
        while (n < SM_INFO_MAX && i->info[n]) n++;
    i->info[n] = 0;
    return 0;
}

int sm_scan(const sm_bus *b, uint8_t *found, int max) {
    int n = 0;
    for (int a = 0x08; a <= 0x77 && n < max; a++)
        if (sm_is_module(b, a)) found[n++] = a;
    return n;
}

int sm_control(const sm_bus *b, uint8_t addr, uint8_t cmd) {
    if (cmd == CMD_PROGRAM) {
        int caps = sm_reg(b, addr, SR_CAPS);
        if (caps < 0) return -1;
        if (!(caps & CAP_PROGRAM)) return -2;
    }
    return sm_reg_write(b, addr, SR_CONTROL, &cmd, 1) ? -1 : 0;
}

int sm_set_address(const sm_bus *b, uint8_t addr, uint8_t new_addr) {
    uint8_t d[2] = { new_addr, (uint8_t)~new_addr };
    if (new_addr < 0x08 || new_addr > 0x77) return -1;
    if (sm_reg_write(b, addr, SR_ADDR, d, 2) || !sm_is_module(b, new_addr)) return -2;
    return 0;
}

int sm_drain(const sm_bus *b, uint8_t addr) {
    int total = 0, n;
    while ((n = sm_reg(b, addr, SR_COUT)) > 0) {
        uint8_t d[64];
        if (n > 64) n = 64;
        if (b->read(b->ctx, addr, SR_CDATA, d, n)) return -1;
        for (int i = 0; i < n; i++) if (d[i] != '\r') b->out(b->ctx, d[i]);
        total += n;
    }
    return total;
}

int sm_type(const sm_bus *b, uint8_t addr, const char *s, int len) {
    while (len > 0) {
        int room = sm_reg(b, addr, SR_CIN);
        if (room < 0) return -1;
        int n = room < len ? room : len;
        if (n > 32) n = 32;
        if (n && sm_reg_write(b, addr, SR_CDATA, (const uint8_t *)s, n)) return -1;
        s += n;
        len -= n;
        b->idle(b->ctx);
        sm_drain(b, addr);
    }
    return 0;
}

void sm_settle(const sm_bus *b, uint8_t addr, long quiet_ms) {
    long quiet_since = b->ms(b->ctx);
    while (b->ms(b->ctx) - quiet_since < quiet_ms) {
        b->idle(b->ctx);
        if (sm_drain(b, addr) > 0) quiet_since = b->ms(b->ctx);
    }
}

int sm_line(const sm_bus *b, uint8_t addr, const char *s, int len) {
    char cr = '\r';
    if (sm_type(b, addr, s, len) || sm_type(b, addr, &cr, 1)) return -1;
    sm_settle(b, addr, 300);
    return 0;
}

int sm_last_ok(const sm_bus *b, uint8_t addr) {
    int ok = sm_reg(b, addr, SR_OK);
    return ok < 0 ? -1 : (ok & 0x10) ? 1 : 0;
}
