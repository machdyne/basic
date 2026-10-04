/*
 * USB bridge protocol for a Sechs programmer (for example Werkzeug).
 *
 * Lines from the computer, answers from the bridge (hex is two digits per
 * byte, no spaces):
 *
 *   w AA DATA        write DATA (hex) to I2C address AA (hex)
 *                    -> "ok", "nack" or "busy"
 *   r AA RR N        read N bytes (decimal, 1-64) from register RR of AA
 *                    -> "ok DATA", "nack" or "busy"
 *   u BAUD           transparent UART on pins 3/4 at 9600 or 115200 until
 *                    the computer closes the port -> "ok" first, or "busy"
 *   v                -> "ok sechs-bridge 1"
 *   f N CRC          an image of N bytes (decimal) with this CRC32 (8 hex
 *                    digits) follows, for programming -> "ok"
 *   d DATA           the next bytes of the image (hex) -> "ok"
 *   p [force]        program the module's firmware with the image (on
 *                    Werkzeug, through the programming wires; see
 *                    docs/ch32prog.md) -> "progress NN" lines, then
 *                    "ok ..." or "fail ..."; nothing is touched unless the
 *                    image is complete and its CRC matches
 *   i                identify the module on the programming wires: stop it,
 *                    read its chip ID, restart it; nothing is written
 *                    -> "ok chip XXXXXXXX hartinfo XXXXXXXX" or "fail ..."
 *   t N              test the programming wires: write and read back a debug
 *                    register N times (1-100000), without stopping the chip
 *                    -> "ok N errors E" or "fail ..."
 *   c 0 | c 1        the programming wire (Werkzeug): 0 the GPIO header's
 *                    pin 1, 1 the Sechs socket's pin A (modules with SWIO
 *                    on A, in programming mode) -> "ok 0" or "ok 1"
 *   s [A B C D E F]  the SWIO timing (Werkzeug): low for
 *                    a 1 and a 0, the gap, the read sample delay (ns), the
 *                    pause after a transaction (us), the line mode (0 driven
 *                    high between bits, 1 released) -> "ok A B C D E F"
 *
 * While p and i run, the bridge sends "status ..." lines describing each
 * step, before the final answer.
 *
 * "busy": the pins are in use by something else on the bridge (on
 * Werkzeug, a BASIC program that declared them). Anything else -> "error".
 * Lines end with LF; CR is ignored.
 */

#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdint.h>

#define BRIDGE_IMAGE_MAX 32768   /* the largest supported chip (CH32V005) */

/* implemented by the bridge's hardware */
/* 0, -1 (not acknowledged) or -2 (busy) */
int bridge_i2c_write(uint8_t addr, const uint8_t *d, uint8_t n);
int bridge_i2c_read(uint8_t addr, uint8_t reg, uint8_t *d, uint8_t n);
int bridge_uart(uint32_t baud);     /* start transparent UART: 0 or -2 */
void bridge_puts(const char *s);    /* to the computer */
/* program img (complete, CRC checked); answers with bridge_puts */
void bridge_prog(const uint8_t *img, uint32_t len, int force);
void bridge_identify(void);             /* answers with bridge_puts */
void bridge_link_test(uint32_t n);      /* answers with bridge_puts */
/* the SWIO timing: n 0 (report) or 6 values; answers with bridge_puts */
void bridge_swio_timing(const uint32_t *v, int n);
void bridge_swio_socket(int socket);    /* answers with bridge_puts */

/* feed one character from the computer */
void bridge_char(char c);

#endif
