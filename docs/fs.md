# Filesystem Design

**Status: implemented for F-RAM and EEPROM, and in a NOR flash mode (2026-10-02).** NOR
flash (Werkzeug) is designed for but not implemented; see section 8.

A small filesystem for programs and data files, from 1KB to 4MB, that
survives power loss at any moment and spreads wear on memories that wear
out. Code: `fs/fs.c`, `fs/fs.h`. Tests: `fs/test/fs_test.c`.

## 1. Requirements

- **Sizes:** 1KB to 4MB.
- **Power loss:** a power cut at any point leaves every file either as it
  was before the operation or as it is after it. An append loses at most
  the line being written. Space is never permanently lost.
- **Wear:** no location is rewritten on every append or save; writes are
  spread across the whole memory.
- **Small RAM:** about 40 bytes plus one bit per block.
- **One format** on every target.
- **Content:** files contain text (UTF-8). The byte 0xFF never occurs in
  UTF-8 and marks the end of a file.

**Assumption:** a write of a single byte either happens or does not. Any
longer write may be interrupted after any byte. Every state change in the
format is therefore a single byte.

## 2. Principles

- **Data goes into unused space.** A file only ever grows into bytes past
  its end marker or into newly allocated blocks.
- **Commit by one byte.** Directory entries and block links are written
  first and made valid by one final byte.
- **Replace, don't overwrite.** Saving a file writes a complete new copy
  and then switches to it.
- **Rotate.** Blocks and directory entries are taken in rotation through
  the whole memory, not lowest-first.

## 3. Layout

All multi-byte values are little-endian.

| Area | Offset | Size |
|---|---|---|
| Header | 0 | 16 bytes |
| Directory | 16 | 20 bytes per entry |
| Data blocks | `data` (rounded up to a block boundary) | to the end |

### 3.1 Header (16 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `MBF1` (written last when formatting) |
| 4 | 1 | version (1) |
| 5 | 1 | block_shift: block size = 2^block_shift (5 to 12) |
| 6 | 2 | number of data blocks |
| 8 | 2 | number of directory entries (2 to 1024) |
| 10 | 4 | offset of block 0 |
| 14 | 2 | reserved (0xFF) |

Mount checks that the offset of block 0 matches the other fields and that
the blocks fit the medium.

### 3.2 Directory entry (20 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | state: 0xFF unused, 0x7F valid, 0x00 deleted |
| 1 | 4 | sequence number |
| 5 | 12 | name, zero padded |
| 17 | 2 | first block |
| 19 | 1 | reserved (0xFF) |

Names are stored exactly as given: 1-12 printable characters, compared
exactly. The caller normalizes them; Machdyne BASIC passes 8.3 names in
upper case (`NAME.EXT`), so the filesystem needs no name handling of its
own.

The fields are written while the state is still unused or deleted; the
state byte, written last, makes the entry valid.

### 3.3 Data block

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | next block |
| 2 | 1 | commit: 0x00 once the next field is valid |
| 3 | block size - 3 | data |

A link counts only when its commit byte is 0x00, so a link interrupted
half way through is ignored. In the last block of a file, the data ends at
the first 0xFF.

### 3.4 Default formats

`fs_format_default()` chooses the block size and directory size from the
size of the medium. Measured with `fs_free()` after formatting:

| Medium | `FS_MAX_BLOCKS` | Block | Entries | Data blocks | Free for files |
|---|---|---|---|---|---|
| 1KB | 128 | 32 | 8 | 26 | 754 bytes |
| 2KB | 128 | 32 | 8 | 58 | 1,682 bytes |
| 8KB (LS10) | 128 | 64 | 16 | 122 | 7,442 bytes |
| 32KB | 128 | 256 | 64 | 122 | 30,866 bytes |
| 256KB | 128 | 2,048 | 64 | 127 | 259,715 bytes |
| 256KB | 65,534 | 256 | 64 | 1,018 | 257,554 bytes |
| 4MB | 65,534 | 512 | 128 | 8,186 | 4,166,674 bytes |

`FS_MAX_BLOCKS` is a build setting: the number of blocks a build can track,
at one bit of RAM each. The default, 128, costs 16 bytes. When a medium has
more blocks than that, larger blocks are used, up to 4KB; a medium that
still has too many (4MB with the default) cannot be formatted by that
build (`FS_ERR_TOO_BIG`). A 2MB Werkzeug area needs at least 512 (64
bytes of RAM) with 4KB blocks, or more for smaller blocks.

One directory entry is always kept free so that any file can be replaced,
so the number of files is one less than the number of entries.

## 4. Operations

### 4.1 Mount

1. Read and check the header.
2. If two valid entries have the same name (a save interrupted after the
   new entry became valid), the one with the higher sequence number wins;
   the other is marked deleted.
3. Walk each file's chain and mark its blocks in use. A pointer outside
   the medium, a block reached twice or a chain longer than the number of
   blocks sets the degraded flag.
4. Every block not in use is free. Blocks of interrupted writes are
   reclaimed this way.
5. Allocation continues after the last block of the most recently saved
   file.

### 4.2 Allocation

A free block is taken in rotation. Only its 3-byte header and the first
data byte (the end marker) are erased to 0xFF; the old contents after the
marker are never read. On EEPROM this halves the writes compared with
erasing the whole block.

### 4.3 Writing data

Each write into a block is ordered so that the file always ends at a valid
place:

1. all bytes except the first,
2. a new end marker (0xFF) after them, unless the block is full,
3. the first byte, over the old end marker.

Until step 3, readers see the file end where it was. When a block is full,
a new block is allocated and linked immediately (an empty last block is
harmless).

### 4.4 Save (`FS_WRITE`)

1. Write the data into newly allocated blocks.
2. On close, write a new directory entry with the next sequence number and
   make it valid.
3. Mark the previous entry with the same name deleted; its blocks become
   free.

A power cut before step 2 completes leaves the old file. Mount completes
step 3 if needed.

### 4.5 Append (`FS_APPEND`)

Data is written at the end of the file and kept as it is written. A file
that does not exist is created empty first. A power cut during an append
can leave the line being written incomplete; earlier data is never
affected. (An earlier version closed such a line with a newline before the
next append; that was removed to fit the CH32V003, so the next append
continues the incomplete line.)

### 4.6 Delete

Mark the entry deleted.

### 4.7 Directory entries

A new entry is written into the next unused or deleted entry in rotation,
directly over its old contents. Each use of an entry writes its fields
once and its state byte twice (valid, later deleted).

## 5. RAM

| Item | Bytes |
|---|---|
| Filesystem state | about 24 |
| Open file | about 20 |
| Block bitmap | `FS_MAX_BLOCKS` / 8 (16 by default) |

## 6. Drivers

| Function | Meaning |
|---|---|
| `fs_media_read(addr, buf, len)` | read bytes |
| `fs_media_prog(addr, buf, len)` | write bytes |

| Target | Driver |
|---|---|
| LS10 | SPI F-RAM, burst transfers (`targets/ls10/fram.c`); the first 8,064 bytes |
| Blaustahl | SPI F-RAM, byte transfers; below the 512-byte metadata area |
| host tests | simulated memory with write counting and power cuts |
| `basic_fs` | an image file standing in for an 8KB F-RAM |

### 6.1 Configuration bytes (LS10)

The last 16 bytes of the LS10 F-RAM are outside the filesystem and hold the
Sechs address (0xA5, address, complement), so the filesystem is 8,176
bytes.

## 7. Tests

`make test` builds the tests with address and undefined-behaviour
sanitizers and runs:

| Test | What it checks |
|---|---|
| Unit | names, errors, replace, append across many blocks, filling up and recovering all space, sizes from 1KB to 4MB |
| Power cut | a scripted sequence of saves, appends and deletes, with a power cut after the 1st byte write, then the 2nd, and so on: about 2,450 cut points on 2KB and 8KB. After each cut: every file is old or new, an append keeps a prefix of its line, the next append starts on a new line, all space can be recovered, and the degraded flag is clear |
| Random | 67,000 random operations from 1KB to 4MB against a model, with random power cuts |
| Wear | simulated use, reporting the most-written byte |
| Corruption | 2,000 media with random damage: mount, list, read and write must not crash or hang |

The tests found four problems during development, all fixed: a link torn
between its two bytes (now committed by a third byte), reads beyond the
medium after header damage (mount now checks against the medium size), a
full directory preventing replacement (one entry is now kept free), and
double writes from erasing whole blocks.

### 7.1 Wear results

For an EEPROM rated at 1,000,000 writes per byte:

| Use | Most-written byte | Lifetime |
|---|---|---|
| Rewriting a settings file once a minute, 2KB | a directory state byte | about 7 years |
| A log line every second plus settings every minute, 8KB | spread evenly | about 10 years |
| A log line every second, 2KB, restarting when full | spread evenly | about 3 years |

The last case rewrites the whole 2KB every few minutes; the limit is the
amount of data, not the filesystem. F-RAM (about 10^12 writes) does not
wear out in practice.

A value updated much more often than once a minute should not be kept in a
file on EEPROM; on F-RAM this does not matter.

## 8. NOR flash (`FS_NOR`)

NOR flash can only clear bits; setting them back to 1 means erasing a
whole sector (4KB). Built with `-DFS_NOR`, `fs.c` uses a layout that never
needs to set a bit outside an erase. Used on Werkzeug, in the upper half of its
flash (2MB, 509 blocks, on V3C).

**Layout.** Sector 0 holds the header (as in section 3.1, with byte 14 =
0x00 marking the NOR layout). Sectors 1 and 2 are two directory areas.
Data blocks are one sector each and follow. A directory area starts with
its generation (4 bytes, written first) and the magic `MBDA` (written
last, the commit); 203 entries follow.

**What is the same.** Entries, links, commit bytes and data are only ever
written into erased space, and every change of state only clears bits:
entry state 0xFF (free) -> 0x7F (valid) -> 0x00 (deleted), commit byte
0xFF -> 0x00.

**What differs:**

- **Allocation** erases the whole block (sector).
- **Entries are not reused.** A new entry needs a clean one (all 0xFF).
  When a file is opened for writing and fewer than two remain, the
  directory is **compacted**: the other area is erased, the valid entries
  are copied to it, it is committed with the next generation, and the old
  area is erased. Mount uses the valid area with the higher generation, so
  a power cut at any point leaves one complete directory.
- **A block's data ends at its first 0xFF**, and reading continues in the
  next block if the link is committed. After a power cut during an append,
  bytes after the end of the file may already be programmed; the next
  append then starts a new block, and the stray bytes are never read.
- **A half-written link** (pointer written, commit byte not) cannot be
  rewritten with another value: the next block is chosen among free blocks
  whose number can be programmed over the pointer (every 0 bit of the
  pointer is also 0 in the number).
- **A torn delete** (a state byte between 0x7F and 0x00) counts as deleted.
- **A torn byte** at the start of an append can show as one wrong character
  where that piece of the append begins; no other data is affected.

**Tests** (`make fs_nor_test`): the same tests on a simulated NOR flash
where programming only clears bits (any attempt to set one fails the test),
erases are whole aligned sectors, and a power cut tears the byte being
programmed (random bits) or the sector being erased (random bytes): basic
tests, a power cut at every write of the script in section 7 (1,166 cut
points), 20,000 random operations, and 20,000 with random power cuts. No
write sets a bit.

**Wear.** Each sector of a 2MB area is erased once per use; at the usual
100,000 erase cycles this is not a concern for programs and logs. The two
directory sectors are erased once per compaction, that is about once per
200 saves.

## 9. Open questions

- Faster F-RAM clock on LS10 once tested on hardware.
