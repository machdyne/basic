/*
 * A simulated CH32V003 or CH32V005 for the programmer's tests: a debug
 * module and a flash controller that are stricter than the real chip. Anything the
 * failsafe rules forbid (docs/ch32prog.md) is counted in `violations`.
 * Provides ch32_dmi_write/read, ch32_reset_line and ch32_delay_us.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "../ch32prog.h"

/* ---- the simulated chip -------------------------------------------------------- */

static struct {
    /* kept across power cuts */
    uint8_t flash[CH32_FLASH_MAX];
    uint32_t size, page;    /* bytes of flash, bytes per page */
    uint32_t chip_id;       /* the word at 0x1FFFF7C4 (CH32V003) */
    uint32_t id7f;          /* debug register 0x7F */
    uint32_t hartinfo;
    int rdprt;              /* read protection set */
    int absent;             /* nothing on the wire */
    int swio_off_fw;        /* its firmware turns SWIO off once it runs */
    int reset_wired;        /* the programmer's RESETN reaches it */
    /* lost at a power cut or reset */
    int cfg_ok, halted, haltreq, running, in_reset, boot_left;
    uint32_t data0, progbuf[8], x8, x9, cmderr;
    int locked, flocked, key_n, mkey_n;
    uint32_t ctlr, addr, statr, latch_a, latch_v, pagebuf[CH32_PAGE_MAX / 4];
} chip;

static long violations, key_writes, dmi_n;
static long cut_at = -1, corrupt_at = -1;
static jmp_buf cut;

static void violation(const char *what, uint32_t a) {
    if (violations++ < 5) printf("  violation: %s at %08x\n", what, a);
}

/* power on (or a reset): the firmware starts unless halted */
static void chip_power(void) {
    chip.cfg_ok = chip.halted = chip.haltreq = chip.in_reset = 0;
    chip.running = 1;
    chip.boot_left = 4;     /* transactions until its firmware turns SWIO off */
    chip.cmderr = 0;
    chip.locked = chip.flocked = 1;
    chip.key_n = chip.mkey_n = 0;
    chip.ctlr = chip.statr = chip.addr = 0;
}

static void chip_new_as(uint32_t fill, int v005) {
    memset(&chip, 0, sizeof(chip));
    chip.size = v005 ? 32768 : 16384;
    chip.page = v005 ? 256 : 64;
    chip.id7f = v005 ? 0x00500500 : 0x00300500;
    chip.chip_id = v005 ? 0xFFFFFFFF : 0x00300500;  /* (V005: elsewhere) */
    chip.hartinfo = v005 ? 0x00212000 : 0x002120f4; /* (V005: not checked) */
    for (uint32_t i = 0; i < chip.size; i++)
        chip.flash[i] = (uint8_t)(fill + i * 7);
    chip.reset_wired = 1;
    chip_power();
}

static void chip_new(uint32_t fill) { chip_new_as(fill, 0); }        /* CH32V003 */
static __attribute__((unused)) void chip_new_v005(uint32_t fill) { chip_new_as(fill, 1); }   /* CH32V005 */

/* SWIO answers unless the chip is absent, or its firmware turned SWIO off
 * (while running, not in reset or halted) */
static int answers(void) {
    if (chip.absent || chip.in_reset) return 0;    /* nothing during reset */
    if (chip.running && !chip.halted && chip.boot_left > 0) chip.boot_left--;
    if (chip.swio_off_fw && chip.running && !chip.halted && chip.boot_left == 0)
        return 0;
    return 1;
}

static uint32_t mem_rd(uint32_t a) {
    if (a >= CH32_FLASH && a < CH32_FLASH + chip.size - 3) {
        uint32_t o = a - CH32_FLASH;
        return chip.flash[o] | chip.flash[o + 1] << 8 |
               chip.flash[o + 2] << 16 | (uint32_t)chip.flash[o + 3] << 24;
    }
    if (a == 0x1FFFF7C4) return chip.chip_id;
    if (a == 0x4002201C) return chip.rdprt ? 2 : 0;
    if (a == 0x40022010) return chip.ctlr | (chip.locked ? 0x80 : 0) | (chip.flocked ? 0x8000 : 0);
    if (a == 0x4002200C) return chip.statr;
    return 0;
}

/* a torn erase or program: some bytes changed, some not */
static void tear_page(uint32_t a, int erase) {
    for (uint32_t i = 0; i < chip.page; i++) {
        if (rand() & 1) continue;
        uint8_t *b = &chip.flash[a - CH32_FLASH + i];
        if (erase) *b = 0xFF;
        else *b &= ((uint8_t *)chip.pagebuf)[i] | (uint8_t)rand();
    }
}

static void mem_wr(uint32_t a, uint32_t v) {
    if (a >= CH32_FLASH && a < CH32_FLASH + chip.size) {
        if (!(chip.ctlr & 0x10000) || chip.locked || chip.flocked)
            violation("write to flash outside page programming", a);
        chip.latch_a = a;
        chip.latch_v = v;
        return;
    }
    switch (a) {
    case 0x40022004:    /* KEYR */
        key_writes++;
        if (chip.key_n == 0 && v == 0x45670123) chip.key_n = 1;
        else if (chip.key_n == 1 && v == 0xCDEF89AB) chip.locked = 0;
        else chip.key_n = 0;
        return;
    case 0x40022024:    /* MODEKEYR */
        key_writes++;
        if (chip.mkey_n == 0 && v == 0x45670123) chip.mkey_n = 1;
        else if (chip.mkey_n == 1 && v == 0xCDEF89AB && !chip.locked) chip.flocked = 0;
        else chip.mkey_n = 0;
        return;
    case 0x4002200C: chip.statr = 0; return;
    case 0x40022014: chip.addr = v; return;
    case 0x40022010:    /* CTLR */
        if (v & 0x80) {
            chip.locked = chip.flocked = 1;
            chip.ctlr = 0;
            return;
        }
        if (chip.locked || chip.flocked) {
            chip.statr |= 0x10;     /* WRPRTERR */
            return;
        }
        chip.ctlr = v & ~0x40u;
        if ((v & 0x80000) && (v & 0x10000)) memset(chip.pagebuf, 0xFF, chip.page);
        if ((v & 0x40000) && (v & 0x10000))
            chip.pagebuf[(chip.latch_a & (chip.page - 1)) / 4] = chip.latch_v;
        if (v & 0x40) {         /* STRT */
            uint32_t p = chip.addr;
            if (p < CH32_FLASH || p >= CH32_FLASH + chip.size || (p & (chip.page - 1))) {
                violation("erase or program outside the main flash", p);
                chip.statr |= 0x10;
                return;
            }
            int erase = (v & 0x20000) != 0;
            if (dmi_n == cut_at) {
                tear_page(p, erase);
                longjmp(cut, 1);
            }
            if (erase) memset(&chip.flash[p - CH32_FLASH], 0xFF, chip.page);
            else if (v & 0x10000)
                for (uint32_t i = 0; i < chip.page; i++)
                    chip.flash[p - CH32_FLASH + i] &= ((uint8_t *)chip.pagebuf)[i];
            chip.statr |= 0x20;     /* EOP */
        }
        return;
    }
    if (a == 0x40022008) violation("option byte key (OBKEYR)", a);
    else if (a >= 0x1FFFF800 && a < 0x1FFFF900) violation("option bytes", a);
    else violation("write to an unexpected address", a);
}

static void execute(void) {
    for (int i = 0; i < 16; i++) {
        uint16_t ins = (chip.progbuf[i / 2] >> (16 * (i & 1))) & 0xFFFF;
        if (ins == 0x9002) return;                       /* c.ebreak */
        if (ins == 0x4000) chip.x8 = mem_rd(chip.x8);    /* c.lw x8,0(x8) */
        else if (ins == 0xC004) mem_wr(chip.x8, chip.x9);/* c.sw x9,0(x8) */
        else {
            chip.cmderr = 3;
            return;
        }
    }
}

static void command(uint32_t c) {
    if (chip.cmderr) return;                    /* ignored until cleared */
    if (!chip.halted) {
        chip.cmderr = 4;
        return;
    }
    if ((c >> 24) != 0 || ((c >> 20) & 7) != 2) {
        chip.cmderr = 2;
        return;
    }
    if (c & 0x20000) {                          /* transfer */
        uint32_t *r = (c & 0xFFFF) == 0x1008 ? &chip.x8 :
                      (c & 0xFFFF) == 0x1009 ? &chip.x9 : NULL;
        if (!r) {
            chip.cmderr = 2;
            return;
        }
        if (c & 0x10000) *r = chip.data0;
        else chip.data0 = *r;
    }
    if (c & 0x40000) execute();
}

int ch32_dmi_write(uint8_t reg, uint32_t v) {
    if (++dmi_n == cut_at) longjmp(cut, 1);
    if (!answers()) return 0;                   /* (writes are not acknowledged) */
    switch (reg) {
    case 0x7D: case 0x7E:
        if ((v >> 16) == 0x5AA5) chip.cfg_ok = 1;
        break;
    case 0x10:
        if (v & 2) {                            /* core reset */
            chip_power();
            chip.cfg_ok = 1;
            chip.halted = (v >> 31) & 1;
            chip.running = !chip.halted;
        } else if (v >> 31) {
            chip.haltreq = 1;
            if (!chip.in_reset) chip.halted = 1, chip.running = 0;
        } else {
            chip.haltreq = 0;
        }
        if ((v >> 30) & 1) chip.halted = 0, chip.running = 1;
        break;
    case 0x04: chip.data0 = v; break;
    case 0x16: if (v & 0x700) chip.cmderr = 0; break;
    case 0x17: command(v); break;
    default:
        if (reg >= 0x20 && reg < 0x28) chip.progbuf[reg - 0x20] = v;
    }
    return 0;
}

int ch32_dmi_read(uint8_t reg, uint32_t *v) {
    if (++dmi_n == cut_at) longjmp(cut, 1);
    if (!answers() || !chip.cfg_ok) return -1;
    switch (reg) {
    case 0x7D: *v = 0x5AA50400; break;
    case 0x11: *v = 0x00000002 | (chip.halted ? 0x300 : 0); break;
    case 0x12: *v = chip.hartinfo; break;
    case 0x7F: *v = chip.id7f; break;
    case 0x04: *v = chip.data0; break;
    case 0x16: *v = 0x08000002 | chip.cmderr << 8; break;
    default: *v = 0;
    }
    if (dmi_n == corrupt_at) *v ^= 1u << (rand() & 31);
    return 0;
}

void ch32_reset_line(int hold) {
    if (!chip.reset_wired) return;
    if (hold) {
        chip.in_reset = 1;
        chip.running = 0;
    } else if (chip.in_reset) {
        chip_power();       /* restarts: its firmware begins to run */
    }
}

void ch32_delay_us(uint32_t us) { (void)us; }
