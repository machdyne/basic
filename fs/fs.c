/*
 * Machdyne BASIC filesystem
 *
 * See fs.h for the interface and docs/fs.md for the design.
 *
 * The one rule: data is only ever written into erased (0xFF) space, and
 * state changes only clear bits. Nothing is rewritten in place except to
 * erase a block or directory entry that is no longer in use. This makes
 * every operation safe against power loss and avoids wear hot spots.
 */

#include <string.h>
#include "fs.h"

/* ---- on-media layout ------------------------------------------------ */

#define HDR_SIZE      16
#define ENT_SIZE      20
#define FS_VERSION    1
#define NO_BLOCK      0xFFFF

/* block header: next block (2 bytes), then a commit byte written after it.
 * A link only counts once its commit byte is 0x00, so a link interrupted
 * half way through is ignored. */
#define B_NEXT        0
#define B_COMMIT      2
#define B_HDR         3
#define COMMITTED     0x00

/* header */
#define H_MAGIC       0    /* "MBF1" */
#define H_VERSION     4
#define H_SHIFT       5
#define H_NBLOCKS     6    /* 2 bytes */
#define H_NENT        8    /* 2 bytes */
#define H_DATA        10   /* 4 bytes: offset of block 0 */
#define H_NOR         14   /* 0x00: NOR layout (two directory areas) */

/* directory entry */
#define E_STATE       0
#define E_SEQ         1    /* 4 bytes */
#define E_NAME        5    /* 12 bytes, zero padded */
#define E_FIRST       17   /* 2 bytes */
#define NAME_LEN      12

#define ST_FREE       0xFF
#define ST_VALID      0x7F
#define ST_DELETED    0x00

static const uint8_t magic[4] = { 'M', 'B', 'F', '1' };

/* ---- state in RAM ---------------------------------------------------- */

static struct {
    uint8_t mounted;
    uint8_t degraded;
    uint8_t shift;
    uint16_t nblocks;
    uint16_t nent;
    uint16_t cursor;        /* next block to try when allocating */
    uint16_t ecursor;       /* next directory entry to try */
    uint32_t data;          /* offset of block 0 */
    uint32_t seq;           /* highest sequence number in use */
    uint8_t map[(FS_MAX_BLOCKS + 7) / 8];   /* 1 = block in use */
#ifdef FS_NOR
    uint32_t dir;           /* the active directory area */
    uint32_t gen;           /* its generation */
    uint16_t mask;          /* next allocation: a block number whose bits
                             * are all set in mask (a half-written link) */
#endif
} fs;

static struct {
    uint8_t mode;           /* 0 = closed */
    uint8_t eof;
    uint16_t first;
    uint16_t blk;
    uint16_t off;           /* offset within the block's payload */
    uint16_t steps;         /* blocks visited, guards against loops */
    uint8_t name[NAME_LEN];
} fh;

/* ---- helpers ---------------------------------------------------------- */

#define PAYLOAD()     ((uint16_t)((1u << fs.shift) - B_HDR))

static uint32_t baddr(uint16_t b) {
    return fs.data + ((uint32_t)b << fs.shift);
}

#ifdef FS_NOR
/* Two directory areas of one sector each, after the header's sector. An
 * area begins with its generation (4 bytes) and the magic "MBDA", written
 * last; the entries follow, one entry size from the start. */
#define AREA(a)       ((uint32_t)(1 + (a)) << FS_NOR_SHIFT)
#define NOR_NENT      (FS_NOR_SECTOR / ENT_SIZE - 1)
static const uint8_t area_magic[4] = { 'M', 'B', 'D', 'A' };

static uint32_t eaddr(uint16_t e) {
    return fs.dir + (uint32_t)(e + 1) * ENT_SIZE;
}
#else
static uint32_t eaddr(uint16_t e) {
    return HDR_SIZE + (uint32_t)e * ENT_SIZE;
}
#endif

static int rd(uint32_t a, uint8_t *buf, uint16_t len) {
    return fs_media_read(a, buf, len) ? FS_ERR_IO : FS_OK;
}

static int wr(uint32_t a, const uint8_t *buf, uint16_t len) {
    return fs_media_prog(a, buf, len) ? FS_ERR_IO : FS_OK;
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

/* next block after b; NO_BLOCK if there is no committed link */
static uint16_t next_of(uint16_t b) {
    uint8_t p[B_HDR];
    if (rd(baddr(b), p, B_HDR) || p[B_COMMIT] != COMMITTED) return NO_BLOCK;
    return get16(p + B_NEXT);
}

/* Link block b to nb. The pointer is written first and committed by a
 * single byte. After an interrupted link the pointer bytes may already
 * be written; they are written again (byte-writable media only). */
static int link(uint16_t b, uint16_t nb) {
    uint8_t p[2];
    put16(p, nb);
    if (wr(baddr(b) + B_NEXT, p, 2)) return FS_ERR_IO;
    p[0] = COMMITTED;
    return wr(baddr(b) + B_COMMIT, p, 1);
}

static int used(uint16_t b) { return fs.map[b >> 3] & (1 << (b & 7)); }
static void mark(uint16_t b) { fs.map[b >> 3] |= (1 << (b & 7)); }
static void unmark(uint16_t b) { fs.map[b >> 3] &= ~(1 << (b & 7)); }

#ifndef FS_NOR
/* Return bytes to 0xFF, writing only those that are not 0xFF already. */
static int erase(uint32_t a, uint32_t len) {
    uint8_t buf[16];
    while (len) {
        uint16_t n = len > sizeof(buf) ? sizeof(buf) : (uint16_t)len;
        if (rd(a, buf, n)) return FS_ERR_IO;
        for (uint16_t i = 0; i < n; i++) {
            if (buf[i] != 0xFF) {
                memset(buf, 0xFF, n);
                if (wr(a, buf, n)) return FS_ERR_IO;
                break;
            }
        }
        a += n;
        len -= n;
    }
    return FS_OK;
}
#endif

/* Clear the in-use bits of a chain (the blocks are not touched). */
static void release(uint16_t b) {
    uint16_t steps = 0;
    while (b < fs.nblocks && used(b) && steps++ < fs.nblocks) {
        unmark(b);
        b = next_of(b);
    }
}

/* Take a free block, in rotation. Only its header and the first payload
 * byte (the end marker) are erased; old contents after that are never
 * read, because data is always followed by an end marker (see put()).
 * This halves the wear compared with erasing the whole block. */
static int alloc(uint16_t *out) {
    for (uint16_t i = 0; i < fs.nblocks; i++) {
        uint16_t b = fs.cursor;
        if (++fs.cursor >= fs.nblocks) fs.cursor = 0;
        if (!used(b)) {
#ifdef FS_NOR
            if ((b & fs.mask) != b) continue;
            fs.mask = 0xFFFF;
            if (fs_media_erase(baddr(b))) return FS_ERR_IO;
#else
            if (erase(baddr(b), B_HDR + 1)) return FS_ERR_IO;
#endif
            mark(b);
            *out = b;
            return FS_OK;
        }
    }
    return FS_ERR_FULL;
}

/* ---- names ------------------------------------------------------------ */

/* Names are stored as given ("NAME.EXT"): the caller normalizes them.
 * Here they only need to be 1-12 printable characters. */
static int pack(const char *in, uint8_t *out) {
    uint8_t n = 0;
    memset(out, 0, NAME_LEN);
    while (in[n]) {
        if (n == NAME_LEN || (uint8_t)in[n] <= ' ' || (uint8_t)in[n] >= 0x7F)
            return FS_ERR_INVALID;
        out[n] = in[n];
        n++;
    }
    return n ? FS_OK : FS_ERR_INVALID;
}

/* ---- directory -------------------------------------------------------- */

/* Valid entry with this name, or -1. Reads the entry into ent. */
static int find(const uint8_t *name, uint8_t *ent) {
    for (uint16_t e = 0; e < fs.nent; e++) {
        if (rd(eaddr(e), ent, ENT_SIZE)) return -1;
        if (ent[E_STATE] == ST_VALID &&
            !memcmp(ent + E_NAME, name, NAME_LEN)) return e;
    }
    return -1;
}

static int set_state(uint16_t e, uint8_t state) {
    return wr(eaddr(e) + E_STATE, &state, 1);
}

/* Delete entry e (read into ent) and free its blocks. */
static int remove_entry(uint16_t e, const uint8_t *ent) {
    int r = set_state(e, ST_DELETED);
    release(get16(ent + E_FIRST));
    return r;
}

/* Write a new valid entry. The fields are written while the state byte is
 * still 0xFF; the single write of the state byte commits it.
 *
 * A new name may not take the last unused entry: one is always kept so
 * that any existing file can still be replaced. */
#ifdef FS_NOR
/* an entry that has never been written (all 0xFF) */
static int clean(uint16_t e) {
    uint8_t ent[ENT_SIZE];
    if (rd(eaddr(e), ent, ENT_SIZE)) return 0;
    for (uint8_t i = 0; i < ENT_SIZE; i++) if (ent[i] != 0xFF) return 0;
    return 1;
}
#define REUSABLE(e, st) clean(e)
#else
#define REUSABLE(e, st) ((st) != ST_VALID)
#endif

static int new_entry(const uint8_t *name, uint16_t first, uint8_t is_new) {
    uint8_t ent[ENT_SIZE];
    if (is_new) {
        uint16_t spare = 0;
        for (uint16_t e = 0; e < fs.nent && spare < 2; e++) {
            uint8_t st;
            if (rd(eaddr(e), &st, 1)) return FS_ERR_IO;
            if (REUSABLE(e, st)) spare++;
        }
        if (spare < 2) return FS_ERR_DIR_FULL;
    }
    for (uint16_t i = 0; i < fs.nent; i++) {
        uint16_t e = fs.ecursor;
        if (++fs.ecursor >= fs.nent) fs.ecursor = 0;
        uint8_t st;
        if (rd(eaddr(e), &st, 1)) return FS_ERR_IO;
        if (!REUSABLE(e, st)) continue;
        /* The fields of an unused entry are written directly; it stays
         * unused until its state byte says otherwise. */
        memset(ent, 0xFF, sizeof(ent));
        put32(ent + E_SEQ, ++fs.seq);
        memcpy(ent + E_NAME, name, NAME_LEN);
        put16(ent + E_FIRST, first);
        if (wr(eaddr(e) + 1, ent + 1, ENT_SIZE - 1)) return FS_ERR_IO;
        return set_state(e, ST_VALID);
    }
    return FS_ERR_DIR_FULL;
}

#ifdef FS_NOR
/* Copy the valid entries to the other directory area, commit it with the
 * next generation, then erase the old one. A power cut leaves one complete
 * area: the old one until the new one's magic is written. */
static int compact(void) {
    uint32_t other = fs.dir == AREA(0) ? AREA(1) : AREA(0);
    uint8_t ent[ENT_SIZE], g[4];
    uint16_t n = 0;
    if (fs_media_erase(other)) return FS_ERR_IO;
    for (uint16_t e = 0; e < fs.nent; e++) {
        if (rd(eaddr(e), ent, ENT_SIZE)) return FS_ERR_IO;
        if (ent[E_STATE] != ST_VALID) continue;
        if (wr(other + (uint32_t)(n + 1) * ENT_SIZE, ent, ENT_SIZE))
            return FS_ERR_IO;
        n++;
    }
    put32(g, fs.gen + 1);
    if (wr(other, g, 4) || wr(other + 4, area_magic, 4)) return FS_ERR_IO;
    if (fs_media_erase(fs.dir)) return FS_ERR_IO;
    fs.dir = other;
    fs.gen++;
    fs.ecursor = n < fs.nent ? n : 0;
    return FS_OK;
}

/* before a file is written: at least two clean entries */
static int room(void) {
    for (int pass = 0; pass < 2; pass++) {
        uint16_t spare = 0;
        for (uint16_t e = 0; e < fs.nent && spare < 2; e++)
            if (clean(e)) spare++;
        if (spare >= 2) return FS_OK;
        if (pass == 0 && compact()) return FS_ERR_IO;
    }
    return FS_ERR_DIR_FULL;
}
#endif

/* ---- format and mount -------------------------------------------------- */

/* offset of block 0 for a given layout */
static uint32_t data_offset(uint8_t shift, uint16_t nent) {
#ifdef FS_NOR
    (void)nent;
    return (uint32_t)3 << shift;    /* header, two directory areas */
#endif
    uint32_t bs = 1u << shift;
    uint32_t data = HDR_SIZE + (uint32_t)nent * ENT_SIZE;
    return (data + bs - 1) & ~(bs - 1);
}

int fs_format(uint32_t size, uint8_t shift, uint16_t nent) {
    uint8_t h[HDR_SIZE];
    uint32_t bs = 1u << shift;

    if (shift < 5 || shift > 12 || nent < 2 || nent > 1024)
        return FS_ERR_INVALID;
#ifdef FS_NOR
    if (shift != FS_NOR_SHIFT || nent > NOR_NENT) return FS_ERR_INVALID;
#endif

    uint32_t data = data_offset(shift, nent);
    if (size < data + 2 * bs) return FS_ERR_INVALID;
    uint32_t nblocks = (size - data) >> shift;
    if (nblocks > FS_MAX_BLOCKS) return FS_ERR_TOO_BIG;

    fh.mode = 0;
    fs.mounted = 0;

#ifdef FS_NOR
    /* unformat first, then a clean first directory area (generation 1) */
    uint8_t g[4];
    if (fs_media_erase(0) || fs_media_erase(AREA(0)) || fs_media_erase(AREA(1)))
        return FS_ERR_IO;
    put32(g, 1);
    if (wr(AREA(0), g, 4) || wr(AREA(0) + 4, area_magic, 4)) return FS_ERR_IO;
#else
    /* unformat first, so an interrupted format is never mistaken for one */
    if (erase(0, HDR_SIZE)) return FS_ERR_IO;
    if (erase(HDR_SIZE, (uint32_t)nent * ENT_SIZE)) return FS_ERR_IO;
#endif

    memset(h, 0xFF, sizeof(h));
#ifdef FS_NOR
    h[H_NOR] = 0x00;
#endif
    h[H_VERSION] = FS_VERSION;
    h[H_SHIFT] = shift;
    put16(h + H_NBLOCKS, (uint16_t)nblocks);
    put16(h + H_NENT, nent);
    put32(h + H_DATA, data);
    if (wr(4, h + 4, HDR_SIZE - 4)) return FS_ERR_IO;
    if (wr(0, magic, 4)) return FS_ERR_IO;

    return fs_mount(size);
}

int fs_format_default(uint32_t size) {
#ifdef FS_NOR
    return fs_format(size, FS_NOR_SHIFT, NOR_NENT);
#endif
    uint8_t shift;
    uint16_t nent;
    if (size <= 2048) { shift = 5; nent = 8; }
    else if (size <= 16384) { shift = 6; nent = 16; }
    else if (size <= 262144) { shift = 8; nent = 64; }
    else { shift = 9; nent = 128; }
    /* larger blocks if this build cannot track that many */
    while (shift < 12 && (size >> shift) > FS_MAX_BLOCKS) shift++;
    return fs_format(size, shift, nent);
}

int fs_mount(uint32_t size) {
    uint8_t h[HDR_SIZE];
    uint8_t ent[ENT_SIZE];
    uint32_t best_seq = 0;
    int best = -1;

    fs.mounted = 0;
    fh.mode = 0;

    if (rd(0, h, HDR_SIZE)) return FS_ERR_IO;
    if (memcmp(h, magic, 4) || h[H_VERSION] != FS_VERSION)
        return FS_ERR_UNFORMATTED;

    fs.shift = h[H_SHIFT];
    fs.nblocks = get16(h + H_NBLOCKS);
    fs.nent = get16(h + H_NENT);
    if (fs.shift < 5 || fs.shift > 12 || fs.nent < 2 || fs.nent > 1024)
        return FS_ERR_UNFORMATTED;
    fs.data = data_offset(fs.shift, fs.nent);   /* (also in the header) */
    if (fs.nblocks == 0 || fs.data + ((uint32_t)fs.nblocks << fs.shift) > size)
        return FS_ERR_UNFORMATTED;
    if (fs.nblocks > FS_MAX_BLOCKS) return FS_ERR_TOO_BIG;

#ifdef FS_NOR
    /* the valid directory area with the higher generation */
    if (h[H_NOR] != 0x00 || fs.shift != FS_NOR_SHIFT || fs.nent > NOR_NENT)
        return FS_ERR_UNFORMATTED;
    fs.dir = 0;
    fs.mask = 0xFFFF;
    for (uint8_t a = 0; a < 2; a++) {
        uint8_t ah[8];
        if (rd(AREA(a), ah, 8)) return FS_ERR_IO;
        if (memcmp(ah + 4, area_magic, 4)) continue;
        if (!fs.dir || get32(ah) > fs.gen) {
            fs.dir = AREA(a);
            fs.gen = get32(ah);
        }
    }
    if (!fs.dir) return FS_ERR_UNFORMATTED;
#endif
    memset(fs.map, 0, sizeof(fs.map));
    fs.degraded = 0;
    fs.seq = 0;
    fs.cursor = 0;
    fs.ecursor = 0;

    /* Pass 1: highest sequence number; resolve duplicate names left by an
     * interrupted save (the newer one wins). */
    for (uint16_t e = 0; e < fs.nent; e++) {
        if (rd(eaddr(e), ent, ENT_SIZE)) return FS_ERR_IO;
        uint8_t st = ent[E_STATE];
        if (st != ST_VALID) {
#ifdef FS_NOR
            /* a delete torn by a power cut: on its way to ST_DELETED */
            if (st != ST_FREE && (st & 0x80)) fs.degraded = 1;
#else
            if (st != ST_FREE && st != ST_DELETED) fs.degraded = 1;
#endif
            continue;
        }
        uint32_t seq = get32(ent + E_SEQ);
        if (seq > fs.seq) fs.seq = seq;
        uint8_t other[ENT_SIZE];
        for (uint16_t f = e + 1; f < fs.nent; f++) {
            if (rd(eaddr(f), other, ENT_SIZE)) return FS_ERR_IO;
            if (other[E_STATE] != ST_VALID ||
                memcmp(other + E_NAME, ent + E_NAME, NAME_LEN)) continue;
            if (get32(other + E_SEQ) > seq) {
                if (set_state(e, ST_DELETED)) return FS_ERR_IO;
                break;
            } else {
                if (set_state(f, ST_DELETED)) return FS_ERR_IO;
            }
        }
    }

    /* Pass 2: mark the blocks of every file in use. */
    for (uint16_t e = 0; e < fs.nent; e++) {
        if (rd(eaddr(e), ent, ENT_SIZE)) return FS_ERR_IO;
        if (ent[E_STATE] != ST_VALID) continue;
        uint16_t b = get16(ent + E_FIRST);
        uint16_t last = NO_BLOCK;
        uint16_t steps = 0;
        while (b != NO_BLOCK) {
            if (b >= fs.nblocks || used(b) || steps++ >= fs.nblocks) {
                fs.degraded = 1;   /* bad pointer, cross-link or loop */
                break;
            }
            mark(b);
            last = b;
            b = next_of(b);
        }
        uint32_t seq = get32(ent + E_SEQ);
        if (best < 0 || seq > best_seq) {
            best = e;
            best_seq = seq;
            if (last != NO_BLOCK) fs.cursor = last + 1;
        }
    }

    /* continue the rotation after the most recently written file */
    if (fs.cursor >= fs.nblocks) fs.cursor = 0;
    fs.ecursor = (best < 0) ? 0 : (uint16_t)(best + 1);
    if (fs.ecursor >= fs.nent) fs.ecursor = 0;

    fs.mounted = 1;
    return FS_OK;
}

/* ---- files ------------------------------------------------------------- */

/* Offset of the first 0xFF in block b's payload (PAYLOAD() if full). */
static int end_in(uint16_t b, uint16_t *out) {
    uint8_t buf[16];
    uint16_t pl = PAYLOAD();
    for (uint16_t off = 0; off < pl; off += sizeof(buf)) {
        uint16_t n = (uint16_t)(pl - off) > sizeof(buf) ?
            (uint16_t)sizeof(buf) : (uint16_t)(pl - off);
        if (rd(baddr(b) + B_HDR + off, buf, n)) return FS_ERR_IO;
        for (uint16_t i = 0; i < n; i++) {
            if (buf[i] == 0xFF) {
                *out = off + i;
                return FS_OK;
            }
        }
    }
    *out = pl;
    return FS_OK;
}

static int put(const uint8_t *buf, uint16_t len) {
    uint16_t pl = PAYLOAD();
    while (len) {
        if (fh.off == pl) {
            /* Block full: take a new one and link it straight away. An
             * empty block at the end of a file is harmless. */
            uint16_t nb;
            int r = alloc(&nb);
            if (r) return r;
            if (link(fh.blk, nb)) return FS_ERR_IO;
            fh.blk = nb;
            fh.off = 0;
        }
        uint16_t n = pl - fh.off;
        if (n > len) n = len;
        /* Write everything but the first byte, then a new end marker
         * after it, then the first byte over the old end marker. Until
         * that last write, readers still see the file end where it was. */
        uint32_t a = baddr(fh.blk) + B_HDR + fh.off;
        uint8_t ff = 0xFF;
        if (n > 1 && wr(a + 1, buf + 1, n - 1)) return FS_ERR_IO;
        if (fh.off + n < pl && wr(a + n, &ff, 1)) return FS_ERR_IO;
        if (wr(a, buf, 1)) return FS_ERR_IO;
        fh.off += n;
        buf += n;
        len -= n;
    }
    return FS_OK;
}

int fs_open(const char *name, uint8_t mode) {
    uint8_t ent[ENT_SIZE];
    int e, r;

    if (!fs.mounted) return FS_ERR_UNFORMATTED;
    if (fh.mode) return FS_ERR_BUSY;
    if (pack(name, fh.name)) return FS_ERR_INVALID;

    fh.eof = 0;
    fh.off = 0;
    fh.steps = 0;

    if (mode == FS_READ) {
        e = find(fh.name, ent);
        if (e < 0) return FS_ERR_NOT_FOUND;
        fh.first = fh.blk = get16(ent + E_FIRST);
        if (fh.blk >= fs.nblocks) return FS_ERR_CORRUPT;
        fh.mode = FS_READ;
        return FS_OK;
    }

#ifdef FS_NOR
    if (mode != FS_READ && (r = room())) return r;
#endif

    if (mode == FS_WRITE) {
        r = alloc(&fh.first);
        if (r) return r;
        fh.blk = fh.first;
        fh.mode = FS_WRITE;
        return FS_OK;
    }

    if (mode == FS_APPEND) {
        e = find(fh.name, ent);
        if (e < 0) {
            /* create an empty file now; appends are kept as they happen */
            r = alloc(&fh.first);
            if (r) return r;
            r = new_entry(fh.name, fh.first, 1);
            if (r) {
                release(fh.first);
                return r;
            }
            fh.blk = fh.first;
            fh.mode = FS_APPEND;
            return FS_OK;
        }

        /* find the end: last block, then the first 0xFF in it */
        uint16_t b = get16(ent + E_FIRST), n;
        uint16_t steps = 0;
        if (b >= fs.nblocks) return FS_ERR_CORRUPT;
        while ((n = next_of(b)) != NO_BLOCK) {
            if (n >= fs.nblocks || ++steps >= fs.nblocks)
                return FS_ERR_CORRUPT;
            b = n;
        }
        fh.first = get16(ent + E_FIRST);
        fh.blk = b;
        if (end_in(b, &fh.off)) return FS_ERR_IO;
#ifdef FS_NOR
        {
            /* A power cut may have left bytes after the end (continue in
             * a new block) or half a link (the next block must be one
             * whose number can be programmed over it). */
            uint8_t t[16], hd[2];
            uint16_t pl = PAYLOAD();
            for (uint16_t o = fh.off; o < pl; o += sizeof(t)) {
                uint16_t k = pl - o < (int)sizeof(t) ? pl - o : (int)sizeof(t);
                if (rd(baddr(b) + B_HDR + o, t, k)) return FS_ERR_IO;
                for (uint16_t i = 0; i < k; i++) if (t[i] != 0xFF) fh.off = pl;
            }
            if (rd(baddr(b) + B_NEXT, hd, 2)) return FS_ERR_IO;
            fs.mask = get16(hd);
        }
#endif
        fh.mode = FS_APPEND;

        return FS_OK;
    }

    return FS_ERR_INVALID;
}

int fs_write(const uint8_t *buf, uint16_t len) {
    if (fh.mode != FS_WRITE && fh.mode != FS_APPEND) return FS_ERR_NOT_OPEN;
    for (uint16_t i = 0; i < len; i++)
        if (buf[i] == 0xFF) return FS_ERR_INVALID;
    return put(buf, len);
}

int fs_read(uint8_t *buf, uint16_t len) {
    uint16_t pl = PAYLOAD();
    int total = 0;

    if (fh.mode != FS_READ) return FS_ERR_NOT_OPEN;

    while (len && !fh.eof) {
        if (fh.off == pl) {
            uint16_t nb = next_of(fh.blk);
            if (nb == NO_BLOCK) {
                fh.eof = 1;
                break;
            }
            if (nb >= fs.nblocks || ++fh.steps >= fs.nblocks) {
                fs.degraded = 1;
                return FS_ERR_CORRUPT;
            }
            fh.blk = nb;
            fh.off = 0;
        }
        uint16_t n = pl - fh.off;
        if (n > len) n = len;
        if (rd(baddr(fh.blk) + B_HDR + fh.off, buf, n)) return FS_ERR_IO;
        uint8_t skip = 0;
        for (uint16_t i = 0; i < n; i++) {
            if (buf[i] == 0xFF) {   /* end of file */
                n = i;
#ifdef FS_NOR
                /* (NOR: the end of this block's data, if another follows) */
                if (next_of(fh.blk) != NO_BLOCK) {
                    skip = 1;
                    break;
                }
#endif
                fh.eof = 1;
                break;
            }
        }
        fh.off += n;
        if (skip) fh.off = pl;
        buf += n;
        len -= n;
        total += n;
    }
    return total;
}

int fs_close(void) {
    uint8_t ent[ENT_SIZE];
    int r = FS_OK;

    if (fh.mode == FS_WRITE) {
        /* commit: the new entry becomes valid, then the old one goes */
        int old = find(fh.name, ent);
        r = new_entry(fh.name, fh.first, old < 0);
        if (r) {
            release(fh.first);
        } else if (old >= 0) {
            r = remove_entry(old, ent);
        }
    } else if (!fh.mode) {
        return FS_ERR_NOT_OPEN;
    }
    fh.mode = 0;
    return r;
}

void fs_abort(void) {
    if (fh.mode == FS_WRITE) release(fh.first);
    fh.mode = 0;
}

int fs_delete(const char *name) {
    uint8_t n[NAME_LEN], ent[ENT_SIZE];
    if (!fs.mounted) return FS_ERR_UNFORMATTED;
    if (fh.mode) return FS_ERR_BUSY;
    if (pack(name, n)) return FS_ERR_INVALID;
    int e = find(n, ent);
    if (e < 0) return FS_ERR_NOT_FOUND;
    return remove_entry(e, ent);
}

int fs_dir(fs_dir_cb cb, void *ctx) {
    uint8_t ent[ENT_SIZE];
    char name[13];
    if (!fs.mounted) return FS_ERR_UNFORMATTED;
    for (uint16_t e = 0; e < fs.nent; e++) {
        if (rd(eaddr(e), ent, ENT_SIZE)) return FS_ERR_IO;
        if (ent[E_STATE] != ST_VALID) continue;
        memcpy(name, ent + E_NAME, NAME_LEN);
        name[NAME_LEN] = 0;
        cb(name, ctx);
    }
    return FS_OK;
}

uint32_t fs_free(void) {
    uint32_t n = 0;
    if (!fs.mounted) return 0;
    for (uint16_t b = 0; b < fs.nblocks; b++) if (!used(b)) n++;
    return n * PAYLOAD();
}

uint8_t fs_degraded(void) {
    return fs.degraded;
}
