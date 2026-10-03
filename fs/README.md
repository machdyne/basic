# Filesystem

A small block filesystem for programs and data files on F-RAM and EEPROM,
from 1KB to 4MB. It survives power loss at any moment and spreads wear.

- Design and on-media format: [docs/fs.md](../docs/fs.md)
- Interface: [fs.h](fs.h)
- Tests: [test/fs_test.c](test/fs_test.c), run with `make test` in the
  repository root

## Using it on a target

Provide the two media functions and mount:

```c
int fs_media_read(uint32_t addr, uint8_t *buf, uint16_t len);
int fs_media_prog(uint32_t addr, const uint8_t *buf, uint16_t len);

fs_mount(size);              // FS_ERR_UNFORMATTED on a new medium
fs_format_default(size);     // erase and format
```

Addresses are offsets from the start of the filesystem area. Both
functions return 0 on success. `FS_MAX_BLOCKS` (default 128, one bit of RAM
per block) limits the number of blocks a build can mount; the default
formats use larger blocks on larger media.

```c
fs_open("LOG.DAT", FS_APPEND);
fs_write((const uint8_t *)"1,23\n", 5);
fs_close();
```

One file is open at a time. Files contain text: the byte 0xFF cannot be
written into a file.
