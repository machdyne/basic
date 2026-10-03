/* Machdyne BASIC for Werkzeug: what werkzeug.c and usb.c share. */

#ifndef WERKZEUG_H
#define WERKZEUG_H

#include <stdint.h>

/* the Sechs socket (PMOD top row): module pins 1-4 */
#define WZ_A    19      /* pin 1: global I2C SCL */
#define WZ_B    17      /* pin 2: global I2C SDA */
#define WZ_C    15      /* pin 3: local SCL; UART to the module */
#define WZ_D    13      /* pin 4: local SDA; UART from the module (UART0 RX) */

/* usb.c: two USB serial ports, BASIC (0) and the Sechs bridge (1) */
void usb_init(void);
void usb_poll(void);            /* call often: USB, bridge, UART relay */
int usb_console_new(void);      /* 1 once each time a terminal connects */
int usb_uart_active(void);      /* the bridge's UART mode has pins 3/4 */

/* werkzeug.c */
int wz_i2c(uint8_t scl, uint8_t sda, uint8_t addr, const uint8_t *w,
           uint8_t wn, uint8_t *r, uint8_t rn);  /* 0, -1 no answer */
int wz_pins_free(uint8_t first);    /* pins first, first+1 free for the bridge */
int wz_prog_pins_free(void);        /* header GPIO0-2 (pins 9-11) free */

/* swio.c: the programming wires */
void swio_init(void);
void swio_release(void);

#endif
