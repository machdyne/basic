/*
 * ch32prog: failsafe CH32V003 programming over SWIO. See ch32prog.h and
 * docs/ch32prog.md.
 *
 * Debug register use and the flash sequence follow WCH's QingKeV2 debug
 * manual and the CH32V003 reference manual, as used by Charles Lohr's
 * ch32v003_swio.h (MIT/NewBSD). This version is deliberately simple: each
 * memory access is self-contained (set x8/x9, run one instruction from
 * the program buffer), and every write to the target passes one gate that
 * allows only the main flash and the flash controller registers needed.
 */

#include <string.h>
#include "ch32prog.h"

/* debug module registers */
#define DATA0       0x04
#define DMCONTROL   0x10
#define DMSTATUS    0x11
#define HARTINFO    0x12
#define ABSTRACTCS  0x16
#define COMMAND     0x17
#define AUTOEXEC    0x18
#define PROGBUF0    0x20
#define CFGR        0x7D
#define SHDWCFGR    0x7E

/* abstract commands: 32-bit register transfer, optionally then execute */
#define CMD_READ_X8     0x00221008u
#define CMD_WRITE_X8    0x00231008u
#define CMD_WRITE_X9    0x00231009u
#define CMD_EXEC        0x00240000u

/* program buffer: one access, then c.ebreak */
#define PB_LW_X8_X8     0x90024000u     /* c.lw x8, 0(x8) */
#define PB_SW_X9_X8     0x9002C004u     /* c.sw x9, 0(x8) */

/* flash controller */
#define FLASH_KEYR      0x40022004u
#define FLASH_STATR     0x4002200Cu
#define FLASH_CTLR      0x40022010u
#define FLASH_ADDR      0x40022014u
#define FLASH_OBR       0x4002201Cu
#define FLASH_MODEKEYR  0x40022024u
#define KEY1            0x45670123u
#define KEY2            0xCDEF89ABu
#define CTLR_LOCK       0x00000080u
#define CTLR_FLOCK      0x00008000u
#define CTLR_STRT       0x00000040u
#define CTLR_PAGE_PG    0x00010000u
#define CTLR_PAGE_ER    0x00020000u
#define CTLR_BUF_LOAD   0x00040000u
#define CTLR_BUF_RST    0x00080000u
#define STATR_BSY       0x00000001u
#define STATR_WRPRTERR  0x00000010u
#define OBR_RDPRT       0x00000002u

/* identification */
#define CHIPID_ADDR     0x1FFFF7C4u     /* CH32V003: 0x003xxxxx */
#define HARTINFO_V003   0x0f4           /* DATA0 at 0xe00000f4 */

uint32_t ch32_chip_id, ch32_hartinfo;
uint32_t ch32_refused;

/* ---- debug access ------------------------------------------------------ */

static int done(void) {
    uint32_t cs;
    for (int t = 0; t < 1000; t++) {
        if (ch32_dmi_read(ABSTRACTCS, &cs)) return -1;
        if (!(cs & (1u << 12))) {               /* not busy */
            if (cs & (7u << 8)) {               /* cmderr */
                ch32_dmi_write(ABSTRACTCS, 7u << 8);
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

static int mem_read(uint32_t addr, uint32_t *val) {
    if (ch32_dmi_write(PROGBUF0, PB_LW_X8_X8) ||
        ch32_dmi_write(DATA0, addr) ||
        ch32_dmi_write(COMMAND, CMD_WRITE_X8 | 0x40000u) ||  /* and execute */
        done() ||
        ch32_dmi_write(COMMAND, CMD_READ_X8) || done() ||
        ch32_dmi_read(DATA0, val)) return -1;
    return 0;
}

/* The write gate: the main flash and the flash controller registers that
 * programming needs. Never OBKEYR, the option bytes or anything else. */
static int allowed(uint32_t addr) {
    if (addr >= CH32_FLASH && addr < CH32_FLASH + CH32_FLASH_SIZE) return 1;
    return addr == FLASH_KEYR || addr == FLASH_STATR || addr == FLASH_CTLR ||
           addr == FLASH_ADDR || addr == FLASH_MODEKEYR;
}

static int mem_write(uint32_t addr, uint32_t val) {
    if (!allowed(addr)) {
        ch32_refused++;
        return -2;
    }
    if (ch32_dmi_write(PROGBUF0, PB_SW_X9_X8) ||
        ch32_dmi_write(DATA0, val) ||
        ch32_dmi_write(COMMAND, CMD_WRITE_X9) || done() ||
        ch32_dmi_write(DATA0, addr) ||
        ch32_dmi_write(COMMAND, CMD_WRITE_X8 | 0x40000u) || done())
        return -1;
    return 0;
}

/* wait for the flash controller; clear its flags */
static int flash_wait(void) {
    uint32_t s = STATR_BSY;
    for (int t = 0; t < 2000 && (s & STATR_BSY); t++)
        if (mem_read(FLASH_STATR, &s)) return -1;
    if (mem_write(FLASH_STATR, 0)) return -1;
    return (s & (STATR_BSY | STATR_WRPRTERR)) ? -1 : 0;
}

/* ---- steps --------------------------------------------------------------- */

/* With RESETN wired, restart the target and, straight after the reset,
 * before its firmware gets far (even firmware that turns SWIO off), enable
 * the debug output and ask it to halt. Without RESETN this simply talks to
 * the running target. */
static int connect(void) {
    uint32_t v = 0;
    ch32_reset_line(1);
    ch32_delay_us(1000);
    ch32_reset_line(0);
    ch32_dmi_write(SHDWCFGR, 0x5AA50400u);
    ch32_dmi_write(CFGR, 0x5AA50400u);
    ch32_dmi_write(DMCONTROL, 0x80000001u);     /* debug module on, halt */
    for (int i = 0; i < 2; i++) {               /* the key reads back */
        ch32_dmi_write(SHDWCFGR, 0x5AA50400u);  /* only from a chip */
        ch32_dmi_write(CFGR, 0x5AA50400u);
    }
    if (ch32_dmi_read(CFGR, &v) || (v & 0xFFFF0000u) != 0x5AA50000u)
        return CH32_NO_CHIP;
    return CH32_OK;
}

/* Halt, then reset the core with the halt request held, so that it stops
 * at the reset vector before the old firmware runs again. */
static int halt(void) {
    uint32_t st = 0;
    ch32_dmi_write(DMCONTROL, 0x80000001u);     /* debug module on, halt */
    ch32_dmi_write(DMCONTROL, 0x80000001u);
    ch32_dmi_write(DMCONTROL, 0x80000003u);     /* core reset, keep halt */
    ch32_delay_us(2000);
    ch32_dmi_write(DMCONTROL, 0x80000001u);     /* end reset, keep halt */
    ch32_dmi_write(DMCONTROL, 0x90000001u);     /* clear havereset */
    for (int t = 0; t < 100; t++) {
        if (!ch32_dmi_read(DMSTATUS, &st) && (st & (1u << 9))) {  /* allhalted */
            ch32_dmi_write(DMCONTROL, 0x00000001u);   /* clear the request */
            ch32_dmi_write(AUTOEXEC, 0);
            ch32_dmi_write(ABSTRACTCS, 7u << 8);
            return CH32_OK;
        }
        ch32_delay_us(1000);
    }
    return CH32_NO_HALT;
}

static int identify(void) {
    uint32_t obr = 0;
    if (ch32_dmi_read(HARTINFO, &ch32_hartinfo) ||
        mem_read(CHIPID_ADDR, &ch32_chip_id)) return CH32_NO_CHIP;
    if ((ch32_hartinfo & 0x7FF) != HARTINFO_V003 || (ch32_chip_id >> 20) != 0x003)
        return CH32_WRONG_CHIP;
    if (mem_read(FLASH_OBR, &obr)) return CH32_NO_CHIP;
    if (obr & OBR_RDPRT) return CH32_LOCKED;
    return CH32_OK;
}

static int unlock(void) {
    uint32_t c = 0;
    if (mem_write(FLASH_KEYR, KEY1) || mem_write(FLASH_KEYR, KEY2) ||
        mem_write(FLASH_MODEKEYR, KEY1) || mem_write(FLASH_MODEKEYR, KEY2) ||
        mem_read(FLASH_CTLR, &c)) return CH32_FLASH_ERR;
    return (c & (CTLR_LOCK | CTLR_FLOCK)) ? CH32_FLASH_ERR : CH32_OK;
}

static int erase_page(uint32_t a) {
    if (flash_wait() ||
        mem_write(FLASH_CTLR, CTLR_PAGE_ER) ||
        mem_write(FLASH_ADDR, a) ||
        mem_write(FLASH_CTLR, CTLR_PAGE_ER | CTLR_STRT) ||
        flash_wait() || mem_write(FLASH_CTLR, 0)) return -1;
    return 0;
}

/* fast page programming: buffer reset, 16 words each loaded, start */
static int program_page(uint32_t a, const uint32_t *w) {
    if (mem_write(FLASH_CTLR, CTLR_PAGE_PG) ||
        mem_write(FLASH_CTLR, CTLR_PAGE_PG | CTLR_BUF_RST) || flash_wait())
        return -1;
    for (int i = 0; i < 16; i++) {
        if (mem_write(a + 4 * i, w[i]) ||
            mem_write(FLASH_CTLR, CTLR_PAGE_PG | CTLR_BUF_LOAD) || flash_wait())
            return -1;
    }
    if (mem_write(FLASH_ADDR, a) ||
        mem_write(FLASH_CTLR, CTLR_PAGE_PG | CTLR_STRT) || flash_wait() ||
        mem_write(FLASH_CTLR, 0)) return -1;
    return 0;
}

static int page_matches(uint32_t a, const uint32_t *w) {
    for (int i = 0; i < 16; i++) {
        uint32_t v;
        if (mem_read(a + 4 * i, &v) || v != w[i]) return 0;
    }
    return 1;
}

static int image_ok(const uint8_t *img, uint32_t len, int force) {
    static const char id[] = "fw=Machdyne BASIC";
    if (!len || len > CH32_FLASH_SIZE) return 0;
    if (force) return 1;
    for (uint32_t i = 0; i + sizeof(id) - 1 <= len; i++)
        if (!memcmp(img + i, id, sizeof(id) - 1)) return 1;
    return 0;
}

/* ---- the whole job ------------------------------------------------------- */

int ch32_flash(const uint8_t *img, uint32_t len, int force,
               void (*progress)(int)) {
    int r;
    ch32_refused = 0;
    if (!image_ok(img, len, force)) return CH32_BAD_IMAGE;     /* rule 4 */
    if ((r = connect())) return r;
    if ((r = halt())) return r;                                /* rule 5 */
    if ((r = identify())) return r;                            /* rule 3 */
    if ((r = unlock())) return r;

    /* every page of the main flash: erase, program (the image, then 0xFF),
     * verify; up to three attempts per page (rule 6) */
    uint32_t pages = CH32_FLASH_SIZE / CH32_PAGE;
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t a = CH32_FLASH + p * CH32_PAGE, w[16];
        memset(w, 0xFF, sizeof(w));
        if (p * CH32_PAGE < len) {
            uint32_t n = len - p * CH32_PAGE;
            memcpy(w, img + p * CH32_PAGE, n < CH32_PAGE ? n : CH32_PAGE);
        }
        int ok = 0;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            if (ch32_refused) return CH32_FORBIDDEN;
            if (erase_page(a)) continue;
            if (p * CH32_PAGE < len && program_page(a, w)) continue;
            ok = page_matches(a, w);
        }
        if (ch32_refused) return CH32_FORBIDDEN;
        if (!ok) return CH32_VERIFY;    /* left halted: try again */
        if (progress) progress((int)((p + 1) * 100 / pages));
    }

    /* lock the flash again and restart the target */
    mem_write(FLASH_CTLR, CTLR_LOCK);
    ch32_dmi_write(DMCONTROL, 0x00000003u);     /* core reset, no halt */
    ch32_delay_us(2000);
    ch32_dmi_write(DMCONTROL, 0x00000001u);
    ch32_dmi_write(DMCONTROL, 0x10000001u);     /* clear havereset */
    ch32_dmi_write(DMCONTROL, 0x40000001u);     /* resume */
    ch32_reset_line(1);                         /* and a hardware reset, */
    ch32_delay_us(1000);                        /* if RESETN is wired */
    ch32_reset_line(0);
    return ch32_refused ? CH32_FORBIDDEN : CH32_OK;
}

const char *ch32_message(int code) {
    switch (code) {
        case CH32_OK: return "written and verified";
        case CH32_NO_CHIP: return "no chip answers on SWIO (check the wiring)";
        case CH32_WRONG_CHIP: return "not a CH32V003: nothing written";
        case CH32_LOCKED: return "the chip is read-protected: nothing written";
        case CH32_BAD_IMAGE: return "not a Machdyne BASIC firmware (or too large)";
        case CH32_NO_HALT: return "the chip would not stop (wire RESETN and try again)";
        case CH32_FLASH_ERR: return "the flash could not be unlocked or written";
        case CH32_VERIFY: return "some pages did not verify: try again";
        case CH32_FORBIDDEN: return "refused an access outside the main flash (a bug)";
    }
    return "?";
}
