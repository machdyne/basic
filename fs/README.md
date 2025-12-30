# Simple F-RAM Filesystem for Embedded BASIC

A lightweight, linked-list based filesystem designed for F-RAM storage in embedded systems.

## Features

- **Dynamic file count**: No fixed limit on number of files (linked-list structure)
- **Large file support**: Files up to 4GB (uint32_t size)
- **Minimal metadata**: ~48 bytes per file entry
- **Corruption recovery**: Checksums and repair functionality
- **Small footprint**: Suitable for systems with 2KB to 8MB of F-RAM
- **Simple API**: Only 3 main functions needed

## Storage Layout

```
+------------------+
| Header (12 bytes)|  Magic, first_file pointer, version
+------------------+
| File Entry 1     |  filename[32], size, next_file, checksum
| Data for File 1  |
+------------------+
| File Entry 2     |
| Data for File 2  |
+------------------+
| ...              |
+------------------+
```

## API Reference

### Main Functions

#### `int hw_save(const char *filename, uint8_t *data, uint16_t len)`
Save a file to the filesystem. If the file exists, it will be overwritten.

**Parameters:**
- `filename`: Null-terminated string, max 31 characters
- `data`: Pointer to data buffer
- `len`: Length of data to save

**Returns:**
- `FS_OK` (0) on success
- `FS_ERR_INVALID` if filename is invalid
- `FS_ERR_NO_SPACE` if not enough space
- `FS_ERR_CORRUPT` if filesystem is corrupted

**Example:**
```c
uint8_t program[] = "10 PRINT \"HELLO\"\n20 GOTO 10";
int ret = hw_save("HELLO.BAS", program, strlen(program));
if (ret != FS_OK) {
    printf("Save failed: %d\n", ret);
}
```

#### `int hw_load(const char *filename, uint8_t *data, uint16_t *len, uint16_t max_len)`
Load a file from the filesystem.

**Parameters:**
- `filename`: Null-terminated string
- `data`: Buffer to receive file data
- `len`: Pointer to receive actual length loaded
- `max_len`: Maximum buffer size

**Returns:**
- `FS_OK` (0) on success
- `FS_ERR_NOT_FOUND` if file doesn't exist
- `FS_ERR_TOO_LARGE` if file is larger than max_len
- `FS_ERR_INVALID` if parameters are invalid

**Example:**
```c
uint8_t buffer[1024];
uint16_t len;
int ret = hw_load("HELLO.BAS", buffer, &len, sizeof(buffer));
if (ret == FS_OK) {
    buffer[len] = '\0';  // Null-terminate if text
    printf("Loaded: %s\n", buffer);
}
```

#### `void hw_list(void)`
Print a list of all files to stdout.

**Example Output:**
```
Files:
  HELLO.BAS                            27 bytes
  TEST.BAS                            100 bytes
  DATA.BIN                           1024 bytes
Total: 3 file(s)
```

### Additional Functions

#### `void fs_init(void)`
Initialize the filesystem. Call once at startup. If the filesystem is not formatted, it will be initialized automatically.

#### `void fs_format(void)`
Format the filesystem, erasing all files. Use with caution!

#### `int hw_delete(const char *filename)`
Delete a file from the filesystem.

**Returns:**
- `FS_OK` on success
- `FS_ERR_NOT_FOUND` if file doesn't exist

#### `int fs_check(void)`
Check filesystem integrity and attempt to repair corruption.

**Returns:**
- `FS_OK` if filesystem is healthy
- `FS_ERR_CORRUPT` if errors were found (some may be repaired)

## Configuration

You can configure the filesystem by defining these macros before including fs.c:

```c
#define FS_START_ADDR 0        // Starting address in F-RAM
#define FS_SIZE (8*1024*1024)  // Total size available
```

## Memory Usage

- **Per file overhead**: 48 bytes (32 byte filename + 16 bytes metadata)
- **Minimum file space**: Entry size + data size
- **Total overhead**: 12 bytes for header + 48 bytes per file

Example: 100 files with 100 bytes each = 12 + (100 × 148) = 14,812 bytes (~14.5KB)

## Corruption Recovery

The filesystem includes basic corruption recovery:

1. **Checksums**: Each file entry has a checksum to detect corruption
2. **Validation**: All entries are validated when accessed
3. **Repair**: `fs_check()` can truncate the file list at corruption points
4. **Graceful degradation**: Partial file lists can still be accessed

### Recovery Procedure

```c
if (hw_load("myfile.bas", buffer, &len, sizeof(buffer)) == FS_ERR_CORRUPT) {
    printf("Filesystem corrupted, attempting repair...\n");
    fs_check();  // Attempt repair
    // Try again or list remaining files
}
```

## Limitations

1. **No fragmentation handling**: Deleted files don't free space for reuse. Use `fs_format()` to reclaim space.
2. **No directories**: Flat filesystem only
3. **No concurrent access**: Single-threaded use only
4. **No wear leveling**: F-RAM doesn't need it, but if using other media, add your own
5. **File size in save**: Limited to uint16_t (65,535 bytes) in the hw_save API, though the filesystem supports uint32_t internally

## Performance Considerations

- **Sequential access**: File operations scan the linked list from the beginning
- **Write time**: Proportional to file size (one FRAM write per byte)
- **Read time**: Proportional to file size (one FRAM read per byte)
- **List time**: Linear in number of files

For better performance with many files, recently saved files are faster to access (they're at the front of the list).

## Example Integration

```c
#include "fs.h"

int main(void) {
    // Initialize filesystem
    fs_init();
    
    // Save a BASIC program
    const char *program = "10 PRINT \"HELLO\"\n20 GOTO 10\n";
    if (hw_save("HELLO.BAS", (uint8_t*)program, strlen(program)) == FS_OK) {
        printf("Program saved\n");
    }
    
    // List files
    hw_list();
    
    // Load it back
    uint8_t buffer[256];
    uint16_t len;
    if (hw_load("HELLO.BAS", buffer, &len, sizeof(buffer)) == FS_OK) {
        buffer[len] = '\0';
        printf("Loaded: %s\n", buffer);
    }
    
    return 0;
}
```

## Testing

Compile and run the test program:

```bash
gcc -o fs_test fs_test.c fs.c
./fs_test
```

## License

This code is provided as-is for use in your embedded BASIC implementation.
