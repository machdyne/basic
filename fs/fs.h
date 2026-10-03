/*
 * Machdyne BASIC filesystem
 *
 * A small block filesystem for F-RAM and EEPROM, from 1KB to 4MB, that
 * survives power loss at any moment and spreads wear. See docs/fs.md.
 *
 * Files contain text. The byte 0xFF marks the end of a file and cannot be
 * written into one.
 *
 * Writes of a single byte are assumed to be atomic (they either happen or
 * not); a power cut may interrupt any longer write at any byte.
 */

#ifndef FS_H
#define FS_H

#include <stdint.h>

/* Status codes */
#define FS_OK               0
#define FS_ERR_NOT_FOUND   -1   /* no such file */
#define FS_ERR_FULL        -2   /* no free blocks */
#define FS_ERR_DIR_FULL    -3   /* no free directory entries */
#define FS_ERR_INVALID     -4   /* bad name or argument, or 0xFF in data */
#define FS_ERR_CORRUPT     -5   /* damaged chain found while reading */
#define FS_ERR_UNFORMATTED -6   /* no valid filesystem on the medium */
#define FS_ERR_BUSY        -7   /* a file is already open */
#define FS_ERR_NOT_OPEN    -8   /* no file open in the right mode */
#define FS_ERR_TOO_BIG     -9   /* more blocks than this build supports */
#define FS_ERR_IO         -10   /* the media driver reported an error */

/* Open modes */
#define FS_READ    1
#define FS_WRITE   2   /* create or replace; replaces atomically at close */
#define FS_APPEND  3   /* create if missing; every write is kept */

/* Largest number of data blocks this build can mount (RAM: 1 bit each);
 * at most 65534. */
#ifndef FS_MAX_BLOCKS
#define FS_MAX_BLOCKS 128
#endif

/*
 * Media driver, supplied by the target. Addresses are byte offsets from
 * the start of the filesystem area. prog() writes bytes; it is used both
 * for data and for returning bytes to 0xFF. Return 0 on success.
 */
int fs_media_read(uint32_t addr, uint8_t *buf, uint16_t len);
int fs_media_prog(uint32_t addr, const uint8_t *buf, uint16_t len);

#ifdef FS_NOR
/* NOR flash (build with -DFS_NOR): fs_media_prog can only clear bits, and
 * the filesystem erases whole sectors of FS_NOR_SECTOR bytes, which are its
 * blocks. fs_media_erase sets the sector at addr (aligned) to 0xFF. */
#define FS_NOR_SHIFT  12
#define FS_NOR_SECTOR (1u << FS_NOR_SHIFT)
int fs_media_erase(uint32_t addr);
#endif

/* Format a medium of 'size' bytes. block_shift: block size = 1 << shift
 * (5..12). dir_entries: maximum number of files plus one (2..1024). */
int fs_format(uint32_t size, uint8_t block_shift, uint16_t dir_entries);

/* Format with the default block size and directory size for 'size'. */
int fs_format_default(uint32_t size);

/* Mount a medium of 'size' bytes. Must succeed before any other call. */
int fs_mount(uint32_t size);

/* One file can be open at a time. */
/* Names are 1-12 printable characters, compared exactly; callers pass
 * them in one normalized form (Machdyne BASIC: 8.3, upper case). */
int fs_open(const char *name, uint8_t mode);
int fs_read(uint8_t *buf, uint16_t len);          /* bytes read, 0 at end */
int fs_write(const uint8_t *buf, uint16_t len);   /* FS_OK or error */
int fs_close(void);
void fs_abort(void);   /* close; a file opened with FS_WRITE is discarded */

int fs_delete(const char *name);

/* Call cb once per file with its name. */
typedef void (*fs_dir_cb)(const char *name, void *ctx);
int fs_dir(fs_dir_cb cb, void *ctx);

uint32_t fs_free(void);       /* free bytes */
uint8_t fs_degraded(void);    /* 1 if damage was found */

#endif /* FS_H */
