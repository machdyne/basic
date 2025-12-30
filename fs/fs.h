/*
 * Simple Linked-List Filesystem for F-RAM
 * Public Interface
 */

#ifndef FS_H
#define FS_H

#include <stdint.h>

/* Status codes */
#define FS_OK 0
#define FS_ERR_NOT_FOUND -1
#define FS_ERR_NO_SPACE -2
#define FS_ERR_EXISTS -3
#define FS_ERR_INVALID -4
#define FS_ERR_TOO_LARGE -5
#define FS_ERR_CORRUPT -6

/* Initialize filesystem (call once at startup) */
void fs_init(void);

/* Format filesystem (erase all files) */
void fs_format(void);

/* Save a file (overwrites if exists) */
int hw_save(const char *filename, uint8_t *data, uint16_t len);

/* Load a file */
int hw_load(const char *filename, uint8_t *data, uint16_t *len, uint16_t max_len);

/* List all files to stdout */
void hw_list(void);

/* Delete a file */
int hw_delete(const char *filename);

/* Check filesystem integrity and attempt repair */
int fs_check(void);

#endif /* FS_H */
