/*
 * Sechs core: the I2C target protocol of a Sechs module. See sechs.h.
 *
 * Register access:
 *   write:  START addr+W reg data...                  STOP
 *   read:   START addr+W reg RESTART addr+R data...   STOP
 * The register number advances after each byte, except for the port
 * registers INFO and CDATA. Broadcasts (general call) are not implemented;
 * modules sharing one address all receive the same writes.
 */

#include "sechs.h"

volatile sechs_t sechs;

/* used only by the interrupt handler */
static uint8_t reg;                 /* register pointer */
static uint8_t first;               /* next byte written is the register */
static uint8_t gc;                  /* this transaction is a broadcast */
static uint8_t info_i;              /* position in INFO */
static uint8_t addr_n, addr_v;      /* ADDR bytes received */
static uint8_t out_tx;              /* CDATA bytes handed out, not committed */
static uint8_t filler;              /* the last byte handed out was not output */

/* shared with the main loop */
static volatile uint8_t in_buf[SECHS_IN_SIZE], out_buf[SECHS_OUT_SIZE];
static volatile uint8_t in_r, in_w, out_r, out_w;

void sechs_init(uint8_t addr, uint8_t caps) {
    sechs.r[SR_SIG0] = 'S';
    sechs.r[SR_SIG1] = '6';
    sechs.r[SR_VER] = SECHS_VERSION;
    sechs.r[SR_CAPS] = caps;
    sechs.r[SR_OK] = 1;
    /* never an address outside 0x08-0x77 (a damaged one in storage) */
    sechs.addr = (uint8_t)(addr - 0x08) <= 0x77 - 0x08 ? addr : SECHS_DEFAULT_ADDR;
}

#define IN_COUNT()  ((uint8_t)(in_w - in_r))
#define OUT_COUNT() ((uint8_t)(out_w - out_r))

void sechs_start(uint8_t general_call) {
    out_tx = 0;
    filler = 1;
    first = 1;
    gc = general_call;
    if (!gc) sechs.networked = 1;
}

static void command(uint8_t c) {
    if (c >= CMD_HALT && c <= CMD_RESET) sechs.cmd = c;
#ifdef SECHS_PROGRAM
    if (c == CMD_PROGRAM) sechs.cmd = c;
#endif
}

void sechs_rx(uint8_t b) {
    if (gc) return;     /* broadcasts are optional; not implemented */
    if (first) {
        reg = b;
        first = 0;
        if (reg == SR_INFO) info_i = 0;
        if (reg == SR_ADDR) addr_n = 0;
        return;
    }
    switch (reg) {
        case SR_CONTROL:
            command(b);
            break;
        case SR_ADDR:
            /* new address, then its complement */
            if (addr_n == 0) addr_v = b;
            else if (addr_n == 1 && (uint8_t)~b == addr_v &&
                     addr_v >= 0x08 && addr_v <= 0x77) sechs.new_addr = addr_v;
            addr_n++;
            return;     /* two bytes at one register: no advance */
        case SR_CDATA:
            sechs.con_active = 1;
            if (b == 0x03) sechs.con_break = 1;
            else if (IN_COUNT() < SECHS_IN_SIZE)
                in_buf[in_w++ & (SECHS_IN_SIZE - 1)] = b;
            return;     /* a port: no advance */
        default:
            if (reg >= SR_REG && reg < SR_REG + 16)
                sechs_regs()[reg - SR_REG] = b;
    }
    reg++;
}

uint8_t sechs_tx(void) {
    uint8_t v = 0, r = reg;
    if (r <= SR_FAULT) {
        v = sechs.r[r];
    } else if (r == SR_INFO) {
        v = sechs_info(info_i);
        if (v) info_i++;
        return v;   /* a port: no advance */
    } else if (r == SR_CIN) {
        v = SECHS_IN_SIZE - IN_COUNT();
    } else if (r == SR_COUT) {
        v = OUT_COUNT();
    } else if (r == SR_CDATA) {
        /* Removed from the buffer only when the transfer ends: the hardware
         * loads one byte ahead, and the last one loaded is never sent.
         * Beyond the bytes waiting, 0 (a filler, not counted). */
        filler = out_tx >= OUT_COUNT();
        if (!filler) v = out_buf[(out_r + out_tx++) & (SECHS_OUT_SIZE - 1)];
        return v;   /* a port: no advance */
    } else if (r >= SR_REG && r < SR_REG + 16) {
        v = sechs_regs()[r - SR_REG];
    }
    reg++;
    return v;
}

void sechs_stop(uint8_t unsent) {
    /* the bytes the master took: those handed out, less the one
     * loaded ahead and never sent, unless that one was a filler */
    if (unsent && !filler && out_tx) out_tx--;
    out_r += out_tx;
    out_tx = 0;
    if (sechs.new_addr) {
        sechs.addr = sechs.new_addr;
        sechs.new_addr = 0;
        sechs_set_addr(sechs.addr);
    }
}

int sechs_getc(void) {
    if (!IN_COUNT()) return -1;
    return in_buf[in_r++ & (SECHS_IN_SIZE - 1)];
}

/* Console output for the master. (Only the interrupt handler moves
 * out_r; this only moves out_w.) */
void sechs_putc(char c) {
    /* full: wait for the master to read (255 sechs_wait calls, about
     * half a second); if nobody reads, the console is left: no more output
     * for it until the master types again (con_active) */
    for (uint8_t t = 1; OUT_COUNT() >= SECHS_OUT_SIZE; t++) {
        if (!t) {
            sechs.con_active = 0;
            return;
        }
        sechs_wait();
    }
    out_buf[out_w++ & (SECHS_OUT_SIZE - 1)] = c;
}
