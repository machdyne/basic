/*
 * Machdyne BASIC for Blaustahl (and Kaltstahl)
 * Copyright (c) 2025 Lone Dynamics Corporation. All rights reserved.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "pico/binary_info.h"

#include "blaustahl.h"

#include "../../basic.h"

#define BUFLEN 128

// the filesystem uses the F-RAM below the metadata area
#define FS_SIZE FRAM_AVAILABLE

void fram_init(void);
uint8_t fram_read(int addr);
void fram_write(int addr, unsigned char d);

uint8_t booting = 1;
volatile uint8_t boot_due = 0;

// The alarm only sets a flag: BASIC must not run in interrupt context.
int64_t timer_callback(alarm_id_t id, void *user_data) {
	boot_due = 1;
	return 0;
}

int main(void) {

	stdio_init_all();
	while (!stdio_usb_connected()) {
		sleep_ms(100);
	}

	// init LED
	gpio_init(BS_LED);
	gpio_set_dir(BS_LED, true);
	gpio_set_pulls(BS_LED, false, false);
	gpio_put(BS_LED, 0);

	// init SPI fram
	fram_init();

	printf("///\r\n");

	// the filesystem uses the F-RAM below the reserved metadata area
	int r = fs_mount(FS_SIZE);
	if (r == FS_ERR_UNFORMATTED)
		printf("NOT FORMATTED\r\n");
	else if (r)
		printf("STORAGE ERROR %d\r\n", r);

	add_alarm_in_ms(3000, timer_callback, NULL, false);

	// parser
   char buf[BUFLEN];
   int bptr = 0;
   int c;

	bzero(buf, BUFLEN);

	// wait for commands
	while (1) {

		if (boot_due) {
			boot_due = 0;
			if (booting && basic_boot()) basic_yield((uint8_t *)"RUN");
			booting = 0;
		}

		c = getchar_timeout_us(10000);

		if (c > 0) {

			booting = 0;

			if (c == 0x0a || c == 0x0d) {
				putchar(0x0a);
				putchar(0x0d);
				fflush(stdout);
				basic_yield((uint8_t *)buf);
				bptr = 0;
				bzero(buf, BUFLEN);
				continue;
			}

			if (bptr >= BUFLEN - 1) {
				printf("# buffer overflow\r\n");
				bptr = 0;
				bzero(buf, BUFLEN);
				continue;
			}

			putchar(c);
			fflush(stdout);
			buf[bptr++] = c;

		}

		tight_loop_contents(); 

	}

	return 0;

}


// ---- console and time --------------------------------------------------

void hw_putc(char c) {
   putchar(c);
}

// Ctrl-C stops a running program. Other characters typed while a program
// runs are discarded.
int hw_break(void) {
   return getchar_timeout_us(0) == 0x03;
}

void hw_delay_ms(uint16_t ms) {
   sleep_ms(ms);
}


// DIR	VAL	GPIOS
// ----------------------
// 0x10	0x15	GPIO 0-7
// 0x11	0x16	GPIO 8-15
// 0x12	0x17	GPIO 16-23
// 0x13	0x18	GPIO 24-31


// ---- files: the filesystem on the F-RAM ---------------------------------

int fs_media_read(uint32_t addr, uint8_t *buf, uint16_t len) {
   if (addr + len > FRAM_AVAILABLE) return -1;
   while (len--) *buf++ = fram_read(addr++);
   return 0;
}

int fs_media_prog(uint32_t addr, const uint8_t *buf, uint16_t len) {
   if (addr + len > FRAM_AVAILABLE) return -1;
   while (len--) fram_write(addr++, *buf++);
   return 0;
}

int hw_fopen(const char *name, uint8_t mode) {
   return fs_open(name, mode);
}

int hw_fread(uint8_t *buf, uint16_t len) {
   return fs_read(buf, len);
}

int hw_fwrite(const uint8_t *buf, uint16_t len) {
   return fs_write(buf, len);
}

int hw_fclose(void) {
   return fs_close();
}

void hw_fabort(void) {
   fs_abort();
}

int hw_fdelete(const char *name) {
   return fs_delete(name);
}

int hw_fdir(fs_dir_cb cb) {
   return fs_dir(cb, 0);
}


int hw_fformat(void) {
   return fs_format_default(FS_SIZE);
}

void hw_led(uint8_t on) {
   gpio_put(BS_LED, on);
}

// ---- pins, I2C: not yet mapped on this board -----------------------------

int hw_pin_mode(uint8_t pin, uint8_t mode) {
   (void)pin;
   return mode == PM_NONE || mode == PM_NET ? 0 : HW_ERR_UNSUPPORTED;
}

void hw_pin_write(uint8_t pin, uint8_t level) {
   (void)pin; (void)level;
}

uint8_t hw_pin_read(uint8_t pin) {
   (void)pin;
   return 0;
}

int16_t hw_adc(uint8_t pin) {
   (void)pin;
   return -1;
}

int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r,
           uint8_t rn) {
   (void)addr; (void)w; (void)wn; (void)r; (void)rn;
   return -1;
}
