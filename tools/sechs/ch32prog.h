/*
 * ch32prog: write firmware to a CH32V003 through its single-wire debug
 * interface (SWIO), failsafe. See docs/ch32prog.md for the rules.
 *
 * Hardware-independent: the caller provides the debug transport (the PIO
 * interface on Werkzeug, a simulated chip in the tests).
 */

#ifndef CH32PROG_H
#define CH32PROG_H

#include <stdint.h>

#define CH32_FLASH      0x08000000u
#define CH32_FLASH_SIZE 16384u
#define CH32_PAGE       64u

enum {
    CH32_OK = 0,
    CH32_NO_CHIP,       /* nothing answers on SWIO */
    CH32_WRONG_CHIP,    /* not a CH32V003 */
    CH32_LOCKED,        /* read-protected: refused, never unlocked */
    CH32_BAD_IMAGE,     /* empty, too large, or not Machdyne BASIC */
    CH32_NO_HALT,       /* the core would not stop */
    CH32_FLASH_ERR,     /* unlock, erase or program failed */
    CH32_VERIFY,        /* pages still differ after three attempts */
    CH32_FORBIDDEN,     /* an access the rules forbid (a bug: never seen) */
};

/* provided by the transport: 0, or -1 if the target did not answer */
int ch32_dmi_write(uint8_t reg, uint32_t val);
int ch32_dmi_read(uint8_t reg, uint32_t *val);
/* RESETN, if wired: 1 holds the target in reset; does nothing if not */
void ch32_reset_line(int hold);
void ch32_delay_us(uint32_t us);

/* Write img (len bytes) to the main flash, verify it and restart the
 * target. force skips the Machdyne BASIC identity check. progress may be
 * NULL; it gets 0-100. Returns a CH32_ code. */
int ch32_flash(const uint8_t *img, uint32_t len, int force,
               void (*progress)(int percent));

/* what was read from the target (for messages) */
extern uint32_t ch32_chip_id, ch32_hartinfo;

/* accesses refused by the write gate; must stay 0 */
extern uint32_t ch32_refused;

const char *ch32_message(int code);

#endif
