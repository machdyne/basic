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

#define BUFLEN 128

void fram_init(void);
void fs_init(void);
void hw_list(void);

void basic_yield(uint8_t *line);

uint8_t booting = 1;

int64_t timer_callback(alarm_id_t id, void *user_data) {
	basic_yield("LOAD BOOT.BAS");
	basic_yield("RUN");
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

   hw_list();

	add_alarm_in_ms(3000, timer_callback, NULL, false);

	// parser
   char buf[BUFLEN];
   int bptr = 0;
   int c;

	bzero(buf, BUFLEN);

	// wait for commands
	while (1) {

		c = getchar();

		if (c > 0) {

			booting = 0;

			if (c == 0x0a || c == 0x0d) {
				putchar(0x0a);
				putchar(0x0d);
				fflush(stdout);
				basic_yield(buf);
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

void hw_sleep(uint16_t secs) {
   sleep_ms(secs * 1000);
}

uint8_t hw_peek(uint8_t addr) {
   if (addr >= 0x15 && addr <= 0x19) {
      uint8_t base_gpio = (addr - 0x15) * 8;
      uint8_t result = 0;
      
      for (uint8_t bit = 0; bit < 8; bit++) {
         uint8_t gpio = base_gpio + bit;
         if (gpio < 29) {  // Only 29 GPIOs available
            bool val = gpio_get(gpio);
            if (val) {
               result |= (1 << bit);  // Set the corresponding bit
            }
         }
      }
      
      printf(" GET GPIO %02x = %02x\r\n", addr, result);
      return result;
   }
   
   // Return 0 for invalid addresses or direction registers (read-only values)
   return 0;
}

// DIR	VAL	GPIOS
// ----------------------
// 0x10	0x15	GPIO 0-7
// 0x11	0x16	GPIO 8-15
// 0x12	0x17	GPIO 16-23
// 0x13	0x18	GPIO 24-31

void hw_poke(uint8_t addr, uint8_t data) {
   if (addr >= 0x10 && addr <= 0x14) {
      uint8_t base_gpio = (addr - 0x10) * 8;
      printf(" SET GPIO DIRECTION %02x = %02x\r\n", addr, data);
      
      for (uint8_t bit = 0; bit < 8; bit++) {
         uint8_t gpio = base_gpio + bit;
         if (gpio < 29) {  // Only 29 GPIOs available
            bool dir = (data >> bit) & 0x01;  // Extract bit value
            gpio_set_dir(gpio, dir);  // true = output, false = input
         }
      }
   } else if (addr >= 0x15 && addr <= 0x19) {
      uint8_t base_gpio = (addr - 0x15) * 8;
      printf(" SET GPIO %02x = %02x\r\n", addr, data);
      
      for (uint8_t bit = 0; bit < 8; bit++) {
         uint8_t gpio = base_gpio + bit;
         if (gpio < 29) {  // Only 29 GPIOs available
            bool value = (data >> bit) & 0x01;  // Extract bit value
            gpio_put(gpio, value);  // Set GPIO high or low
         }
      }
   }
}
