/*
 * Werkzeug's USB device: two serial ports.
 *
 *   port 0 (interface 0, "Werkzeug BASIC"):  the BASIC console (stdio)
 *   port 1 (interface 2, "Werkzeug Sechs bridge"): the Sechs bridge for a
 *       module in the PMOD socket (tools/sechs/bridge.h), used by sechsctl
 *
 * usb_poll() runs TinyUSB, feeds the bridge and relays its UART mode. BASIC
 * calls it between statements and while it waits, so the bridge works
 * while a program runs.
 */

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/unique_id.h"
#include "hardware/uart.h"
#include "hardware/pio.h"
#include "tusb.h"

#include "werkzeug.h"
#include "../../tools/sechs/bridge.h"
#include "../../tools/sechs/ch32prog.h"
#include "uart_tx.pio.h"

/* ---- descriptors ---------------------------------------------------------- */

/* pid.codes test IDs: replace with Machdyne's own before release */
#define USB_VID 0x1209
#define USB_PID 0x0001

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,        /* interface associations (IAD) */
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

enum { ITF_BASIC, ITF_BASIC_DATA, ITF_BRIDGE, ITF_BRIDGE_DATA, ITF_TOTAL };
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static const uint8_t desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_BASIC, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_CDC_DESCRIPTOR(ITF_BRIDGE, 5, 0x83, 8, 0x04, 0x84, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
static const char *const strings[] = {
    NULL, "Machdyne", "Werkzeug", serial, "Werkzeug BASIC",
    "Werkzeug Sechs bridge",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t d[40];
    uint8_t n = 0;
    (void)langid;
    if (index == 0) {
        d[1] = 0x0409;
        n = 1;
    } else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
        for (const char *s = strings[index]; *s && n < 39; s++) d[1 + n++] = *s;
    }
    d[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    return d;
}

/* ---- port 0: the BASIC console as stdio ----------------------------------- */

static int console_was;     /* a terminal was connected */
static int console_new;

static void basic_out(const char *buf, int len) {
    while (len > 0) {
        if (!tud_cdc_n_connected(0)) return;   /* nobody listening: drop */
        uint32_t n = tud_cdc_n_write(0, buf, len);
        buf += n;
        len -= n;
        tud_cdc_n_write_flush(0);
        if (len) usb_poll();
    }
}

static void basic_flush(void) {
    tud_cdc_n_write_flush(0);
}

static int basic_in(char *buf, int len) {
    usb_poll();
    if (!tud_cdc_n_available(0)) return PICO_ERROR_NO_DATA;
    return (int)tud_cdc_n_read(0, buf, len);
}

static stdio_driver_t basic_stdio = {
    .out_chars = basic_out,
    .out_flush = basic_flush,
    .in_chars = basic_in,
};

int usb_console_new(void) {
    int n = console_new;
    console_new = 0;
    return n;
}

/* ---- port 1: the Sechs bridge ---------------------------------------------- */

static uint32_t uart_baud;      /* UART mode, 0 if off */
static PIO tx_pio = pio0;
static int tx_sm = -1;
static uint tx_offset;

int usb_uart_active(void) {
    return uart_baud != 0;
}

int bridge_i2c_write(uint8_t addr, const uint8_t *d, uint8_t n) {
    if (!wz_pins_free(1)) return -2;
    return wz_i2c(WZ_A, WZ_B, addr, d, n, 0, 0);
}

int bridge_i2c_read(uint8_t addr, uint8_t reg, uint8_t *d, uint8_t n) {
    if (!wz_pins_free(1)) return -2;
    return wz_i2c(WZ_A, WZ_B, addr, &reg, 1, d, n);
}

/* UART mode: PIO transmits on pin 3, UART0 receives on pin 4 */
int bridge_uart(uint32_t baud) {
    if (!wz_pins_free(3) || uart_baud) return -2;
    if (tx_sm < 0) {
        tx_sm = pio_claim_unused_sm(tx_pio, true);
        tx_offset = pio_add_program(tx_pio, &uart_tx_program);
    }
    uart_tx_program_init(tx_pio, tx_sm, tx_offset, WZ_C, baud);
    uart_init(uart0, baud);
    gpio_set_function(WZ_D, GPIO_FUNC_UART);
    uart_baud = baud;
    return 0;
}

static void uart_stop(void) {
    pio_sm_set_enabled(tx_pio, tx_sm, false);
    uart_deinit(uart0);
    gpio_init(WZ_C);            /* both pins back to inputs, pulled up */
    gpio_init(WZ_D);
    gpio_set_pulls(WZ_C, true, false);
    gpio_set_pulls(WZ_D, true, false);
    uart_baud = 0;
}

/* p: program a module through the programming wires (swio.c) */
static void prog_progress(int pct) {
    static int last = -1;
    char b[24];
    if (pct / 10 == last) return;
    last = pct / 10;
    sprintf(b, "progress %d\n", pct);
    bridge_puts(b);
    tud_task();         /* let the progress out */
}

static void prog_status(const char *msg) {
    char b[100];
    snprintf(b, sizeof(b), "status %s\n", msg);
    bridge_puts(b);
    tud_task();
}

void bridge_identify(void) {
    char b[100];
    if (!wz_prog_pins_free()) {
        bridge_puts("busy\n");
        return;
    }
    ch32_status = prog_status;
    swio_init();
    int r = ch32_identify();
    swio_release();
    if (r == CH32_NO_CHIP)
        snprintf(b, sizeof(b), "fail %s (read %08lx%s)\n", ch32_message(r),
                 (unsigned long)ch32_last_read,
                 ch32_last_read_ok ? "" : ", the line stayed low");
    else if (r && r != CH32_WRONG_CHIP && r != CH32_LOCKED)
        snprintf(b, sizeof(b), "fail %s\n", ch32_message(r));
    else
        snprintf(b, sizeof(b), "%s chip %08lx hartinfo %08lx%s\n", r ? "fail" : "ok",
                 (unsigned long)ch32_chip_id, (unsigned long)ch32_hartinfo,
                 r ? " (not a CH32V003, or read-protected)" : "");
    bridge_puts(b);
}

void bridge_link_test(uint32_t n) {
    char b[100];
    uint32_t errors;
    if (!wz_prog_pins_free()) {
        bridge_puts("busy\n");
        return;
    }
    swio_init();
    uint64_t t0 = time_us_64();
    int r = ch32_link_test(n, &errors);
    uint32_t us = (uint32_t)(time_us_64() - t0);
    swio_release();
    if (r == CH32_NO_CHIP)
        snprintf(b, sizeof(b), "fail %s (read %08lx%s)\n", ch32_message(r),
                 (unsigned long)ch32_last_read,
                 ch32_last_read_ok ? "" : ", the line stayed low");
    else if (r) snprintf(b, sizeof(b), "fail %s\n", ch32_message(r));
    else snprintf(b, sizeof(b), "ok %lu errors %lu (%lu us per round trip)\n",
                  (unsigned long)n, (unsigned long)errors, (unsigned long)(us / n));
    bridge_puts(b);
}

void bridge_swio_timing(const uint32_t *v, int n) {
    uint32_t t[6];
    char b[100];
    if (n == 6) swio_set_timing(v);
    swio_get_timing(t);
    snprintf(b, sizeof(b), "ok %lu %lu %lu %lu %lu %lu\n", (unsigned long)t[0],
             (unsigned long)t[1], (unsigned long)t[2], (unsigned long)t[3],
             (unsigned long)t[4], (unsigned long)t[5]);
    bridge_puts(b);
}

void bridge_prog(const uint8_t *img, uint32_t len, int force) {
    char b[100];
    if (!wz_prog_pins_free()) {
        bridge_puts("busy\n");
        return;
    }
    ch32_status = prog_status;
    swio_init();
    int r = ch32_flash(img, len, force, prog_progress);
    swio_release();
    if (r == CH32_NO_CHIP || r == CH32_WRONG_CHIP)
        sprintf(b, "fail %s (id %08lx)\n", ch32_message(r), (unsigned long)ch32_chip_id);
    else
        sprintf(b, "%s %s\n", r ? "fail" : "ok", ch32_message(r));
    bridge_puts(b);
}

void bridge_puts(const char *s) {
    tud_cdc_n_write_str(1, s);
    tud_cdc_n_write_flush(1);
}

/* ---- polling ------------------------------------------------------------------ */

void usb_poll(void) {
    static int busy;
    if (busy) return;           /* (the bridge can print while we poll) */
    busy = 1;
    tud_task();

    int c = tud_cdc_n_connected(0);
    if (c && !console_was) console_new = 1;
    console_was = c;

    while (tud_cdc_n_available(1)) {
        uint8_t ch;
        tud_cdc_n_read(1, &ch, 1);
        if (uart_baud) pio_sm_put_blocking(tx_pio, tx_sm, ch);
        else bridge_char(ch);
    }
    if (uart_baud) {
        while (uart_is_readable(uart0) && tud_cdc_n_write_available(1))
            tud_cdc_n_write_char(1, uart_getc(uart0));
        tud_cdc_n_write_flush(1);
        if (!tud_cdc_n_connected(1)) uart_stop();  /* the port was closed */
    }
    busy = 0;
}

void usb_init(void) {
    pico_get_unique_board_id_string(serial, sizeof(serial));
    tud_init(BOARD_TUD_RHPORT);
    stdio_set_driver_enabled(&basic_stdio, true);
}
