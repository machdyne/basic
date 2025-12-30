/*
 * Simple Linked-List Filesystem for F-RAM
 * 
 * Layout:
 * - Header block at address 0
 * - File entries as linked list
 * - Data blocks following each file entry
 * 
 * Features:
 * - No fixed file limit (linked list)
 * - Support for files up to 4GB
 * - Minimal metadata
 * - Basic corruption recovery
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* External F-RAM interface */
extern uint8_t fram_read(int addr);
extern void fram_write(int addr, unsigned char d);
extern void fram_write_enable(void);

/* Configuration */
#ifndef FS_START_ADDR
#define FS_START_ADDR 0
#endif

#ifndef FS_SIZE
#define FS_SIZE (8 * 1024 * 1024)  /* Default 8MB */
#endif

#define FS_MAX_FILENAME 31
#define FS_MAGIC 0x46534250  /* "FSBP" - Filesystem BASIC */

/* Status codes */
#define FS_OK 0
#define FS_ERR_NOT_FOUND -1
#define FS_ERR_NO_SPACE -2
#define FS_ERR_EXISTS -3
#define FS_ERR_INVALID -4
#define FS_ERR_TOO_LARGE -5
#define FS_ERR_CORRUPT -6

/* Filesystem header */
typedef struct {
    uint32_t magic;
    uint32_t first_file;  /* Address of first file entry, 0 if none */
    uint32_t version;
} fs_header_t;

/* File entry structure */
typedef struct {
    char filename[FS_MAX_FILENAME + 1];
    uint32_t size;
    uint32_t next_file;  /* Address of next file entry, 0 if last */
    uint16_t checksum;   /* Simple checksum of header */
} fs_entry_t;

#define FS_HEADER_SIZE sizeof(fs_header_t)
#define FS_ENTRY_SIZE sizeof(fs_entry_t)

/* Internal helper functions */
static void read_bytes(int addr, uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = fram_read(addr + i);
    }
}

static void write_bytes(int addr, const uint8_t *buf, uint16_t len) {
    fram_write_enable();
    for (uint16_t i = 0; i < len; i++) {
        fram_write(addr + i, buf[i]);
    }
}

static uint16_t calculate_checksum(const fs_entry_t *entry) {
    uint16_t sum = 0;
    const uint8_t *ptr = (const uint8_t *)entry;
    /* Checksum everything except the checksum field itself */
    for (size_t i = 0; i < offsetof(fs_entry_t, checksum); i++) {
        sum += ptr[i];
    }
    return sum;
}

static void read_header(fs_header_t *header) {
    read_bytes(FS_START_ADDR, (uint8_t *)header, FS_HEADER_SIZE);
}

static void write_header(const fs_header_t *header) {
    write_bytes(FS_START_ADDR, (const uint8_t *)header, FS_HEADER_SIZE);
}

static void read_entry(uint32_t addr, fs_entry_t *entry) {
    read_bytes(addr, (uint8_t *)entry, FS_ENTRY_SIZE);
}

static void write_entry(uint32_t addr, const fs_entry_t *entry) {
    write_bytes(addr, (const uint8_t *)entry, FS_ENTRY_SIZE);
}

static int validate_entry(const fs_entry_t *entry) {
    uint16_t expected = calculate_checksum(entry);
    if (entry->checksum != expected) {
        return 0;
    }
    /* Check filename is null-terminated */
    int has_null = 0;
    for (int i = 0; i < FS_MAX_FILENAME + 1; i++) {
        if (entry->filename[i] == '\0') {
            has_null = 1;
            break;
        }
    }
    return has_null;
}

/* Initialize filesystem */
void fs_init(void) {
    fs_header_t header;
    read_header(&header);
    
    if (header.magic != FS_MAGIC) {
        /* Initialize new filesystem */
        header.magic = FS_MAGIC;
        header.first_file = 0;
        header.version = 1;
        write_header(&header);
    }
}

/* Format filesystem (erase all files) */
void fs_format(void) {
    fs_header_t header;
    header.magic = FS_MAGIC;
    header.first_file = 0;
    header.version = 1;
    write_header(&header);
}

/* Find a file by name, returns address or 0 if not found */
static uint32_t find_file(const char *filename, fs_entry_t *entry, uint32_t *prev_addr) {
    fs_header_t header;
    read_header(&header);
    
    if (header.magic != FS_MAGIC) {
        return 0;
    }
    
    uint32_t addr = header.first_file;
    uint32_t prev = 0;
    
    while (addr != 0 && addr < FS_START_ADDR + FS_SIZE) {
        read_entry(addr, entry);
        
        if (!validate_entry(entry)) {
            /* Corruption detected */
            return 0;
        }
        
        if (strcmp(entry->filename, filename) == 0) {
            if (prev_addr) *prev_addr = prev;
            return addr;
        }
        
        prev = addr;
        addr = entry->next_file;
    }
    
    if (prev_addr) *prev_addr = prev;
    return 0;
}

/* Find free space for new file */
static uint32_t find_free_space(uint32_t needed_size) {
    fs_header_t header;
    read_header(&header);
    
    if (header.first_file == 0) {
        /* No files yet, use space right after header */
        return FS_START_ADDR + FS_HEADER_SIZE;
    }
    
    /* Build a list of used regions */
    uint32_t addr = header.first_file;
    uint32_t max_used = FS_START_ADDR + FS_HEADER_SIZE;
    
    while (addr != 0 && addr < FS_START_ADDR + FS_SIZE) {
        fs_entry_t entry;
        read_entry(addr, &entry);
        
        if (!validate_entry(&entry)) {
            break;
        }
        
        uint32_t block_end = addr + FS_ENTRY_SIZE + entry.size;
        if (block_end > max_used) {
            max_used = block_end;
        }
        
        addr = entry.next_file;
    }
    
    /* Check if we have space */
    if (max_used + needed_size > FS_START_ADDR + FS_SIZE) {
        return 0;
    }
    
    return max_used;
}

/* Save a file */
int hw_save(const char *filename, uint8_t *data, uint16_t len) {
    if (!filename || strlen(filename) == 0 || strlen(filename) > FS_MAX_FILENAME) {
        return FS_ERR_INVALID;
    }
    
    fs_init();
    
    /* Check if file exists */
    fs_entry_t existing;
    uint32_t prev_addr;
    uint32_t existing_addr = find_file(filename, &existing, &prev_addr);
    
    if (existing_addr != 0) {
        /* File exists - delete it first */
        fs_header_t header;
        read_header(&header);
        
        if (existing_addr == header.first_file) {
            /* Deleting first file */
            header.first_file = existing.next_file;
            write_header(&header);
        } else {
            /* Update previous file's next pointer */
            fs_entry_t prev_entry;
            read_entry(prev_addr, &prev_entry);
            prev_entry.next_file = existing.next_file;
            prev_entry.checksum = calculate_checksum(&prev_entry);
            write_entry(prev_addr, &prev_entry);
        }
    }
    
    /* Find space for new file */
    uint32_t needed = FS_ENTRY_SIZE + len;
    uint32_t new_addr = find_free_space(needed);
    
    if (new_addr == 0) {
        return FS_ERR_NO_SPACE;
    }
    
    /* Create new entry */
    fs_entry_t new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    strncpy(new_entry.filename, filename, FS_MAX_FILENAME);
    new_entry.filename[FS_MAX_FILENAME] = '\0';
    new_entry.size = len;
    
    /* Insert at beginning of list */
    fs_header_t header;
    read_header(&header);
    new_entry.next_file = header.first_file;
    new_entry.checksum = calculate_checksum(&new_entry);
    
    /* Write entry */
    write_entry(new_addr, &new_entry);
    
    /* Write data */
    write_bytes(new_addr + FS_ENTRY_SIZE, data, len);
    
    /* Update header */
    header.first_file = new_addr;
    write_header(&header);
    
    return FS_OK;
}

/* Load a file */
int hw_load(const char *filename, uint8_t *data, uint16_t *len, uint16_t max_len) {
    if (!filename || !data || !len) {
        return FS_ERR_INVALID;
    }
    
    fs_init();
    
    fs_entry_t entry;
    uint32_t addr = find_file(filename, &entry, NULL);
    
    if (addr == 0) {
        return FS_ERR_NOT_FOUND;
    }
    
    if (entry.size > max_len) {
        return FS_ERR_TOO_LARGE;
    }
    
    /* Read data */
    read_bytes(addr + FS_ENTRY_SIZE, data, entry.size);
    *len = entry.size;
    
    return FS_OK;
}

/* List all files */
void hw_list(void) {
    fs_init();
    
    fs_header_t header;
    read_header(&header);
    
    if (header.magic != FS_MAGIC) {
        printf("Filesystem not initialized\n");
        return;
    }
    
    if (header.first_file == 0) {
        printf("No files\n");
        return;
    }
    
    printf("Files:\n");
    uint32_t addr = header.first_file;
    int count = 0;
    
    while (addr != 0 && addr < FS_START_ADDR + FS_SIZE) {
        fs_entry_t entry;
        read_entry(addr, &entry);
        
        if (!validate_entry(&entry)) {
            printf("  [CORRUPT at 0x%08X]\n", (unsigned int)addr);
            break;
        }
        
        printf("  %-32s %10u bytes\n", entry.filename, (unsigned int)entry.size);
        count++;
        addr = entry.next_file;
        
        /* Safety check to prevent infinite loops */
        if (count > 1000) {
            printf("  [List truncated - possible corruption]\n");
            break;
        }
    }
    
    printf("Total: %d file(s)\n", count);
}

/* Delete a file */
int hw_delete(const char *filename) {
    if (!filename) {
        return FS_ERR_INVALID;
    }
    
    fs_init();
    
    fs_entry_t entry;
    uint32_t prev_addr;
    uint32_t addr = find_file(filename, &entry, &prev_addr);
    
    if (addr == 0) {
        return FS_ERR_NOT_FOUND;
    }
    
    fs_header_t header;
    read_header(&header);
    
    if (addr == header.first_file) {
        /* Deleting first file */
        header.first_file = entry.next_file;
        write_header(&header);
    } else {
        /* Update previous file's next pointer */
        fs_entry_t prev_entry;
        read_entry(prev_addr, &prev_entry);
        prev_entry.next_file = entry.next_file;
        prev_entry.checksum = calculate_checksum(&prev_entry);
        write_entry(prev_addr, &prev_entry);
    }
    
    return FS_OK;
}

/* Check and repair filesystem */
int fs_check(void) {
    fs_header_t header;
    read_header(&header);
    
    if (header.magic != FS_MAGIC) {
        printf("FS: Invalid magic number\n");
        return FS_ERR_CORRUPT;
    }
    
    printf("FS: Checking filesystem...\n");
    
    uint32_t addr = header.first_file;
    int count = 0;
    int errors = 0;
    uint32_t prev_addr = 0;
    
    while (addr != 0 && addr < FS_START_ADDR + FS_SIZE) {
        fs_entry_t entry;
        read_entry(addr, &entry);
        
        if (!validate_entry(&entry)) {
            printf("FS: Corrupt entry at 0x%08X\n", (unsigned int)addr);
            errors++;
            
            /* Try to repair by truncating list */
            if (prev_addr == 0) {
                header.first_file = 0;
                write_header(&header);
            } else {
                fs_entry_t prev_entry;
                read_entry(prev_addr, &prev_entry);
                prev_entry.next_file = 0;
                prev_entry.checksum = calculate_checksum(&prev_entry);
                write_entry(prev_addr, &prev_entry);
            }
            printf("FS: Truncated file list at corruption point\n");
            break;
        }
        
        count++;
        prev_addr = addr;
        addr = entry.next_file;
        
        if (count > 1000) {
            printf("FS: Possible infinite loop detected\n");
            errors++;
            break;
        }
    }
    
    printf("FS: Check complete. %d files, %d errors\n", count, errors);
    return errors == 0 ? FS_OK : FS_ERR_CORRUPT;
}
