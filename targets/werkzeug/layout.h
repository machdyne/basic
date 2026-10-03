/*
 * Where Werkzeug keeps its files, decided from the flash chip's JEDEC
 * capacity code (the chip holds 2^code bytes) and the end of the firmware.
 *
 * The files take the upper half of the flash, at most WZ_FILES_MAX (what
 * the filesystem mounts: 512 blocks of 4KB), at the end of the flash:
 *
 *   1MB (earlier Werkzeug)   512KB of files at 512KB
 *   2MB                      1MB at 1MB
 *   4MB (Werkzeug V3C)       2MB at 2MB
 *   8MB or more              2MB at the last 2MB
 *
 * An unknown code counts as 1MB, the smallest. If the firmware reaches
 * into that area, there are no files rather than a damaged firmware.
 * Kept free of the Pico SDK so the test suite can check it.
 */

#ifndef WZ_LAYOUT_H
#define WZ_LAYOUT_H

#include <stdint.h>

#define WZ_FILES_MAX (2u << 20)

/* 1 and the files' offset and size in the flash, or 0: no room */
static inline int wz_layout(uint8_t code, uint32_t fw_end,
                            uint32_t *offset, uint32_t *size) {
    uint32_t flash = code >= 20 && code <= 24 ? 1u << code : 1u << 20;
    uint32_t files = flash / 2;
    if (files > WZ_FILES_MAX) files = WZ_FILES_MAX;
    if (fw_end > flash - files) return 0;
    *offset = flash - files;
    *size = files;
    return 1;
}

#endif
