/*
 * The interpreter with the module filesystem, on the host.
 *
 * Runs basic.c exactly as a module target does (files go through fs.c),
 * with an 8KB F-RAM simulated by an image file, so that the module file
 * path can be tested without hardware. Used by testsuite.sh.
 *
 *   ./basic_fs [image]      (default: ZZFRAM.IMG)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../../basic.h"

#define SIZE    8192            /* the F-RAM */
#define FS_SIZE SIZE

static int img;

int fs_media_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    if (addr + len > SIZE) return -1;
    return pread(img, buf, len, addr) == len ? 0 : -1;
}

int fs_media_prog(uint32_t addr, const uint8_t *buf, uint16_t len) {
    if (addr + len > SIZE) return -1;
    return pwrite(img, buf, len, addr) == len ? 0 : -1;
}

void hw_putc(char c) { putchar(c); }
int hw_break(void) { return 0; }
void hw_delay_ms(uint16_t ms) { usleep((useconds_t)ms * 1000); }

/* no pins on this target */
int hw_pin_mode(uint8_t pin, uint8_t mode) {
    (void)pin;
    return mode == PM_NONE || mode == PM_NET ? 0 : HW_ERR_UNSUPPORTED;
}
void hw_pin_write(uint8_t pin, uint8_t level) { (void)pin; (void)level; }
uint8_t hw_pin_read(uint8_t pin) { (void)pin; return 0; }
int16_t hw_adc(uint8_t pin) { (void)pin; return -1; }
void hw_led(uint8_t on) { (void)on; }
int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r,
           uint8_t rn) {
    (void)addr; (void)w; (void)wn; (void)r; (void)rn;
    return -1;
}

int hw_fopen(const char *name, uint8_t mode) { return fs_open(name, mode); }
int hw_fread(uint8_t *buf, uint16_t len) { return fs_read(buf, len); }
int hw_fwrite(const uint8_t *buf, uint16_t len) { return fs_write(buf, len); }
int hw_fclose(void) { return fs_close(); }
void hw_fabort(void) { fs_abort(); }
int hw_fdelete(const char *name) { return fs_delete(name); }
int hw_fdir(fs_dir_cb cb) { return fs_dir(cb, 0); }
int hw_fformat(void) { return fs_format_default(FS_SIZE); }

int main(int argc, char **argv) {
    char line[256];
    const char *path = argc > 1 ? argv[1] : "ZZFRAM.IMG";
    int fresh = access(path, F_OK) != 0;

    img = open(path, O_RDWR | O_CREAT, 0644);
    if (img < 0) return 1;
    if (fresh) {
        /* a new F-RAM: random contents, like a real part */
        for (int i = 0; i < SIZE; i++) {
            uint8_t b = rand();
            if (write(img, &b, 1) != 1) return 1;
        }
    }

    puts("///");
    if (fs_mount(FS_SIZE) == FS_ERR_UNFORMATTED)
        puts("NOT FORMATTED");
    if (basic_boot()) basic_yield((uint8_t *)"RUN");

    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        basic_yield((uint8_t *)line);
    }
    return 0;
}
