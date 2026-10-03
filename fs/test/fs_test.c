/*
 * Host tests for the Machdyne BASIC filesystem.
 *
 *   unit        basic behaviour, names, errors, sizes from 1KB to 4MB
 *   powercut    a power cut after every single byte write of a script
 *   random      random operations (and random power cuts) against a model
 *   wear        simulated EEPROM use, reporting the most-written byte
 *   corrupt     random damage must never crash or hang
 *
 * Build and run with "make test" in the repository root.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include "../fs.h"

/* ---- simulated medium ------------------------------------------------- */

static uint8_t *mem;
static uint32_t *wcount;
static uint32_t msize;
static long nwrites;          /* byte writes since arming */
static long cut_at = -1;      /* power cut before this write; -1 = never */
static jmp_buf cut_jmp;

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
    printf("\n"); failures++; } } while (0)

static void media_new(uint32_t size, uint8_t fill) {
    free(mem);
    free(wcount);
    mem = malloc(size);
    wcount = calloc(size, sizeof(uint32_t));
    memset(mem, fill, size);
    msize = size;
    cut_at = -1;
}

int fs_media_read(uint32_t a, uint8_t *buf, uint16_t len) {
    if ((uint64_t)a + len > msize) {
        printf("FAIL: read out of range %u+%u\n", a, len);
        abort();
    }
    memcpy(buf, mem + a, len);
    return 0;
}

#ifndef FS_NOR
int fs_media_prog(uint32_t a, const uint8_t *buf, uint16_t len) {
    if ((uint64_t)a + len > msize) {
        printf("FAIL: write out of range %u+%u\n", a, len);
        abort();
    }
    for (uint16_t i = 0; i < len; i++) {
        if (cut_at >= 0 && nwrites == cut_at) longjmp(cut_jmp, 1);
        mem[a + i] = buf[i];
        wcount[a + i]++;
        nwrites++;
    }
    return 0;
}
#else
/* NOR flash: programming only clears bits; a power cut tears the byte
 * being programmed (some of its bits) or the sector being erased (some
 * of its bytes). */
static int nor_violations;

int fs_media_prog(uint32_t a, const uint8_t *buf, uint16_t len) {
    if ((uint64_t)a + len > msize) {
        printf("FAIL: write out of range %u+%u\n", a, len);
        abort();
    }
    for (uint16_t i = 0; i < len; i++) {
        if (cut_at >= 0 && nwrites == cut_at) {
            mem[a + i] &= buf[i] | (uint8_t)rand();
            longjmp(cut_jmp, 1);
        }
        if ((mem[a + i] & buf[i]) != buf[i]) {
            if (!nor_violations++)
                printf("FAIL: NOR write sets bits at %u (%02x over %02x)\n",
                    a + i, buf[i], mem[a + i]);
            failures++;
        }
        mem[a + i] &= buf[i];
        wcount[a + i]++;
        nwrites++;
    }
    return 0;
}

int fs_media_erase(uint32_t a) {
    if (a % FS_NOR_SECTOR || a + FS_NOR_SECTOR > msize) {
        printf("FAIL: bad erase at %u\n", a);
        abort();
    }
    if (cut_at >= 0 && nwrites == cut_at) {
        for (uint32_t i = 0; i < FS_NOR_SECTOR; i++)
            if (rand() & 1) mem[a + i] = 0xFF;
        longjmp(cut_jmp, 1);
    }
    memset(mem + a, 0xFF, FS_NOR_SECTOR);
    wcount[a]++;            /* erases counted at the sector's first byte */
    nwrites++;
    return 0;
}
#endif

/* ---- helpers ------------------------------------------------------------ */

static int save(const char *name, const char *data) {
    int r = fs_open(name, FS_WRITE);
    if (r) return r;
    r = fs_write((const uint8_t *)data, strlen(data));
    if (r) {
        fs_abort();
        return r;
    }
    return fs_close();
}

static int append(const char *name, const char *data) {
    int r = fs_open(name, FS_APPEND);
    if (r) return r;
    r = fs_write((const uint8_t *)data, strlen(data));
    int c = fs_close();
    return r ? r : c;
}

/* Read a whole file into a malloc'd string; NULL if not found. */
static char *load(const char *name, int *err) {
    int r = fs_open(name, FS_READ);
    if (err) *err = r;
    if (r) return NULL;
    size_t cap = 256, len = 0;
    char *s = malloc(cap);
    for (;;) {
        if (len + 64 + 1 > cap) s = realloc(s, cap *= 2);
        int n = fs_read((uint8_t *)s + len, 64);
        if (n < 0) {
            if (err) *err = n;
            fs_abort();
            free(s);
            return NULL;
        }
        if (n == 0) break;
        len += n;
    }
    s[len] = 0;
    fs_close();
    return s;
}

/* total payload capacity of the mounted filesystem when empty */
static char dir_names[1024][13];
static int dir_n;

static void collect(const char *name, void *ctx) {
    (void)ctx;
    if (dir_n < 1024) strcpy(dir_names[dir_n++], name);
}

static uint32_t capacity_after_delete_all(void) {
    dir_n = 0;
    fs_dir(collect, NULL);
    for (int i = 0; i < dir_n; i++) fs_delete(dir_names[i]);
    return fs_free();
}

static void rand_text(char *buf, int len) {
    for (int i = 0; i < len; i++) {
        int r = rand() % 40;
        buf[i] = r == 0 ? '\n' : (char)(' ' + rand() % 95);
    }
    buf[len] = 0;
}

/* ---- unit tests -------------------------------------------------------- */

static void test_names(void) {
    /* names are stored as given: 1-12 printable characters */
    CHECK(save("BOOT.BAS", "1\n") == FS_OK, "8.3 name");
    CHECK(save("12345678.123", "1\n") == FS_OK, "12 characters");
    CHECK(save("123456789.123", "1\n") == FS_ERR_INVALID, "13 characters");
    CHECK(save("", "1\n") == FS_ERR_INVALID, "empty");
    CHECK(save("A B", "1\n") == FS_ERR_INVALID, "space");
    CHECK(fs_open("boot.bas", FS_READ) == FS_ERR_NOT_FOUND, "exact match");
}

static void test_basic(uint32_t size) {
    char *s;
    int err;

    media_new(size, 0xFF);
    CHECK(fs_mount(msize) == FS_ERR_UNFORMATTED, "blank medium not unformatted");
    CHECK(fs_format_default(size) == FS_OK, "format %u", size);
    uint32_t cap = fs_free();
    CHECK(cap > size / 2, "capacity %u of %u", cap, size);

    CHECK(save("A.BAS", "10 PRINT 1\n") == FS_OK, "save");
    s = load("A.BAS", &err);
    CHECK(s && !strcmp(s, "10 PRINT 1\n"), "load: %s", s ? s : "(null)");
    free(s);

    /* replace */
    CHECK(save("A.BAS", "20 END\n") == FS_OK, "replace");
    s = load("A.BAS", NULL);
    CHECK(s && !strcmp(s, "20 END\n"), "replaced: %s", s ? s : "(null)");
    free(s);

    /* append, across many blocks */
    char line[40], expect[40000] = "";
    for (int i = 0; i < 300; i++) {
        snprintf(line, sizeof(line), "%d,%d\n", i, i * 7);
        CHECK(append("LOG.DAT", line) == FS_OK, "append %d", i);
        strcat(expect, line);
        if (fs_free() < 200) break;
    }
    s = load("LOG.DAT", NULL);
    CHECK(s && !strcmp(s, expect), "append content (%zu vs %zu)",
        s ? strlen(s) : 0, strlen(expect));
    free(s);

    /* survives remount */
    CHECK(fs_mount(msize) == FS_OK, "remount");
    s = load("LOG.DAT", NULL);
    CHECK(s && !strcmp(s, expect), "after remount");
    free(s);

    /* errors */
    CHECK(fs_open("NONE.BAS", FS_READ) == FS_ERR_NOT_FOUND, "not found");
    CHECK(fs_delete("NONE.BAS") == FS_ERR_NOT_FOUND, "delete not found");
    CHECK(fs_open("A.BAS", FS_READ) == FS_OK, "open");
    CHECK(fs_open("B.BAS", FS_READ) == FS_ERR_BUSY, "busy");
    CHECK(fs_write((const uint8_t *)"x", 1) == FS_ERR_NOT_OPEN, "write ro");
    fs_close();
    CHECK(fs_open("B.BAS", FS_WRITE) == FS_OK, "open w");
    CHECK(fs_write((const uint8_t *)"\xff", 1) == FS_ERR_INVALID, "0xFF");
    fs_abort();
    CHECK(fs_open("B.BAS", FS_READ) == FS_ERR_NOT_FOUND, "aborted file");

    /* fill up, then make sure everything comes back */
    CHECK(fs_delete("LOG.DAT") == FS_OK, "delete");
    char big[5000];
    rand_text(big, sizeof(big) - 1);
    int r = FS_OK, i = 0;
    while (r == FS_OK && i < 10000) {
        char name[13];
        snprintf(name, sizeof(name), "F%d", i++);
        r = save(name, big);
    }
    CHECK(r == FS_ERR_FULL || r == FS_ERR_DIR_FULL, "fill: %d", r);
    CHECK(capacity_after_delete_all() == cap, "space recovered");
    CHECK(fs_mount(msize) == FS_OK && fs_free() == cap, "space after remount");
    CHECK(!fs_degraded(), "degraded");
}

static void test_dir_full(void) {
    media_new(2048, 0xFF);
    fs_format(2048, 5, 4);   /* 4 entries: 3 files plus one spare */
    CHECK(save("A", "1\n") == FS_OK, "a");
    CHECK(save("B", "2\n") == FS_OK, "b");
    CHECK(save("C", "3\n") == FS_OK, "c");
    CHECK(save("D", "4\n") == FS_ERR_DIR_FULL, "d should not fit");
    CHECK(save("A", "5\n") == FS_OK, "replace with spare entry");
    char *s = load("A", NULL);
    CHECK(s && !strcmp(s, "5\n"), "a replaced");
    free(s);
}

static void test_sizes(void) {
    static const uint32_t sizes[] = {
        1024, 2048, 8192, 32768, 262144, 4u << 20
    };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        test_basic(sizes[i]);
    CHECK(fs_format(1024, 4, 8) == FS_ERR_INVALID, "shift 4");
    CHECK(fs_format(1024, 13, 8) == FS_ERR_INVALID, "shift 13");
    CHECK(fs_format(64, 5, 8) == FS_ERR_INVALID, "too small");
}

/* ---- power cut at every write ----------------------------------------- */

#define MAXF 8

typedef struct {
    char *data[MAXF];   /* NULL = no file */
} model_t;

static const char *fnames[MAXF] = {
    "P1.BAS", "P2.BAS", "LOG.DAT", "CFG.DAT", "A", "B", "C", "D"
};

static void model_copy(model_t *dst, const model_t *src) {
    for (int i = 0; i < MAXF; i++)
        dst->data[i] = src->data[i] ? strdup(src->data[i]) : NULL;
}

static void model_free(model_t *m) {
    for (int i = 0; i < MAXF; i++) {
        free(m->data[i]);
        m->data[i] = NULL;
    }
}

typedef struct {
    char op;            /* 's' save, 'a' append, 'd' delete */
    int f;
    const char *data;
} step_t;

static void model_apply(model_t *m, const step_t *s) {
    char **d = &m->data[s->f];
    if (s->op == 's') {
        free(*d);
        *d = strdup(s->data);
    } else if (s->op == 'd') {
        free(*d);
        *d = NULL;
    } else {
        size_t a = *d ? strlen(*d) : 0;
        char *n = malloc(a + strlen(s->data) + 1);
        if (*d) memcpy(n, *d, a);
        strcpy(n + a, s->data);
        free(*d);
        *d = n;
    }
}

static int run_step(const step_t *s) {
    if (s->op == 's') return save(fnames[s->f], s->data);
    if (s->op == 'a') return append(fnames[s->f], s->data);
    return fs_delete(fnames[s->f]);
}

static int is_prefix(const char *p, const char *s) {
    return !strncmp(p, s, strlen(p));
}

/* After a cut during step k, check every file against the model. */
static void verify_cut(const model_t *before, const model_t *after,
                       const step_t *s, long cut) {
    for (int i = 0; i < MAXF; i++) {
        char *got = load(fnames[i], NULL);
        const char *b = before->data[i], *a = after->data[i];
        int ok;
        if (i != s->f) {
            ok = (!got && !b) || (got && b && !strcmp(got, b));
        } else if (s->op == 'a') {
            /* old content plus any prefix of what the append adds */
            ok = (!got && !b) || (got && a && is_prefix(b ? b : "", got) &&
                is_prefix(got, a));
#ifdef FS_NOR
            /* NOR: a torn byte may show as one wrong character where a
             * piece of the append begins */
            if (!ok && got && a && is_prefix(b ? b : "", got) &&
                strlen(got) <= strlen(a)) {
                int diff = 0;
                for (size_t k = 0; got[k]; k++) diff += got[k] != a[k];
                ok = diff == 1;
            }
#endif
        } else {
            ok = (!got && !b) || (!got && !a) ||
                (got && b && !strcmp(got, b)) ||
                (got && a && !strcmp(got, a));
        }
        CHECK(ok, "cut at write %ld, step %c %s: file %s wrong (%s)",
            cut, s->op, fnames[s->f], fnames[i], got ? got : "(none)");
        free(got);
        if (!ok) return;
    }
}

static void test_powercut(uint32_t size, uint8_t shift, uint16_t nent) {
    static char big1[400], big2[300], mid[90];
    rand_text(big1, sizeof(big1) - 1);
    rand_text(big2, sizeof(big2) - 1);
    rand_text(mid, sizeof(mid) - 1);

    const step_t script[] = {
        { 's', 0, "10 PRINT \"HELLO\"\n20 GOTO 10\n" },
        { 'a', 2, "1,23\n" },
        { 's', 1, big1 },
        { 'a', 2, "2,24\n" },
        { 'a', 2, "a line that is long enough to cross a block boundary\n" },
        { 's', 0, big2 },
        { 's', 3, mid },
        { 'd', 1, NULL },
        { 'a', 2, "3,25\n" },
        { 's', 3, "short\n" },
        { 's', 1, mid },
        { 'd', 2, NULL },
        { 'a', 2, "4,26\n" },
    };
    const int nsteps = sizeof(script) / sizeof(script[0]);

    /* initial image and total number of writes */
    media_new(size, 0xFF);
    fs_format(size, shift, nent);
    uint8_t *image = malloc(size);
    memcpy(image, mem, size);
    uint32_t cap = fs_free();

    nwrites = 0;
    for (int i = 0; i < nsteps; i++)
        CHECK(run_step(&script[i]) == FS_OK, "script step %d", i);
    long total = nwrites;

    long cuts = 0;
    for (long cut = 0; cut < total; cut++) {
        memcpy(mem, image, size);
        memset(wcount, 0, size * sizeof(uint32_t));
        fs_mount(msize);

        model_t before = {{0}}, after = {{0}};
        volatile int k = 0;
        nwrites = 0;
        cut_at = cut;
        if (!setjmp(cut_jmp)) {
            for (k = 0; k < nsteps; k++) {
                model_free(&before);
                model_copy(&before, &after);
                run_step(&script[k]);
                model_apply(&after, &script[k]);
            }
            CHECK(0, "no cut happened at %ld", cut);
        }
        cut_at = -1;
        cuts++;

        /* the cut happened during step k: 'after' does not include it yet */
        model_t done = {{0}};
        model_copy(&done, &after);
        model_apply(&done, &script[k]);

        CHECK(fs_mount(msize) == FS_OK, "mount after cut %ld", cut);
        CHECK(!fs_degraded(), "degraded after cut %ld", cut);
        verify_cut(&after, &done, &script[k], cut);

        /* appending still works after the cut */
        char *was = load("LOG.DAT", NULL);
        CHECK(append("LOG.DAT", "Z\n") == FS_OK, "append after cut %ld", cut);
        char *now = load("LOG.DAT", NULL);
        if (now) {
            size_t wl = was ? strlen(was) : 0;
            int ok = (was ? is_prefix(was, now) : 1) && !strcmp(now + wl, "Z\n");
            CHECK(ok, "append after cut %ld: '%s' -> '%s'", cut,
                was ? was : "", now);
        }
        free(was);
        free(now);

        /* no space may be lost */
        CHECK(capacity_after_delete_all() == cap, "space lost after cut %ld",
            cut);

        model_free(&before);
        model_free(&after);
        model_free(&done);
        if (failures > 20) break;
    }
    free(image);
    printf("  powercut %uB/%dB blocks: %ld cut points checked\n",
        size, 1 << shift, cuts);
}

/* ---- random operations against a model --------------------------------- */

static void test_random(uint32_t size, int ops, int with_cuts) {
    model_t m = {{0}};
    char buf[3000];

    media_new(size, 0xFF);
    CHECK(fs_format_default(size) == FS_OK, "format %u", size);
    uint32_t cap = fs_free();
    int maxlen = size < 4096 ? 300 : 2900;

    for (int n = 0; n < ops && failures < 20; n++) {
        step_t s;
        int f = rand() % MAXF;
        int r = rand() % 10;
        s.f = f;
        if (r < 4) {
            s.op = 's';
            rand_text(buf, rand() % maxlen);
        } else if (r < 8) {
            s.op = 'a';
            rand_text(buf, rand() % 40);
            strcat(buf, "\n");
        } else {
            s.op = 'd';
        }
        s.data = buf;

        model_t before = {{0}};
        model_copy(&before, &m);

        int cut = with_cuts && rand() % 50 == 0;
        volatile int res = 0, cutdone = 0;
        nwrites = 0;
        cut_at = cut ? rand() % 300 : -1;
        if (!setjmp(cut_jmp)) {
            res = run_step(&s);
        } else {
            cutdone = 1;
        }
        cut_at = -1;

        if (cutdone) {
            model_t done = {{0}};
            model_copy(&done, &m);
            model_apply(&done, &s);
            CHECK(fs_mount(msize) == FS_OK, "mount after random cut");
            verify_cut(&before, &done, &s, -1);
            /* resynchronise the model with what is on the medium */
            model_free(&m);
            for (int i = 0; i < MAXF; i++) m.data[i] = load(fnames[i], NULL);
            model_free(&done);
        } else if (res == FS_OK) {
            model_apply(&m, &s);
        } else if (s.op == 'd') {
            CHECK(res == FS_ERR_NOT_FOUND && !m.data[f], "delete: %d", res);
        } else if (s.op == 'a' && res == FS_ERR_FULL) {
            /* part of an append may have been kept */
            free(m.data[f]);
            m.data[f] = load(fnames[f], NULL);
        } else {
            CHECK(res == FS_ERR_FULL || res == FS_ERR_DIR_FULL,
                "op %c: %d", s.op, res);
        }
        model_free(&before);

        if (rand() % 100 == 0) CHECK(fs_mount(msize) == FS_OK, "remount");

        /* check a random file, and occasionally all of them */
        for (int i = 0; i < MAXF; i++) {
            if (rand() % 8 && i != f) continue;
            char *got = load(fnames[i], NULL);
            CHECK((!got && !m.data[i]) ||
                (got && m.data[i] && !strcmp(got, m.data[i])),
                "random op %d (%c %s -> %d): %s differs: got %zd, want %zd",
                n, s.op, fnames[f], res, fnames[i],
                got ? (ssize_t)strlen(got) : -1,
                m.data[i] ? (ssize_t)strlen(m.data[i]) : -1);
            free(got);
        }
    }
    CHECK(!fs_degraded(), "degraded after random ops");
    CHECK(capacity_after_delete_all() == cap, "space lost in random test");
    model_free(&m);
    printf("  random %uB: %d operations%s\n", size, ops,
        with_cuts ? " with power cuts" : "");
}

/* ---- wear ---------------------------------------------------------------- */

static void wear_report(const char *what, uint32_t size, long ops,
                        double ops_per_day) {
    uint32_t max = 0, maxa = 0;
    double sum = 0;
    for (uint32_t a = 0; a < size; a++) {
        sum += wcount[a];
        if (wcount[a] > max) {
            max = wcount[a];
            maxa = a;
        }
    }
    double per_op = (double)max / ops;
    double days = 1e6 / (per_op * ops_per_day);
    printf("  wear %-34s max %u writes at %u (mean %.1f) -> "
        "1M-write EEPROM lasts %.0f years\n",
        what, max, maxa, sum / size, days / 365);
    CHECK(wcount[0] <= 1, "header written %u times", wcount[0]);
}

static void test_wear(void) {
    char line[32];
    long ops = 200000;

    /* logging one line per second to a 2KB EEPROM, restarting when full */
    media_new(2048, 0xFF);
    fs_format_default(2048);
    memset(wcount, 0, 2048 * sizeof(uint32_t));
    for (long i = 0; i < ops; i++) {
        snprintf(line, sizeof(line), "%ld,%ld\n", i % 100000, i % 977);
        if (append("LOG.DAT", line) == FS_ERR_FULL) fs_delete("LOG.DAT");
    }
    wear_report("log line every second (2KB)", 2048, ops, 86400);

    /* rewriting a settings file every minute */
    media_new(2048, 0xFF);
    fs_format_default(2048);
    save("PROG.BAS", "10 PRINT 1\n20 GOTO 10\n");
    memset(wcount, 0, 2048 * sizeof(uint32_t));
    for (long i = 0; i < ops; i++) {
        snprintf(line, sizeof(line), "%ld\n", i);
        save("CFG.DAT", line);
    }
    wear_report("settings rewrite every minute (2KB)", 2048, ops, 1440);

    /* both, on 8KB */
    media_new(8192, 0xFF);
    fs_format_default(8192);
    memset(wcount, 0, 8192 * sizeof(uint32_t));
    for (long i = 0; i < ops; i++) {
        snprintf(line, sizeof(line), "%ld,%ld\n", i % 100000, i % 977);
        if (append("LOG.DAT", line) == FS_ERR_FULL) fs_delete("LOG.DAT");
        if (i % 60 == 0) save("CFG.DAT", line);
    }
    wear_report("log every second + settings (8KB)", 8192, ops, 86400);
}

/* ---- corruption ---------------------------------------------------------- */

static void on_alarm(int sig) {
    (void)sig;
    printf("FAIL: corruption test hung\n");
    _exit(1);
}

static void test_corrupt(int rounds) {
    char text[600];
    signal(SIGALRM, on_alarm);
    for (int n = 0; n < rounds; n++) {
        uint32_t size = (n & 1) ? 2048 : 8192;
        media_new(size, 0xFF);
        fs_format_default(size);
        for (int i = 0; i < 6; i++) {
            rand_text(text, rand() % 500);
            save(fnames[i], text);
            append("LOG.DAT", "1,2,3\n");
        }
        int flips = 1 + rand() % 20;
        for (int i = 0; i < flips; i++) mem[rand() % size] = rand();

        alarm(5);
        if (fs_mount(msize) == FS_OK) {
            dir_n = 0;
            fs_dir(collect, NULL);
            for (int i = 0; i < MAXF; i++) {
                char *s = load(fnames[i], NULL);
                free(s);
            }
            append("LOG.DAT", "after\n");
            rand_text(text, 300);
            save("NEW.DAT", text);
            fs_mount(msize);
        }
        alarm(0);
    }
    printf("  corrupt: %d damaged media survived\n", rounds);
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    int quick = argc > 1 && !strcmp(argv[1], "quick");
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(12345);

#ifdef FS_NOR
    printf("filesystem tests (NOR flash)\n");
    media_new(65536, 0xFF);
    fs_format_default(65536);
    test_names();
    test_basic(65536);
    printf("  unit tests done\n");
    test_powercut(65536, FS_NOR_SHIFT, 203);
    test_random(65536, quick ? 2000 : 20000, 0);
    test_random(131072, quick ? 2000 : 20000, 1);
    if (nor_violations) printf("  %d writes tried to set bits\n", nor_violations);
    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("all NOR filesystem tests passed\n");
    return 0;
#endif
    printf("filesystem tests\n");
    test_sizes();
    test_names();
    test_dir_full();
    printf("  unit tests done\n");

    test_powercut(2048, 5, 8);
    test_powercut(8192, 6, 16);

    test_random(1024, quick ? 2000 : 20000, 0);
    test_random(2048, quick ? 2000 : 20000, 1);
    test_random(8192, quick ? 2000 : 20000, 1);
    test_random(262144, quick ? 500 : 5000, 1);
    test_random(4u << 20, quick ? 200 : 2000, 1);

    test_wear();
    test_corrupt(quick ? 200 : 2000);

    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("all filesystem tests passed\n");
    return 0;
}
