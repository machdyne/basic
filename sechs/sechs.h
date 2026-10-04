/*
 * Sechs core: the I2C target protocol of a Sechs module.
 *
 * Hardware independent: the target's I2C interrupt handler calls
 * sechs_start/sechs_rx/sechs_tx/sechs_stop, and its main loop uses the
 * console functions and sechs_cmd. See docs/sechs.md.
 */

#ifndef SECHS_H
#define SECHS_H

#include <stdint.h>

#define SECHS_VERSION   0x05    /* Sechs 0.5 */
#define SECHS_DEFAULT_ADDR 0x0C
#define SECHS_BROADCAST 0x5A    /* first byte of every broadcast */

/* core registers */
#define SR_SIG0     0x00
#define SR_SIG1     0x01
#define SR_VER      0x02
#define SR_CAPS     0x03
#define SR_STATUS   0x04
#define SR_OK       0x05
#define SR_FAULT    0x06
#define SR_CONTROL  0x07
#define SR_ADDR     0x08
#define SR_INFO     0x09
/* console */
#define SR_CIN      0x18
#define SR_COUT     0x19
#define SR_CDATA    0x1A
/* program registers 0x80-0x8F: REG 0-15 */
#define SR_REG      0x80

/* CAPS */
#define CAP_FILES       0x01    /* file commands through the consoles */
#define CAP_UART_CON    0x02
#define CAP_I2C_CON     0x04
#define CAP_IDENTIFY    0x10

/* STATUS */
#define ST_BOOT         0x01
#define ST_HALTED       0x02
#define ST_RUNNING      0x04
#define ST_CONSOLE      0x08
#define ST_NETWORKED    0x10
#define ST_FAULT        0x20
#define ST_DEGRADED     0x40

/* CONTROL and broadcast commands */
#define CMD_HALT        1
#define CMD_RUN         2
#define CMD_RESET       3
#define CMD_HOLD        4   /* optional: not implemented */
#define CMD_IDENTIFY    5   /* optional: not implemented */

/* FAULT */
#define FAULT_BUS       1   /* program drives A/B on a bus */
#define FAULT_PROGRAM   3   /* program stopped with an error */

#define SECHS_IN_SIZE   32  /* console input buffer (power of 2) */
#define SECHS_OUT_SIZE  64  /* console output buffer (power of 2) */

typedef struct {
    /* registers 0x00-0x06 as read by the master; the target keeps
     * STATUS, OK and FAULT up to date (sechs_init sets the others) */
    uint8_t r[SR_FAULT + 1];
    uint8_t addr;           /* current address */
    uint8_t new_addr;       /* to apply at STOP, 0 if none */
    uint8_t networked;      /* a master has addressed the module */
    uint8_t cmd;            /* pending CONTROL command, 0 if none */
    uint8_t con_active;     /* the I2C console has been used */
    uint8_t con_break;      /* Ctrl-C received on the I2C console */
} sechs_t;

extern volatile sechs_t sechs;

void sechs_init(uint8_t addr, uint8_t caps);

/* ---- called by the target's I2C interrupt handler ---- */
void sechs_start(uint8_t general_call);   /* addressed (write or read) */
void sechs_rx(uint8_t b);                 /* byte received */
uint8_t sechs_tx(void);                   /* byte to send */
void sechs_stop(uint8_t unsent);          /* end of a transfer; unsent:
                                             bytes loaded by sechs_tx but
                                             never sent (read prefetch) */

/* ---- called by the target's main loop ---- */
int sechs_getc(void);                     /* console input, -1 if none */
void sechs_putc(char c);                  /* console output */

/* ---- implemented by the target ---- */
/* the console output is full: wait about 2 ms (the master reads
 * meanwhile, in the I2C interrupt); after 255 waits without room, the
 * console counts as left */
void sechs_wait(void);
void sechs_set_addr(uint8_t addr);        /* new address: hardware and
                                             persistent storage */
uint8_t sechs_info(uint8_t i);            /* byte i of INFO, 0 at end */
uint8_t *sechs_regs(void);                /* the 16 program registers */

#endif
