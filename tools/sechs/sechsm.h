#ifndef SECHSM_H
#define SECHSM_H

/*
 * sechsm: the master side of Sechs (docs/sechs.md), independent of how the
 * bus is reached. sechsctl uses it over Linux I2C or Werkzeug's bridge;
 * Zeitlos's sechs command over its bit-banged I2C. Plain C: no
 * allocation, no stdio, no operating system.
 */

#include <stdint.h>
#include "../../sechs/sechs.h"      /* register numbers, commands, CAPS */

/* How the bus is reached. write: one write transfer. read: write the
 * register number, repeated start, read n bytes (n <= 64). Both return 0,
 * or nonzero if the module did not answer. idle: a short pause between
 * polls (a millisecond or two). ms: a millisecond clock. out: one
 * character of console output (carriage returns are already dropped). */
typedef struct {
    int (*write)(void *ctx, uint8_t addr, const uint8_t *d, int n);
    int (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *d, int n);
    void (*idle)(void *ctx);
    long (*ms)(void *ctx);
    void (*out)(void *ctx, char c);
    void *ctx;
} sm_bus;

#define SM_INFO_MAX 64              /* INFO is read in one transfer */

typedef struct {
    uint8_t version, caps, status, ok, fault;
    char info[SM_INFO_MAX + 1];     /* INFO, up to its terminating zero */
} sm_info_t;

/* the STATUS bits, bit 0 first: "boot", "halted", ... */
extern const char *const sm_status_names[7];

int sm_reg_write(const sm_bus *b, uint8_t addr, uint8_t reg, const uint8_t *d, int n);
int sm_reg(const sm_bus *b, uint8_t addr, uint8_t reg);   /* its value, or -1 */
int sm_is_module(const sm_bus *b, uint8_t addr);          /* 1 if it says S6 */

/* the identification registers and INFO: 0, or -1 if no module answers */
int sm_info(const sm_bus *b, uint8_t addr, sm_info_t *i);

/* the modules at 0x08-0x77, at most max of them: how many */
int sm_scan(const sm_bus *b, uint8_t *found, int max);

/* CONTROL: CMD_HALT, CMD_RUN, CMD_RESET or CMD_PROGRAM. 0, -1 if the
 * module did not answer, -2 for PROGRAM to a module without a
 * programming mode (CAPS bit 7) */
int sm_control(const sm_bus *b, uint8_t addr, uint8_t cmd);

/* ADDR: the module moves to new_addr (0x08-0x77). 0, -1 for an address
 * outside that range, -2 if the module is not found at the new address */
int sm_set_address(const sm_bus *b, uint8_t addr, uint8_t new_addr);

/* The I2C console. drain: everything printed so far, to out(); how many
 * bytes, or -1. type: text into the console, never more than the module
 * can take (CIN); 0 or -1. settle: until nothing is printed for quiet_ms
 * (a module may take a moment to start answering). line: type a line
 * and a carriage return, then settle for 300 ms; 0 or -1. last_ok: 1 if
 * the last console command succeeded (OK bit 4), 0 if not, -1 if no
 * answer. */
int sm_drain(const sm_bus *b, uint8_t addr);
int sm_type(const sm_bus *b, uint8_t addr, const char *s, int len);
void sm_settle(const sm_bus *b, uint8_t addr, long quiet_ms);
int sm_line(const sm_bus *b, uint8_t addr, const char *s, int len);
int sm_last_ok(const sm_bus *b, uint8_t addr);

#endif
