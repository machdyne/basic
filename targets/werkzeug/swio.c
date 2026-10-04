/*
 * Werkzeug: the single-wire debug interface (SWIO) for programming a
 * CH32V003 (tools/sechs/ch32prog.c, docs/ch32prog.md).
 *
 * Wiring, on the GPIO header's top row (female jumpers on its pins):
 *   header 1 (GPIO0)  SWIO, directly: no resistor
 *   header 3 (GPIO2)  RESETN, optional (open drain): for recovery
 * Ground and power come from the module's own connection (for example
 * the Wolfszahn on the PMOD), or header 9 (GND) and 10 (3V3).
 *
 * One pin, as the reference programmers: the line is driven high between
 * bits, pulled low for each bit, and released only inside a read bit, so
 * the chip can answer by holding it low; the RP2040's internal pull-up is
 * enough. Bits are timed in CPU cycles by code running from RAM with
 * interrupts off for each packet. The timing (measured, see below) can be
 * changed at run time: bridge command s, sechsctl swio-timing.
 */

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "../../tools/sechs/ch32prog.h"

#define SWIO_PIN    0
#define SWIO_RST    2
#define SWIO_MASK   (1u << SWIO_PIN)

// Line mode 0 (default): driven high between bits; the chip only ever
// drives the line during a read bit, so nothing can fight. Mode 1: released
// (internal pull-up) except during the pulses, for a line shared with other
// devices (both measured to work).

// Nanoseconds (and microseconds for the pause after a transaction), in the
// middle of the windows measured on a CH32V003 (LS10, 2026-10-04; the
// chip's debug clock period T is about 83 ns): a 1 works from 120 to 300 ns
// (T to 4T), a 0 from 500 ns (6T) to at least 2000 ns, any read sample
// delay from 50 to 250 ns, a pause from 2 us. Both line modes.
static uint32_t t_one = 180, t_zero = 900, t_gap = 150, t_sample = 150;
static uint32_t t_stop_us = 4, line_mode = 0;
static uint32_t c1, c0, cg, cs, cw;      // the same in CPU cycles

static void timing_cycles(void) {
    uint32_t mhz = clock_get_hz(clk_sys) / 1000000;
    c1 = t_one * mhz / 1000;
    c0 = t_zero * mhz / 1000;
    cg = t_gap * mhz / 1000;
    cs = t_sample * mhz / 1000;
    cw = 20000;                         // reads: give up after this many loops
}

// set the timing: 1 low, 0 low, gap, sample (ns), pause (us), mode
void swio_set_timing(const uint32_t *v) {
    t_one = v[0];
    t_zero = v[1];
    t_gap = v[2];
    t_sample = v[3];
    t_stop_us = v[4];
    line_mode = v[5];
    timing_cycles();
}

void swio_get_timing(uint32_t *v) {
    v[0] = t_one;
    v[1] = t_zero;
    v[2] = t_gap;
    v[3] = t_sample;
    v[4] = t_stop_us;
    v[5] = line_mode;
}

void swio_init(void) {
    timing_cycles();
    gpio_init(SWIO_PIN);
    gpio_set_pulls(SWIO_PIN, true, false);
    gpio_set_drive_strength(SWIO_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(SWIO_PIN, GPIO_SLEW_RATE_FAST);
    gpio_put(SWIO_PIN, 1);
    gpio_set_dir(SWIO_PIN, line_mode == 0);  // mode 0: driven high
    gpio_init(SWIO_RST);                    // (RESETN: optional, released)
    gpio_set_pulls(SWIO_RST, true, false);
    gpio_set_dir(SWIO_RST, false);
    busy_wait_us(100);                      // the line high for a moment
}

void swio_release(void) {
    gpio_init(SWIO_PIN);
    gpio_init(SWIO_RST);
}

static inline void __not_in_flash_func(drive_low)(void) {
    sio_hw->gpio_clr = SWIO_MASK;
    sio_hw->gpio_oe_set = SWIO_MASK;
}

// end of a pulse: mode 0 drives high; mode 1 pushes high briefly and lets go
static inline void __not_in_flash_func(end_pulse)(void) {
    sio_hw->gpio_set = SWIO_MASK;
    sio_hw->gpio_oe_set = SWIO_MASK;
    if (line_mode) {
        busy_wait_at_least_cycles(3);
        sio_hw->gpio_oe_clr = SWIO_MASK;
    }
}

static void __not_in_flash_func(send)(uint64_t bits, int n) {
    while (n--) {
        int b = (bits >> n) & 1;
        drive_low();
        busy_wait_at_least_cycles(b ? c1 : c0);
        end_pulse();
        busy_wait_at_least_cycles(cg);
    }
}

// one bit from the chip: 0 if it holds the line low; -1 if it stays low
static int __not_in_flash_func(get)(void) {
    drive_low();
    busy_wait_at_least_cycles(c1);
    sio_hw->gpio_set = SWIO_MASK;           // high, then let go: the chip
    sio_hw->gpio_oe_clr = SWIO_MASK;        // may now hold the line low
    busy_wait_at_least_cycles(cs);
    int b = (sio_hw->gpio_in >> SWIO_PIN) & 1;
    uint32_t t = 0;
    while (!((sio_hw->gpio_in >> SWIO_PIN) & 1))
        if (++t > cw) return -1;
    end_pulse();                            // high again (mode 0: driven)
    busy_wait_at_least_cycles(cg);
    return b;
}

static void stop(void) {
    busy_wait_us(t_stop_us);        // the line high between transactions
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
