/*
 * Werkzeug: the single-wire debug interface (SWIO) for programming a
 * CH32V003 (tools/sechs/ch32prog.c, docs/ch32prog.md).
 *
 * Wiring, on the GPIO header's top row (female jumpers on its pins):
 *   header 1 (GPIO0)  drives SWIO through a 1k resistor
 *   header 2 (GPIO1)  senses SWIO, on the module's side of the resistor
 *   header 3 (GPIO2)  RESETN, optional (open drain): for recovery
 *   header 9, 10      GND, 3V3
 *
 * The resistor is also the pull-up: the drive pin stays high between
 * pulses, and the module can still pull the line low to answer a read.
 *
 * Bits are timed in CPU cycles (8 ns at 125 MHz) by code running from RAM
 * with interrupts off for each packet, as in the reference programmer:
 * a 1 is low for about 80 ns, a 0 for about 330 ns, each followed by about
 * 80 ns high; a read pulses low for about 80 ns and samples about 160 ns
 * after release.
 */

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "../../tools/sechs/ch32prog.h"

#define SWIO_DRV    0
#define SWIO_SNS    1
#define SWIO_RST    2

static uint32_t c1, c0, ch, cs;     /* cycles: 1 low, 0 low, high, sample */

void swio_init(void) {
    uint32_t mhz = clock_get_hz(clk_sys) / 1000000;
    c1 = 80 * mhz / 1000;
    c0 = 330 * mhz / 1000;
    ch = 80 * mhz / 1000;
    cs = 160 * mhz / 1000;
    gpio_init(SWIO_DRV);
    gpio_put(SWIO_DRV, 1);
    gpio_set_dir(SWIO_DRV, true);
    gpio_init(SWIO_SNS);
    gpio_set_dir(SWIO_SNS, false);
    gpio_init(SWIO_RST);            /* released: input with pull-up */
    gpio_set_pulls(SWIO_RST, true, false);
    gpio_set_dir(SWIO_RST, false);
}

void swio_release(void) {           /* all three back to plain inputs */
    gpio_init(SWIO_DRV);
    gpio_init(SWIO_SNS);
    gpio_init(SWIO_RST);
}

static inline void __not_in_flash_func(low)(void) { sio_hw->gpio_clr = 1u << SWIO_DRV; }
static inline void __not_in_flash_func(high)(void) { sio_hw->gpio_set = 1u << SWIO_DRV; }

static void __not_in_flash_func(send)(uint64_t bits, int n) {
    while (n--) {
        int b = (bits >> n) & 1;
        low();
        busy_wait_at_least_cycles(b ? c1 : c0);
        high();
        busy_wait_at_least_cycles(ch);
    }
}

/* one bit from the target: 0 if it holds the line low; -1 if stuck low */
static int __not_in_flash_func(get)(void) {
    low();
    busy_wait_at_least_cycles(c1);
    high();
    busy_wait_at_least_cycles(cs);
    int b = (sio_hw->gpio_in >> SWIO_SNS) & 1;
    for (int t = 0; !((sio_hw->gpio_in >> SWIO_SNS) & 1); t++)
        if (t > 2000) return -1;
    busy_wait_at_least_cycles(ch);
    return b;
}

static void stop(void) {
    high();
    busy_wait_us(2);                /* a stop: the line high for > 18T */
}

int __not_in_flash_func(ch32_dmi_write)(uint8_t reg, uint32_t val) {
    uint64_t p = (1ull << 40) | ((uint64_t)(reg & 0x7F) << 33) | (1ull << 32) | val;
    uint32_t ints = save_and_disable_interrupts();
    send(p, 41);                    /* start, address, write, data */
    restore_interrupts(ints);
    stop();
    return 0;
}

int __not_in_flash_func(ch32_dmi_read)(uint8_t reg, uint32_t *val) {
    uint32_t v = 0;
    int ok = 1;
    uint32_t ints = save_and_disable_interrupts();
    send((1u << 8) | ((reg & 0x7F) << 1), 9);   /* start, address, read */
    for (int i = 0; i < 32; i++) {
        int b = get();
        if (b < 0) ok = 0;
        v = (v << 1) | (b > 0);
    }
    restore_interrupts(ints);
    stop();
    *val = v;
    return ok ? 0 : -1;
}

void ch32_reset_line(int hold) {
    gpio_set_dir(SWIO_RST, hold != 0);  /* output low, or released */
    gpio_put(SWIO_RST, 0);
}

void ch32_delay_us(uint32_t us) {
    busy_wait_us(us);
}
