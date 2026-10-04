/*
 * LS11: the I2C EEPROM (AT24C64) as the filesystem's media
 * (fs_media_read/fs_media_prog), bit-banged on two pins of its own, never
 * on the Sechs bus.
 *
 * SDA is an open-drain output with a 10k pull-up on the board. SCL is
 * driven during a transfer (the EEPROM never stretches the clock) and
 * released, with the internal pull-up, between transfers, so that a clip
 * reader can use the bus. WP is driven high (protected) except while
 * writing, when it is released: the EEPROM pulls it down (writable). It is
 * never driven low.
 *
 * Writes are split at the 32-byte pages; after each page the EEPROM is
 * busy for up to 5 ms and answers its address again when done
 * (acknowledge polling).
 */

#include "ch32fun.h"
#include "board.h"
#include "../../fs/fs.h"

void storage_init(void);

static void cfg(GPIO_TypeDef *port, uint8_t bit, uint32_t c) {
    port->CFGLR = (port->CFGLR & ~(0xfu << (4 * bit))) | (c << (4 * bit));
}

static void sda(int high) {
    EE_SDA_PORT->BSHR = 1u << (EE_SDA + (high ? 0 : 16));
    Delay_Us(2);
}

static void scl(int high) {
    EE_SCL_PORT->BSHR = 1u << (EE_SCL + (high ? 0 : 16));
    Delay_Us(2);
}

static int sda_in(void) {
    return (EE_SDA_PORT->INDR >> EE_SDA) & 1;
}

static void bus_on(void) {
    EE_SDA_PORT->BSHR = 1u << EE_SDA;                       // released
    cfg(EE_SDA_PORT, EE_SDA, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD);
    EE_SCL_PORT->BSHR = 1u << EE_SCL;                       // high
    cfg(EE_SCL_PORT, EE_SCL, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
}

static void bus_off(void) {
    cfg(EE_SCL_PORT, EE_SCL, GPIO_CNF_IN_PUPD);   // released, pulled up
    EE_SCL_PORT->BSHR = 1u << EE_SCL;
}

static void wp(int protect) {
    if (protect) {
        EE_WP_PORT->BSHR = 1u << EE_WP;
        cfg(EE_WP_PORT, EE_WP, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    } else {
        cfg(EE_WP_PORT, EE_WP, GPIO_CNF_IN_FLOATING);
    }
}

static void start(void) {
    sda(1);
    scl(1);
    sda(0);
    scl(0);
}

static void stop(void) {
    sda(0);
    scl(1);
    sda(1);
}

// one byte out; 0 if the EEPROM acknowledged it
static int put(uint8_t v) {
    for (int i = 0; i < 8; i++, v <<= 1) {
        sda(v & 0x80);
        scl(1);
        scl(0);
    }
    sda(1);
    scl(1);
    int nack = sda_in();
    scl(0);
    return nack ? -1 : 0;
}

// one byte in; acknowledged unless it is the last
static uint8_t get(int ack) {
    uint8_t v = 0;
    sda(1);
    for (int i = 0; i < 8; i++) {
        scl(1);
        v = (v << 1) | sda_in();
        scl(0);
    }
    sda(!ack);
    scl(1);
    scl(0);
    sda(1);
    return v;
}

static int address(uint32_t addr) {
    start();
    if (put(EE_ADDR << 1)) return -1;
    if (put(addr >> 8)) return -1;
    return put(addr & 0xff);
}

void storage_init(void) {
    wp(1);
    bus_on();
    // a transfer cut short may have left the EEPROM holding SDA low:
    // clock it out, then a stop
    for (int i = 0; i < 9 && !sda_in(); i++) {
        scl(1);
        scl(0);
    }
    stop();
    bus_off();
}

int fs_media_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    if (addr + len > EE_SIZE) return -1;
    bus_on();
    int r = address(addr);
    if (!r) {
        start();
        r = put((EE_ADDR << 1) | 1);
        for (uint16_t i = 0; !r && i < len; i++) buf[i] = get(i + 1 < len);
    }
    stop();
    bus_off();
    return r ? -1 : 0;
}

int fs_media_prog(uint32_t addr, const uint8_t *buf, uint16_t len) {
    if (addr + len > EE_SIZE) return -1;
    int r = 0;
    bus_on();
    wp(0);
    while (len && !r) {
        uint16_t n = EE_PAGE - (addr % EE_PAGE);    // to the end of the page
        if (n > len) n = len;
        r = address(addr);
        for (uint16_t i = 0; !r && i < n; i++) r = put(buf[i]);
        stop();
        // the write cycle: the EEPROM answers its address again when done
        int busy = 1;
        for (int t = 0; busy && t < 200; t++) {
            start();
            busy = put(EE_ADDR << 1);
            stop();
        }
        if (busy) r = -1;
        addr += n;
        buf += n;
        len -= n;
    }
    wp(1);
    bus_off();
    return r ? -1 : 0;
}
