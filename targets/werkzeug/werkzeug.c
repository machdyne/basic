/*
 * Machdyne BASIC for Werkzeug
 * Copyright (c) 2025 Lone Dynamics Corporation. All rights reserved.
 *
 * One firmware, two USB serial ports (usb.c): the BASIC console, and the
 * Sechs bridge for a module in the PMOD socket. See docs/targets.md.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/adc.h"

#include "../../basic.h"
#include "werkzeug.h"
#include "layout.h"

// files: the upper half of the flash (at most 2MB), whose size is read
// from the chip at start-up: see layout.h
static uint32_t fs_offset, fs_size;
#define FS_OFFSET   fs_offset
#define FS_SIZE     fs_size
static void flash_layout(void);

#define LED_GREEN   20
#define BUFLEN      128

// ---- pins -------------------------------------------------------------------
//
// 1-4   the Sechs socket (PMOD top row)       GPIO19, 17, 15, 13
// 5-8   the PMOD bottom row                    GPIO18, 16, 14, 12
// 9-20  the GPIO header                        GPIO0-11
// 21-24 the GPIO header, analog inputs         GPIO26-29 (ADC0-3)
//
// Pins 5 and up are declared with PIN n, mode (an extension of BASIC 1).
// Open-drain outputs (OD, I2C) are emulated: low drives 0, high is an
// input with the pull-up.

static const uint8_t pin_gpio[HW_PINS] = {
    19, 17, 15, 13, 18, 16, 14, 12,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    26, 27, 28, 29,
};
static uint8_t pin_mode[HW_PINS];

static void od_write(uint8_t g, uint8_t high) {
    gpio_set_dir(g, !high);
}

int hw_pin_mode(uint8_t pin, uint8_t mode) {
    uint8_t g = pin_gpio[pin - 1];
    if (mode == PM_UART) return HW_ERR_UNSUPPORTED;
    if (mode == PM_AIN && pin < 21) return HW_ERR_UNSUPPORTED;
    // the bridge's UART mode has pins 3 and 4 until its port is closed
    if ((pin == 3 || pin == 4) && mode != PM_NONE && usb_uart_active())
        return HW_ERR_BUS;
    pin_mode[pin - 1] = mode;
    if (mode == PM_AIN) {
        static int adc_on;
        if (!adc_on) {
            adc_init();
            adc_on = 1;
        }
        adc_gpio_init(g);
        return 0;
    }
    gpio_init(g);
    gpio_put(g, 0);
    // pull-ups: open-drain outputs, and the Sechs socket (pins 1-4)
    // whenever Werkzeug does not drive it: Werkzeug is the carrier, which
    // must keep the module's bus and UART lines from floating
    gpio_set_pulls(g, mode == PM_OD || mode == PM_I2C || pin <= 4, false);
    gpio_set_dir(g, mode == PM_PP);
    return 0;
}

void hw_pin_write(uint8_t pin, uint8_t level) {
    uint8_t g = pin_gpio[pin - 1];
    uint8_t m = pin_mode[pin - 1];
    if (m == PM_OD || m == PM_I2C) od_write(g, level);
    else gpio_put(g, level);
}

uint8_t hw_pin_read(uint8_t pin) {
    return gpio_get(pin_gpio[pin - 1]);
}

// 12-bit ADC, scaled to BASIC's 0-1023
int16_t hw_adc(uint8_t pin) {
    if (pin < 21) return -1;
    adc_select_input(pin_gpio[pin - 1] - 26);
    return adc_read() >> 2;
}

// The bridge may use pins 1-2 (I2C) or 3-4 (UART) unless a BASIC program
// has declared them for itself.
int wz_pins_free(uint8_t first) {
    for (uint8_t i = first - 1; i <= first; i++) {
        uint8_t m = pin_mode[i];
        if (m != PM_NONE && !(first == 1 && m == PM_NET)) return 0;
    }
    return 1;
}

// The programmer uses header GPIO0-2 (pins 9-11) while it runs.
int wz_prog_pins_free(void) {
    return !pin_mode[8] && !pin_mode[10];   // GPIO0 (SWIO), GPIO2 (RESETN)
}

// the green LED (active low)
void hw_led(uint8_t on) {
    gpio_put(LED_GREEN, !on);
}

// ---- I2C master, bit-banged (BASIC on pins 3/4, the bridge on 1/2) ----
//
// The master drives the clock, so timing jitter does no harm; devices
// may stretch the clock.

// Half a clock period: 5 us for BASIC's I2C (pins 3/4, about 50 kHz with
// the pull-ups a user adds); 25 us for the bridge (pins 1/2, about 10 kHz),
// so that even the RP2040's weak internal pull-ups (50-80k, rise times of
// microseconds) give clean edges.
static uint8_t half_us = 5;

static void line(uint8_t g, uint8_t high, uint8_t scl) {
    od_write(g, high);
    sleep_us(half_us);
    for (int t = 0; high && g == scl && !gpio_get(scl) && t < 400; t++)
        sleep_us(5);    // clock stretching: up to ~2 ms
}

static int i2c_byte(uint8_t scl, uint8_t sda, uint8_t out, int ack,
                    uint8_t *in) {
    uint8_t v = 0;
    for (int b = 0; b < 8; b++, out <<= 1) {
        line(sda, out & 0x80, scl);
        line(scl, 1, scl);
        v = (v << 1) | gpio_get(sda);
        line(scl, 0, scl);
    }
    line(sda, !ack, scl);
    line(scl, 1, scl);
    int nack = gpio_get(sda);
    line(scl, 0, scl);
    line(sda, 1, scl);
    if (in) *in = v;
    return nack;
}

static void i2c_start(uint8_t scl, uint8_t sda) {
    line(sda, 1, scl);
    line(scl, 1, scl);
    line(sda, 0, scl);
    line(scl, 0, scl);
}

// Write wn bytes, then read rn bytes (repeated start).
int wz_i2c(uint8_t scl, uint8_t sda, uint8_t addr, const uint8_t *w,
           uint8_t wn, uint8_t *r, uint8_t rn) {
    half_us = scl == WZ_A ? 25 : 5;     // the bridge: slow, see line()
    for (int i = 0; i < 2; i++) {       // open drain, released
        uint8_t g = i ? sda : scl;
        gpio_init(g);
        gpio_put(g, 0);
        gpio_set_pulls(g, true, false);
        gpio_set_dir(g, false);
    }
    // a device left holding SDA low (a transfer cut short): clock it out,
    // then a STOP
    for (int i = 0; i < 9 && !gpio_get(sda); i++) {
        line(scl, 0, scl);
        line(scl, 1, scl);
    }
    line(sda, 0, scl);
    line(sda, 1, scl);
    int res = 0;
    if (wn || !rn) {
        i2c_start(scl, sda);
        res = i2c_byte(scl, sda, addr << 1, 0, 0);
        while (!res && wn--) res = i2c_byte(scl, sda, *w++, 0, 0);
    }
    if (!res && rn) {
        i2c_start(scl, sda);
        res = i2c_byte(scl, sda, (addr << 1) | 1, 0, 0);
        while (!res && rn--) i2c_byte(scl, sda, 0xFF, rn != 0, r++);
    }
    line(sda, 0, scl);      // stop
    line(scl, 1, scl);
    line(sda, 1, scl);
    return res ? -1 : 0;
}

int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r,
           uint8_t rn) {
    return wz_i2c(WZ_C, WZ_D, addr, w, wn, r, rn);
}

// ---- console and time ----------------------------------------------------------

void hw_putc(char c) {
    putchar(c);
}

// Ctrl-C stops a running program. Other characters typed while a program
// runs are discarded. The USB device and the bridge are served here.
int hw_break(void) {
    usb_poll();
    return getchar_timeout_us(0) == 0x03;
}

void hw_delay_ms(uint16_t ms) {
    while (ms--) {
        sleep_ms(1);
        usb_poll();
    }
}

// ---- files: the upper half of the flash (NOR mode of fs.c) ----------------
//
// The RP2040 programs flash in 256-byte pages; bytes not being written are
// programmed as 0xFF, which leaves them unchanged. Interrupts are off while
// the flash is busy (the program runs from that flash).

int fs_media_read(uint32_t a, uint8_t *buf, uint16_t len) {
   memcpy(buf, (const uint8_t *)(XIP_BASE + FS_OFFSET + a), len);
   return 0;
}

int fs_media_prog(uint32_t a, const uint8_t *buf, uint16_t len) {
   static uint8_t page[FLASH_PAGE_SIZE];
   while (len) {
      uint32_t base = a & ~(FLASH_PAGE_SIZE - 1);
      uint16_t off = a - base;
      uint16_t n = FLASH_PAGE_SIZE - off;
      if (n > len) n = len;
      memset(page, 0xFF, sizeof(page));
      memcpy(page + off, buf, n);
      uint32_t ints = save_and_disable_interrupts();
      flash_range_program(FS_OFFSET + base, page, FLASH_PAGE_SIZE);
      restore_interrupts(ints);
      a += n;
      buf += n;
      len -= n;
   }
   return 0;
}

int fs_media_erase(uint32_t a) {
   uint32_t ints = save_and_disable_interrupts();
   flash_range_erase(FS_OFFSET + a, FS_NOR_SECTOR);
   restore_interrupts(ints);
   return 0;
}

int hw_fformat(void) {
   if (!fs_size) return HW_ERR_UNSUPPORTED;
   return fs_format_default(FS_SIZE);
}

// The flash size from its JEDEC ID; layout.h decides where the files go.
extern char __flash_binary_end;

static void flash_layout(void) {
   uint8_t tx[4] = { 0x9F, 0, 0, 0 }, rx[4] = { 0 };
   uint32_t ints = save_and_disable_interrupts();
   flash_do_cmd(tx, rx, 4);
   restore_interrupts(ints);
   uint32_t used = (uint32_t)&__flash_binary_end - XIP_BASE;
   if (!wz_layout(rx[3], used, &fs_offset, &fs_size)) fs_offset = fs_size = 0;
}

// ---- start-up and the console ---------------------------------------------------

int main(void) {
    usb_init();

    for (int i = 0; i < HW_PINS; i++) {     // every BASIC pin an input;
        gpio_init(pin_gpio[i]);             // the Sechs socket pulled up,
        gpio_set_pulls(pin_gpio[i], i < 4, false);  // so its bus never floats
    }
    // pins 1/2 are the Sechs bus (NET) from the start: pulled up, so that a
    // module's I2C target never sees floating lines
    hw_pin_mode(1, PM_NET);
    hw_pin_mode(2, PM_NET);
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, true);
    gpio_put(LED_GREEN, 1);                 // off (active low)

    // files; BOOT.BAS runs at start-up, as on modules, with or without a
    // computer connected
    flash_layout();
    int mounted = fs_size && fs_mount(FS_SIZE) == FS_OK;
    if (mounted && basic_boot()) basic_yield((uint8_t *)"RUN");

    char buf[BUFLEN];
    int n = 0;
    for (;;) {
        if (usb_console_new()) {            // a terminal connected
            printf("///\r\nWerkzeug firmware " __DATE__ " " __TIME__ "\r\n");
            if (!fs_size) printf("NO ROOM FOR FILES\r\n");
            else if (!mounted) printf("NOT FORMATTED\r\n");
            n = 0;
        }
        int c = getchar_timeout_us(1000);
        if (c < 0) continue;
        if (c == '\r' || c == '\n') {
            printf("\r\n");
            buf[n] = 0;
            n = 0;
            basic_yield((uint8_t *)buf);
            mounted = 1;    // (FORMAT YES may have mounted it)
        } else if ((c == 8 || c == 127) && n) {
            n--;
            printf("\b \b");
        } else if (c >= ' ' && n < BUFLEN - 1) {
            buf[n++] = c;
            putchar(c);
        }
        fflush(stdout);
    }
}
